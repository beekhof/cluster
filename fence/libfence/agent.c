#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#include "libfence.h"
#include "ccs.h"

#define MAX_METHODS		8
#define MAX_DEVICES		8

#define METHOD_NAME_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/fence/method[%d]/@name"
#define DEVICE_NAME_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/fence/method[@name=\"%s\"]/device[%d]/@name"
#define NODE_FENCE_ARGS_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/fence/method[@name=\"%s\"]/device[%d]/@*"
#define AGENT_NAME_PATH			"/cluster/fencedevices/fencedevice[@name=\"%s\"]/@agent"
#define FENCE_DEVICE_ARGS_PATH	"/cluster/fencedevices/fencedevice[@name=\"%s\"]/@*"



static int run_agent(char *agent, char *args, int *agent_result)
{
	int pid, status, len;
	int pr_fd, pw_fd;  /* parent read/write file descriptors */
	int cr_fd, cw_fd;  /* child read/write file descriptors */
	int fd1[2];
	int fd2[2];

	cr_fd = cw_fd = pr_fd = pw_fd = -1;

	if (args == NULL || agent == NULL)
		goto fail;
	len = strlen(args);

	if (pipe(fd1))
		goto fail;
	pr_fd = fd1[0];
	cw_fd = fd1[1];

	if (pipe(fd2))
		goto fail;
	cr_fd = fd2[0];
	pw_fd = fd2[1];

	pid = fork();
	if (pid < 0) {
		*agent_result = FE_AGENT_FORK;
		goto fail;
	}

	if (pid) {
		/* parent */
		int ret;

		fcntl(pr_fd, F_SETFL, fcntl(pr_fd, F_GETFL, 0) | O_NONBLOCK);

		do {
			ret = write(pw_fd, args, len);
		} while (ret < 0 && errno == EINTR);

		if (ret != len)
			goto fail;

		close(pw_fd);
		waitpid(pid, &status, 0);

		if (!WIFEXITED(status) || WEXITSTATUS(status)) {
			*agent_result = FE_AGENT_ERROR;
			goto fail;
		} else {
			*agent_result = FE_AGENT_SUCCESS;
		}
	} else {
		/* child */

		close(1);
		if (dup(cw_fd) < 0)
			goto fail;
		close(2);
		if (dup(cw_fd) < 0)
			goto fail;
		close(0);
		if (dup(cr_fd) < 0)
			goto fail;
		/* keep cw_fd open so parent can report all errors. */
		close(pr_fd);
		close(cr_fd);
		close(pw_fd);

		execlp(agent, agent, NULL);
		exit(EXIT_FAILURE);
	}

	close(pr_fd);
	close(cw_fd);
	close(cr_fd);
	close(pw_fd);
	return 0;

 fail:
	close(pr_fd);
	close(cw_fd);
	close(cr_fd);
	close(pw_fd);
	return -1;
}

static int make_args(int cd, char *victim, char *method, int d,
		     char *device, char **args_out)
{
	char path[PATH_MAX];
	char *args, *str;
	int error, ret, cnt = 0;
	size_t len, pos;

	args = malloc(FENCE_AGENT_ARGS_MAX);
	if (!args)
		return -ENOMEM;
	memset(args, 0, FENCE_AGENT_ARGS_MAX);

	len = FENCE_AGENT_ARGS_MAX - 1;
	pos = 0;

	/* node-specific args for victim */

	memset(path, 0, PATH_MAX);
	sprintf(path, NODE_FENCE_ARGS_PATH, victim, method, d+1);

	for (;;) {
		error = ccs_get_list(cd, path, &str);
		if (error || !str)
			break;
		++cnt;

		if (!strncmp(str, "name=", 5)) {
			free(str);
			continue;
		}

		ret = snprintf(args + pos, len - pos, "%s\n", str);

		free(str);

		if (ret >= len - pos) {
			error = -E2BIG;
			goto out;
		}
		pos += ret;
	}

	/* add nodename of victim to args */

	if (!strstr(args, "nodename=")) {
		ret = snprintf(args + pos, len - pos, "nodename=%s\n", victim);
		if (ret >= len - pos) {
			error = -E2BIG;
			goto out;
		}
		pos += ret;
	}

	/* device-specific args */

	memset(path, 0, PATH_MAX);
	sprintf(path, FENCE_DEVICE_ARGS_PATH, device);

	for (;;) {
		error = ccs_get_list(cd, path, &str);
		if (error || !str)
			break;
		++cnt;

		if (!strncmp(str, "name=", 5)) {
			free(str);
			continue;
		}

		ret = snprintf(args + pos, len - pos, "%s\n", str);

		free(str);

		if (ret >= len - pos) {
			error = -E2BIG;
			goto out;
		}
		pos += ret;
	}

	if (cnt)
		error = 0;
 out:
	if (error) {
		free(args);
		args = NULL;
	}

	*args_out = args;
	return error;
}

