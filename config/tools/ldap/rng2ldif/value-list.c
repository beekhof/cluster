#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "value-list.h"
#include "zalloc.h"

void
id_insert(struct idinfo *i, struct idval *v)
{
	if (v->type == ATTR) {
		if (v->value > i->max_attr)
			i->max_attr = v->value;
		if (!v->value)
			v->value = ++i->max_attr;
	} else /* if type == OBJ */ {
		if (v->value > i->max_obj)
			i->max_obj = v->value;
		if (!v->value)
			v->value = ++i->max_obj;
	}

	if (!i->head)
		i->head = v;
	if (i->tail)
		i->tail->next = v;
	i->tail = v;
}


struct idval *
id_find(struct idinfo *oi, const char *val, int type, int id)
{
	struct idval *v;

	if (!val && !id)
		return NULL;

	/* linear search */
	for (v = oi->head; v; v = v->next) {
		if (v->type != type)
			continue;
		if (val && !strcmp(val, v->name))
			return v;
		if (id && id == v->value)
			return v;
	}

	return NULL;
}


void
id_dump(struct idinfo *oi, FILE *fp)
{
	struct idval *v;

	fprintf(fp, "# Max attribute value: %d\n", oi->max_attr);
	fprintf(fp, "# Max object class value: %d\n", oi->max_obj);

	for (v = oi->head; v; v = v->next) {
		fprintf(fp, "%s,%s,%s,%d\n", v->type==OBJ?"obj":"attr",
			v->name, v->rawname, v->value);
	}
}



int
id_writefile(struct idinfo *oi, char *filename)
{
	char tmpfn[4096];
	FILE *fp;
	int fd;

	snprintf(tmpfn, sizeof(tmpfn), "%s.XXXXXX", filename);
	fd = mkstemp(tmpfn);
	if (fd < 0)
		return -1;

	fp = fdopen(fd, "w");
	if (!fp) {
		close(fd);
		unlink(tmpfn);
		return -1;
	}

	id_dump(oi, fp);
	fflush(fp);
	fsync(fd);
	fclose(fp);
	close(fd);

	/* done */
	rename(tmpfn, filename);

	return 0;
}


int
id_readfile(struct idinfo *oi, char *filename)
{
	char *c, *valp;
	struct idval *v;
	struct idval *tmp;
	FILE *fp;
	char buf[4096];
	int len, lineno = 0, entries = 0;

	fp = fopen(filename, "r");
	if (!filename) {
		perror("fopen");
		return 1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		++lineno;
		if (!strlen(buf))
			continue;
		if (buf[0] == '#')
			continue;
		len = strlen(buf);
		while (buf[len-1] < ' ') {
			buf[len-1] = 0;
			--len;
		}
		v = zalloc(sizeof(*v));

		/* Attribute / object */
		c = strchr(buf, ',');
		if (!c || strlen(c) == 1) {
			fprintf(stderr,
				"%s: Malformed input on line %d: '%s'\n",
				filename, lineno, buf);
			exit(-2);
		}

		*c = 0;
		c++;
		if (!strncasecmp(buf, "attr",4)) {
			v->type = ATTR;
		} else if (!strncasecmp(buf, "obj", 3)) {
			v->type = OBJ;
		} else {
			fprintf(stderr, "%s: Unknown type on line %d: '%s'\n",
				filename, lineno, buf);
			exit(-2);
		}

		valp = strchr(c, ',');
		if (valp) {
			*valp = 0;
			valp++;
		}
		v->name = strdup(c);
		assert(v->name);

		c = valp;

		valp = strchr(c, ',');
		v->value = 0;
		if (valp) {
			*valp = 0;
			valp++;
			if (strlen(valp))
				v->value = atoi(valp);
		}

		v->rawname = strdup(c);
		assert(v->rawname);

		if (!id_find(oi, v->name, v->type, 0)) {
			tmp = id_find(oi, NULL, v->type, v->value);
			if (tmp) {
				fprintf(stderr, "%s: Duplicate id value: "
					"%d & %d, type %d\n", filename,
					tmp->value, v->value, v->type);
				exit(-1);
			}
			id_insert(oi, v);
		}

		++entries;
	}

	fclose(fp);

	return 0;
}
