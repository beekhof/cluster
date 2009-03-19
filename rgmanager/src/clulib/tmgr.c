#ifdef WRAP_THREADS
#include <stdio.h>
#include <sys/types.h>
#include <gettid.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <pthread.h>
#include <list.h>
#include <execinfo.h>

typedef struct _thr {
	list_head();
	void *(*fn)(void *arg);
	char **name;
	pthread_t th;
} mthread_t;

typedef struct _arglist {
	void *(*real_thread_fn)(void *arg);
	void *real_thread_arg;
} thread_arg_t;

static mthread_t *_tlist = NULL;
static int _tcount = 0;
static pthread_rwlock_t _tlock = PTHREAD_RWLOCK_INITIALIZER;

void *
setup_thread(void *thread_arg)
{
	thread_arg_t *args = (thread_arg_t *)thread_arg;
	void *(*thread_func)(void *arg);
	sigset_t set;

	/* Block all non-fatal signals */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGUSR2);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGHUP);
	sigprocmask(SIG_BLOCK, &set, NULL);

	thread_func = args->real_thread_fn;
	thread_arg = args->real_thread_arg;
	free(args);
	return thread_func(thread_arg);
}

void
dump_thread_states(FILE *fp)
{
	int x;
	mthread_t *curr;
	fprintf(fp, "Thread Information\n");
	pthread_rwlock_rdlock(&_tlock);
	list_for(&_tlist, curr, x) {
		fprintf(fp, "  Thread #%d   id: %d   function: %s\n",
			x, (unsigned)curr->th, curr->name[0]);
	}
	pthread_rwlock_unlock(&_tlock);
	fprintf(fp, "\n\n");
}


int __real_pthread_create(pthread_t *, const pthread_attr_t *,
			  void *(*)(void*), void *);
int
__wrap_pthread_create(pthread_t *th, const pthread_attr_t *attr,
	 	      void *(*start_routine)(void*),
	 	      void *arg)
{
	void *fn = start_routine;
	mthread_t *newthread;
	thread_arg_t *targ;
	int ret;

	newthread = malloc(sizeof (*newthread));
	if (!newthread)
		return -1;
	targ = malloc(sizeof (*targ));
	if (!targ) {
		free(newthread)
		return -1;
	}

	targ->real_thread_fn = start_routine;
	targ->real_thread_arg = arg;

	ret = __real_pthread_create(th, attr, setup_thread, targ);
	if (ret) {
		if (newthread)
			free(newthread);
		if (targ)
			free(targ);
		return ret;
	}

	if (newthread) {
		newthread->th = *th;
		newthread->fn = start_routine;
		newthread->name = backtrace_symbols(&new->fn, 1);
		pthread_rwlock_wrlock(&_tlock);
		list_insert(&_tlist, newthread);
		++_tcount;
		pthread_rwlock_unlock(&_tlock);
	}

	return ret;
}


void __real_pthread_exit(void *);
void
__wrap_pthread_exit(void *exitval)
{
	mthread_t *old;
	int ret = 0, found = 0;
	pthread_t me = pthread_self();

	pthread_rwlock_rdlock(&_tlock);
	list_for(&_tlist, old, ret) {
		if (old->th == me) {
			found = 1;
			break;
		}
	}
	if (!found)
		old = NULL;
	pthread_rwlock_unlock(&_tlock);

	if (!old)
		__real_pthread_exit(exitval);

	pthread_rwlock_wrlock(&_tlock);
	list_remove(&_tlist, old);
	--_tcount;
	pthread_rwlock_unlock(&_tlock);

	if (old->name)
		free(old->name);
	free(old);
	__real_pthread_exit(exitval);
}
#endif
