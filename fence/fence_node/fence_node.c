#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <liblogthread.h>
#include <libcman.h>

#include "libfence.h"
#include "libfenced.h"
#include "copyright.cf"

static char *prog_name;
static char our_name[CMAN_MAX_NODENAME_LEN+1];
static int verbose;
static int unfence;

#define FL_SIZE 32
static struct fence_log flog[FL_SIZE];
static int flog_count;
static const char *action = "fence";

#define OPTION_STRING "UvhV"

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] node_name\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -U    Unfence the node, default local node name\n");
	printf("  -v    Show fence agent results, -vv for agent args\n");
	printf("  -h    Print this help, then exit\n");
	printf("  -V    Print program version information, then exit\n");
	printf("\n");
}

static int setup_cman(void)
{
	cman_handle_t ch;
	cman_node_t node;
	int active = 0;
	int rv;

	ch = cman_init(NULL);
	if (!ch)
		return -1;

 retry_active:
	rv = cman_is_active(ch);
	if (!rv) {
		if (active++ < 2) {
			sleep(1);
			goto retry_active;
		}
		cman_finish(ch);
		return -1;
	}

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		cman_finish(ch);
		return -1;
	}

	memset(our_name, 0, sizeof(our_name));
	strncpy(our_name, node.cn_name, CMAN_MAX_NODENAME_LEN);
	cman_finish(ch);
	return 0;
}

static const char *fe_str(int r)
{
	switch (r) {
	case FE_AGENT_SUCCESS:
		return "success";
	case FE_AGENT_ERROR:
		return "error from agent";
	case FE_AGENT_FORK:
		return "error from fork";
	case FE_NO_CONFIG:
		return "error from ccs";
	case FE_NO_METHOD:
		return "error no method";
	case FE_NO_DEVICE:
		return "error no device";
	case FE_READ_AGENT:
		return "error config agent";
	case FE_READ_ARGS:
		return "error config args";
	case FE_READ_METHOD:
		return "error config method";
	case FE_READ_DEVICE:
		return "error config device";
	default:
		return "error unknown";
	}
}

int main(int argc, char *argv[])
{
	char *victim = NULL;
	int cont = 1, optchar, error, rv, i, c;

	prog_name = argv[0];

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'U':
			unfence = 1;
			action = "unfence";
			break;

		case 'v':
			verbose++;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s %s (built %s %s)\n", prog_name,
				RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
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
			die("unknown option: %c", optchar);
			break;
		};
	}

	while (optind < argc) {
		if (victim)
			die("unknown option %s", argv[optind]);
		victim = argv[optind];
		optind++;
	}

	if (!victim && !unfence)
		die("no node name specified");

	error = setup_cman();
	if (error)
		die("cannot connect to cman");

	if (!victim && unfence)
		victim = our_name;

	memset(&flog, 0, sizeof(flog));
	flog_count = 0;

	if (unfence)
		error = unfence_node(victim, flog, FL_SIZE, &flog_count);
	else
		error = fence_node(victim, flog, FL_SIZE, &flog_count);

	if (!verbose)
		goto skip;

	if (flog_count > FL_SIZE) {
		fprintf(stderr, "%s_node log overflow %d", action, flog_count);
		flog_count = FL_SIZE;
	}

	for (i = 0; i < flog_count; i++) {
		fprintf(stderr, "%s %s dev %d.%d agent %s result: %s\n",
			action, victim, flog[i].method_num, flog[i].device_num,
			flog[i].agent_name[0] ? flog[i].agent_name : "none",
			fe_str(flog[i].error));

		if (verbose < 2)
			continue;

		for (c = 0; c < strlen(flog[i].agent_args); c++) {
			if (flog[i].agent_args[c] == '\n')
				flog[i].agent_args[c] = ' ';
		}
		fprintf(stderr, "agent args: %s\n", flog[i].agent_args);
	}

 skip:
	logt_init("fence_node", LOG_MODE_OUTPUT_SYSLOG, SYSLOGFACILITY,
		  SYSLOGLEVEL, 0, NULL);

	if (unfence) {
		if (error == -2) {
			fprintf(stderr, "unfence %s undefined\n", victim);
			rv = 2;
		} else if (error) {
			fprintf(stderr, "unfence %s failed\n", victim);
			logt_print(LOG_ERR, "unfence %s failed\n", victim);
			rv = EXIT_FAILURE;
		} else {
			fprintf(stderr, "unfence %s success\n", victim);
			logt_print(LOG_ERR, "unfence %s success\n", victim);
			rv = EXIT_SUCCESS;
		}
	} else {
		if (error == -2) {
			fprintf(stderr, "fence %s undefined\n", victim);
			logt_print(LOG_ERR, "fence %s undefined\n", victim);
			rv = 2;
		} else if (error) {
			fprintf(stderr, "fence %s failed\n", victim);
			logt_print(LOG_ERR, "fence %s failed\n", victim);
			rv = EXIT_FAILURE;
		} else {
			fprintf(stderr, "fence %s success\n", victim);
			logt_print(LOG_ERR, "fence %s success\n", victim);
			rv = EXIT_SUCCESS;

			/* Tell fenced what we've done so that it can avoid
			   fencing this node again if the fence_node() rebooted
			   it. */
			fenced_external(victim);
		}
	}

	logt_exit();
	exit(rv);
}

