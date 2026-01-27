/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

/*!
 * \file
 * \author George Joseph <gjoseph@sangoma.com>
 *
 * \brief Common config file handling for res_cdrel_custom.
 *
 * This file is a 'bit' complex.  The reasoning is that the functions
 * do as much work as possible at module load time to reduce the workload
 * at run time.
 *
 */

#include "cdrel.h"

#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"

/*
 * The DSV files get placed in specific subdirectories
 * while the SQL databases get placed directly in /var/log/asterisk.
 */
static const char *dirname_map[cdrel_backend_type_end][cdrel_record_type_end] = {
	[cdrel_backend_text] = {
		[cdrel_record_cdr] = "cdr-custom",
		[cdrel_record_cel] = "cel-custom",
	},
	[cdrel_backend_db] = {
		[cdrel_record_cdr] = NULL,
		[cdrel_record_cel] = NULL,
	}
};

static void field_free(struct cdrel_field *f) {
	if (f) {
		ast_free(f);
	}
}

struct field_parse_result {
	char *result;
	int functions;
	int csv_quote;
	int cdr;
	int is_literal;
	int unknown_functions;
	int parse_failed;
};

/*
 * To maximize the possibility that we can put a legacy config through the
 * much faster advanced process, we need to ensure that we can handle
 * everything in the legacy config.
 */
static const char *allowed_functions[] = {
	[cdrel_record_cdr] = "CSV_QUOTE CDR CALLERID CHANNEL",
	[cdrel_record_cel] = "CSV_QUOTE CALLERID CHANNEL eventtype eventtime eventenum userdeftype eventextra BRIDGEPEER",
};

static const char *special_vars[] = {
	[cdrel_record_cdr] = "",
	[cdrel_record_cel] = "eventtype eventtime eventenum userdeftype eventextra BRIDGEPEER",
};

/*!
 * \internal
 * \brief Parse a raw legacy field template.
 *
 * \example
 *
 * ${CSV_QUOTE(${eventtype})}
 * ${CSV_QUOTE(${CALLERID(name)})}
 * ${CSV_QUOTE(${CDR(src)})}
 * ${CDR(uservar)}
 * "some literal"
 * ${CSV_QUOTE("some literal")}
 *
 * \param record_type CDR or CEL
 * \param input_field_template The trimmed raw field template
 * \return
 */

static struct field_parse_result parse_field(enum cdrel_record_type record_type, char *input_field_template)
{
	char *tmp_field = NULL;
	struct field_parse_result result = { 0, };

	/*
	 * If the template starts with a double-quote, it's automatically
	 * a literal.
	 */
	if (input_field_template[0] == '"') {
		result.result = ast_strdup(ast_strip_quoted(ast_strdupa(input_field_template), "\"", "\""));
		result.csv_quote = 1;
		result.is_literal = 1;
		return result;
	}

	/*
	 * If it starts with a single quote, it's probably a legacy SQL template
	 * so we need to force quote it on output.
	 */
	tmp_field = ast_strip(ast_strdupa(input_field_template));

	if (tmp_field[0] == '\'') {
		result.csv_quote = 1;
	}

	/*
	 * I really hate the fact that ast_strip really trims whitespace
	 * and ast_strip_quoted will strip anything in pairs.
	 * Anyway, get rid of any remaining enclosing quotes.
	 */
	tmp_field = ast_strip(ast_strip_quoted(tmp_field, "\"'", "\"'"));

	/*
	 * If the template now starts with a '$' it's either a dialplan function
	 * call or one of the special CEL field names.
	 *
	 * Examples: ${CSV_QUOTE(${CALLERID(name)})}
	 *           ${eventtime}
	 * We're going to iterate over function removal until there's just
	 * a plain text string left.
	 *
	 */
	while (tmp_field[0] == '$') {
		char *ptr = NULL;
		/*
		 * A function name longer that 64 characters is highly unlikely but
		 * we'll check later.
		 */
		char func_name[65];

		/*
		 * Skip over the '$'
		 * {CSV_QUOTE(${CALLERID(name)})}
		 * {eventtime}
		 */
		tmp_field++;
		/*
		 * Remove any enclosing brace-like characters
		 * CSV_QUOTE(${CALLERID(name)})
		 * eventtime
		 */
		tmp_field = ast_strip(ast_strip_quoted(tmp_field, "[{(", "]})"));

		/*
		 * Check what's left to see if it matches a special variable.
		 * If it does (like "eventtime" in the example), we're done.
		 */
		if (strstr(special_vars[record_type], tmp_field) != NULL) {
			result.functions++;
			break;
		}

		/*
		 * At this point, it has to be a function name so find the
		 * openening '('.
		 * CSV_QUOTE(${CALLERID(name)})
		 *          ^
		 * If we don't find one, it's something we don't recognise
		 * so bail.
		 */
		ptr = strchr(tmp_field, '(');
		if (!ptr) {
			result.parse_failed++;
			continue;
		}

		/*
		 * Copy from the beginning to the '(' to func_name,
		 * not exceeding func_name's size.
		 *
		 * CSV_QUOTE(${CALLERID(name)})
		 *          ^
		 * CSV_QUOTE
		 *
		 * Then check that it's a function we can handle.
		 * If not, bail.
		 */
		ast_copy_string(func_name, tmp_field, MIN(sizeof(func_name), ptr - tmp_field + 1));
		if (strstr(allowed_functions[record_type], func_name) == NULL) {
			result.parse_failed++;
			result.unknown_functions++;
			continue;
		}
		result.functions++;
		/*
		 * If the function is CSV_QUOTE, we need to set the csv_quote flag.
		 */
		if (strcmp("CSV_QUOTE", func_name) == 0) {
			result.csv_quote = 1;
		} else if (strcmp("CDR", func_name) == 0) {
			result.cdr = 1;
		}

		/*
		 * ptr still points to the opening '(' so now strip it and the
		 * matching parens.
		 *
		 * ${CALLERID(name)}
		 *
		 */
		tmp_field = ast_strip_quoted(ptr, "(", ")");
		if (tmp_field[0] == '"' || tmp_field[0] == '\'') {
			result.result = ast_strdup(ast_strip_quoted(tmp_field, "\"'", "\"'"));
			result.csv_quote = 1;
			result.is_literal = 1;
			return result;
		}

		/* Repeat the loop until there are no more functions or variables */
	}

