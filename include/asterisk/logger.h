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

#include "asterisk/options.h"	/* need option_debug */

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define EVENTLOG "event_log"
#define	QUEUELOG	"queue_log"

#define DEBUG_M(a) { \
	a; \
}

#define VERBOSE_PREFIX_1 " "
#define VERBOSE_PREFIX_2 "  == "
#define VERBOSE_PREFIX_3 "    -- "
#define VERBOSE_PREFIX_4 "       > "

/*! \brief Used for sending a log message
	This is the standard logger function.  Probably the only way you will invoke it would be something like this:
	ast_log(AST_LOG_WHATEVER, "Problem with the %s Captain.  We should get some more.  Will %d be enough?\n", "flux capacitor", 10);
	where WHATEVER is one of ERROR, DEBUG, EVENT, NOTICE, or WARNING depending
	on which log you wish to output to. These are implemented as macros, that
	will provide the function with the needed arguments.

 	\param level	Type of log event
	\param file	Will be provided by the AST_LOG_* macro
	\param line	Will be provided by the AST_LOG_* macro
	\param function	Will be provided by the AST_LOG_* macro
	\param fmt	This is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
 */

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

void ast_backtrace(void);

/*! \brief Reload logger without rotating log files */
int logger_reload(void);

void __attribute__((format(printf, 5, 6))) ast_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...);

/*! Send a verbose message (based on verbose level)
 	\brief This works like ast_log, but prints verbose messages to the console depending on verbosity level set.
 	ast_verbose(VERBOSE_PREFIX_3 "Whatever %s is happening\n", "nothing");
 	This will print the message to the console if the verbose level is set to a level >= 3
 	Note the abscence of a comma after the VERBOSE_PREFIX_3.  This is important.
 	VERBOSE_PREFIX_1 through VERBOSE_PREFIX_3 are defined.
 */
void __attribute__((format(printf, 4, 5))) __ast_verbose(const char *file, int line, const char *func, const char *fmt, ...);

#define ast_verbose(...) __ast_verbose(__FILE__, __LINE__, __PRETTY_FUNCTION__,  __VA_ARGS__)

void __attribute__((format(printf, 4, 0))) __ast_verbose_ap(const char *file, int line, const char *func, const char *fmt, va_list ap);

#define ast_verbose_ap(fmt, ap)	__ast_verbose_ap(__FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, ap)

void __attribute__((format(printf, 2, 3))) ast_child_verbose(int level, const char *fmt, ...);

int ast_register_verbose(void (*verboser)(const char *string)) attribute_warn_unused_result;
int ast_unregister_verbose(void (*verboser)(const char *string)) attribute_warn_unused_result;

void ast_console_puts(const char *string);

/*!
 * \brief log the string to the console, and all attached
 * console clients
 * \version 1.6.1 added level parameter
 */
void ast_console_puts_mutable(const char *string, int level);
void ast_console_toggle_mute(int fd, int silent);

/*!
 * \since 1.6.1
 */
void ast_console_toggle_loglevel(int fd, int level, int state);

/* Note: The AST_LOG_* macros below are the same as
 * the LOG_* macros and are intended to eventually replace
 * the LOG_* macros to avoid name collisions as has been
 * seen in app_voicemail. However, please do NOT remove
 * the LOG_* macros from the source since these may be still
 * needed for third-party modules
 */

#define _A_ __FILE__, __LINE__, __PRETTY_FUNCTION__

#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#define __LOG_DEBUG    0
#define LOG_DEBUG      __LOG_DEBUG, _A_

#ifdef AST_LOG_DEBUG
#undef AST_LOG_DEBUG
#endif
#define AST_LOG_DEBUG      __LOG_DEBUG, _A_

#ifdef LOG_EVENT
#undef LOG_EVENT
#endif
#define __LOG_EVENT    1
#define LOG_EVENT      __LOG_EVENT, _A_

#ifdef AST_LOG_EVENT
#undef AST_LOG_EVENT
#endif
#define AST_LOG_EVENT      __LOG_EVENT, _A_

#ifdef LOG_NOTICE
#undef LOG_NOTICE
#endif
#define __LOG_NOTICE   2
#define LOG_NOTICE     __LOG_NOTICE, _A_

#ifdef AST_LOG_NOTICE
#undef AST_LOG_NOTICE
#endif
#define AST_LOG_NOTICE     __LOG_NOTICE, _A_

