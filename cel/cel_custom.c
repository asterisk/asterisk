/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Steve Murphy <murf@digium.com>
 * much borrowed from cdr code (cdr_custom.c), author Mark Spencer
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
 *
 * \brief Custom Comma Separated Value CEL records.
 *
 * \author Steve Murphy <murf@digium.com>
 * Logs in LOG_DIR/cel_custom
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/paths.h"
#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"
#include "asterisk/vector.h"
#include "asterisk/json.h"

#include "custom_common.h"

#define CONFIG "cel_custom.conf"

AST_THREADSTORAGE(custom_buf);

static const char name[] = "cel-custom";


#define CUSTOM_BACKEND_NAME "CEL Custom CSV Logging"

static AST_RWLIST_HEAD_STATIC(sinks, cel_config);

static void free_config(void)
{
	struct cel_config *sink;

	while ((sink = AST_RWLIST_REMOVE_HEAD(&sinks, list))) {
		cel_free_sink(sink);
	}
}

static int load_basic_config(struct ast_category *category, int *mappings)
{
	struct ast_variable *var = NULL;
	struct cel_config *sink = NULL;
	int res = 0;

	for (var = ast_category_first(category); var; var = var->next) {
		if (ast_strlen_zero(var->name) || ast_strlen_zero(var->value)) {
			ast_log(LOG_WARNING, "CEL Mapping must have both a filename and a template at line %d\n", var->lineno);
			continue;
		}
		sink = ast_calloc_with_stringfields(1, struct cel_config, 1024);
		if (!sink) {
			return -2;
		}
		sink->sink_type = cel_sink_legacy;
		res = ast_string_field_build(sink, template, "%s\n", var->value);
		if (res != 0) {
			cel_free_sink(sink);
			return -2;
		}
		if (var->name[0] == '/') {
			res = ast_string_field_build(sink, filename, "%s", var->name);
		} else {
			res = ast_string_field_build(sink, filename, "%s/%s/%s", ast_config_AST_LOG_DIR, name, var->name);
		}
		if (res != 0) {
			cel_free_sink(sink);
			return -2;
		}
		ast_mutex_init(&sink->lock);
		ast_verb(3, "Added CEL basic CSV mapping for '%s'.\n", sink->filename);
		AST_RWLIST_INSERT_TAIL(&sinks, sink, list);
		(*mappings)++;
		sink = NULL;
	}

	return 0;
}


