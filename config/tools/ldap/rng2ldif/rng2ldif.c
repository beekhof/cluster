#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include "value-list.h"
#include "tree.h"


struct faux_list {
	struct faux_list *next;
};


static void __attribute__((noinline))
reverse(struct faux_list **list)
{
	struct faux_list *node = NULL;
	struct faux_list *newlist = NULL;

	if (!list || (*list == NULL))
		return;

	while ((*list) != NULL) {
		node = *list;
		*list = node->next;
		node->next = newlist;
		newlist = node;
	}
	*list = node;
}


static int
print_attr_node(FILE *fp, struct ldap_attr_node *node)
{
	if (!node)
		return -1;
	if (!fp)
		fp = stdout;

	if (!strcasecmp(node->name, "name")) {
		/* Don't print 'name' out as an attr. */
		return 0;
	}

	fprintf(fp, "attributeTypes: (\n");
	fprintf(fp, "  1.3.6.1.4.1.2312.8.1.1.%d NAME '%s'\n",
		node->idval->value, node->name);
	fprintf(fp, "  EQUALITY %s\n", node->ldap_equality);
	fprintf(fp, "  SYNTAX %s\n", node->ldap_syntax);
	fprintf(fp, "  SINGLE-VALUE\n  )\n");
	return 0;
}


static int
print_obj_node(FILE *fp, struct ldap_object_node *node)
{
	struct ldap_attr_meta_node *n;
	const char *cmt = "";

	if (!node)
		return -1;
	if (!fp)
		fp = stdout;

	if (!node->required_attrs && !node->optional_attrs) {
		cmt = "#";
		fprintf(fp, "### Placeholder for %s\n", node->name);
		fprintf(fp,
			"### This object class currently has no attributes\n");
	}

	fprintf(fp, "%sobjectClasses: (\n", cmt);
	fprintf(fp, "%s     1.3.6.1.4.1.2312.8.1.2.%d NAME '%s' SUP top STRUCTURAL\n", cmt, node->idval->value, node->name);

	if (node->required_attrs) {
		fprintf(fp, "%s     MUST ( ", cmt);

		if (node->need_cn) {
			fprintf(fp, "cn $ ");
		}

		for (n = node->required_attrs; n->next != NULL; n = n->next)
			fprintf(fp, "%s $ ", n->node->name);
		fprintf(fp, "%s )\n", n->node->name);
	} else {
		if (node->need_cn) {
			fprintf(fp, "%s     MUST ( cn )\n", cmt);
		}
	}

	if (node->optional_attrs) {
		fprintf(fp, "%s     MAY ( ", cmt);
		for (n = node->optional_attrs; n->next != NULL; n = n->next)
			fprintf(fp, "%s $ ", n->node->name);
		fprintf(fp, "%s )\n", n->node->name);
	}
	fprintf(fp, "%s   )\n", cmt);
	return 0;
}


static xmlDocPtr 
open_relaxng(const char *filename)
{
	xmlDocPtr p;
	xmlNodePtr n;

	p = xmlParseFile(filename);
	if (!p) {
		printf("Failed to parse %s\n", filename);
	}

	n = xmlDocGetRootElement(p);
	if (xmlStrcmp(n->name, (xmlChar *)"grammar")) {
		printf("%s is not a relaxng grammar\n", filename);
		xmlFreeDoc(p);
		return NULL;
	}

	return p;
}


static int
write_ldap_schema(const char *rng, const char *arg,
		  struct ldap_attr_node *attrs,
		  struct ldap_object_node *objs)
{
	struct ldap_attr_node *attr = NULL;
	struct ldap_object_node *obj = NULL;
	char filename[4096];
	FILE *out_ldap = NULL;
	char now_asc[128];
	time_t now;
	struct tm now_tm;
	int fd = -1;

	if (!strcmp(arg, "-")) {
		out_ldap = stdout;
	} else {
		snprintf(filename, sizeof(filename), "%s.XXXXXX", arg);
		fd = mkstemp(filename);
		if (fd < 0) {
			perror("mkstemp");
			return -1;
		}

		out_ldap = fdopen(fd, "w");
		if (out_ldap == NULL) {
			perror("fdopen");
			close(fd);
			return -1;
		}
	}

	now = time(NULL);
	memset(&now_tm, 0, sizeof(now_tm));
	if (localtime_r(&now, &now_tm) == NULL) {
		snprintf(now_asc, sizeof(now_asc), "???");
	} else {
		strftime(now_asc, sizeof(now_asc), "%F %T", &now_tm);
	}

	fprintf(out_ldap, "# Auto-generated @ %s\n", now_asc);
	fprintf(out_ldap, "dn: cn=schema\n");

	for (attr = attrs; attr; attr = attr->next)
		print_attr_node(out_ldap, attr);
	for (obj = objs; obj; obj= obj->next)
		print_obj_node(out_ldap, obj);

	fflush(out_ldap);

	if (fd >= 0) {
		fsync(fd);
		fclose(out_ldap);
		close(fd);
		rename(filename, arg);
	}

	return 0;
}


int
main(int argc, char **argv)
{
	struct ldap_attr_node *attrs = NULL;
	struct ldap_object_node *objs = NULL;
	struct idinfo info;
	xmlDocPtr doc;

	memset(&info, 0, sizeof(info));

	if (argc < 4) {
		printf("Translate cluster RelaxNG -> LDIF schema and update\n"
		       "global .csv file for future reuse of IDs\n");
		printf("Usage: %s cluster.rng ldap-base.csv cluster.ldif\n",
		       argv[0]);
		return 1;
	}

	doc = open_relaxng(argv[1]);
       	if (doc == NULL) {
		printf("Cannot continue\n");
		return 1;
	}

	if (id_readfile(&info, argv[2]) < 0) {
		printf("Can't read %s\n", argv[2]);
		return 1;
	}

	find_objects(xmlDocGetRootElement(doc), &objs, &attrs, &info);

	reverse((struct faux_list **)&attrs);
	reverse((struct faux_list **)&objs);

	if (write_ldap_schema(argv[1], argv[3], attrs, objs) < 0)
		return -1;

	id_writefile(&info, argv[2]);

	return 0;
}
