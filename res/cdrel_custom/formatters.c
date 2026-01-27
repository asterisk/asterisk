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
 * \brief Formatters
 *
 */

#include "cdrel.h"
#include "asterisk/json.h"
#include "asterisk/cdr.h"
#include "asterisk/cel.h"

static char *quote_escaper(const char *value, char quote, char quote_escape, char *qvalue)
{
	char *ptr = qvalue;
	const char *dataptr = value;

	if (!qvalue) {
		return NULL;
	}

	while(*dataptr != '\0') {
		if (*dataptr == quote) {
			*ptr++ = quote_escape;
		}
		*ptr++ = *dataptr++;
	}
	*ptr='\0';
	return qvalue;
}

static int format_string(struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *input_value, struct cdrel_value *output_value)
{
	int quotes_count = 0;
	int needs_quoting = ast_test_flag(&field->flags, cdrel_flag_quote);
	int ix = 0;
	int input_len = strlen(input_value->values.string ?: "");
	int res = 0;
	char *qvalue = NULL;
	char *evalue = (char *)input_value->values.string;

	output_value->data_type = cdrel_type_string;
	output_value->field_name = input_value->field_name;

	if (input_len == 0) {
		output_value->values.string = ast_strdup(needs_quoting ? "\"\"" : "");
		return 0;
	}

	for (ix = 0; ix < input_len; ix++) {
		char c = input_value->values.string[ix];
		if (c == config->quote[0]) {
			quotes_count++;
			needs_quoting = 1;
		} else if (c == config->separator[0] || c == '\r' || c == '\n') {
			needs_quoting = 1;
		}
	}

	ast_debug(5, "%s: %s=%s %s", cdrel_basename(config->output_filename), input_value->field_name,
		input_value->values.string, ast_str_tmp(128, cdrel_get_field_flags(&field->flags, &STR_TMP)));

	if (!needs_quoting) {
		output_value->values.string = ast_strdup(input_value->values.string);
		return output_value->values.string == NULL ? -1 : 0;
	}

	/* For every quote_count, we need an extra byte for the quote escape character. */
	qvalue = ast_alloca(input_len + quotes_count + 1);
	if (quotes_count) {
		evalue = quote_escaper(input_value->values.string, config->quote[0], config->quote_escape[0], qvalue);
	}
	res = ast_asprintf(&output_value->values.string, "%s%s%s", config->quote, evalue, config->quote);
	return res < 0 ? -1 : 0;
}

#define DEFINE_FORMATTER(_name, _field, _type, _fmt) \
	static int format_ ## _name (struct cdrel_config *config, \
		struct cdrel_field *field, struct cdrel_value *input_value, struct cdrel_value *output_value) \
	{ \
		int res = 0; \
		char *quote = ""; \
		if (input_value->data_type != output_value->data_type) { \
			/* Forward to other formatter */ \
			return cdrel_field_formatters[output_value->data_type](config, field, input_value, output_value); \
		} \
		output_value->field_name = input_value->field_name; \
		if ((config->quoting_method == cdrel_quoting_method_all || ast_test_flag(&field->flags, cdrel_flag_quote)) \
			&& !ast_test_flag(&field->flags, cdrel_flag_noquote)) { \
			quote = config->quote; \
		} \
		res = ast_asprintf(&output_value->values.string, "%s" _fmt "%s", quote, input_value->values._field, quote); \
		output_value->data_type = cdrel_type_string; \
		return res < 0 ? -1 : 0; \
	}

DEFINE_FORMATTER(uint32, uint32, uint32_t, "%u")
DEFINE_FORMATTER(int32, int32, int32_t, "%d")
DEFINE_FORMATTER(uint64, uint64, uint64_t, "%lu")
DEFINE_FORMATTER(int64, int64, int64_t, "%ld")
DEFINE_FORMATTER(float, floater, float, "%.1f")

static int format_timeval(struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *input_value, struct cdrel_value *output_value)
{
	struct ast_tm tm;
	char tempbuf[64];
	int res = 0;
	const char *format = "%Y-%m-%d %T";

	output_value->field_name = input_value->field_name;

	if (field->output_data_type == cdrel_type_int64) {
		output_value->data_type = cdrel_type_int64;
		output_value->values.int64 = input_value->values.tv.tv_sec;
		return format_int64(config, field, output_value, output_value);
	} else if (field->output_data_type == cdrel_type_float) {
		output_value->data_type = cdrel_type_float;
		output_value->values.floater = ((float)input_value->values.tv.tv_sec) + ((float)input_value->values.tv.tv_usec) / 1000000.0;
		return format_float(config, field, output_value, output_value);
	} else 	if (!ast_strlen_zero(field->data)) {
		format = field->data;
	}

	ast_localtime(&input_value->values.tv, &tm, NULL);
	ast_strftime(tempbuf, sizeof(tempbuf), format, &tm);
	input_value->data_type = cdrel_type_string;
	output_value->data_type = cdrel_type_string;
	input_value->values.string = tempbuf;
	res = format_string(config, field, input_value, output_value);
	return res;
}

static int format_amaflags(struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *input_value, struct cdrel_value *output_value)
{
	int res = 0;

	input_value->values.string = (char *)ast_channel_amaflags2string(input_value->values.int64);
	input_value->data_type = cdrel_type_string;
	output_value->data_type = cdrel_type_string;
	res = format_string(config, field, input_value, output_value);
	return res;
}

static int format_disposition(struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *input_value, struct cdrel_value *output_value)
{
	int res = 0;

	input_value->values.string = (char *)ast_cdr_disp2str(input_value->values.int64);
	input_value->data_type = cdrel_type_string;
	output_value->data_type = cdrel_type_string;
	res = format_string(config, field, input_value, output_value);
	return res;
}

int load_formatters(void)
{
	ast_debug(1, "Loading Formatters\n");
	cdrel_field_formatters[cdrel_type_string] = format_string;
	cdrel_field_formatters[cdrel_type_int32] = format_int32;
	cdrel_field_formatters[cdrel_type_uint32] = format_uint32;
	cdrel_field_formatters[cdrel_type_int64] = format_int64;
	cdrel_field_formatters[cdrel_type_uint64] = format_uint64;
	cdrel_field_formatters[cdrel_type_timeval] = format_timeval;
	cdrel_field_formatters[cdrel_type_float] = format_float;
	cdrel_field_formatters[cdrel_type_amaflags] = format_amaflags;
	cdrel_field_formatters[cdrel_type_disposition] = format_disposition;

	return 0;
}