/* return name of m'th method for nodes/<victim>/fence/ */

static int get_method(int cd, char *victim, int m, char **method)
{
	char path[PATH_MAX], *str = NULL;
	int error;

	memset(path, 0, PATH_MAX);
	sprintf(path, METHOD_NAME_PATH, victim, m+1);

	error = ccs_get(cd, path, &str);
	*method = str;
	return error;
}

/* return name of d'th device under nodes/<victim>/fence/<method>/ */

static int get_device(int cd, char *victim, char *method, int d, char **device)
{
	char path[PATH_MAX], *str = NULL;
	int error;

	memset(path, 0, PATH_MAX);
	sprintf(path, DEVICE_NAME_PATH, victim, method, d+1);

	error = ccs_get(cd, path, &str);
	*device = str;
	return error;
}

static int count_methods(int cd, char *victim)
{
	char path[PATH_MAX], *name;
	int error, i;

	for (i = 0; i < MAX_METHODS; i++) {
		memset(path, 0, PATH_MAX);
		sprintf(path, METHOD_NAME_PATH, victim, i+1);

		error = ccs_get(cd, path, &name);
		if (error)
			break;
		free(name);
	}
	return i;
}

static int count_devices(int cd, char *victim, char *method)
{
	char path[PATH_MAX], *name;
	int error, i;

	for (i = 0; i < MAX_DEVICES; i++) {
		memset(path, 0, PATH_MAX);
		sprintf(path, DEVICE_NAME_PATH, victim, method, i+1);

		error = ccs_get(cd, path, &name);
		if (error)
			break;
		free(name);
	}
	return i;
}

static int use_device(int cd, char *victim, char *method, int d,
		      char *device, struct fence_log *lp)
{
	char path[PATH_MAX], *agent, *args = NULL;
	int error;

	memset(path, 0, PATH_MAX);
	sprintf(path, AGENT_NAME_PATH, device);

	error = ccs_get(cd, path, &agent);
	if (error) {
		lp->error = FE_READ_AGENT;
		goto out;
	}

	strncpy(lp->agent_name, agent, FENCE_AGENT_NAME_MAX-1);

	error = make_args(cd, victim, method, d, device, &args);
	if (error) {
		lp->error = FE_READ_ARGS;
		goto out_agent;
	}

	strncpy(lp->agent_args, args, FENCE_AGENT_ARGS_MAX-1);

	error = run_agent(agent, args, &lp->error);

	free(args);
 out_agent:
	free(agent);
 out:
	return error;
}

