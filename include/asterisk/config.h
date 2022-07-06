/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Configuration File Parser
 */

#ifndef _ASTERISK_CONFIG_H
#define _ASTERISK_CONFIG_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/utils.h"
#include "asterisk/inline_api.h"

struct ast_config;

struct ast_category;

/*! Options for ast_config_load()
 */
enum {
	/*! Load the configuration, including comments */
	CONFIG_FLAG_WITHCOMMENTS  = (1 << 0),
	/*! On a reload, give us a -1 if the file hasn't changed. */
	CONFIG_FLAG_FILEUNCHANGED = (1 << 1),
	/*! Don't attempt to cache mtime on this config file. */
	CONFIG_FLAG_NOCACHE       = (1 << 2),
	/*! Don't attempt to load from realtime (typically called from a realtime driver dependency) */
	CONFIG_FLAG_NOREALTIME    = (1 << 3),
};

/*! Flags for ast_config_text_file_save2()
 */
enum config_save_flags {
	CONFIG_SAVE_FLAG_NONE = (0),
	/*! Insure a context doesn't effectively change if a template changes (pre 13.2 behavior) */
	CONFIG_SAVE_FLAG_PRESERVE_EFFECTIVE_CONTEXT = (1 << 0),
};

#define	CONFIG_STATUS_FILEMISSING	(void *)0
#define	CONFIG_STATUS_FILEUNCHANGED	(void *)-1
#define	CONFIG_STATUS_FILEINVALID	(void *)-2

/*!
 * \brief Types used in ast_realtime_require_field
 */
typedef enum {
	RQ_INTEGER1,
	RQ_UINTEGER1,
	RQ_INTEGER2,
	RQ_UINTEGER2,
	RQ_INTEGER3,
	RQ_UINTEGER3,
	RQ_INTEGER4,
	RQ_UINTEGER4,
	RQ_INTEGER8,
	RQ_UINTEGER8,
	RQ_CHAR,
	RQ_FLOAT,
	RQ_DATE,
	RQ_DATETIME,
} require_type;

/*! \brief Structure for variables, used for configurations and for channel variables */
struct ast_variable {
	/*! Variable name.  Stored in stuff[] at struct end. */
	const char *name;
	/*! Variable value.  Stored in stuff[] at struct end. */
	const char *value;

	/*! Next node in the list. */
	struct ast_variable *next;

	/*! Filename where variable found.  Stored in stuff[] at struct end. */
	const char *file;

	int lineno;
	int object;		/*!< 0 for variable, 1 for object */
	int blanklines;		/*!< Number of blanklines following entry */
	int inherited;		/*!< 1 for inherited from template or other base */
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_comment *trailing; /*!< the last object in the list will get assigned any trailing comments when EOF is hit */
	/*!
	 * \brief Contents of file, name, and value in that order stuffed here.
	 * \note File must be stuffed before name because of ast_include_rename().
	 */
	char stuff[0];
};

typedef struct ast_config *config_load_func(const char *database, const char *table, const char *configfile, struct ast_config *config, struct ast_flags flags, const char *suggested_include_file, const char *who_asked);
typedef struct ast_variable *realtime_var_get(const char *database, const char *table, const struct ast_variable *fields);
typedef struct ast_config *realtime_multi_get(const char *database, const char *table, const struct ast_variable *fields);
typedef int realtime_update(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields);
typedef int realtime_update2(const char *database, const char *table, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields);
typedef int realtime_store(const char *database, const char *table, const struct ast_variable *fields);
typedef int realtime_destroy(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields);

/*!
 * \brief Function pointer called to ensure database schema is properly configured for realtime use
 * \since 1.6.1
 */
typedef int realtime_require(const char *database, const char *table, va_list ap);

/*!
 * \brief Function pointer called to clear the database cache and free resources used for such
 * \since 1.6.1
 */
typedef int realtime_unload(const char *database, const char *table);

/*! \brief Configuration engine structure, used to define realtime drivers */
struct ast_config_engine {
	char *name;
	config_load_func *load_func;
	realtime_var_get *realtime_func;
	realtime_multi_get *realtime_multi_func;
	realtime_update *update_func;
	realtime_update2 *update2_func;
	realtime_store *store_func;
	realtime_destroy *destroy_func;
	realtime_require *require_func;
	realtime_unload *unload_func;
	struct ast_config_engine *next;
};

/*!
 * \brief Load a config file
 *
 * \param filename path of file to open.  If no preceding '/' character,
 * path is considered relative to AST_CONFIG_DIR
 * \param who_asked The module which is making this request.
 * \param flags Optional flags:
 * CONFIG_FLAG_WITHCOMMENTS - load the file with comments intact;
 * CONFIG_FLAG_FILEUNCHANGED - check the file mtime and return CONFIG_STATUS_FILEUNCHANGED if the mtime is the same; or
 * CONFIG_FLAG_NOCACHE - don't cache file mtime (main purpose of this option is to save memory on temporary files).
 *
 * \details
 * Create a config structure from a given configuration file.
 *
 * \return an ast_config data structure on success
 * \retval NULL on error
 */
struct ast_config *ast_config_load2(const char *filename, const char *who_asked, struct ast_flags flags);

/*!
 * \brief Load a config file
 *
 * \param filename path of file to open.  If no preceding '/' character,
 * path is considered relative to AST_CONFIG_DIR
 * \param flags Optional flags:
 * CONFIG_FLAG_WITHCOMMENTS - load the file with comments intact;
 * CONFIG_FLAG_FILEUNCHANGED - check the file mtime and return CONFIG_STATUS_FILEUNCHANGED if the mtime is the same; or
 * CONFIG_FLAG_NOCACHE - don't cache file mtime (main purpose of this option is to save memory on temporary files).
 *
 * \details
 * Create a config structure from a given configuration file.
 *
 * \return an ast_config data structure on success
 * \retval NULL on error
 */
