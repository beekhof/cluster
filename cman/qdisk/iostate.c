#include <pthread.h>
#include <iostate.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <liblogthread.h>
#include "iostate.h"

static iostate_t main_state = 0;
static int main_incarnation = 0;
static int qdisk_timeout = 0, sleeptime = 0;
static int thread_active = 0;
static pthread_t io_nanny_tid = 0;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t state_cond = PTHREAD_COND_INITIALIZER;

struct state_table {
	iostate_t state;
	const char *value;
};

static struct state_table io_state_table[] = {
{	STATE_NONE,	"none"	},
{	STATE_WRITE,	"write"	},
{	STATE_READ,	"read"	},
{	STATE_LSEEK,	"seek"	},
{	-1,		NULL	} };

static const char *
state_to_string(iostate_t state)
{
	static const char *ret = "unknown";
	int i;

	for (i=0; io_state_table[i].value; i++) {
		if (io_state_table[i].state == state) {
			ret = io_state_table[i].value;
			break;
		}
	}

	return ret;
}


void
io_state(iostate_t state)
{
	pthread_mutex_lock(&state_mutex);
	main_state = state;
	main_incarnation++; /* it does not matter if this wraps. */

	/* Optimization: Don't signal on STATE_NONE */
	if (state != STATE_NONE)
		pthread_cond_broadcast(&state_cond);

	pthread_mutex_unlock(&state_mutex);
}


static void *
io_nanny_thread(void *arg)
{
	struct timespec wait_time;
	iostate_t last_main_state = 0, current_main_state = 0;
	int last_main_incarnation = 0, current_main_incarnation = 0;
	int logged_incarnation = 0;

	/* Start with wherever we're at now */
	pthread_mutex_lock(&state_mutex);
	current_main_state = last_main_state = main_state;
	current_main_incarnation = last_main_incarnation = main_incarnation;
	pthread_mutex_unlock(&state_mutex);

	while (thread_active) {
		pthread_mutex_lock(&state_mutex);
    		clock_gettime(CLOCK_REALTIME, &wait_time);
		wait_time.tv_sec += sleeptime;
		pthread_cond_timedwait(&state_cond, &state_mutex, &wait_time);
		current_main_state = main_state;
		current_main_incarnation = main_incarnation;
		pthread_mutex_unlock(&state_mutex);

		if (!thread_active)
			break;

		if (!current_main_state)
			continue;

		/* if the state or incarnation changed, the main qdiskd
		 * thread is healthy */
		if (current_main_state != last_main_state ||
		    current_main_incarnation != last_main_incarnation) {
			last_main_state = current_main_state;
			last_main_incarnation = current_main_incarnation;
			continue;
		}

		/* Don't log things twice */
		if (logged_incarnation == current_main_incarnation)
			continue;
		logged_incarnation = current_main_incarnation;

		logt_print(LOG_WARNING, "qdiskd: %s "
			   "(system call) has hung for %d seconds\n",
			   state_to_string(current_main_state), sleeptime);
		logt_print(LOG_WARNING,
			   "In %d more seconds, we will be evicted\n",
			   (qdisk_timeout-sleeptime));
	}

	return NULL;
}


int
io_nanny_start(int timeout)
{
	int ret;

	pthread_mutex_lock(&state_mutex);

	sleeptime = timeout / 2;
	qdisk_timeout = timeout;
	thread_active = 1;

	ret = pthread_create(&io_nanny_tid, NULL, io_nanny_thread, NULL);
	pthread_mutex_unlock(&state_mutex);

	return ret;
}


int
io_nanny_stop(void)
{
	thread_active = 0;
	pthread_cond_broadcast(&state_cond);
	pthread_join(io_nanny_tid, NULL);
	io_nanny_tid = 0;

	return 0;
}
