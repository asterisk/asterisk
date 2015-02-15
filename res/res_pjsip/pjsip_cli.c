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

static struct ao2_container *formatter_registry;

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

	separator = ast_alloca(max_name_width + max_value_width + 8);

	memset(separator, '=', max_name_width + max_value_width + 3);
	separator[max_name_width + max_value_width + 3] = 0;

	ast_str_append(&context->output_buffer, 0, " %-*s : %s\n", max_name_width, "ParameterName", "ParameterValue");
	ast_str_append(&context->output_buffer, 0, " %s\n", separator);

	objset = ast_variable_list_sort(objset);

	for (i = objset; i; i = i->next) {
		ast_str_append(&context->output_buffer, 0, " %-*s : %s\n", max_name_width, i->name, i->value);
	}

	ast_variables_destroy(objset);

	return 0;
}

static char *complete_show_sorcery_object(struct ao2_container *container,
	struct ast_sip_cli_formatter_entry *formatter_entry,
	const char *word, int state)
{
	char *result = NULL;
	int wordlen = strlen(word);
	int which = 0;

	struct ao2_iterator i = ao2_iterator_init(container, 0);
	void *object;

	while ((object = ao2_t_iterator_next(&i, "iterate thru endpoints table"))) {
		const char *id = formatter_entry->get_id(object);
		if (!strncasecmp(word, id, wordlen)
			&& ++which > state) {
			result = ast_strdup(id);
		}
		ao2_t_ref(object, -1, "toss iterator endpoint ptr before break");
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&i);

	return result;
}

static void dump_str_and_free(int fd, struct ast_str *buf)
{
	ast_cli(fd, "%s", ast_str_buffer(buf));
	ast_free(buf);
}

char *ast_sip_cli_traverse_objects(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_cli_formatter_entry *, formatter_entry, NULL, ao2_cleanup);
	RAII_VAR(void *, object, NULL, ao2_cleanup);
	int is_container = 0;
	const char *cmd1;
	const char *cmd2;
	const char *object_id;
	char formatter_type[64];

	struct ast_sip_cli_context context = {
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
		ast_copy_string(formatter_type, cmd2, sizeof(formatter_type));
		is_container = 0;
	} else if (ast_ends_with(cmd2, "ies")) {
		/* Take the plural "ies" off of the object name and re[place with "y". */
		int l = strlen(cmd2);
		snprintf(formatter_type, 64, "%*.*sy", l - 3, l - 3, cmd2);
		is_container = 1;
	} else {
		/* Take the plural "s" off of the object name. */
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
		ast_log(LOG_ERROR, "No formatter registered for object type %s.\n",
			formatter_type);
		ast_free(context.output_buffer);
		return CLI_FAILURE;
	}
	ast_str_append(&context.output_buffer, 0, "\n");
	formatter_entry->print_header(NULL, &context, 0);
	ast_str_append(&context.output_buffer, 0,
		" =========================================================================================\n\n");

	if (is_container || cmd == CLI_GENERATE) {
		container = formatter_entry->get_container();
		if (!container) {
			ast_cli(a->fd, "No container returned for object type %s.\n",
				formatter_type);
			ast_free(context.output_buffer);
			return CLI_FAILURE;
		}
	}

	if (cmd == CLI_GENERATE) {
		ast_free(context.output_buffer);
		return complete_show_sorcery_object(container, formatter_entry, a->word, a->n);
	}

	if (is_container) {
		if (!ao2_container_count(container)) {
			ast_free(context.output_buffer);
			ast_cli(a->fd, "No objects found.\n\n");
			return CLI_SUCCESS;
		}
		ao2_callback(container, OBJ_NODATA, formatter_entry->print_body, &context);
	} else {
		if (ast_strlen_zero(object_id)) {
			ast_free(context.output_buffer);
			ast_cli(a->fd, "No object specified.\n");
			return CLI_FAILURE;
		}

		object = formatter_entry->retrieve_by_id(object_id);
		if (!object) {
			ast_free(context.output_buffer);
			ast_cli(a->fd, "Unable to find object %s.\n\n", object_id);
			return CLI_SUCCESS;
		}
		formatter_entry->print_body(object, &context, 0);
	}

	ast_str_append(&context.output_buffer, 0, "\n");
	dump_str_and_free(a->fd, context.output_buffer);
	return CLI_SUCCESS;
}

static int formatter_sort(const void *obj, const void *arg, int flags)
{
	const struct ast_sip_cli_formatter_entry *left_obj = obj;
	const struct ast_sip_cli_formatter_entry *right_obj = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_obj->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left_obj->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left_obj->name, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int formatter_compare(void *obj, void *arg, int flags)
{
	const struct ast_sip_cli_formatter_entry *left_obj = obj;
	const struct ast_sip_cli_formatter_entry *right_obj = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_obj->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		if (strcmp(left_obj->name, right_key) == 0) {;
			cmp = CMP_MATCH | CMP_STOP;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		if (strncmp(left_obj->name, right_key, strlen(right_key)) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int formatter_hash(const void *obj, int flags)
{
	const struct ast_sip_cli_formatter_entry *left_obj = obj;
	if (flags & OBJ_SEARCH_OBJECT) {
		return ast_str_hash(left_obj->name);
	} else if (flags & OBJ_SEARCH_KEY) {
		return ast_str_hash(obj);
	}

	return -1;
}

struct ast_sip_cli_formatter_entry *ast_sip_lookup_cli_formatter(const char *name)
{
	return ao2_find(formatter_registry, name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
}

int ast_sip_register_cli_formatter(struct ast_sip_cli_formatter_entry *formatter)
{
	ast_assert(formatter != NULL);
	ast_assert(formatter->name != NULL);
	ast_assert(formatter->print_body != NULL);
	ast_assert(formatter->print_header != NULL);
	ast_assert(formatter->get_container != NULL);
	ast_assert(formatter->iterate != NULL);
	ast_assert(formatter->get_id != NULL);
	ast_assert(formatter->retrieve_by_id != NULL);

	ao2_link(formatter_registry, formatter);

	return 0;
}

int ast_sip_unregister_cli_formatter(struct ast_sip_cli_formatter_entry *formatter)
{
	if (formatter) {
		ao2_wrlock(formatter_registry);
		if (ao2_ref(formatter, -1) == 2) {
			ao2_unlink_flags(formatter_registry, formatter, OBJ_NOLOCK);
		}
		ao2_unlock(formatter_registry);
	}
	return 0;
}

static char *handle_pjsip_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch(cmd) {
	case CLI_INIT:
		e->command = "pjsip show version";
		e->usage =
			"Usage: pjsip show version\n"
			"       Show the version of pjproject that res_pjsip is running against\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "PJPROJECT version currently running against: %s\n", pj_get_version());

	return CLI_SUCCESS;
}

static struct ast_cli_entry pjsip_cli[] = {
	AST_CLI_DEFINE(handle_pjsip_show_version, "Show the version of pjproject in use"),
};

int ast_sip_initialize_cli(void)
{
	formatter_registry = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, 17,
		formatter_hash, formatter_sort, formatter_compare);

	if (!formatter_registry) {
		ast_log(LOG_ERROR, "Unable to create formatter_registry.\n");
		return -1;
	}

	ast_cli_register_multiple(pjsip_cli, ARRAY_LEN(pjsip_cli));

	return 0;
}

void ast_sip_destroy_cli(void)
{
	ast_cli_unregister_multiple(pjsip_cli, ARRAY_LEN(pjsip_cli));
	ao2_ref(formatter_registry, -1);
}
