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

#define AST_CONFIG_DIR "/etc/asterisk"
#define AST_MODULE_DIR "/usr/lib/asterisk/modules"
#define AST_SPOOL_DIR  "/var/spool/asterisk"
#define AST_VAR_DIR    "/var/lib/asterisk"
#define AST_LOG_DIR	   "/var/log/asterisk"

#define AST_CONFIG_FILE "asterisk.conf"

#define AST_SOUNDS AST_VAR_DIR "/sounds"

/* Provided by module.c */
extern int load_modules(void);
/* Provided by pbx.c */
extern int load_pbx(void);
/* Provided by logger.c */
extern int init_logger(void);

#endif