	/*
	 * If the parse failed we'll send back the entire template.
	 */
	if (result.parse_failed) {
		tmp_field = input_field_template;
	} else {
		/*
		 * If there were no functions or variables parsed then we'll
		 * assume it's a literal.
		 */
		if (result.functions == 0) {
			result.is_literal = 1;
		}
	}

	result.result = ast_strdup(tmp_field);
	if (result.result == NULL) {
		result.parse_failed = 1;
	}

	return result;
}

/*!
 * \internal
 * \brief Parse a legacy DSV template string into a vector of individual strings.
 *
 * The resulting vector will look like it came from an advanced config and will
 * be treated as such.
 *
 * \param record_type CDR or CEL.
 * \param config_filename Config filename for logging purposes.
 * \param output_filename Output filename for logging purposes.
 * \param template The full template.
 * \param fields A pointer to a string vector to receive the result.
 * \retval 1 on success.
 * \retval 0 on failure.
 */
static int parse_legacy_template(enum cdrel_record_type record_type, const char *config_filename,
	const char *output_filename, const char *input_template, struct ast_vector_string *fields)
{
	char *template = ast_strdupa(input_template);
	char *field_template = NULL;
	int res = 0;

	/*
	 * We have no choice but to assume that a legacy config template uses commas
	 * as field delimiters.  We don't have a reliable way to determine this ourselves.
	 */
	while((field_template = ast_strsep(&template, ',', AST_STRSEP_TRIM))) {
		char *uservar = "";
		char *literal = "";
		/* Try to parse the field. */
		struct field_parse_result result = parse_field(record_type, field_template);

		ast_debug(2, "field: '%s' literal: %d quote: %d cdr: %d failed: %d funcs: %d unknfuncs: %d\n", result.result,
			result.is_literal, result.csv_quote, result.cdr,
			result.parse_failed, result.functions, result.unknown_functions);

		/*
		 * If it failed,
		 */
		if (!result.result || result.parse_failed) {
			ast_free(result.result);
			return 0;
		}
		if (result.is_literal) {
			literal = "literal^";
		}

		if (!get_registered_field_by_name(record_type, result.result)) {
			ast_debug(3, "   %s->%s: field '%s' not found\n", cdrel_basename(config_filename),
				cdrel_basename(output_filename), result.result);
			/*
			 * If the result was found in a CDR function, treat it as a CDR user variable
			 * otherwise treat it as a literal.
			 */
			if (result.cdr) {
				uservar = "uservar^";
			} else {
				literal = "literal^";
			}
		}
		res = ast_asprintf(&field_template, "%s(%s%s)", result.result, S_OR(literal,uservar), result.csv_quote ? "quote" : "noquote");
		ast_free(result.result);

		if (!field_template || res < 0) {
			ast_free(field_template);
			return 0;
		}
		res = AST_VECTOR_APPEND(fields, field_template);
		if (res != 0) {
			ast_free(field_template);
			return 0;
		}
		ast_debug(2, "   field template: %s\n", field_template);
	}

	return 1;
}

/*!
 * \fn Parse an advanced field template and allocate a cdrel_field for it.
 * \brief
 *
 * \param config Config object.
 * \param input_field_template Trimmed advanced field template.
 * \return
 */
static struct cdrel_field *field_alloc(struct cdrel_config *config, const char *input_field_template)
{
	RAII_VAR(struct cdrel_field *, field, NULL, field_free);
	const struct cdrel_field *registered_field = NULL;
	struct cdrel_field *rtn_field = NULL;
	char *field_name = NULL;
	char *data = NULL;
	char *tmp_data = NULL;
	char *closeparen = NULL;
	char *qualifier = NULL;
	enum cdrel_data_type forced_output_data_type = cdrel_data_type_end;
	struct ast_flags field_flags = { 0 };

	/*
	 * The database fields are specified field-by-field for legacy so we treat them
	 * as literals containing expressions which will be evaluated record-by-record.
	 */
	if (config->backend_type == cdrel_backend_db && config->config_type == cdrel_config_legacy) {
		registered_field = get_registered_field_by_name(config->record_type, "literal");
		ast_assert(registered_field != NULL);
		rtn_field = ast_calloc(1, sizeof(*field) + strlen(input_field_template) + 1);
		if (!rtn_field) {
			return NULL;
		}
		memcpy(rtn_field, registered_field, sizeof(*registered_field));
		strcpy(rtn_field->data, input_field_template); /* Safe */
		return rtn_field;
	}

	/*
	 * If the field template is a quoted string, it's a literal.
	 * We don't check for qualifiers.
	 */
	if (input_field_template[0] == '"' || input_field_template[0] == '\'') {
		data = ast_strip_quoted(ast_strdupa(input_field_template), "\"'", "\"'");
		ast_set_flag(&field_flags, cdrel_flag_literal);
		ast_debug(3, "   Using qualifier 'literal' for field '%s' flags: %s\n", data,
			ast_str_tmp(128, cdrel_get_field_flags(&field_flags, &STR_TMP)));
		field_name = "literal";
	} else {
		field_name = ast_strdupa(input_field_template);
		data = strchr(field_name, '(');

		if (data) {
			*data = '\0';
			data++;
			closeparen = strchr(data, ')');
			if (closeparen) {
				*closeparen = '\0';
			}
		}
	}

