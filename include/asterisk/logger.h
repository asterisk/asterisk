/*
 * Cheops Next Generation
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C) 1999, Adtran, Inc.
 * 
 * Distributed under the terms of the GNU General Public License (GPL) Version
 *
 * Logging routines
 *
 */

#ifndef _LOGGER_H
#define _LOGGER_H

#include <stdarg.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define EVENTLOG "event_log"

#define DEBUG_M(a) { \
	a; \
}

extern void ast_log(int level, char *file, int line, char *function, char *fmt, ...);
extern void ast_verbose(char *fmt, ...);

extern int ast_register_verbose(void (*verboser)(char *string, int opos, int replacelast, int complete));
extern int ast_unregister_verbose(void (*verboser)(char *string, int opos, int replacelast, int complete));

#define _A_ __FILE__, __LINE__, __PRETTY_FUNCTION__

#define LOG_DEBUG	0, _A_
#define LOG_EVENT   1, _A_
#define LOG_NOTICE  2, _A_
#define LOG_WARNING 3, _A_
#define LOG_ERROR	4, _A_

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
