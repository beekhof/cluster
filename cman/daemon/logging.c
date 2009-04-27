#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <corosync/engine/logsys.h>
#include "list.h"
#include "cman.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "logging.h"

LOGSYS_DECLARE_SUBSYS (CMAN_NAME);

int subsys_mask = 0;

void set_debuglog(int subsystems)
{
	if (subsystems)
		logsys_config_mode_set(CMAN_NAME, LOGSYS_LEVEL_DEBUG);
	else
		logsys_config_mode_set(CMAN_NAME, LOGSYS_LEVEL_INFO);
	subsys_mask = subsystems;
}