	if (!ast_strlen_zero(data) && !ast_test_flag(&field_flags, cdrel_flag_literal)) {
		char *data_swap = NULL;
		tmp_data = ast_strdupa(data);

		while((qualifier = ast_strsep(&tmp_data, '^', AST_STRSEP_STRIP | AST_STRSEP_TRIM))) {
			enum cdrel_data_type fodt = cdrel_data_type_end;
			if (ast_strlen_zero(qualifier)) {
				continue;
			}
			fodt = cdrel_data_type_from_str(qualifier);
			if (fodt < cdrel_data_type_end) {
				ast_set_flag(&field_flags, cdrel_flag_type_forced);
				if (fodt == cdrel_type_uservar) {
					ast_set_flag(&field_flags, cdrel_flag_uservar);
					ast_debug(3, "   Using qualifier '%s' for field '%s' flags: %s\n", qualifier,
						field_name, ast_str_tmp(128, cdrel_get_field_flags(&field_flags, &STR_TMP)));
					data_swap = ast_strdupa(field_name);
					field_name = "uservar";
				} else if (fodt == cdrel_type_literal) {
					ast_set_flag(&field_flags, cdrel_flag_literal);
					ast_debug(3, "   Using qualifier '%s' for field '%s' flags: %s\n", qualifier,
						field_name, ast_str_tmp(128, cdrel_get_field_flags(&field_flags, &STR_TMP)));
					data_swap = ast_strdupa(field_name);
					field_name = "literal";
				} else {
					forced_output_data_type = fodt;
					ast_debug(3, "   Using qualifier '%s' for field '%s' flags: %s\n", qualifier,
						field_name, ast_str_tmp(128, cdrel_get_field_flags(&field_flags, &STR_TMP)));
				}
				continue;
			}
			if (strcasecmp(qualifier, "quote") == 0) {
				ast_set_flag(&field_flags, cdrel_flag_quote);
				ast_debug(3, "   Using qualifier '%s' for field '%s' flags: %s\n", qualifier,
					field_name, ast_str_tmp(128, cdrel_get_field_flags(&field_flags, &STR_TMP)));
				continue;
			}
			if (strcasecmp(qualifier, "noquote") == 0) {
				ast_set_flag(&field_flags, cdrel_flag_noquote);
				ast_debug(3, "   Using qualifier '%s' for field '%s' flags: %s\n", qualifier,
					field_name, ast_str_tmp(128, cdrel_get_field_flags(&field_flags, &STR_TMP)));
				continue;
			}
			if (strchr(qualifier, '%') != NULL) {
				data_swap = ast_strdupa(qualifier);
				ast_set_flag(&field_flags, cdrel_flag_format_spec);
				ast_debug(3, "   Using qualifier '%s' for field '%s' flags: %s\n", qualifier,
					field_name, ast_str_tmp(128, cdrel_get_field_flags(&field_flags, &STR_TMP)));
			}
		}
		if (ast_test_flag(&field_flags, cdrel_flag_quote) && ast_test_flag(&field_flags, cdrel_flag_noquote)) {
			ast_log(LOG_WARNING, "%s->%s: Field '%s(%s)' has both quote and noquote\n",
				config->config_filename, config->output_filename, field_name, data);
			return NULL;
		}
		data = data_swap;
	}

	/*
	 * Check again for literal.
	 */
	if (ast_test_flag(&field_flags, cdrel_flag_literal)) {
		if (config->format_type == cdrel_format_json && !strchr(data, ':')) {
			ast_log(LOG_WARNING, "%s->%s: Literal field '%s' must be formatted as \"name: value\" when using the 'json' format\n",
				cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
				input_field_template);
			return NULL;
		}
	}

	/*
	 * Now look the field up by just the field name without any data.
	 */
	registered_field = get_registered_field_by_name(config->record_type, field_name);
	if (!registered_field) {
		ast_log(LOG_WARNING, "%s->%s: Field '%s' not found\n",
			cdrel_basename(config->config_filename), cdrel_basename(config->output_filename), field_name);
		return NULL;
	}

	field = ast_calloc(1, sizeof(*registered_field) + strlen(input_field_template) + 1);
	if (!field) {
		return NULL;
	}
	memcpy(field, registered_field, sizeof(*field));

	if (!ast_strlen_zero(data)) {
		strcpy(field->data, data); /* Safe */
	}

	/*
	 * For user variables, we use the field name from the data
	 * we set above.
	 */
	if (field->input_data_type == cdrel_type_uservar) {
		field->name = field->data;
	}

	if (field->input_data_type == cdrel_type_literal && config->format_type == cdrel_format_json) {
		/*
		 * data should look something like this...  lname: lvalue
		 * We'll need to make field->name point to "lname" and
		 * field->data point to "lvalue" so that when output the
		 * json will look like...  "lname": "lvalue".
		 * Since field->data is already long enough to to handle both,
		 * we'll do this...
		 * field->data = lvalue\0lname\0
		 * field->name =         ^
		 */
		char *ptr = strchr(data, ':');/* Safe since we checked data for a ':' above */
		*ptr = '\0';
		ptr++;
		/*
		 * data: lname\0 lvalue
		 * ptr:         ^
		 */
		strcpy(field->data, ast_strip_quoted(ptr, "\"", "\"")); /* Safe */
		/*
		 * field->data: lvalue\0
		 */
		ptr = field->data + strlen(field->data);
		ptr++;
		/*
		 * field->data: lvalue\0
		 * ptr:                 ^
		 * data: lname\0 lvalue
		 */
		strcpy(ptr, data); /* Safe */
		/*
		 * field->data: lvalue\0lname\0
		 */
		field->name = ptr;
		/*
		 * field->data: lvalue\0lname\0
		 * field->name:         ^
		 */
	}

	if (forced_output_data_type < cdrel_data_type_end) {
		field->output_data_type = forced_output_data_type;
	}
	field->flags = field_flags;

