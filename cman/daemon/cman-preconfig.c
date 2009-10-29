#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netdb.h>
#include <fcntl.h>
#define SYSLOG_NAMES
#include <sys/syslog.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

/* corosync headers */
#include <corosync/engine/logsys.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>

#include "cman.h"
#define OBJDB_API struct objdb_iface_ver0
#include "cnxman-socket.h"
#include "nodelist.h"

#define MAX_PATH_LEN PATH_MAX

static unsigned int debug;
static int cmanpre_readconfig(struct objdb_iface_ver0 *objdb, const char **error_string);
static int cmanpre_reloadconfig(struct objdb_iface_ver0 *objdb, int flush, const char **error_string);

static char *nodename_env;
static int expected_votes;
static int votes;
static int num_interfaces;
static int startup_pipe;
static unsigned int cluster_id;
static char nodename[MAX_CLUSTER_MEMBER_NAME_LEN];
static int nodeid;
static int two_node;
static unsigned int disable_openais;
static unsigned int portnum;
static int num_nodenames;
static char *key_filename=NULL;
static char *mcast_name;
static char *cluster_name;
static char error_reason[1024] = { '\0' };
static hdb_handle_t cluster_parent_handle;

/*
 * Exports the interface for the service
 */
static struct config_iface_ver0 cmanpreconfig_iface_ver0 = {
	.config_readconfig        = cmanpre_readconfig,
	.config_reloadconfig      = cmanpre_reloadconfig
};

static struct lcr_iface ifaces_ver0[2] = {
	{
		.name		       	= "cmanpreconfig",
		.version	       	= 0,
		.versions_replace      	= 0,
		.versions_replace_count	= 0,
		.dependencies	       	= 0,
		.dependency_count      	= 0,
		.constructor	       	= NULL,
		.destructor	       	= NULL,
		.interfaces	       	= NULL,
	}
};

static struct lcr_comp cmanpre_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= ifaces_ver0,
};



__attribute__ ((constructor)) static void cmanpre_comp_register(void) {
	lcr_interfaces_set(&ifaces_ver0[0], &cmanpreconfig_iface_ver0);
	lcr_component_register(&cmanpre_comp_ver0);
}

static char *facility_name_get (unsigned int facility)
{
	unsigned int i;

	for (i = 0; facilitynames[i].c_name != NULL; i++) {
		if (facility == facilitynames[i].c_val) {
			return (facilitynames[i].c_name);
		}
	}
	return (NULL);
}

static char *priority_name_get (unsigned int priority)
{
	unsigned int i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (priority == prioritynames[i].c_val) {
			return (prioritynames[i].c_name);
		}
	}
	return (NULL);
}


#define LOCALHOST_IPV4 "127.0.0.1"
#define LOCALHOST_IPV6 "::1"

/* Compare two addresses */
static int ipaddr_equal(struct sockaddr_storage *addr1, struct sockaddr_storage *addr2)
{
	int addrlen = 0;

	if (addr1->ss_family != addr2->ss_family)
		return 0;

	if (addr1->ss_family == AF_INET) {
		addrlen = sizeof(struct sockaddr_in);
	}
	if (addr1->ss_family == AF_INET6) {
		addrlen = sizeof(struct sockaddr_in6);
	}
	assert(addrlen);

	if (memcmp(addr1, addr2, addrlen) == 0)
		return 1;
	else
		return 0;

}

/* Build a localhost ip_address */
static int get_localhost(int family, struct sockaddr_storage *localhost)
{
	const char *addr_text;
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	int ret;

	if (family == AF_INET) {
		addr_text = LOCALHOST_IPV4;
	} else {
		addr_text = LOCALHOST_IPV6;
	}

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	ahints.ai_family = family;

	/* Lookup the nodename address */
	ret = getaddrinfo(addr_text, NULL, &ahints, &ainfo);
	if (ret)
		return -1;

	memset(localhost, 0, sizeof(struct sockaddr_storage));
	memcpy(localhost, ainfo->ai_addr, ainfo->ai_addrlen);

	freeaddrinfo(ainfo);
	return 0;
}

/* Return the address family of an IP[46] name */
static int address_family(char *addr, struct sockaddr_storage *ssaddr, int family_hint)
{
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	int family;
	int ret;

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	ahints.ai_family = family_hint;

	/* Lookup the nodename address */
	ret = getaddrinfo(addr, NULL, &ahints, &ainfo);
	if (ret)
		return -1;

	memset(ssaddr, 0, sizeof(struct sockaddr_storage));
	memcpy(ssaddr, ainfo->ai_addr, ainfo->ai_addrlen);
	family = ainfo->ai_family;

	freeaddrinfo(ainfo);
	return family;
}


/* Find the "CMAN" logger_subsys object. Or create one if it does not
   exist
*/
static hdb_handle_t find_cman_logger(struct objdb_iface_ver0 *objdb, hdb_handle_t object_handle)
{
	hdb_handle_t subsys_handle;
	hdb_handle_t find_handle;
	char *str;

	objdb->object_find_create(object_handle, "logger_subsys", strlen("logger_subsys"), &find_handle);
	while (!objdb->object_find_next(find_handle, &subsys_handle)) {
		if (!objdb_get_string(objdb, subsys_handle, "subsys", &str)) {
			if (strncmp(str, CMAN_NAME, 4) == 0) {
				objdb->object_find_destroy(find_handle);
				return subsys_handle;
			}
		}
	}
	objdb->object_find_destroy(find_handle);

	return -1;

}


