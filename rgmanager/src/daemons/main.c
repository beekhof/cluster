#include <platform.h>
#include <ccs.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <rg_locks.h>
#include <fcntl.h>
#include <restart_counter.h>
#include <resgroup.h>
#include <reslist.h>
#include <logging.h>
#include <members.h>
#include <msgsimple.h>
#include <vf.h>
#include <lock.h>
#include <sys/socket.h>
#include <message.h>
#include <rg_queue.h>
#include <malloc.h>
#include <cman-private.h>
#include <event.h>
#include <members.h>
#include <daemon_init.h>
#include <groups.h>

#ifdef WRAP_THREADS
void dump_thread_states(FILE *);
#endif
static int configure_rgmanager(int ccsfd, int debug, int *cluster_timeout);
void set_transition_throttling(int);

void flag_shutdown(int sig);

int watchdog_init(void);


int cluster_timeout = 10;
int shutdown_pending = 0, running = 1, need_reconfigure = 0;
char debug = 0; /* XXX* */
static int signalled = 0;
static uint8_t ALIGNED port = RG_PORT;
static char *rgmanager_lsname = (char *)"rgmanager"; /* XXX default */
static int status_poll_interval = DEFAULT_CHECK_INTERVAL;


static void
segfault(int __attribute__ ((unused)) sig)
{
	char ow[64];
	int err; // dumb error checking... will be replaced by logsys

	snprintf(ow, sizeof(ow)-1, "PID %d Thread %d: SIGSEGV\n", getpid(),
		 gettid());
	err = write(2, ow, strlen(ow));
	while(1)
		sleep(60);
}


static int
send_exit_msg(msgctx_t *ctx)
{
	msg_send_simple(ctx, RG_EXITING, my_id(), 0);

	return 0;
}


static void
send_node_states(msgctx_t *ctx)
{
	int x;
	event_master_t master;
	generic_msg_hdr hdr;
	cluster_member_list_t *ml = member_list();

	master.m_nodeid = 0;
	event_master_info_cached(&master);

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cn_member == 1) {
			msg_send_simple(ctx, RG_STATUS_NODE,
					ml->cml_members[x].cn_nodeid, 
					(ml->cml_members[x].cn_nodeid &&
					 (ml->cml_members[x].cn_nodeid == 
					  (int)master.m_nodeid)));
		}
	}
	msg_send_simple(ctx, RG_SUCCESS, 0, 0);
	msg_receive(ctx, &hdr, sizeof(hdr), 10);
	free_member_list(ml);
}


/**
  This updates our local membership view and handles whether or not we
  should exit, as well as determines node transitions (thus, calling
  node_event()).
 
  @see				node_event
  @return			0
 */