	/*
	 * Unless the field has the 'noquote' flag, we'll set the 'quote'
	 * flag if quoting method is 'all' or 'non_numeric'.
	 */
	if (!ast_test_flag(&field->flags, cdrel_flag_noquote)) {
		if (config->quoting_method == cdrel_quoting_method_all) {
			ast_set_flag(&field->flags, cdrel_flag_quote);
		} else if (config->quoting_method == cdrel_quoting_method_non_numeric) {
			if (field->output_data_type > cdrel_data_type_strings_end) {
				ast_set_flag(&field->flags, cdrel_flag_noquote);
			} else {
				ast_set_flag(&field->flags, cdrel_flag_quote);
			}
		}
	}

	if (config->quoting_method == cdrel_quoting_method_none) {
		ast_clear_flag(&field->flags, cdrel_flag_quote);
		ast_set_flag(&field->flags, cdrel_flag_noquote);
	}

	ast_debug(2, "%s->%s: Field '%s' processed -> name:'%s' input_type:%s output_type:%s flags:'%s' data:'%s'\n",
		cdrel_basename(config->config_filename), cdrel_basename(config->output_filename), input_field_template,
		field->name, DATA_TYPE_STR(field->input_data_type),
		DATA_TYPE_STR(field->output_data_type),
		ast_str_tmp(128, cdrel_get_field_flags(&field->flags, &STR_TMP)),
		field->data);

	rtn_field = field;
	field = NULL;
	return rtn_field;
}

static void field_template_vector_free(struct ast_vector_string *fields) {
	AST_VECTOR_RESET(fields, ast_free);
	AST_VECTOR_PTR_FREE(fields);
}

/*!
 * \internal
 * \brief Load all the fields in the string vector.
 *
 * \param config Config object
 * \param fields String vector.
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int load_fields(struct cdrel_config *config, struct ast_vector_string *fields)
{
	int res = 0;
	int ix = 0;

	ast_debug(1, "%s->%s: Loading fields\n", cdrel_basename(config->config_filename),
		cdrel_basename(config->output_filename));

	for (ix = 0; ix < AST_VECTOR_SIZE(fields); ix++) {
		char *field_name = AST_VECTOR_GET(fields, ix);
		struct cdrel_field *field = NULL;

		field = field_alloc(config, field_name);
		if (!field) {
			res = -1;
			continue;
		}

		if (AST_VECTOR_APPEND(&config->fields, field) != 0) {
			field_free(field);
			return -1;
		}
	}

	return res;
}

static void config_free(struct cdrel_config *config)
{
	if (!config) {
		return;
	}

	if (config->insert) {
		sqlite3_finalize(config->insert);
		config->insert = NULL;
	}

	if (config->db) {
		sqlite3_close(config->db);
		config->db = NULL;
	}

	ast_mutex_destroy(&config->lock);
	ast_string_field_free_memory(config);
	AST_VECTOR_RESET(&config->fields, field_free);
	AST_VECTOR_FREE(&config->fields);
	ast_free(config);
}

/*!
 * \internal
 * \brief Allocate a config object.
 *
 * You should know what these are by now :)
 *
 * \param record_type
 * \param backend_type
 * \param config_type
 * \param config_filename
 * \param output_filename
 * \param template
 * \return
 */
static struct cdrel_config *config_alloc(enum cdrel_record_type record_type,
	enum cdrel_backend_type backend_type, enum cdrel_config_type config_type,
	const char *config_filename, const char *output_filename, const char *template)
{
	RAII_VAR(struct cdrel_config *, config, NULL, config_free);
	struct cdrel_config *rtn_config = NULL;
	const char *file_suffix = "";
	int res = 0;

	ast_debug(1, "%s->%s: Loading\n", cdrel_basename(config_filename), cdrel_basename(output_filename));

	config = ast_calloc_with_stringfields(1, struct cdrel_config, 1024);
	if (!config) {
		return NULL;
	}

	if (ast_string_field_set(config, config_filename, config_filename) != 0) {
		return NULL;
	}

	config->record_type = record_type;
	config->backend_type = backend_type;
	config->dummy_channel_alloc = cdrel_dummy_channel_allocators[record_type];
	config->config_type = config_type;

	/* Set defaults */
	config->format_type = cdrel_format_dsv;
	config->separator[0] = ',';
	switch(backend_type) {
	case cdrel_backend_text:
		config->quote[0] = '"';
		config->quoting_method = cdrel_quoting_method_all;
		break;
	case cdrel_backend_db:
		config->quote[0] = '\0';
		config->format_type = cdrel_format_sql;
		config->quoting_method = cdrel_quoting_method_none;
		if (!ast_ends_with(output_filename, ".db")) {
			file_suffix = ".db";
		}
		break;
	default:
		ast_log(LOG_ERROR, "%s->%s: Unknown backend type '%d'\n", cdrel_basename(config_filename),
			cdrel_basename(output_filename), backend_type);
		break;
	}
	config->quote_escape[0] = config->quote[0];

	res = ast_string_field_set(config, template, template);
	if (res != 0) {
		return NULL;
	}

	if (output_filename[0] == '/') {
		res = ast_string_field_build(config, output_filename, "%s%s", output_filename, file_suffix);
	} else {
		const char *subdir = dirname_map[backend_type][record_type];
		res = ast_string_field_build(config, output_filename, "%s/%s%s%s%s",
			ast_config_AST_LOG_DIR, S_OR(subdir, ""), ast_strlen_zero(subdir) ? "" : "/", output_filename, file_suffix);
	}
	if (res != 0) {
		return NULL;
	}
	ast_mutex_init(&config->lock);

	rtn_config = config;
	config = NULL;
	return rtn_config;
}

