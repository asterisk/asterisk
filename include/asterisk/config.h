/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_CONFIG_H
#define _ASTERISK_CONFIG_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_config;

struct ast_variable {
	char *name;
	char *value;
	struct ast_variable *next;
};

/* Create a config structure from a given configuration file */
struct ast_config *ast_load(char *configfile);
/* Free memory associated with a given config */
void ast_destroy(struct ast_config *config);
/* List categories of config file */
char *ast_category_browse(struct ast_config *config, char *prev);
/* List variables of config file */
struct ast_variable *ast_variable_browse(struct ast_config *config, char *category);
/* Retrieve a specific variable */
char *ast_variable_retrieve(struct ast_config *config, char *category, char *value);
/* Determine affermativeness of a boolean value */
int ast_true(char *val);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