static int
membership_update(void)
{
	cluster_member_list_t *new_ml = NULL, *node_delta = NULL,
			      *old_membership = NULL;
	int		x;
	int		me = 0;
	cman_handle_t 	h;
	int 		quorate;

	h = cman_init(NULL);
	quorate = cman_is_quorate(h);
	if (!quorate) {
		cman_finish(h);

		if (!rg_quorate())
			return -1;

		logt_print(LOG_EMERG, "#1: Quorum Dissolved\n");
		rg_set_inquorate();
		member_list_update(NULL);/* Clear member list */
		rg_lockall(L_SYS);
		rg_doall(RG_INIT, 1, "Emergency stop of %s\n");
#ifndef USE_OPENAIS
		logt_print(LOG_DEBUG, "Invalidating local VF cache\n");
		vf_invalidate();
#endif
		logt_print(LOG_DEBUG, "Flushing resource group cache\n");
		kill_resource_groups();
		rg_clear_initialized(0);
		return -1;
	} else if (!rg_quorate()) {

		rg_set_quorate();
		rg_unlockall(L_SYS);
		rg_unlockall(L_USER);
		logt_print(LOG_NOTICE, "Quorum Regained\n");
	}

	old_membership = member_list();
	new_ml = get_member_list(h);
	memb_mark_down(new_ml, 0);

	for(x=0; new_ml && x<new_ml->cml_count;x++) {
		if (new_ml->cml_members[x].cn_nodeid == 0) {
		    new_ml->cml_members[x].cn_member = 0;
		}
	}

	for (x = 0; new_ml && x < new_ml->cml_count; x++) {

		if (new_ml->cml_members[x].cn_member == 0) {
			printf("skipping %d - node not member\n",
			       new_ml->cml_members[x].cn_nodeid);
			continue;
		}
		if (new_ml->cml_members[x].cn_nodeid == my_id())
			continue;

#ifdef DEBUG
		printf("Checking for listening status of %d\n",
		       new_ml->cml_members[x].cn_nodeid);
#endif

		do {
			quorate = cman_is_listening(h,
					new_ml->cml_members[x].cn_nodeid,
					port);

			if (quorate == 0) {
				logt_print(LOG_DEBUG, "Node %d is not listening\n",
					new_ml->cml_members[x].cn_nodeid);
				new_ml->cml_members[x].cn_member = 0;
				break;
			} else if (quorate < 0) {
				if (errno == ENOTCONN) {
					new_ml->cml_members[x].cn_member = 0;
					break;
				}
				perror("cman_is_listening");
				usleep(50000);
				continue;
			}
#ifdef DEBUG
		       	else {
				printf("Node %d IS listening\n",
				       new_ml->cml_members[x].cn_nodeid);
			}
#endif
			break;
		} while(1);
	}

	cman_finish(h);
	member_list_update(new_ml);

	/*
	 * Handle nodes lost.  Do our local node event first.
	 */
	node_delta = memb_lost(old_membership, new_ml);

	me = memb_online(node_delta, my_id());
	if (me) {
		/* Should not happen */
		logt_print(LOG_INFO, "State change: LOCAL OFFLINE\n");
		if (node_delta)
			free_member_list(node_delta);
		node_event(1, my_id(), 0, 0);
		/* NOTREACHED */
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		logt_print(LOG_INFO, "State change: %s DOWN\n",
		       node_delta->cml_members[x].cn_name);
		/* Don't bother evaluating anything resource groups are
		   locked.  This is just a performance thing */
		if (!rg_locked()) {
			node_event_q(0, node_delta->cml_members[x].cn_nodeid,
				     0, 0);
		} else {
			logt_print(LOG_DEBUG, "Not taking action - services"
			       " locked\n");
		}
	}

	free_member_list(node_delta);

	/*
	 * Handle nodes gained.  Do our local node event first.
	 */
	node_delta = memb_gained(old_membership, new_ml);

	me = memb_online(node_delta, my_id());
	if (me) {
		logt_print(LOG_INFO, "State change: Local UP\n");
		node_event_q(1, my_id(), 1, 1);
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		if (!node_delta->cml_members[x].cn_member)
			continue;

		if (node_delta->cml_members[x].cn_nodeid == my_id())
			continue;

		logt_print(LOG_INFO, "State change: %s UP\n",
		       node_delta->cml_members[x].cn_name);
		node_event_q(0, node_delta->cml_members[x].cn_nodeid, 1, 1);
	}

	free_member_list(node_delta);
	free_member_list(new_ml);
	free_member_list(old_membership);

	rg_unlockall(L_SYS);

	return 0;
}


static int
lock_commit_cb(char __attribute__ ((unused)) *key,
	       uint64_t __attribute__ ((unused)) viewno,
	       void *data, uint32_t datalen)
{
	char lockstate;

	if (datalen != 1) {
		logt_print(LOG_WARNING, "%s: invalid data length!\n", __FUNCTION__);
		free(data);
		return 0;
	}

       	lockstate = *(char *)data;
	free(data);

	if (lockstate == 0) {
		rg_unlockall(L_USER); /* Doing this multiple times
					 has no effect */
		logt_print(LOG_NOTICE, "Resource Groups Unlocked\n");
		return 0;
	}

	if (lockstate == 1) {
		rg_lockall(L_USER); /* Doing this multiple times
				       has no effect */
		logt_print(LOG_NOTICE, "Resource Groups Locked\n");
		return 0;
	}

	logt_print(LOG_DEBUG, "Invalid lock state in callback: %d\n", lockstate);
	return 0;
}