/*!
 * \internal
 * \brief Load the "columns" parameter from a database config.
 *
 * \param config Config object
 * \param columns The columns parameter.
 * \param column_count Set to the count of columns parsed.
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int load_database_columns(struct cdrel_config *config, const char *columns, int *column_count)
{
	char *col = NULL;
	char *cols = NULL;
	RAII_VAR(struct ast_str *, column_string, NULL, ast_free);

	ast_debug(1, "%s->%s: Loading columns\n", cdrel_basename(config->config_filename),
		cdrel_basename(config->output_filename));

	if (!(column_string = ast_str_create(1024))) {
		return -1;
	}

	cols = ast_strdupa(columns);
	*column_count = 0;

	/* We need to trim and remove any single or double quotes from each column name. */
	while((col = ast_strsep(&cols, ',', AST_STRSEP_TRIM))) {
		col = ast_strip(ast_strip_quoted(col, "'\"", "'\""));
		if (ast_str_append(&column_string, 0, "%s%s", *column_count ? "," : "", col) <= 0) {
			return -1;
		}
		(*column_count)++;
	}

	if (ast_string_field_set(config, db_columns, ast_str_buffer(column_string)) != 0) {
		return -1;
	}

	return 0;
}

static char *make_stmt_placeholders(int columns)
{
	char *placeholders = ast_malloc(2 * columns), *c = placeholders;
	if (placeholders) {
		for (; columns; columns--) {
			*c++ = '?';
			*c++ = ',';
		}
		*(c - 1) = 0;
	}
	return placeholders;
}