int fence_node(char *victim, struct fence_log *log, int log_size,
	       int *log_count)
{
	struct fence_log stub;
	struct fence_log *lp = log;
	char *method = NULL, *device = NULL;
	char *victim_nodename = NULL;
	int num_methods, num_devices, m, d, cd, rv;
	int left = log_size;
	int error = -1;
	int count = 0;

	cd = ccs_connect();
	if (cd < 0) {
		if (lp && left) {
			lp->error = FE_NO_CONFIG;
			lp++;
			left--;
		}
		count++;
		error = -1;
		goto ret;
	}

	if (ccs_lookup_nodename(cd, victim, &victim_nodename) == 0)
		victim = victim_nodename;

	num_methods = count_methods(cd, victim);
	if (!num_methods) {
		if (lp && left) {
			lp->error = FE_NO_METHOD;
			lp++;
			left--;
		}
		count++;
		error = -2;		/* No fencing */
		goto out;
	}

	for (m = 0; m < num_methods; m++) {

		rv = get_method(cd, victim, m, &method);
		if (rv) {
			if (lp && left) {
				lp->error = FE_READ_METHOD;
				lp->method_num = m;
				lp++;
				left--;
			}
			count++;
			error = -1;
			continue;
		}

		num_devices = count_devices(cd, victim, method);
		if (!num_devices) {
			if (lp && left) {
				lp->error = FE_NO_DEVICE;
				lp->method_num = m;
				lp++;
				left--;
			}
			count++;
			error = -1;
			continue;
		}

		for (d = 0; d < num_devices; d++) {
			rv = get_device(cd, victim, method, d, &device);
			if (rv) {
				if (lp && left) {
					lp->error = FE_READ_DEVICE;
					lp->method_num = m;
					lp->device_num = d;
					lp++;
					left--;
				}
				count++;
				error = -1;
				break;
			}

			/* every call to use_device generates a log entry,
			   whether success or fail */

			error = use_device(cd, victim, method, d, device,
					   (lp && left) ? lp : &stub);
			count++;
			if (lp && left) {
				/* error, name, args already set */
				lp->method_num = m;
				lp->device_num = d;
				lp++;
				left--;
			}

			if (error)
				break;

			free(device);
			device = NULL;
		}

		if (device)
			free(device);

		free(method);

		/* we return 0 for fencing success when use_device has
		   returned zero for each device in this method */

		if (!error)
			break;
	}

	if (victim_nodename)
		free(victim_nodename);
 out:
	ccs_disconnect(cd);
 ret:
	if (log_count)
		*log_count = count;
	return error;
}

#define UN_DEVICE_NAME_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/unfence/device[%d]/@name"
#define UN_NODE_FENCE_ARGS_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/unfence/device[%d]/@*"

static int make_args_unfence(int cd, char *victim, int d,
			     char *device, char **args_out)
{
	char path[PATH_MAX];
	char *args, *str;
	int error, ret, cnt = 0;
	size_t len, pos;

	args = malloc(FENCE_AGENT_ARGS_MAX);
	if (!args)
		return -ENOMEM;
	memset(args, 0, FENCE_AGENT_ARGS_MAX);

	len = FENCE_AGENT_ARGS_MAX - 1;
	pos = 0;

	/* node-specific args for victim */

	memset(path, 0, PATH_MAX);
	sprintf(path, UN_NODE_FENCE_ARGS_PATH, victim, d+1);

	for (;;) {
		error = ccs_get_list(cd, path, &str);
		if (error || !str)
			break;
		++cnt;

		if (!strncmp(str, "name=", 5)) {
			free(str);
			continue;
		}

		ret = snprintf(args + pos, len - pos, "%s\n", str);

		free(str);
		
		if (ret >= len - pos) {
			error = -E2BIG;
			goto out;
		}
		pos += ret;
	}

	/* add nodename of victim to args */

	if (!strstr(args, "nodename=")) {
		ret = snprintf(args + pos, len - pos, "nodename=%s\n", victim);
		if (ret >= len - pos) {
			error = -E2BIG;
			goto out;
		}
		pos += ret;
	}

	/* device-specific args */

	memset(path, 0, PATH_MAX);
	sprintf(path, FENCE_DEVICE_ARGS_PATH, device);

	for (;;) {
		error = ccs_get_list(cd, path, &str);
		if (error || !str)
			break;
		++cnt;

		if (!strncmp(str, "name=", 5)) {
			free(str);
			continue;
		}

		ret = snprintf(args + pos, len - pos, "%s\n", str);

		free(str);

		if (ret >= len - pos) {
			error = -E2BIG;
			goto out;
		}
		pos += ret;
	}

	if (cnt)
		error = 0;
 out:
	if (error) {
		free(args);
		args = NULL;
	}

	*args_out = args;
	return error;
}

