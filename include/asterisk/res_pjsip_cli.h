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

/*
 * \brief CLI Formatter Context
 */
struct ast_sip_cli_context {
	int peers_mon_online;
	int peers_mon_offline;
	int peers_unmon_offline;
	int peers_unmon_online;
	struct ast_str *output_buffer;
	const struct ast_cli_args *a;
	const struct ast_sip_endpoint *current_endpoint;
	const struct ast_sip_auth *current_auth;
	const struct ast_sip_aor *current_aor;
	char *auth_direction;
	unsigned int print_flags;
	int indent_level;
	unsigned show_details : 1;
	unsigned recurse : 1;
	unsigned show_details_only_level_0 : 1;
};

/*
 * \brief CLI Formatter Registry Entry
 */
struct ast_sip_cli_formatter_entry {
	const char *name;
	ao2_callback_fn *print_header;
	ao2_callback_fn *print_body;
	struct ao2_container *(* get_container)(void);
	int (* iterator)(const void *container, ao2_callback_fn callback, void *args);
	ao2_sort_fn *comparator;
};

/*!
 * \brief Registers a CLI formatter.
 *
 * \param name The name of the formatter, usually the sorcery object type.
 * \param formatter An ao2_callback_fn that outputs the formatted data.
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_register_cli_formatter(struct ast_sip_cli_formatter_entry *formatter);

/*!
 * \brief Unregisters a CLI formatter.
 *
 * \param name The name of the formatter, usually the sorcery object type.
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
 * \param arg The ast_sip_cli_context.
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_cli_print_sorcery_objectset(void *obj, void *arg, int flags);

char *ast_sip_cli_traverse_objects(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);


#endif /* RES_PJSIP_CLI_H_ */
