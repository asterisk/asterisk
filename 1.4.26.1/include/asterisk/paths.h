/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Paths to configurable Asterisk directories
 * 
 * Copyright (C) 1999-2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief Asterisk file paths, configured in asterisk.conf
 */

#ifndef _ASTERISK_PATHS_H
#define _ASTERISK_PATHS_H

#include <limits.h>

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

#endif /* _ASTERISK_PATHS_H */
