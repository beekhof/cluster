#ifndef __RESGROUP_H
#define __RESGROUP_H

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <gettid.h>
#include <rg_locks.h>
#include <message.h>
#include <rg_queue.h>
#include <signals.h>

/**
 * Service state as represented on disk.
 *
 * This structure represents a service description.  This data structure
 * represents the in-memory service description.  (There is a separate
 * description of the on-disk format.)
 */
typedef struct {
	char		rs_name[64];	/**< Service name */
	/* 64 */
	uint32_t	rs_id;		/**< Service ID */
	uint32_t	rs_magic;	/**< Magic ID */
	uint32_t	rs_owner;	/**< Member ID running service. */
	uint32_t	rs_last_owner;	/**< Last member to run the service. */
	/* 80 */
	uint32_t	rs_state;	/**< State of service. */
	uint32_t	rs_restarts;	/**< Number of cluster-induced 
					     restarts */
	uint64_t	rs_transition;	/**< Last service transition time */
	/* 96 */
	uint32_t	rs_flags;	/**< User setted flags */
	/* 100 */
	uint8_t		rs_version;	/**< State version */
	uint8_t		_pad_[3];
	/* 104 */
} rg_state_t;

#define swab_rg_state_t(ptr) \
{\
	swab32((ptr)->rs_id);\
	swab32((ptr)->rs_magic);\
	swab32((ptr)->rs_owner);\
	swab32((ptr)->rs_last_owner);\
	swab32((ptr)->rs_state);\
	swab32((ptr)->rs_restarts);\
	swab64((ptr)->rs_transition);\
	swab32((ptr)->rs_flags);\
}

#if 0
/* Future upgrade compatibility */
#define RG_STATE_MINSIZE		96
#define RG_STATE_CURRENT_VERSION	1

extern size_t rg_state_t_version_sizes[];
#endif


#define RG_PORT    177

/* Constants moved to src/clulib/constants.c */
/* DO NOT EDIT */
#define RG_MAGIC   0x11398fed

#define RG_ACTION_REQUEST	/* Message header */ 0x138582
/* Argument to RG_ACTION_REQUEST */
#define RG_ACTION_MASTER	0xfe0db143
#define RG_ACTION_USER		0x3f173bfd
/* */
#define RG_EVENT		0x138583

/* Requests */
#define RG_SUCCESS	  0
#define RG_FAIL		  1
#define RG_START	  2
#define RG_STOP		  3
#define RG_STATUS	  4
#define RG_DISABLE	  5
#define RG_STOP_RECOVER	  6
#define RG_START_RECOVER  7
#define RG_RESTART	  8
#define RG_EXITING	  9 
#define RG_INIT		  10
#define RG_ENABLE	  11
#define RG_STATUS_NODE	  12
#define RG_RELOCATE	  13
#define RG_CONDSTOP	  14
#define RG_CONDSTART	  15
#define RG_START_REMOTE   16	/* Part of a relocate */
#define RG_STOP_USER	  17	/* User-stop request */
#define RG_STOP_EXITING	  18	/* Exiting. */
#define RG_LOCK		  19
#define RG_UNLOCK	  20
#define RG_QUERY_LOCK	  21
#define RG_MIGRATE	  22
#define RG_FREEZE	  23
#define RG_UNFREEZE	  24
#define RG_STATUS_INQUIRY 25
#define RG_CONVALESCE	  26
#define RG_NONE		  999

const char *rg_req_str(int req);

int handle_relocate_req(char *svcName, int request, int preferred_target,
			int *new_owner);
int handle_start_req(char *svcName, int req, int *new_owner);
int handle_fd_start_req(char *svcName, int req, int *new_owner);
int handle_recover_req(char *svcName, int *new_owner);
int handle_start_remote_req(char *svcName, int req);

/* Resource group states (for now) */
#define RG_STATE_BASE			110
#define RG_STATE_STOPPED		110	/** Resource group is stopped */
#define RG_STATE_STARTING		111	/** Resource is starting */
#define RG_STATE_STARTED		112	/** Resource is started */
#define RG_STATE_STOPPING		113	/** Resource is stopping */
#define RG_STATE_FAILED			114	/** Resource has failed */
#define RG_STATE_UNINITIALIZED		115	/** Thread not running yet */
#define RG_STATE_CHECK			116	/** Checking status */
#define RG_STATE_ERROR			117	/** Recoverable error */
#define RG_STATE_RECOVER		118	/** Pending recovery */
#define RG_STATE_DISABLED		119	/** Resource not allowd to run */
#define RG_STATE_MIGRATE		120	/** Resource migrating */

#define DEFAULT_CHECK_INTERVAL		10

