/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
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
 * \brief Configuration option-handling
 * \author Terry Wilson <twilson@digium.com>
 */

#ifndef _ASTERISK_CONFIG_OPTIONS_H
#define _ASTERISK_CONFIG_OPTIONS_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <regex.h>

#include "asterisk/config.h"
#include "asterisk/astobj2.h"

struct aco_option;
struct aco_info_internal;
struct aco_type_internal;

enum aco_type_t {
	ACO_GLOBAL,
	ACO_ITEM,
	ACO_IGNORE,
};

/*! Type of category matching to perform */
enum aco_category_op {
	/*! Regex based blacklist. */
	ACO_BLACKLIST = 0,
	/*! Regex based whitelist. */
	ACO_WHITELIST,
	/*! Blacklist with a single string matched with strcasecmp. */
	ACO_BLACKLIST_EXACT,
	/*! Whitelist with a single string matched with strcasecmp. */
	ACO_WHITELIST_EXACT,
	/*! Blacklist with a NULL terminated array of strings matched with strcasecmp. */
	ACO_BLACKLIST_ARRAY,
	/*! Whitelist with a NULL terminated array of strings matched with strcasecmp. */
	ACO_WHITELIST_ARRAY,
};

/*! \brief What kind of matching should be done on an option name */
enum aco_matchtype {
	ACO_EXACT = 1,
	ACO_REGEX,
	ACO_PREFIX,
};

/*! Callback functions for option parsing via aco_process_config() */

/*! \brief Allocate a configurable ao2 object
 * \param category The config category the object is being generated for
 * \retval NULL error
 * \retval non-NULL a new configurable ao2 object
 */
typedef void *(*aco_type_item_alloc)(const char *category);

/*! \brief Find a item given a category and container of items
 * \param newcontainer The container to search for the item
 * \param category The category associated with the item
 * \retval non-NULL item from the container
 * \retval NULL item does not exist in container
 */
typedef void *(*aco_type_item_find)(struct ao2_container *newcontainer, const char *category);

/*! \brief Callback function that is called after a config object is initialized with defaults
 *
 * \note This callback is called during config processing after a new config is allocated and
 * and defaults applied but before values from the config are read. This callback could be used
 * to merge in settings inherited from the global settings if necessary, despite that being a
 * bad thing to do!
 *
 * \param newitem The newly allocated config object with defaults populated
 * \retval 0 succes, continue processing
 * \retval non-zero failure, stop processing
 */
typedef int (*aco_type_item_pre_process)(void *newitem);

/*! \brief Callback function that is called after config processing, but before linking
 *
 * \note This callback is called after config processing, but before linking the object
 * in the config container. This callback can be used to verify that all settings make
 * sense together, that required options have been set, etc.
 *
 * \param newitem The newly configured object
 * \retval 0 success, continue processing
 * \retval non-zero failure, stop processing
 */
typedef int (*aco_type_prelink)(void *newitem);

/*! \brief A function for determining whether the value for the matchfield in an aco_type is sufficient for a match
 * \param text The value of the option
 * \retval -1 The value is sufficient for a match
 * \retval 0 The value is not sufficient for a match
 */
typedef int (*aco_matchvalue_func)(const char *text);

/*! \struct aco_type
 * \brief Type information about a category-level configurable object
 */
struct aco_type {
	/* common stuff */
	enum aco_type_t type;   /*!< Whether this is a global or item type */
	const char *name;       /*!< The name of this type (must match XML documentation) */
	const char *category;   /*!< A regular expression for matching categories to be allowed or denied */
	const char *matchfield; /*!< An option name to match for this type (i.e. a 'type'-like column) */
	const char *matchvalue; /*!< The value of the option to require for matching (i.e. 'peer' for type= in sip.conf) */
	aco_matchvalue_func matchfunc;       /*!< A function for determining whether the option value matches (i.e. hassip= requires ast_true()) */
	enum aco_category_op category_match; /*!< Whether the following category regex is a whitelist or blacklist */
	size_t item_offset;                  /*!< The offset in the config snapshot for the global config or item config container */
	unsigned int hidden;  /*!< Type is for internal purposes only and it and all options should not be visible to users */

