#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include "zalloc.h"
#include "value-list.h"
#include "tree.h"
#include "name.h"
#include "ldaptypes.h"
#include "debug.h"


static struct ldap_attr_node *
find_attr_byname(struct ldap_attr_node *attrs, const char *name)
{
	struct ldap_attr_node *n;

	for (n = attrs; n; n = n->next) {
		if (!strcmp(n->name, name))
			return n;
	}
	return NULL;
}


static struct ldap_attr_meta_node *
find_meta_attr(struct ldap_attr_meta_node *metas, struct ldap_attr_node *attr)
{
	struct ldap_attr_meta_node *n;

	for (n = metas; n; n = n->next) {
		if (n->node == attr)
			return n;
	}
	return NULL;
}


static struct ldap_attr_meta_node *
find_meta_attr_byname(struct ldap_attr_meta_node *metas, const char *name)
{
	struct ldap_attr_meta_node *n;

	for (n = metas; n; n = n->next) {
		if (!strcmp(n->node->name, name))
			return n;
	}
	return NULL;
}


static int
find_data_match_fn(xmlNodePtr curr_node, char **match_fn, char **ldap_syntax)
{
	xmlNodePtr n = NULL;
	char *type = NULL;
	int need_free = 1;

	for (n = curr_node; n; n = n->next) {
		if (!n->name ||
    		    strcasecmp((char *)n->name, "data"))
			continue;
		break;	
	}

	if (n)
		type = (char *)xmlGetProp(n, (xmlChar *)"type");

	dbg_printf("type %s\n", type);

	if (!type) {
		type = (char *)"string";
		need_free = 0;
	}

	find_ldap_type_info(type, match_fn, ldap_syntax);
	
	if (need_free)
		xmlFree(type);

	return 1;
}


static struct ldap_attr_node *
get_attr(xmlNodePtr curr_node, struct ldap_attr_node **attrs,
	 struct idinfo *ids)
{
	struct ldap_attr_node *n;
	struct idval *v;
	char *name, *normalized;

	name = (char *)xmlGetProp(curr_node, (xmlChar *)"name");
	normalized = normalize_name((const char *)name);

	n = find_attr_byname(*attrs, normalized);
	if (n) {
		free(normalized);
		return n;
	}

	n = zalloc(sizeof(*n));

	v = id_find(ids, normalized, ATTR, 0);
	if (!v) {
		v = zalloc(sizeof(*v));
		v->name = normalized;
		v->type = ATTR;
		v->rawname = (char *)name;
		id_insert(ids, v);
	} else {
		free(normalized);
	}

	n->idval = v;
	n->name = n->idval->name;

	dbg_printf("Lookin for data type for %s\n", n->name);
	find_data_match_fn(curr_node->xmlChildrenNode, &n->ldap_equality,
			   &n->ldap_syntax);

	n->next = *attrs;
	*attrs = n;

	return n;
}


static struct ldap_object_node *
find_obj(struct ldap_object_node *objs, const char *name)
{
	struct ldap_object_node *n;

	for (n = objs; n; n = n->next) {
		if (!strcmp(n->name, name))
			return n;
	}
	return NULL;
}



static int
find_optional_attributes(xmlNodePtr curr_node, int in_block,
			 struct ldap_object_node *curr_obj,
			 struct ldap_attr_node **attrs,
			 struct idinfo *ids)
{
	xmlNodePtr node;
	struct ldap_attr_node *attr;
	struct ldap_attr_meta_node *n;

	if (!curr_node || (curr_node->type == XML_ELEMENT_NODE &&
	    (curr_node->name && !strcasecmp((char *)curr_node->name, "element")))) {
		return 0;
	}

	dbg_printf("lookin for optionals\n");

