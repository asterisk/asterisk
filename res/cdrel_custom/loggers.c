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
 * \brief Common log entrypoint from the cdr/cel modules
 *
 */

#include "cdrel.h"
#include "asterisk/pbx.h"
#include "asterisk/vector.h"

/*
 * We can save some time and ast_str memory allocation work by allocating a single
 * thread-local buffer and re-using it.
 */
AST_THREADSTORAGE(custom_buf);

/*!
 * \internal
 * \brief Free an ast_value object
 *
 * ... and if the data type is "string", free it as well.
 *
 * \param data
 */
static void free_value(void *data)
{
	struct cdrel_value *val = data;
	if (val->data_type == cdrel_type_string) {
		ast_free(val->values.string);
		val->values.string = NULL;
	}
	ast_free(val);
}

/*!
 * \internal
 * \brief Free a vector of cdrel_values
 *
 * \param data
 */
static void free_value_vector(void *data)
{
	struct cdrel_values *values = data;
	AST_VECTOR_RESET(values, free_value);
	AST_VECTOR_PTR_FREE(values);
}

/*!
 * \internal
 * \brief Log a legacy record to a file.
 *
 * The file legacy format specifies one long string with dialplan functions.  We have no idea
 * what the separator is so we need to pass the entire string to ast_str_substitute_variables.
 * This is where the cycles are spent.  We then write the result directly to the backend
 * file bypassing all of the advanced processing.
 *
 * \param config The configuration object.
 * \param data The data to write. May be an ast_cdr or an ast_event.
 * \retval 0 on success
 * \retval -1 on failure
 */
static int log_legacy_dsv_record(struct cdrel_config *config, void *data)
{
	struct ast_channel *dummy = data;
	struct ast_str *str;

	if (!(str = ast_str_thread_get(&custom_buf, 1024))) {
		return -1;
	}
	ast_str_reset(str);

	ast_str_substitute_variables(&str, 0, dummy, config->template);

	return write_record_to_file(config, str);
}

/*!
 * \internal
 * \brief Log a legacy record to a database.
 *
 * Unlike the file backends, the legacy database backend configs always use commas
 * as field separators but they all still use dialplan functions so we need still
 * need to do evaluation and substitution.  Since we know the separator however,
 * we can iterate over the individual fields.
 *
 * \param config The configuration object.
 * \param data The data to write. May be an ast_cdr or an ast_event.
 * \retval 0 on success
 * \retval -1 on failure
 */
static int log_legacy_database_record(struct cdrel_config *config, void *data)
{
	struct ast_channel *dummy = data;
	int ix = 0;
	int res = 0;
	char subst_buf[2048];
	size_t field_count = AST_VECTOR_SIZE(&config->fields);
	RAII_VAR(struct cdrel_values *, values, ast_calloc(1, sizeof(*values)), free_value_vector);

	if (!values) {
		return -1;
	}

	res = AST_VECTOR_INIT(values, field_count);
	if (res != 0) {
		return -1;
	}

	if (config->db == NULL) {
		return -1;
	}

	for (ix = 0; ix < AST_VECTOR_SIZE(&config->fields); ix++) {
		struct cdrel_field *field = AST_VECTOR_GET(&config->fields, ix);
		struct cdrel_value *output_value = ast_calloc(1, sizeof(*output_value));

		if (!output_value) {
			return -1;
		}
		output_value->mallocd = 1;

		pbx_substitute_variables_helper(dummy, field->data, subst_buf, sizeof(subst_buf) - 1);
		output_value->data_type = cdrel_type_string;

		output_value->field_name = field->name;
		output_value->values.string = ast_strdup(ast_strip_quoted(subst_buf,  "'\"", "'\""));
		if (!output_value->values.string) {
			return -1;
		}

		res = AST_VECTOR_APPEND(values, output_value);
		if (res != 0) {
			ast_free(output_value);
			return -1;
		}
	}

	return write_record_to_database(config, values);
}

/*!
 * \internal
 * \brief Log an advanced record
 *
 * For the file advanced formats, we know what the field separator is so we
 * iterate over them and accumulate the results in a vector of cdrel_values.
 * No dialplan function evaluation needed.
 *
 * \param config The configuration object.
 * \param data The data to log. May be an ast_cdr or an ast_event.
 * \retval 0 on success
 * \retval -1 on failure
 */