static int add_ifaddr(struct objdb_iface_ver0 *objdb, char *mcast, char *ifaddr, int port, int broadcast)
{
	hdb_handle_t totem_object_handle;
	hdb_handle_t find_handle;
	hdb_handle_t interface_object_handle;
	struct sockaddr_storage if_addr, localhost, mcast_addr;
	char tmp[132];
	int ret = 0;

	/* Check the families match */
	if (address_family(mcast, &mcast_addr, 0) !=
	    address_family(ifaddr, &if_addr, mcast_addr.ss_family)) {
		sprintf(error_reason, "Node address family does not match multicast address family");
		return -1;
	}

	/* Check it's not bound to localhost, sigh */
	get_localhost(if_addr.ss_family, &localhost);
	if (ipaddr_equal(&localhost, &if_addr)) {
		sprintf(error_reason, "Node name resolves to localhost, please check /etc/hosts and assign this node a network IP address");
		return -1;
	}

	objdb->object_find_create(OBJECT_PARENT_HANDLE, "totem", strlen("totem"), &find_handle);
	if (objdb->object_find_next(find_handle, &totem_object_handle)) {

		objdb->object_create(OBJECT_PARENT_HANDLE, &totem_object_handle,
				     "totem", strlen("totem"));
        }
	objdb->object_find_destroy(find_handle);

	if (objdb->object_create(totem_object_handle, &interface_object_handle,
				 "interface", strlen("interface")) == 0) {
		struct sockaddr_in *in = (struct sockaddr_in *)&if_addr;
		struct sockaddr_in6 *in6= (struct sockaddr_in6 *)&if_addr;
		void *addrptr;

		sprintf(tmp, "%d", num_interfaces);
		objdb->object_key_create(interface_object_handle, "ringnumber", strlen("ringnumber"),
					 tmp, strlen(tmp)+1);

		if (if_addr.ss_family == AF_INET)
			addrptr = &in->sin_addr;
		else
			addrptr = &in6->sin6_addr;
		inet_ntop(if_addr.ss_family, addrptr, tmp, sizeof(tmp));
		objdb->object_key_create(interface_object_handle, "bindnetaddr", strlen("bindnetaddr"),
					 tmp, strlen(tmp)+1);

		if (broadcast)
			objdb->object_key_create(interface_object_handle, "broadcast", strlen("broadcast"),
						 "yes", strlen("yes")+1);
		else
			objdb->object_key_create(interface_object_handle, "mcastaddr", strlen("mcastaddr"),
						 mcast, strlen(mcast)+1);

		sprintf(tmp, "%d", port);
		objdb->object_key_create(interface_object_handle, "mcastport", strlen("mcastport"),
					 tmp, strlen(tmp)+1);

		num_interfaces++;
	}
	return ret;
}

static uint16_t generate_cluster_id(char *name)
{
	int i;
	int value = 0;

	for (i=0; i<strlen(name); i++) {
		value <<= 1;
		value += name[i];
	}
	sprintf(error_reason, "Generated cluster id for '%s' is %d\n", name, value & 0xFFFF);
	return value & 0xFFFF;
}

static char *default_mcast(char *node, uint16_t clusterid)
{
        struct addrinfo *ainfo;
        struct addrinfo ahints;
	int ret;
	int family;
	static char addr[132];

        memset(&ahints, 0, sizeof(ahints));

        /* Lookup the the nodename address and use it's IP type to
	   default a multicast address */
        ret = getaddrinfo(node, NULL, &ahints, &ainfo);
	if (ret) {
		sprintf(error_reason, "Can't determine address family of nodename %s\n", node);
		write_cman_pipe("Can't determine address family of nodename");
		return NULL;
	}

	family = ainfo->ai_family;
	freeaddrinfo(ainfo);

	if (family == AF_INET) {
		snprintf(addr, sizeof(addr), "239.192.%d.%d", clusterid >> 8, clusterid % 0xFF);
		return addr;
	}
	if (family == AF_INET6) {
		snprintf(addr, sizeof(addr), "ff15::%x", clusterid);
		return addr;
	}

	return NULL;
}

