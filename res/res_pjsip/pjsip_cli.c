/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Fairview 5 Engineering, LLC
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/acl.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"
#include "asterisk/hashtab.h"
#include "asterisk/utils.h"
#include "asterisk/sorcery.h"

static struct ast_hashtab *formatter_registry;

static struct ast_sorcery *sip_sorcery;

struct ast_sip_cli_formatter_entry *ast_sip_lookup_cli_formatter(const char *name)
{
	struct ast_sip_cli_formatter_entry fake_entry = {
		.name = name,
	};
	return ast_hashtab_lookup(formatter_registry, &fake_entry);
}

int ast_sip_cli_print_sorcery_objectset(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	struct ast_variable *i;
	int max_name_width = 13;
	int max_value_width = 14;
	int width;
	char *separator;
	struct ast_variable *objset;

	if (!context->output_buffer) {
		return -1;
	}

	objset = ast_sorcery_objectset_create(ast_sip_get_sorcery(),obj);
	if (!objset) {
		return -1;
	}

	for (i = objset; i; i = i->next) {
		if (i->name) {
			width = strlen(i->name);
			max_name_width = width > max_name_width ? width : max_name_width;
		}
		if (i->value) {
			width = strlen(i->value);
			max_value_width = width > max_value_width ? width : max_value_width;
		}
	}

	if (!(separator = alloca(max_name_width + max_value_width + 8))) {
		return -1;
	}
	memset(separator, '=', max_name_width + max_value_width + 3);
	separator[max_name_width + max_value_width + 3] = 0;

	ast_str_append(&context->output_buffer, 0, " %-*s : %s\n", max_name_width, "ParameterName", "ParameterValue");
	ast_str_append(&context->output_buffer, 0, " %s\n", separator);

	objset = ast_variable_list_sort(objset);

	for (i = objset; i; i = i->next) {
		ast_str_append(&context->output_buffer, 0, " %-*s : %s\n", max_name_width, i->name, i->value);
	}

	return 0;
}

static char *complete_show_sorcery_object(struct ao2_container *container,
	const char *word, int state)
{
	char *result = NULL;
	int wordlen = strlen(word);
	int which = 0;

	struct ao2_iterator i = ao2_iterator_init(container, 0);
	void *object;

	while ((object = ao2_t_iterator_next(&i, "iterate thru endpoints table"))) {
		if (!strncasecmp(word, ast_sorcery_object_get_id(object), wordlen)
			&& ++which > state) {
			result = ast_strdup(ast_sorcery_object_get_id(object));
		}
		ao2_t_ref(object, -1, "toss iterator endpoint ptr before break");
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&i);
	return result;
}

static void dump_str_and_free(int fd, struct ast_str *buf) {
	ast_cli(fd, "%s", ast_str_buffer(buf));
	ast_free(buf);
}

