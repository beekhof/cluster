#ifndef __CCS_DOT_H__
#define __CCS_DOT_H__

int ccs_connect(void);
int ccs_force_connect(const char *cluster_name, int blocking);
int ccs_disconnect(int desc);
int ccs_get(int desc, const char *query, char **rtn);
int ccs_get_list(int desc, const char *query, char **rtn);
int ccs_set(int desc, const char *path, char *val);
int ccs_lookup_nodename(int desc, const char *nodename, char **rtn);
void ccs_read_logging(int fd, const char *name, int *debug, int *mode,
                      int *syslog_facility, int *syslog_priority,
                      int *logfile_priority, char *logfile);
extern int fullxpath;

#endif /*  __CCS_DOT_H__ */