static void 
do_lockreq(msgctx_t *ctx, int req)
{
	int ret;
	char state;
#ifdef OPENAIS
	msgctx_t everyone;
#else
	cluster_member_list_t *m = member_list();
#endif

	state = (req==RG_LOCK)?1:0;

#ifdef OPENAIS
	ret = ds_write("rg_lockdown", &state, 1);
	logt_print(LOG_INFO, "FIXME: send RG_LOCK update to all!\n");
#else
	ret = vf_write(m, VFF_IGN_CONN_ERRORS, "rg_lockdown", &state, 1);
	free_member_list(m);
#endif

	if (ret == 0) {
		msg_send_simple(ctx, RG_SUCCESS, 0, 0);
	} else {
		msg_send_simple(ctx, RG_FAIL, 0, 0);
	}
}


/**
 * Receive and process a message on a file descriptor and decide what to
 * do with it.  This function doesn't handle messages from the quorum daemon.
 *
 * @param fd		File descriptor with a waiting message.S
 * @return		-1 - failed to receive/handle message, or invalid
 *			data received.  0 - handled message successfully.
 * @see			quorum_msg
 */
static int
dispatch_msg(msgctx_t *ctx, int nodeid, int need_close)
{
	int ret = 0, sz = -1, nid, read_only = 1;
	char msgbuf[4096];
	generic_msg_hdr	*msg_hdr = (generic_msg_hdr *)msgbuf;
	SmMessageSt	*msg_sm = (SmMessageSt *)msgbuf;

	if (ctx->type == MSG_CLUSTER) {
		read_only = 0;
	} else if (ctx->u.local_info.cred.uid == 0) {
		read_only = 0;
	}

	memset(msgbuf, 0, sizeof(msgbuf));

	/* Peek-a-boo */
	sz = msg_receive(ctx, msg_hdr, sizeof(msgbuf), 1);
	if (sz < (int)sizeof (generic_msg_hdr)) {
		logt_print(LOG_ERR,
		       "#37: Error receiving header from %d sz=%d CTX %p\n",
		       nodeid, sz, ctx);
		goto out;
	}

	if (sz < 0)
		return -1;

	if (sz > (int)sizeof(msgbuf)) {
		raise(SIGSTOP);
	}

	/*
	printf("RECEIVED %d %d %d %p\n", sz, (int)sizeof(msgbuf),
	       (int)sizeof(generic_msg_hdr), ctx);
	msg_print(ctx);
	 */

	/* Decode the header */
	swab_generic_msg_hdr(msg_hdr);
	if ((msg_hdr->gh_magic != GENERIC_HDR_MAGIC)) {
		logt_print(LOG_ERR,
		       "#38: Invalid magic: Wanted 0x%08x, got 0x%08x\n",
		       GENERIC_HDR_MAGIC, msg_hdr->gh_magic);
		goto out;
	}

	if ((int)msg_hdr->gh_length != sz) {
		logt_print(LOG_ERR, "#XX: Read size mismatch: %d %d\n",
		       ret, msg_hdr->gh_length);
		goto out;
	}

	switch (msg_hdr->gh_command) {
	case RG_STATUS:
		//logt_print(LOG_DEBUG, "Sending service states to CTX%p\n",ctx);
		if (send_rg_states(ctx, msg_hdr->gh_arg1) == 0)
			need_close = 0;
		break;

	case RG_STATUS_NODE:
		//log_printf(LOG_DEBUG, "Sending node states to CTX%p\n",ctx);
		send_node_states(ctx);
		break;

	case RG_LOCK:
	case RG_UNLOCK:
		if (read_only) {
			msg_send_simple(ctx, RG_FAIL, RG_EPERM, 0);
			goto out;
		}
		if (rg_quorate())
			do_lockreq(ctx, msg_hdr->gh_command);
		break;

	case RG_QUERY_LOCK:
		if (rg_quorate()) {
			ret = (rg_locked() & L_USER) ? RG_LOCK : RG_UNLOCK;
			msg_send_simple(ctx, ret, 0, 0);
		}
		break;

	case RG_ACTION_REQUEST:
		if (read_only) {
			msg_send_simple(ctx, RG_FAIL, RG_EPERM, 0);
			goto out;
		}

		if (sz < (int)sizeof(msg_sm)) {
			logt_print(LOG_ERR,
			       "#39: Error receiving entire request (%d/%d)\n",
			       ret, (int)sizeof(msg_sm));
			ret = -1;
			goto out;
		}

		/* XXX perf: reencode header */
		swab_generic_msg_hdr(msg_hdr);
		/* Decode SmMessageSt message */
		swab_SmMessageSt(msg_sm);

		if (!svc_exists(msg_sm->sm_data.d_svcName)) {
			msg_sm->sm_data.d_ret = RG_ENOSERVICE;
			/* No such service! */
			swab_SmMessageSt(msg_sm);

			if (msg_send(ctx, msg_sm, sizeof (SmMessageSt)) <
		    	    (int)sizeof (SmMessageSt))
				logt_print(LOG_ERR, "#40: Error replying to "
				       "action request.\n");
			ret = -1;
			goto out;
		}

		if (central_events_enabled() &&
		    msg_sm->sm_hdr.gh_arg1 != RG_ACTION_MASTER) {
			
			/* Centralized processing or request is from
			   clusvcadm */
			nid = event_master();
			if (nid != my_id()) {
				/* Forward the message to the event master */
				forward_message(ctx, msg_sm, nid);
			} else {
				/* for us: queue it */
				user_event_q(msg_sm->sm_data.d_svcName,
					     msg_sm->sm_data.d_action,
					     msg_sm->sm_hdr.gh_arg1,
					     msg_sm->sm_hdr.gh_arg2,
					     msg_sm->sm_data.d_svcOwner,
					     ctx);
			}

			return 0;
		}

		/* Distributed processing and/or request is from master node
		   -- Queue request */
		if (rt_enqueue_request(msg_sm->sm_data.d_svcName,
		      		       msg_sm->sm_data.d_action,
		      		       ctx, 0, msg_sm->sm_data.d_svcOwner,
		      		       msg_sm->sm_hdr.gh_arg1,
		      		       msg_sm->sm_hdr.gh_arg2) != 0) {

			/* Clean up this context if we fail to 
			 * queue the request. */
			send_ret(ctx, msg_sm->sm_data.d_svcName,
				 RG_EAGAIN, msg_sm->sm_data.d_action, 0);
			need_close = 1;
			ret = 0;
			goto out;
		}
		return 0;

	case RG_EVENT:
		if (read_only) {
			msg_send_simple(ctx, RG_FAIL, RG_EPERM, 0);
			goto out;
		}

		/* Service event.  Run a dependency check */
		if (sz < (int)sizeof(msg_sm)) {
			logt_print(LOG_ERR,
			       "#39: Error receiving entire request (%d/%d)\n",
			       ret, (int)sizeof(msg_sm));
			ret = -1;
			goto out;
		}

		/* XXX perf: reencode header */
		swab_generic_msg_hdr(msg_hdr);
		/* Decode SmMessageSt message */
		swab_SmMessageSt(msg_sm);

		/* Send to our rg event handler */
		rg_event_q(msg_sm->sm_data.d_svcName,
			   msg_sm->sm_data.d_action,
			   msg_sm->sm_hdr.gh_arg1,
			   msg_sm->sm_hdr.gh_arg2);
		break;

	case RG_EXITING:
		if (read_only) {
			msg_send_simple(ctx, RG_FAIL, RG_EPERM, 0);
			goto out;
		}

		if (!member_online(msg_hdr->gh_arg1))
			break;

		logt_print(LOG_NOTICE, "Member %d shutting down\n",
		       msg_hdr->gh_arg1);
	       	member_set_state(msg_hdr->gh_arg1, 0);
		node_event_q(0, msg_hdr->gh_arg1, 0, 1);
		break;

	case VF_MESSAGE:
		if (read_only) {
			msg_send_simple(ctx, RG_FAIL, RG_EPERM, 0);
			goto out;
		}

		/* Ignore; our VF thread handles these
		    - except for VF_CURRENT XXX (bad design) */
		if (msg_hdr->gh_arg1 == VF_CURRENT)
			vf_process_msg(ctx, 0, msg_hdr, sz);
		break;

	default:
		if (read_only) {
			goto out;
		}

		logt_print(LOG_DEBUG, "unhandled message request %d\n",
		       msg_hdr->gh_command);
		break;
	}
out:
	if (need_close) {
		msg_close(ctx);
		msg_free_ctx(ctx);
	}
	return ret;
}