/*!
 * \internal
 * \brief Open an sqlite3 database and create the table if needed.
 *
 * \param config Config object.
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int open_database(struct cdrel_config *config)
{
	char *sql = NULL;
	int res = 0;
	char *placeholders = NULL;

	ast_debug(1, "%s->%s: opening database\n", cdrel_basename(config->config_filename),
		cdrel_basename(config->output_filename));
	res = sqlite3_open(config->output_filename, &config->db);
	if (res != SQLITE_OK) {
		ast_log(LOG_WARNING, "%s->%s: Could not open database\n", cdrel_basename(config->config_filename),
			cdrel_basename(config->output_filename));
		return -1;
	}

	sqlite3_busy_timeout(config->db, config->busy_timeout);

	/* is the table there? */
	sql = sqlite3_mprintf("SELECT COUNT(*) FROM %q;", config->db_table);
	if (!sql) {
		return -1;
	}
	res = sqlite3_exec(config->db, sql, NULL, NULL, NULL);
	sqlite3_free(sql);
	if (res != SQLITE_OK) {
		/*
		 * Create the table.
		 * We don't use %q for the column list here since we already escaped when building it
		 */
		sql = sqlite3_mprintf("CREATE TABLE %q (AcctId INTEGER PRIMARY KEY, %s)",
			config->db_table, config->db_columns);
		res = sqlite3_exec(config->db, sql, NULL, NULL, NULL);
		sqlite3_free(sql);
		if (res != SQLITE_OK) {
			ast_log(LOG_WARNING, "%s->%s: Unable to create table '%s': %s\n",
				cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
				config->db_table, sqlite3_errmsg(config->db));
			return -1;
		}
	} else {
		/*
		 * If the table exists, make sure the number of columns
		 * matches the config.
		 */
		sqlite3_stmt *get_stmt;
		int existing_columns = 0;
		int config_columns = AST_VECTOR_SIZE(&config->fields);

		sql = sqlite3_mprintf("SELECT * FROM %q;", config->db_table);
		if (!sql) {
			return -1;
		}
		res = sqlite3_prepare_v2(config->db, sql, -1, &get_stmt, NULL);
		sqlite3_free(sql);
		if (res != SQLITE_OK) {
			ast_log(LOG_WARNING, "%s->%s: Unable to get column count for table '%s': %s\n",
				cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
				config->db_table, sqlite3_errmsg(config->db));
			return -1;
		}
		/*
		 * prepare figures out the number of columns that would be in a result
		 * set. We don't need to execute the statement.
		 */
		existing_columns = sqlite3_column_count(get_stmt);
		sqlite3_finalize(get_stmt);
		/* config_columns doesn't include the sequence field */
		if ((config_columns + 1) != existing_columns) {
			ast_log(LOG_WARNING, "%s->%s: The number of fields in the config (%d) doesn't equal the"
				" nummber of data columns (%d) in the existing %s table. This config is disabled.\n",
				cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
				config_columns, existing_columns - 1, config->db_table);
			return -1;
		}
	}

	placeholders = make_stmt_placeholders(AST_VECTOR_SIZE(&config->fields));
	if (!placeholders) {
		return -1;
	}

	/* Inserting NULL in the ID column still generates an ID */
	sql = sqlite3_mprintf("INSERT INTO %q VALUES (NULL,%s)", config->db_table, placeholders);
	ast_free(placeholders);
	if (!sql) {
		return -1;
	}

	res = sqlite3_prepare_v3(config->db, sql, -1, SQLITE_PREPARE_PERSISTENT, &config->insert, NULL);
	if (res != SQLITE_OK) {
		ast_log(LOG_ERROR, "%s->%s: Unable to prepare INSERT statement '%s': %s\n",
			cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
			sql, sqlite3_errmsg(config->db));
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Load a database config from a config file category.
 *
 * \param record_type CDR or CEL.
 * \param category The category (becomes the database file name).
 * \param config_filename The config filename for logging purposes.
 * \return config or NULL.
 */
static struct cdrel_config *load_database_config(enum cdrel_record_type record_type,
	struct ast_category *category, const char *config_filename)
{
	const char *category_name = ast_category_get_name(category);
	RAII_VAR(struct cdrel_config *, config, NULL, config_free);
	struct cdrel_config *rtn_config = NULL;
	int res = 0;
	int column_count = 0;
	int value_count = 0;
	int field_check_passed = 0;
	const char *template = ast_variable_find(category, "values");
	enum cdrel_config_type config_type;
	const char *value;
	char *tmp_fields = NULL;
	RAII_VAR(struct ast_vector_string *, field_templates, ast_calloc(1, sizeof(*field_templates)), field_template_vector_free);

	if (!ast_strlen_zero(template)) {
		config_type = cdrel_config_legacy;
	} else {
		template = ast_variable_find(category, "fields");
		if (!ast_strlen_zero(template)) {
			config_type = cdrel_config_advanced;
		}
	}
	if (ast_strlen_zero(template)) {
		ast_log(LOG_WARNING, "%s->%s: Neither 'values' nor 'fields' specified\n",
			cdrel_basename(config_filename), cdrel_basename(category_name));
		return NULL;
	}

	res = AST_VECTOR_INIT(field_templates, 25);
	if (res != 0) {
		return NULL;
	}

	/*
	 * Let's try and and parse a legacy config to see if we can turn
	 * it into an advanced condig.
	 */
	if (config_type == cdrel_config_legacy) {
		field_check_passed = parse_legacy_template(record_type, config_filename,
			category_name, template, field_templates);
		if (field_check_passed) {
			config_type = cdrel_config_advanced;
			ast_log(LOG_NOTICE, "%s->%s: Legacy config upgraded to advanced\n",
				cdrel_basename(config_filename), cdrel_basename(category_name));
		} else {
			AST_VECTOR_RESET(field_templates, ast_free);
			ast_log(LOG_NOTICE, "%s->%s: Unable to upgrade legacy config to advanced. Continuing to process as legacy\n",
				cdrel_basename(config_filename), cdrel_basename(category_name));
		}
	}

	/*
	 * If we could, the fields vector will be populated so we don't need to do it again.
	 * If it was an advanced config or a legacy one we couldn't parse,
	 * we need to split the string into the vector.
	 */
	if (AST_VECTOR_SIZE(field_templates) == 0) {
		tmp_fields = ast_strdupa(template);
		while((value = ast_strsep(&tmp_fields, ',', AST_STRSEP_TRIM))) {
			res = AST_VECTOR_APPEND(field_templates, ast_strdup(value));
			if (res != 0) {
				return NULL;
			}
		}
	}

	config = config_alloc(record_type, cdrel_backend_db, config_type,
		config_filename, category_name, template);
	if (!config) {
		return NULL;
	}

	config->busy_timeout = 1000;

	res = ast_string_field_set(config, db_table,
		S_OR(ast_variable_find(category, "table"), config->record_type == cdrel_record_cdr ? "cdr" : "cel"));
	if (res != 0) {
		return NULL;
	}

	/* sqlite3_busy_timeout in miliseconds */
	if ((value = ast_variable_find(category, "busy_timeout")) != NULL) {
		if (ast_parse_arg(value, PARSE_INT32|PARSE_DEFAULT, &config->busy_timeout, 1000) != 0) {
			ast_log(LOG_WARNING, "%s->%s: Invalid busy_timeout value '%s' specified. Using 1000 instead.\n",
				cdrel_basename(config->config_filename), cdrel_basename(config->output_filename), value);
		}
	}

	/* Columns */
	value = ast_variable_find(category, "columns");
	if (ast_strlen_zero(value)) {
		ast_log(LOG_WARNING, "%s->%s: The 'columns' parameter is missing",
			cdrel_basename(config->config_filename), cdrel_basename(config->output_filename));
		return NULL;
	}

	if (load_database_columns(config, value, &column_count) != 0) {
		return NULL;
	}

	if (AST_VECTOR_INIT(&config->fields, AST_VECTOR_SIZE(field_templates)) != 0) {
		return NULL;
	}

	if (load_fields(config, field_templates) != 0) {
		return NULL;
	}

	value_count = AST_VECTOR_SIZE(&config->fields);

	if (value_count != column_count) {
		ast_log(LOG_WARNING, "%s->%s: There are %d columns but %d values\n",
			cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
			column_count, value_count);
		return NULL;
	}

	res = open_database(config);
	if (res != 0) {
		return NULL;
	}

	ast_log(LOG_NOTICE, "%s->%s: Logging %s records to table '%s'\n",
		cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
		RECORD_TYPE_STR(config->record_type),
		config->db_table);

	rtn_config = config;
	config = NULL;
	return rtn_config;
}

/*!
 * \internal
 * \brief Load all the categories in a database config file.
 *
 * \param record_type
 * \param configs
 * \param config_filename
 * \param reload
 * \retval 0 on success or reload not needed.
 * \retval -1 on failure.
 */
static int load_database_config_file(enum cdrel_record_type record_type, struct cdrel_configs *configs,
	const char *config_filename, int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_category *category = NULL;

	ast_debug(1, "%s: %soading\n", config_filename, reload ? "Rel" : "L");
	cfg = ast_config_load(config_filename, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load %s. Not logging %ss to custom database\n",
			config_filename, RECORD_TYPE_STR(record_type));
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(1, "%s: Config file unchanged, not reloading\n", config_filename);
		return 0;
	}

	while ((category = ast_category_browse_filtered(cfg, NULL, category, NULL))) {
		struct cdrel_config *config = NULL;

		config = load_database_config(record_type, category, config_filename);
		if (!config) {
			continue;
		}

		if (AST_VECTOR_APPEND(configs, config) != 0) {
			config_free(config);
			break;
		}
	}

	ast_config_destroy(cfg);

	ast_log(LOG_NOTICE, "%s: Loaded %d configs\n", config_filename, (int)AST_VECTOR_SIZE(configs));

	/* Only fail if no configs were valid. */
	return AST_VECTOR_SIZE(configs) > 0 ? 0 : -1;
}

/*!
 * \internal
 * \brief Load a legacy config from a single entry in the "mappings" castegory.
 *
 * \param record_type
 * \param config_filename
 * \param output_filename
 * \param template
 * \return config or NULL.
 */
