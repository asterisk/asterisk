/*
 * Asterisk -- A telephony toolkit for Linux.
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

#if !defined(NO_MALLOC_DEBUG) && !defined(STANDALONE_AEL) && defined(MALLOC_DEBUG)
#include "asterisk/astmm.h"
#endif

#include "asterisk/compat.h"

#include "asterisk/paths.h"

#define DEFAULT_LANGUAGE "en"

#define DEFAULT_SAMPLE_RATE 8000
#define DEFAULT_SAMPLES_PER_MS  ((DEFAULT_SAMPLE_RATE)/1000)
#define	setpriority	__PLEASE_USE_ast_set_priority_INSTEAD_OF_setpriority__
#define	sched_setscheduler	__PLEASE_USE_ast_set_priority_INSTEAD_OF_sched_setscheduler__

#if defined(DEBUG_FD_LEAKS) && !defined(STANDALONE) && !defined(STANDALONE_AEL)
/* These includes are all about ordering */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#define	open(a,...)	__ast_fdleak_open(__FILE__,__LINE__,__PRETTY_FUNCTION__, a, __VA_ARGS__)
#define pipe(a)	__ast_fdleak_pipe(a, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define socket(a,b,c)	__ast_fdleak_socket(a, b, c, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define close(a)	__ast_fdleak_close(a)
#define	fopen(a,b)	__ast_fdleak_fopen(a, b, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define	fclose(a)	__ast_fdleak_fclose(a)
#define	dup2(a,b)	__ast_fdleak_dup2(a, b, __FILE__,__LINE__,__PRETTY_FUNCTION__)
#define dup(a)	__ast_fdleak_dup(a, __FILE__,__LINE__,__PRETTY_FUNCTION__)

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif
int __ast_fdleak_open(const char *file, int line, const char *func, const char *path, int flags, ...);
int __ast_fdleak_pipe(int *fds, const char *file, int line, const char *func);
int __ast_fdleak_socket(int domain, int type, int protocol, const char *file, int line, const char *func);
int __ast_fdleak_close(int fd);
FILE *__ast_fdleak_fopen(const char *path, const char *mode, const char *file, int line, const char *func);
int __ast_fdleak_fclose(FILE *ptr);
int __ast_fdleak_dup2(int oldfd, int newfd, const char *file, int line, const char *func);
int __ast_fdleak_dup(int oldfd, const char *file, int line, const char *func);
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif
#endif

/* provided in asterisk.c */
extern char ast_config_AST_CONFIG_DIR[PATH_MAX];
extern char ast_config_AST_CONFIG_FILE[PATH_MAX];
extern char ast_config_AST_MODULE_DIR[PATH_MAX];
extern char ast_config_AST_SPOOL_DIR[PATH_MAX];
extern char ast_config_AST_MONITOR_DIR[PATH_MAX];
extern char ast_config_AST_VAR_DIR[PATH_MAX];
extern char ast_config_AST_DATA_DIR[PATH_MAX];
extern char ast_config_AST_LOG_DIR[PATH_MAX];
extern char ast_config_AST_AGI_DIR[PATH_MAX];
extern char ast_config_AST_DB[PATH_MAX];
extern char ast_config_AST_KEY_DIR[PATH_MAX];
extern char ast_config_AST_PID[PATH_MAX];
extern char ast_config_AST_SOCKET[PATH_MAX];
extern char ast_config_AST_RUN_DIR[PATH_MAX];
extern char ast_config_AST_CTL_PERMISSIONS[PATH_MAX];
extern char ast_config_AST_CTL_OWNER[PATH_MAX];
extern char ast_config_AST_CTL_GROUP[PATH_MAX];
extern char ast_config_AST_CTL[PATH_MAX];
extern char ast_config_AST_SYSTEM_NAME[20];

int ast_set_priority(int);			/*!< Provided by asterisk.c */
int load_modules(unsigned int);			/*!< Provided by loader.c */
int load_pbx(void);				/*!< Provided by pbx.c */
int init_logger(void);				/*!< Provided by logger.c */
void close_logger(void);			/*!< Provided by logger.c */
int reload_logger(int);				/*!< Provided by logger.c */
int init_framer(void);				/*!< Provided by frame.c */
int ast_term_init(void);			/*!< Provided by term.c */
int astdb_init(void);				/*!< Provided by db.c */
void ast_channels_init(void);			/*!< Provided by channel.c */
void ast_builtins_init(void);			/*!< Provided by cli.c */
int dnsmgr_init(void);				/*!< Provided by dnsmgr.c */ 
void dnsmgr_start_refresh(void);		/*!< Provided by dnsmgr.c */
int dnsmgr_reload(void);			/*!< Provided by dnsmgr.c */
void threadstorage_init(void);			/*!< Provided by threadstorage.c */
int astobj2_init(void);				/*! Provided by astobj2.c */
void ast_autoservice_init(void);    /*!< Provided by autoservice.c */
int ast_fd_init(void);				/*!< Provided by astfd.c */

