#ifndef _DAEMON_INIT_H
#define _DAEMON_INIT_H

int check_pid_valid(pid_t pid, char *prog);
int check_process_running(char *prog, pid_t * pid);
void daemon_init(char *prog);
void daemon_cleanup(void);

#endif
