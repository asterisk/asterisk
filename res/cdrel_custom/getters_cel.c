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
 * \brief CEL Getters
 *
 */

#include "cdrel.h"
#include "asterisk/cel.h"

static int cel_get_string(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	struct ast_event *event = record;
	value->data_type = cdrel_type_string;
	value->field_name = field->name;
	value->values.string = (char *)ast_event_get_ie_str(event, field->field_id);
	return 0;
}

static int cel_get_literal(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	value->data_type = cdrel_type_string;
	value->field_name = field->name;
	value->values.string = field->data;
	return 0;
}

static int cel_get_timeval(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	struct ast_event *event = record;
	value->data_type = cdrel_type_timeval;
	value->field_name = field->name;
	value->values.tv.tv_sec = ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TIME);
	value->values.tv.tv_usec = ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TIME_USEC);
	return 0;
}

static int cel_get_uint32(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	struct ast_event *event = record;
	value->data_type = cdrel_type_uint32;
	value->field_name = field->name;
	value->values.uint32 = ast_event_get_ie_uint(event, field->field_id);
	return 0;
}

static int cel_get_event_type(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	struct ast_event *event = record;
	const char *val = NULL;
	value->data_type = cdrel_type_string;
	value->field_name = field->name;

	if (ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TYPE) == AST_CEL_USER_DEFINED) {
		val = ast_event_get_ie_str(event, AST_EVENT_IE_CEL_USEREVENT_NAME);
	} else {
		val = ast_cel_get_type_name(ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TYPE));
	}
	value->values.string = (char *)val;
	return 0;
}

static int cel_get_event_enum(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value)
{
	struct ast_event *event = record;
	value->data_type = cdrel_type_string;
	value->field_name = field->name;
	value->values.string = (char *)ast_cel_get_type_name(ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TYPE));
	return 0;
}

static struct ast_channel *dummy_chan_alloc_cel(struct cdrel_config *config, void *data)
{
	struct ast_event *event = data;

	return ast_cel_fabricate_channel_from_event(event);
}

int load_cel(void)
{
	ast_debug(1, "Loading CEL getters\n");
	cdrel_field_getters[cdrel_record_cel][cdrel_type_string] = cel_get_string;
	cdrel_field_getters[cdrel_record_cel][cdrel_type_literal] = cel_get_literal;
	cdrel_field_getters[cdrel_record_cel][cdrel_type_uint32] = cel_get_uint32;
	cdrel_field_getters[cdrel_record_cel][cdrel_type_timeval] = cel_get_timeval;
	cdrel_field_getters[cdrel_record_cel][cdrel_type_event_type] = cel_get_event_type;
	cdrel_field_getters[cdrel_record_cel][cdrel_type_event_enum] = cel_get_event_enum;
	cdrel_dummy_channel_allocators[cdrel_record_cel] = dummy_chan_alloc_cel;

	return 0;
}