	/* non-global callbacks */
	aco_type_item_alloc item_alloc;         /*!< An allocation function for item associated with this type */
	aco_type_item_find item_find;           /*!< A callback function to find an existing item in a particular container */
	aco_type_item_pre_process item_pre_process; /*!< An optional callback function that is called after defaults are applied, but before config processing */
	aco_type_prelink item_prelink;          /*!< An optional callback function that is called after config processing, but before applying changes */
	struct aco_type_internal *internal;
};

/*! \brief A callback function to run just prior to applying config changes
 * \retval 0 Success
 * \retval non-zero Failure. Changes not applied
 */
typedef int (*aco_pre_apply_config)(void);

/*! \brief A callback function called only if config changes have been applied
 *
 * \note If a config file has not been edited prior to performing a reload, this
 * callback will not be called.
 */
typedef void (*aco_post_apply_config)(void);

/*! \brief A callback function for allocating an object to hold all config objects
 * \retval NULL error
 * \retval non-NULL a config object container
 */
typedef void *(*aco_snapshot_alloc)(void);

/*! \brief The representation of a single configuration file to be processed */
struct aco_file {
	const char *filename;       /*!< The filename to be processed */
	const char *alias;          /*!< An alias filename to be tried if 'filename' cannot be found */
	const char **preload;       /*!< A null-terminated ordered array of categories to be loaded first */
	const char *skip_category;  /*!< A regular expression of categories to skip in the file. Use when a file is processed by multiple modules */
	struct aco_type *types[];   /*!< The list of types for this config. Required. Use a sentinel! */
};

struct aco_info {
	const char *module; /*!< The name of the module whose config is being processed */
	unsigned int hidden:1;                /*!< If enabled, this config item is hidden from users */
	aco_pre_apply_config pre_apply_config; /*!< A callback called after processing, but before changes are applied */
	aco_post_apply_config post_apply_config;/*!< A callback called after changes are applied */
	aco_snapshot_alloc snapshot_alloc;     /*!< Allocate an object to hold all global configs and item containers */
	struct ao2_global_obj *global_obj;     /*!< The global object array that holds the user-defined config object */
	struct aco_info_internal *internal;
	struct aco_file *files[];    /*!< An array of aco_files to process */
};

/*! \brief A helper macro to ensure that aco_info types always have a sentinel */
#define ACO_TYPES(...) { __VA_ARGS__, NULL, }
#define ACO_FILES(...) { __VA_ARGS__, NULL, }

/*! \brief Get pending config changes
 * \note This will most likely be called from the pre_apply_config callback function
 * \param info An initialized aco_info
 * \retval NULL error
 * \retval non-NULL A pointer to the user-defined config object with un-applied changes
 */
void *aco_pending_config(struct aco_info *info);

/*! \def CONFIG_INFO_STANDARD
 * \brief Declare an aco_info struct with default module and preload values
 * \param name The name of the struct
 * \param arr The global object array for holding the user-defined config object
 * \param alloc The allocater for the user-defined config object
 *
 * Example:
 * \code
 * static AO2_GLOBAL_OBJ_STATIC(globals, 1);
 * CONFIG_INFO_STANDARD(cfg_info, globals, skel_config_alloc,
 *     .pre_apply_config = skel_pre_apply_config,
 *     .files = { &app_skel_conf, NULL },
 * );
 * ...
 * if (aco_info_init(&cfg_info)) {
 *     return AST_MODULE_LOAD_DECLINE;
 * }
 * ...
 * aco_info_destroy(&cfg_info);
 * \endcode
 */
#define CONFIG_INFO_STANDARD(name, arr, alloc, ...) \
static struct aco_info name = { \
	.module = AST_MODULE, \
	.global_obj = &arr, \
	.snapshot_alloc = alloc, \
	__VA_ARGS__ \
};

#define CONFIG_INFO_CORE(mod, name, arr, alloc, ...) \
static struct aco_info name = { \
	.module = mod, \
	.global_obj = &arr, \
	.snapshot_alloc = alloc, \
	__VA_ARGS__ \
};

#define CONFIG_INFO_TEST(name, arr, alloc, ...) \
static struct aco_info name = { \
	.module = AST_MODULE, \
	.global_obj = &arr, \
	.snapshot_alloc = alloc, \
	.hidden = 1, \
	__VA_ARGS__ \
};

