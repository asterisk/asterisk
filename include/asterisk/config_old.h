/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser (Deprecated APIs)
 * 
 * Copyright (C) 1999-2005, Mark Spencer
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_CONFIG_OLD_H
#define _ASTERISK_CONFIG_OLD_H

/*! Load a config file */
/*! 
 * \param configfile path of file to open.  If no preceding '/' character, path is considered relative to AST_CONFIG_DIR
 * Create a config structure from a given configuration file.
 * Returns NULL on error, or an ast_config data structure on success
 */
struct ast_config *ast_load(char *configfile);

/*! Removes a config */
/*!
 * \param config config data structure associated with the config.
 * Free memory associated with a given config
 * Returns nothing
 */
void ast_destroy(struct ast_config *config);

/*! Free variable list */
/*!
 * \param var the linked list of variables to free
 * This function frees a list of variables.
 */
void ast_destroy_realtime(struct ast_variable *var);

struct ast_config *ast_internal_load(const char *configfile, struct ast_config *cfg);

#endif
