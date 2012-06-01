/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Terry Wilson <twilson@digium.com>
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
 * \brief Config-related tests
 *
 * \author Terry Wilson <twilson@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "")

#include <math.h> /* HUGE_VAL */

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/netsock2.h"
#include "asterisk/acl.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"
#include "asterisk/logger.h"

enum {
	EXPECT_FAIL = 0,
	EXPECT_SUCCEED,
};

#define TOOBIG_I32 "2147483649"
#define TOOSMALL_I32 "-2147483649"
#define TOOBIG_U32 "4294967297"
#define TOOSMALL_U32 "-4294967297"
#define DEFAULTVAL 42
#define EPSILON 0.001

#define TEST_PARSE(input, should_succeed, expected_result, flags, result, ...) do {\
	int __res = ast_parse_arg(input, (flags), result, ##__VA_ARGS__); \
	if (!__res == !should_succeed) { \
		ast_test_status_update(test, "ast_parse_arg failed on '%s'. %d/%d\n", input, __res, should_succeed); \
		ret = AST_TEST_FAIL; \
	} else { \
		if (((flags) & PARSE_TYPE) == PARSE_INT32) { \
			int32_t *r = (int32_t *) (void *) result; \
			int32_t e = (int32_t) expected_result; \
			if (*r != e) { \
				ast_test_status_update(test, "ast_parse_arg int32_t failed with %d != %d\n", *r, e); \
				ret = AST_TEST_FAIL; \
			} \
		} else if (((flags) & PARSE_TYPE) == PARSE_UINT32) { \
			uint32_t *r = (uint32_t *) (void *) result; \
			uint32_t e = (uint32_t) expected_result; \
			if (*r != e) { \
				ast_test_status_update(test, "ast_parse_arg uint32_t failed with %u != %u\n", *r, e); \
				ret = AST_TEST_FAIL; \
			} \
		} else if (((flags) & PARSE_TYPE) == PARSE_DOUBLE) { \
			double *r = (double *) (void *) result; \
			double e = (double) expected_result; \
			if (fabs(*r - e) > EPSILON) { \
				ast_test_status_update(test, "ast_parse_arg double failed with %f != %f\n", *r, e); \
				ret = AST_TEST_FAIL; \
			} \
		} \
	} \
	*(result) = DEFAULTVAL; \
} while (0)

AST_TEST_DEFINE(ast_parse_arg_test)
{
	int ret = AST_TEST_PASS;
	int32_t int32_t_val = DEFAULTVAL;
	uint32_t uint32_t_val = DEFAULTVAL;
	double double_val = DEFAULTVAL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_parse_arg";
		info->category = "/config/";
		info->summary = "Test the output of ast_parse_arg";
		info->description =
			"Ensures that ast_parse_arg behaves as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* int32 testing */
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32, &int32_t_val);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32, &int32_t_val);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32, &int32_t_val);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, -200, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, -1, 0);
	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, 0, 122);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, -122, 100);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, -200, 100);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, -1, 0);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, 0, 122);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, -122, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, -200, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, -1, 0);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, 0, 122);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, -122, 100);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, -200, 100);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, -1, 0);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, 0, 122);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, -122, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);

	/* uuint32 testing */
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 1);

	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 122);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);

	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 1);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 122);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 200);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 1);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 122);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 100);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 1);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 122);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);

	TEST_PARSE("   -123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);

	/* double testing */
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE, &double_val);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE, &double_val);
	TEST_PARSE("7.0not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE, &double_val);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("7.0not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);

	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);

	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);

	/* ast_sockaddr_parse is tested extensively in test_netsock2.c and PARSE_ADDR is a very simple wrapper */

	return ret;
}

struct test_item {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(stropt);
	);
	int32_t intopt;
	uint32_t uintopt;
	double doubleopt;
	struct ast_sockaddr sockaddropt;
	int boolopt;
	struct ast_ha *aclopt;
	struct ast_codec_pref codecprefopt;
	struct ast_format_cap *codeccapopt;
	unsigned int customopt:1;
};
struct test_config {
	struct test_item *global;
	struct test_item *global_defaults;
	struct ao2_container *items;
};

