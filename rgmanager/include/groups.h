#ifndef _GROUPS_H
#define _GROUPS_H

int node_should_start_safe(uint32_t, cluster_member_list_t *, const char *);
int group_property(const char *groupname, const char *property,
		   char *ret_val, size_t len);
int count_resource_groups(cluster_member_list_t *ml);
int count_resource_groups_local(cman_node_t *mp);
int is_exclusive(const char *svcName);
int have_exclusive_resources(void);
int check_exclusive_resources(cluster_member_list_t *membership,
			      const char *svcName);
int check_depend(resource_t *res);

void dump_config_version(FILE *fp);
int init_resource_groups(int reconfigure, int do_init);
void get_recovery_policy(const char *rg_name, char *buf, size_t buflen);
int get_service_property(const char *rg_name, const char *prop,
			 char *buf, size_t buflen);

int add_restart(const char *rg_name);
int check_restart(const char *rg_name);
void kill_resource_groups(void);

/* do this op on all resource groups.  The handler for the request 
   will sort out whether or not it's a valid request given the state */
void rg_doall(int request, int block, const char *debugfmt);
void do_status_checks(void); /* Queue status checks for locally running
				services */

int svc_exists(const char *svcname);
int send_rg_states(msgctx_t *ctx, int fast);

int check_depend_safe(const char *servicename);
int group_migratory(const char *servicename, int lock);
int group_event(const char *rg_name, uint32_t state, int owner);

char **get_service_names(int *len);



#endif
