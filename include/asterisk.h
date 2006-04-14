/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General Definitions for Asterisk top level program
 * 
 * Copyright (C) 1999-2005, Mark Spencer
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief Asterisk main include file. File version handling, generic pbx functions.
 */

#include "asterisk/compat.h"

#ifndef _ASTERISK_H
#define _ASTERISK_H

#define DEFAULT_LANGUAGE "en"

#define DEFAULT_SAMPLE_RATE 8000
#define DEFAULT_SAMPLES_PER_MS  ((DEFAULT_SAMPLE_RATE)/1000)

#define AST_CONFIG_MAX_PATH 255

/* provided in asterisk.c */
extern char ast_config_AST_CONFIG_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_CONFIG_FILE[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_MODULE_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_SPOOL_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_MONITOR_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_VAR_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_LOG_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_AGI_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_DB[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_KEY_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_PID[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_SOCKET[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_RUN_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_CTL_PERMISSIONS[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_CTL_OWNER[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_CTL_GROUP[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_CTL[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_SYSTEM_NAME[20];

/* Provided by asterisk.c */
int ast_set_priority(int);
/* Provided by module.c */
int load_modules(const int preload_only);
/* Provided by pbx.c */
int load_pbx(void);
/* Provided by logger.c */
int init_logger(void);
void close_logger(void);
/* Provided by frame.c */
int init_framer(void);
/* Provided by logger.c */
int reload_logger(int);
/* Provided by term.c */
int term_init(void);
/* Provided by db.c */
int astdb_init(void);
/* Provided by channel.c */
void ast_channels_init(void);
/* Provided by cli.c */
void ast_builtins_init(void);
/* Provided by dnsmgr.c */
int dnsmgr_init(void);
void dnsmgr_start_refresh(void);
int dnsmgr_reload(void);

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
 * \brief support for event profiling
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
#if defined(__GNUC__) && !defined(LOW_MEMORY)
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
#else
#define ASTERISK_FILE_VERSION(file, version) \
	static void __attribute__((constructor)) __register_file_version(void) \
	{ \
		ast_register_file_version(file, version); \
	} \
	static void __attribute__((destructor)) __unregister_file_version(void) \
	{ \
		ast_unregister_file_version(file); \
	}
#endif
#elif !defined(LOW_MEMORY) /* ! __GNUC__  && ! LOW_MEMORY*/
#define ASTERISK_FILE_VERSION(file, x) static const char __file_version[] = x;
#else /* LOW_MEMORY */
#define ASTERISK_FILE_VERSION(file, x)
#endif /* __GNUC__ */

#endif /* _ASTERISK_H */