static struct cdrel_config *load_text_file_legacy_config(enum cdrel_record_type record_type,
	const char *config_filename, const char *output_filename, const char *template)
{
	struct cdrel_config *config = NULL;
	int field_check_passed = 0;
	int res = 0;
	RAII_VAR(struct ast_vector_string *, fields, ast_calloc(1, sizeof(*fields)), field_template_vector_free);

	res = AST_VECTOR_INIT(fields, 25);
	if (res != 0) {
		return NULL;
	}

	/*
	 * Let's try and and parse a legacy config to see if we can turn
	 * it into an advanced condig.
	 */
	field_check_passed = parse_legacy_template(record_type, config_filename,
		output_filename, template, fields);

	/*
	 * If we couldn't, treat as legacy.
	 */
	if (!field_check_passed) {
		config = config_alloc(record_type, cdrel_backend_text, cdrel_config_legacy,
			config_filename, output_filename, template);
		ast_log(LOG_NOTICE, "%s->%s: Logging legacy %s records\n",
			cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
			RECORD_TYPE_STR(config->record_type));
		return config;
	}

	config = config_alloc(record_type, cdrel_backend_text, cdrel_config_advanced,
		config_filename, output_filename, template);
	if (!config) {
		return NULL;
	}
	config->format_type = cdrel_format_dsv;
	config->quote[0] = '"';
	config->quote_escape[0] = '"';
	config->separator[0] = ',';
	config->quoting_method = cdrel_quoting_method_all;

	if (AST_VECTOR_INIT(&config->fields, AST_VECTOR_SIZE(fields)) != 0) {
		return NULL;
	}

	if (load_fields(config, fields) != 0) {
		return NULL;
	}

	ast_log(LOG_NOTICE, "%s->%s: Logging %s records\n",
		cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
		RECORD_TYPE_STR(config->record_type));

	return config;
}

/*!
 * \internal
 * \brief Load an advanced config from a config file category.
 *
 * \param record_type
 * \param category
 * \param config_filename
 * \return config or NULL.
 */
static struct cdrel_config *load_text_file_advanced_config(enum cdrel_record_type record_type,
	struct ast_category *category, const char *config_filename)
{
	const char *category_name = ast_category_get_name(category);
	RAII_VAR(struct cdrel_config *, config, NULL, config_free);
	struct cdrel_config *rtn_config = NULL;
	const char *value;
	int res = 0;
	const char *fields_value = ast_variable_find(category, "fields");
	char *tmp_fields = NULL;
	RAII_VAR(struct ast_vector_string *, fields, ast_calloc(1, sizeof(*fields)), field_template_vector_free);

	if (ast_strlen_zero(fields_value)) {
		ast_log(LOG_WARNING, "%s->%s: Missing 'fields' parameter\n",
			cdrel_basename(config_filename), category_name);
		return NULL;
	}

	config = config_alloc(record_type, cdrel_backend_text, cdrel_config_advanced,
		config_filename, category_name, fields_value);

	value = ast_variable_find(category, "format");
	if (!ast_strlen_zero(value)) {
		if (ast_strings_equal(value, "json")) {
			config->format_type = cdrel_format_json;
			config->separator[0] = ',';
			config->quote[0] = '"';
			config->quote_escape[0] = '\\';
			config->quoting_method = cdrel_quoting_method_non_numeric;
		} else if (ast_strings_equal(value, "dsv")) {
			config->format_type = cdrel_format_dsv;
		} else {
			ast_log(LOG_WARNING, "%s->%s: Invalid format '%s'\n",
				cdrel_basename(config->config_filename), cdrel_basename(config->output_filename), value);
			return NULL;
		}
	}

	if (config->format_type != cdrel_format_json) {
		value = ast_variable_find(category, "separator_character");
		if (!ast_strlen_zero(value)) {
			ast_copy_string(config->separator, ast_unescape_c(ast_strdupa(value)), 2);
		}

		value = ast_variable_find(category, "quote_character");
		if (!ast_strlen_zero(value)) {
			ast_copy_string(config->quote, value, 2);
		}

		value = ast_variable_find(category, "quote_escape_character");
		if (!ast_strlen_zero(value)) {
			ast_copy_string(config->quote_escape, value, 2);
		}

		value = ast_variable_find(category, "quoting_method");
		if (!ast_strlen_zero(value)) {
			if (ast_strings_equal(value, "all")) {
				config->quoting_method = cdrel_quoting_method_all;
			} else if (ast_strings_equal(value, "minimal")) {
				config->quoting_method = cdrel_quoting_method_minimal;
			} else if (ast_strings_equal(value, "non_numeric")) {
				config->quoting_method = cdrel_quoting_method_non_numeric;
			} else if (ast_strings_equal(value, "none")) {
				config->quoting_method = cdrel_quoting_method_none;
			} else {
				ast_log(LOG_WARNING, "%s->%s: Invalid quoting method '%s'\n",
					cdrel_basename(config->config_filename), cdrel_basename(config->output_filename), value);
				return NULL;
			}
		}
	}

	res = AST_VECTOR_INIT(fields, 20);
	if (res != 0) {
		return NULL;
	}
	tmp_fields = ast_strdupa(fields_value);
	while((value = ast_strsep(&tmp_fields, ',', AST_STRSEP_TRIM))) {
		res = AST_VECTOR_APPEND(fields, ast_strdup(value));
		if (res != 0) {
			return NULL;
		}
	}

	if (AST_VECTOR_INIT(&config->fields, AST_VECTOR_SIZE(fields)) != 0) {
		return NULL;
	}

	if (load_fields(config, fields) != 0) {
		return NULL;
	}

	ast_log(LOG_NOTICE, "%s->%s: Logging %s records\n",
		cdrel_basename(config->config_filename), cdrel_basename(config->output_filename),
		RECORD_TYPE_STR(config->record_type));

	rtn_config = config;
	config = NULL;

	return rtn_config;
}