#define ast_config_load(filename, flags)	ast_config_load2(filename, AST_MODULE, flags)

/*!
 * \brief Destroys a config
 *
 * \param cfg pointer to config data structure
 *
 * \details
 * Free memory associated with a given config
 */
void ast_config_destroy(struct ast_config *cfg);

/*!
 * \brief returns the root ast_variable of a config
 *
 * \param config pointer to an ast_config data structure
 * \param cat name of the category for which you want the root
 *
 * \return the category specified
 */
struct ast_variable *ast_category_root(struct ast_config *config, char *cat);

/*!
 * \brief Sorts categories in a config in the order of a numerical value contained within them.
 *
 * \param config The config structure you wish to sort
 * \param comparator variable Which numerical value you wish to sort by
 * \param descending If true, we sort highest to lowest instead of lowest to highest
 *
 * \details
 * This function will assume a value of 0 for any non-numerical strings and NULL fields.
 */
void ast_config_sort_categories(struct ast_config *config, int descending,
								int (*comparator)(struct ast_category *p, struct ast_category *q));

/*!
 * \brief Browse categories with filters
 *
 * \param config Which config structure you wish to "browse"
 * \param category_name An optional category name.
 * Pass NULL to not restrict by category name.
 * \param prev A pointer to the starting category structure.
 * Pass NULL to start at the beginning.
 * \param filter An optional comma-separated list of \<name_regex\>=\<value_regex\>
 * pairs.  Only categories with matching variables will be returned.
 * The special name 'TEMPLATES' can be used with the special values
 * 'include' or 'restrict' to include templates in the result or
 * restrict the result to only templates.
 *
 * \retval a category on success
 * \retval NULL on failure/no-more-categories
 */
struct ast_category *ast_category_browse_filtered(struct ast_config *config,
	const char *category_name, struct ast_category *prev, const char *filter);

/*!
 * \brief Browse categories
 *
 * \param config Which config structure you wish to "browse"
 * \param prev_name A pointer to a previous category name.
 *
 * \details
 * This function is kind of non-intuitive in it's use.
 * To begin, one passes NULL as the second argument.
 * It will return a pointer to the string of the first category in the file.
 * From here on after, one must then pass the previous usage's return value
 * as the second pointer, and it will return a pointer to the category name
 * afterwards.
 *
 * \retval a category name on success
 * \retval NULL on failure/no-more-categories
 *
 * \note ast_category_browse maintains internal state.  Therefore is not thread
 * safe, cannot be called recursively, and it is not safe to add or remove
 * categories while browsing.
 * ast_category_browse_filtered does not have these restrictions.
 */
char *ast_category_browse(struct ast_config *config, const char *prev_name);

/*!
 * \brief Browse variables
 * \param config Which config structure you wish to "browse"
 * \param category_name Which category to "browse"
 * \param filter an optional comma-separated list of \<name_regex\>=\<value_regex\>
 * pairs.  Only categories with matching variables will be browsed.
 * The special name 'TEMPLATES' can be used with the special values
 * 'include' or 'restrict' to include templates in the result or
 * restrict the result to only templates.
 *
 * \details
 * Somewhat similar in intent as the ast_category_browse.
 * List variables of config file category
 *
 * \retval ast_variable list on success
 * \retval NULL on failure
 */
struct ast_variable *ast_variable_browse_filtered(const struct ast_config *config,
	const char *category_name, const char *filter);
struct ast_variable *ast_variable_browse(const struct ast_config *config,
	const char *category_name);

/*!
 * \brief given a pointer to a category, return the root variable.
 *
 * \details
 * This is equivalent to ast_variable_browse(), but more efficient if we
 * already have the struct ast_category * (e.g. from ast_category_get())
 */
struct ast_variable *ast_category_first(struct ast_category *cat);

/*!
 * \brief Gets a variable by context and variable names
 *
 * \param config which (opened) config to use
 * \param category category under which the variable lies
 * \param variable which variable you wish to get the data for
 * \param filter an optional comma-separated list of \<name_regex\>=\<value_regex\>
 * pairs.  Only categories with matching variables will be searched.
 * The special name 'TEMPLATES' can be used with the special values
 * 'include' or 'restrict' to include templates in the result or
 * restrict the result to only templates.
 *
 * \retval The variable value on success
 * \retval NULL if unable to find it.
 */
const char *ast_variable_retrieve_filtered(struct ast_config *config,
	const char *category, const char *variable, const char *filter);
const char *ast_variable_retrieve(struct ast_config *config,
	const char *category, const char *variable);

/*!
 * \brief Gets a variable value from a specific category structure by name
 *
 * \param category category structure under which the variable lies
 * \param variable which variable you wish to get the data for
 *
 * \details
 * Goes through a given category and searches for the given variable
 *
 * \retval The variable value on success
 * \retval NULL if unable to find it.
 */
const char *ast_variable_find(const struct ast_category *category, const char *variable);

/*!
 * \brief Gets the value of a variable from a variable list by name
 *
 * \param list variable list to search
 * \param variable which variable you wish to get the data for
 *
 * \details
 * Goes through a given variable list and searches for the given variable
 *
 * \retval The variable value on success
 * \retval NULL if unable to find it.
 */
const char *ast_variable_find_in_list(const struct ast_variable *list, const char *variable);

/*!
 * \brief Gets the value of the LAST occurrence of a variable from a variable list
 *
 * \param list The ast_variable list to search
 * \param variable The name of the ast_variable you wish to fetch data for
 *
 * \details
 * Iterates over a given ast_variable list to search for the last occurrence of an
 * ast_variable entry with a name attribute matching the given name (variable).
 * This is useful if the list has duplicate entries (such as in cases where entries
 * are created by a template)
 *
 * \retval The variable value on success
 * \retval NULL if unable to find it.
 */
const char *ast_variable_find_last_in_list(const struct ast_variable *list, const char *variable);