/**
  Grab an event off of the designated context

  @param fd		File descriptor to check
  @return		Event
 */
static int
handle_cluster_event(msgctx_t *ctx)
{
	int ret;
	msgctx_t *newctx;
	int nodeid;
	
	ret = msg_wait(ctx, 0);

	switch(ret) {
	case M_PORTOPENED:
		msg_receive(ctx, NULL, 0, 0);
		logt_print(LOG_DEBUG, "Event: Port Opened\n");
		membership_update();
		break;
	case M_PORTCLOSED:
		/* Might want to handle powerclosed like membership change */
		msg_receive(ctx, NULL, 0, 0);
		logt_print(LOG_DEBUG, "Event: Port Closed\n");
		membership_update();
		break;
	case M_NONE:
		msg_receive(ctx, NULL, 0, 0);
		logt_print(LOG_DEBUG, "NULL cluster message\n");
		break;
	case M_OPEN:
		newctx = msg_new_ctx();
		if (msg_accept(ctx, newctx) >= 0 &&
		    rg_quorate()) {
			/* Handle message */
			/* When request completes, the fd is closed */
			nodeid = msg_get_nodeid(newctx);
			dispatch_msg(newctx, nodeid, 1);
			break;
		}
		break;

	case M_DATA:
		nodeid = msg_get_nodeid(ctx);
		dispatch_msg(ctx, nodeid, 0);
		break;
		
	case M_OPEN_ACK:
	case M_CLOSE:
		logt_print(LOG_DEBUG, "I should NOT get here: %d\n",
		       ret);
		break;
	case M_STATECHANGE:
		msg_receive(ctx, NULL, 0, 0);
		logt_print(LOG_DEBUG, "Membership Change Event\n");
		if (running) {
			rg_unlockall(L_SYS);
			membership_update();
		}
		break;
	case M_TRY_SHUTDOWN:
		msg_receive(ctx, NULL, 0, 0);
		logt_print(LOG_WARNING, "#67: Shutting down uncleanly\n");
		rg_set_inquorate();
		rg_doall(RG_INIT, 1, "Emergency stop of %s");
		rg_clear_initialized(0);
#if defined(LIBCMAN_VERSION) && LIBCMAN_VERSION >= 2
		/* cman_replyto_shutdown() */
#endif
		running = 0;
		break;
	case M_CONFIG_UPDATE:
		msg_receive(ctx, NULL, 0, 0);
		need_reconfigure = 1;
		break;
	}

	return ret;
}


