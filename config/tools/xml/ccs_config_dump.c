#include <stdio.h>
#include <limits.h>
#include <corosync/corotypes.h>
#include <corosync/confdb.h>

static confdb_callbacks_t callbacks = {};

static int dump_objdb_buff(confdb_handle_t dump_handle, hdb_handle_t cluster_handle,
			   hdb_handle_t parent_object_handle)
{
	hdb_handle_t object_handle;
	char object_name[PATH_MAX], key_name[PATH_MAX], key_value[PATH_MAX];
	size_t key_value_len = 0, key_name_len = 0, object_name_len = 0;

	if (confdb_key_iter_start(dump_handle, parent_object_handle) != CS_OK)
		return -1;

	while (confdb_key_iter(dump_handle, parent_object_handle, key_name,
				&key_name_len, key_value,
				&key_value_len) == CS_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';
		printf(" %s=\"%s\"", key_name, key_value);
	}

	if (parent_object_handle > 0)
		printf(">\n");

	if (confdb_object_iter_start(dump_handle, parent_object_handle) != CS_OK)
		return -1;

	while (confdb_object_iter(dump_handle, parent_object_handle,
				   &object_handle, object_name,
				   &object_name_len) == CS_OK) {
		hdb_handle_t parent;

		if (confdb_object_parent_get(dump_handle, object_handle, &parent) != CS_OK)
			return -1;

		object_name[object_name_len] = '\0';
		printf("<%s", object_name);

		if(dump_objdb_buff(dump_handle, cluster_handle, object_handle))
			return -1;

		if (object_handle != parent_object_handle)
			printf("</%s>\n", object_name);
		else
			printf(">\n");
	}
	return 0;
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

	if (dump_objdb_buff(handle, cluster_handle, cluster_handle))
		return -1;

	printf("</cluster>\n");

	if (confdb_finalize(handle) != CS_OK)
		return -1;

	return 0;
}