/*!
 * \brief Gets a variable from a variable list by name
 * \since 13.9.0
 *
 * \param list variable list to search
 * \param variable_name name you wish to get the data for
 *
 * \details
 * Goes through a given variable list and searches for the given variable
 *
 * \retval The variable (not the value) on success
 * \retval NULL if unable to find it.
 */
const struct ast_variable *ast_variable_find_variable_in_list(const struct ast_variable *list, const char *variable_name);

/*!
 * \brief Retrieve a category if it exists
 *
 * \param config which config to use
 * \param category_name name of the category you're looking for
 * \param filter If a config contains more than 1 category with the same name,
 * you can specify a filter to narrow the search.  The filter is a comma-separated
 * list of \<name_regex\>=\<value_regex\> pairs.  Only a category with matching
 * variables will be returned. The special name 'TEMPLATES' can be used with the
 * special values 'include' or 'restrict' to include templates in the result or
 * restrict the result to only templates.
 *
 * \details
 * This will search through the categories within a given config file for a match.
 *
 * \retval pointer to category if found
 * \retval NULL if not.
 */
struct ast_category *ast_category_get(const struct ast_config *config,
	const char *category_name, const char *filter);

/*!
 * \brief Return the name of the category
 *
 * \param category category structure
 *
 * \retval pointer to category name if found
 * \retval NULL if not.
 */
const char *ast_category_get_name(const struct ast_category *category);

/*!
 * \brief Check if category is a template
 *
 * \param category category structure
 *
 * \retval 1 if a template.
 * \retval 0 if not.
 */
int ast_category_is_template(const struct ast_category *category);

/*!
 * \brief Return the template names this category inherits from
 *
 * \param category category structure
 *
 * \return an ast_str (which must be freed after use) with a comma
 * separated list of templates names or NULL if there were no templates.
 */
struct ast_str *ast_category_get_templates(const struct ast_category *category);

/*!
 * \brief Check for category duplicates
 *
 * \param config which config to use
 * \param category_name name of the category you're looking for
 * \param filter an optional comma-separated list of \<name_regex\>=\<value_regex\>
 * pairs.  Only categories with matching variables will be returned.
 * The special name 'TEMPLATES' can be used with the special values
 * 'include' or 'restrict' to include templates in the result or
 * restrict the result to only templates.
 *
 * \details
 * This will search through the categories within a given config file for a match.
 *
 * \return non-zero if found
 */
int ast_category_exist(const struct ast_config *config, const char *category_name,
	const char *filter);

/*!
 * \brief Retrieve realtime configuration
 *
 * \param family which family/config to lookup
 * \param fields which fields to lookup
 *
 * \details
 * This will use builtin configuration backends to look up a particular
 * entity in realtime and return a variable list of its parameters.
 *
 * \note
 * Unlike the variables in ast_config, the resulting list of variables
 * MUST be freed with ast_variables_destroy() as there is no container.
 *
 * \note
 * The difference between these two calls is that ast_load_realtime excludes
 * fields whose values are NULL, while ast_load_realtime_all loads all columns.
 *
 * \note
 * You should use the constant SENTINEL to terminate arguments, in
 * order to preserve cross-platform compatibility.
 */
struct ast_variable *ast_load_realtime_fields(const char *family, const struct ast_variable *fields);
struct ast_variable *ast_load_realtime(const char *family, ...) attribute_sentinel;
struct ast_variable *ast_load_realtime_all_fields(const char *family, const struct ast_variable *fields);
struct ast_variable *ast_load_realtime_all(const char *family, ...) attribute_sentinel;

/*!
 * \brief Release any resources cached for a realtime family
 * \since 1.6.1
 *
 * \param family which family/config to destroy
 *
 * \details
 * Various backends may cache attributes about a realtime data storage
 * facility; on reload, a front end resource may request to purge that cache.
 *
 * \retval 0 If any cache was purged
 * \retval -1 If no cache was found
 */
int ast_unload_realtime(const char *family);

/*!
 * \brief Inform realtime what fields that may be stored
 * \since 1.6.1
 *
 * \param family which family/config is referenced
 *
 * \details
 * This will inform builtin configuration backends that particular fields
 * may be updated during the use of that configuration section.  This is
 * mainly to be used during startup routines, to ensure that various fields
 * exist in the backend.  The backends may take various actions, such as
 * creating new fields in the data store or warning the administrator that
 * new fields may need to be created, in order to ensure proper function.
 *
 * The arguments are specified in groups of 3:  column name, column type,
 * and column size.  The column types are specified as integer constants,
 * defined by the enum require_type.  Note that the size is specified as
 * the number of equivalent character fields that a field may take up, even
 * if a field is otherwise specified as an integer type.  This is due to
 * the fact that some fields have historically been specified as character
 * types, even if they contained integer values.
 *
 * A family should always specify its fields to the minimum necessary
 * requirements to fulfill all possible values (within reason; for example,
 * a timeout value may reasonably be specified as an INTEGER2, with size 5.
 * Even though values above 32767 seconds are possible, they are unlikely
 * to be useful, and we should not complain about that size).
 *
 * \retval 0 Required fields met specified standards
 * \retval -1 One or more fields was missing or insufficient
 *
 * \note You should use the constant SENTINEL to terminate arguments, in
 * order to preserve cross-platform compatibility.
 *
 * TODO The return value of this function is routinely ignored. Ignoring
 * the return value means that it's mostly pointless to be calling this.
 * You'll see some warning messages potentially, but that's it.
 *
 * XXX This function is super useful for detecting configuration problems
 * early, but unfortunately, the latest in configuration management, sorcery,
 * doesn't work well with this. Users of sorcery are familiar with the fields
 * they will need to write but don't know if realtime is being used. Sorcery
 * knows what storage mechanism is being used but has no high-level knowledge
 * of what sort of data is going to be written.
 */
int ast_realtime_require_field(const char *family, ...) attribute_sentinel;