/*! \brief Initialize an aco_info structure
 * \note aco_info_destroy must be called if this succeeds
 * \param info The address of an aco_info struct to initialize
 * \retval 0 Success
 * \retval non-zero Failure
 */
int aco_info_init(struct aco_info *info);

/*! \brief Destroy an initialized aco_info struct
 * \param info The address of the aco_info struct to destroy
 */
void aco_info_destroy(struct aco_info *info);

/*! \brief The option types
 *
 * \note aco_option_register takes an option type which is used
 * to look up the handler for that type. Each non-custom type requires
 * field names for specific types in the struct being configured. Each
 * option below is commented with the field types, additional arguments
 * and example usage with aco_option_register
 */
enum aco_option_type {
	/*! \brief Type for default option handler for ACLs
	 * \note aco_option_register flags:
	 *   non-zero : "permit"
	 *   0        : "deny"
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type struct ast_ha *.
	 *
	 * Example:
	 * \code
	 * struct test_item {
	 *     struct ast_ha *ha;
	 * };
	 * aco_option_register(&cfg_info, "permit", ACO_EXACT, my_types, NULL, OPT_ACL_T, 1, FLDSET(struct test_item, ha));
	 * aco_option_register(&cfg_info, "deny", ACO_EXACT, my_types, NULL, OPT_ACL_T, 0, FLDSET(struct test_item, ha));
	 * \endcode
	 */
	OPT_ACL_T,

	/*! \brief Type for default option handler for bools (ast_true/ast_false)
	 * \note aco_option_register flags:
	 *   non-zero : process via ast_true
	 *   0        : process via ast_false
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type int. It is important to note that the field
	 *   cannot be a bitfield. If bitfields are required, they must be set via a custom handler.
	 *
	 * Example:
	 * \code
	 * struct test_item {
	 *     int enabled;
	 * };
	 * aco_option_register(&cfg_info, "enabled", ACO_EXACT, my_types, "no", OPT_BOOL_T, 1, FLDSET(struct test_item, enabled));
	 * \endcode
	 */
	OPT_BOOL_T,

	/*! \brief Type for default option handler for bools (ast_true/ast_false) that are stored in a flag
	 * \note aco_option_register flags:
	 *   non-zero : process via ast_true
	 *   0        : process via ast_false
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type of unsigned int.
	 *   The flag to set
	 *
	 * Example:
	 * \code
	 * #define MY_TYPE_ISQUIET    1 << 4
	 * struct test_item {
	 *     unsigned int flags;
	 * };
	 * aco_option_register(&cfg_info, "quiet", ACO_EXACT, my_types, "no", OPT_BOOLFLAG_T, 1, FLDSET(struct test_item, flags), MY_TYPE_ISQUIET);
	 * \endcode
	 */
	OPT_BOOLFLAG_T,

	/*! \brief Type for default option handler for character array strings
	 * \note aco_option_register flags:
	 *   non-zero : String cannot be empty.
	 *   0        : String can be empty.
	 * \note aco_option_register varargs:
	 *   CHARFLDSET macro with a field of type char[]
	 *
	 * Example:
	 * \code
	 * struct test_item {
	 *     char description[128];
	 * };
	 * aco_option_register(&cfg_info, "description", ACO_EXACT, my_types, "none", OPT_CHAR_ARRAY_T, 0, CHARFLDSET(struct test_item, description));
	 * \endcode
	 */
	OPT_CHAR_ARRAY_T,

	/*! \brief Type for default option handler for format capabilities
	 * \note aco_option_register flags:
	 *   non-zero : This is an "allow" style option
	 *   0        : This is a "disallow" style option
	 * aco_option_register varargs:
	 *   FLDSET macro with field representing a struct ast_format_cap *
	 *
	 * Example:
	 * \code
	 * struct test_item {
	 *     struct ast_format cap *cap;
	 * };
	 * aco_option_register(&cfg_info, "allow", ACO_EXACT, my_types, "ulaw,alaw", OPT_CODEC_T, 1, FLDSET(struct test_item, cap));
	 * aco_option_register(&cfg_info, "disallow", ACO_EXACT, my_types, "all", OPT_CODEC_T, 0, FLDSET(struct test_item, cap));
	 * \endcode
	 */
	OPT_CODEC_T,

