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
 * \brief CDR Getters
 *
 */

#include "cdrel.h"
#include "asterisk/cdr.h"

static int cdr_get_string(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	struct ast_cdr *cdr = record;
	value->data_type = field->input_data_type;
	value->field_name = field->name;
	value->values.string = CDR_FIELD(cdr, field->field_id);
	return 0;
}

#define DEFINE_CDR_GETTER(_sname, _ename, _type) \
static int cdr_get_ ## _ename (void *record, struct cdrel_config *config, \
	struct cdrel_field *field, struct cdrel_value *value) \
{ \
	struct ast_cdr *cdr = record; \
	value->data_type = field->input_data_type; \
	value->field_name = field->name; \
	value->values._sname = *(_type *)CDR_FIELD(cdr, field->field_id); \
	return 0; \
}\

DEFINE_CDR_GETTER(int32, int32, int32_t)
DEFINE_CDR_GETTER(uint32, uint32, uint32_t)
DEFINE_CDR_GETTER(int64, int64, int64_t)
DEFINE_CDR_GETTER(uint64, uint64, uint64_t)
DEFINE_CDR_GETTER(tv, timeval, struct timeval)
DEFINE_CDR_GETTER(floater, float, float)

static int cdr_get_literal(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	value->data_type = cdrel_type_string;
	value->field_name = field->name;
	value->values.string = field->data;
	return 0;
}

static int cdr_get_uservar(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	struct ast_cdr *cdr = record;
	struct ast_var_t *variables;
	const char *rtn = NULL;

	value->data_type = cdrel_type_string;
	value->field_name = field->name;
	AST_LIST_TRAVERSE(&cdr->varshead, variables, entries) {
		if (strcasecmp(field->data, ast_var_name(variables)) == 0) {
			rtn = ast_var_value(variables);
		}
	}

	value->values.string = (char *)S_OR(rtn, "");
	return 0;
}

static struct ast_channel *dummy_chan_alloc_cdr(struct cdrel_config *config, void *data)
{
	struct ast_cdr *cdr = data;
	struct ast_channel *dummy = NULL;

	dummy = ast_dummy_channel_alloc();
	if (!dummy) {
		ast_log(LOG_ERROR, "Unable to fabricate channel from CDR for '%s'\n",
			config->output_filename);
		return NULL;
	}
	ast_channel_cdr_set(dummy, ast_cdr_dup(cdr));
	return dummy;
}

int load_cdr(void)
{
	ast_debug(1, "Loading CDR getters\n");
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_string] = cdr_get_string;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_literal] = cdr_get_literal;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_int32] = cdr_get_int32;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_uint32] = cdr_get_uint32;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_int64] = cdr_get_int64;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_uint64] = cdr_get_uint64;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_timeval] = cdr_get_timeval;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_float] = cdr_get_float;
	cdrel_field_getters[cdrel_record_cdr][cdrel_type_uservar] = cdr_get_uservar;
	cdrel_dummy_channel_allocators[cdrel_record_cdr] = dummy_chan_alloc_cdr;

	return 0;
}
