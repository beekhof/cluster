#include "gfs_daemon.h"
#include "config.h"
#include <libcman.h>

static cman_handle_t	ch;
static cman_handle_t	ch_admin;
static cman_cluster_t	cluster;
static cman_node_t      old_nodes[MAX_NODES];
static int              old_node_count;
static cman_node_t      cman_nodes[MAX_NODES];
static int              cman_node_count;

void kick_node_from_cluster(int nodeid)
{
	if (!nodeid) {
		log_error("telling cman to shut down cluster locally");
		cman_shutdown(ch_admin, CMAN_SHUTDOWN_ANYWAY);
	} else {
		log_error("telling cman to remove nodeid %d from cluster",
			  nodeid);
		cman_kill_node(ch_admin, nodeid);
	}
}

static int is_member(cman_node_t *node_list, int count, int nodeid)
{
	int i;

	for (i = 0; i < count; i++) {
		if (node_list[i].cn_nodeid == nodeid)
			return node_list[i].cn_member;
	}
	return 0;
}

static int is_old_member(int nodeid)
{
	return is_member(old_nodes, old_node_count, nodeid);
}

static int is_cluster_member(int nodeid)
{
	return is_member(cman_nodes, cman_node_count, nodeid);
}

static void statechange(void)
{
	int i, rv;

	old_node_count = cman_node_count;
	memcpy(&old_nodes, &cman_nodes, sizeof(old_nodes));

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_debug("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	/* Never allow node ID 0 to be considered a member #315711 */
	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_nodeid == 0) {
			cman_nodes[i].cn_member = 0;
			break;
		}
	}

	for (i = 0; i < old_node_count; i++) {
		if (old_nodes[i].cn_member &&
		    !is_cluster_member(old_nodes[i].cn_nodeid)) {

			log_debug("cluster node %d removed",
				  old_nodes[i].cn_nodeid);

			node_history_cluster_remove(old_nodes[i].cn_nodeid);
		}
	}

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_member &&
		    !is_old_member(cman_nodes[i].cn_nodeid)) {

			log_debug("cluster node %d added",
				  cman_nodes[i].cn_nodeid);

			node_history_cluster_add(cman_nodes[i].cn_nodeid);
		}
	}
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	switch (reason) {
	case CMAN_REASON_TRY_SHUTDOWN:
		if (list_empty(&mountgroups))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
		break;
	case CMAN_REASON_STATECHANGE:
		statechange();
		break;
	case CMAN_REASON_CONFIG_UPDATE:
		setup_logging();
		setup_ccs();
		break;
	}
}

void process_cluster(int ci)
{
	int rv;

	rv = cman_dispatch(ch, CMAN_DISPATCH_ALL);
	if (rv == -1 && errno == EHOSTDOWN)
		cluster_dead(0);
}

int setup_cluster(void)
{
	cman_node_t node;
	int rv, fd;
	int init = 0, active = 0;

 retry_init:
	ch_admin = cman_admin_init(NULL);
	if (!ch_admin) {
		if (init++ < 2) {
			sleep(1);
			goto retry_init;
		}
		log_error("cman_admin_init error %d", errno);
		return -ENOTCONN;
	}

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %d", errno);
		cman_finish(ch_admin);
		return -ENOTCONN;
	}

 retry_active:
	rv = cman_is_active(ch);
	if (!rv) {
		if (active++ < 2) {
			sleep(1);
			goto retry_active;
		}
		log_error("cman_is_active error %d", errno);
		cman_finish(ch);
		cman_finish(ch_admin);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_error("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		cman_finish(ch_admin);
		return rv;
	}

	fd = cman_get_fd(ch);

	/* FIXME: wait here for us to be a member of the cluster */
	memset(&cluster, 0, sizeof(cluster));
	rv = cman_get_cluster(ch, &cluster);
	if (rv < 0) {
		log_error("cman_get_cluster error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		cman_finish(ch_admin);
		return rv;
	}
	clustername = cluster.ci_name;

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		cman_finish(ch_admin);
		fd = rv;
		goto out;
	}
	our_nodeid = node.cn_nodeid;

 	old_node_count = 0;
	memset(&old_nodes, 0, sizeof(old_nodes));
	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
 out:
	return fd;
}

void close_cluster(void)
{
	cman_finish(ch);
	cman_finish(ch_admin);
}

/* Force re-read of cman nodes */
void update_cluster(void)
{
	statechange();
}

