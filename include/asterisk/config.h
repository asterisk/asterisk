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
	struct ast_comment *next;
	char cmt[0];
};

struct ast_variable {
	char *name;
	char *value;
	int lineno;
	int object;		/* 0 for variable, 1 for object */
	int blanklines; 	/* Number of blanklines following entry */
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_variable *next;
	char stuff[0];
};

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

/*! Goes through categories */
/*!
 * \param config Which config file you wish to "browse"
 * \param prev A pointer to a previous category.
 * This funtion is kind of non-intuitive in it's use.  To begin, one passes NULL as the second arguement.  It will return a pointer to the string of the first category in the file.  From here on after, one must then pass the previous usage's return value as the second pointer, and it will return a pointer to the category name afterwards.  Note:  If you manually strcpy a string into a character array and pass it thinking it will return your category, it will not; the comparisons are not done doing strcmp, they are done by checking whether the value of the string POINTER is the same.
 * Returns a category on success, or NULL on failure/no-more-categories
 */
char *ast_category_browse(struct ast_config *config, char *prev);

/*! Goes through variables */
/*!
 * Somewhat similar in intent as the ast_category_browse.  The category MUST be an actual pointer to an actual category (such as one obtained by using ast_category_browse()).
 * List variables of config file
 * Returns ast_variable list on success, or NULL on failure
 */
struct ast_variable *ast_variable_browse(const struct ast_config *config, const char *category);

/*! Gets a variable */
/*!
 * \param config which (opened) config to use
 * \param category category under which the variable lies (must be a pointer to the category, such as one given by ast_category_browse)
 * \param value which variable you wish to get the data for
 * Goes through a given config file in the given category and searches for the given variable
 * Returns the variable value on success, or NULL if unable to find it.
 * Retrieve a specific variable */
char *ast_variable_retrieve(const struct ast_config *config, const char *category, const char *value);

/*! Make sure something is true */
/*!
 * Determine affermativeness of a boolean value.
 * This function checks to see whether a string passed to it is an indication of an affirmitave value.  It checks to see if the string is "yes", "true", "y", "t", and "1".  
 * Returns 0 if the value of s is a NULL pointer, 0 on "truth", and -1 on falsehood.
 */
int ast_true(const char *val);

/*! Make sure something is false */
/*!
 * Determine falseness of a boolean value.
 * This function checks to see whether a string passed to it is an indication of a negatirve value.  It checks to see if the string is "no", "false", "n", "f", and "0".  
 * Returns 0 if the value of s is a NULL pointer, 0 on "truth", and -1 on falsehood.
 */
int ast_false(const char *val);

/*! Retrieve a category if it exists
 * \param config which config to use
 * \param category_name name of the category you're looking for
 * This will search through the categories within a given config file and search for a match.  The passed category_name can be a regular string.
 * Returns pointer to category if found, NULL if not. */
struct ast_category *ast_category_get(const struct ast_config *config, const char *category_name);

/*! Check for category duplicates */
/*!
 * \param config which config to use
 * \param category_name name of the category you're looking for
 * This will search through the categories within a given config file and search for a match.  The passed category_name can be a regular string (as opposed to a pointer of an existent string, lol)
 * Browse config structure and check for category duplicity Return non-zero if found */
int ast_category_exist(struct ast_config *config, char *category_name);

/*! Retrieve realtime configuration */
/*!
 * \param family which family/config to lookup
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * This will use builtin configuration backends to look up a particular 
 * entity in realtime and return a variable list of its parameters.  Note
 * that unlike the variables in ast_config, the resulting list of variables
 * MUST be fred with ast_free_runtime() as there is no container.
 */
struct ast_variable *ast_load_realtime(const char *family, ...);

/*! Retrieve realtime configuration */
/*!
 * \param family which family/config to lookup
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * This will use builtin configuration backends to look up a particular 
 * entity in realtime and return a variable list of its parameters. Unlike
 * the ast_load_realtime, this function can return more than one entry and
 * is thus stored inside a taditional ast_config structure rather than 
 * just returning a linked list of variables.
 */
struct ast_config *ast_load_realtime_multientry(const char *family, ...);

/*! Update realtime configuration */
/*!
 * \param family which family/config to be updated
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * \param variable which variable should be updated in the config, NULL to end list
 * \param value the value to be assigned to that variable in the given entity.
 * This function is used to update a parameter in realtime configuration space.
 *
 */
int ast_update_realtime(const char *family, const char *keyfield, const char *lookup, ...);

/*! Free realtime configuration */
/*!
 * \param var the linked list of variables to free
 * This command free's a list of variables and should ONLY be used
 * in conjunction with ast_load_realtime and not with the regular ast_load.
 */
void ast_destroy_realtime(struct ast_variable *var);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
