#ifndef _TREE_H
#define _TREE_H

struct ldap_attr_list {
	struct ldap_attr_node *head, *tail;
};

struct ldap_attr_node {
	struct ldap_attr_node *next;
	char *name; /* self->idval->name; do not free() */
	char *desc;
	char *ldap_equality;
	char *ldap_syntax;
	struct idval *idval;
};

struct ldap_attr_meta_node {
	struct ldap_attr_meta_node *next;
	struct ldap_attr_node *node;
};

struct ldap_object_node {
	struct ldap_object_node *next;
	char *name; /* self->idval->name; do not free() */
	char *desc;
	struct idval *idval;
	struct ldap_attr_meta_node *optional_attrs;
	struct ldap_attr_meta_node *required_attrs;
	int need_cn; /* If we don't have a 'name' or a 'cn' attribute,
			add one when we output the LDIF */
};

int
find_objects(xmlNodePtr curr_node,
	     struct ldap_object_node **objs,
	     struct ldap_attr_node **attrs,
	     struct idinfo *ids);


#endif
