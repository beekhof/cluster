#ifndef _FO_DOMAIN_H
#define _FO_DOMAIN_H

/*
 * Fail-over domain states
 */
#define FOD_ILLEGAL		0
#define FOD_GOOD		1
#define FOD_BETTER		2
#define FOD_BEST		3

/* 
   Fail-over domain flags
 */
#define FOD_ORDERED		(1<<0)
#define FOD_RESTRICTED		(1<<1)
#define FOD_NOFAILBACK		(1<<2)

 
typedef struct _fod_node {
	list_head();
	char	*fdn_name;
	int	fdn_prio;
	int	fdn_nodeid;
} fod_node_t;

typedef struct _fod {
	list_head();
	char	*fd_name;
	fod_node_t	*fd_nodes;
	int	fd_flags;
	int	_pad_; /* align */
} fod_t;

 
/*
   Construct/deconstruct failover domains
 */
int construct_domains(int ccsfd, fod_t **domains);
void deconstruct_domains(fod_t **domains);
void print_domains(fod_t **domains);
void dump_domains(FILE *fp, fod_t **domains);
int node_should_start(int nodeid, cluster_member_list_t *membership,
	      	      const char *rg_name, fod_t **domains);
int node_domain_set(fod_t **domains, char *name, int **ret,
		    int *retlen, int *flags);
int node_domain_set_safe(char *domainname, int **ret, int *retlen, int *flags);

#endif
