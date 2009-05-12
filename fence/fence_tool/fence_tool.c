#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <libgen.h>

#include "ccs.h"
#include "copyright.cf"
#include "libcman.h"
#include "libfenced.h"

#define OP_JOIN  			1
#define OP_LEAVE 			2
#define OP_LIST				3
#define OP_DUMP				4

#define MAX_NODES			128

int all_nodeids[MAX_NODES];
int all_nodeids_count;
cman_handle_t ch;
cman_node_t cman_nodes[MAX_NODES];
int cman_nodes_count;
struct fenced_node nodes[MAX_NODES];
char *prog_name;
int operation;

#define DEFAULT_RETRY_CMAN 0 /* fail immediately if we can't connect to cman */
#define DEFAULT_DELAY_QUORUM 0
#define DEFAULT_DELAY_MEMBERS 0
#define DEFAULT_WAIT_JOINLEAVE 0

int opt_all_nodes = 0;
int opt_retry_cman = DEFAULT_RETRY_CMAN;
int opt_delay_quorum = DEFAULT_DELAY_QUORUM;
int opt_delay_members = DEFAULT_DELAY_MEMBERS;
int opt_wait_joinleave = DEFAULT_WAIT_JOINLEAVE;


#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt "\n", ##args); \
	exit(EXIT_FAILURE); \
} while (0)

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, (char *)buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0)
		return rv;

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

#define LOCKFILE_NAME           "/var/run/fenced.pid"

static void check_fenced_running(void)
{
	struct flock lock;
	int fd, rv;

	fd = open(LOCKFILE_NAME, O_RDONLY);
	if (fd < 0)
		die("fenced not running, no lockfile");

	lock.l_type = F_RDLCK;
	lock.l_start = 0;
	lock.l_whence= SEEK_SET;
	lock.l_len = 0;

	rv = fcntl(fd, F_GETLK, &lock);
	if (rv < 0)
		die("fenced not running, get lockfile");

	if (lock.l_type == F_UNLCK)
		die("fenced not running, unlocked lockfile");

	close(fd);
}

static int check_gfs(void)
{
	FILE *file;
	char line[PATH_MAX];
	char device[PATH_MAX];
	char path[PATH_MAX];
	char type[PATH_MAX];
	int count = 0;

	file = fopen("/proc/mounts", "r");
	if (!file)
		return 0;

	while (fgets(line, PATH_MAX, file)) {
		if (sscanf(line, "%s %s %s", device, path, type) != 3)
			continue;
		if (!strcmp(type, "gfs") || !strcmp(type, "gfs2")) {
			fprintf(stderr, "found %s file system mounted from %s on %s\n",
				type, device, path);
			count++;
		}
	}

	fclose(file);
	return count;
}

static int check_controlled_dir(const char *path)
{
	DIR *d;
	struct dirent *de;
	int count = 0;

	d = opendir(path);
	if (!d)
		return 0;

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

#if 0
		if (strstr(path, "fs/gfs") && ignore_nolock(path, de->d_name))
			continue;
#endif

		fprintf(stderr, "found dlm lockspace %s/%s\n", path, de->d_name);
		count++;
	}

	closedir(d);
	return count;
}

/* Copying fenced's check_uncontrolled_entries()/check_controlled_dir()
   in part here.
   We could also use check_controlled_dir to detect gfs file systems intead
   of looking at /proc/mounts... /proc/mounts gives us more info (mountpoint)
   to report about the offending fs */

static void check_controlled_systems(void)
{
	int count = 0;

	count += check_gfs();
	count += check_controlled_dir("/sys/kernel/dlm");

	if (count)
		die("cannot leave due to active systems");
}

static int we_are_in_fence_domain(void)
{
	struct fenced_node nodeinfo;
	int rv;

	memset(&nodeinfo, 0, sizeof(nodeinfo));

	rv = fenced_node_info(FENCED_NODEID_US, &nodeinfo);
	if (rv < 0)
		return 0;

	if (nodeinfo.member)
		return 1;
	return 0;
}

