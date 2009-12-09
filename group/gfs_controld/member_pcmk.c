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
#include <pacemaker/crm/ais.h>

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
    /* cluster_quorate = crm_have_quorum; */
}

void update_cluster(void)
{
}

int setup_cluster(void)
{
    ais_fd_async = -1;
    crm_log_init("cluster-gfs", LOG_DEBUG, FALSE, TRUE, 0, NULL);
    
    if(init_ais_connection(NULL, NULL, NULL, NULL, &our_nodeid) == FALSE) {
        log_error("Connection to our AIS plugin failed");
        return -1;
    }

    if(crm_get_cluster_name(&clustername) == FALSE) {
        log_error("No cluster name supplied");
	return -1;

    } else {
        log_debug("Connected as node %u to cluster '%s'", our_nodeid, clustername);
    }
    
    /* Sign up for membership updates */
    send_ais_text(crm_class_notify, "true", TRUE, NULL, crm_msg_ais);
    
    /* Requesting the current list of known nodes */
    send_ais_text(crm_class_members, __FUNCTION__, TRUE, NULL, crm_msg_ais);

    return ais_fd_async;
}

void close_cluster(void)
{
    terminate_ais_connection();
}

#include <../../fence/libfenced/libfenced.h>
int fenced_node_info(int nodeid, struct fenced_node *node) 
{
    /* The caller, we_are_in_fence_domain(), wants to check
     * if we're part of the fencing domain.
     * Check the node is alive and synthesize the
     * appropriate value for .member
     */

    crm_node_t *pnode = crm_get_peer(nodeid, NULL);
    node->member = crm_is_member_active(pnode);
    return 0;
}