	/*! \brief Type for a custom (user-defined) option handler */
	OPT_CUSTOM_T,

	/*! \brief Type for default option handler for doubles
	 *
	 * \note aco_option_register flags:
	 *   See flags available for use with the PARSE_DOUBLE type for the ast_parse_arg function
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type double
	 *
	 * Example:
	 * struct test_item {
	 *     double dub;
	 * };
	 * \code
	 * aco_option_register(&cfg_info, "doubleopt", ACO_EXACT, my_types, "3", OPT_DOUBLE_T, 0, FLDSET(struct test_item, dub));
	 * \endcode
	 */
	OPT_DOUBLE_T,

	/*! \brief Type for default option handler for signed integers
	 *
	 * \note aco_option_register flags:
	 *   See flags available for use with the PARSE_INT32 type for the ast_parse_arg function
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type int32_t
	 *   The remaining varargs for should be arguments compatible with the varargs for the
	 *   ast_parse_arg function with the PARSE_INT32 type and the flags passed in the
	 *   aco_option_register flags parameter.
	 *
	 * \note In most situations, it is preferable to not pass the PARSE_DEFAULT flag. If a config
	 * contains an invalid value, it is better to let the config loading fail with warnings so that
	 * the problem is fixed by the administrator.
	 *
	 * Example:
	 * struct test_item {
	 *     int32_t intopt;
	 * };
	 * \code
	 * aco_option_register(&cfg_info, "intopt", ACO_EXACT, my_types, "3", OPT_INT_T, PARSE_IN_RANGE, FLDSET(struct test_item, intopt), -10, 10);
	 * \endcode
	 */
	OPT_INT_T,

	/*! \brief Type for a default handler that should do nothing
	 *
	 * \note This might be useful for a "type" field that is valid, but doesn't
	 * actually need to do anything
	 */
	OPT_NOOP_T,

	/*! \brief Type for default handler for ast_sockaddrs
	 *
	 * \note aco_option_register flags:
	 *   See flags available for use with the PARSE_ADDR type for the ast_parse_arg function
	 * aco_option_register varargs:
	 *   FLDSET macro with the field being of type struct ast_sockaddr.
	 *
	 * Example:
	 * \code
	 * struct test_item {
	 *     struct ast_sockaddr addr;
	 * };
	 * aco_option_register(&cfg_info, "sockaddropt", ACO_EXACT, my_types, "0.0.0.0:1234", OPT_SOCKADDR_T, 0, FLDSET(struct test_item, addr));
	 * \endcode
	 */
	OPT_SOCKADDR_T,

	/*! \brief Type for default option handler for stringfields
	 * \note aco_option_register flags:
	 *   non-zero : String cannot be empty.
	 *   0        : String can be empty.
	 * aco_option_register varargs:
	 *   STRFLDSET macro with the field being the field created by AST_STRING_FIELD
	 *
	 * Example:
	 * \code
	 * struct test_item {
	 *     AST_DECLARE_STRING_FIELDS(
	 *         AST_STRING_FIELD(thing);
	 *     );
	 * };
	 * aco_option_register(&cfg_info, "thing", ACO_EXACT, my_types, NULL, OPT_STRINGFIELD_T, 0, STRFLDSET(struct test_item, thing));
	 * \endcode
	 */
	OPT_STRINGFIELD_T,

	/*! \brief Type for default option handler for unsigned integers
	 *
	 * \note aco_option_register flags:
	 *   See flags available for use with the PARSE_UINT32 type for the ast_parse_arg function
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type uint32_t
	 *   The remaining varargs for should be arguments compatible with the varargs for the
	 *   ast_parse_arg function with the PARSE_UINT32 type and the flags passed in the
	 *   aco_option_register flags parameter.
	 *
	 * \note In most situations, it is preferable to not pass the PARSE_DEFAULT flag. If a config
	 * contains an invalid value, it is better to let the config loading fail with warnings so that
	 * the problem is fixed by the administrator.
	 *
	 * Example:
	 * struct test_item {
	 *     int32_t intopt;
	 * };
	 * \code
	 * aco_option_register(&cfg_info, "uintopt", ACO_EXACT, my_types, "3", OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct test_item, uintopt), 1, 10);
	 * \endcode
	 */
	OPT_UINT_T,

