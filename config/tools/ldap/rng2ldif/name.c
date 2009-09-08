#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "name.h"
#include "zalloc.h"

char *
normalize_name(const char *name)
{
	char *ret_val;
	int size = 4, x;

	if (!strcasecmp(name, "name")) {
		ret_val = strdup(name);
		assert(ret_val!=NULL);
		goto out;
	}

	size = strlen(name)+5;
	ret_val = zalloc(size); /* 1 byte for null, 4 for rhcs */
	if (!ret_val)
		return NULL;

	snprintf(ret_val, size, "rhcs%s", name);

out:
	for (x = 0; x < 4; x++)
		ret_val[x] |= 32;
	if (ret_val[4] == '_')
		ret_val[4] = '-';
	else
		ret_val[4] &= ~32;
	for (x = 5; x < size; x++) {
		if (ret_val[x] == '_') {
			ret_val[x] = '-';
		}
	}

	return ret_val;
}

#ifdef STANDALONE
int
main(int argc, char **argv)
{
	char *val;
	if (argc < 2)
		return -1;

	val = normalize_name(argv[1]);
	if (!val) 
		fprintf(stderr, "oops\n");
	printf("%s\n", val);
	free(val);
	return 0;
}
#endif