static void wait_domain(int joining)
{
	int in, tries = 0;

	while (1) {
		if (joining)
			check_fenced_running();

		in = we_are_in_fence_domain();

		if (joining && in)
			break;

		if (!joining && !in)
			break;

		tries++;

		if (opt_wait_joinleave < 0)
			goto retry_domain;

		if (!opt_wait_joinleave || tries >= opt_wait_joinleave) {
			fprintf(stderr, "%s: %s not complete\n",
			       prog_name, joining ? "join" : "leave");
			break;
		}
 retry_domain:
		if (!(tries % 10))
			fprintf(stderr, "%s: waiting for fenced to %s the fence group.\n",
			       prog_name, joining ? "join" : "leave");

		sleep(1);
	}

	return;
}

static void read_ccs_nodeids(int cd)
{
	char path[PATH_MAX];
	char *nodeid_str;
	int i, error;

	memset(all_nodeids, 0, sizeof(all_nodeids));
	all_nodeids_count = 0;

	for (i = 1; ; i++) {
		nodeid_str = NULL;
		memset(path, 0, sizeof(path));
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@nodeid", i);

		error = ccs_get(cd, path, &nodeid_str);
		if (error || !nodeid_str)
			break;

		all_nodeids[all_nodeids_count++] = atoi(nodeid_str);
		free(nodeid_str);
	}
}

static int all_nodeids_are_members(void)
{
	int i, j, rv, found;

	memset(&cman_nodes, 0, sizeof(cman_nodes));
	cman_nodes_count = 0;

	rv = cman_get_nodes(ch, MAX_NODES, &cman_nodes_count, cman_nodes);
	if (rv < 0)
		return -1;

	for (i = 0; i < all_nodeids_count; i++) {
		found = 0;

		for (j = 0; j < cman_nodes_count; j++) {
			if (cman_nodes[j].cn_nodeid == all_nodeids[i] &&
			    cman_nodes[j].cn_member) {
				found = 1;
				break;
			}
		}

		if (!found)
			return 0;
	}
	return 1;
}

static int connect_cman(void)
{
	int rv, tries = 0;

	while (1) {
		ch = cman_init(NULL);
		if (ch)
			break;

		tries++;

		if (opt_retry_cman < 0)
			goto retry_init;

		if (!opt_retry_cman || tries >= opt_retry_cman)
			return -1;
 retry_init:
		if (!(tries % 10))
			fprintf(stderr, "%s: retrying cman connection\n", prog_name);
		sleep(1);
	}

	while (1) {
		rv = cman_is_active(ch);
		if (rv)
			break;

		tries++;

		if (opt_retry_cman < 0)
			goto retry_active;

		if (!opt_retry_cman || tries >= opt_retry_cman) {
			cman_finish(ch);
			return -1;
		}
 retry_active:
		if (!(tries % 10))
			fprintf(stderr, "%s: retrying cman active check\n", prog_name);
		sleep(1);
	}

	return 0;
}

static void delay_quorum(void)
{
	int rv, tries = 0;

	while (1) {
		rv = cman_is_quorate(ch);
		if (rv)
			break;

		rv = cman_is_active(ch);
		if (!rv) {
			cman_finish(ch);
			die("lost cman connection");
		}
		
		tries++;

		if (opt_delay_quorum < 0)
			goto retry_quorum;

		if (!opt_delay_quorum || tries >= opt_delay_quorum) {
			fprintf(stderr, "%s: continuing without quorum\n", prog_name);
			break;
		}
 retry_quorum:
		if (!(tries % 10))
			fprintf(stderr, "%s: delaying for quorum\n", prog_name);

		sleep(1);
	}

	return;
}

static void delay_members(void)
{
	int rv, tries = 0;
	int cd;

	cd = ccs_connect();
	if (cd < 0) {
		cman_finish(ch);
		die("lost cman/ccs connection");
	}

	read_ccs_nodeids(cd);

	while (1) {
		rv = all_nodeids_are_members();
		if (rv < 0) {
			ccs_disconnect(cd);
			cman_finish(ch);
			die("lost cman connection");
		}
		if (rv)
			break;

		tries++;

		if (opt_delay_members < 0)
			goto retry_members;

		if (!opt_delay_members || tries > opt_delay_members) {
			fprintf(stderr, "%s: continuing without all members\n", prog_name);
			break;
		}
 retry_members:
		if (!(tries % 10))
			fprintf(stderr, "%s: delaying for members\n", prog_name);

		sleep(1);
	}

	ccs_disconnect(cd);
	return;
}

