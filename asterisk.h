/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General Definitions for Asterisk top level program
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_H
#define _ASTERISK_H

#define DEFAULT_LANGUAGE "en"

#define AST_CONFIG_MAX_PATH 255
#define AST_CONFIG_DIR 	ASTETCDIR
#define AST_RUN_DIR	ASTVARRUNDIR
#define AST_SOCKET	ASTVARRUNDIR "/asterisk.ctl"
#define AST_PID		ASTVARRUNDIR "/asterisk.pid"
#define AST_MODULE_DIR 	ASTMODDIR
#define AST_SPOOL_DIR  	ASTSPOOLDIR
#define AST_VAR_DIR    	ASTVARLIBDIR
#define AST_LOG_DIR	ASTLOGDIR
#define AST_AGI_DIR	ASTAGIDIR
#define AST_KEY_DIR	ASTVARLIBDIR "/keys"
#define AST_DB		ASTVARLIBDIR "/astdb"
#define AST_TMP_DIR	ASTSPOOLDIR "/tmp"

#define AST_CONFIG_FILE ASTCONFPATH

#define AST_SOUNDS AST_VAR_DIR "/sounds"
#define AST_IMAGES AST_VAR_DIR "/images"

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

#endif
