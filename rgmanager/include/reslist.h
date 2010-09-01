#ifndef _RESLIST_H
#define _RESLIST_H

#include <stdint.h>
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xpath.h>


#define RA_PRIMARY	(1<<0)	/** Primary key */
#define RA_UNIQUE	(1<<1)	/** Unique for given type */
#define RA_REQUIRED	(1<<2)	/** Required (or an error if not present */
#define RA_INHERIT	(1<<3)	/** Inherit a parent resource's attr */
#define RA_RECONFIG	(1<<4)	/** Allow inline reconfiguration */

#define RF_INLINE	(1<<0)
#define RF_DEFINED	(1<<1)
#define RF_NEEDSTART	(1<<2)	/** Used when adding/changing resources */
#define RF_NEEDSTOP	(1<<3)  /** Used when deleting/changing resources */
#define RF_COMMON	(1<<4)	/** " */
#define RF_INDEPENDENT	(1<<5)  /** Define this for a resource if it is
				  otherwise an independent subtree */
#define RF_RECONFIG	(1<<6)

#define RF_INIT		(1<<7)	/** Resource rule: Initialize this resource
				  class on startup */
#define RF_DESTROY	(1<<8)	/** Resource rule flag: Destroy this
				  resource class if you delete it from
				  the configuration */
#define RF_ENFORCE_TIMEOUTS (1<<9) /** Enforce timeouts for this node */
#define RF_NON_CRITICAL (1<<10) /** stop this resource if it fails */
#define RF_QUIESCE	(1<<11) /** don't restart this resource */



#define RES_STOPPED	(0)
#define RES_STARTED	(1)
#define RES_FAILED	(2)
#define RES_DISABLED	(3)

#ifndef SHAREDIR
#define SHAREDIR		"/usr/share/rgmanager"
#endif

#define RESOURCE_ROOTDIR	SHAREDIR
#define RESOURCE_TREE_ROOT	"/cluster/rm"
#define RESOURCE_BASE		RESOURCE_TREE_ROOT "/resources"
#define RESOURCE_DEFAULTS	RESOURCE_TREE_ROOT "/resource-defaults"
#define RESOURCE_ROOT_FMT 	RESOURCE_TREE_ROOT "/%s[%d]"

#define RESOURCE_MAX_LEVELS	100

/* Include OCF definitions */
#include <res-ocf.h>


typedef struct _resource_attribute {
	char	*ra_name;
	char	*ra_value;
	int	ra_flags;
	int	_pad_;
} resource_attr_t;


typedef struct _resource_child {
	char	*rc_name;
	int	rc_startlevel;
	int	rc_stoplevel;
	int	rc_forbid;
	int	rc_flags;
} resource_child_t;


typedef struct _resource_act {
	char	*ra_name;
	time_t	ra_timeout;
	time_t	ra_last;
	time_t	ra_interval;
	int	ra_depth;
	int	ra_flags;
} resource_act_t;


typedef struct _resource_rule {
	list_head();
	char *	rr_type;
	char *	rr_agent;
	char *	rr_version;	/** agent XML spec version; OCF-ism */
	int	rr_flags;
	int	rr_maxrefs;
	resource_attr_t *	rr_attrs;
	resource_child_t *	rr_childtypes;
	resource_act_t *	rr_actions;
} resource_rule_t;


typedef struct _resource {
	list_head();
	pthread_mutex_t		r_mutex;
	resource_rule_t *	r_rule;
	char *	r_name;
	resource_attr_t *	r_attrs;
	resource_act_t *	r_actions;
	int	r_flags;
	int	r_refs;
	int	r_incarnations;	/** Number of instances running locally */
	int	_pad_; /* align */
} resource_t;


typedef struct _rg_node {
	list_head();
	struct _rg_node	*rn_child, *rn_parent;
	resource_t	*rn_resource;
	resource_act_t	*rn_actions;
	restart_counter_t rn_restart_counter;
	restart_counter_t rn_failure_counter;
	int	rn_state; /* State of this instance of rn_resource */
	int	rn_flags;
	int	rn_last_status;
	int 	rn_last_depth;
	int	rn_checked;
	int	rn_pad;
} resource_node_t;


/*
   Exported Functions
 */
int res_start(resource_node_t **tree, resource_t *res, void *ret);
int res_stop(resource_node_t **tree, resource_t *res, void *ret);
int res_status(resource_node_t **tree, resource_t *res, void *ret);
int res_status_inquiry(resource_node_t **tree, resource_t *res, void *ret);
int res_convalesce(resource_node_t **tree, resource_t *res, void *ret);
int res_condstart(resource_node_t **tree, resource_t *res, void *ret);
int res_condstop(resource_node_t **tree, resource_t *res, void *ret);
int res_exec(resource_node_t *node, int op, const char *arg, int depth);
/*int res_resinfo(resource_node_t **tree, resource_t *res, void *ret);*/
int expand_time(char *val);
int store_action(resource_act_t **actsp, char *name, int depth, int timeout, int interval);


/*
   Calculate differences
 */
int resource_delta(resource_t **leftres, resource_t **rightres);
int resource_tree_delta(resource_node_t **, resource_node_t **);


/*
   Load/kill resource rule sets
 */
int load_resource_rules(const char *rpath, resource_rule_t **rules);
int load_resource_defaults(int ccsfd, resource_rule_t **rules);
void print_resource_rule(FILE *fp, resource_rule_t *rule);
void print_resource_rules(resource_rule_t **rules);
void dump_resource_rules(FILE *fp, resource_rule_t **rules);
void destroy_resource_rules(resource_rule_t **rules);

/*
   Load/kill resource sets
 */
int load_resources(int ccsfd, resource_t **reslist, resource_rule_t **rulelist);
void print_resource(FILE *fp, resource_t *res);
void print_resources(resource_t **reslist);
void dump_resources(FILE *fp, resource_t **reslist);
void destroy_resources(resource_t **list);

/*
   Construct/deconstruct resource trees
 */
int build_resource_tree(int ccsfd, resource_node_t **tree,
			resource_rule_t **rulelist, resource_t **reslist);
void print_resource_tree(resource_node_t **tree);
void dump_resource_tree(FILE *fp, resource_node_t **tree);
void destroy_resource_tree(resource_node_t **tree);

void *act_dup(resource_act_t *acts);
void dump_resource_info(FILE *fp);


/*
   Handy functions
 */
resource_t *find_resource_by_ref(resource_t **reslist, const char *type,
				 const char *ref);
resource_t *find_root_by_ref(resource_t **reslist, const char *ref);
resource_rule_t *find_rule_by_type(resource_rule_t **rulelist,
				   const char *type);
void res_build_name(char *, size_t, resource_t *);

/*
   Internal functions; shouldn't be needed.
 */
char *xpath_get_one(xmlDocPtr doc, xmlXPathContextPtr ctx,
			  const char *query);
int store_attribute(resource_attr_t **attrsp, char *name, char *value,
		    int flags);

resource_t *load_resource(int ccsfd, resource_rule_t *rule, char *base);
int store_resource(resource_t **reslist, resource_t *newres);
void destroy_resource(resource_t *res);

const char *attr_value(resource_node_t *node, const char *attrname);
const char *rg_attr_value(resource_node_t *node, const char *attrname);
const char *res_attr_value(resource_t *res, const char *attrname);
const char *primary_attr_value(resource_t *);
int rescmp(resource_t *l, resource_t *r);

#endif /* _RESLIST_H */