/* return name of d'th device under nodes/<victim>/unfence/ */

static int get_device_unfence(int cd, char *victim, int d, char **device)
{
	char path[PATH_MAX], *str = NULL;
	int error;

	memset(path, 0, PATH_MAX);
	sprintf(path, UN_DEVICE_NAME_PATH, victim, d+1);

	error = ccs_get(cd, path, &str);
	*device = str;
	return error;
}

static int count_devices_unfence(int cd, char *victim)
{
	char path[PATH_MAX], *name;
	int error, i;

	for (i = 0; i < MAX_DEVICES; i++) {
		memset(path, 0, PATH_MAX);
		sprintf(path, UN_DEVICE_NAME_PATH, victim, i+1);

		error = ccs_get(cd, path, &name);
		if (error)
			break;
		free(name);
	}
	return i;
}

static int use_device_unfence(int cd, char *victim, int d,
			      char *device, struct fence_log *lp)
{
	char path[PATH_MAX], *agent, *args = NULL;
	int error;

	memset(path, 0, PATH_MAX);
	sprintf(path, AGENT_NAME_PATH, device);

	error = ccs_get(cd, path, &agent);
	if (error) {
		lp->error = FE_READ_AGENT;
		goto out;
	}

	strncpy(lp->agent_name, agent, FENCE_AGENT_NAME_MAX-1);

	error = make_args_unfence(cd, victim, d, device, &args);
	if (error) {
		lp->error = FE_READ_ARGS;
		goto out_agent;
	}

	strncpy(lp->agent_args, args, FENCE_AGENT_ARGS_MAX);

	error = run_agent(agent, args, &lp->error);

	free(args);
 out_agent:
	free(agent);
 out:
	return error;
}

int unfence_node(char *victim, struct fence_log *log, int log_size,
		 int *log_count)
{
	struct fence_log stub;
	struct fence_log *lp = log;
	char *device = NULL;
	char *victim_nodename = NULL;
	int num_devices, d, cd, rv;
	int left = log_size;
	int error = -1;
	int count = 0;

	cd = ccs_connect();
	if (cd < 0) {
		if (lp && left) {
			lp->error = FE_NO_CONFIG;
			lp++;
			left--;
		}
		count++;
		error = -1;
		goto ret;
	}

	if (ccs_lookup_nodename(cd, victim, &victim_nodename) == 0)
		victim = victim_nodename;

	/* return -2 if unfencing fails because there's no unfencing
	   defined for the node */

	num_devices = count_devices_unfence(cd, victim);
	if (!num_devices) {
		if (lp && left) {
			lp->error = FE_NO_DEVICE;
			lp++;
			left--;
		}
		count++;
		error = -2;
		goto out;
	}

	/* try to unfence all devices even if some of them fail,
	   but the final return value is 0 only if all succeed */

	error = 0;

	for (d = 0; d < num_devices; d++) {
		rv = get_device_unfence(cd, victim, d, &device);
		if (rv) {
			if (lp && left) {
				lp->error = FE_READ_DEVICE;
				lp->device_num = d;
				lp++;
				left--;
			}
			count++;
			error = -1;
			continue;
		}

		/* every call to use_device generates a log entry,
		   whether success or fail */

		rv = use_device_unfence(cd, victim, d, device,
					(lp && left) ? lp : &stub);
		count++;
		if (lp && left) {
			/* error, name, args already set */
			lp->device_num = d;
			lp++;
			left--;
		}

		if (rv)
			error = -1;

		free(device);
		device = NULL;
	}

	if (victim_nodename)
		free(victim_nodename);
 out:
	ccs_disconnect(cd);
 ret:
	if (log_count)
		*log_count = count;
	return error;
}

