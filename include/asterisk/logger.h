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

#define AST_CALLID_BUFFER_LENGTH 13

enum ast_logger_results {
	AST_LOGGER_SUCCESS = 0, /*!< Log channel was created or deleted successfully*/
	AST_LOGGER_FAILURE = 1, /*!< Log channel already exists for create or doesn't exist for deletion of log channel */
	AST_LOGGER_DECLINE = -1, /*!< Log channel request was not accepted */
	AST_LOGGER_ALLOC_ERROR = -2 /*!< filename allocation error */
};

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

void ast_log_ap(int level, const char *file, int line, const char *function, const char *fmt, va_list ap)
	 __attribute__((format(printf, 5, 0)));

/*!
 * \brief Used for sending a log message with protection against recursion.
 *
 * \note This function should be used by all error messages that might be directly
 * or indirectly caused by logging.
 *
 * \see ast_log for documentation on the parameters.
 */
void ast_log_safe(int level, const char *file, int line, const char *function, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

/* XXX needs documentation */
typedef unsigned int ast_callid;

/*! \brief Used for sending a log message with a known call_id
	This is a modified logger function which is functionally identical to the above logger function,
	it just include a call_id argument as well. If NULL is specified here, no attempt will be made to
	join the log message with a call_id.

	\param level	Type of log event
	\param file	Will be provided by the AST_LOG_* macro
	\param line	Will be provided by the AST_LOG_* macro
	\param function	Will be provided by the AST_LOG_* macro
	\param callid	This is the ast_callid that is associated with the log message. May be NULL.
	\param fmt	This is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
*/
void ast_log_callid(int level, const char *file, int line, const char *function, ast_callid callid, const char *fmt, ...)
	__attribute__((format(printf, 6, 7)));

/*!
 * \brief Retrieve the existing log channels
 * \param logentry A callback to an updater function
 * \param data Data passed into the callback for manipulation
 *
 * For each of the logging channels, logentry will be executed with the
 * channel file name, log type, status of the log, and configuration levels.
 *
 * \retval 0 on success
 * \retval 1 on failure
 * \retval -2 on allocation error
 */
int ast_logger_get_channels(int (*logentry)(const char *channel, const char *type,
	const char *status, const char *configuration, void *data), void *data);

/*!
 * \brief Create a log channel
 *
 * \param log_channel Log channel to create
 * \param components Logging config levels to add to the log channel
 */
int ast_logger_create_channel(const char *log_channel, const char *components);

/*!
 * \brief Delete the specified log channel
 *
 * \param log_channel The log channel to delete
 */
int ast_logger_remove_channel(const char *log_channel);

/*!
 * \brief Log a backtrace of the current thread's execution stack to the Asterisk log
 */
void ast_log_backtrace(void);

/*! \brief Reload logger while rotating log files */
int ast_logger_rotate(void);

/*!
 * \brief Rotate the specified log channel.
 *
 * \param log_channel The log channel to rotate
 */
int ast_logger_rotate_channel(const char *log_channel);

void __attribute__((format(printf, 5, 6))) ast_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...);

/*!
 * \brief Send a verbose message (based on verbose level)
 *
 * \details This works like ast_log, but prints verbose messages to the console depending on verbosity level set.
 *
 * ast_verbose(VERBOSE_PREFIX_3 "Whatever %s is happening\n", "nothing");
 *
 * This will print the message to the console if the verbose level is set to a level >= 3
 *
 * Note the absence of a comma after the VERBOSE_PREFIX_3.  This is important.
 * VERBOSE_PREFIX_1 through VERBOSE_PREFIX_4 are defined.
 *
 * \version 11 added level parameter
 */
void __attribute__((format(printf, 5, 6))) __ast_verbose(const char *file, int line, const char *func, int level, const char *fmt, ...);

/*!
 * \brief Send a verbose message (based on verbose level) with deliberately specified callid
 *
 * \details just like __ast_verbose, only __ast_verbose_callid allows you to specify which callid is being used
 * for the log without needing to bind it to a thread. NULL is a valid argument for this function and will
 * allow you to specify that a log will never display a call id even when there is a call id bound to the
 * thread.
 */
void __attribute__((format(printf, 6, 7))) __ast_verbose_callid(const char *file, int line, const char *func, int level, ast_callid callid, const char *fmt, ...);