static char *cli_traverse_objects(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, s_container, NULL, ao2_cleanup);
	RAII_VAR(void *, object, NULL, ao2_cleanup);
	int is_container = 0;
	const char *cmd1 = NULL;
	const char *cmd2 = NULL;
	const char *object_id = NULL;
	char formatter_type[64];
	struct ast_sip_cli_formatter_entry *formatter_entry;

	struct ast_sip_cli_context context = {
		.peers_mon_online = 0,
		.peers_mon_offline = 0,
		.peers_unmon_online = 0,
		.peers_unmon_offline = 0,
		.a = a,
		.indent_level = 0,
		.show_details = 0,
		.show_details_only_level_0 = 0,
		.recurse = 0,
	};

	if (cmd == CLI_INIT) {
		return NULL;
	}

	cmd1 = e->cmda[1];
	cmd2 = e->cmda[2];
	object_id = a->argv[3];

	if (!ast_ends_with(cmd2, "s")) {
		ast_copy_string(formatter_type, cmd2, strlen(cmd2)+1);
		is_container = 0;
	} else {
		ast_copy_string(formatter_type, cmd2, strlen(cmd2));
		is_container = 1;
	}

	if (!strcmp(cmd1, "show")) {
		context.show_details_only_level_0 = !is_container;
		context.recurse = 1;
	} else {
		is_container = 1;
	}

	if (cmd == CLI_GENERATE
		&& (is_container
			|| a->argc > 4
			|| (a->argc == 4 && ast_strlen_zero(a->word)))) {
		return CLI_SUCCESS;
	}

	context.output_buffer = ast_str_create(256);
	if (!context.output_buffer) {
		return CLI_FAILURE;
	}

	formatter_entry = ast_sip_lookup_cli_formatter(formatter_type);
	if (!formatter_entry) {
		ast_log(LOG_ERROR, "CLI TRAVERSE failure.  No container found for object type %s\n", formatter_type);
		ast_free(context.output_buffer);
		return CLI_FAILURE;
	}
	ast_str_append(&context.output_buffer, 0, "\n");
	formatter_entry->print_header(NULL, &context, 0);
	ast_str_append(&context.output_buffer, 0, " =========================================================================================\n\n");

	if (is_container || cmd == CLI_GENERATE) {
		container = formatter_entry->get_container(sip_sorcery);
		if (!container) {
			ast_cli(a->fd, "CLI TRAVERSE failure.  No container found for object type %s\n", formatter_type);
			ast_free(context.output_buffer);
			return CLI_FAILURE;
		}
	}

	if (cmd == CLI_GENERATE) {
		ast_free(context.output_buffer);
		return complete_show_sorcery_object(container, a->word, a->n);
	}

	if (is_container) {
		if (!ao2_container_count(container)) {
			dump_str_and_free(a->fd, context.output_buffer);
			ast_cli(a->fd, "No objects found.\n\n");
			return CLI_SUCCESS;
		}

		if (!strcmp(formatter_type, "channel") || !strcmp(formatter_type, "contact")) {
			s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
		} else {
			s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, &ast_sorcery_object_id_compare, NULL);
		}

		ao2_container_dup(s_container, container, OBJ_ORDER_ASCENDING);

		ao2_callback(s_container, OBJ_NODATA, formatter_entry->print_body, &context);
	} else {
		if (!(object = ast_sorcery_retrieve_by_id(
			ast_sip_get_sorcery(), formatter_type, object_id))) {
			dump_str_and_free(a->fd, context.output_buffer);
			ast_cli(a->fd, "Unable to retrieve object %s\n", object_id);
			return CLI_FAILURE;
		}
		formatter_entry->print_body(object, &context, 0);
	}

	ast_str_append(&context.output_buffer, 0, "\n");
	dump_str_and_free(a->fd, context.output_buffer);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(cli_traverse_objects, "List PJSIP Channels", .command = "pjsip list channels",
			.usage = "Usage: pjsip list channels\n       List the active PJSIP channels\n"),
	AST_CLI_DEFINE(cli_traverse_objects, "Show PJSIP Channels", .command = "pjsip show channels",
			.usage = "Usage: pjsip show channels\n       List(detailed) the active PJSIP channels\n"),

	AST_CLI_DEFINE(cli_traverse_objects, "List PJSIP Aors", .command = "pjsip list aors",
			.usage = "Usage: pjsip list aors\n       List the configured PJSIP Aors\n"),
	AST_CLI_DEFINE(cli_traverse_objects, "Show PJSIP Aors", .command = "pjsip show aors",
			.usage = "Usage: pjsip show aors\n       Show the configured PJSIP Aors\n"),
	AST_CLI_DEFINE(cli_traverse_objects, "Show PJSIP Aor", .command = "pjsip show aor",
			.usage = "Usage: pjsip show aor\n       Show the configured PJSIP Aor\n"),

	AST_CLI_DEFINE(cli_traverse_objects, "List PJSIP Contacts", .command = "pjsip list contacts",
			.usage = "Usage: pjsip list contacts\n       List the configured PJSIP contacts\n"),

	AST_CLI_DEFINE(cli_traverse_objects, "List PJSIP Endpoints", .command = "pjsip list endpoints",
			.usage = "Usage: pjsip list endpoints\n       List the configured PJSIP endpoints\n"),
	AST_CLI_DEFINE(cli_traverse_objects, "Show PJSIP Endpoints", .command = "pjsip show endpoints",
			.usage = "Usage: pjsip show endpoints\n       List(detailed) the configured PJSIP endpoints\n"),
	AST_CLI_DEFINE(cli_traverse_objects, "Show PJSIP Endpoint", .command = "pjsip show endpoint",
			.usage = "Usage: pjsip show endpoint <id>\n       Show the configured PJSIP endpoint\n"),

	AST_CLI_DEFINE(cli_traverse_objects, "List PJSIP Auths", .command = "pjsip list auths",
			.usage = "Usage: pjsip list auths\n       List the configured PJSIP Auths\n"),
	AST_CLI_DEFINE(cli_traverse_objects, "Show PJSIP Auths", .command = "pjsip show auths",
			.usage = "Usage: pjsip show auths\n       Show the configured PJSIP Auths\n"),
	AST_CLI_DEFINE(cli_traverse_objects, "Show PJSIP Auth", .command = "pjsip show auth",
			.usage = "Usage: pjsip show auth\n       Show the configured PJSIP Auth\n"),

};


static int compare_formatters(const void *a, const void *b) {
	const struct ast_sip_cli_formatter_entry *afe = a;
	const struct ast_sip_cli_formatter_entry *bfe = b;
	if (!afe || !bfe) {
		ast_log(LOG_ERROR, "One of the arguments to compare_formatters was NULL\n");
		return -1;
	}
	return strcmp(afe->name, bfe->name);
}

static unsigned int hash_formatters(const void *a) {
	const struct ast_sip_cli_formatter_entry *afe = a;
	return ast_hashtab_hash_string(afe->name);
}

int ast_sip_register_cli_formatter(struct ast_sip_cli_formatter_entry *formatter) {
	ast_hashtab_insert_safe(formatter_registry, formatter);
	return 0;
}

int ast_sip_unregister_cli_formatter(struct ast_sip_cli_formatter_entry *formatter) {
	struct ast_sip_cli_formatter_entry *entry = ast_hashtab_lookup(formatter_registry, formatter);
	if (!entry) {
		return -1;
	}
	ast_hashtab_remove_this_object(formatter_registry, entry);
	return 0;
}

int ast_sip_initialize_cli(struct ast_sorcery *sorcery)
{
	formatter_registry = ast_hashtab_create(17, compare_formatters,
		ast_hashtab_resize_java, ast_hashtab_newsize_java, hash_formatters, 0);
	if (!formatter_registry) {
		ast_log(LOG_ERROR, "Unable to create formatter_registry.\n");
		return -1;
	}

	if (ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands))) {
		ast_log(LOG_ERROR, "Failed to register pjsip cli commands.\n");
		ast_hashtab_destroy(formatter_registry, ast_free_ptr);
		return -1;
	}
	sip_sorcery = sorcery;
	return 0;
}

void ast_sip_destroy_cli(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	if (formatter_registry) {
		ast_hashtab_destroy(formatter_registry, ast_free_ptr);
	}
}