/* Many headers need 'ast_channel' to be defined */
struct ast_channel;

/* Many headers need 'ast_module' to be defined */
struct ast_module;

/*!
 * \brief Reload asterisk modules.
 * \param name the name of the module to reload
 *
 * This function reloads the specified module, or if no modules are specified,
 * it will reload all loaded modules.
 *
 * \note Modules are reloaded using their reload() functions, not unloading
 * them and loading them again.
 * 
 * \return Zero if the specified module was not found, 1 if the module was
 * found but cannot be reloaded, -1 if a reload operation is already in
 * progress, and 2 if the specfied module was found and reloaded.
 */
int ast_module_reload(const char *name);

/*!
 * \brief Process reload requests received during startup.
 *
 * This function requests that the loader execute the pending reload requests
 * that were queued during server startup.
 *
 * \note This function will do nothing if the server has not completely started
 *       up.  Once called, the reload queue is emptied, and further invocations
 *       will have no affect.
 */
void ast_process_pending_reloads(void);

/*!
 * \brief Register a function to be executed before Asterisk exits.
 * \param func The callback function to use.
 *
 * \return Zero on success, -1 on error.
 */
int ast_register_atexit(void (*func)(void));

/*!   
 * \brief Unregister a function registered with ast_register_atexit().
 * \param func The callback function to unregister.   
 */
void ast_unregister_atexit(void (*func)(void));

#if !defined(LOW_MEMORY)
/*!
 * \brief Register the version of a source code file with the core.
 * \param file the source file name
 * \param version the version string (typically a CVS revision keyword string)
 * \return nothing
 *
 * This function should not be called directly, but instead the
 * ASTERISK_FILE_VERSION macro should be used to register a file with the core.
 */
void ast_register_file_version(const char *file, const char *version);

/*!
 * \brief Unregister a source code file from the core.
 * \param file the source file name
 * \return nothing
 *
 * This function should not be called directly, but instead the
 * ASTERISK_FILE_VERSION macro should be used to automatically unregister
 * the file when the module is unloaded.
 */
void ast_unregister_file_version(const char *file);

/*!
 * \brief Register/unregister a source code file with the core.
 * \param file the source file name
 * \param version the version string (typically a CVS revision keyword string)
 *
 * This macro will place a file-scope constructor and destructor into the
 * source of the module using it; this will cause the version of this file
 * to registered with the Asterisk core (and unregistered) at the appropriate
 * times.
 *
 * Example:
 *
 * \code
 * ASTERISK_FILE_VERSION(__FILE__, "\$Revision\$")
 * \endcode
 *
 * \note The dollar signs above have been protected with backslashes to keep
 * CVS from modifying them in this file; under normal circumstances they would
 * not be present and CVS would expand the Revision keyword into the file's
 * revision number.
 */
#ifdef MTX_PROFILE
#define	HAVE_MTX_PROFILE	/* used in lock.h */
#define ASTERISK_FILE_VERSION(file, version) \
	static int mtx_prof = -1;       /* profile mutex */	\
	static void __attribute__((constructor)) __register_file_version(void) \
	{ \
		mtx_prof = ast_add_profile("mtx_lock_" file, 0);	\
		ast_register_file_version(file, version); \
	} \
	static void __attribute__((destructor)) __unregister_file_version(void) \
	{ \
		ast_unregister_file_version(file); \
	}
#else /* !MTX_PROFILE */
#define ASTERISK_FILE_VERSION(file, version) \
	static void __attribute__((constructor)) __register_file_version(void) \
	{ \
		ast_register_file_version(file, version); \
	} \
	static void __attribute__((destructor)) __unregister_file_version(void) \
	{ \
		ast_unregister_file_version(file); \
	}
#endif /* !MTX_PROFILE */
#else /* LOW_MEMORY */
#define ASTERISK_FILE_VERSION(file, x)
#endif /* LOW_MEMORY */

#if !defined(LOW_MEMORY)
/*!
 * \brief support for event profiling
 *
 * (note, this must be documented a lot more)
 * ast_add_profile allocates a generic 'counter' with a given name,
 * which can be shown with the command 'show profile <name>'
 *
 * The counter accumulates positive or negative values supplied by
 * ast_add_profile(), dividing them by the 'scale' value passed in the
 * create call, and also counts the number of 'events'.
 * Values can also be taked by the TSC counter on ia32 architectures,
 * in which case you can mark the start of an event calling ast_mark(id, 1)
 * and then the end of the event with ast_mark(id, 0).
 * For non-i386 architectures, these two calls return 0.
 */
int ast_add_profile(const char *, uint64_t scale);
int64_t ast_profile(int, int64_t);
int64_t ast_mark(int, int start1_stop0);
#else /* LOW_MEMORY */
#define ast_add_profile(a, b) 0
#define ast_profile(a, b) do { } while (0)
#define ast_mark(a, b) do { } while (0)
#endif /* LOW_MEMORY */

#endif /* _ASTERISK_H */
