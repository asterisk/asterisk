/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
  \file logger.h
  \brief Support for logging to various files, console and syslog
	Configuration in file logger.conf
*/

#ifndef _ASTERISK_LOGGER_H
#define _ASTERISK_LOGGER_H

#include "asterisk/compat.h"

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
	\brief This is the standard logger function.  Probably the only way you will invoke it would be something like this:
	ast_log(LOG_WHATEVER, "Problem with the %s Captain.  We should get some more.  Will %d be enough?", "flux capacitor", 10);
	where WHATEVER is one of ERROR, DEBUG, EVENT, NOTICE, or WARNING depending
	on which log you wish to output to. These are implemented as macros, that
	will provide the function with the needed arguments.

 	\param level	Type of log event
	\param file	Will be provided by the LOG_* macro
	\param line	Will be provided by the LOG_* macro
	\param function	Will be provided by the LOG_* macro
	\param fmt	This is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
 */
extern void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

extern void ast_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

/*! Send a verbose message (based on verbose level) 
 	\brief This works like ast_log, but prints verbose messages to the console depending on verbosity level set.
 	ast_verbose(VERBOSE_PREFIX_3 "Whatever %s is happening\n", "nothing");
 	This will print the message to the console if the verbose level is set to a level >= 3
 	Note the abscence of a comma after the VERBOSE_PREFIX_3.  This is important.
 	VERBOSE_PREFIX_1 through VERBOSE_PREFIX_3 are defined.
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

#ifdef LOG_DTMF
#undef LOG_DTMF
#endif
#define __LOG_DTMF  6
#define LOG_DTMF    __LOG_DTMF, _A_

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_LOGGER_H */