static int verify_nodename(struct objdb_iface_ver0 *objdb, char *node)
{
	char nodename2[MAX_CLUSTER_MEMBER_NAME_LEN+1];
	char nodename3[MAX_CLUSTER_MEMBER_NAME_LEN+1];
	char *str, *dot = NULL;
	struct ifaddrs *ifa, *ifa_list;
	struct sockaddr *sa;
	hdb_handle_t nodes_handle;
	hdb_handle_t find_handle = 0;
	int error;

	/* nodename is either from commandline or from uname */
	if (nodelist_byname(objdb, cluster_parent_handle, node))
		return 0;

	/* If nodename was from uname, try a domain-less version of it */
	strcpy(nodename2, node);
	dot = strchr(nodename2, '.');
	if (dot) {
		*dot = '\0';

		if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
			strcpy(node, nodename2);
			return 0;
		}
	}

	/* If nodename (from uname) is domain-less, try to match against
	   cluster.conf names which may have domainname specified */
	nodes_handle = nodeslist_init(objdb, cluster_parent_handle, &find_handle);
	do {
		int len;

		if (objdb_get_string(objdb, nodes_handle, "name", &str)) {
			sprintf(error_reason, "Cannot get node name");
			nodes_handle = nodeslist_next(objdb, find_handle);
			continue;
		}

		strcpy(nodename3, str);
		dot = strchr(nodename3, '.');
		if (dot)
			len = dot-nodename3;
		else
			len = strlen(nodename3);

		if (strlen(nodename2) == len &&
		    !strncmp(nodename2, nodename3, len)) {
			strcpy(node, str);
			return 0;
		}
		nodes_handle = nodeslist_next(objdb, find_handle);
	} while (nodes_handle);
	objdb->object_find_destroy(find_handle);


	/* The cluster.conf names may not be related to uname at all,
	   they may match a hostname on some network interface.
	   NOTE: This is IPv4 only */
	error = getifaddrs(&ifa_list);
	if (error)
		return -1;

	for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
		socklen_t salen = 0;

		/* Restore this */
		strcpy(nodename2, node);
		sa = ifa->ifa_addr;
		if (!sa)
			continue;
		if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
			continue;

		if (sa->sa_family == AF_INET)
			salen = sizeof(struct sockaddr_in);
		if (sa->sa_family == AF_INET6)
			salen = sizeof(struct sockaddr_in6);

		error = getnameinfo(sa, salen, nodename2,
				    sizeof(nodename2), NULL, 0, 0);
		if (!error) {

			if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
				strcpy(node, nodename2);
				goto out;
			}

			/* Truncate this name and try again */
			dot = strchr(nodename2, '.');
			if (dot) {
				*dot = '\0';

				if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
					strcpy(node, nodename2);
					goto out;
				}
			}
		}

		/* See if it's the IP address that's in cluster.conf */
		error = getnameinfo(sa, sizeof(*sa), nodename2,
				    sizeof(nodename2), NULL, 0, NI_NUMERICHOST);
		if (error)
			continue;

		if (nodelist_byname(objdb, cluster_parent_handle, nodename2)) {
			strcpy(node, nodename2);
			goto out;
		}
	}

	error = -1;
 out:
	freeifaddrs(ifa_list);
	return error;
}

/* Get any environment variable overrides */
static int get_env_overrides(void)
{
	if (getenv("CMAN_CLUSTER_NAME")) {
		cluster_name = strdup(getenv("CMAN_CLUSTER_NAME"));
	}

	nodename_env = getenv("CMAN_NODENAME");

	expected_votes = 0;
	if (getenv("CMAN_EXPECTEDVOTES")) {
		expected_votes = atoi(getenv("CMAN_EXPECTEDVOTES"));
		if (expected_votes < 1) {
			expected_votes = 0;
		}
	}

	/* optional port */
	if (getenv("CMAN_IP_PORT")) {
		portnum = atoi(getenv("CMAN_IP_PORT"));
	}

	/* optional security key filename */
	if (getenv("CMAN_KEYFILE")) {
		key_filename = strdup(getenv("CMAN_KEYFILE"));
		if (key_filename == NULL) {
			write_cman_pipe("Cannot allocate memory for key filename");
			return -1;
		}
	}

	/* find our own number of votes */
	if (getenv("CMAN_VOTES")) {
		votes = atoi(getenv("CMAN_VOTES"));
	}

	/* nodeid */
	if (getenv("CMAN_NODEID")) {
		nodeid = atoi(getenv("CMAN_NODEID"));
	}

	if (getenv("CMAN_MCAST_ADDR")) {
		mcast_name = getenv("CMAN_MCAST_ADDR");
	}

	if (getenv("CMAN_2NODE")) {
		two_node = 1;
		expected_votes = 1;
		votes = 1;
	}
	if (getenv("CMAN_DEBUG")) {
		debug = atoi(getenv("CMAN_DEBUG"));
		if (debug > 0)
			debug = 1;
	}

	return 0;
}


