#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "zalloc.h"

void *
zalloc(size_t size)
{
	void *ret;
	size_t oddball;

	oddball = ( size % sizeof(void *) );
	if (oddball) {
		size -= oddball;
		size += sizeof(void *);
	}

	ret = malloc(size);
	if (!ret) 
		assert(0);
	memset(ret, 0, size);
	return ret;
}