static int log_advanced_record(struct cdrel_config *config, void *data)
{
	int ix = 0;
	int res = 0;
	size_t field_count = AST_VECTOR_SIZE(&config->fields);
	RAII_VAR(struct cdrel_values *, values, ast_calloc(1, sizeof(*values)), free_value_vector);

	if (!values) {
		return -1;
	}

	res = AST_VECTOR_INIT(values, field_count);
	if (res != 0) {
		return -1;
	}

	for (ix = 0; ix < AST_VECTOR_SIZE(&config->fields); ix++) {
		struct cdrel_field *field = AST_VECTOR_GET(&config->fields, ix);
		struct cdrel_value input_value = { 0, };
		struct cdrel_value *output_value = ast_calloc(1, sizeof(*output_value));

		if (!output_value) {
			return -1;
		}
		output_value->mallocd = 1;

		/*
		 * Get a field from a CDR structure or CEL event into an cdrel_value.
		 */
		res = cdrel_field_getters[config->record_type][field->input_data_type](data, config, field, &input_value);
		if (res != 0) {
			ast_free(output_value);
			return -1;
		}

		/*
		 * Set the output data type to the type we want to see in the output.
		 */
		output_value->data_type = field->output_data_type;

		/*
		 * Now call the formatter based on the INPUT data type.
		 */
		res = cdrel_field_formatters[input_value.data_type](config, field, &input_value, output_value);
		if (res != 0) {
			ast_free(output_value);
			return -1;
		}

		res = AST_VECTOR_APPEND(values, output_value);
		if (res != 0) {
			ast_free(output_value);
			return -1;
		}
	}
	return cdrel_backend_writers[config->format_type](config, values);
}

/*
 * These callbacks are only used in this file so there's no need to
 * make them available to the rest of the module.
 */
typedef int (*cdrel_logger_cb)(struct cdrel_config *config, void *data);

static const cdrel_logger_cb logger_callbacks[cdrel_backend_type_end][cdrel_config_type_end] = {
	[cdrel_backend_text] = {
		[cdrel_config_legacy] = log_legacy_dsv_record,
		[cdrel_config_advanced] = log_advanced_record
	},
	[cdrel_backend_db] = {
		[cdrel_config_legacy] = log_legacy_database_record,
		[cdrel_config_advanced] = log_advanced_record
	},
};

/*!
 * \internal
 * \brief Main logging entrypoint from the individual modules.
 *
 * This is the entrypoint from the individual cdr and cel modules.
 * "data" will either be an ast_cdr or ast_event structure but we
 * don't actually care at this point.
 *
 * For legacy configs, we need to create a dummy channel so we'll
 * do that if/when we hit the first one and we'll reuse it for all
 * further legacy configs.  If we fail to get a channel, we'll skip
 * all further configs.
 *
 * \warning This function MUST be called with the module's config_lock
 * held for reading to prevent reloads from happening while we're logging.
 *
 * \param configs The calling module's vector of configuration objects.
 * \param data The data to write. May be an ast_cdr or an ast_event.
 * \retval 0 on success
 * \retval <0 on failure. The magnitude indicates how many configs failed.
 */
int cdrel_logger(struct cdrel_configs *configs, void *data)
{
	struct ast_channel *dummy = NULL;
	int ix = 0;
	int skip_legacy = 0;
	int res = 0;

	for(ix = 0; ix < AST_VECTOR_SIZE(configs); ix++) {
		struct cdrel_config *config = AST_VECTOR_GET(configs, ix);
		void *chan_or_data = NULL;

		if (config->config_type == cdrel_config_legacy) {
			if (skip_legacy) {
				continue;
			}
			if (!dummy) {
				dummy = config->dummy_channel_alloc(config, data);
				if (!dummy) {
					ast_log(LOG_ERROR, "Unable to fabricate channel from CEL event for '%s'\n",
						config->output_filename);
					skip_legacy = 1;
					res--;
					continue;
				}
			}
			chan_or_data = dummy;
		} else {
			chan_or_data = data;
		}
		res += logger_callbacks[config->backend_type][config->config_type](config, chan_or_data);
	}

	if (dummy) {
		ast_channel_unref(dummy);
	}
	return res;
}

