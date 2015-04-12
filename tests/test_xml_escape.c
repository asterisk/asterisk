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

/*!
 * \file
 * \brief Test ast_xml_escape
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

static enum ast_test_result_state test_res = AST_TEST_PASS;

static void test_xml(struct ast_test *test, const char *input, const char *expected, int max_len, int expected_res)
{
	char actual[256] = "";
	int res;

	if (max_len == -1) {
		max_len = sizeof(actual);
	}

	res = ast_xml_escape(input, actual, max_len);
	if (res != expected_res) {
		ast_test_status_update(test, "Expected result '%d', got '%d'\n", expected_res, res);
		test_res = AST_TEST_FAIL;
	}

	if (strcmp(expected, actual) != 0) {
		ast_test_status_update(test, "Expected output '%s', got '%s'\n", expected, actual);
		test_res = AST_TEST_FAIL;
	}
}

AST_TEST_DEFINE(xml_escape_test)
{
	char *input;
	char *expected;

	switch (cmd) {
	case TEST_INIT:
		info->name = "xml_escape_test";
		info->category = "/main/xml_escape/";
		info->summary = "Test XML escaping";
		info->description =
			"Test XML escaping";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_res = AST_TEST_PASS;

	/* happy path */
	input = "encode me: <&>'\"";
	expected = "encode me: &lt;&amp;&gt;&apos;&quot;";
	test_xml(test, input, expected, -1, 0);

	/* size 0 should fail without changing anything */
	input = "foo";
	expected = "";
	test_xml(test, input, expected, 0, -1);

	/* truncate chars */
	input = "<truncated>";
	expected = "&lt;trunc";
	test_xml(test, input, expected, 10, -1);

	/* truncate entity */
	input = "trunc<";
	expected = "trunc";
	test_xml(test, input, expected, 9, -1);

	return test_res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(xml_escape_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(xml_escape_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Skeleton (sample) Test");