static int test_item_hash(const void *obj, const int flags)
{
	const struct test_item *item = obj;
	const char *name = (flags & OBJ_KEY) ? obj : item->name;
	return ast_str_case_hash(name);
}
static int test_item_cmp(void *obj, void *arg, int flags)
{
	struct test_item *one = obj, *two = arg;
	const char *match = (flags & OBJ_KEY) ? arg : two->name;
	return strcasecmp(one->name, match) ? 0 : (CMP_MATCH | CMP_STOP);
}
static void test_item_destructor(void *obj)
{
	struct test_item *item = obj;
	ast_string_field_free_memory(item);
	if (item->codeccapopt) {
		ast_format_cap_destroy(item->codeccapopt);
	}
	if (item->aclopt) {
		ast_free_ha(item->aclopt);
	}
	return;
}
static void *test_item_alloc(const char *cat)
{
	struct test_item *item;
	if (!(item = ao2_alloc(sizeof(*item), test_item_destructor))) {
		return NULL;
	}
	if (ast_string_field_init(item, 128)) {
		ao2_ref(item, -1);
		return NULL;
	}
	if (!(item->codeccapopt = ast_format_cap_alloc())) {
		ao2_ref(item, -1);
		return NULL;
	}
	ast_string_field_set(item, name, cat);
	return item;
}
static void test_config_destructor(void *obj)
{
	struct test_config *cfg = obj;
	ao2_cleanup(cfg->global);
	ao2_cleanup(cfg->global_defaults);
	ao2_cleanup(cfg->items);
}
static void *test_config_alloc(void)
{
	struct test_config *cfg;
	if (!(cfg = ao2_alloc(sizeof(*cfg), test_config_destructor))) {
		goto error;
	}
	if (!(cfg->global = test_item_alloc("global"))) {
		goto error;
	}
	if (!(cfg->global_defaults = test_item_alloc("global_defaults"))) {
		goto error;
	}
	if (!(cfg->items = ao2_container_alloc(1, test_item_hash, test_item_cmp))) {
		goto error;
	}
	return cfg;
error:
	ao2_cleanup(cfg);
	return NULL;
}
static void *test_item_find(struct ao2_container *container, const char *cat)
{
	return ao2_find(container, cat, OBJ_KEY);
}

static int customopt_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct test_item *item = obj;
	if (!strcasecmp(var->name, "customopt")) {
		item->customopt = ast_true(var->value);
	} else {
		return -1;
	}

	return 0;
}

