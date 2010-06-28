#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <signals.h>
#include <logging.h>

int watchdog_init(void);

static pid_t child = 0;

static void 
signal_handler(int signum)
{
        kill(child, signum);
}


static void 
redirect_signals(void)
{
        int i;
        for (i = 0; i < _NSIG; i++) {
	        switch (i) {
		case SIGCHLD:
		case SIGILL:
		case SIGFPE:
		case SIGSEGV:
		case SIGBUS:
		        setup_signal(i, SIG_DFL);
			break;
		default:
		        setup_signal(i, signal_handler);
		}
	}
}


static int
sysrq_reboot(void)
{
	int fd;

	fd = open("/proc/sysrq-trigger", O_WRONLY|O_SYNC);
	if (fd < 0)
		return fd;

	write(fd, "b\n", 2);
	fsync(fd);
	fdatasync(fd);
	close(fd);

	return 0;
}


/**
 return watchdog's pid, or 0 on failure
*/
int 
watchdog_init(void)
{
	int status;
	pid_t parent;
	
	parent = getpid();
	child = fork();
	if (child < 0)
	        return 0;
	else if (!child)
		return parent;
	
	redirect_signals();
	mlockall(MCL_CURRENT); /* shouldn't need MCL_FUTURE */
	
	while (1) {
	        if (waitpid(child, &status, 0) <= 0)
		        continue;
		
		if (WIFEXITED(status))
		        exit(WEXITSTATUS(status));
		
		if (WIFSIGNALED(status)) {
		        if (WTERMSIG(status) == SIGKILL) {
				/* Assume the admin did a 'killall' - it will
				 * kill us within a couple of seconds.  If 
				 * we are still alive after this sleep, it
				 * could have been the OOM killer killing
				 * rgmanager proper and we need to reboot.
				 */
				sleep(3);
			}
#ifdef DEBUG
		        logt_print(LOG_CRIT, "Watchdog: Daemon died, but not rebooting because DEBUG is set\n");
#else
			logt_print(LOG_CRIT, "Watchdog: Daemon died, rebooting...\n");
			sync();
			sysrq_reboot();
		        reboot(RB_AUTOBOOT);
#endif
			exit(255);
		}
	}
}