/*!
 * \internal
 * \brief Load a legacy configs from the "mappings" category.
 *
 * \param record_type
 * \param configs
 * \param category
 * \param config_filename
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int load_text_file_legacy_mappings(enum cdrel_record_type record_type,
	struct cdrel_configs *configs, struct ast_category *category,
	const char *config_filename)
{
	struct ast_variable *var = NULL;

	for (var = ast_category_first(category); var; var = var->next) {
		struct cdrel_config *config = NULL;

		if (ast_strlen_zero(var->name) || ast_strlen_zero(var->value)) {
			ast_log(LOG_WARNING, "%s: %s mapping must have both a filename and a template at line %d\n",
				cdrel_basename(config_filename), RECORD_TYPE_STR(config->record_type), var->lineno);
			continue;
		}

		config = load_text_file_legacy_config(record_type, config_filename, var->name, var->value);
		if (!config) {
			continue;
		}

		if (AST_VECTOR_APPEND(configs, config) != 0) {
			config_free(config);
			return -1;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Load all text file backend configs from a config file.
 *
 * \param record_type
 * \param configs
 * \param config_filename
 * \param reload
 * \return
 */
static int load_text_file_config_file(enum cdrel_record_type record_type,
	struct cdrel_configs *configs, const char *config_filename, int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_category *category = NULL;

	ast_debug(1, "%s: %soading\n", config_filename, reload ? "Rel" : "L");
	cfg = ast_config_load(config_filename, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load %s. Not logging %ss to custom files\n",
			config_filename, RECORD_TYPE_STR(record_type));
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(1, "%s: Config file unchanged, not reloading\n", config_filename);
		return 0;
	}

	while ((category = ast_category_browse_filtered(cfg, NULL, category, NULL))) {
		const char *category_name = ast_category_get_name(category);

		if (ast_strings_equal(category_name, "mappings")) {
			load_text_file_legacy_mappings(record_type, configs, category, config_filename);
		} else {
			struct cdrel_config * config = load_text_file_advanced_config(record_type, category,
				config_filename);
			if (!config) {
				continue;
			}
			if (AST_VECTOR_APPEND(configs, config) != 0) {
				config_free(config);
				return -1;
			}
		}
	}

	ast_config_destroy(cfg);

	ast_log(LOG_NOTICE, "%s: Loaded %d configs\n", config_filename, (int)AST_VECTOR_SIZE(configs));

	/* Only fail if no configs were valid. */
	return AST_VECTOR_SIZE(configs) > 0 ? 0 : -1;
}

static int register_backend(enum cdrel_record_type record_type, const char *backend_name, void *log_cb)
{
	switch(record_type) {
	case cdrel_record_cdr:
		return 	ast_cdr_register(backend_name, "", log_cb);
	case cdrel_backend_db:
		return ast_cel_backend_register(backend_name, log_cb);
	default:
		return -1;
	}
}

static int unregister_backend(enum cdrel_record_type record_type, const char *backend_name)
{
	switch(record_type) {
	case cdrel_record_cdr:
		return ast_cdr_unregister(backend_name);
	case cdrel_record_cel:
		return ast_cel_backend_unregister(backend_name);
	default:
		return -1;
	}
}

static int load_config_file(enum cdrel_backend_type output_type, enum cdrel_record_type record_type,
	struct cdrel_configs *configs, const char *filename, int reload)
{
	switch(output_type) {
	case cdrel_backend_text:
		return load_text_file_config_file(record_type, configs, filename, reload);
	case cdrel_backend_db:
		return load_database_config_file(record_type, configs, filename, reload);
	default:
		return -1;
	}
}

int cdrel_reload_module(enum cdrel_backend_type output_type, enum cdrel_record_type record_type,
	struct cdrel_configs **configs, const char *filename)
{
	int res = 0;
	struct cdrel_configs *old_configs = *configs;
	struct cdrel_configs *new_configs = NULL;

	/*
	 * Save new config to a temporary vector to make sure the
	 * configs are valid before swapping them in.
	 */
	new_configs = ast_malloc(sizeof(*new_configs));
	if (!new_configs) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (AST_VECTOR_INIT(new_configs, AST_VECTOR_SIZE(old_configs)) != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	res = load_config_file(output_type, record_type, new_configs, filename, 1);
	if (res != 0) {
		AST_VECTOR_RESET(new_configs, config_free);
		AST_VECTOR_PTR_FREE(new_configs);
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Now swap the new ones in. */
	*configs = new_configs;

	/* Free the old ones. */
	AST_VECTOR_RESET(old_configs, config_free);
	AST_VECTOR_PTR_FREE(old_configs);

	return AST_MODULE_LOAD_SUCCESS;


	return -1;
}

struct cdrel_configs *cdrel_load_module(enum cdrel_backend_type backend_type,
	enum cdrel_record_type record_type, const char *filename, const char *backend_name,
	void *log_cb)
{
	struct cdrel_configs *configs = ast_calloc(1, sizeof(*configs));
	if (!configs) {
		return NULL;
	}
	ast_debug(1, "Loading %s %s\n", RECORD_TYPE_STR(record_type), MODULE_TYPE_STR(backend_type));

	if (AST_VECTOR_INIT(configs, 5) != 0) {
		cdrel_unload_module(backend_type, record_type, configs, backend_name);
		return NULL;
	}

	if (load_config_file(backend_type, record_type, configs, filename, 0) != 0) {
		cdrel_unload_module(backend_type, record_type, configs, backend_name);
		return NULL;
	}

	if (register_backend(record_type, backend_name, log_cb)) {
		cdrel_unload_module(backend_type, record_type, configs, backend_name);
		return NULL;
	}

	return configs;
}

int cdrel_unload_module(enum cdrel_backend_type backend_type, enum cdrel_record_type record_type,
	struct cdrel_configs *configs, const char *backend_name)
{
	if (unregister_backend(record_type, backend_name) != 0) {
		return -1;
	}

	AST_VECTOR_RESET(configs, config_free);
	AST_VECTOR_PTR_FREE(configs);

	return 0;
}