#define ast_verbose(...) __ast_verbose(__FILE__, __LINE__, __PRETTY_FUNCTION__, -1, __VA_ARGS__)
#define ast_verbose_callid(callid, ...) __ast_verbose_callid(__FILE__, __LINE__, __PRETTY_FUNCTION__, -1, callid, __VA_ARGS__)

void __attribute__((format(printf, 6, 0))) __ast_verbose_ap(const char *file, int line, const char *func, int level, ast_callid callid, const char *fmt, va_list ap);

void __attribute__((format(printf, 2, 3))) ast_child_verbose(int level, const char *fmt, ...);

int ast_register_verbose(void (*verboser)(const char *string)) attribute_warn_unused_result;
int ast_unregister_verbose(void (*verboser)(const char *string)) attribute_warn_unused_result;

/*
 * These gymnastics are due to platforms which designate char as unsigned by
 * default.  Level is the negative character -- offset by 1, because \0 is
 * the string terminator.
 */
#define VERBOSE_MAGIC2LEVEL(x) (((char) -*(signed char *) (x)) - 1)
#define VERBOSE_HASMAGIC(x)	(*(signed char *) (x) < 0)

void ast_console_puts(const char *string);

/*!
 * \brief log the string to the console, and all attached console clients
 *
 * \param string The message to write to the console
 * \param level The log level of the message
 *
 * \version 1.6.1 added level parameter
 */
void ast_console_puts_mutable(const char *string, int level);

/*!
 * \brief log the string to the console, and all attached console clients
 * \since 14.0.0
 *
 * \param message The message to write to the console
 * \param sublevel If the log level supports it, the sub-level of the message
 * \param level The log level of the message
 */
void ast_console_puts_mutable_full(const char *message, int level, int sublevel);

void ast_console_toggle_mute(int fd, int silent);

/*!
 * \brief enables or disables logging of a specified level to the console
 * fd specifies the index of the console receiving the level change
 * level specifies the index of the logging level being toggled
 * state indicates whether logging will be on or off (0 for off, 1 for on)
 */
void ast_console_toggle_loglevel(int fd, int level, int state);

/* Note: The AST_LOG_* macros below are the same as
 * the LOG_* macros and are intended to eventually replace
 * the LOG_* macros to avoid name collisions with the syslog(3)
 * log levels. However, please do NOT remove
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

#ifdef LOG_TRACE
#undef LOG_TRACE
#endif
#define __LOG_TRACE   1
#define LOG_TRACE     __LOG_TRACE, _A_

#ifdef AST_LOG_TRACE
#undef AST_LOG_TRACE
#endif
#define AST_LOG_TRACE     __LOG_TRACE, _A_

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
#define AST_LOG_VERBOSE    __LOG_VERBOSE, _A_

#ifdef LOG_DTMF
#undef LOG_DTMF
#endif
#define __LOG_DTMF  6
#define LOG_DTMF    __LOG_DTMF, _A_

#ifdef AST_LOG_DTMF
#undef AST_LOG_DTMF
#endif
#define AST_LOG_DTMF    __LOG_DTMF, _A_

#define NUMLOGLEVELS 32

/*!
 * \brief Get the debug level for a module
 * \param module the name of module
 * \return the debug level
 */
unsigned int ast_debug_get_by_module(const char *module);

/*!
 * \brief Register a new logger level
 * \param name The name of the level to be registered
 * \retval -1 if an error occurs
 * \retval non-zero level to be used with ast_log for sending messages to this level
 * \since 1.8
 */
int ast_logger_register_level(const char *name);

/*!
 * \brief Retrieve dynamic logging level id
 * \param name The name of the level
 * \retval The unique integer id for the given level
 * \retval -1 if level name not found
 */
int ast_logger_get_dynamic_level(const char *name);

/*!
 * \brief Unregister a previously registered logger level
 * \param name The name of the level to be unregistered
 * \return nothing
 * \since 1.8
 */
void ast_logger_unregister_level(const char *name);

/*!
 * \brief Get the logger configured date format
 *
 * \retval The date format string
 *
 * \since 13.0.0
 */
const char *ast_logger_get_dateformat(void);

