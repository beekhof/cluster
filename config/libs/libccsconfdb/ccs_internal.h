#ifndef __CCS_INTERNAL_DOT_H__
#define __CCS_INTERNAL_DOT_H__

/* NOTE: use __attribute__ to hide the internal API */

/* from libccs.c */
void reset_iterator(confdb_handle_t handle, hdb_handle_t connection_handle)
    __attribute__ ((visibility("hidden")));
int get_previous_query(confdb_handle_t handle, hdb_handle_t connection_handle,
		       char *previous_query, hdb_handle_t *query_handle)
    __attribute__ ((visibility("hidden")));
int set_previous_query(confdb_handle_t handle, hdb_handle_t connection_handle,
		       const char *previous_query, hdb_handle_t query_handle)
    __attribute__ ((visibility("hidden")));

/* from xpathlite.c */
char *_ccs_get_xpathlite(confdb_handle_t handle, hdb_handle_t connection_handle,
			 const char *query, int list)
    __attribute__ ((visibility("hidden")));

/* from fullxpath.c */
char *_ccs_get_fullxpath(confdb_handle_t handle, hdb_handle_t connection_handle,
			 const char *query, int list)
    __attribute__ ((visibility("hidden")));
int xpathfull_init(confdb_handle_t handle)
    __attribute__ ((visibility("hidden")));
void xpathfull_finish(void) __attribute__ ((visibility("hidden")));

#endif /*  __CCS_INTERNAL_DOT_H__ */
