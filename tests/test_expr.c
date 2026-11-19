/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Tilghman Lesher <tlesher AT digium DOT com>
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
 * \brief Expression Tests
 *
 * \author\verbatim Tilghman Lesher <tlesher AT digium DOT com> \endverbatim
 *
 * Verify that the expression parser works as intended.
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/ast_expr.h"

AST_TEST_DEFINE(expr_test)
{
	int res = AST_TEST_PASS, i, len;
	struct {
		char *input;
		const char *output;
	} tests[] = {
		{ "2 + 2", "4" },
		{ "      2     +       2            ", "4" },
		{ "", "" },
		{ "2 - 4", "-2" },
		{ "4 - 2", "2" },
		{ "-4 - -2", "-2" },
		{ "4 + 2 * 8", "20" },
		{ "(4 + 2) * 8", "48" },
		{ "4 + (2 * 8)", "20" },
		{ "4 + (2 * 8) ? 3 :: 6", "3" },
		{ "4 + 8 / 2", "8" },
		{ "FLOOR(4 + 8 / 3)", "6" }, /* Floating point op on 1.6 and higher, need FLOOR() to keep result sane */
		{ "(4+8) / 3", "4" },
		{ "4 + 8 % 3", "6" },
		{ "4 + 9 % 3", "4" },
		{ "(4+9) %3", "1" },
		{ "(4+8) %3", "0" },
		{ "(4+9) %3", "1" },
		{ "(4+8) %3", "0" },
		{ "(4+9) % 3", "1" },
		{ "(4+8) % 3", "0" },
		{ "(4+9) % 3", "1" },
		{ "(4+8) % 3", "0" },
		{ "(4+9)% 3", "1" },
		{ "(4+8)% 3", "0" },
		{ "(4+9)% 3", "1" },
		{ "(4+8)% 3", "0" },
		{ "4 & 4", "4" },
		{ "0 & 4", "0" },
		{ "0 & 0", "0" },
		{ "2 | 0", "2" },
		{ "2 | 4", "2" },
		{ "0 | 0", "0" },
		{ "!0 | 0", "1" },
		{ "!4 | 0", "0" },
		{ "4 | !0", "4" },
		{ "!4 | !0", "1" },
		{ "0", "0" },
		{ "!0", "1" },
		{ "00", "00" },
		{ "!00", "1" },
		{ "1", "1" },
		{ "!1", "0" },
		{ "01", "01" },
		{ "!01", "0" },
		{ "3 < 4", "1" },
		{ "4 < 3", "0" },
		{ "3 > 4", "0" },
		{ "4 > 3", "1" },
		{ "3 = 3", "1" },
		{ "3 = 4", "0" },
		{ "3 != 3", "0" },
		{ "3 != 4", "1" },
		{ "3 >= 4", "0" },
		{ "3 >= 3", "1" },
		{ "4 >= 3", "1" },
		{ "3 <= 4", "1" },
		{ "4 <= 3", "0" },
		{ "4 <= 4", "1" },
		{ "3 > 4 & 4 < 3", "0" },
		{ "4 > 3 & 3 < 4", "1" },
		{ "x = x", "1" },
		{ "y = x", "0" },
		{ "x != y", "1" },
		{ "x != x", "0" },
		{ "\"Something interesting\" =~ interesting", "11" },
		{ "\"Something interesting\" =~ Something", "9" },
		{ "\"Something interesting\" : Something", "9" },
		{ "\"Something interesting\" : interesting", "0" },
		{ "\"Something interesting\" =~ \"interesting\"", "11" },
		{ "\"Something interesting\" =~ \"Something\"", "9" },
		{ "\"Something interesting\" : \"Something\"", "9" },
		{ "\"Something interesting\" : \"interesting\"", "0" },
		{ "\"Something interesting\" =~ (interesting)", "11" },
		{ "\"Something interesting\" =~ (Something)", "9" },
		{ "\"Something interesting\" : (Something)", "9" },
		{ "\"Something interesting\" : (interesting)", "0" },
		{ "\"Something interesting\" =~ \"\\(interesting\\)\"", "0" },
		{ "\"Something interesting\" =~ \"\\(Something\\)\"", "0" },
		{ "\"Something interesting\" : \"\\(Something\\)\"", "0" },
		{ "\"Something interesting\" : \"\\(interesting\\)\"", "0" },
		{ "\"011043567857575\" : \"011\\(..\\)\"", "0" },
		{ "\"9011043567857575\" : \"011\\(..\\)\"", "0" },
		{ "\"011043567857575\" =~ \"011\\(..\\)\"", "0" },
		{ "\"9011043567857575\" =~ \"011\\(..\\)\"", "0" },
		{ "\"Something interesting\" =~ (interesting)", "11" },
		{ "\"Something interesting\" =~ (Something)", "9" },
		{ "\"Something interesting\" : (Something)", "9" },
		{ "\"Something interesting\" : (interesting)", "0" },
		{ "\"Something interesting\" =~ \"(interesting)\"", "interesting" },
		{ "\"Something interesting\" =~ \"(Something)\"", "Something" },
		{ "\"Something interesting\" : \"(Something)\"", "Something" },
		{ "\"Something interesting\" : \"(interesting)\"", "" },
		{ "\"011043567857575\" : \"011(..)\"", "04" },
		{ "\"9011043567857575\" : \"011(..)\"", "" },
		{ "\"011043567857575\" =~ \"011(..)\"", "04" },
		{ "\"9011043567857575\" =~ \"011(..)\"", "04" },
		{ "3", "3" },
		{ "something", "something" },
		{ "043", "043" },
		{ "${GLOBAL(ULKOPREFIX)}9${x}", "${GLOBAL(ULKOPREFIX)}9${x}" },
		{ "512059${x}", "512059${x}" },
		/* A syntax error must evaluate to "0", not to the leading fragment */
		{ "\"a\" b == \"c\"", "0" },
		{ "hello world == foo", "0" },
		{ "\"{\"k\":\"v\"}\"==\"unknown\"", "0" },
		{ "2 + 2 junk", "0" },
		{ "== \"c\"", "0" },
	};
	char buf[32];

	switch (cmd) {
	case TEST_INIT:
		info->name = "expr_test";
		info->category = "/main/ast_expr/";
		info->summary = "unit test for the internal expression engine";
		info->description =
			"Verifies behavior for the internal expression engine";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		memset(buf, 0, sizeof(buf));
		len = ast_expr(tests[i].input, buf, sizeof(buf), NULL);
		buf[len] = '\0';
		if (strcmp(buf, tests[i].output)) {
			ast_test_status_update(test, "Case %d: expression '%s' evaluated as '%s', but should have evaluated as '%s'\n", i + 1, tests[i].input, buf, tests[i].output);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(expr_str_test)
{
	int res = AST_TEST_PASS;
	int i;
	struct ast_str *str;
	struct {
		char *input;
		const char *output;
	} tests[] = {
		{ "2 + 2", "4" },
		{ "x = x", "1" },
		{ "", "" },
		/* A syntax error must evaluate to "0", not to the leading fragment */
		{ "\"a\" b == \"c\"", "0" },
		{ "hello world == foo", "0" },
		{ "2 + 2 junk", "0" },
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "expr_str_test";
		info->category = "/main/ast_expr/";
		info->summary = "unit test for ast_str_expr()";
		info->description =
			"Verifies that ast_str_expr() evaluates expressions like ast_expr(), "
			"including returning \"0\" for an expression that fails to parse";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	str = ast_str_create(32);
	if (!str) {
		return AST_TEST_FAIL;
	}

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		ast_str_reset(str);
		ast_str_expr(&str, 0, NULL, tests[i].input);
		if (strcmp(ast_str_buffer(str), tests[i].output)) {
			ast_test_status_update(test, "Case %d: expression '%s' evaluated as '%s', but should have evaluated as '%s'\n", i + 1, tests[i].input, ast_str_buffer(str), tests[i].output);
			res = AST_TEST_FAIL;
		}
	}

	ast_free(str);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(expr_test);
	AST_TEST_UNREGISTER(expr_str_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(expr_test);
	AST_TEST_REGISTER(expr_str_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Expression evaluation tests");
