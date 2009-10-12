#ifndef _LIBFENCE_H_
#define _LIBFENCE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define FE_AGENT_SUCCESS	1	/* agent exited with EXIT_SUCCESS */
#define FE_AGENT_ERROR		2	/* agent exited with EXIT_FAILURE */
#define FE_AGENT_FORK		3	/* error forking agent */
#define FE_NO_CONFIG		4	/* ccs_connect error */
#define FE_NO_METHOD		5	/* zero methods defined */
#define FE_NO_DEVICE		6	/* zero devices defined in method */
#define FE_READ_AGENT		7	/* read (ccs) error on agent path */
#define FE_READ_ARGS		8	/* read (ccs) error on node/dev args */
#define FE_READ_METHOD		9	/* read (ccs) error on method */
#define FE_READ_DEVICE		10	/* read (ccs) error on method/device */

#define FENCE_AGENT_NAME_MAX 256	/* including terminating \0 */
#define FENCE_AGENT_ARGS_MAX 4096	/* including terminating \0 */

struct fence_log {
	int error;
	int method_num;
	int device_num;
	char agent_name[FENCE_AGENT_NAME_MAX];
	char agent_args[FENCE_AGENT_ARGS_MAX];
};

int fence_node(char *name, struct fence_log *log, int log_size, int *log_count);

int unfence_node(char *name, struct fence_log *log, int log_size,
		 int *log_count);

#ifdef __cplusplus
}
#endif

#endif
