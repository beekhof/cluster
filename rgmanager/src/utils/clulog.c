/** @file
 * Utility for logging arbitrary strings to the cluster log file via syslog.
 *
 * Author: Lon Hohberger <lhh at redhat.com>
 * Based on original code by: Jeff Moyer <jmoyer at redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <getopt.h>
#include <string.h>
#include <ccs.h>
#include <logging.h>

static void
usage(char *progname)
{
	fprintf(stdout, "%s [-m logname] -s severity \"message text\"\n", progname);
	exit(0);
}


int
main(int argc, char **argv)
{
	int opt, ccsfd;
	int severity = -1;

	char *logmsg = NULL;
	char *myname = NULL;

	while ((opt = getopt(argc, argv, "m:l:s:h")) != EOF) {
		switch(opt) {
		case 'l':
		case 's':
			severity = atoi(optarg);
			break;
		case 'm':
			myname = optarg;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return 0;
		}
	}

	logmsg = argv[optind];

	if (severity < 0)
		severity = SYSLOGLEVEL;

	init_logging((char *)"rgmanager", 1, severity);
	ccsfd = ccs_connect();
	setup_logging(ccsfd);
	ccs_disconnect(ccsfd);

	if (myname && strcmp(myname, "rgmanager")) {
		logt_print(severity, "[%s] %s\n", myname, logmsg);
	} else {
		logt_print(severity, "%s\n", logmsg);
	}

	close_logging();
	return 0;
}
