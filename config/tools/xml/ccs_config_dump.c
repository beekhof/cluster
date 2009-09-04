#include <stdio.h>
#include <limits.h>
#include <corosync/corotypes.h>
#include <corosync/confdb.h>

static confdb_callbacks_t callbacks = {};

static int dump_objdb_buff(confdb_handle_t dump_handle, hdb_handle_t cluster_handle,
			   hdb_handle_t parent_object_handle, int level)
{
	hdb_handle_t object_handle;
	char object_name[PATH_MAX], key_name[PATH_MAX], key_value[PATH_MAX];
	size_t key_value_len = 0, key_name_len = 0, object_name_len = 0;
	int current_level = level+1;
	int has_childs = 0;

	if (confdb_key_iter_start(dump_handle, parent_object_handle) != CS_OK)
		return -1;

	while (confdb_key_iter(dump_handle, parent_object_handle, key_name,
				&key_name_len, key_value,
				&key_value_len) == CS_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';
		printf(" %s=\"%s\"", key_name, key_value);
	}

	if (confdb_object_iter_start(dump_handle, parent_object_handle) != CS_OK)
		return -1;

	while (confdb_object_iter(dump_handle, parent_object_handle,
				   &object_handle, object_name,
				   &object_name_len) == CS_OK) {
		hdb_handle_t parent;
		int i;
		int found_childs;

		if ((!has_childs) && (parent_object_handle > 0))
			printf(">\n");

		has_childs = 1;

		if (confdb_object_parent_get(dump_handle, object_handle, &parent) != CS_OK)
			return -1;

		object_name[object_name_len] = '\0';
		for (i=0; i<current_level; i++) {
			printf("\t");
		}
		printf("<%s", object_name);

		found_childs = dump_objdb_buff(dump_handle, cluster_handle, object_handle, current_level);
		if (found_childs < 0)
			return -1;

		if ((object_handle != parent_object_handle) && (found_childs)) {
			for (i=0; i<current_level; i++) {
				printf("\t");
			}
			printf("</%s>\n", object_name);
		}
	}

	if(!has_childs)
		printf("/>\n");

	return has_childs;
}

int main(void) {
	confdb_handle_t handle = 0;
	hdb_handle_t cluster_handle;

	if (confdb_initialize(&handle, &callbacks) != CS_OK)
		return -1;

	if (confdb_object_find_start(handle, OBJECT_PARENT_HANDLE) != CS_OK)
		return -1;

	if (confdb_object_find(handle, OBJECT_PARENT_HANDLE, "cluster", strlen("cluster"), &cluster_handle) != CS_OK)
		return -1;

	printf("<?xml version=\"1.0\"?>\n<cluster");

	if (dump_objdb_buff(handle, cluster_handle, cluster_handle, 0) < 0)
		return -1;

	printf("</cluster>\n");

	if (confdb_finalize(handle) != CS_OK)
		return -1;

	return 0;
}