static int get_nodename(struct objdb_iface_ver0 *objdb)
{
	char *nodeid_str = NULL;
	hdb_handle_t object_handle;
	hdb_handle_t find_handle;
	hdb_handle_t node_object_handle;
	hdb_handle_t alt_object;
	int broadcast = 0;
	char *str;
	int error;

	if (!getenv("CMAN_NOCONFIG")) {
		/* our nodename */
		if (nodename_env != NULL) {
			if (strlen(nodename_env) >= sizeof(nodename)) {
				sprintf(error_reason, "Overridden node name %s is too long", nodename);
				write_cman_pipe("Overridden node name is too long");
				error = -1;
				goto out;
			}

			strcpy(nodename, nodename_env);

			if (!(node_object_handle = nodelist_byname(objdb, cluster_parent_handle, nodename))) {
				sprintf(error_reason, "Overridden node name %s is not in CCS", nodename);
				write_cman_pipe("Overridden node name is not in CCS");
				error = -1;
				goto out;
			}

		} else {
			struct utsname utsname;

			error = uname(&utsname);
			if (error) {
				sprintf(error_reason, "cannot get node name, uname failed");
				write_cman_pipe("Can't determine local node name, uname failed");
				error = -1;
				goto out;
			}

			if (strlen(utsname.nodename) >= sizeof(nodename)) {
				sprintf(error_reason, "node name from uname is too long");
				write_cman_pipe("local node name is too long");
				error = -1;
				goto out;
			}

			strcpy(nodename, utsname.nodename);
		}
		if (verify_nodename(objdb, nodename)) {
			write_cman_pipe("Cannot find node name in cluster.conf");
			return -1;
		}

	}

	/* Add <cman> bits to pass down to the main module*/
	if ( (node_object_handle = nodelist_byname(objdb, cluster_parent_handle, nodename))) {
		if (objdb_get_string(objdb, node_object_handle, "nodeid", &nodeid_str)) {
			sprintf(error_reason, "This node has no nodeid in cluster.conf");
			write_cman_pipe("This node has no nodeid in cluster.conf");
			return -1;
		}
	}

	objdb->object_find_create(cluster_parent_handle, "cman", strlen("cman"), &find_handle);

	if (objdb->object_find_next(find_handle, &object_handle) == 0) {

		hdb_handle_t mcast_handle;
		hdb_handle_t find_handle2;

		if (!mcast_name) {

			objdb->object_find_create(object_handle, "multicast", strlen("multicast"), &find_handle2);
			if (objdb->object_find_next(find_handle2, &mcast_handle) == 0) {

				objdb_get_string(objdb, mcast_handle, "addr", &mcast_name);
			}
			objdb->object_find_destroy(find_handle2);
		}

		if (!mcast_name) {
			mcast_name = default_mcast(nodename, cluster_id);

		}
		if (!mcast_name)
			return -1;

		/* See if the user wants our default set of openais services (default=yes) */
		objdb_get_int(objdb, object_handle, "disable_openais", &disable_openais, 0);

		objdb->object_key_create(object_handle, "nodename", strlen("nodename"),
					    nodename, strlen(nodename)+1);
	}
	objdb->object_find_destroy(find_handle);

	nodeid = atoi(nodeid_str);
	error = 0;

	/* optional port */
	if (!portnum) {
		objdb_get_int(objdb, object_handle, "port", &portnum, DEFAULT_PORT);
	}

	/* Check for broadcast */
	if (!objdb_get_string(objdb, object_handle, "broadcast", &str)) {
		if (strcmp(str, "yes") == 0) {
			mcast_name = strdup("255.255.255.255");
			if (!mcast_name)
				return -1;
			broadcast = 1;
		}
		free(str);
	}

	if (add_ifaddr(objdb, mcast_name, nodename, portnum, broadcast)) {
		write_cman_pipe(error_reason);
		return -1;
	}

	/* Get all alternative node names */
	num_nodenames = 1;
	objdb->object_find_create(node_object_handle,"altname", strlen("altname"), &find_handle);
	while (objdb->object_find_next(find_handle, &alt_object) == 0) {
		unsigned int port;
		char *node;
		char *mcast;

		if (objdb_get_string(objdb, alt_object, "name", &node)) {
			continue;
		}

		objdb_get_int(objdb, alt_object, "port", &port, portnum);

		if (objdb_get_string(objdb, alt_object, "mcast", &mcast)) {
			mcast = mcast_name;
		}

		if (add_ifaddr(objdb, mcast, node, portnum, broadcast)) {
			write_cman_pipe(error_reason);
			return -1;
		}

		num_nodenames++;
	}
	objdb->object_find_destroy(find_handle);

out:
	return error;
}