static int load_advanced_config(struct ast_category *category, int *mappings)
{
	const char *category_name = ast_category_get_name(category);
	struct cel_config *sink = NULL;
	const char *value;
	int res = 0;

	char *fields = NULL;
	char *field = NULL;

	ast_debug(2, "Processing CEL file '%s'\n", category_name);
	sink = ast_calloc_with_stringfields(1, struct cel_config, 512);
	if (!sink) {
		ast_log(LOG_ERROR, "Unable to allocate memory for configuration settings.\n");
		return -2;
	}
	sink->sink_type = cel_sink_advanced;
	if (category_name[0] == '/') {
		res = ast_string_field_build(sink, filename, "%s", category_name);
	} else {
		res = ast_string_field_build(sink, filename, "%s/%s/%s", ast_config_AST_LOG_DIR, name, category_name);
	}
	if (res != 0) {
		cel_free_sink(sink);
		return -2;
	}
	sink->format_type = cel_format_csv;
	strcpy(sink->separator, ","); /* Safe */
	strcpy(sink->quote, "\""); /* Safe */
	strcpy(sink->quote_escape, sink->quote); /* Safe */
	sink->quoting_method = cel_quoting_method_all;

	value = ast_variable_find(category, "format");
	if (!ast_strlen_zero(value)) {
		if (ast_strings_equal(value, "json")) {
			sink->format_type = cel_format_json;
		} else if (ast_strings_equal(value, "csv")) {
			sink->format_type = cel_format_csv;
		} else {
			ast_log(LOG_WARNING, "Custom CEL destination '%s' has invalid format '%s'\n",
				sink->filename, value);
			cel_free_sink(sink);
			return -1;
		}
	}
	ast_debug(2, "%s: format: %s\n", category_name, S_OR(value, "csv"));

	value = ast_variable_find(category, "separator_character");
	if (!ast_strlen_zero(value)) {
		ast_copy_string(sink->separator, ast_unescape_c(ast_strdupa(value)), 2);
	}
	ast_debug(2, "%s: separator: %s\n", category_name, sink->separator);

	value = ast_variable_find(category, "quote_character");
	if (!ast_strlen_zero(value)) {
		ast_copy_string(sink->quote, value, 2);
	}
	ast_debug(2, "%s: quote: %s\n", category_name, sink->quote);

	value = ast_variable_find(category, "quote_escape_character");
	if (!ast_strlen_zero(value)) {
		ast_copy_string(sink->quote_escape, value, 2);
	}
	ast_debug(2, "%s: quote_escape: %s\n", category_name, sink->quote_escape);

	value = ast_variable_find(category, "quoting_method");
	if (!ast_strlen_zero(value)) {
		if (ast_strings_equal(value, "all")) {
			sink->quoting_method = cel_quoting_method_all;
		} else if (ast_strings_equal(value, "minimal")) {
			sink->quoting_method = cel_quoting_method_minimal;
		} else if (ast_strings_equal(value, "non_numeric")) {
			sink->quoting_method = cel_quoting_method_non_numeric;
		} else if (ast_strings_equal(value, "none")) {
			sink->quoting_method = cel_quoting_method_none;
		} else {
			ast_log(LOG_WARNING, "Custom CEL destination '%s' has invalid quoting method '%s'\n",
				sink->filename, value);
			cel_free_sink(sink);
			return -1;
		}
	}
	ast_debug(2, "%s: quoting_method: %s\n", category_name, S_OR(value, "all"));

	value = ast_variable_find(category, "fields");
	if (ast_strlen_zero(value)) {
		ast_log(LOG_WARNING, "Custom CEL destination '%s' 'fields' parameter is missing or empty\n",
			sink->filename);
		cel_free_sink(sink);
		return -1;
	}
	fields = ast_strdupa(value);

	if (AST_VECTOR_INIT(&sink->fields, 20) != 0) {
		cel_free_sink(sink);
		return -2;
	}

	while((field = ast_strsep_quoted(&fields, ',', '"', AST_STRSEP_ALL))) {
		struct cel_field *cel_field = cel_field_alloc(field, sink->format_type, category_name);

		if (!cel_field) {
			ast_log(LOG_WARNING, "nf: %s\n", field);
			continue;
		}

		res = AST_VECTOR_APPEND(&sink->fields, cel_field);
		if (res != 0) {
			cel_free_sink(sink);
			return -1;
		}
	}
	ast_debug(2, "fields: %d\n", (int) AST_VECTOR_SIZE(&sink->fields));

	ast_mutex_init(&sink->lock);
	ast_verb(3, "Added CEL advanced CSV mapping for '%s'.\n", sink->filename);
	AST_RWLIST_INSERT_TAIL(&sinks, sink, list);
	(*mappings)++;
	return 0;
}

static int load_config(void)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { 0 };
	struct ast_category *category = NULL;

	int mappings = 0;
	int res = 0;

	cfg = ast_config_load(CONFIG, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load " CONFIG ". Not logging CEL to custom CSVs.\n");
		return -1;
	}

	while ((category = ast_category_browse_filtered(cfg, NULL, category, NULL))) {
		const char *category_name = ast_category_get_name(category);

		if (ast_strings_equal(category_name, "mappings")) {
			res += load_basic_config(category, &mappings);
		} else {
			res += load_advanced_config(category, &mappings);
		}
	}

	ast_config_destroy(cfg);

	ast_verb(1, "Added CEL CSV mapping for %d files.\n", mappings);

	return res;
}

static void custom_log_basic(struct ast_event *event, struct cel_config *config,
	struct ast_channel *dummy)
{
	struct ast_str *str;
	FILE *out;

	/* Batching saves memory management here.  Otherwise, it's the same as doing an allocation and free each time. */
	if (!(str = ast_str_thread_get(&custom_buf, 16))) {
		return;
	}

