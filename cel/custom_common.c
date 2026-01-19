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

#include "asterisk.h"

#include "asterisk/strings.h"
#include "asterisk/vector.h"
#include "asterisk/lock.h"
#include "asterisk/json.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/cel.h"

#include "custom_common.h"

static char *quoter(const char *value, char quote, char quote_escape)
{
	char *bufptr = ast_malloc((strlen(value) * 2) + 1);
	char *ptr = bufptr;
	const char *dataptr = value;

	if (!bufptr) {
		return NULL;
	}

	while(*dataptr != '\0') {
		if (*dataptr == quote) {
			*bufptr++ = quote_escape;
		}
		*bufptr++ = *dataptr++;
	}
	*bufptr='\0';
	return ptr;
}

static int csv_append_string(struct ast_str **str, int is_first, const char *value, struct cel_config *config)
{
	int res = 0;
	char *sep_str = is_first ? "" : config->separator;

	if (config->quoting_method == cel_quoting_method_all || config->quoting_method == cel_quoting_method_non_numeric) {
		char *evalue = (char *)value;
		if (strchr(value, config->quote[0])) {
			evalue = quoter(value, config->quote[0], config->quote_escape[0]);
		}
		res = ast_str_append(str, 0, "%s%s%s%s", sep_str, config->quote, S_OR(evalue, ""), config->quote);
		if (evalue != value) {
			ast_free(evalue);
		}
	} else if (config->quoting_method == cel_quoting_method_minimal) {
		if (strchr(value, config->separator[0])) {
			char *evalue = (char *)value;
			if (strchr(value, config->quote[0])) {
				evalue = quoter(value, config->quote[0], config->quote_escape[0]);
			}
			res = ast_str_append(str, 0, "%s%s%s%s", sep_str, config->quote, S_OR(evalue, ""), config->quote);
			if (evalue != value) {
				ast_free(evalue);
			}
		} else {
			res = ast_str_append(str, 0, "%s%s", sep_str, value);
		}
	} else {
		res = ast_str_append(str, 0, "%s%s", sep_str, value);
	}
	return res;
}

static int csv_append_uint(struct ast_str **str, int is_first, uint32_t value, struct cel_config *config)
{
	int res = 0;
	char *sep_str = is_first ? "" : config->separator;

	if (config->quoting_method == cel_quoting_method_all) {
		res = ast_str_append(str, 0, "%s%s%u%s", sep_str, config->quote, value, config->quote);
	} else {
		res = ast_str_append(str, 0, "%s%d", sep_str, value);
	}
	return res;
}

static char *get_event_time(struct ast_event *event)
{
	char timebuf[32];
	struct timeval tv = {
		.tv_sec = ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TIME),
		.tv_usec = ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TIME_USEC)
	};
	ast_cel_format_eventtime(tv, timebuf, sizeof(timebuf));
	return ast_strdup(timebuf);
}

const static char *get_event_type(struct ast_event *event, int explicit)
{
	const char *value = NULL;
	if (explicit || ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TYPE) != AST_CEL_USER_DEFINED) {
		value = ast_cel_get_type_name(ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TYPE));
	} else {
		value = ast_event_get_ie_str(event, AST_EVENT_IE_CEL_USEREVENT_NAME);
	}
	return value;
}

static void append_csv_event_string(struct ast_str **str, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	const char *value = ast_event_get_ie_str(event, cel_field->ie_type);
	csv_append_string(str, is_first, value, config);
}

static void append_csv_event_time(struct ast_str **str, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	char *value = get_event_time(event);
	csv_append_string(str, is_first, value, config);
	ast_free(value);
}

static void append_csv_event_type(struct ast_str **str, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	const char *value = get_event_type(event, 0);
	csv_append_string(str, is_first, value, config);
}

static void append_csv_event_enum(struct ast_str **str, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	const char *value = get_event_type(event, 1);
	csv_append_string(str, is_first, value, config);
}

static void append_csv_literal(struct ast_str **str, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	csv_append_string(str, is_first, cel_field->literal_data, config);
}

static void append_csv_event_uint(struct ast_str **str, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	csv_append_uint(str, is_first, ast_event_get_ie_uint(event, cel_field->ie_type), config);
}

/* JSON */

static void append_json_event_string(struct ast_json *json, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	const char *value = ast_event_get_ie_str(event, cel_field->ie_type);
	ast_json_object_set(json, cel_field->name, ast_json_string_create(value));
}

static void append_json_event_time(struct ast_json *json, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	char *value = get_event_time(event);
	ast_json_object_set(json, cel_field->name, ast_json_string_create(value));
	ast_free(value);
}

static void append_json_event_type(struct ast_json *json, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	const char *value = get_event_type(event, 0);
	ast_json_object_set(json, cel_field->name, ast_json_string_create(value));
}

static void append_json_event_enum(struct ast_json *json, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	const char *value = get_event_type(event, 1);
	ast_json_object_set(json, cel_field->name, ast_json_string_create(value));
}

static void append_json_literal(struct ast_json *json, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	char *field_name = ast_strdupa(cel_field->literal_data);
	char *sep = strchr(field_name, ':');
	if (sep) {
		*sep = '\0';
		sep++;
		ast_json_object_set(json, ast_strip(field_name), ast_json_string_create(ast_strip(sep)));
	}
}

