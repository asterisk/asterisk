/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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
 * \brief Conversions Unit Tests
 *
 * \author Kevin Harwell <kharwell@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/conversions.h"

#define CATEGORY "/main/conversions/"

AST_TEST_DEFINE(str_to_int)
{
	const char *invalid = "abc";
	const char *invalid_partial = "7abc";
	const char *negative = "-7";
	const char *negative_spaces = "  -7";
	const char *negative_out_of_range = "-9999999999";
	const char *out_of_range = "9999999999";
	const char *spaces = "  ";
	const char *valid = "7";
	const char *valid_spaces = "  7";
	const char *valid_decimal = "08";
	int val;
	char str[64];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "convert a string to a signed integer";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_str_to_int(NULL, &val));
	ast_test_validate(test, ast_str_to_int("\0", &val));
	ast_test_validate(test, ast_str_to_int(invalid, &val));
	ast_test_validate(test, ast_str_to_int(invalid_partial, &val));
	ast_test_validate(test, !ast_str_to_int(negative, &val));
	ast_test_validate(test, !ast_str_to_int(negative_spaces, &val));
	ast_test_validate(test, ast_str_to_int(negative_out_of_range, &val));
	ast_test_validate(test, ast_str_to_int(out_of_range, &val));
	ast_test_validate(test, ast_str_to_int(spaces, &val));
	ast_test_validate(test, !ast_str_to_int(valid, &val));
	ast_test_validate(test, !ast_str_to_int(valid_spaces, &val));
	ast_test_validate(test, !ast_str_to_int(valid_decimal, &val));

	ast_test_validate(test, snprintf(str, sizeof(str), "%d", INT_MAX) > 0);
	ast_test_validate(test, !ast_str_to_int(str, &val));
	ast_test_validate(test, val == INT_MAX);

	ast_test_validate(test, snprintf(str, sizeof(str), "%d", INT_MIN) > 0);
	ast_test_validate(test, !ast_str_to_int(str, &val));
	ast_test_validate(test, val == INT_MIN);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(str_to_uint)
{
	const char *invalid = "abc";
	const char *invalid_partial = "7abc";
	const char *negative = "-7";
	const char *negative_spaces = "  -7";
	const char *out_of_range = "9999999999";
	const char *spaces = "  ";
	const char *valid = "7";
	const char *valid_spaces = "  7";
	const char *valid_decimal = "08";
	unsigned int val;
	char str[64];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "convert a string to an unsigned integer";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_str_to_uint(NULL, &val));
	ast_test_validate(test, ast_str_to_uint("\0", &val));
	ast_test_validate(test, ast_str_to_uint(invalid, &val));
	ast_test_validate(test, ast_str_to_uint(invalid_partial, &val));
	ast_test_validate(test, ast_str_to_uint(negative, &val));
	ast_test_validate(test, ast_str_to_uint(negative_spaces, &val));
	ast_test_validate(test, ast_str_to_uint(out_of_range, &val));
	ast_test_validate(test, ast_str_to_uint(spaces, &val));
	ast_test_validate(test, !ast_str_to_uint(valid, &val));
	ast_test_validate(test, !ast_str_to_uint(valid_spaces, &val));
	ast_test_validate(test, !ast_str_to_uint(valid_decimal, &val));

	ast_test_validate(test, snprintf(str, sizeof(str), "%u", UINT_MAX) > 0);
	ast_test_validate(test, !ast_str_to_uint(str, &val));
	ast_test_validate(test, val == UINT_MAX);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(str_to_long)
{
	const char *invalid = "abc";
	const char *invalid_partial = "7abc";
	const char *negative = "-7";
	const char *negative_spaces = "  -7";
	const char *negative_out_of_range = "-99999999999999999999";
	const char *out_of_range = "99999999999999999999";
	const char *spaces = "  ";
	const char *valid = "7";
	const char *valid_spaces = "  7";
	const char *valid_decimal = "08";
	long val;
	char str[64];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "convert a string to a signed long";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_str_to_long(NULL, &val));
	ast_test_validate(test, ast_str_to_long("\0", &val));
	ast_test_validate(test, ast_str_to_long(invalid, &val));
	ast_test_validate(test, ast_str_to_long(invalid_partial, &val));
	ast_test_validate(test, !ast_str_to_long(negative, &val));
	ast_test_validate(test, !ast_str_to_long(negative_spaces, &val));
	ast_test_validate(test, ast_str_to_long(negative_out_of_range, &val));
	ast_test_validate(test, ast_str_to_long(out_of_range, &val));
	ast_test_validate(test, ast_str_to_long(spaces, &val));
	ast_test_validate(test, !ast_str_to_long(valid, &val));
	ast_test_validate(test, !ast_str_to_long(valid_spaces, &val));
	ast_test_validate(test, !ast_str_to_long(valid_decimal, &val));

	ast_test_validate(test, snprintf(str, sizeof(str), "%ld", LONG_MAX) > 0);
	ast_test_validate(test, !ast_str_to_long(str, &val));
	ast_test_validate(test, val == LONG_MAX);

	ast_test_validate(test, snprintf(str, sizeof(str), "%ld", LONG_MIN) > 0);
	ast_test_validate(test, !ast_str_to_long(str, &val));
	ast_test_validate(test, val == LONG_MIN);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(str_to_ulong)
{
	const char *invalid = "abc";
	const char *invalid_partial = "7abc";
	const char *negative = "-7";
	const char *negative_spaces = "  -7";
	const char *out_of_range = "99999999999999999999";
	const char *spaces = "  ";
	const char *valid = "7";
	const char *valid_spaces = "  7";
	const char *valid_decimal = "08";
	unsigned long val;
	char str[64];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "convert a string to an unsigned long";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_str_to_ulong(NULL, &val));
	ast_test_validate(test, ast_str_to_ulong("\0", &val));
	ast_test_validate(test, ast_str_to_ulong(invalid, &val));
	ast_test_validate(test, ast_str_to_ulong(invalid_partial, &val));
	ast_test_validate(test, ast_str_to_ulong(negative, &val));
	ast_test_validate(test, ast_str_to_ulong(negative_spaces, &val));
	ast_test_validate(test, ast_str_to_ulong(out_of_range, &val));
	ast_test_validate(test, ast_str_to_ulong(spaces, &val));
	ast_test_validate(test, !ast_str_to_ulong(valid, &val));
	ast_test_validate(test, !ast_str_to_ulong(valid_spaces, &val));
	ast_test_validate(test, !ast_str_to_ulong(valid_decimal, &val));

	ast_test_validate(test, snprintf(str, sizeof(str), "%lu", ULONG_MAX) > 0);
	ast_test_validate(test, !ast_str_to_ulong(str, &val));
	ast_test_validate(test, val == ULONG_MAX);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(str_to_imax)
{
	const char *invalid = "abc";
	const char *invalid_partial = "7abc";
	const char *negative = "-7";
	const char *negative_spaces = "  -7";
	const char *negative_out_of_range = "-99999999999999999999999999999999999999999999999999";
	const char *out_of_range = "99999999999999999999999999999999999999999999999999";
	const char *spaces = "  ";
	const char *valid = "7";
	const char *valid_spaces = "  7";
	const char *valid_decimal = "08";
	intmax_t val;
	char str[64];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "convert a string to a signed max size integer";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_str_to_imax(NULL, &val));
	ast_test_validate(test, ast_str_to_imax("\0", &val));
	ast_test_validate(test, ast_str_to_imax(invalid, &val));
	ast_test_validate(test, ast_str_to_imax(invalid_partial, &val));
	ast_test_validate(test, !ast_str_to_imax(negative, &val));
	ast_test_validate(test, !ast_str_to_imax(negative_spaces, &val));
	ast_test_validate(test, ast_str_to_imax(negative_out_of_range, &val));
	ast_test_validate(test, ast_str_to_imax(out_of_range, &val));
	ast_test_validate(test, ast_str_to_imax(spaces, &val));
	ast_test_validate(test, !ast_str_to_imax(valid, &val));
	ast_test_validate(test, !ast_str_to_imax(valid_spaces, &val));
	ast_test_validate(test, !ast_str_to_imax(valid_decimal, &val));

	ast_test_validate(test, snprintf(str, sizeof(str), "%jd", INTMAX_MAX) > 0);
	ast_test_validate(test, !ast_str_to_imax(str, &val));
	ast_test_validate(test, val == INTMAX_MAX);

	ast_test_validate(test, snprintf(str, sizeof(str), "%jd", INTMAX_MIN) > 0);
	ast_test_validate(test, !ast_str_to_imax(str, &val));
	ast_test_validate(test, val == INTMAX_MIN);

	return AST_TEST_PASS;
}