/* These are basically cman overrides to the totem config bits */
static void add_cman_overrides(struct objdb_iface_ver0 *objdb)
{
	char *logstr;
	char *logfacility;
	char *loglevel;
	hdb_handle_t object_handle;
	hdb_handle_t find_handle;
	char tmp[256];

	/* "totem" key already exists, because we have added the interfaces by now */
	objdb->object_find_create(OBJECT_PARENT_HANDLE,"totem", strlen("totem"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle) == 0)
	{
		char *value;

		objdb->object_key_create(object_handle, "version", strlen("version"),
					 "2", 2);

		sprintf(tmp, "%d", nodeid);
		objdb->object_key_create(object_handle, "nodeid", strlen("nodeid"),
					 tmp, strlen(tmp)+1);

		objdb->object_key_create(object_handle, "vsftype", strlen("vsftype"),
					 "none", strlen("none")+1);

		/* Set the token timeout is 10 seconds, but don't overrride anything that
		   might be in cluster.conf */
		if (objdb_get_string(objdb, object_handle, "token", &value)) {
			snprintf(tmp, sizeof(tmp), "%d", DEFAULT_TOKEN_TIMEOUT);
			objdb->object_key_create(object_handle, "token", strlen("token"),
						 tmp, strlen(tmp)+1);
		}
		if (objdb_get_string(objdb, object_handle, "token_retransmits_before_loss_const", &value)) {
			objdb->object_key_create(object_handle, "token_retransmits_before_loss_const",
						 strlen("token_retransmits_before_loss_const"),
						 "20", strlen("20")+1);
		}

		/* Extend consensus & join timeouts per bz#214290 */
		if (objdb_get_string(objdb, object_handle, "join", &value)) {
			objdb->object_key_create(object_handle, "join", strlen("join"),
						 "60", strlen("60")+1);
		}
		if (objdb_get_string(objdb, object_handle, "consensus", &value)) {
			objdb->object_key_create(object_handle, "consensus", strlen("consensus"),
						 "4800", strlen("4800")+1);
		}

		/* Set RRP mode appropriately */
		if (objdb_get_string(objdb, object_handle, "rrp_mode", &value)) {
			if (num_interfaces > 1) {
				objdb->object_key_create(object_handle, "rrp_mode", strlen("rrp_mode"),
							 "active", strlen("active")+1);
			}
			else {
				objdb->object_key_create(object_handle, "rrp_mode", strlen("rrp_mode"),
							 "none", strlen("none")+1);
			}
		}

		if (objdb_get_string(objdb, object_handle, "secauth", &value)) {
			sprintf(tmp, "%d", 1);
			objdb->object_key_create(object_handle, "secauth", strlen("secauth"),
						 tmp, strlen(tmp)+1);
		}

		/* optional security key filename */
		if (!key_filename) {
			objdb_get_string(objdb, object_handle, "keyfile", &key_filename);
		}
		else {
			objdb->object_key_create(object_handle, "keyfile", strlen("keyfile"),
						 key_filename, strlen(key_filename)+1);
		}
		if (!key_filename) {
			/* Use the cluster name as key,
			 * This isn't a good isolation strategy but it does make sure that
			 * clusters on the same port/multicast by mistake don't actually interfere
			 * and that we have some form of encryption going.
			 */

			int keylen;
			memset(tmp, 0, sizeof(tmp));

			strcpy(tmp, cluster_name);

			/* Key length must be a multiple of 4 */
			keylen = (strlen(cluster_name)+4) & 0xFC;
			objdb->object_key_create(object_handle, "key", strlen("key"),
						 tmp, keylen);
		}
	}
	objdb->object_find_destroy(find_handle);

	/* Make sure mainconfig doesn't stomp on our logging options */
	objdb->object_find_create(OBJECT_PARENT_HANDLE, "logging", strlen("logging"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle)) {

                objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
					    "logging", strlen("logging"));
        }
	objdb->object_find_destroy(find_handle);

	logfacility = facility_name_get(SYSLOGFACILITY);
	loglevel = priority_name_get(SYSLOGLEVEL);

	/* enable timestamps on logging */
	if (objdb_get_string(objdb, object_handle, "timestamp", &logstr)) {
		objdb->object_key_create(object_handle, "timestamp", strlen("timestamp"),
					    "on", strlen("on")+1);
	}

	/* configure logfile */
	if (objdb_get_string(objdb, object_handle, "to_logfile", &logstr)) {
		objdb->object_key_create(object_handle, "to_logfile", strlen("to_logfile"),
					    "yes", strlen("yes")+1);
	}

	if (objdb_get_string(objdb, object_handle, "logfile", &logstr)) {
		objdb->object_key_create(object_handle, "logfile", strlen("logfile"),
					    LOGDIR "/corosync.log", strlen(LOGDIR "/corosync.log")+1);
	}

	if (objdb_get_string(objdb, object_handle, "logfile_priority", &logstr)) {
		objdb->object_key_create(object_handle, "logfile_priority", strlen("logfile_priority"),
					    loglevel, strlen(loglevel)+1);
	}

	/* syslog */
	if (objdb_get_string(objdb, object_handle, "to_syslog", &logstr)) {
		objdb->object_key_create(object_handle, "to_syslog", strlen("to_syslog"),
					    "yes", strlen("yes")+1);
	}

	if (objdb_get_string(objdb, object_handle, "syslog_facility", &logstr)) {
		objdb->object_key_create(object_handle, "syslog_facility", strlen("syslog_facility"),
					    logfacility, strlen(logfacility)+1);
	}

	if (objdb_get_string(objdb, object_handle, "syslog_priority", &logstr)) {
		objdb->object_key_create(object_handle, "syslog_priority", strlen("syslog_priority"),
					    loglevel, strlen(loglevel)+1);
	}

	if (!debug) {
		hdb_handle_t logger_object_handle;

		if (!objdb_get_string(objdb, object_handle, "debug", &logstr)) {
			if (!strncmp(logstr, "on", 2)) {
				debug=1;
			}
		}

		logger_object_handle = find_cman_logger(objdb, object_handle);
		if (logger_object_handle > -1) {
			if (!objdb_get_string(objdb, logger_object_handle, "debug", &logstr)) {
				if (!strncmp(logstr, "on", 2)) {
					debug=1;
				}
				if (!strncmp(logstr, "off", 3)) {
					debug=0;
				}
			}
		}
	}

	if (debug) {
		objdb->object_key_create(object_handle, "to_stderr", strlen("to_stderr"),
					    "yes", strlen("yes")+1);
	}

	/* Make sure we allow connections from user/group "ais" */
	objdb->object_find_create(OBJECT_PARENT_HANDLE, "aisexec", strlen("aisexec"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle) != 0) {
		objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
					"aisexec", strlen("aisexec"));
	}
	objdb->object_find_destroy(find_handle);
	objdb->object_key_create(object_handle, "user", strlen("user"),
				    "ais", strlen("ais") + 1);
	objdb->object_key_create(object_handle, "group", strlen("group"),
				    "ais", strlen("ais") + 1);

	objdb->object_find_create(cluster_parent_handle, "cman", strlen("cman"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle) == 0)
	{
		char str[255];

		sprintf(str, "%d", cluster_id);

		objdb->object_key_create(object_handle, "cluster_id", strlen("cluster_id"),
					 str, strlen(str) + 1);

		if (two_node) {
			sprintf(str, "%d", 1);
			objdb->object_key_create(object_handle, "two_node", strlen("two_node"),
						 str, strlen(str) + 1);
		}
	}
	objdb->object_find_destroy(find_handle);

	/* Load the quorum service */
	objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
			     "service", strlen("service"));
	objdb->object_key_create(object_handle, "name", strlen("name"),
				 "corosync_quorum", strlen("corosync_quorum") + 1);
	objdb->object_key_create(object_handle, "ver", strlen("ver"),
				 "0", 2);

	/* Make sure we load our alter-ego - the main cman module */
	objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
			     "service", strlen("service"));
	objdb->object_key_create(object_handle, "name", strlen("name"),
				 "corosync_cman", strlen("corosync_cman") + 1);
	objdb->object_key_create(object_handle, "ver", strlen("ver"),
				 "0", 2);

	/* Define cman as the quorum provider for corosync */
	objdb->object_find_create(OBJECT_PARENT_HANDLE, "quorum", strlen("quorum"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle) != 0) {
		objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
					"quorum", strlen("quorum"));
	}
	objdb->object_find_destroy(find_handle);

	objdb->object_key_create(object_handle, "provider", strlen("provider"),
				 "quorum_cman", strlen("quorum_cman") + 1);
}

