#ifndef _FSCK_H
#define _FSCK_H


#include "fsck_incore.h"
#include "log.h"

/*
 * Exit codes used by fsck-type programs
 * Copied from e2fsck's e2fsck.h
 */
#define FSCK_OK          0      /* No errors */
#define FSCK_NONDESTRUCT 1      /* File system errors corrected */
#define FSCK_REBOOT      2      /* System should be rebooted */
#define FSCK_UNCORRECTED 4      /* File system errors left uncorrected */
#define FSCK_ERROR       8      /* Operational error */
#define FSCK_USAGE       16     /* Usage or syntax error */
#define FSCK_CANCELED    32     /* Aborted with a signal or ^C */
#define FSCK_LIBRARY     128    /* Shared library error */

struct gfs_sb;
struct fsck_sb;

struct options {
	char *device;
	int yes:1;
	int no:1;
};

extern uint64_t last_fs_block, last_reported_block;
extern int skip_this_pass, fsck_abort, fsck_query;
extern int errors_found, errors_corrected;

int initialize(struct fsck_sb *sbp);
void destroy(struct fsck_sb *sbp);
int block_mounters(struct fsck_sb *sbp, int block_em);
int pass1(struct fsck_sb *sbp);
int pass1b(struct fsck_sb *sbp);
int pass1c(struct fsck_sb *sbp);
int pass2(struct fsck_sb *sbp, struct options *opts);
int pass3(struct fsck_sb *sbp, struct options *opts);
int pass4(struct fsck_sb *sbp, struct options *opts);
int pass5(struct fsck_sb *sbp, struct options *opts);

/* FIXME: Hack to get this going for pass2 - this should be pulled out
 * of pass1 and put somewhere else... */
int add_to_dir_list(struct fsck_sb *sbp, uint64_t block);

#endif /* _FSCK_H */