/*!
 * \brief factory function to create a new uniquely identifying callid.
 *
 * \retval The call id
 */
ast_callid ast_create_callid(void);

/*!
 * \brief extracts the callerid from the thread
 *
 * \retval Non-zero Call id related to the thread
 * \retval 0 if no call_id is present in the thread
 */
ast_callid ast_read_threadstorage_callid(void);

/*!
 * \brief Sets what is stored in the thread storage to the given
 *        callid if it does not match what is already there.
 *
 * \retval 0 - success
 * \retval non-zero - failure
 */
int ast_callid_threadassoc_change(ast_callid callid);

/*!
 * \brief Adds a known callid to thread storage of the calling thread
 *
 * \retval 0 - success
 * \retval non-zero - failure
 */
int ast_callid_threadassoc_add(ast_callid callid);

/*!
 * \brief Removes callid from thread storage of the calling thread
 *
 * \retval 0 - success
 * \retval non-zero - failure
 */
int ast_callid_threadassoc_remove(void);

/*!
 * \brief Checks thread storage for a callid and stores a reference if it exists.
 *        If not, then a new one will be created, bound to the thread, and a reference
 *        to it will be stored.
 *
 * \param callid pointer to store the callid
 * \retval 0 - callid was found
 * \retval 1 - callid was created
 * \retval -1 - the function failed somehow (presumably memory problems)
 */
int ast_callid_threadstorage_auto(ast_callid *callid);

/*!
 * \brief Use in conjunction with ast_callid_threadstorage_auto. Cleans up the
 *        references and if the callid was created by threadstorage_auto, unbinds
 *        the callid from the threadstorage
 * \param callid The callid set by ast_callid_threadstorage_auto
 * \param callid_created The integer returned through ast_callid_threadstorage_auto
 */
void ast_callid_threadstorage_auto_clean(ast_callid callid, int callid_created);

/*!
 * \brief copy a string representation of the callid into a target string
 *
 * \param buffer destination of callid string (should be able to store 13 characters or more)
 * \param buffer_size maximum writable length of the string (Less than 13 will result in truncation)
 * \param callid Callid for which string is being requested
 */
void ast_callid_strnprint(char *buffer, size_t buffer_size, ast_callid callid);

/*!
 * \brief Send a log message to a dynamically registered log level
 * \param level The log level to send the message to
 *
 * Like ast_log, the log message may include printf-style formats, and
 * the data for these must be provided as additional parameters after
 * the log message.
 *
 * \return nothing
 * \since 1.8
 */

#define ast_log_dynamic_level(level, ...) ast_log(level, __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)

#define DEBUG_ATLEAST(level) \
	(option_debug >= (level) \
		|| (ast_opt_dbg_module \
        	&& ((int)ast_debug_get_by_module(AST_MODULE) >= (level) \
				|| (int)ast_debug_get_by_module(__FILE__) >= (level))))

/*!
 * \brief Log a DEBUG message
 * \param level The minimum value of option_debug for this message
 *        to get logged
 */
#define ast_debug(level, ...) \
	do { \
		if (DEBUG_ATLEAST(level)) { \
			ast_log(AST_LOG_DEBUG, __VA_ARGS__); \
		} \
	} while (0)

extern int ast_verb_sys_level;

#define VERBOSITY_ATLEAST(level) ((level) <= ast_verb_sys_level)

#define ast_verb(level, ...) \
	do { \
		if (VERBOSITY_ATLEAST(level) ) { \
			__ast_verbose(__FILE__, __LINE__, __PRETTY_FUNCTION__, level, __VA_ARGS__); \
		} \
	} while (0)

#define ast_verb_callid(level, callid, ...) \
	do { \
		if (VERBOSITY_ATLEAST(level) ) { \
			__ast_verbose_callid(__FILE__, __LINE__, __PRETTY_FUNCTION__, level, callid, __VA_ARGS__); \
		} \
	} while (0)

/*!
 * \brief Re-evaluate the system max verbosity level (ast_verb_sys_level).
 *
 * \return Nothing
 */
void ast_verb_update(void);

/*!
 * \brief Register this thread's console verbosity level pointer.
 *
 * \param level Where the verbose level value is.
 *
 * \return Nothing
 */
void ast_verb_console_register(int *level);