static void do_join(int argc, char *argv[])
{
	int rv, tries = 0;

	rv = connect_cman();
	if (rv < 0)
		die("can't connect to cman");

	/* if delay_quorum() or delay_members() fail on any cman/ccs
	   connection or operation, they call cman_finish() and exit
	   with failure */

	if (opt_delay_quorum)
		delay_quorum();

	if (opt_delay_members)
		delay_members();

	cman_finish(ch);

	/* This loop deals with the case where fenced is slow enough starting
	   up that fenced_join fails.  Do we also want to add a delay here to
	   deal with the case where fenced is so slow starting up that it hasn't
	   locked its lockfile yet, causing check_fenced_running to fail? */

	while (1) {
		rv = fenced_join();
		if (!rv)
			break;

		check_fenced_running();

		tries++;
		if (!(tries % 10))
			fprintf(stderr, "%s: retrying join\n", prog_name);
		sleep(1);
	}

	if (opt_wait_joinleave)
		wait_domain(1);

	exit(EXIT_SUCCESS);
}

static void do_leave(void)
{
	int rv;

	check_controlled_systems();

	rv = fenced_leave();
	if (rv < 0)
		die("leave: can't communicate with fenced");

	if (opt_wait_joinleave)
		wait_domain(0);

	exit(EXIT_SUCCESS);
}

static void do_dump(void)
{
	char buf[FENCED_DUMP_SIZE];
	int rv;

	rv = fenced_dump_debug(buf);
	if (rv < 0)
		die("dump: can't communicate with fenced");

	do_write(STDOUT_FILENO, buf, strlen(buf));

	exit(EXIT_SUCCESS);
}

static int node_compare(const void *va, const void *vb)
{
	const struct fenced_node *a = va;
	const struct fenced_node *b = vb;

	return a->nodeid - b->nodeid;
}

/* copied from fence/fenced/fd.h, should probably be in libfenced.h */
#define CGST_WAIT_CONDITIONS	1
#define CGST_WAIT_MESSAGES	2
#define CGST_WAIT_FENCING	3

static const char *wait_str(int state)
{
	switch (state) {
	case 0:
		return "none";
	case CGST_WAIT_CONDITIONS:
		return "quorum";
	case CGST_WAIT_MESSAGES:
		return "messages";
	case CGST_WAIT_FENCING:
		return "fencing";
	}
	return "unknown";
}

/* copied from fence/fenced/fd.h, should probably be in libfenced.h */
#define VIC_DONE_AGENT          1
#define VIC_DONE_MEMBER         2
#define VIC_DONE_OVERRIDE       3
#define VIC_DONE_EXTERNAL       4

static const char *how_str(int how)
{
	switch (how) {
	case 0:
		return "none";
	case VIC_DONE_AGENT:
		return "agent";
	case VIC_DONE_MEMBER:
		return "member";
	case VIC_DONE_OVERRIDE:
		return "override";
	case VIC_DONE_EXTERNAL:
		return "external";
	}
	return "unknown";
}