	/*! \brief Type for default option handler for bools (ast_true/ast_false)
	 * \note aco_option_register flags:
	 *   non-zero : process via ast_true
	 *   0        : process via ast_false
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type int. It is important to note that the field
	 *   cannot be a bitfield. If bitfields are required, they must be set via a custom handler.
	 *
	 * This is exactly the same as OPT_BOOL_T. The only difference is that when
	 * translated to a string, OPT_BOOL_T becomes "true" or "false"; OPT_YESNO_T becomes
	 * "yes" or "no".
	 *
	 * Example:
	 * \code
	 * struct test_item {
	 *     int enabled;
	 * };
	 * aco_option_register(&cfg_info, "enabled", ACO_EXACT, my_types, "no", OPT_YESNO_T, 1, FLDSET(struct test_item, enabled));
	 * \endcode
	 */
	OPT_YESNO_T,

	/*! \brief Type for default option handler for time length signed integers
	 *
	 * \note aco_option_register flags:
	 *   See flags available for use with the PARSE_TIMELEN type for the ast_parse_arg function
	 * aco_option_register varargs:
	 *   FLDSET macro with the field of type int
	 *   The remaining varargs for should be arguments compatible with the varargs for the
	 *   ast_parse_arg function with the PARSE_TIMELEN type and the flags passed in the
	 *   aco_option_register flags parameter.
	 *
	 * \note In most situations, it is preferable to not pass the PARSE_DEFAULT flag. If a config
	 * contains an invalid value, it is better to let the config loading fail with warnings so that
	 * the problem is fixed by the administrator.
	 *
	 * Example:
	 * struct test_item {
	 *     int timelen;
	 * };
	 * \code
	 * aco_option_register(&cfg_info, "timelen", ACO_EXACT, my_types, "3", OPT_TIMELEN_T, PARSE_IN_RANGE, FLDSET(struct test_item, intopt), TIMELEN_MILLISECONDS, -10, 10);
	 * \endcode
	 */
	OPT_TIMELEN_T,

};

/*! \brief A callback function for handling a particular option
 * \param opt The option being configured
 * \param var The config variable to use to configure \a obj
 * \param obj The object to be configured
 *
 * \retval 0 Parsing and recording the config value succeeded
 * \retval non-zero Failure. Parsing should stop and no reload applied
 */
typedef int (*aco_option_handler)(const struct aco_option *opt, struct ast_variable *var, void *obj);

/*! \brief Allocate a container to hold config options */
struct ao2_container *aco_option_container_alloc(void);

/*! \brief Return values for the aco_process functions
 */
enum aco_process_status {
	ACO_PROCESS_OK,        /*!< \brief The config was processed and applied */
	ACO_PROCESS_UNCHANGED, /*!< \brief The config had not been edited and no changes applied */
	ACO_PROCESS_ERROR,     /*!< \brief Their was an error and no changes were applied */
};

/*! \brief Process a config info via the options registered with an aco_info
 *
 * \param info The config_options_info to be used for handling the config
 * \param reload Non-zero if this is for a reload.
 *
 * \retval ACO_PROCESS_OK Success
 * \retval ACO_PROCESS_ERROR Failure
 * \retval ACO_PROCESS_UNCHANGED No change due to unedited config file
 */
enum aco_process_status aco_process_config(struct aco_info *info, int reload);

/*! \brief Process config info from an ast_config via options registered with an aco_info
 *
 * \param info The aco_info to be used for handling the config
 * \param file The file attached to aco_info that the config represents
 * \param cfg A pointer to a loaded ast_config to parse
 *
 * \retval ACO_PROCESS_OK Success
 * \retval ACO_PROCESS_ERROR Failure
 */
enum aco_process_status aco_process_ast_config(struct aco_info *info, struct aco_file *file, struct ast_config *cfg);