/*!
 * \brief Unregister this thread's console verbosity level.
 *
 * \return Nothing
 */
void ast_verb_console_unregister(void);

/*!
 * \brief Get this thread's console verbosity level.
 *
 * \retval verbosity level of the console.
 */
int ast_verb_console_get(void);

/*!
 * \brief Set this thread's console verbosity level.
 *
 * \param verb_level New level to set.
 *
 * \return Nothing
 */
void ast_verb_console_set(int verb_level);

/*!
 * \brief Test if logger is initialized
 *
 * \retval true if the logger is initialized
 */
int ast_is_logger_initialized(void);

/*!
 * \brief Set the maximum number of messages allowed in the processing queue
 *
 * \param queue_limit
 *
 * \return Nothing
 */
void ast_logger_set_queue_limit(int queue_limit);

/*!
 * \brief Get the maximum number of messages allowed in the processing queue
 *
 * \return Queue limit
 */
int ast_logger_get_queue_limit(void);


/*!
 \page Scope_Trace Scope Trace

The Scope Trace facility allows you to instrument code and output scope entry
and exit messages with associated data.

To start using it:
 * You must have used --enable-dev-mode.
 * In logger.conf, set a logger channel to output the "trace" level.
 * Instrument your code as specified below.
 * Use the cli or cli.conf to enable tracing: CLI> core set trace <trace_level> [ module ]

Its simplest usage requires only 1 macro call that...
	- Registers a destructor for a special variable that gets called when the
	  variable goes out of scope.  Uses the same principle as RAII_VAR.
	  The destructor prints the name of the function with an "exiting" indicator
	  along with an optional message.
	- Prints the name of the function with an "entering" indicator along with
	  an optional message.

Simple Example:
The following code...
\code
static struct pjmedia_sdp_session *create_local_sdp(pjsip_inv_session *inv,
	struct ast_sip_session *session, const pjmedia_sdp_session *offer)
{
	SCOPE_TRACE(1, "%s\n", ast_sip_session_get_name(session));
	...
}
\endcode
would produce...
\b [2020-05-17 15:16:51 -0600] TRACE[953402] : --> res_pjsip_session.c:4283 create_local_sdp PJSIP/1173-00000001
\b [2020-05-17 15:16:51 -0600] TRACE[953402] : <-- res_pjsip_session.c:4283 create_local_sdp PJSIP/1173-00000001

There is one odd bit.  There's no way to capture the line number of there the scope exited
so it's always going to be the line where SCOPE_TRACE is located.

Similar to RAII_VAR, any block scope can be traced including "if", "for", "while", etc.
\note "case" statements don't create a scope block by themselves but you can create
a block for it, or use the generic trace functions mentioned below.

Scope Output and Level:

Rather than sending trace messages to the debug facility, a new facility "trace" has been
added to logger.  A corresponding CLI command "core set trace", and a corresponding "trace"
parameter in asterisk.conf were added.  This allows a separate log channel to be created
just for storing trace messages.  The levels are the same as those for debug and verbose.

Scope Indenting:

Each time SCOPE_TRACE or SCOPE_TRACE is called, a thread-local indent value is
incremented on scope enter, and decremented on scope exit.  This allows output
like the following (timestamp omitted for brevity):

TRACE[953402] : --> res_pjsip_session.c:3940 session_inv_on_tsx_state_changed PJSIP/1173-00000001 TSX State: Proceeding  Inv State: CALLING
TRACE[953402] : 	--> res_pjsip_session.c:3680 handle_incoming PJSIP/1173-00000001
TRACE[953402] : 		--> res_pjsip_session.c:3661 handle_incoming_response PJSIP/1173-00000001 Method: INVITE Status: 100
TRACE[953402] : 			--> res_pjsip_session.c:3669 handle_incoming_response PJSIP/1173-00000001 Method: INVITE Status: 100 Supplement: chan_pjsip
TRACE[953402] : 				--> chan_pjsip.c:3265 chan_pjsip_incoming_response_after_media PJSIP/1173-00000001 Method: INVITE Status: 100  After Media
TRACE[953402] : 					--> chan_pjsip.c:3194 chan_pjsip_incoming_response PJSIP/1173-00000001 Method: INVITE Status: 100
TRACE[953402] : 					    chan_pjsip.c:3245 chan_pjsip_incoming_response PJSIP/1173-00000001 Method: INVITE Status: 100  Ignored
TRACE[953402] : 					<-- chan_pjsip.c:3194 chan_pjsip_incoming_response PJSIP/1173-00000001 Method: INVITE Status: 100
TRACE[953402] : 				<-- chan_pjsip.c:3265 chan_pjsip_incoming_response_after_media PJSIP/1173-00000001 Method: INVITE Status: 100  After Media
TRACE[953402] : 			<-- res_pjsip_session.c:3669 handle_incoming_response PJSIP/1173-00000001 Method: INVITE Status: 100 Supplement: chan_pjsip
TRACE[953402] : 		<-- res_pjsip_session.c:3661 handle_incoming_response PJSIP/1173-00000001 Method: INVITE Status: 100
TRACE[953402] : 	<-- res_pjsip_session.c:3680 handle_incoming PJSIP/1173-00000001
TRACE[953402] : <-- res_pjsip_session.c:3940 session_inv_on_tsx_state_changed PJSIP/1173-00000001 TSX State: Proceeding  Inv State: CALLING

\note The trace level indicates which messages to print and has no effect on indent.

Generic Trace Messages:

Sometimes you may just want to print a message to the trace log with the appropriate indent
such as when executing a "case" clause in a "switch" statement. For example, the deepest
message in the sample output above (chan_pjsip.c:3245) is just a single message instead of
an entry/exit message.  To do so, you can use the ast_trace macros...
\code
	ast_trace(1, "%s Method: %.*s Status: %d  Ignored\n", ast_sip_session_get_name(session),
		(int)rdata->msg_info.cseq->method.name.slen, rdata->msg_info.cseq->method.name.ptr, status.code);
\endcode

/note Final note:  The trace facility, like debug, is only available when AST_DEVMODE is defined.

 */