	for (node = curr_node; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (!strcasecmp((char *)node->name, "choice")) {
			find_optional_attributes(node->xmlChildrenNode, 1,
						 curr_obj,
						 attrs, ids);
			continue;
		}
		if (!strcasecmp((char *)node->name, "group")) {
			find_optional_attributes(node->xmlChildrenNode, 1,
						 curr_obj,
						 attrs, ids);
			continue;
		}
		if (!strcasecmp((char *)node->name, "optional")) {
			find_optional_attributes(node->xmlChildrenNode, 1,
						 curr_obj,
						 attrs, ids);
			continue;
		}

		if (!node->name || strcmp((char *)node->name,
			    "attribute")) {
			continue;
		}

		if (!in_block)
			continue;

		attr = get_attr(node, attrs, ids);
		n = zalloc(sizeof(*n));

		dbg_printf("opt attr '%s'\n", attr->idval->name);

		if (find_meta_attr(curr_obj->required_attrs,
				   attr)) {
			dbg_printf("skipping dup attr\n");
			continue;
		}
		if (find_meta_attr(curr_obj->optional_attrs,
				   attr)) {
			dbg_printf("skipping dup attr on optional list\n");
			continue;
		}

		n->node = attr;
		n->next = curr_obj->optional_attrs;
		curr_obj->optional_attrs = n;
	}
	return 0;
}


static int
find_required_attributes(xmlNodePtr curr_node,
			 struct ldap_object_node *curr_obj,
			 struct ldap_attr_node **attrs,
			 struct idinfo *ids)
{
	xmlNodePtr node;
	struct ldap_attr_node *attr;
	struct ldap_attr_meta_node *n;

	dbg_printf("lookin for required\n");

	for (node = curr_node; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, (xmlChar *)"attribute"))
			continue;

		attr = get_attr(node, attrs, ids);
		n = zalloc(sizeof(*n));

		dbg_printf("req attr '%s'\n", attr->idval->name);

		if (find_meta_attr(curr_obj->required_attrs,
				   attr)) {
			dbg_printf("skipping dup attr\n");
			continue;
		}
		if (find_meta_attr(curr_obj->optional_attrs,
				   attr)) {
			dbg_printf("skipping dup attr on optional list\n");
			continue;
		}

		n->node = attr;
		n->next = curr_obj->required_attrs;
		curr_obj->required_attrs = n;
	}
	return 0;
}


static void
parse_element_tag(xmlNodePtr curr_node,
		  struct ldap_object_node **objs,
		  struct ldap_attr_node **attrs,
		  struct idinfo *ids)
{
	struct ldap_object_node *obj;
	struct ldap_attr_meta_node *attrm;
	char *n, *normalized;
	struct idval *v;
	int need_cn = 1;
	
	dbg_printf("Trying to parse element tag\n");
	n = (char *)xmlGetProp(curr_node, (xmlChar *)"name");
	normalized = normalize_name(n);
	v = id_find(ids, normalized, OBJ, 0);

	if (!v) {
		v = zalloc(sizeof(*v));
		v->name = normalized;
		v->rawname = n;
		v->type = OBJ;
		id_insert(ids, v);
	}

	obj = find_obj(*objs, v->name);

	if (!obj) {
		obj = zalloc(sizeof(*obj));
		obj->name = v->name;
		obj->idval = v;
		obj->next = *objs;
		*objs = obj;
		dbg_printf("New object class %s \n",obj->name);
	}

	find_optional_attributes(curr_node->xmlChildrenNode, 0,
				 obj, attrs, ids);
	find_required_attributes(curr_node->xmlChildrenNode,
				 obj, attrs, ids);

	if (find_meta_attr_byname(obj->required_attrs, "name") ||
	    find_meta_attr_byname(obj->required_attrs, "cn")) {
		need_cn = 0;
	}

	if (need_cn &&
	    (find_meta_attr_byname(obj->optional_attrs, "name") ||
	     find_meta_attr_byname(obj->optional_attrs, "cn"))) {
		need_cn = 0;
	}

	if (need_cn) {
		dbg_printf("Object class might %s need 'MUST ( cn )' for proper LDIF\n", obj->name);
		obj->need_cn = 1;
	}
}


int
find_objects(xmlNodePtr curr_node,
	     struct ldap_object_node **objs,
	     struct ldap_attr_node **attrs,
	     struct idinfo *ids)
{
	if (!curr_node)
		return 0;

	for (; curr_node; curr_node = curr_node->next) {
		if (curr_node->type != XML_ELEMENT_NODE)
			continue;
		if (!strcasecmp((char *)curr_node->name, "element")) {
			parse_element_tag(curr_node, objs, attrs, ids);
		} else {
			dbg_printf("Descend on %s\n", curr_node->name);
		}
		find_objects(curr_node->xmlChildrenNode,
			     objs, attrs, ids);

	}

	return 0;
}