/*! \brief Parse a single ast_variable and apply it to an object
 * \note This function can be used to build up an object by repeatedly passing in
 * the config variable name and values that would be found in a config file. This can
 * be useful if the object is to be populated by a dialplan function, for example.
 *
 * \param type The aco_type associated with the object
 * \param cat The category to use
 * \param var A variable to apply to the object
 * \param obj A pointer to the object to be configured
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int aco_process_var(struct aco_type *type, const char *cat, struct ast_variable *var, void *obj);

/*! \brief Parse each option defined in a config category
 * \param type The aco_type with the options for parsing
 * \param cfg The ast_config being parsed
 * \param cat The config category being parsed
 * \param obj The user-defined config object that will store the parsed config items
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int aco_process_category_options(struct aco_type *type, struct ast_config *cfg, const char *cat, void *obj);

/*! \brief Set all default options of \a obj
 * \param type The aco_type with the options
 * \param category The configuration category from which \a obj is being configured
 * \param obj The object being configured
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int aco_set_defaults(struct aco_type *type, const char *category, void *obj);

/*! \brief register a config option
 *
 * \note this should probably only be called by one of the aco_option_register* macros
 *
 * \param info The aco_info holding this module's config information
 * \param name The name of the option
 * \param match_type
 * \param types An array of valid option types for matching categories to the correct struct type
 * \param default_val The default value of the option in the same format as defined in a config file
 * \param type The option type (only for default handlers)
 * \param handler The handler function for the option (only for non-default types)
 * \param flags a type specific flags, stored in the option and available to the handler
 * \param no_doc if non-zero, this option should not have documentation
 * \param argc The number for variadic arguments
 * \param ... field offsets to store for default handlers
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __aco_option_register(struct aco_info *info, const char *name, enum aco_matchtype match_type, struct aco_type **types,
	const char *default_val, enum aco_option_type type, aco_option_handler handler, unsigned int flags, unsigned int no_doc, size_t argc, ...);

/*! \brief Register a config option
 * \param info A pointer to the aco_info struct
 * \param name The name of the option
 * \param matchtype
 * \param types An array of valid option types for matching categories to the correct struct type
 * \param default_val The default value of the option in the same format as defined in a config file
 * \param opt_type The option type for default option type handling
 * \param flags a type specific flags, stored in the option and available to the handler
 * \param ...
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define aco_option_register(info, name, matchtype, types, default_val, opt_type, flags, ...) \
	__aco_option_register(info, name, matchtype, types, default_val, opt_type, NULL, flags, 0, VA_NARGS(__VA_ARGS__), __VA_ARGS__);

/*! \brief Register a config option
 * \param info A pointer to the aco_info struct
 * \param name The name of the option
 * \param matchtype
 * \param types An array of valid option types for matching categories to the correct struct type
 * \param default_val The default value of the option in the same format as defined in a config file
 * \param handler The handler callback for the option
 * \param flags \a type specific flags, stored in the option and available to the handler
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define aco_option_register_custom(info, name, matchtype, types, default_val, handler, flags) \
	__aco_option_register(info, name, matchtype, types, default_val, OPT_CUSTOM_T, handler, flags, 0, 0);

/*! \brief Register a config option with no expected documentation
 * \param info A pointer to the aco_info struct
 * \param name The name of the option
 * \param matchtype
 * \param types An array of valid option types for matching categories to the correct struct type
 * \param default_val The default value of the option in the same format as defined in a config file
 * \param handler The handler callback for the option
 * \param flags \a type specific flags, stored in the option and available to the handler
 *
 * \note This is used primarily with custom options that only have internal purposes
 * and that should be ignored by the user.
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define aco_option_register_custom_nodoc(info, name, matchtype, types, default_val, handler, flags) \
	__aco_option_register(info, name, matchtype, types, default_val, OPT_CUSTOM_T, handler, flags, 1, 0);

/*! \brief Register a deprecated (and aliased) config option
 * \param info A pointer to the aco_info struct
 * \param name The name of the deprecated option
 * \param types An array of valid option types for matching categories to the correct struct type
 * \param aliased_to The name of the option that this deprecated option matches to
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int aco_option_register_deprecated(struct aco_info *info, const char *name, struct aco_type **types, const char *aliased_to);

/*!
 * \brief Read the flags of a config option - useful when using a custom callback for a config option
 * \since 12
 *
 * \param option Pointer to the aco_option struct
 *
 * \retval value of the flags on the config option
 */
unsigned int aco_option_get_flags(const struct aco_option *option);

