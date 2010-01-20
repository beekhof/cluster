#ifndef _VALUE_LIST_H
#define _VALUE_LIST_H

struct idval {
	struct idval *next;
	char *name;
	char *rawname;
	int type;
	int value;
};

struct idinfo {
	struct idval *head;
	struct idval *tail;
	int max_obj;
	int max_attr;
};

void id_insert(struct idinfo *i, struct idval *v);
struct idval * id_find(struct idinfo *oi, const char *val, int type, int id);
void id_dump(struct idinfo *oi, FILE *fp);
int id_readfile(struct idinfo *oi, char *filename);
int id_writefile(struct idinfo *oi, char *filename);

#define OBJ 1
#define ATTR 0

#endif