/*!
 * \brief Retrieve realtime configuration
 *
 * \param family which family/config to lookup
 * \param fields list of fields
 *
 * \details
 * This will use builtin configuration backends to look up a particular
 * entity in realtime and return a variable list of its parameters. Unlike
 * the ast_load_realtime, this function can return more than one entry and
 * is thus stored inside a traditional ast_config structure rather than
 * just returning a linked list of variables.
 *
 * \return An ast_config with one or more results
 * \retval NULL Error or no results returned
 */
struct ast_config *ast_load_realtime_multientry_fields(const char *family, const struct ast_variable *fields);

/*!
 * \brief Retrieve realtime configuration
 *
 * \param family which family/config to lookup
 *
 * \details
 * This will use builtin configuration backends to look up a particular
 * entity in realtime and return a variable list of its parameters. Unlike
 * the ast_load_realtime, this function can return more than one entry and
 * is thus stored inside a traditional ast_config structure rather than
 * just returning a linked list of variables.
 *
 * \return An ast_config with one or more results
 * \retval NULL Error or no results returned
 *
 * \note You should use the constant SENTINEL to terminate arguments, in
 * order to preserve cross-platform compatibility.
 */
struct ast_config *ast_load_realtime_multientry(const char *family, ...) attribute_sentinel;

/*!
 * \brief Update realtime configuration
 *
 * \param family which family/config to be updated
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * \param fields fields to update
 *
 * \details
 * This function is used to update a parameter in realtime configuration space.
 *
 * \return Number of rows affected, or -1 on error.
 */
int ast_update_realtime_fields(const char *family, const char *keyfield, const char *lookup, const struct ast_variable *fields);

/*!
 * \brief Update realtime configuration
 *
 * \param family which family/config to be updated
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 *
 * \details
 * This function is used to update a parameter in realtime configuration space.
 *
 * \return Number of rows affected, or -1 on error.
 *
 * \note You should use the constant SENTINEL to terminate arguments, in
 * order to preserve cross-platform compatibility.
 */
int ast_update_realtime(const char *family, const char *keyfield, const char *lookup, ...) attribute_sentinel;

/*!
 * \brief Update realtime configuration
 *
 * \param family which family/config to be updated
 * \param lookup_fields fields used to look up entries
 * \param update_fields fields to update
 *
 * \details
 * This function is used to update a parameter in realtime configuration space.
 * It includes the ability to lookup a row based upon multiple key criteria.
 * As a result, this function includes two sentinel values, one to terminate
 * lookup values and the other to terminate the listing of fields to update.
 *
 * \return Number of rows affected, or -1 on error.
 */
int ast_update2_realtime_fields(const char *family, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields);

/*!
 * \brief Update realtime configuration
 *
 * \param family which family/config to be updated
 *
 * \details
 * This function is used to update a parameter in realtime configuration space.
 * It includes the ability to lookup a row based upon multiple key criteria.
 * As a result, this function includes two sentinel values, one to terminate
 * lookup values and the other to terminate the listing of fields to update.
 *
 * \return Number of rows affected, or -1 on error.
 *
 * \note You should use the constant SENTINEL to terminate arguments, in
 * order to preserve cross-platform compatibility.
 */
int ast_update2_realtime(const char *family, ...) attribute_sentinel;

/*!
 * \brief Create realtime configuration
 *
 * \param family which family/config to be created
 * \param fields fields themselves
 *
 * \details
 * This function is used to create a parameter in realtime configuration space.
 *
 * \return Number of rows affected, or -1 on error.
 *
 * \note
 * On the MySQL engine only, for reasons of backwards compatibility, the return
 * value is the insert ID.  This value is nonportable and may be changed in a
 * future version to match the other engines.
 */
int ast_store_realtime_fields(const char *family, const struct ast_variable *fields);

/*!
 * \brief Create realtime configuration
 *
 * \param family which family/config to be created
 *
 * \details
 * This function is used to create a parameter in realtime configuration space.
 *
 * \return Number of rows affected, or -1 on error.
 *
 * \note
 * On the MySQL engine only, for reasons of backwards compatibility, the return
 * value is the insert ID.  This value is nonportable and may be changed in a
 * future version to match the other engines.
 *
 * \note You should use the constant SENTINEL to terminate arguments, in
 * order to preserve cross-platform compatibility.
 */
int ast_store_realtime(const char *family, ...) attribute_sentinel;

/*!
 * \brief Destroy realtime configuration
 *
 * \param family which family/config to be destroyed
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 * \param fields fields themselves
 *
 * \details
 * This function is used to destroy an entry in realtime configuration space.
 * Additional params are used as keys.
 *
 * \return Number of rows affected, or -1 on error.
 */
int ast_destroy_realtime_fields(const char *family, const char *keyfield, const char *lookup, const struct ast_variable *fields);

/*!
 * \brief Destroy realtime configuration
 *
 * \param family which family/config to be destroyed
 * \param keyfield which field to use as the key
 * \param lookup which value to look for in the key field to match the entry.
 *
 * \details
 * This function is used to destroy an entry in realtime configuration space.
 * Additional params are used as keys.
 *
 * \return Number of rows affected, or -1 on error.
 *
 * \note You should use the constant SENTINEL to terminate arguments, in
 * order to preserve cross-platform compatibility.
 */
int ast_destroy_realtime(const char *family, const char *keyfield, const char *lookup, ...) attribute_sentinel;

/*!
 * \brief Check if realtime engine is configured for family
 * \param family which family/config to be checked
 * \return 1 if family is configured in realtime and engine exists
 */
int ast_check_realtime(const char *family);

/*! \brief Check if there's any realtime engines loaded */
int ast_realtime_enabled(void);

/*!
 * \brief Duplicate variable list
 * \param var the linked list of variables to clone
 * \return A duplicated list which you'll need to free with
 * ast_variables_destroy or NULL when out of memory.
 *
 * \note Do not depend on this to copy more than just name, value and filename
 * (the arguments to ast_variables_new).
 */