/*!
 * \brief Get the offset position for an argument within a config option
 *
 * \param option Pointer to the aco_option struct
 * \param position Argument number
 *
 * \retval position of the argument
 */
intptr_t aco_option_get_argument(const struct aco_option *option, unsigned int position);

/*! \note  Everything below this point is to handle converting varargs
 * containing field names, to varargs containing a count of args, followed
 * by the offset of each of the field names in the struct type that is
 * passed in. It is currently limited to 8 arguments, but 8 variadic
 * arguments, like 640K, should be good enough for anyone. If not, it is
 * easy to add more.
 *
 */

/*!
 * \brief Map \a func(\a func_arg, field) across all fields including \a x
 * \param func The function (almost certainly offsetof) to map across the fields
 * \param func_arg The first argument (almost certainly a type (e.g. "struct mystruct")
 * \param x The first field
 * \param ... varargs The rest of the fields
 *
 * Example usage:
 * \code
 * struct foo {
 *     int a;
 *     char *b;
 *     foo *c;
 * };
 * ARGMAP(offsetof, struct foo, a, c)
 * \endcode
 *
 * produces the string:
 *
 * \code
 * 2, offsetof(struct foo, a), offsetof(struct foo, b)
 * \endcode
 * which can be passed as the varargs to some other function
 *
 * The macro isn't limited to offsetof, but that is the only purpose for
 * which it has been tested.
 *
 * As an example of how the processing works:
 * \verbatim
 * ARGMAP(offsetof, struct foo, a, b, c) ->
 * ARGMAP_(3, offsetof, struct foo, a, b, c) ->
 * ARGMAP_3(offsetof, struct foo, 3, a, b, c) ->
 * ARGMAP_2(offsetof, struct foo, ARGIFY(3, offsetof(struct foo, a)), b, c) ->
 * ARGMAP_1(offsetof, struct foo, ARGIFY(3, offsetof(struct foo, a), offsetof(struct foo, b)), c) ->
 * ARGIFY(3, offsetof(struct foo, a), offsetof(struct foo, b), offsetof(struct foo, c)) ->
 * 3, offsetof(struct foo, a), offsetof(struct foo, b), offsetof(struct foo, c)
 * \endverbatim
 *
 */
#define ARGMAP(func, func_arg, x, ...) ARGMAP_(VA_NARGS(x, ##__VA_ARGS__), func, func_arg, x, __VA_ARGS__)

/*! \note This is sneaky. On the very first argument, we set "in" to N, the number of arguments, so
 * that the accumulation both works properly for the first argument (since "in" can't be empty) and
 * we get the number of arguments in our varargs as a bonus
 */
#define ARGMAP_(N, func, func_arg, x, ...) PASTE(ARGMAP_, N)(func, func_arg, N, x, __VA_ARGS__)

/*! \def PASTE(arg1, arg2)
 * \brief Paste two arguments together, even if they are macros themselves
 * \note Uses two levels to handle the case where arg1 and arg2 are macros themselves
 */
#define PASTE(arg1, arg2)  PASTE1(arg1, arg2)
#define PASTE1(arg1, arg2) arg1##arg2

/*! \brief Take a comma-separated list and allow it to be passed as a single argument to another macro */
#define ARGIFY(...) __VA_ARGS__

/*! \brief The individual field handlers for ARGMAP
 * \param func The function (most likely offsetof)
 * \param func_arg The first argument to func (most likely a type e.g. "struct my_struct")
 * \param in The accumulated function-mapped field names so far
 * \param x The next field name
 * \param ... varargs The rest of the field names
 */
#define ARGMAP_1(func, func_arg, in, x, ...) ARGIFY(in, func(func_arg, x))
#define ARGMAP_2(func, func_arg, in, x, ...)\
	ARGMAP_1(func, func_arg, ARGIFY(in, func(func_arg, x)), __VA_ARGS__)
#define ARGMAP_3(func, func_arg, in, x, ...)\
	ARGMAP_2(func, func_arg, ARGIFY(in, func(func_arg, x)), __VA_ARGS__)
#define ARGMAP_4(func, func_arg, in, x, ...)\
	ARGMAP_3(func, func_arg, ARGIFY(in, func(func_arg, x)), __VA_ARGS__)
