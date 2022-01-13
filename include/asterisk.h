/*
 * Asterisk -- An open source telephony toolkit.
 *
 * General Definitions for Asterisk top level program
 *
 * Copyright (C) 1999-2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief Asterisk main include file. File version handling, generic pbx functions.
 */

#ifndef _ASTERISK_H
#define _ASTERISK_H

#include "asterisk/autoconfig.h"
#include "asterisk/compat.h"
#include "asterisk/astmm.h"

/* Default to allowing the umask or filesystem ACLs to determine actual file
 * creation permissions
 */
#ifndef AST_DIR_MODE
#define AST_DIR_MODE 0777
#endif
#ifndef AST_FILE_MODE
#define AST_FILE_MODE 0666
#endif

/* Make sure PATH_MAX is defined on platforms (HURD) that don't define it.
 * Also be sure to handle the case of a path larger than PATH_MAX
 * (err safely) in the code.
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


#define AST_CURL_USER_AGENT "asterisk-libcurl-agent/1.0"

#define DEFAULT_LANGUAGE "en"

#define DEFAULT_SAMPLE_RATE 8000
#define DEFAULT_SAMPLES_PER_MS  ((DEFAULT_SAMPLE_RATE)/1000)
#define	setpriority	__PLEASE_USE_ast_set_priority_INSTEAD_OF_setpriority__
#define	sched_setscheduler	__PLEASE_USE_ast_set_priority_INSTEAD_OF_sched_setscheduler__
#define	strtok	__PLEASE_USE_strtok_r_INSTEAD_OF_strtok__

#if defined(DEBUG_FD_LEAKS) && !defined(STANDALONE) && !defined(STANDALONE2) && !defined(STANDALONE_AEL)
/* These includes are all about ordering */
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>

#define	open(a,...)	__ast_fdleak_open(__FILE__,__LINE__,__PRETTY_FUNCTION__, a, __VA_ARGS__)
#define pipe(a)		__ast_fdleak_pipe(a, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define socketpair(a,b,c,d)	__ast_fdleak_socketpair(a, b, c, d, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define socket(a,b,c)	__ast_fdleak_socket(a, b, c, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define accept(a,b,c)	__ast_fdleak_accept(a, b, c, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define close(a)	__ast_fdleak_close(a)
#define	fopen(a,b)	__ast_fdleak_fopen(a, b, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define	fclose(a)	__ast_fdleak_fclose(a)
#define	dup2(a,b)	__ast_fdleak_dup2(a, b, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define dup(a)		__ast_fdleak_dup(a, __FILE__,__LINE__,__PRETTY_FUNCTION__)

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif
int __ast_fdleak_open(const char *file, int line, const char *func, const char *path, int flags, ...);
int __ast_fdleak_pipe(int *fds, const char *file, int line, const char *func);
int __ast_fdleak_socketpair(int domain, int type, int protocol, int sv[2],
	const char *file, int line, const char *func);
int __ast_fdleak_socket(int domain, int type, int protocol, const char *file, int line, const char *func);
int __ast_fdleak_accept(int socket, struct sockaddr *address, socklen_t *address_len,
	const char *file, int line, const char *func);
#if defined(HAVE_EVENTFD)
#include <sys/eventfd.h>
#define eventfd(a,b)	__ast_fdleak_eventfd(a,b, __FILE__,__LINE__,__PRETTY_FUNCTION__)
int __ast_fdleak_eventfd(unsigned int initval, int flags, const char *file, int line, const char *func);
#endif
#if defined(HAVE_TIMERFD)
#include <sys/timerfd.h>
#define timerfd_create(a,b)	__ast_fdleak_timerfd_create(a,b, __FILE__,__LINE__,__PRETTY_FUNCTION__)
int __ast_fdleak_timerfd_create(int clockid, int flags, const char *file, int line, const char *func);
#endif
int __ast_fdleak_close(int fd);
FILE *__ast_fdleak_fopen(const char *path, const char *mode, const char *file, int line, const char *func);
int __ast_fdleak_fclose(FILE *ptr);
int __ast_fdleak_dup2(int oldfd, int newfd, const char *file, int line, const char *func);
int __ast_fdleak_dup(int oldfd, const char *file, int line, const char *func);
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif
#endif

int ast_set_priority(int);			/*!< Provided by asterisk.c */
int ast_fd_init(void);				/*!< Provided by astfd.c */
int ast_pbx_init(void);				/*!< Provided by pbx.c */