struct ast_variable *ast_variables_dup(struct ast_variable *var);

/*!
 * \brief Reverse a variable list
 * \param var the linked list of variables to reverse
 * \return The head of the reversed variable list
 *
 * \note The variable list var is not preserved in this function and should
 * not be used after reversing it.
 */
struct ast_variable *ast_variables_reverse(struct ast_variable *var);

/*!
 * \brief Free variable list
 * \param var the linked list of variables to free
 *
 * \details
 * This function frees a list of variables.
 */
void ast_variables_destroy(struct ast_variable *var);

/*!
 * \brief Register config engine
 * \retval 1 Always
 */
int ast_config_engine_register(struct ast_config_engine *newconfig);

/*!
 * \brief Deregister config engine
 * \retval 0 Always
 */
int ast_config_engine_deregister(struct ast_config_engine *del);

/*!
 * \brief Determine if a mapping exists for a given family
 *
 * \param family which family you are looking to see if a mapping exists for
 * \retval 1 if it is mapped
 * \retval 0 if it is not
 */
int ast_realtime_is_mapping_defined(const char *family);

#ifdef TEST_FRAMEWORK
/*!
 * \brief Add an explicit mapping for a family
 *
 * \param name Family name
 * \param driver Driver to use
 * \param database Database to access
 * \param table Table to use
 * \param priority Priority of this mapping
 */
int ast_realtime_append_mapping(const char *name, const char *driver, const char *database, const char *table, int priority);
#endif

/*!
 * \brief Exposed initialization method for core process
 *
 * \details
 * This method is intended for use only with the core initialization and is
 * not designed to be called from any user applications.
 */
int register_config_cli(void);

/*! \brief Create a new base configuration structure */
struct ast_config *ast_config_new(void);

/*!
 * \brief Retrieve the current category name being built.
 *
 * \details
 * API for backend configuration engines while building a configuration set.
 */
struct ast_category *ast_config_get_current_category(const struct ast_config *cfg);

/*!
 * \brief Set the category within the configuration as being current.
 *
 * \details
 * API for backend configuration engines while building a configuration set.
 */
void ast_config_set_current_category(struct ast_config *cfg, const struct ast_category *cat);

/*!
 * \brief Retrieve a configuration variable within the configuration set.
 *
 * \details
 * Retrieves the named variable \p var within category \p cat of configuration
 * set \p cfg.  If not found, attempts to retrieve the named variable \p var
 * from within category \em general.
 *
 * \return Value of \p var, or NULL if not found.
 */
const char *ast_config_option(struct ast_config *cfg, const char *cat, const char *var);

/*!
 * \brief Create a category
 *
 * \param name name of new category
 * \param in_file filename which contained the new config
 * \param lineno line number
 */
struct ast_category *ast_category_new(const char *name, const char *in_file, int lineno);

/*!
 * \brief Create a category that is not backed by a file
 *
 * \param name name of new category
 */
#define ast_category_new_dynamic(name) ast_category_new(name, "", -1)

/*!
 * \brief Create a nameless category that is not backed by a file
 */
#define ast_category_new_anonymous() ast_category_new_dynamic("")

/*!
 * \brief Create a category making it a template
 *
 * \param name name of new template
 * \param in_file filename which contained the new config
 * \param lineno line number
 */
struct ast_category *ast_category_new_template(const char *name, const char *in_file, int lineno);

/*!
 * \brief Inserts new category
 *
 * \param config which config to use
 * \param cat newly created category to insert
 * \param match which category to insert above
 *
 * \details
 * This function is used to insert a new category above another category
 * matching the match parameter.
 *
 * \retval 0 if succeeded
 * \retval -1 if the specified match category wasn't found
 */
int ast_category_insert(struct ast_config *config, struct ast_category *cat, const char *match);

/*!
 * \brief Delete a category
 *
 * \param cfg which config to use
 * \param cat category to delete
 *
 * \return the category after the deleted one which could be NULL.
 *
 * \note It is not safe to call ast_category_delete while browsing with
 * ast_category_browse.  It is safe with ast_category_browse_filtered.
 */
struct ast_category *ast_category_delete(struct ast_config *cfg, struct ast_category *cat);

/*!
 * \brief Appends a category to a config
 *
 * \param config which config to use
 * \param category category to insert
 */
void ast_category_append(struct ast_config *config, struct ast_category *category);

/*!
 * \brief Applies base (template) to category.
 *
 * \param existing existing category
 * \param base base category
 *
 * \details
 * This function is used to apply a base (template) to an existing category
 *
 * \retval 0 if succeeded
 * \retval -1 if the memory allocation failed
 */
int ast_category_inherit(struct ast_category *existing, const struct ast_category *base);

/*!
 * \brief Removes and destroys all variables in a category
 *
 * \param category category to empty
 *
 * \retval 0 if succeeded
 * \retval -1 if category is NULL
 */
int ast_category_empty(struct ast_category *category);

void ast_category_destroy(struct ast_category *cat);
struct ast_variable *ast_category_detach_variables(struct ast_category *cat);
void ast_category_rename(struct ast_category *cat, const char *name);

struct ast_variable *_ast_variable_new(const char *name, const char *value, const char *filename, const char *file, const char *function, int lineno);
#define ast_variable_new(name, value, filename) _ast_variable_new(name, value, filename, __FILE__, __PRETTY_FUNCTION__, __LINE__)

struct ast_config_include *ast_include_new(struct ast_config *conf, const char *from_file, const char *included_file, int is_exec, const char *exec_file, int from_lineno, char *real_included_file_name, int real_included_file_name_size);
struct ast_config_include *ast_include_find(struct ast_config *conf, const char *included_file);
void ast_include_rename(struct ast_config *conf, const char *from_file, const char *to_file);
void ast_variable_append(struct ast_category *category, struct ast_variable *variable);
void ast_variable_insert(struct ast_category *category, struct ast_variable *variable, const char *line);
int ast_variable_delete(struct ast_category *category, const char *variable, const char *match, const char *line);