#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
#define __LOG_WARNING  3
#define LOG_WARNING    __LOG_WARNING, _A_

#ifdef AST_LOG_WARNING
#undef AST_LOG_WARNING
#endif
#define AST_LOG_WARNING    __LOG_WARNING, _A_

#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#define __LOG_ERROR    4
#define LOG_ERROR      __LOG_ERROR, _A_

#ifdef AST_LOG_ERROR
#undef AST_LOG_ERROR
#endif
#define AST_LOG_ERROR      __LOG_ERROR, _A_

#ifdef LOG_VERBOSE
#undef LOG_VERBOSE
#endif
#define __LOG_VERBOSE  5
#define LOG_VERBOSE    __LOG_VERBOSE, _A_

#ifdef AST_LOG_VERBOSE
#undef AST_LOG_VERBOSE
#endif
#define LOG_VERBOSE    __LOG_VERBOSE, _A_

#ifdef LOG_DTMF
#undef LOG_DTMF
#endif
#define __LOG_DTMF  6
#define LOG_DTMF    __LOG_DTMF, _A_

#ifdef AST_LOG_DTMF
#undef AST_LOG_DTMF
#endif
#define AST_LOG_DTMF    __LOG_DTMF, _A_

#define NUMLOGLEVELS 6

/*!
 * \brief Get the debug level for a file
 * \param file the filename
 * \return the debug level
 */
unsigned int ast_debug_get_by_file(const char *file);

/*!
 * \brief Get the debug level for a file
 * \param file the filename
 * \return the debug level
 */
unsigned int ast_verbose_get_by_file(const char *file);

/*!
 * \brief Log a DEBUG message
 * \param level The minimum value of option_debug for this message
 *        to get logged
 */
#define ast_debug(level, ...) do {       \
	if (option_debug >= (level) || (ast_opt_dbg_file && ast_debug_get_by_file(__FILE__) >= (level)) ) \
		ast_log(AST_LOG_DEBUG, __VA_ARGS__); \
} while (0)

#define VERBOSITY_ATLEAST(level) (option_verbose >= (level) || (ast_opt_verb_file && ast_verbose_get_by_file(__FILE__) >= (level)))

#define ast_verb(level, ...) do { \
	if (VERBOSITY_ATLEAST((level)) ) { \
		if (level >= 4) \
			ast_verbose(VERBOSE_PREFIX_4 __VA_ARGS__); \
		else if (level == 3) \
			ast_verbose(VERBOSE_PREFIX_3 __VA_ARGS__); \
		else if (level == 2) \
			ast_verbose(VERBOSE_PREFIX_2 __VA_ARGS__); \
		else if (level == 1) \
			ast_verbose(VERBOSE_PREFIX_1 __VA_ARGS__); \
		else \
			ast_verbose(__VA_ARGS__); \
	} \
} while (0)

#ifndef _LOGGER_BACKTRACE_H
#define _LOGGER_BACKTRACE_H
#ifdef HAVE_BKTR
#define AST_MAX_BT_FRAMES 32
/* \brief
 *
 * A structure to hold backtrace information. This structure provides an easy means to
 * store backtrace information or pass backtraces to other functions.
 */
struct ast_bt {
	/*! The addresses of the stack frames. This is filled in by calling the glibc backtrace() function */
	void *addresses[AST_MAX_BT_FRAMES];
	/*! The number of stack frames in the backtrace */
	int num_frames;
	/*! Tells if the ast_bt structure was dynamically allocated */
	unsigned int alloced:1;
};

/* \brief
 * Allocates memory for an ast_bt and stores addresses and symbols.
 *
 * \return Returns NULL on failure, or the allocated ast_bt on success
 * \since 1.6.1
 */
struct ast_bt *ast_bt_create(void);

/* \brief
 * Fill an allocated ast_bt with addresses
 *
 * \retval 0 Success
 * \retval -1 Failure
 * \since 1.6.1
 */
int ast_bt_get_addresses(struct ast_bt *bt);

/* \brief
 *
 * Free dynamically allocated portions of an ast_bt
 *
 * \retval NULL.
 * \since 1.6.1
 */
void *ast_bt_destroy(struct ast_bt *bt);

#endif /* HAVE_BKTR */
#endif /* _LOGGER_BACKTRACE_H */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_LOGGER_H */
