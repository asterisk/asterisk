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

/* Provided by module.c */
extern int load_modules(void);
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

void ast_register_file_version(const char *file, const char *version);
void ast_unregister_file_version(const char *file);

#ifdef __GNUC__
#define ASTERISK_FILE_VERSION(file, version) \
	static void __attribute__((constructor)) __register_file_version(void) \
	{ \
		ast_register_file_version(file, version); \
	} \
	static void __attribute__((destructor)) __unregister_file_version(void) \
	{ \
		ast_unregister_file_version(file); \
	}
#else /* ! __GNUC__ */
#define ASTERISK_FILE_VERSION(x) static const char __file_version[] = x;
#endif /* __GNUC__ */

#endif /* _ASTERISK_H */
