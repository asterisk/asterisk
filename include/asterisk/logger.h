/*
 * Cheops Next Generation
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C) Mark Spencer
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

/*! Used for sending a log message */
/*!
 * \param level don't need to worry about it
 * \param file ditto
 * \param line ditto
 * \param function ditto
 * \param fmt this is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
 * This is the standard logger function.  Probably the only way you will invoke it would be something like this:
 * ast_log(LOG_WHATEVER, "Problem with the %s Captain.  We should get some more.  Will %d be enough?", "flux capacitor", 10);
 * where WHATEVER is one of ERROR, DEBUG, EVENT, NOTICE, or WARNING depending on which log you wish to output to.
 */
extern void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

extern void ast_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

/*! Send a verbose message (based on verbose level) */
/*!
 * This works like ast_log, but prints verbose messages to the console depending on verbosity level set.
 * ast_verbose(VERBOSE_PREFIX_3 "Whatever %s is happening\n", "nothing");
 * This will print the message to the console if the verbose level is set to a level >= 3
 * Note the abscence of a comma after the VERBOSE_PREFIX_3.  This is important.
 * VERBOSE_PREFIX_1 through VERBOSE_PREFIX_3 are defined.
 */
extern void ast_verbose(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

extern int ast_register_verbose(void (*verboser)(const char *string, int opos, int replacelast, int complete));
extern int ast_unregister_verbose(void (*verboser)(const char *string, int opos, int replacelast, int complete));
extern int ast_verbose_dmesg(void (*verboser)(const char *string, int opos, int replacelast, int complete));
extern void ast_console_puts(const char *string);

#define _A_ __FILE__, __LINE__, __PRETTY_FUNCTION__

#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#define __LOG_DEBUG    0
#define LOG_DEBUG      __LOG_DEBUG, _A_

#ifdef LOG_EVENT
#undef LOG_EVENT
#endif
#define __LOG_EVENT    1
#define LOG_EVENT      __LOG_EVENT, _A_

#ifdef LOG_NOTICE
#undef LOG_NOTICE
#endif
#define __LOG_NOTICE   2
#define LOG_NOTICE     __LOG_NOTICE, _A_

#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
#define __LOG_WARNING  3
#define LOG_WARNING    __LOG_WARNING, _A_

#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#define __LOG_ERROR    4
#define LOG_ERROR      __LOG_ERROR, _A_

#ifdef LOG_VERBOSE
#undef LOG_VERBOSE
#endif
#define __LOG_VERBOSE  5
#define LOG_VERBOSE    __LOG_VERBOSE, _A_

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
