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

//! Used for sending a log message
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
extern void ast_log(int level, char *file, int line, char *function, char *fmt, ...);

//! Send a verbose message (based on verbose level)
/*!
 * This works like ast_log, but prints verbose messages to the console depending on verbosity level set.
 * ast_verbose(VERBOSE_PREFIX_3 "Whatever %s is happening\n", "nothing");
 * This will print the message to the console if the verbose level is set to a level >= 3
 * Note the abscence of a comma after the VERBOSE_PREFIX_3.  This is important.
 * VERBOSE_PREFIX_1 through VERBOSE_PREFIX_3 are defined.
 */
extern void ast_verbose(char *fmt, ...);

extern int ast_register_verbose(void (*verboser)(char *string, int opos, int replacelast, int complete));
extern int ast_unregister_verbose(void (*verboser)(char *string, int opos, int replacelast, int complete));
extern int ast_verbose_dmesg(void (*verboser)(char *string, int opos, int replacelast, int complete));
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