void dump_threads(FILE *fp);
void dump_config_version(FILE *fp);
void dump_vf_states(FILE *fp);
void dump_cluster_ctx(FILE *fp);
void dump_resource_info(FILE *fp);


static void
dump_internal_state(const char *loc)
{
	FILE *fp;
	fp=fopen(loc, "w+");
 	dump_config_version(fp);
 	dump_threads(fp);
 	dump_vf_states(fp);
#ifdef WRAP_THREADS
	dump_thread_states(fp);
#endif
	dump_cluster_ctx(fp);
	dump_resource_info(fp);
 	fclose(fp);
}

static int
event_loop(msgctx_t *localctx, msgctx_t *clusterctx)
{
 	int n = 0, max, ret;
	fd_set rfds;
	msgctx_t *newctx;
	struct timeval tv;
	int nodeid;

	tv.tv_sec = status_poll_interval;
	tv.tv_usec = 0;

	if (signalled) {
		signalled = 0;

		dump_internal_state("/var/lib/cluster/rgmanager-dump");
	}

	while (running && (tv.tv_sec || tv.tv_usec)) {
		FD_ZERO(&rfds);
		max = -1;
		msg_fd_set(clusterctx, &rfds, &max);
		msg_fd_set(localctx, &rfds, &max);

		n = select(max + 1, &rfds, NULL, NULL, &tv);

		if (n <= 0)
			break;

		if (msg_fd_isset(clusterctx, &rfds)) {
			msg_fd_clr(clusterctx, &rfds);
			handle_cluster_event(clusterctx);
			if (need_reconfigure)
				break;
			continue;
		}

		if (!msg_fd_isset(localctx, &rfds)) {
			continue;
		}

		msg_fd_clr(localctx, &rfds);
		newctx = msg_new_ctx();
		ret = msg_accept(localctx, newctx);

		if (ret == -1)
			continue;

		if (rg_quorate()) {
			/* Handle message */
			/* When request completes, the fd is closed */
			nodeid = msg_get_nodeid(newctx);
			dispatch_msg(newctx, nodeid, 1);
			continue;
		}
			
		if (!rg_initialized()) {
			msg_send_simple(newctx, RG_FAIL, RG_EQUORUM, 0);
			msg_close(newctx);
			msg_free_ctx(newctx);
			continue;
		}

		if (!rg_quorate()) {
			printf("Dropping connect: NO QUORUM\n");
			msg_send_simple(newctx, RG_FAIL, RG_EQUORUM, 0);
			msg_close(newctx);
			msg_free_ctx(newctx);
		}
	}

	if (!running)
		return 0;

	if (need_reconfigure) {
		need_reconfigure = 0;
		configure_rgmanager(-1, 0, NULL);
		config_event_q();
		return 0;
	}

	/* Did we receive a SIGTERM? */
	if (n < 0)
		return 0;

	/* No new messages.  Drop in the status check requests.  */
	if (n == 0 && rg_quorate()) {
		do_status_checks();
		return 0;
	}

	return 0;
}