/* If ccs is not available then use some defaults */
static int set_noccs_defaults(struct objdb_iface_ver0 *objdb)
{
	char tmp[255];
	hdb_handle_t object_handle;
	hdb_handle_t find_handle;

	/* Enforce key */
	key_filename = strdup(NOCCS_KEY_FILENAME);
	if (!key_filename) {
		sprintf(error_reason, "cannot allocate memory for key file name");
		write_cman_pipe("cannot allocate memory for key file name");
		return -1;
	}

	if (!cluster_name)
		cluster_name = strdup(DEFAULT_CLUSTER_NAME);

	if (!cluster_name) {
		sprintf(error_reason, "cannot allocate memory for cluster_name");
		write_cman_pipe("cannot allocate memory for cluster_name");
		return -1;
	}

	if (!cluster_id)
		cluster_id = generate_cluster_id(cluster_name);

	if (!nodename_env) {
		int error;
		struct utsname utsname;

		error = uname(&utsname);
		if (error) {
			sprintf(error_reason, "cannot get node name, uname failed");
			write_cman_pipe("Can't determine local node name");
			return -1;
		}

		nodename_env = (char *)&utsname.nodename;
	}
	strcpy(nodename, nodename_env);
	num_nodenames = 1;

	if (!mcast_name) {
		mcast_name = default_mcast(nodename, cluster_id);
	}

	/* This will increase as nodes join the cluster */
	if (!expected_votes)
		expected_votes = 1;
	if (!votes)
		votes = 1;

	if (!portnum)
		portnum = DEFAULT_PORT;

	/* Invent a node ID */
	if (!nodeid) {
		struct addrinfo *ainfo;
		struct addrinfo ahints;
		int ret;

		memset(&ahints, 0, sizeof(ahints));
		ret = getaddrinfo(nodename, NULL, &ahints, &ainfo);
		if (ret) {
			sprintf(error_reason, "Can't determine address family of nodename %s\n", nodename);
			write_cman_pipe("Can't determine address family of nodename");
			return -1;
		}

		if (ainfo->ai_family == AF_INET) {
			struct sockaddr_in *addr = (struct sockaddr_in *)ainfo->ai_addr;
			memcpy(&nodeid, &addr->sin_addr, sizeof(int));
		}
		if (ainfo->ai_family == AF_INET6) {
			struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ainfo->ai_addr;
			memcpy(&nodeid, &addr->sin6_addr.s6_addr32[3], sizeof(int));
		}
		freeaddrinfo(ainfo);
	}

	/* Write a local <clusternode> entry to keep the rest of the code happy */
	objdb->object_create(cluster_parent_handle, &object_handle,
			     "clusternodes", strlen("clusternodes"));
	objdb->object_create(object_handle, &object_handle,
			     "clusternode", strlen("clusternode"));
	objdb->object_key_create(object_handle, "name", strlen("name"),
				 nodename, strlen(nodename)+1);

	sprintf(tmp, "%d", votes);
	objdb->object_key_create(object_handle, "votes", strlen("votes"),
				 tmp, strlen(tmp)+1);

	sprintf(tmp, "%d", nodeid);
	objdb->object_key_create(object_handle, "nodeid", strlen("nodeid"),
				 tmp, strlen(tmp)+1);

	/* Write the default cluster name & ID in here too */
	objdb->object_key_create(cluster_parent_handle, "name", strlen("name"),
				 cluster_name, strlen(cluster_name)+1);


	objdb->object_find_create(cluster_parent_handle, "cman", strlen("cman"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle) == 0) {

                objdb->object_create(cluster_parent_handle, &object_handle,
                                            "cman", strlen("cman"));
        }
	sprintf(tmp, "%d", cluster_id);
	objdb->object_key_create(object_handle, "cluster_id", strlen("cluster_id"),
				    tmp, strlen(tmp)+1);

	sprintf(tmp, "%d", expected_votes);
	objdb->object_key_create(object_handle, "expected_votes", strlen("expected_votes"),
				    tmp, strlen(tmp)+1);

	objdb->object_find_destroy(find_handle);
	return 0;
}

/* Move an object/key tree */
static int copy_config_tree(struct objdb_iface_ver0 *objdb, hdb_handle_t source_object, hdb_handle_t target_parent_object, int always_create)
{
	hdb_handle_t object_handle;
	hdb_handle_t new_object;
	hdb_handle_t find_handle;
	char object_name[1024];
	size_t object_name_len;
	void *key_name;
	size_t key_name_len;
	void *key_value;
	size_t key_value_len;
	int res;

	/* Create new parent object if necessary */
	objdb->object_name_get(source_object, object_name, &object_name_len);

	objdb->object_find_create(target_parent_object, object_name, strlen(object_name), &find_handle);
	if (always_create || objdb->object_find_next(find_handle, &new_object))
			objdb->object_create(target_parent_object, &new_object, object_name, object_name_len);
	objdb->object_find_destroy(find_handle);

	/* Copy the keys */
	objdb->object_key_iter_reset(new_object);

	while (!objdb->object_key_iter(source_object, &key_name, &key_name_len,
				       &key_value, &key_value_len)) {

		objdb->object_key_create(new_object, key_name, key_name_len,
					 key_value, key_value_len);
	}

	/* Create sub-objects */
	res = objdb->object_find_create(source_object, NULL, 0, &find_handle);
	if (res) {
		sprintf(error_reason, "error resetting object iterator for object %ud: %d\n", (unsigned int)source_object, res);
		return -1;
	}

	while ( (res = objdb->object_find_next(find_handle, &object_handle) == 0)) {

		/* Down we go ... */
		copy_config_tree(objdb, object_handle, new_object, 0);
	}
	objdb->object_find_destroy(find_handle);

	return 0;
}