/*!
 * \brief Performs an in-place sort on the variable list by ascending name
 *
 * \param head The variable list head
 *
 * \return The new list head
 */
struct ast_variable *ast_variable_list_sort(struct ast_variable *head);

/*!
 * \brief Appends a variable list to the end of another list
 *
 * \param head A pointer to an ast_variable * of the existing variable list head. May NOT be NULL
 * but the content may be to initialize a new list.  If so, upon return, this parameter will be updated
 * with a pointer to the new list head.
 * \param search_hint The place in the current list to start searching for the end of the list.
 * Might help performance on longer lists.  If NULL, it defaults to head.
 * \param new_var The head of the new variable list to be appended
 *
 * \return The tail of the resulting list.
 *
 * \note If the existing *head is NULL, it will be updated to new_var.  This allows you to call
 * ast_variable_list_append in a loop or callback without initializing the list first.
 */
struct ast_variable *ast_variable_list_append_hint(struct ast_variable **head, struct ast_variable *search_hint,
	struct ast_variable *new_var);
#define ast_variable_list_append(head, new_var) ast_variable_list_append_hint(head, NULL, new_var)

/*!
 * \brief Replace a variable in the given list with a new value
 * \since 13.30.0
 *
 * \param head A pointer to an ast_variable * of the existing variable list head. May NOT be NULL
 * but the content may be to initialize a new list.  If so, upon return, this parameter will be updated
 * with a pointer to the new list head.
 * \param replacement The variable that replaces another variable in the list with the
 * same name.
 *
 * \retval 0 if a variable was replaced in the list
 * \retval -1 if no replacement occured
 *
 * \note The variable name comparison is performed case-sensitively
 * \note If a variable is replaced, its memory is freed.
 */
int ast_variable_list_replace(struct ast_variable **head, struct ast_variable *replacement);

/*!
 * \brief Replace a variable in the given list with a new variable
 *
 * \param head   A pointer to the current variable list head.  Since the variable to be
 *               replaced, this pointer may be updated with the new head.
 * \param oldvar A pointer to the existing variable to be replaced.
 * \param newvar A pointer to the new variable that will replace the old one.
 *
 * \retval 0 if a variable was replaced in the list
 * \retval -1 if no replacement occured
 *
 * \note The search for the old variable is done simply on the pointer.
 * \note If a variable is replaced, its memory is freed.
 */
int ast_variable_list_replace_variable(struct ast_variable **head,
	struct ast_variable *oldvar,
	struct ast_variable *newvar);

/*!
 * \brief Join an ast_variable list with specified separators and quoted values
 *
 * \param head                 A pointer to an ast_variable list head.
 * \param item_separator       The string to use to separate the list items.
 *                             If NULL, "," will be used.
 * \param name_value_separator The string to use to separate each item's name and value.
 *                             If NULL, "=" will be used.
 * \param str                  A pointer to a pre-allocated ast_str in which to put the results.
 *                             If NULL, one will be allocated and returned.
 * \param quote_char           The quote char to use for the values.
 *                             May be NULL or empty for no quoting.
 *
 * \retval A pointer to the result ast_str. This may NOT be the same as the pointer
 *         passed in if the original ast_str wasn't large enough to hold the result.
 *         Regardless, the pointer MUST be freed after use.
 * \retval NULL if there was an error.
 */
struct ast_str *ast_variable_list_join(const struct ast_variable *head, const char *item_separator,
	const char *name_value_separator, const char *quote_char, struct ast_str **str);

/*!
 * \brief Parse a string into an ast_variable list.  The reverse of ast_variable_list_join
 *
 * \param input                The name-value pair string to parse.
 * \param item_separator       The string used to separate the list items.
 *                             Only the first character in the string will be used.
 *                             If NULL, "," will be used.
 * \param name_value_separator The string used to separate each item's name and value.
 *                             Only the first character in the string will be used.
 *                             If NULL, "=" will be used.
 *
 * \retval A pointer to a list of ast_variables.
 * \retval NULL if there was an error or no variables could be parsed.
 */
struct ast_variable *ast_variable_list_from_string(const char *input, const char *item_separator,
	const char *name_value_separator);

/*!
 * \brief Update variable value within a config
 *
 * \param category Category element within the config
 * \param variable Name of the variable to change
 * \param value New value of the variable
 * \param match If set, previous value of the variable (if NULL or zero-length, no matching will be done)
 * \param object Boolean of whether to make the new variable an object
 *
 * \return 0 on success or -1 on failure.
 */
int ast_variable_update(struct ast_category *category, const char *variable,
						const char *value, const char *match, unsigned int object);

/*!
 * \brief Save a config text file
 * \since 13.2.0
 *
 * \param filename Filename
 * \param cfg ast_config
 * \param generator generator
 * \param flags List of config_save_flags
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_config_text_file_save2(const char *filename, const struct ast_config *cfg, const char *generator, uint32_t flags);

/*!
 * \brief Save a config text file preserving the pre 13.2 behavior
 *
 * \param filename Filename
 * \param cfg ast_config
 * \param generator generator
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_config_text_file_save(const char *filename, const struct ast_config *cfg, const char *generator);

struct ast_config *ast_config_internal_load(const char *configfile, struct ast_config *cfg, struct ast_flags flags, const char *suggested_incl_file, const char *who_asked);
/*!
 * \brief
 * Copies the contents of one ast_config into another
 *
 * \note
 * This creates a config on the heap. The caller of this must
 * be prepared to free the memory returned.
 *
 * \param orig the config to copy
 * \return The new config on success, NULL on failure.
 */
struct ast_config *ast_config_copy(const struct ast_config *orig);

