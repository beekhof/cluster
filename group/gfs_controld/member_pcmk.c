#include "gfs_daemon.h"
#include "config.h"

#include <glib.h>
#include <bzlib.h>
#include <heartbeat/ha_msg.h>

#include <pacemaker/crm_config.h>

#include <pacemaker/crm/crm.h>
#include <pacemaker/crm/ais.h>
#include <pacemaker/crm/attrd.h>
/* heartbeat support is irrelevant here */
#undef SUPPORT_HEARTBEAT 
#define SUPPORT_HEARTBEAT 0
#include <pacemaker/crm/common/cluster.h>
#include <pacemaker/crm/common/stack.h>
#include <pacemaker/crm/common/ipc.h>
#include <pacemaker/crm/msg_xml.h>
#include <pacemaker/crm/cib.h>

extern int ais_fd_async;

static int pcmk_cluster_fd = 0;
static void attrd_deadfn(int ci) 
{
    log_error("%s: Lost connection to the cluster", __FUNCTION__);
    pcmk_cluster_fd = 0;
    return;
}

void kick_node_from_cluster(int nodeid)
{
    int fd = pcmk_cluster_fd;
    int rc = crm_terminate_member_no_mainloop(nodeid, NULL, &fd);
    
    if(fd > 0 && fd != pcmk_cluster_fd) {
	pcmk_cluster_fd = fd;
	client_add(pcmk_cluster_fd, NULL, attrd_deadfn);
    }
    
    switch(rc) {
	case 1:
	    log_debug("Requested that node %d be kicked from the cluster", nodeid);
	    break;
	case -1:
	    log_error("Don't know how to kick node %d from the cluster", nodeid);
	    break;
	case 0:
	    log_error("Could not kick node %d from the cluster", nodeid);
	    break;
	default:
	    log_error("Unknown result when kicking node %d from the cluster", nodeid);
	    break;
    }
    return;
}

void process_cluster(int ci)
{
    ais_dispatch(ais_fd_async, NULL);
    cluster_quorate = crm_have_quorum;
}

int setup_cluster(void)
{
    /* To avoid creating an additional place for the dlm to be configured,
     * only allow configuration from the command-line until CoroSync is stable
     * enough to be used with Pacemaker
     */
    return 0;
}

void close_cluster(void)
{
    terminate_ais_connection();
}

#include <../../fence/libfenced/libfenced.h>
int fenced_node_info(int nodeid, struct fenced_node *node) 
{
  /* Not implemented */
  return -1;
}