void
flag_shutdown(int __attribute__ ((unused)) sig)
{
	shutdown_pending = 1;
}


static void
cleanup(msgctx_t *clusterctx)
{
	kill_resource_groups();
	send_exit_msg(clusterctx);
}



static void
statedump(int __attribute__ ((unused)) sig)
{
	signalled++;
}


/*
 * Configure logging based on data in cluster.conf
 */
static int
configure_rgmanager(int ccsfd, int dbg, int *token_secs)
{
	char *v;
	char internal = 0;
	int status_child_max = 0;
	int tmp;

	if (ccsfd < 0) {
		internal = 1;
		ccsfd = ccs_connect();
		if (ccsfd < 0)
			return -1;
	}

	setup_logging(ccsfd);

	if (token_secs && ccs_get(ccsfd, "/cluster/totem/@token", &v) == 0) {
		tmp = atoi(v);
		if (tmp >= 1000) {
			*token_secs = tmp / 1000;
			if (tmp % 1000)
				++(*token_secs);
		}
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@transition_throttling", &v) == 0) {
		set_transition_throttling(atoi(v));
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@central_processing", &v) == 0) {
		set_central_events(atoi(v));
		if (atoi(v))
			logt_print(LOG_NOTICE,
			       "Centralized Event Processing enabled\n");
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@status_poll_interval", &v) == 0) {
		status_poll_interval = atoi(v);
		if (status_poll_interval >= 1) {
			logt_print(LOG_NOTICE,
			       "Status Polling Interval set to %d\n",
			       status_poll_interval);
		} else {
			logt_print(LOG_WARNING, "Ignoring illegal "
			       "status_poll_interval of %s\n", v);
			status_poll_interval = DEFAULT_CHECK_INTERVAL;
		}
		
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@status_child_max", &v) == 0) {
		status_child_max = atoi(v);
		if (status_child_max >= 1) {
			logt_print(LOG_NOTICE,
			       "Status Child Max set to %d\n",
			       status_child_max);
			rg_set_childmax(status_child_max);
		} else {
			logt_print(LOG_WARNING, "Ignoring illegal "
			       "status_child_max of %s\n", v);
		}
		
		free(v);
	}

	if (internal)
		ccs_disconnect(ccsfd);

	return 0;
}


