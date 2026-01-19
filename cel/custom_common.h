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

#ifndef _CUSTOM_COMMON_H
#define _CUSTOM_COMMON_H

#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/vector.h"

enum cel_format_type {
	cel_format_csv = 0,
	cel_format_json,
	cel_format_sql
};

enum cel_sink_type {
	cel_sink_legacy = 0,
	cel_sink_advanced,
};

enum cel_quoting_method {
	cel_quoting_method_none = 0,
	cel_quoting_method_all,
	cel_quoting_method_minimal,
	cel_quoting_method_non_numeric,
};

struct cel_config;
struct cel_field;

typedef void (*cel_csv_field_appender)(struct ast_str **str, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first);

typedef void (*cel_json_field_appender)(struct ast_json *, struct ast_event *event, struct cel_config *config,
	struct cel_field *cel_field, int is_first);

struct cel_field {
	enum ast_event_ie_type ie_type;
	enum ast_event_ie_pltype ie_pltype;
	cel_csv_field_appender csv_field_appender;
	cel_json_field_appender json_field_appender;
	const char *name;
	int mallocd;
	char literal_data[0];
};

struct cel_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(filename);
		AST_STRING_FIELD(template);
	);
	enum cel_sink_type sink_type;
	enum cel_format_type format_type;
	enum cel_quoting_method quoting_method;
	char separator[2];
	char quote[2];
	char quote_escape[2];
	AST_VECTOR(, struct cel_field *) fields;
	ast_mutex_t lock;
	AST_RWLIST_ENTRY(cel_config) list;
};

#define AST_EVENT_IE_CEL_LITERAL (AST_EVENT_IE_TOTAL + 1)
#define AST_EVENT_IE_CEL_EVENT_ENUM (AST_EVENT_IE_TOTAL + 2)

struct cel_field *cel_field_alloc(const char *field, enum cel_format_type format_type,
	const char *filename);
void cel_free_sink(struct cel_config *sink);

#endif /* _CUSTOM_COMMON_H */
