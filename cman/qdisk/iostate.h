#ifndef _IOSTATE_H
#define _IOSTATE_H

typedef enum {
	STATE_NONE	= 0,
	STATE_READ	= 1,
	STATE_WRITE	= 2,
	STATE_LSEEK	= 3,
	STATE_UNKNOWN	= 4
} iostate_t;

void io_state(iostate_t state);

int io_nanny_start(int timeout);
int io_nanny_stop(void);

#endif