static void append_json_event_uint(struct ast_json *json, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first)
{
	ast_json_object_set(json, cel_field->name, ast_json_integer_create(ast_event_get_ie_uint(event, cel_field->ie_type)));
}


static const struct cel_field cel_field_registry[] = {
	{AST_EVENT_IE_CEL_EVENT_ENUM, AST_EVENT_IE_PLTYPE_UINT, append_csv_event_enum, append_json_event_enum, "EventEnum", 0 },
	{AST_EVENT_IE_CEL_EVENT_TYPE, AST_EVENT_IE_PLTYPE_UINT, append_csv_event_type, append_json_event_type, "EventType", 0 },
	{AST_EVENT_IE_CEL_EVENT_TIME, AST_EVENT_IE_PLTYPE_UINT, append_csv_event_time, append_json_event_time, "EventTime", 0 },
	{AST_EVENT_IE_CEL_EVENT_TIME_USEC, AST_EVENT_IE_PLTYPE_UINT, append_csv_event_uint, append_json_event_uint, "EventTimeUSec", 0 },
	{AST_EVENT_IE_CEL_USEREVENT_NAME, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "UserEventName", 0 },
	{AST_EVENT_IE_CEL_USEREVENT_NAME, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "UserDefType", 0 },
	{AST_EVENT_IE_CEL_CIDNAME, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "CIDName", 0 },
	{AST_EVENT_IE_CEL_CIDNUM, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "CIDNum", 0 },
	{AST_EVENT_IE_CEL_EXTEN, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "Exten", 0 },
	{AST_EVENT_IE_CEL_CONTEXT, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "Context", 0 },
	{AST_EVENT_IE_CEL_CHANNAME, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "ChanName", 0 },
	{AST_EVENT_IE_CEL_APPNAME, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "AppName", 0 },
	{AST_EVENT_IE_CEL_APPDATA, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "AppData", 0 },
	{AST_EVENT_IE_CEL_AMAFLAGS, AST_EVENT_IE_PLTYPE_UINT, append_csv_event_uint, append_json_event_uint, "AMAFlags", 0 },
	{AST_EVENT_IE_CEL_ACCTCODE, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "AcctCode", 0 },
	{AST_EVENT_IE_CEL_UNIQUEID, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "UniqueID", 0 },
	{AST_EVENT_IE_CEL_USERFIELD, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "UserField", 0 },
	{AST_EVENT_IE_CEL_CIDANI, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "CIDani", 0 },
	{AST_EVENT_IE_CEL_CIDRDNIS, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "CIDrdnis", 0 },
	{AST_EVENT_IE_CEL_CIDDNID, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "CIDdnid", 0 },
	{AST_EVENT_IE_CEL_PEER, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "Peer", 0 },
	{AST_EVENT_IE_CEL_PEER, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "BridgePeer", 0 },
	{AST_EVENT_IE_CEL_LINKEDID, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "LinkedID", 0 },
	{AST_EVENT_IE_CEL_PEERACCT, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "PeerAcct", 0 },
	{AST_EVENT_IE_CEL_EXTRA, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "Extra", 0 },
	{AST_EVENT_IE_CEL_EXTRA, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "EventExtra", 0 },
	{AST_EVENT_IE_CEL_TENANTID, AST_EVENT_IE_PLTYPE_STR, append_csv_event_string, append_json_event_string,  "TenantID", 0 },
	{AST_EVENT_IE_CEL_LITERAL, AST_EVENT_IE_PLTYPE_STR, append_csv_literal, append_json_literal,  "_LITERAL", 1 },
};

static const struct cel_field *get_registered_field_by_name(const char *name)
{
	int ix = 0;

	for (ix = 0; ix < ARRAY_LEN(cel_field_registry); ix++) {
		if (strcasecmp(cel_field_registry[ix].name, name) == 0) {
			return &cel_field_registry[ix];
		}
	}
	return NULL;
}

struct cel_field *cel_field_alloc(const char *field, enum cel_format_type format_type,
	const char *filename)
{
	const struct cel_field *cel_field = NULL;
	struct cel_field *rtn = NULL;

	cel_field = get_registered_field_by_name(field);
	if (cel_field) {
		ast_debug(2, "%s: CEL event '%s' found\n", filename, field);
		return (struct cel_field *)cel_field;
	}

	if (format_type == cel_format_json && !strchr(field, ':')) {
		ast_log(LOG_WARNING, "%s: Literal field '%s' must be formatted as \"name: value\" when using the 'json' format\n",
			filename, field);
		return NULL;
	}

	cel_field = get_registered_field_by_name("_LITERAL");
	rtn = ast_calloc(1, sizeof(*cel_field) + strlen(field) + 1);
	if (!rtn) {
		return NULL;
	}
	memcpy(rtn, cel_field, sizeof(*cel_field));
	strcpy(rtn->literal_data, field); /* Safe */

	ast_debug(2, "%s: Literal field '%s' found\n", filename, field);
	return rtn;
}

static void free_field(struct cel_field *f) {
	if (f->mallocd) {
		ast_free(f);
	}
}

void cel_free_sink(struct cel_config *sink)
{
	ast_mutex_destroy(&sink->lock);
	ast_string_field_free_memory(sink);
	AST_VECTOR_RESET(&sink->fields, free_field);
	AST_VECTOR_FREE(&sink->fields);
	ast_free(sink);
}