/* Resource group flags (for now) */
#define RG_FLAG_FROZEN			(1<<0)	/** Resource frozen */
#define RG_FLAG_PARTIAL			(1<<1)	/** One or more non-critical
						    resources offline */

const char *rg_state_str(int val);
const char *rg_flag_str(int val);
const char *rg_flags_str(char *flags_string, size_t size, int val, char *separator);
int rg_state_str_to_id(const char *val);
const char *rg_flags_str(char *flags_string, size_t size, int val, char *separator);
const char *agent_op_str(int val);

int eval_groups(int local, uint32_t nodeid, int nodeStatus);
int group_migrate(const char *groupname, int target);

int rg_status(const char *resgroupname);
int group_op(const char *rgname, int op);
void rg_init(void);
int init_resource_groups(int, int);

/* Basic service operations */
int svc_start(const char *svcName, int req);
int svc_stop(const char *svcName, int error);
int svc_status(const char *svcName);
int svc_status_inquiry(const char *svcName);
int svc_disable(const char *svcName);
int svc_fail(const char *svcName);
int svc_freeze(const char *svcName);
int svc_unfreeze(const char *svcName);
int svc_convalesce(const char *svcName);
int svc_migrate(const char *svcName, int target);
int svc_start_remote(const char *svcName, int request, uint32_t target);
int svc_report_failure(const char *svcName);

int rt_enqueue_request(const char *resgroupname, int request,
		       msgctx_t *resp_ctx,
       		       int max, uint32_t target, int arg0, int arg1);
void dump_threads(FILE *fp);

void send_response(int ret, int node, request_t *req);
void send_ret(msgctx_t *ctx, char *name, int ret, int orig_request,
	      int new_owner);

/* from rg_state.c */
int set_rg_state(const char *name, rg_state_t *svcblk);
int get_rg_state(const char *servicename, rg_state_t *svcblk);
int get_rg_state_local(const char *servicename, rg_state_t *svcblk);
uint32_t best_target_node(cluster_member_list_t *allowed, uint32_t owner,
			  const char *rg_name, int lock);

extern int cluster_timeout;

#ifdef DEBUG
int _rg_lock(const char *name, struct dlm_lksb *p);
int _rg_lock_dbg(const char *, struct dlm_lksb *, const char *, int);
#define rg_lock(name, p) _rg_lock_dbg(name, p, __FILE__, __LINE__)

int _rg_unlock(struct dlm_lksb *p);
int _rg_unlock_dbg(struct dlm_lksb *, const char *, int);
#define rg_unlock(p) _rg_unlock_dbg(p, __FILE__, __LINE__)

#else
int rg_lock(const char *name, struct dlm_lksb *p);
int rg_unlock(struct dlm_lksb *p);
#endif


/* Return codes */
#define RG_EPERM	-18		/* Permission denied */
#define RG_ERELO	-17		/* Relocation failure; service running
					   on original node */
#define RG_EEXCL	-16		/* Service not runnable due to
					   inability to start exclusively */
#define RG_EDOMAIN	-15		/* Service not runnable given the
					   set of nodes and its failover
					   domain */
#define RG_ESCRIPT	-14		/* S/Lang script failed */
#define RG_EFENCE	-13		/* Fencing operation pending */
#define RG_ENODE	-12		/* Node is dead/nonexistent */
#define RG_EFROZEN	-11		/* Service is frozen */
#define RG_ERUN		-10		/* Service is already running */
#define RG_EQUORUM	-9		/* Operation requires quorum */
#define RG_EINVAL	-8		/* Invalid operation for resource */
#define RG_EDEPEND 	-7		/* Operation violates dependency */
#define RG_EAGAIN	-6		/* Try again */
#define RG_EDEADLCK	-5		/* Aborted - would deadlock */
#define RG_ENOSERVICE	-4		/* Service does not exist */
#define RG_EFORWARD	-3		/* Service not mastered locally */
#define RG_EABORT	-2		/* Abort; service unrecoverable */
#define RG_EFAIL	-1		/* Generic failure */
#define RG_ESUCCESS	0
#define RG_YES		1
#define RG_NO		2


const char *rg_strerror(int val);

/*
   Status tree flags
 */
#define SFL_FAILURE		(1<<0)
#define SFL_RECOVERABLE		(1<<1)
#define SFL_PARTIAL		(1<<2)

//#define DEBUG
#ifdef DEBUG

#define dbg_printf(fmt, args...) \
{\
	printf("{%d} ", gettid());\
	printf(fmt, ##args);\
	fflush(stdout);\
}

#else /* DEBUG */

#define dbg_printf(fmt, args...)

#endif

#endif
