#ifndef _DEBUG_H
#define _DEBUG_H

#ifdef DEBUG
#define dbg_printf printf
#else
#define dbg_printf(args...)
#endif

#endif
