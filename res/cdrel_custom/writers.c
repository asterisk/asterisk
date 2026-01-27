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
 * \brief Backend output functions.
 *
 * The writers all take a vector of cdrel_value objects and write them to the output
 * file or database.
 *
 */

#include "cdrel.h"

/*
 * We can save some time and ast_str memory allocation work by allocating a single
 * thread-local buffer and re-using it.
 */
AST_THREADSTORAGE(custom_buf);

/*!
 * \internal
 * \brief Write a record to a text file
 *
 * Besides being used here, this function is also used by the legacy loggers
 * that shortcut the advanced stuff.
 *
 * \param config The configuration object.
 * \param record The data to write.
 * \retval 0 on success
 * \retval -1 on failure
 */
int write_record_to_file(struct cdrel_config *config, struct ast_str *record)
{
	FILE *out;
	int res = 0;

	ast_mutex_lock(&config->lock);
	if ((out = fopen(config->output_filename, "a"))) {
		res = fputs(ast_str_buffer(record), out);
		fputs("\n", out);
		fflush(out);
		fclose(out);
	}
	ast_mutex_unlock(&config->lock);

	if (!out || res < 0) {
		ast_log(LOG_ERROR, "Unable to write %s to file %s : %s\n",
			RECORD_TYPE_STR(config->record_type), config->output_filename, strerror(errno));
		return -1;
	}
	return 0;
}

/*!
 * \internal
 * \brief Concatenate and append a list of values to an ast_str
 *
 * \param config Config object
 * \param values A vector of values
 * \param str The ast_str to append to
 * \retval 0 on success
 * \retval -1 on failure
 */
static int dsv_appender(struct cdrel_config *config, struct cdrel_values *values, struct ast_str **str)
{
	int ix = 0;
	int res = 0;

	for (ix = 0; ix < AST_VECTOR_SIZE(values); ix++) {
		struct cdrel_value *value = AST_VECTOR_GET(values, ix);
		ast_assert(value->data_type == cdrel_type_string);

		res = ast_str_append(str, -1, "%s%s", ix == 0 ? "" : config->separator, value->values.string);
		if (res < 0) {
			return -1;
		}
	}

	return res;
}

/*!
 * \internal
 * \brief Write a DSV list of values to a text file
 *
 * \param config Config object
 * \param values A vector of values
 * \retval 0 on success
 * \retval -1 on failure
 */
static int dsv_writer(struct cdrel_config *config, struct cdrel_values *values)
{
	int res = 0;
	struct ast_str *str = ast_str_thread_get(&custom_buf, 1024);

	ast_str_reset(str);

	res = dsv_appender(config, values, &str);
	if (res < 0) {
		return -1;
	}

	return write_record_to_file(config, str);
}

/*!
 * \internal
 * \brief Write a list of values as a JSON object to a text file
 *
 * \param config Config object
 * \param values A vector of values
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \note We are intentionally NOT using the ast_json APIs here because
 * they're expensive and these are simple objects.
 */
static int json_writer(struct cdrel_config *config, struct cdrel_values *values)
{
	int ix = 0;
	int res = 0;
	struct ast_str *str = ast_str_thread_get(&custom_buf, 1024);

	ast_str_set(&str, -1, "%s", "{");

	for (ix = 0; ix < AST_VECTOR_SIZE(values); ix++) {
		struct cdrel_value *value = AST_VECTOR_GET(values, ix);
		ast_assert(value->data_type == cdrel_type_string);

		res = ast_str_append(&str, -1, "%s\"%s\":%s", ix == 0 ? "" : config->separator, value->field_name, value->values.string);
		if (res < 0) {
			return -1;
		}
	}
	ast_str_append(&str, -1, "%s", "}");

	return write_record_to_file(config, str);
}

/*!
 * \internal
 * \brief Write a record to a database
 *
 * Besides being used here, this function is also used by the legacy loggers
 * that shortcut the advanced stuff.
 *
 * \param config The configuration object.
 * \param record The data to write.
 * \retval 0 on success
 * \retval -1 on failure
 */
int write_record_to_database(struct cdrel_config *config, struct cdrel_values *values)
{
	int res = 0;
	int ix;

	ast_mutex_lock(&config->lock);
	for (ix = 0; ix < AST_VECTOR_SIZE(values); ix++) {
		struct cdrel_value *value = AST_VECTOR_GET(values, ix);
		ast_assert(value->data_type == cdrel_type_string);
		ast_debug(6, "%s '%s'\n", value->field_name, value->values.string);
		res = sqlite3_bind_text(config->insert, ix + 1, value->values.string, -1, SQLITE_STATIC);
		if (res != SQLITE_OK) {
			ast_log(LOG_ERROR, "Unable to write %s to database %s.  SQL bind for field %s:'%s'. Error: %s\n",
				RECORD_TYPE_STR(config->record_type), config->output_filename,
				value->field_name, value->values.string,
				sqlite3_errmsg(config->db));
			sqlite3_reset(config->insert);
			ast_mutex_unlock(&config->lock);
			return -1;
		}
	}

	res = sqlite3_step(config->insert);
	if (res != SQLITE_DONE) {
		ast_log(LOG_ERROR, "Unable to write %s to database %s. Error: %s\n",
			RECORD_TYPE_STR(config->record_type), config->output_filename,
			sqlite3_errmsg(config->db));
		sqlite3_reset(config->insert);
		ast_mutex_unlock(&config->lock);
		return -1;
	}

	sqlite3_reset(config->insert);
	ast_mutex_unlock(&config->lock);

	return res;
}

/*!
 * \internal
 * \brief Write a list of values to a database
 *
 * \param config Config object
 * \param values A vector of values
 * \retval 0 on success
 * \retval -1 on failure
 */
static int database_writer(struct cdrel_config *config, struct cdrel_values *values)
{
	return write_record_to_database(config, values);
}

int load_writers(void)
{
	ast_debug(1, "Loading Writers\n");
	cdrel_backend_writers[cdrel_format_dsv] = dsv_writer;
	cdrel_backend_writers[cdrel_format_json] = json_writer;
	cdrel_backend_writers[cdrel_format_sql] = database_writer;

	return 0;
}