static int do_list(void)
{
	struct fenced_domain d;
	struct fenced_node *np;
	int node_count;
	int rv, i;

	rv = fenced_domain_info(&d);
	if (rv < 0)
		exit(EXIT_FAILURE); /* fenced probably not running */

	printf("fence domain\n");
	printf("member count  %d\n", d.member_count);
	printf("victim count  %d\n", d.victim_count);
	printf("victim now    %d\n", d.current_victim);
	printf("master nodeid %d\n", d.master_nodeid);
	printf("wait state    %s\n", wait_str(d.state));
	printf("members       ");

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));

	rv = fenced_domain_nodes(FENCED_NODES_MEMBERS, MAX_NODES,
				 &node_count, nodes);
	if (rv < 0) {
		printf("error\n");
		goto fail;
	}

	qsort(&nodes, node_count, sizeof(struct fenced_node), node_compare);

	np = nodes;
	for (i = 0; i < node_count; i++) {
		printf("%d ", np->nodeid);
		np++;
	}
	printf("\n");

	if (!opt_all_nodes) {
		printf("\n");
		exit(EXIT_SUCCESS);
	}

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));

	rv = fenced_domain_nodes(FENCED_NODES_ALL, MAX_NODES,
				 &node_count, nodes);
	if (rv < 0)
		goto fail;

	qsort(&nodes, node_count, sizeof(struct fenced_node), node_compare);

	printf("all nodes\n");

	np = nodes;
	for (i = 0; i < node_count; i++) {
		printf("nodeid %d member %d victim %d last fence master %d how %s\n",
				np->nodeid,
				np->member,
				np->victim,
				np->last_fenced_master,
				how_str(np->last_fenced_how));
		np++;
	}
	printf("\n");
	exit(EXIT_SUCCESS);
 fail:
	fprintf(stderr, "fenced query error %d\n", rv);
	printf("\n");
	exit(EXIT_FAILURE);
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <ls|join|leave|dump> [options]\n", prog_name);
	printf("\n");
	printf("Actions:\n");
	printf("  ls		   List nodes status\n");
	printf("  join             Join the default fence domain\n");
	printf("  leave            Leave default fence domain\n");
	printf("  dump		   Dump debug buffer from fenced\n");
	printf("\n");
	printf("Options:\n");
	printf("  -n               Show all node information in ls\n");

	printf("  -t <seconds>     Retry cman connection for <seconds>.\n");
	printf("                   Default %d.  0 no retry, -1 indefinite retry.\n",
				   DEFAULT_RETRY_CMAN);

	printf("  -q <seconds>     Delay join up to <seconds> for the cluster to have quorum.\n");
	printf("                   Default %d.  0 no delay, -1 indefinite delay.\n",
				   DEFAULT_DELAY_QUORUM);

	printf("  -m <seconds>     Delay join up to <seconds> for all nodes in cluster.conf\n");
	printf("                   to be cluster members.\n");
	printf("                   Default %d.  0 no delay, -1 indefinite delay\n",
				   DEFAULT_DELAY_MEMBERS);

	printf("  -w <seconds>     Wait up to <seconds> for join or leave result.\n");
	printf("                   Default %d.  0 no wait, -1 indefinite wait.\n",
				   DEFAULT_WAIT_JOINLEAVE);

	printf("  -V               Print program version information, then exit\n");
	printf("  -h               Print this help, then exit\n");
	printf("\n");
}

#define OPTION_STRING "nt:q:m:w:Vh"

static void decode_arguments(int argc, char *argv[])
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'n':
			opt_all_nodes = 1;
			break;

		case 't':
			opt_retry_cman = atoi(optarg);
			break;

		case 'q':
			opt_delay_quorum = atoi(optarg);
			break;

		case 'm':
			opt_delay_members = atoi(optarg);
			break;

		case 'w':
			opt_wait_joinleave = atoi(optarg);
			break;

		case 'V':
			printf("fence_tool %s (built %s %s)\n",
			       RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			die("unknown option: %c\n", optchar);
			break;
		}
	}

	while (optind < argc) {
		if (strcmp(argv[optind], "join") == 0) {
			operation = OP_JOIN;
		} else if (strcmp(argv[optind], "leave") == 0) {
			operation = OP_LEAVE;
		} else if (strcmp(argv[optind], "dump") == 0) {
			operation = OP_DUMP;
		} else if (strcmp(argv[optind], "ls") == 0) {
			operation = OP_LIST;
		} else
			die("unknown option %s\n", argv[optind]);
		optind++;
	}

	if (!operation)
		die("no operation specified\n");
}

int main(int argc, char *argv[])
{
	prog_name = basename(argv[0]);

	decode_arguments(argc, argv);

	switch (operation) {
	case OP_JOIN:
		do_join(argc, argv);
		break;
	case OP_LEAVE:
		do_leave();
		break;
	case OP_DUMP:
		do_dump();
		break;
	case OP_LIST:
		do_list();
		break;
	}

	return EXIT_FAILURE;
}

