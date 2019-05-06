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

	ast_test_validate(test, snprintf(str, sizeof(str), "%u", UINT_MAX) > 0);
	ast_test_validate(test, !ast_str_to_uint(str, &val));
	ast_test_validate(test, val == UINT_MAX);

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

	ast_test_validate(test, snprintf(str, sizeof(str), "%lu", ULONG_MAX) > 0);
	ast_test_validate(test, !ast_str_to_ulong(str, &val));
	ast_test_validate(test, val == ULONG_MAX);

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

	ast_test_validate(test, snprintf(str, sizeof(str), "%lu", UINTMAX_MAX) > 0);
	ast_test_validate(test, !ast_str_to_umax(str, &val));
	ast_test_validate(test, val == UINTMAX_MAX);

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(str_to_uint);
	AST_TEST_REGISTER(str_to_ulong);
	AST_TEST_REGISTER(str_to_umax);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(str_to_uint);
	AST_TEST_UNREGISTER(str_to_ulong);
	AST_TEST_UNREGISTER(str_to_umax);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Conversions test module");