/*!
 * \brief Register a function to be executed before Asterisk exits.
 * \param func The callback function to use.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note This function should be rarely used in situations where
 * something must be shutdown to avoid corruption, excessive data
 * loss, or when external programs must be stopped.  All other
 * cleanup in the core should use ast_register_cleanup.
 */
int ast_register_atexit(void (*func)(void));

/*!
 * \since 11.9
 * \brief Register a function to be executed before Asterisk gracefully exits.
 *
 * If Asterisk is immediately shutdown (core stop now, or sending the TERM
 * signal), the callback is not run. When the callbacks are run, they are run in
 * sequence with ast_register_atexit() callbacks, in the reverse order of
 * registration.
 *
 * \param func The callback function to use.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_register_cleanup(void (*func)(void));

/*!
 * \brief Unregister a function registered with ast_register_atexit().
 * \param func The callback function to unregister.
 */
void ast_unregister_atexit(void (*func)(void));

/*!
 * \brief Cancel an existing shutdown and return to normal operation.
 *
 * \note Shutdown can be cancelled while the server is waiting for
 * any existing channels to be destroyed before shutdown becomes
 * irreversible.
 *
 * \return non-zero if shutdown cancelled.
 */
int ast_cancel_shutdown(void);

/*!
 * \details
 * The server is preventing new channel creation in preparation for
 * shutdown and may actively be releasing resources.  The shutdown
 * process may be canceled by ast_cancel_shutdown() if it is not too
 * late.
 *
 * \note The preparation to shutdown phase can be quite lengthy
 * if we are gracefully shutting down.  How long existing calls will
 * last is not up to us.
 *
 * \return non-zero if the server is preparing to or actively shutting down.
 */
int ast_shutting_down(void);

/*!
 * \return non-zero if the server is actively shutting down.
 * \since 13.3.0
 *
 * \details
 * The server is releasing resources and unloading modules.
 * It won't be long now.
 */
int ast_shutdown_final(void);

#ifdef MTX_PROFILE
#define	HAVE_MTX_PROFILE	/* used in lock.h */
#endif /* MTX_PROFILE */

/*!
 * \brief support for event profiling
 *
 * (note, this must be documented a lot more)
 * ast_add_profile allocates a generic 'counter' with a given name,
 * which can be shown with the command 'core show profile &lt;name&gt;'
 *
 * The counter accumulates positive or negative values supplied by
 * \see ast_add_profile(), dividing them by the 'scale' value passed in the
 * create call, and also counts the number of 'events'.
 * Values can also be taked by the TSC counter on ia32 architectures,
 * in which case you can mark the start of an event calling ast_mark(id, 1)
 * and then the end of the event with ast_mark(id, 0).
 * For non-i386 architectures, these two calls return 0.
 */
int ast_add_profile(const char *, uint64_t scale);
int64_t ast_profile(int, int64_t);
int64_t ast_mark(int, int start1_stop0);

/*! \brief
 * Definition of various structures that many asterisk files need,
 * but only because they need to know that the type exists.
 *
 */

struct ast_channel;
struct ast_frame;
struct ast_module;
struct ast_variable;
struct ast_str;
struct ast_sched_context;
struct ast_json;

/* Some handy macros for turning a preprocessor token into (effectively) a quoted string */
#define __stringify_1(x)	#x
#define __stringify(x)		__stringify_1(x)

#if defined(AST_IN_CORE) \
	|| (!defined(AST_MODULE_SELF_SYM) \
		&& (defined(STANDALONE) || defined(STANDALONE2) || defined(AST_NOT_MODULE)))

#define AST_MODULE_SELF NULL

#elif defined(AST_MODULE_SELF_SYM)

/*! Retrieve the 'struct ast_module *' for the current module. */
#define AST_MODULE_SELF AST_MODULE_SELF_SYM()

struct ast_module;
/* Internal/forward declaration, AST_MODULE_SELF should be used instead. */
struct ast_module *AST_MODULE_SELF_SYM(void);

#else

#error "Externally compiled modules must declare AST_MODULE_SELF_SYM."

#endif

/*!
 * \brief Retrieve the PBX UUID
 * \param pbx_uuid A buffer of at least AST_UUID_STR_LEN (36 + 1) size to receive the UUID
 * \param length The buffer length
 */
int ast_pbx_uuid_get(char *pbx_uuid, int length);

#endif /* _ASTERISK_H */
