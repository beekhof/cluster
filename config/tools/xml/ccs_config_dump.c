#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <corosync/corotypes.h>
#include <corosync/confdb.h>

#include "copyright.cf"

#define OPTION_STRING	"hVr"

static confdb_callbacks_t callbacks = {};

static int dump_objdb_buff(confdb_handle_t dump_handle, hdb_handle_t cluster_handle,
			   hdb_handle_t parent_object_handle, int level)
{
	hdb_handle_t object_handle;
	char object_name[PATH_MAX], key_name[PATH_MAX], key_value[PATH_MAX];
	size_t key_value_len = 0, key_name_len = 0, object_name_len = 0;
	int current_level = level+1;
	int has_children = 0;

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
		int found_children;

		if ((!has_children) && (parent_object_handle > 0))
			printf(">\n");

		has_children = 1;

		if (confdb_object_parent_get(dump_handle, object_handle, &parent) != CS_OK)
			return -1;

		object_name[object_name_len] = '\0';
		for (i=0; i<current_level; i++) {
			printf("\t");
		}
		printf("<%s", object_name);

		found_children = dump_objdb_buff(dump_handle, cluster_handle, object_handle, current_level);
		if (found_children < 0)
			return -1;

		if ((object_handle != parent_object_handle) && (found_children)) {
			for (i=0; i<current_level; i++) {
				printf("\t");
			}
			printf("</%s>\n", object_name);
		}
	}

	if(!has_children)
		printf("/>\n");

	return has_children;
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("ccs_config_dump [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("  -r               Force dump of runtime configuration (see man page)\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
	return;
}

static void read_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("ccs_config_dump %s (built %s %s)\n%s\n",
				RELEASE_VERSION, __DATE__, __TIME__,
				REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'r':
			if(unsetenv("COROSYNC_DEFAULT_CONFIG_IFACE") < 0) {
				fprintf(stderr, "Unable to unset env vars\n");
				exit(EXIT_FAILURE);
			}
			break;

		case EOF:
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c\n", optchar);
			print_usage();
			exit(EXIT_FAILURE);
			break;
		}
	}
}

int main(int argc, char *argv[], char *envp[])
{
	confdb_handle_t handle = 0;
	hdb_handle_t cluster_handle;

	read_arguments(argc, argv);

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