#define ARGMAP_5(func, func_arg, in, x, ...)\
	ARGMAP_4(func, func_arg, ARGIFY(in, func(func_arg, x)), __VA_ARGS__)
#define ARGMAP_6(func, func_arg, in, x, ...)\
	ARGMAP_5(func, func_arg, ARGIFY(in, func(func_arg, x)), __VA_ARGS__)
#define ARGMAP_7(func, func_arg, in, x, ...)\
	ARGMAP_6(func, func_arg, ARGIFY(in, func(func_arg, x)), __VA_ARGS__)
#define ARGMAP_8(func, func_arg, in, x, ...)\
	ARGMAP_7(func, func_arg, ARGIFY(in, func(func_arg, x)), __VA_ARGS__)

/*! \def VA_NARGS(...)
 * \brief Results in the number of arguments passed to it
 * \note Currently only up to 8, but expanding is easy. This macro basically counts
 * commas + 1. To visualize:
 * \verbatim
 * VA_NARGS(one, two, three) ->                    v
 * VA_NARGS1(one, two, three,  8,  7,  6,  5,  4,  3,  2,  1,  0) ->
 * VA_NARGS1( _1,  _2,    _3, _4, _5, _6, _7, _8,  N, ...       ) N -> 3
 *
 * Note that VA_NARGS *does not* work when there are no arguments passed. Pasting an empty
 * __VA_ARGS__ with a comma like ", ##__VA_ARGS__" will delete the leading comma, but it
 * does not work when __VA_ARGS__ is the first argument. Instead, 1 is returned instead of 0:
 *
 * VA_NARGS() ->                              v
 * VA_NARGS1(  ,  8,  7,  6,  5,  4,  3,  2,  1,  0) ->
 * VA_NARGS1(_1, _2, _3, _4, _5, _6, _7, _8,  N) -> 1
 * \endverbatim
 */
#define VA_NARGS(...) VA_NARGS1(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define VA_NARGS1(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N

/*! \def FLDSET(type, ...)
 * \brief Convert a struct and list of fields to an argument list of field offsets
 * \param type The type with the fields (e.g. "struct my_struct")
 * \param ... varags The fields in the struct whose offsets are needed as arguments
 *
 * For example:
 * \code
 * struct foo {int a, char b[128], char *c};
 * FLDSET(struct foo, a, c)
 * \endcode
 *
 * produces
 * \code
 * offsetof(struct foo, a), offsetof(struct foo, c)
 * \endcode
 */
#define FLDSET(type, ...) FLDSET1(type, ##__VA_ARGS__)
#define FLDSET1(type, ...) POPPED(ARGMAP(offsetof, type, ##__VA_ARGS__))

/*! \def STRFLDSET(type, ...)
 * \brief Convert a struct and a list of stringfield fields to an argument list of field offsets
 * \note Stringfields require the passing of the field manager pool, and field manager to the
 * default stringfield option handler, so registering options that point to stringfields requires
 * this macro to be called instead of the FLDSET macro.
 * \param type The type with the fields (e.g. "struct my_struct")
 * \param ... varargs The fields in the struct whose offsets are needed as arguments
 */
#define STRFLDSET(type, ...) FLDSET(type, __VA_ARGS__, __field_mgr_pool, __field_mgr)

/*! \def CHARFLDSET(type, field)
 * \brief A helper macro to pass the appropriate arguments to aco_option_register for OPT_CHAR_ARRAY_T
 * \note This will pass the offset of the field and its length as arguments
 * \param type The type with the char array field (e.g. "struct my_struct")
 * \param field The name of char array field
 */
#define CHARFLDSET(type, field) ARGIFY(offsetof(type, field), sizeof(((type *)0)->field))

/*! \def POPPED(...)
 * \brief A list of arguments without the first argument
 * \note Used internally to remove the leading "number of arguments" argument from ARGMAP for
 * FLDSET. This is because a call to FLDSET may be followed by additional arguments in
 * aco_register_option, so the true number of arguments will possibly be different than what
 * ARGMAP returns.
 * \param ... varags A list of arguments
 * \verbatim
 * POPPED(a, b, c) -> b, c
 * \endverbatim
 */
#define POPPED(...) POPPED1(__VA_ARGS__)
#define POPPED1(x, ...) __VA_ARGS__

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CONFIG_OPTIONS_H */
