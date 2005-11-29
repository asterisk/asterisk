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

#ifndef _ASTCONF_H
#define _ASTCONF_H

#define AST_CONFIG_MAX_PATH 255

extern char ast_config_AST_CONFIG_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_CONFIG_FILE[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_MODULE_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_SPOOL_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_VAR_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_LOG_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_AGI_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_DB[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_KEY_DIR[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_PID[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_SOCKET[AST_CONFIG_MAX_PATH];
extern char ast_config_AST_RUN_DIR[AST_CONFIG_MAX_PATH];

#endif
