/**
  @file Quorum daemon scoring functions + thread.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <ccs.h>
#include <liblogthread.h>
#include <sched.h>
#include <sys/mman.h>
#include "disk.h"
#include "score.h"

static pthread_mutex_t sc_lock = PTHREAD_MUTEX_INITIALIZER;
static int _score = 0, _maxscore = 0, _score_thread_running = 0;
static pthread_t score_thread = (pthread_t)0;
extern void set_priority(int, int);

struct h_arg {
	struct h_data *h;
	int sched_queue;
	int sched_prio;
	int count;
};

/*
  XXX Messy, but works for now... 
 */
static void
nullify(void)
{
	int fd[3];

	close(0);
	close(1);
	close(2);

	fd[0] = open("/dev/null", O_RDONLY);
	if (fd[0] != 0)
		dup2(fd[0], 0);
	fd[1] = open("/dev/null", O_WRONLY);
	if (fd[1] != 1)
		dup2(fd[1], 1);
	fd[2] = open("/dev/null", O_WRONLY);
	if (fd[2] != 2)
		dup2(fd[2], 2);
}


/**
  Set all signal handlers to default for exec of a script.
  ONLY do this after a fork().
 */
static void
restore_signals(void)
{
	sigset_t set;
	int x;

	for (x = 1; x < _NSIG; x++)
		signal(x, SIG_DFL);

	sigfillset(&set);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}


/**
  Spin off a user-defined heuristic
 */
static int
fork_heuristic(struct h_data *h, struct timespec *now)
{
	int pid;
	char *argv[4];

	if (h->childpid) {	
		errno = EINPROGRESS;
		return -1;
	}

	if (now->tv_sec < h->nextrun.tv_sec ||
	    now->tv_nsec < h->nextrun.tv_nsec)
		return 0;

	h->nextrun.tv_sec = now->tv_sec + h->interval;
	h->nextrun.tv_nsec = now->tv_nsec;

	h->failtime.tv_sec = now->tv_sec + h->maxtime;
	h->failtime.tv_nsec = now->tv_nsec;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid) {
		h->childpid = pid;
		return 0;
	}
	
	/*
	 * always use SCHED_OTHER for the child processes 
	 * nice -1 is fine; but we don't know what the child process
	 * might do, so leaving it (potentially) in SCHED_RR or SCHED_FIFO
	 * is out of the question
	 * 
	 * XXX if you set SCHED_OTHER in the conf file and nice 20, the below
	 * will make the heuristics a higher prio than qdiskd.  This should be
	 * fine in practice, because running qdiskd at nice 20 will cause all
	 * sorts of problems on a busy system.
	 */
	set_priority(SCHED_OTHER, -1);
	munlockall();
	restore_signals();

	argv[0] = strdup("/bin/sh");
	argv[1] = strdup("-c");
	argv[2] = h->program;
	argv[3] = NULL;

	nullify();

	execv("/bin/sh", argv);

	free(argv[0]);
	free(argv[1]);

	logt_print(LOG_ERR, "Execv failed\n");
	return 0;
}


/**
  Total our current score
 */
static void
total_score(struct h_data *h, int max, int *score, int *maxscore)
{
	int x;

	*score = 0;
	*maxscore = 0;
	
	/* Allow operation w/o any heuristics */
	if (!max) {
		*score = *maxscore = 1;
		return;
	}

	for (x = 0; x < max; x++) {
		*maxscore += h[x].score;
		if (h[x].available)
			*score += h[x].score;
	}
}


/**
  Check for response from a user-defined heuristic / script
 */
static int
check_heuristic(struct h_data *h, int block, struct timespec *now)
{
	int ret;
	int status;

	if (h->childpid == 0)
		/* No child to check */
		return 0;

	ret = waitpid(h->childpid, &status, block?0:WNOHANG);
	if (!block && ret == 0) {
		/* No children exited */

		/* no timeout */
		if (!h->maxtime)
			return 0;

		/* If we overran our timeout, the heuristic is dead */
		if (now->tv_sec > h->failtime.tv_sec ||
		    (now->tv_sec == h->failtime.tv_sec &&
		     now->tv_nsec > h->failtime.tv_nsec)) {
			h->misses = h->tko;
			h->failed = ETIMEDOUT;
			if (h->available) {
				logt_print(LOG_INFO, "Heuristic: '%s' DOWN - "
					"Exceeded timeout of %d seconds\n",
					h->program, h->maxtime);
				h->available = 0;
			}
		}

		return 0;
	}

	h->childpid = 0;
	if (ret < 0 && errno == ECHILD)
		/* wrong child? */
		goto miss;

	/* Timed out previously; this run must be ignored.  */
	if (h->failed) {
		h->failed = 0;
		goto miss;
	}
	if (!WIFEXITED(status)) {
		ret = 0;
		goto miss;
	}
	if (WEXITSTATUS(status) != 0) {
		ret = 0;
		goto miss;
	}

	/* Returned 0 and was not killed */
	if (!h->available) {
		h->available = 1;
		logt_print(LOG_INFO, "Heuristic: '%s' UP\n", h->program);
	}
	h->misses = 0;
	return 0;
	
miss:
	if (h->available) {
		h->misses++;
		if (h->misses >= h->tko) {
			logt_print(LOG_INFO,
				"Heuristic: '%s' DOWN (%d/%d)\n",
				h->program, h->misses, h->tko);
			h->available = 0;
		} else {
			logt_print(LOG_DEBUG,
				"Heuristic: '%s' missed (%d/%d)\n",
				h->program, h->misses, h->tko);
		}
	}
	
	return ret;
}


/**
  Kick off all available heuristics
 */