/*
 * Copy trees from /cluster where they live in cluster.conf, into the root
 * of the config tree where corosync expects to find them.
 */
static int copy_tree_to_root(struct objdb_iface_ver0 *objdb, const char *name, int always_create)
{
	hdb_handle_t find_handle;
	hdb_handle_t object_handle;
	int res=0;

	objdb->object_find_create(cluster_parent_handle, name, strlen(name), &find_handle);
	while (objdb->object_find_next(find_handle, &object_handle) == 0) {
		res = copy_config_tree(objdb, object_handle, OBJECT_PARENT_HANDLE, always_create);
	}
	objdb->object_find_destroy(find_handle);

	return res;
}

static int get_cman_globals(struct objdb_iface_ver0 *objdb)
{
	hdb_handle_t object_handle;
	hdb_handle_t find_handle;

	objdb_get_string(objdb, cluster_parent_handle, "name", &cluster_name);

	/* Get the <cman> bits that override <totem> bits */
	objdb->object_find_create(cluster_parent_handle, "cman", strlen("cman"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle) == 0) {
		if (!portnum)
			objdb_get_int(objdb, object_handle, "port", &portnum, DEFAULT_PORT);

		if (!key_filename)
			objdb_get_string(objdb, object_handle, "keyfile", &key_filename);

		if (!cluster_id)
			objdb_get_int(objdb, object_handle, "cluster_id", &cluster_id, 0);

		if (!cluster_id)
			cluster_id = generate_cluster_id(cluster_name);
	}
	objdb->object_find_destroy(find_handle);
	return 0;
}

static int cmanpre_reloadconfig(struct objdb_iface_ver0 *objdb, int flush, const char **error_string)
{
	int ret = -1;
	hdb_handle_t object_handle;
	hdb_handle_t find_handle;
	hdb_handle_t cluster_parent_handle_new;

	/* don't reload if we've been told to run configless */
	if (getenv("CMAN_NOCONFIG")) {
		sprintf(error_reason, "Config not updated because we were run with cman_tool -X");
		ret = 0;
		goto err;
	}

	/* find both /cluster entries */
	objdb->object_find_create(OBJECT_PARENT_HANDLE, "cluster", strlen("cluster"), &find_handle);
	objdb->object_find_next(find_handle, &cluster_parent_handle);
	if (!cluster_parent_handle) {
		sprintf (error_reason, "%s", "Cannot find old /cluster/ key in configuration\n");
		goto err;
	}
	objdb->object_find_next(find_handle, &cluster_parent_handle_new);
	if (!cluster_parent_handle_new) {
		sprintf (error_reason, "%s", "Cannot find new /cluster/ key in configuration\n");
		goto err;
	}
	objdb->object_find_destroy(find_handle);

	/* destroy the old one */
	objdb->object_destroy(cluster_parent_handle);

	/* update the reference to the new config */
	cluster_parent_handle = cluster_parent_handle_new;

	/* destroy top level /logging */
	objdb->object_find_create(OBJECT_PARENT_HANDLE, "logging", strlen("logging"), &find_handle);
	ret = objdb->object_find_next(find_handle, &object_handle);
	objdb->object_find_destroy(find_handle);
	if (ret) {
		objdb->object_destroy(object_handle);
	}

	/* copy /cluster/logging to /logging */
	ret = copy_tree_to_root(objdb, "logging", 0);

	/* Note: we do NOT delete /totem as corosync stores other things in there that
	   it needs! */

	/* copy /cluster/totem to /totem */
	ret = copy_tree_to_root(objdb, "totem", 0);

	return 0;

err:
	*error_string = error_reason;
	return ret;
}


static hdb_handle_t find_or_create_object(struct objdb_iface_ver0 *objdb, const char *name, hdb_handle_t parent_handle)
{
	hdb_handle_t find_handle;
	hdb_handle_t ret_handle = 0;

	objdb->object_find_create(parent_handle, name, strlen(name), &find_handle);
        objdb->object_find_next(find_handle, &ret_handle);
	objdb->object_find_destroy(find_handle);

	if (!ret_handle) {
		objdb->object_create(parent_handle, &ret_handle, name, strlen(name));
	}

	return ret_handle;
}

static const char *groupd_compat="groupd_compat";
static const char *clvmd_interface="interface";
static const char *cman_disallowed="disallowed";
static const char *totem_crypto="crypto_accept";

/*
 * Flags to set:
 * - groupd:
 * - clvmd
 * - disallowed (on)
 * - rgmanager
 */