/*!
 * \brief
 * Flags that affect the behaviour of config hooks.
 */
enum config_hook_flags {
	butt,
};

/*!
 * \brief Callback when configuration is updated
 *
 * \param cfg A copy of the configuration that is being changed.
 *            This MUST be freed by the callback before returning.
 */
typedef int (*config_hook_cb)(struct ast_config *cfg);

/*!
 * \brief
 * Register a config hook for a particular file and module
 *
 * \param name The name of the hook you are registering.
 * \param filename The file whose config you wish to hook into.
 * \param module The module that is reloading the config. This
 *               can be useful if multiple modules may possibly
 *               reload the same file, but you are only interested
 *               when a specific module reloads the file
 * \param flags Flags that affect the way hooks work.
 * \param hook The callback to be called when config is loaded.
 * return 0 Success
 * return -1 Unsuccess, also known as UTTER AND COMPLETE FAILURE
 */
int ast_config_hook_register(const char *name,
		const char *filename,
		const char *module,
		enum config_hook_flags flags,
		config_hook_cb hook);

/*!
 * \brief
 * Unregister a config hook
 *
 * \param name The name of the hook to unregister
 */
void ast_config_hook_unregister(const char *name);

/*!
 * \brief Support code to parse config file arguments
 *
 * \details
 * The function ast_parse_arg() provides a generic interface to parse
 * strings (e.g. numbers, network addresses and so on) in a flexible
 * way, e.g. by doing proper error and bound checks, provide default
 * values, and so on.
 * The function (described later) takes a string as an argument,
 * a set of flags to specify the result format and checks to perform,
 * a pointer to the result, and optionally some additional arguments.
 *
 * \return 0 on success, != 0 otherwise.
 */
enum ast_parse_flags {
	/* low 4 bits of flags are used for the operand type */
	PARSE_TYPE	=	0x000f,
	/* numeric types, with optional default value and bound checks.
	 * Additional arguments are passed by value.
	 */
	PARSE_INT32	= 	0x0001,
	PARSE_UINT32	= 	0x0002,
	PARSE_DOUBLE	= 	0x0003,
#if 0	/* not supported yet */
	PARSE_INT16	= 	0x0004,
	PARSE_UINT16	= 	0x0005,
#endif

	/* Returns an int processed by ast_app_parse_timelen.
	 * The first argument is an enum ast_timelen value (required).
	 */
	PARSE_TIMELEN	=	0x0006,

	/* Returns a struct ast_sockaddr, with optional default value
	 * (passed by reference) and port handling (accept, ignore,
	 * require, forbid). The format is 'ipaddress[:port]'. IPv6 address
	 * literals need square brackets around them if a port is specified.
	 */
	PARSE_ADDR	=	0x000e,

	/* Returns a struct sockaddr_in, with optional default value
	 * (passed by reference) and port handling (accept, ignore,
	 * require, forbid). The format is 'host.name[:port]'
	 */
	PARSE_INADDR	= 	0x000f,

	/* Other data types can be added as needed */

	/* If PARSE_DEFAULT is set, next argument is a default value
	 * which is returned in case of error. The argument is passed
	 * by value in case of numeric types, by reference in other cases.
 	 */
	PARSE_DEFAULT	=	0x0010,	/* assign default on error */

	/* Request a range check, applicable to numbers. Two additional
	 * arguments are passed by value, specifying the low-high end of
	 * the range (inclusive). An error is returned if the value
	 * is outside or inside the range, respectively.
	 */
	PARSE_IN_RANGE =       0x0020, /* accept values inside a range */
	PARSE_OUT_RANGE =      0x0040, /* accept values outside a range */
	PARSE_RANGE_DEFAULTS = 0x0080, /* default to range min/max on range error */

	/* Port handling, for ast_sockaddr. accept/ignore/require/forbid
	 * port number after the hostname or address.
	 */
	PARSE_PORT_MASK =	0x0300, /* 0x000: accept port if present */
	PARSE_PORT_IGNORE =	0x0100, /* 0x100: ignore port if present */
	PARSE_PORT_REQUIRE =	0x0200, /* 0x200: require port number */
	PARSE_PORT_FORBID =	0x0300, /* 0x100: forbid port number */
};

/*!
 * \brief The argument parsing routine.
 *
 * \param arg the string to parse. It is not modified.
 * \param flags combination of ast_parse_flags to specify the
 * 	return type and additional checks.
 * \param p_result pointer to the result. NULL is valid here, and can
 * 	be used to perform only the validity checks.
 * \param ... extra arguments are required according to flags.
 *
 * \retval 0 in case of success, != 0 otherwise.
 * \retval result returns the parsed value in case of success,
 * the default value in case of error, or it is left unchanged
 * in case of error and no default specified. Note that in certain
 * cases (e.g. sockaddr_in, with multi-field return values) some
 * of the fields in result may be changed even if an error occurs.
 *
 * \details
 * Examples of use:
 *     ast_parse_arg("223", PARSE_INT32|PARSE_IN_RANGE, &a, -1000, 1000);
 * returns 0, a = 223
 *     ast_parse_arg("22345", PARSE_INT32|PARSE_IN_RANGE|PARSE_DEFAULT, &a, 9999, 10, 100);
 * returns 1, a = 9999
 *     ast_parse_arg("22345ssf", PARSE_UINT32|PARSE_IN_RANGE, &b, 10, 100);
 * returns 1, b unchanged
 *    ast_parse_arg("12", PARSE_UINT32|PARSE_IN_RANGE|PARSE_RANGE_DEFAULTS, &a, 1, 10);
 * returns 1, a = 10
 *     ast_parse_arg("223", PARSE_TIMELEN|PARSE_IN_RANGE, &a, TIMELEN_SECONDS, -1000, 1000);
 * returns 0, a = 1000
 *     ast_parse_arg("223", PARSE_TIMELEN|PARSE_IN_RANGE, &a, TIMELEN_SECONDS, -1000, 250000);
 * returns 0, a = 223000
 *     ast_parse_arg("223", PARSE_TIMELEN|PARSE_IN_RANGE|PARSE_DEFAULT, &a, TIMELEN_SECONDS, 9999, -1000, 250000);
 * returns 0, a = 9999
 *    ast_parse_arg("www.foo.biz:44", PARSE_INADDR, &sa);
 * returns 0, sa contains address and port
 *    ast_parse_arg("www.foo.biz", PARSE_INADDR|PARSE_PORT_REQUIRE, &sa);
 * returns 1 because port is missing, sa contains address
 */
