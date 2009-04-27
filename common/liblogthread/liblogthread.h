#ifndef LOGTHREAD_DOT_H
#define LOGTHREAD_DOT_H

#include <syslog.h>

#define LOG_MODE_OUTPUT_FILE	1
#define LOG_MODE_OUTPUT_SYSLOG	2
#define LOG_MODE_OUTPUT_STDERR	4

int logt_init(const char *name, int mode, int syslog_facility, int syslog_priority,
	      int logfile_priority, const char *logfile);
void logt_conf(const char *name, int mode, int syslog_facility, int syslog_priority,
	       int logfile_priority, const char *logfile);
void logt_exit(void);
int logt_reinit(void);
void logt_print(int level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));;

#endif
