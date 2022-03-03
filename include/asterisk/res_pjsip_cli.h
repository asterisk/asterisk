/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Fairview 5 Engineering, LLC.
 *
 * George Joseph <george.joseph@fairview5.com>
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

#ifndef RES_PJSIP_CLI_H_
#define RES_PJSIP_CLI_H_

#include "asterisk/cli.h"

#define CLI_HEADER_FILLER ".........................................................................................."
#define CLI_DETAIL_FILLER "                                                                                          "
#define CLI_MAX_WIDTH 90
#define CLI_LAST_TABSTOP 62
#define CLI_MAX_TITLE_NAME 8
#define CLI_INDENT_TO_SPACES(x) ((x * 2) + 1 + CLI_MAX_TITLE_NAME)

/*!
 * \brief CLI Formatter Context passed to all formatters.
 */
struct ast_sip_cli_context {
	/*! Buffer used to accumulate cli output. */
	struct ast_str *output_buffer;
	/*! Used to indicate which direction an auth is used for. "I" or "O" */
	char *auth_direction;
	/*! Allows formatters to know how far to indent their output. */
	int indent_level;
	/*! Tells a formatter to dump its object_set. */
	unsigned show_details : 1;
	/*! Tells a formatter to descend into child objects. */
	unsigned recurse : 1;
	/*! Tells a formatter to dump it's object_set only if it's the root object. */
	unsigned show_details_only_level_0 : 1;
};

/*!
 * \brief CLI Formatter Registry Entry
 */
struct ast_sip_cli_formatter_entry {
	/*! A globally unique name for this formatter.  If this formatter entry
	 * is for an existing sorcery object type, then this name must match
	 * the sorcery object type.  Otherwise it can be any string as long as
	 * it's globally unique.
	 */
	const char *name;
	/*! The callback used to print the object's column headers. */
	ao2_callback_fn *print_header;
	/*! The callback used to print the details of the object. */
	ao2_callback_fn *print_body;
	/*! The function used to retrieve a container of all objects of this type. */
	struct ao2_container *(* get_container)(const char *regex);
	/*! The function used to iterate over a container of objects. */
	int (* iterate)(void *container, ao2_callback_fn callback, void *args);
	/*! The function used to retrieve a specific object from it's container. */
	void *(* retrieve_by_id)(const char *id);
	/*! The function used to retrieve an id string from an object. */
	const char *(* get_id)(const void *obj);
};

/*!
 * \brief Registers a CLI formatter.
 *
 * \param formatter An ao2_callback_fn that outputs the formatted data.
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_register_cli_formatter(struct ast_sip_cli_formatter_entry *formatter);

/*!
 * \brief Unregisters a CLI formatter.
 *
 * \param formatter The name of the formatter, usually the sorcery object type.
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_unregister_cli_formatter(struct ast_sip_cli_formatter_entry *formatter);

/*!
 * \brief Looks up a CLI formatter by type.
 *
 * \param name The name of the formatter, usually the sorcery object type.
 * \retval Pointer to formatter entry structure
 */
struct ast_sip_cli_formatter_entry *ast_sip_lookup_cli_formatter(const char *name);

/*!
 * \brief Prints a sorcery object's ast_variable list
 *
 * \param obj The sorcery object
 * \param arg The ast_sip_cli_context
 * \param flags
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_cli_print_sorcery_objectset(void *obj, void *arg, int flags);

char *ast_sip_cli_traverse_objects(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);


#endif /* RES_PJSIP_CLI_H_ */
