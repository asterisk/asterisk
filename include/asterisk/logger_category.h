/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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
#ifndef ASTERISK_LOGGER_CATEGORY_H
#define ASTERISK_LOGGER_CATEGORY_H

#include "asterisk/logger.h"

/*!
 * Logger category is enabled
 */
#define AST_LOG_CATEGORY_ENABLED -1

/*!
 * Logger category is disabled
 */
#define AST_LOG_CATEGORY_DISABLED 0

/*!
 * \brief Load/Initialize system wide logger category functionality
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
int ast_logger_category_load(void);

/*!
 * \brief Unload system wide logger category functionality
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
int ast_logger_category_unload(void);

/*!
 * \brief Register a debug level logger category
 *
 * \param name The name of the category
 *
 * \retval 0 if failed to register/retrieve an id
 * \return id for the registered category
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
uintmax_t ast_debug_category_register(const char *name);

/*!
 * \brief Un-register a debug level logger category
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
int ast_debug_category_unregister(const char *name);

/*!
 * \brief Set the debug category's sublevel
 *
 * Statements are output at a specified sublevel. Typically any number greater
 * than or equal to 0. Other acceptable values include AST_LOG_CATEGORY_ENABLED
 * and AST_LOG_CATEGORY_DISABLED.
 *
 * \param name The name of the category
 * \param sublevel The debug sublevel output number
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
int ast_debug_category_set_sublevel(const char *name, int sublevel);

/*!
 * \brief Set one or more debug category's sublevel.
 *
 * Accepts an array of category names, and optional associated sublevels. Sublevels can
 * be associated with a name by using a ':' as a separator. For example:
 * \verbatim <category name>:<category sublevel> \endverbatim
 *
 * The given default sublevel is used if no sublevel is associated with a name.
 *
 * \param names An array of category names
 * \param size The size of the array (number of elements)
 * \param default_sublevel The sublevel value to use if one is not associated with a name
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
int ast_debug_category_set_sublevels(const char * const *names, size_t size, int default_sublevel);

/*!
 * \brief Add a unique (no duplicates) result to a request for completion for debug categories.
 *
 * \param argv A list of already completed options
 * \param argc The number of already completed options
 * \param word The word to complete
 * \param state The state
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
char *ast_debug_category_complete(const char * const *argv, int argc, const char *word, int state);

/*!
 * \brief Check if a debug category is enabled, and allowed to output
 *
 * \note If more than one id is specified then if even one is allowed "true"
 *       is returned.
 *
 * \param sublevel Current set sublevel must be this sublevel or less
 * \param ids One or more unique category ids to check
 *
 * \retval 1 if allowed
 * \retval 0 if not allowed
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
*/
int ast_debug_category_is_allowed(int sublevel, uintmax_t ids);

/*!
 * \brief Log for a debug category.
 *
 * This will output log data for debug under the following conditions:
 *
 *  1. The specified sublevel is at, or below the current system debug level
 *  2. At least one of the given category ids is enabled AND
 *     a. The category sublevel is enabled OR the given sublevel is at, or
 *        below a category's specified sublevel.
 *
 * \param sublevel The minimum level to output at
 * \param ids One or more unique category ids to output for
 *
 * \since 16.14
 * \since 17.8
 * \since 18.0
 */
#define ast_debug_category(sublevel, ids, ...) \
	do { \
		if (DEBUG_ATLEAST(sublevel) || ast_debug_category_is_allowed(sublevel, ids)) { \
			ast_log(AST_LOG_DEBUG, __VA_ARGS__); \
		} \
	} while (0)

#endif /* ASTERISK_LOGGER_CATEGORY_H */