	ast_str_substitute_variables(&str, 0, dummy, config->template);

	/* Even though we have a lock on the list, we could be being chased by
	   another thread and this lock ensures that we won't step on anyone's
	   toes.  Once each CEL backend gets it's own thread, this lock can be
	   removed. */
	ast_mutex_lock(&config->lock);

	/* Because of the absolutely unconditional need for the
	   highest reliability possible in writing billing records,
	   we open write and close the log file each time */
	if ((out = fopen(config->filename, "a"))) {
		fputs(ast_str_buffer(str), out);
		fflush(out); /* be particularly anal here */
		fclose(out);
	} else {
		ast_log(LOG_ERROR, "Unable to re-open master file %s : %s\n", config->filename, strerror(errno));
	}

	ast_mutex_unlock(&config->lock);
}

static void custom_log_advanced(struct ast_event *event, struct cel_config *config)
{
	int ix = 0;
	struct ast_str *str;
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	FILE *out;

	if (config->format_type == cel_format_csv) {
		if (!(str = ast_str_thread_get(&custom_buf, 512))) {
			return;
		}
		ast_str_reset(str);
	} else {
		if (!(json = ast_json_object_create())) {
			return;
		}
	}

	for (ix = 0; ix < AST_VECTOR_SIZE(&config->fields); ix++) {
		struct cel_field *cel_field = AST_VECTOR_GET(&config->fields, ix);
		if (config->format_type == cel_format_csv) {
			cel_field->csv_field_appender(&str, event, config, cel_field, ix == 0);
		} else {
			cel_field->json_field_appender(json, event, config, cel_field, ix == 0);
		}
	}

	ast_mutex_lock(&config->lock);
	/* Because of the absolutely unconditional need for the
	   highest reliability possible in writing billing records,
	   we open write and close the log file each time */
	if ((out = fopen(config->filename, "a"))) {
		if (config->format_type == cel_format_csv) {
			ast_str_append(&str, 0, "\n");
			fputs(ast_str_buffer(str), out);
		} else {
			ast_json_dump_file_format(json, out, AST_JSON_COMPACT);
			fputs("\n", out);
		}
		fflush(out); /* be particularly anal here */
		fclose(out);
	} else {
		ast_log(LOG_ERROR, "Unable to open CEL file %s : %s\n", config->filename, strerror(errno));
	}
	ast_mutex_unlock(&config->lock);
}


static void custom_log(struct ast_event *event)
{
	struct cel_config *config = NULL;
	struct ast_channel *dummy = NULL;
	int skip_basic = 0;

	AST_RWLIST_RDLOCK(&sinks);

	AST_LIST_TRAVERSE(&sinks, config, list) {
		if (config->sink_type == cel_sink_legacy) {
			if (skip_basic) {
				continue;
			}
			if (!dummy) {
				dummy = ast_cel_fabricate_channel_from_event(event);
				if (!dummy) {
					ast_log(LOG_ERROR, "Unable to fabricate channel from CEL event for '%s'\n",
						config->filename);
					skip_basic = 1;
				}
			}
			custom_log_basic(event, config, dummy);
		} else {
			custom_log_advanced(event, config);
		}
	}

	AST_RWLIST_UNLOCK(&sinks);

	if (dummy) {
		ast_channel_unref(dummy);
	}
}

static int unload_module(void)
{

	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Unload failed.\n");
		return -1;
	}

	free_config();
	AST_RWLIST_UNLOCK(&sinks);
	ast_cel_backend_unregister(CUSTOM_BACKEND_NAME);
	return 0;
}

static enum ast_module_load_result load_module(void)
{
	int res = 0;
	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	res = load_config();
	AST_RWLIST_UNLOCK(&sinks);
	if  (res != 0) {
		free_config();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_cel_backend_register(CUSTOM_BACKEND_NAME, custom_log)) {
		free_config();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	int res = 0;
	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	free_config();
	res = load_config();
	if  (res != 0) {
		free_config();
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_RWLIST_UNLOCK(&sinks);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Customizable Comma Separated Values CEL Backend",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cel",
);
