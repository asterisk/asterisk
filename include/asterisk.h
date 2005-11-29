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

#ifndef _ASTERISK_H
#define _ASTERISK_H

#define DEFAULT_LANGUAGE "en"

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

/* Provided by asterisk.c */
extern int ast_set_priority(int);
/* Provided by module.c */
extern int load_modules(const int preload_only);
/* Provided by pbx.c */
extern int load_pbx(void);
/* Provided by logger.c */
extern int init_logger(void);
extern void close_logger(void);
/* Provided by frame.c */
extern int init_framer(void);
/* Provided by logger.c */
extern int reload_logger(int);
/* Provided by term.c */
extern int term_init(void);
/* Provided by db.c */
extern int astdb_init(void);
/* Provided by channel.c */
extern void ast_channels_init(void);
/* Provided by dnsmgr.c */
extern int dnsmgr_init(void);
extern void dnsmgr_reload(void);

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
#if defined(__GNUC__) && !defined(LOW_MEMORY)
#define ASTERISK_FILE_VERSION(file, version) \
	static void __attribute__((constructor)) __register_file_version(void) \
	{ \
		ast_register_file_version(file, version); \
	} \
	static void __attribute__((destructor)) __unregister_file_version(void) \
	{ \
		ast_unregister_file_version(file); \
	}
#elif !defined(LOW_MEMORY) /* ! __GNUC__  && ! LOW_MEMORY*/
#define ASTERISK_FILE_VERSION(file, x) static const char __file_version[] = x;
#else /* LOW_MEMORY */
#define ASTERISK_FILE_VERSION(file, x)
#endif /* __GNUC__ */

#endif /* _ASTERISK_H */