static int
fork_heuristics(struct h_data *h, int max)
{
	struct timespec now;
	int x;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (x = 0; x < max; x++)
		fork_heuristic(&h[x], &now);
	return 0;
}


/**
  Check all available heuristics
 */
static int
check_heuristics(struct h_data *h, int max, int block)
{
	struct timespec now;
	int x;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (x = 0; x < max; x++)
		check_heuristic(&h[x], block, &now);
	return 0;
}


/*
 * absmax should be qdiskd (interval * (tko-1))
 */
static void
auto_heuristic_timing(int *interval, int *tko, int absmax)
{
	if (!interval || ! tko)
		return;

	if (absmax < 3)
		return;

	if (absmax <= 4) {
		*interval = 1;
	} else if (absmax <= 22) {
		*interval = 2;
	} else if (absmax <= 39) {
		*interval = 3;
	} else if (absmax <= 50) {
		*interval = 4;
	} else {
		*interval = 5;
	}

	*tko = absmax / (*interval);
}


/**
  Read configuration data from CCS into the array provided
 */
int
configure_heuristics(int ccsfd, struct h_data *h, int max, int maxtime)
{
	int x = 0;
	char *val;
	char query[128];

	if (!h || !max)
		return -1;

	do {
		h[x].program = NULL;
		h[x].available = 0;
		h[x].misses = 0;
		auto_heuristic_timing(&h[x].interval, &h[x].tko, maxtime);
		h[x].maxtime = maxtime;
		h[x].score = 1;
		h[x].childpid = 0;
		h[x].nextrun.tv_sec = 0;
		h[x].nextrun.tv_nsec = 0;
		h[x].failtime.tv_sec = 0;
		h[x].failtime.tv_nsec = 0;

		/* Get program */
		snprintf(query, sizeof(query),
			 "/cluster/quorumd/heuristic[%d]/@program", x+1);
		if (ccs_get(ccsfd, query, &val) != 0)
			/* No more */
			break;
		h[x].program = val;

		/* Get score */
		snprintf(query, sizeof(query),
			 "/cluster/quorumd/heuristic[%d]/@score", x+1);
		if (ccs_get(ccsfd, query, &val) == 0) {
			h[x].score = atoi(val);
			free(val);
			if (h[x].score <= 0)
				h[x].score = 1;
		}
		
		/* Get query interval */
		snprintf(query, sizeof(query),
			 "/cluster/quorumd/heuristic[%d]/@interval", x+1);
		if (ccs_get(ccsfd, query, &val) == 0) {
			h[x].interval = atoi(val);
			free(val);
			if (h[x].interval <= 0)
				h[x].interval = 2;
		}
		
		/* Get tko for this heuristic */
		snprintf(query, sizeof(query),
			 "/cluster/quorumd/heuristic[%d]/@tko", x+1);
		if (ccs_get(ccsfd, query, &val) == 0) {
			h[x].tko= atoi(val);
			free(val);
			if (h[x].tko <= 0)
				h[x].tko = 1;
		}

		logt_print(LOG_DEBUG,
		       "Heuristic: '%s' score=%d interval=%d tko=%d\n",
		       h[x].program, h[x].score, h[x].interval, h[x].tko);

	} while (++x < max);

	logt_print(LOG_DEBUG, "%d heuristics loaded\n", x);
		
	return x;
}


/**
  Return the current score + maxscore to the caller
 */
int
get_my_score(int *score, int *maxscore)
{
	pthread_mutex_lock(&sc_lock);
	*score = _score;
	*maxscore = _maxscore;
	pthread_mutex_unlock(&sc_lock);

	return 0;
}


/**
  Call this if no heuristics are set to run in master-wins mode
 */
int
fudge_scoring(void)
{
	pthread_mutex_lock(&sc_lock);
	_score = _maxscore = 1;
	pthread_mutex_unlock(&sc_lock);

	return 0;
}


/**
  Loop for the scoring thread.
 */
static void *
score_thread_main(void *arg)
{
	struct h_arg *args = (struct h_arg *)arg;
	int score, maxscore;
	
	set_priority(args->sched_queue, args->sched_prio);

	while (_score_thread_running) {
		fork_heuristics(args->h, args->count);
		check_heuristics(args->h, args->count, 0);
		total_score(args->h, args->count, &score, &maxscore);

		pthread_mutex_lock(&sc_lock);
		_score = score;
		_maxscore = maxscore;
		pthread_mutex_unlock(&sc_lock);

		if (_score_thread_running)
			sleep(1);
	}

	free(args->h);
	free(args);
	logt_print(LOG_INFO, "Score thread going away\n");
	return (NULL);
}


/**
  Start the score thread.  h is copied into an argument which is
  passed in as the arg parameter in the score thread, so it is safe
  to pass in h if it was allocated on the stack.
 */
int
start_score_thread(qd_ctx *ctx, struct h_data *h, int count)
{
	pthread_attr_t attrs;
	struct h_arg *args;

	if (!h || !count)
		return -1;

	args = malloc(sizeof(struct h_arg));
	if (!args)
		return -1;

	args->h = malloc(sizeof(struct h_data) * count);
	if (!args->h) {
		free(args);
		return -1;
	}

	memcpy(args->h, h, (sizeof(struct h_data) * count));
	args->count = count;
	args->sched_queue = ctx->qc_sched;
	args->sched_prio = ctx->qc_sched_prio;

	_score_thread_running = 1;
	
        pthread_attr_init(&attrs);
        pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        pthread_create(&score_thread, &attrs, score_thread_main, args);
        pthread_attr_destroy(&attrs);

	if (score_thread)
		return 0;
	_score_thread_running = 0;
	return -1;	
}