static void setup_old_compat(struct objdb_iface_ver0 *objdb, hdb_handle_t cluster_handle)
{
	hdb_handle_t groupd_handle;
	hdb_handle_t clvmd_handle;
	hdb_handle_t cman_handle;
	hdb_handle_t totem_handle;
	char *value;

	/* Set groupd to backwards compatibility mode */
	groupd_handle = find_or_create_object(objdb, "group", cluster_handle);
	if (objdb->object_key_get(groupd_handle, groupd_compat, strlen(groupd_compat),
				  (void *)&value, NULL) ||
	    !value) {
		objdb->object_key_create(groupd_handle, groupd_compat, strlen(groupd_compat),
					 "1", 2);
	}

	/* Make clvmd use cman */
	clvmd_handle = find_or_create_object(objdb, "clvmd", cluster_handle);
	if (objdb->object_key_get(clvmd_handle, clvmd_interface, strlen(clvmd_interface),
				  (void *)&value, NULL) ||
	    !value) {
		objdb->object_key_create(clvmd_handle, clvmd_interface, strlen(clvmd_interface),
					 "cman", 5);
	}

	/* Make cman use disallowed mode */
	cman_handle = find_or_create_object(objdb, "cman", cluster_handle);
	if (objdb->object_key_get(cman_handle, cman_disallowed, strlen(cman_disallowed),
				  (void *)&value, NULL) ||
	    !value) {
		objdb->object_key_create(cman_handle, cman_disallowed, strlen(cman_disallowed),
					 "1", 2);
	}

	/* Make totem use the old communications method */
	totem_handle = find_or_create_object(objdb, "totem", OBJECT_PARENT_HANDLE);
	if (objdb->object_key_get(totem_handle, totem_crypto, strlen(totem_crypto),
				  (void *)&value, NULL) ||
	    !value) {
		objdb->object_key_create(totem_handle, totem_crypto, strlen(totem_crypto),
					 "old", 4);
	}
}

static int cmanpre_readconfig(struct objdb_iface_ver0 *objdb, const char **error_string)
{
	int ret = 0;
	hdb_handle_t object_handle;
	hdb_handle_t find_handle;
	char *str;

	if (getenv("CMAN_PIPE"))
                startup_pipe = atoi(getenv("CMAN_PIPE"));

	objdb->object_find_create(OBJECT_PARENT_HANDLE, "cluster", strlen("cluster"), &find_handle);
        objdb->object_find_next(find_handle, &cluster_parent_handle);
	objdb->object_find_destroy(find_handle);
	if (!cluster_parent_handle) {
                objdb->object_create(OBJECT_PARENT_HANDLE, &cluster_parent_handle,
				     "cluster", strlen("cluster"));
	}
	else {
		/* Move these to a place where corosync expects to find them */
		ret = copy_tree_to_root(objdb, "totem", 0);
		ret = copy_tree_to_root(objdb, "logging", 0);
		ret = copy_tree_to_root(objdb, "event", 0);
		ret = copy_tree_to_root(objdb, "amf", 0);
		ret = copy_tree_to_root(objdb, "aisexec", 0);
		ret = copy_tree_to_root(objdb, "service", 1);
	}

	objdb->object_find_create(cluster_parent_handle, "cman", strlen("cman"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle)) {
                objdb->object_create(cluster_parent_handle, &object_handle,
					"cman", strlen("cman"));
        }
	objdb->object_find_destroy(find_handle);

	/* Set up STABLE2/RHEL5 compatibility modes */
	objdb_get_string(objdb, object_handle, "upgrading", &str);
	if (str && (strcasecmp(str, "on")==0 || strcasecmp(str, "yes")==0)) {
		setup_old_compat(objdb, cluster_parent_handle);
	}

	/* This will create /libccs/@next_handle.
	 * next_handle will be atomically incremented by corosync to return ccs_handle down the pipe.
	 * We create it in cman-preconfig to avoid an "init" race in libccs.
	 */

	objdb->object_find_create(OBJECT_PARENT_HANDLE, "libccs", strlen("libccs"), &find_handle);
	if (objdb->object_find_next(find_handle, &object_handle)) {
		int next_handle = 0;

		objdb->object_create(OBJECT_PARENT_HANDLE, &object_handle,
					"libccs", strlen("libccs"));

		objdb->object_key_create(object_handle, "next_handle", strlen("next_handle"),
					 &next_handle, sizeof(int));
	}
	objdb->object_find_destroy(find_handle);

	get_env_overrides();
	if (getenv("CMAN_NOCONFIG"))
		ret = set_noccs_defaults(objdb);
	else
		ret = get_cman_globals(objdb);

	if (!ret) {
		ret = get_nodename(objdb);
		add_cman_overrides(objdb);
	}


	if (!ret) {
		sprintf (error_reason, "%s", "Successfully parsed cman config\n");
	}
	else {
		if (error_reason[0] == '\0')
			sprintf (error_reason, "%s", "Error parsing cman config\n");
	}
        *error_string = error_reason;


	/* nullify stderr, because cman_tool tells corosync not to.
	   This helps pass error messages back to the command-line, when
	   debug is enabled.
	*/
	if (!debug) {
		int tmpfd;
		tmpfd = open("/dev/null", O_RDWR);
		if (tmpfd > -1 && tmpfd != STDERR_FILENO) {
			dup2(tmpfd, STDERR_FILENO);
			close(tmpfd);
		}

	}
	return ret;
}

/* Write an error message down the CMAN startup pipe so
   that cman_tool can display it */
int write_cman_pipe(const char *message)
{
	if (startup_pipe)
		return write(startup_pipe, message, strlen(message)+1);

	return 0;
}