/*!
 * \brief Get the trace level for a module
 * \param module the name of module
 * \return the trace level
 */
unsigned int ast_trace_get_by_module(const char *module);

/*!
 * \brief load logger.conf configuration for console socket connections
 */
void ast_init_logger_for_socket_console(void);

#define TRACE_ATLEAST(level) \
	(option_trace >= (level) \
		|| (ast_opt_trace_module \
			&& ((int)ast_trace_get_by_module(AST_MODULE) >= (level) \
				|| (int)ast_trace_get_by_module(__FILE__) >= (level))))

/*!
 * \brief Controls if and when indenting is applied.
 */
enum ast_trace_indent_type {
	/*! Use the existing indent level */
	AST_TRACE_INDENT_SAME = 0,
	/*! Increment the indent before printing the message */
	AST_TRACE_INDENT_INC_BEFORE,
	/*! Increment the indent after printing the message */
	AST_TRACE_INDENT_INC_AFTER,
	/*! Decrement the indent before printing the message */
	AST_TRACE_INDENT_DEC_BEFORE,
	/*! Decrement the indent after printing the message */
	AST_TRACE_INDENT_DEC_AFTER,
	/*! Set the indent to the one provided */
	AST_TRACE_INDENT_PROVIDED,
	/*! Don't use or alter the level */
	AST_TRACE_INDENT_NONE,
};

#ifdef AST_DEVMODE

void __attribute__((format (printf, 6, 7))) __ast_trace(const char *file, int line, const char *func,
	enum ast_trace_indent_type indent_type, unsigned long indent, const char* format, ...);

/*!
 * \brief Print a trace message
 *
 * \param level The trace level
 * \param indent_type One of the \ref ast_trace_indent_type values
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 *
 */
#define ast_trace_raw(level, indent_type, ...) \
	ast_debug(level < 0 ? __scope_level : level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(level < 0 ? __scope_level : level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, indent_type, 0, " " __VA_ARGS__); \
	}

/*!
 * \brief Print a basic trace message
 *
 * \param level The trace level
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 *
 *  This will print the file, line and function at the current indent level
 */
#define ast_trace(level, ...) \
	ast_debug(level < 0 ? __scope_level : level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(level < 0 ? __scope_level : level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, AST_TRACE_INDENT_SAME, 0, " " __VA_ARGS__); \
	}