AST_TEST_DEFINE(str_to_umax)
{
	const char *invalid = "abc";
	const char *invalid_partial = "7abc";
	const char *negative = "-7";
	const char *negative_spaces = "  -7";
	const char *out_of_range = "99999999999999999999999999999999999999999999999999";
	const char *spaces = "  ";
	const char *valid = "7";
	const char *valid_spaces = "  7";
	const char *valid_decimal = "08";
	uintmax_t val;
	char str[64];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "convert a string to an unsigned max size integer";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_str_to_umax(NULL, &val));
	ast_test_validate(test, ast_str_to_umax("\0", &val));
	ast_test_validate(test, ast_str_to_umax(invalid, &val));
	ast_test_validate(test, ast_str_to_umax(invalid_partial, &val));
	ast_test_validate(test, ast_str_to_umax(negative, &val));
	ast_test_validate(test, ast_str_to_umax(negative_spaces, &val));
	ast_test_validate(test, ast_str_to_umax(out_of_range, &val));
	ast_test_validate(test, ast_str_to_umax(spaces, &val));
	ast_test_validate(test, !ast_str_to_umax(valid, &val));
	ast_test_validate(test, !ast_str_to_umax(valid_spaces, &val));
	ast_test_validate(test, !ast_str_to_umax(valid_decimal, &val));

	ast_test_validate(test, snprintf(str, sizeof(str), "%ju", UINTMAX_MAX) > 0);
	ast_test_validate(test, !ast_str_to_umax(str, &val));
	ast_test_validate(test, val == UINTMAX_MAX);

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(str_to_int);
	AST_TEST_REGISTER(str_to_uint);
	AST_TEST_REGISTER(str_to_long);
	AST_TEST_REGISTER(str_to_ulong);
	AST_TEST_REGISTER(str_to_imax);
	AST_TEST_REGISTER(str_to_umax);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(str_to_int);
	AST_TEST_UNREGISTER(str_to_uint);
	AST_TEST_UNREGISTER(str_to_long);
	AST_TEST_UNREGISTER(str_to_ulong);
	AST_TEST_UNREGISTER(str_to_imax);
	AST_TEST_UNREGISTER(str_to_umax);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Conversions test module");