static struct aco_type global = {
	.type = ACO_GLOBAL,
	.item_offset = offsetof(struct test_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^global$",
};
static struct aco_type global_defaults = {
	.type = ACO_GLOBAL,
	.item_offset = offsetof(struct test_config, global_defaults),
	.category_match = ACO_WHITELIST,
	.category = "^global_defaults$",
};
static struct aco_type item = {
	.type = ACO_ITEM,
	.category_match = ACO_BLACKLIST,
	.category = "^(global|global_defaults)$",
	.item_alloc = test_item_alloc,
	.item_find = test_item_find,
	.item_offset = offsetof(struct test_config, items),
};

struct aco_file config_test_conf = {
	.filename = "config_test.conf",
	.types = ACO_TYPES(&global, &global_defaults, &item),
};

static AO2_GLOBAL_OBJ_STATIC(global_obj);
CONFIG_INFO_STANDARD(cfg_info, global_obj, test_config_alloc,
	.files = ACO_FILES(&config_test_conf),
);

AST_TEST_DEFINE(config_options_test)
{
	int res = AST_TEST_PASS, x, error;
	struct test_item defaults = { 0, }, configs = { 0, };
	struct test_item *arr[4];
	struct ast_sockaddr acl_allow = {{ 0, }}, acl_fail = {{ 0, }};
	RAII_VAR(struct test_config *, cfg, NULL, ao2_cleanup);
	RAII_VAR(struct test_item *, item, NULL, ao2_cleanup);
	RAII_VAR(struct test_item *, item_defaults, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "config_options_test";
		info->category = "/config/";
		info->summary = "Config opptions unit test";
		info->description =
			"Tests the Config Options API";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

#define INT_DEFAULT "-2"
#define INT_CONFIG "-1"
#define UINT_DEFAULT "2"
#define UINT_CONFIG "1"
#define DOUBLE_DEFAULT "1.1"
#define DOUBLE_CONFIG "0.1"
#define SOCKADDR_DEFAULT "4.3.2.1:4321"
#define SOCKADDR_CONFIG "1.2.3.4:1234"
#define BOOL_DEFAULT "false"
#define BOOL_CONFIG "true"
#define ACL_DEFAULT NULL
#define ACL_CONFIG_PERMIT "1.2.3.4/32"
#define ACL_CONFIG_DENY "0.0.0.0/0"
#define CODEC_DEFAULT "!all,alaw"
#define CODEC_CONFIG "!all,ulaw,g729"
#define STR_DEFAULT "default"
#define STR_CONFIG "test"
#define CUSTOM_DEFAULT "no"
#define CUSTOM_CONFIG "yes"

	if (aco_info_init(&cfg_info)) {
		ast_test_status_update(test, "Could not init cfg info\n");
		return AST_TEST_FAIL;
	}

	/* Register all options */
	aco_option_register(&cfg_info, "intopt", ACO_EXACT, config_test_conf.types, INT_DEFAULT, OPT_INT_T, 0, FLDSET(struct test_item, intopt));
	aco_option_register(&cfg_info, "uintopt", ACO_EXACT, config_test_conf.types, UINT_DEFAULT, OPT_UINT_T, 0, FLDSET(struct test_item, uintopt));
	aco_option_register(&cfg_info, "doubleopt", ACO_EXACT, config_test_conf.types, DOUBLE_DEFAULT, OPT_DOUBLE_T, 0, FLDSET(struct test_item, doubleopt));
	aco_option_register(&cfg_info, "sockaddropt", ACO_EXACT, config_test_conf.types, SOCKADDR_DEFAULT, OPT_SOCKADDR_T, 0, FLDSET(struct test_item, sockaddropt));
	aco_option_register(&cfg_info, "boolopt", ACO_EXACT, config_test_conf.types, BOOL_DEFAULT, OPT_BOOL_T, 1, FLDSET(struct test_item, boolopt));
	aco_option_register(&cfg_info, "aclpermitopt", ACO_EXACT, config_test_conf.types, ACL_DEFAULT, OPT_ACL_T, 1, FLDSET(struct test_item, aclopt), "permit");
	aco_option_register(&cfg_info, "acldenyopt", ACO_EXACT, config_test_conf.types, ACL_DEFAULT, OPT_ACL_T, 0, FLDSET(struct test_item, aclopt), "deny");
	aco_option_register(&cfg_info, "codecopt", ACO_EXACT, config_test_conf.types, CODEC_DEFAULT, OPT_CODEC_T, 1, FLDSET(struct test_item, codecprefopt, codeccapopt));
	aco_option_register(&cfg_info, "stropt", ACO_EXACT, config_test_conf.types, STR_DEFAULT, OPT_STRINGFIELD_T, 0, STRFLDSET(struct test_item, stropt));
	aco_option_register_custom(&cfg_info, "customopt", ACO_EXACT, config_test_conf.types, CUSTOM_DEFAULT, customopt_handler, 0);

	if (aco_process_config(&cfg_info, 0)) {
		ast_test_status_update(test, "Could not parse config\n");
		return AST_TEST_FAIL;
	}

	ast_parse_arg(INT_DEFAULT, PARSE_INT32, &defaults.intopt);
	ast_parse_arg(INT_CONFIG, PARSE_INT32, &configs.intopt);
	ast_parse_arg(UINT_DEFAULT, PARSE_UINT32, &defaults.uintopt);
	ast_parse_arg(UINT_CONFIG, PARSE_UINT32, &configs.uintopt);
	ast_parse_arg(DOUBLE_DEFAULT, PARSE_DOUBLE, &defaults.doubleopt);
	ast_parse_arg(DOUBLE_CONFIG, PARSE_DOUBLE, &configs.doubleopt);
	ast_parse_arg(SOCKADDR_DEFAULT, PARSE_ADDR, &defaults.sockaddropt);
	ast_parse_arg(SOCKADDR_CONFIG, PARSE_ADDR, &configs.sockaddropt);
	defaults.boolopt = ast_true(BOOL_DEFAULT);
	configs.boolopt = ast_true(BOOL_CONFIG);

	defaults.aclopt = NULL;
	configs.aclopt = ast_append_ha("deny", ACL_CONFIG_DENY, configs.aclopt, &error);
	configs.aclopt = ast_append_ha("permit", ACL_CONFIG_PERMIT, configs.aclopt, &error);
	ast_sockaddr_parse(&acl_allow, "1.2.3.4", PARSE_PORT_FORBID);
	ast_sockaddr_parse(&acl_fail, "1.1.1.1", PARSE_PORT_FORBID);

	defaults.codeccapopt = ast_format_cap_alloc();
	ast_parse_allow_disallow(&defaults.codecprefopt, defaults.codeccapopt, CODEC_DEFAULT, 1);

	configs.codeccapopt = ast_format_cap_alloc();
	ast_parse_allow_disallow(&configs.codecprefopt, configs.codeccapopt, CODEC_CONFIG, 1);

	ast_string_field_init(&defaults, 128);
	ast_string_field_init(&configs, 128);
	ast_string_field_set(&defaults, stropt, STR_DEFAULT);
	ast_string_field_set(&configs, stropt, STR_CONFIG);

	defaults.customopt = ast_true(CUSTOM_DEFAULT);
	configs.customopt = ast_true(CUSTOM_CONFIG);


	cfg = ao2_global_obj_ref(global_obj);
	if (!(item = ao2_find(cfg->items, "item", OBJ_KEY))) {
		ast_test_status_update(test, "could not look up 'item'\n");
		return AST_TEST_FAIL;
	}
	if (!(item_defaults = ao2_find(cfg->items, "item_defaults", OBJ_KEY))) {
		ast_test_status_update(test, "could not look up 'item_defaults'\n");
		return AST_TEST_FAIL;
	}
	arr[0] = cfg->global;
	arr[1] = item;
	arr[2] = cfg->global_defaults;
	arr[3] = item_defaults;
	/* Test global and item against configs, global_defaults and item_defaults against defaults */

#define NOT_EQUAL_FAIL(field)  \
	if (arr[x]->field != control->field) { \
		ast_test_status_update(test, "%s di not match: %d != %d with x = %d\n", #field, arr[x]->field, control->field, x); \
		res = AST_TEST_FAIL; \
	}
	for (x = 0; x < 4; x++) {
		struct test_item *control = x < 2 ? &configs : &defaults;

		NOT_EQUAL_FAIL(intopt);
		NOT_EQUAL_FAIL(uintopt);
		NOT_EQUAL_FAIL(boolopt);
		NOT_EQUAL_FAIL(customopt);
		if (fabs(arr[x]->doubleopt - control->doubleopt) > 0.001) {
			ast_test_status_update(test, "doubleopt did not match: %f vs %f on loop %d\n", arr[x]->doubleopt, control->doubleopt, x);
			res = AST_TEST_FAIL;
		}
		if (ast_sockaddr_cmp(&arr[x]->sockaddropt, &control->sockaddropt)) {
			ast_test_status_update(test, "sockaddr did not match on loop %d\n", x);
			res = AST_TEST_FAIL;
		}
		if (!ast_format_cap_identical(arr[x]->codeccapopt, control->codeccapopt)) {
			char buf1[128], buf2[128];
			ast_getformatname_multiple(buf1, sizeof(buf1), arr[x]->codeccapopt);
			ast_getformatname_multiple(buf2, sizeof(buf2), control->codeccapopt);
			ast_test_status_update(test, "format did not match: '%s' vs '%s' on loop %d\n", buf1, buf2, x);
			res = AST_TEST_FAIL;
		}
		if (strcasecmp(arr[x]->stropt, control->stropt)) {
			ast_test_status_update(test, "stropt did not match: '%s' vs '%s' on loop %d\n", arr[x]->stropt, control->stropt, x);
			res = AST_TEST_FAIL;
		}
		if (arr[x]->aclopt != control->aclopt && (ast_apply_ha(arr[x]->aclopt, &acl_allow) != ast_apply_ha(control->aclopt, &acl_allow) ||
				ast_apply_ha(arr[x]->aclopt, &acl_fail) != ast_apply_ha(control->aclopt, &acl_fail))) {
			ast_test_status_update(test, "acl not match: on loop %d\n", x);
			res = AST_TEST_FAIL;
		}
	}

	ast_free_ha(configs.aclopt);
	ast_format_cap_destroy(defaults.codeccapopt);
	ast_format_cap_destroy(configs.codeccapopt);
	ast_string_field_free_memory(&defaults);
	ast_string_field_free_memory(&configs);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(ast_parse_arg_test);
	AST_TEST_UNREGISTER(config_options_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(ast_parse_arg_test);
	AST_TEST_REGISTER(config_options_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "config test module");
