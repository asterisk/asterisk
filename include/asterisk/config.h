/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser
 * 
 * Copyright (C) 1999, Mark Spencer
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

struct ast_comment {
	char *comment;
	struct ast_comment *next;
};

struct ast_variable {
	char *name;
	char *value;
	int lineno;
	int object;		/* 0 for variable, 1 for object */
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_variable *next;
};

//! Load a config file
/*! 
 * \param configfile path of file to open.  If no preceding '/' character, path is considered relative to AST_CONFIG_DIR
 * Create a config structure from a given configuration file.
 * Returns NULL on error, or an ast_config data structure on success
 */
struct ast_config *ast_load(char *configfile);

//! Removes a config
/*!
 * \param config config data structure associated with the config.
 * Free memory associated with a given config
 * Returns nothing
 */
void ast_destroy(struct ast_config *config);

//! Goes through categories
/*!
 * \param config Which config file you wish to "browse"
 * \param prev A pointer to a previous category.
 * This funtion is kind of non-intuitive in it's use.  To begin, one passes NULL as the second arguement.  It will return a pointer to the string of the first category in the file.  From here on after, one must then pass the previous usage's return value as the second pointer, and it will return a pointer to the category name afterwards.  Note:  If you manually strcpy a string into a character array and pass it thinking it will return your category, it will not; the comparisons are not done doing strcmp, they are done by checking whether the value of the string POINTER is the same.
 * Returns a category on success, or NULL on failure/no-more-categories
 */
char *ast_category_browse(struct ast_config *config, char *prev);

//! Goes through variables
/*!
 * Somewhat similar in intent as the ast_category_browse.  The category MUST be an actual pointer to an actual category (such as one obtained by using ast_category_browse()).
 * List variables of config file
 * Returns ast_variable list on success, or NULL on failure
 */
struct ast_variable *ast_variable_browse(struct ast_config *config, char *category);

//! Gets a variable
/*!
 * \param config which (opened) config to use
 * \param category category under which the variable lies (must be a pointer to the category, such as one given by ast_category_browse)
 * \param value which variable you wish to get the data for
 * Goes through a given config file in the given category and searches for the given variable
 * Returns the variable value on success, or NULL if unable to find it.
 * Retrieve a specific variable */
char *ast_variable_retrieve(struct ast_config *config, char *category, char *value);

//! Make sure something is true
/*!
 * Determine affermativeness of a boolean value.
 * This function checks to see whether a string passed to it is an indication of an affirmitave value.  It checks to see if the string is "yes", "true", "y", "t", and "1".  
 * Returns 0 if the value of s is a NULL pointer, 0 on "truth", and -1 on falsehood.
 */
int ast_true(char *val);

//! Check for category duplicates
/*!
 * \param config which config to use
 * \param category_name name of the category you're looking for
 * This will search through the categories within a given config file and search for a match.  The passed category_name can be a regular string (as opposed to a pointer of an existent string, lol)
 * Browse config structure and check for category duplicity Return non-zero if found */
int ast_category_exist(struct ast_config *config, char *category_name);

/* These are only in the config engine at this point */
struct ast_variable *ast_variable_append_modify(struct ast_config *cfg, char *category, char *variable, char *newvalue, int newcat, int newvar, int move);

int ast_category_delete(struct ast_config *cfg, char *category);
int ast_variable_delete(struct ast_config *cfg, char *category, char *variable, char *value);
int ast_save(char *filename, struct ast_config *cfg, char *generator);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