/*!
 * \brief Get the current indent level
 *
 * \returns The current indent level
 */
unsigned long _ast_trace_get_indent(void);
#define ast_trace_get_indent() _ast_trace_get_indent()

/*!
 * \brief Set the current indent level
 *
 * \param indent The new indent level
 */
void _ast_trace_set_indent(unsigned long indent);
#define ast_trace_set_indent(indent) _ast_trace_set_indent(indent)

/*!
 * \brief Increment the indent level
 *
 * \returns The new indent level
 */
unsigned long _ast_trace_inc_indent(void);
#define ast_trace_inc_indent() _ast_trace_inc_indent()

/*!
 * \brief Decrement the indent level
 *
 * \returns The new indent level
 */
unsigned long _ast_trace_dec_indent(void);
#define ast_trace_dec_indent() _ast_trace_dec_indent()

/*!
 * \brief Print a trace message with details when a scope is entered or existed.
 *
 * \param level The trace level
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 *
 *  This will print the file, line and function plus details at the current indent level.
 * \note Like RAII_VAR, this macro must be called before any code in the scope.
 *
 * \note The variables used to detect scope change will look like
 * __scopevar1234__EXIT and __scopevar1234__ENTER.
 * The ENTER variable and function are needed to prevent mixed code and declaration issues.
 * If we simple called __ast_trace, then this macro would need to be the last line
 * of scope variable declaration.  The following would fail.
 *
 * 	SCOPE_TRACE(1, "Help!\n");
 * 	int i;
 */
#define SCOPE_TRACE(level, ...) \
	const char *__trace_funcname = __PRETTY_FUNCTION__; \
	auto void __scopevar ## __LINE__ ## __EXIT(void * v); \
	void __scopevar ## __LINE__ ## __EXIT(void * v __attribute__((unused))) { \
		if (TRACE_ATLEAST(level)) { \
			__ast_trace(__FILE__, __LINE__, __trace_funcname, AST_TRACE_INDENT_DEC_BEFORE, 0, " " __VA_ARGS__); \
		} \
	} \
	void *__scopevar ## __LINE__ ## __TRACER __attribute__((cleanup(__scopevar ## __LINE__ ## __EXIT))) = (void *) __PRETTY_FUNCTION__ ; \
	auto int __scopevar ## __LINE__ ## __ENTER(void); \
	int __scopevar ## __LINE__ ## __ENTER(void) { \
		if (TRACE_ATLEAST(level)) { \
			__ast_trace(__FILE__, __LINE__, __trace_funcname, AST_TRACE_INDENT_INC_AFTER, 0, " " __VA_ARGS__); \
		} \
		return 0; \
	} \
	int __scopevar ## __LINE__ ## __RETURN __attribute__((unused)) = __scopevar ## __LINE__ ## __ENTER()

/*!
 * \brief Non RAII_VAR Scope Trace macros
 * The advantage of these macros is that the EXITs will have the actual
 * line number where the scope exited.  Much less code is required as well.
 */

/*!
 * \brief Scope Enter
 *
 * \param level The trace level
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 */
#define SCOPE_ENTER(level, ...) \
	int __scope_level = level; \
	int __scope_task = 0; \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, AST_TRACE_INDENT_INC_AFTER, 0, " " __VA_ARGS__); \
	} \

#define SCOPE_ENTER_TASK(level, indent, ...) \
	int __scope_level = level; \
	int __scope_task = 1; \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, AST_TRACE_INDENT_PROVIDED, indent, " " __VA_ARGS__); \
	} \

/*!
 * \brief Scope Exit
 *
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 *
 * \details
 * This macro can be used at the exit points of a statement block since it just prints the message.
 */
#define SCOPE_EXIT(...) \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(__scope_level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, AST_TRACE_INDENT_DEC_BEFORE, 0, " " __VA_ARGS__); \
		if (__scope_task) { \
			_ast_trace_set_indent(0); \
		} \
	} \

/*!
 * \brief Scope Exit with expression
 *
 * \param __expr An expression to execute after printing the message
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 *
 * \details
 * Handy for getting out of or continuing loops.
 *
 * \example
 * while(something) {
 *     SCOPE_ENTER(2, "In a while\n");
 *     if (something) {
 *         SCOPE_EXIT_EXPR(break, "Somethiung broke me\n");
 *     } else {
 *         SCOPE_EXIT_EXPR(continue, "Somethiung continued me\n");
 *     }
 * }
 */
