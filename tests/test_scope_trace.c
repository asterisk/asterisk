/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corp
 *
 * George Joseph <gjoseph@digium.com>
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
 * \brief Test for Scope Trace
 *
 * \author\verbatim George Joseph <gjoseph@digium.com> \endverbatim
 *
 * tests for Scope Trace
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
#include "asterisk/logger.h"


static const char *str_appender(struct ast_str**buf, char *a)
{
	ast_str_append(buf, 0, "<append %s>", a);
	return ast_str_buffer(*buf);
}

static void test_scope_trace(void)
{
	SCOPE_ENTER(1, "subfunction\n");
	SCOPE_EXIT_RTN("got out\n");
}

static int test_scope_enter_function(void)
{
	SCOPE_ENTER(1, "%s %s %s %s %s %s %s\n",
		ast_str_tmp(12, str_appender(&STR_TMP, "str1")),
		ast_str_tmp(12, str_appender(&STR_TMP, "str2")),
		ast_str_tmp(32, str_appender(&STR_TMP, "AAAAAAAAAAAAAAAAAAAAAAAA")),
		ast_str_tmp(12, str_appender(&STR_TMP, "B")),
		"ccccccccccccc",
		ast_str_tmp(12, str_appender(&STR_TMP, "DDDDD")),
		ast_str_tmp(12, str_appender(&STR_TMP, "ww"))
		);

	test_scope_trace();

	SCOPE_EXIT_RTN_VALUE(AST_TEST_PASS, "test no variables\n");
}


AST_TEST_DEFINE(scope_test)
{
	SCOPE_ENTER(1, "top %s function\n", "scope_test");

	ast_trace(1, "%s\n", "test outer");

	switch (cmd) {
	case TEST_INIT:
	{
		SCOPE_ENTER(1, "TEST_INIT\n");
		info->name = "scope_test";
		info->category = "/main/logging/";
		info->summary = "Scope Trace Tests";
		info->description = "Scope Trace Tests";
		/* need to exit the case scope */
		SCOPE_EXIT("TEST_INIT\n");
		/* need to exit the function */
		SCOPE_EXIT_RTN_VALUE(AST_TEST_NOT_RUN, "BYE\n");
	}
	case TEST_EXECUTE:
	{
		SCOPE_ENTER(1, "TEST_EXECUTE\n");
		ast_trace(1, "%s\n", "test execute");
		SCOPE_EXIT_EXPR(break, "TEST_EXECUTE\n");
	}
	default:
		ast_test_status_update(test, "Shouldn't have gotten here\n");
		return AST_TEST_FAIL;
	}

	if (1) {
		SCOPE_TRACE(1, "IF block\n");
		test_scope_enter_function();
	}

	ast_trace(1);
	ast_trace(1, "test no variables\n");
	ast_trace(1, "%s\n", "test variable");

	SCOPE_EXIT_RTN_VALUE(AST_TEST_PASS, "Something: %d\n", AST_TEST_PASS);
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(scope_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(scope_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Scope Trace Test");