static int
cman_connect(cman_handle_t *ch)
{
	if (!ch)
		exit(1);

	*ch = cman_init(NULL);
	if (!(*ch)) {
		logt_print(LOG_NOTICE, "Waiting for CMAN to start\n");

		while (!(*ch = cman_init(NULL))) {
			sleep(1);
			if (shutdown_pending)
				return 1;
		}
	}

        if (!cman_is_quorate(*ch)) {
		/*
		   There are two ways to do this; this happens to be the simpler
		   of the two.  The other method is to join with a NULL group 
		   and log in -- this will cause the plugin to not select any
		   node group (if any exist).
		 */
		logt_print(LOG_NOTICE, "Waiting for quorum to form\n");

		while (cman_is_quorate(*ch) == 0) {
			sleep(1);
			if (shutdown_pending)
				return 1;
		}
		logt_print(LOG_NOTICE, "Quorum formed\n");
	}

	return 0;
}


static int
wait_for_fencing(void)
{
        if (node_has_fencing(my_id()) && !fence_domain_joined()) {
		logt_print(LOG_INFO, "Waiting for fence domain join operation "
		       "to complete\n");

		while (fence_domain_joined() == 0) {
			if (shutdown_pending)
				return 1;
			sleep(1);
		}
		logt_print(LOG_INFO, "Fence domain joined\n");
	} else {
		logt_print(LOG_DEBUG, "Fence domain already joined "
		       "or no fencing configured\n");
	}

	return 0;
}


static void *
shutdown_thread(void __attribute__ ((unused)) *arg)
{
	rg_lockall(L_SYS|L_SHUTDOWN);
	rg_doall(RG_STOP_EXITING, 1, NULL);
	running = 0;

	pthread_exit(NULL);
}


