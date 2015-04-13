/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Implementation Swagger validators.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "ari/ari_model_validators.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"

#include <regex.h>

/* Regex to match date strings */
static regex_t date_regex;

/* Regex for YYYY-MM-DD */
#define REGEX_YMD "[0-9]{4}-[01][0-9]-[0-3][0-9]"

/* Regex for hh:mm(:ss(.s)); seconds and subseconds optional
 * Handles the probably impossible case of a leap second, too */
#define REGEX_HMS "[0-2][0-9]:[0-5][0-9](:[0-6][0-9](.[0-9]+)?)?"

/* Regex for timezone: (+|-)hh(:mm), with optional colon. */
#define REGEX_TZ "(Z|[-+][0-2][0-9](:?[0-5][0-9])?)"

/* REGEX for ISO 8601, the time specifier optional */
#define ISO8601_PATTERN "^" REGEX_YMD "(T" REGEX_HMS REGEX_TZ ")?$"

static int check_type(struct ast_json *json, enum ast_json_type expected)
{
	enum ast_json_type actual;

	if (!json) {
		ast_log(LOG_ERROR, "Expected type %s, was NULL\n",
			ast_json_typename(expected));
		return 0;
	}

	actual = ast_json_typeof(json);
	if (expected != actual) {
		ast_log(LOG_ERROR, "Expected type %s, was %s\n",
			ast_json_typename(expected), ast_json_typename(actual));
		return 0;
	}
	return 1;
}

static int check_range(intmax_t minval, intmax_t maxval, struct ast_json *json)
{
	intmax_t v;

	if (!check_type(json, AST_JSON_INTEGER)) {
		return 0;
	}

	v = ast_json_integer_get(json);

	if (v < minval || maxval < v) {
		ast_log(LOG_ERROR, "Value out of range. Expected %jd <= %jd <= %jd\n", minval, v, maxval);
		return 0;
	}
	return 1;
}

int ast_ari_validate_void(struct ast_json *json)
{
	return check_type(json, AST_JSON_NULL);
}

int ast_ari_validate_object(struct ast_json *json)
{
	return check_type(json, AST_JSON_OBJECT);
}

int ast_ari_validate_byte(struct ast_json *json)
{
	/* Java bytes are signed, which accounts for great fun for all */
	return check_range(-128, 255, json);
}

int ast_ari_validate_boolean(struct ast_json *json)
{
	enum ast_json_type actual = ast_json_typeof(json);
	switch (actual) {
	case AST_JSON_TRUE:
	case AST_JSON_FALSE:
		return 1;
	default:
		ast_log(LOG_ERROR, "Expected type boolean, was %s\n",
			ast_json_typename(actual));
		return 0;
	}
}

int ast_ari_validate_int(struct ast_json *json)
{
	/* Swagger int's are 32-bit */
	return check_range(-2147483648LL, 2147483647LL, json);
}

int ast_ari_validate_long(struct ast_json *json)
{
	/* All integral values are valid longs. No need for range check. */
	return check_type(json, AST_JSON_INTEGER);
}

int ast_ari_validate_float(struct ast_json *json)
{
	return check_type(json, AST_JSON_REAL);
}

int ast_ari_validate_double(struct ast_json *json)
{
	return check_type(json, AST_JSON_REAL);
}

int ast_ari_validate_string(struct ast_json *json)
{
	return check_type(json, AST_JSON_STRING);
}

int ast_ari_validate_date(struct ast_json *json)
{
	/* Dates are ISO-8601 strings */
	const char *str;
	if (!check_type(json, AST_JSON_STRING)) {
		return 0;
	}
	str = ast_json_string_get(json);
	ast_assert(str != NULL);
	if (regexec(&date_regex, str, 0, NULL, 0) != 0) {
		ast_log(LOG_ERROR, "Date field is malformed: '%s'\n", str);
		return 0;
	}
	return 1;
}

int ast_ari_validate_list(struct ast_json *json, int (*fn)(struct ast_json *))
{
	int res = 1;
	size_t i;

	if (!check_type(json, AST_JSON_ARRAY)) {
		return 0;
	}

	for (i = 0; i < ast_json_array_size(json); ++i) {
		int member_res;
		member_res = fn(ast_json_array_get(json, i));
		if (!member_res) {
			ast_log(LOG_ERROR,
				"Array member %zu failed validation\n", i);
			res = 0;
		}
	}

	return res;
}

static int load_module(void)
{
	int res;
	res = regcomp(&date_regex, ISO8601_PATTERN,
		REG_EXTENDED | REG_ICASE | REG_NOSUB);

	if (res != 0) {
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	regfree(&date_regex);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER | AST_MODFLAG_GLOBAL_SYMBOLS, "ARI Model validators",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
        );