int ast_parse_arg(const char *arg, enum ast_parse_flags flags,
        void *p_result, ...);

/*
 * Parsing config file options in C is slightly annoying because we cannot use
 * string in a switch() statement, yet we need a similar behaviour, with many
 * branches and a break on a matching one.
 * The following somehow simplifies the job: we create a block using
 * the CV_START and CV_END macros, and then within the block we can run
 * actions such as "if (condition) { body; break; }"
 * Additional macros are present to run simple functions (e.g. ast_copy_string)
 * or to pass arguments to ast_parse_arg()
 *
 * As an example:

	CV_START(v->name, v->value);	// start the block
	CV_STR("foo", x_foo);		// static string
	CV_DSTR("bar", y_bar);		// malloc'ed string
	CV_F("bar", ...);		// call a generic function
	CV_END;				// end the block
 */

/*! \brief the macro to open a block for variable parsing */
#define CV_START(__in_var, __in_val) 		\
	do {					\
		const char *__var = __in_var;	\
		const char *__val = __in_val;

/*! \brief close a variable parsing block */
#define	CV_END			} while (0)

/*! \brief call a generic function if the name matches. */
#define	CV_F(__pattern, __body)	if (!strcasecmp((__var), __pattern)) { __body; break; }

/*!
 * \brief helper macros to assign the value to a BOOL, UINT, static string and
 * dynamic string
 */
#define	CV_BOOL(__x, __dst)	CV_F(__x, (__dst) = ast_true(__val) )
#define CV_UINT(__x, __dst)	CV_F(__x, (__dst) = strtoul(__val, NULL, 0) )
#define CV_STR(__x, __dst)	CV_F(__x, ast_copy_string(__dst, __val, sizeof(__dst)))
#define CV_DSTR(__x, __dst)	CV_F(__x, ast_free(__dst); __dst = ast_strdup(__val))
#define CV_STRFIELD(__x, __obj, __field) CV_F(__x, ast_string_field_set(__obj, __field, __val))

/*! \brief Check if require type is an integer type */
AST_INLINE_API(
int ast_rq_is_int(require_type type),
{
	switch (type) {
	case RQ_INTEGER1:
	case RQ_UINTEGER1:
	case RQ_INTEGER2:
	case RQ_UINTEGER2:
	case RQ_INTEGER3:
	case RQ_UINTEGER3:
	case RQ_INTEGER4:
	case RQ_UINTEGER4:
	case RQ_INTEGER8:
	case RQ_UINTEGER8:
		return 1;
	default:
		return 0;
	}
}
)

/*!
 * \brief Remove standard encoding from realtime values, which ensures
 * that a semicolon embedded within a single value is not treated upon
 * retrieval as multiple values.
 * \param chunk Data to be decoded
 * \return The decoded data, in the original buffer
 * \since 1.8
 * \warning This function modifies the original buffer
 */
char *ast_realtime_decode_chunk(char *chunk);

/*!
 * \brief Encodes a chunk of data for realtime
 * \param dest Destination buffer
 * \param maxlen Length passed through to ast_str_* functions
 * \param chunk Source data to be encoded
 * \return Buffer within dest
 * \since 1.8
 */
char *ast_realtime_encode_chunk(struct ast_str **dest, ssize_t maxlen, const char *chunk);

/*!
 * \brief Tests 2 variable values to see if they match
 * \since 13.9.0
 *
 * \param left Variable to test
 * \param right Variable to match against with an optional realtime-style operator in the name
 *
 * \retval 1 matches
 * \retval 0 doesn't match
 *
 * \details
 *
 * The values of the variables are passed to ast_strings_match.
 * If right->name is suffixed with a space and an operator, that operator
 * is also passed to ast_strings_match.
 *
 * Examples:
 *
 * left->name = "id" (ignored)
 * left->value = "abc"
 * right->name = "id regex" (id is ignored)
 * right->value = "a[bdef]c"
 *
 * will result in ast_strings_match("abc", "regex", "a[bdef]c") which will return 1.
 *
 * left->name = "id" (ignored)
 * left->value = "abc"
 * right->name = "id" (ignored)
 * right->value = "abc"
 *
 * will result in ast_strings_match("abc", NULL, "abc") which will return 1.
 *
 * See the documentation for ast_strings_match for the valid operators.
 */
int ast_variables_match(const struct ast_variable *left, const struct ast_variable *right);

/*!
 * \brief Tests 2 variable lists to see if they match
 * \since 13.9.0
 *
 * \param left Variable list to test
 * \param right Variable list with an optional realtime-style operator in the names
 * \param exact_match If true, all variables in left must match all variables in right
 *        and vice versa.  This does exact value matches only.  Operators aren't supported.
 *        Except for order, the left and right lists must be equal.
 *
 *        If false, every variable in the right list must match some variable in the left list
 *        using the operators supplied. Variables in the left list that aren't in the right
 *        list are ignored for matching purposes.
 *
 * \retval 1 matches
 * \retval 0 doesn't match
 *
 * \details
 * Iterates over the variable lists calling ast_variables_match.  If any match fails
 * or a variable in the right list isn't in the left list, 0 is returned.
 */
int ast_variable_lists_match(const struct ast_variable *left, const struct ast_variable *right,
	int exact_match);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CONFIG_H */