#define SCOPE_EXIT_EXPR(__expr, ...) \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(__scope_level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, AST_TRACE_INDENT_DEC_BEFORE, 0, " " __VA_ARGS__); \
		if (__scope_task) { \
			_ast_trace_set_indent(0); \
		} \
	} \
	__expr

/*!
 * \brief Scope Exit with return
 *
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 *
 * \details
 * This macro can be used at the exit points of a function when no value
 * needs to be returned.
 */
#define SCOPE_EXIT_RTN(...) \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(__scope_level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, AST_TRACE_INDENT_DEC_BEFORE, 0, " " __VA_ARGS__); \
		if (__scope_task) { \
			_ast_trace_set_indent(0); \
		} \
	} \
	return

/*!
 * \brief Scope Exit with return value
 *
 * \param __return_value The return value
 * \param (optional) A printf style format string
 * \param (optional) Arguments
 *
 * \details
 * This macro can be used at the exit points of a function when a value
 * needs to be returned.
 */
#define SCOPE_EXIT_RTN_VALUE(__return_value, ...) \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	if (TRACE_ATLEAST(__scope_level)) { \
		__ast_trace(__FILE__, __LINE__, __PRETTY_FUNCTION__, AST_TRACE_INDENT_DEC_BEFORE, 0, " " __VA_ARGS__); \
		if (__scope_task) { \
			_ast_trace_set_indent(0); \
		} \
	} \
	return(__return_value)

#else  /* AST_DEVMODE */
#define ast_trace_raw(level, indent_type, ...) \
	ast_debug(level < 0 ? __scope_level : level, " " __VA_ARGS__)

#define ast_trace(level, ...) \
	ast_debug(level < 0 ? __scope_level : level, " " __VA_ARGS__)

#define ast_trace_get_indent() (0)
#define ast_trace_set_indent(indent)
#define ast_trace_inc_indent()
#define ast_trace_dec_indent()
#define SCOPE_TRACE(__level, ...)

#define SCOPE_ENTER(level, ...) \
	int __scope_level = level; \
	ast_debug(level, " " __VA_ARGS__)

#define SCOPE_ENTER_TASK(level, indent, ...) \
	int __scope_level = level; \
	ast_debug(level, " " __VA_ARGS__)

#define SCOPE_EXIT(...) \
	ast_debug(__scope_level, " " __VA_ARGS__)

#define SCOPE_EXIT_EXPR(__expr, ...) \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	__expr

#define SCOPE_EXIT_RTN(...) \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	return

#define SCOPE_EXIT_RTN_VALUE(__return_value, ...) \
	ast_debug(__scope_level, " " __VA_ARGS__); \
	return __return_value

#endif /* AST_DEVMODE */

/*!
 * The following macros will print log messages before running
 * the associated SCOPE_ macro.
 */

#define SCOPE_EXIT_LOG(__log_level, ...) \
({ \
	ast_log(__log_level, " " __VA_ARGS__); \
	SCOPE_EXIT(" " __VA_ARGS__); \
})

#define SCOPE_EXIT_LOG_RTN(__log_level, ...) \
({ \
	ast_log(__log_level, " " __VA_ARGS__); \
	SCOPE_EXIT_RTN(" " __VA_ARGS__); \
})

#define SCOPE_EXIT_LOG_RTN_VALUE(__value, __log_level, ...) \
({ \
	ast_log(__log_level, " " __VA_ARGS__); \
	SCOPE_EXIT_RTN_VALUE(__value, " " __VA_ARGS__); \
})

#define SCOPE_EXIT_LOG_EXPR(__expr, __log_level, ...) \
({ \
	ast_log(__log_level, " " __VA_ARGS__); \
	SCOPE_EXIT_EXPR(__expr, " " __VA_ARGS__); \
})

#define ast_trace_log(__level, __log_level, ...) \
({ \
	ast_log(__log_level, " " __VA_ARGS__); \
	ast_trace(__level < 0 ? __scope_level : __level, " " __VA_ARGS__); \
})


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_LOGGER_H */