#ifdef WRAP_THREADS
void dump_thread_states(FILE *);
#endif
int
main(int argc, char **argv)
{
	int rv, do_init = 1;
	char foreground = 0, wd = 1;
	cman_node_t me;
	msgctx_t *cluster_ctx;
	msgctx_t *local_ctx;
	pthread_t th;
	cman_handle_t clu = NULL;
	int cluster_timeout = 10;

	while ((rv = getopt(argc, argv, "wfdN")) != EOF) {
		switch (rv) {
		case 'w':
			wd = 0;
			break;
		case 'd':
			debug = 1;
			break;
		case 'N':
			do_init = 0;
			break;
		case 'f':
			foreground = 1;
			break;
		default:
			return 1;
			break;
		}
	}

	if (getenv("RGMANAGER_DEBUG")) {
		debug = 1;
	}

	if (!foreground && (geteuid() == 0)) {
		daemon_init(argv[0]);
		if (wd && !debug && !watchdog_init())
			logt_print(LOG_NOTICE, "Failed to start watchdog\n");
	}

	setup_signal(SIGINT, flag_shutdown);
	setup_signal(SIGTERM, flag_shutdown);
	setup_signal(SIGUSR1, statedump);
	unblock_signal(SIGCHLD);
	setup_signal(SIGPIPE, SIG_IGN);

	if (debug) {
		setup_signal(SIGSEGV, segfault);
	} else {
		unblock_signal(SIGSEGV);
	}

	init_logging(NULL, foreground, (debug? LOG_DEBUG : SYSLOGLEVEL));

	rv = -1;
	if (cman_connect(&clu) != 0)
		goto out;	/* Clean exit if sigint/sigterm here */

	if (cman_init_subsys(clu) < 0) {
		perror("cman_init_subsys");
		return -1;
	}

	if (clu_lock_init(rgmanager_lsname) != 0) {
		printf("Locks not working!\n");
		cman_finish(clu);
		return -1;
	}

	memset(&me, 0, sizeof(me));
        cman_get_node(clu, CMAN_NODEID_US, &me);

	if (me.cn_nodeid == 0) {
		printf("Unable to determine local node ID\n");
		perror("cman_get_node");
		goto out_ls;
	}
	set_my_id(me.cn_nodeid);

	logt_print(LOG_INFO, "I am node #%d\n", my_id());

	if (wait_for_fencing() != 0) {
		rv = 0;
		goto out_ls;
	}

	/*
	   We know we're quorate.  At this point, we need to
	   read the resource group trees from ccsd.
	 */
	configure_rgmanager(-1, debug, &cluster_timeout);
	logt_print(LOG_NOTICE, "Resource Group Manager Starting\n");

	if (init_resource_groups(0, do_init) != 0) {
		logt_print(LOG_CRIT, "#8: Couldn't initialize services\n");
		goto out_ls;
	}

	if (shutdown_pending) {
		rv = 0;
		goto out_ls;
	}

	if (msg_listen(MSG_SOCKET, RGMGR_SOCK, me.cn_nodeid, &local_ctx) < 0) {
		logt_print(LOG_CRIT,
		       "#10: Couldn't set up cluster message system: %s\n",
		       strerror(errno));
		goto out_ls;
	}

	if (msg_listen(MSG_CLUSTER, &port, me.cn_nodeid, &cluster_ctx) < 0) {
		logt_print(LOG_CRIT,
		       "#10b: Couldn't set up cluster message system: %s\n",
		       strerror(errno));
		goto out_ls;
	}

	rg_set_quorate();

	/*
	msg_print(local_ctx);
	msg_print(cluster_ctx);
	 */

	/*
	   Initialize the VF stuff.
	 */
#ifdef OPENAIS
	if (ds_init() < 0) {
		logt_print(LOG_CRIT, "#11b: Couldn't initialize SAI AIS CKPT\n");
		goto out_ls;
	}

	ds_key_init("rg_lockdown", 32, 10);
#else
	if (vf_init(me.cn_nodeid, port, NULL, NULL, cluster_timeout) != 0) {
		logt_print(LOG_CRIT, "#11: Couldn't set up VF listen socket\n");
		goto out_ls;
	}

	vf_key_init("rg_lockdown", 10, NULL, lock_commit_cb);
	vf_key_init("Transition-Master", 10, NULL, master_event_callback);
#endif

	/*
	   Do everything useful
	 */
	while (running) {
		event_loop(local_ctx, cluster_ctx);

		if (shutdown_pending == 1) {
			/* Kill local socket; local requests need to
			   be ignored here */
			msg_close(local_ctx);
			++shutdown_pending;
			logt_print(LOG_NOTICE, "Shutting down\n");
			pthread_create(&th, NULL, shutdown_thread, NULL);
		}
	}

	if (rg_initialized())
		cleanup(cluster_ctx);
	rv = 0;
out_ls:
	clu_lock_finished(rgmanager_lsname);

out:
	logt_print(LOG_NOTICE, "Shutdown complete, exiting\n");
	cman_finish(clu);
	
	close_logging();
	/*malloc_stats();*/

	daemon_cleanup();
	exit(rv);
}
