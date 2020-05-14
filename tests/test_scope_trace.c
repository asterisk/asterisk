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

static void test_scope2(void)
{
	SCOPE_TRACE(1);
}

static void test_scope(void)
{
	SCOPE_TRACE(1, "nested function: %d * %d = %d\n", 6, 7, (6 * 7));

	test_scope2();

	ast_trace(1, "test no variables\n");
}


AST_TEST_DEFINE(scope_test)
{
	SCOPE_TRACE(1, "top %s function\n", "scope_test");

	ast_trace(1, "%s\n", "test outer");

	switch (cmd) {
	case TEST_INIT:
		info->name = "scope_test";
		info->category = "/main/logging/";
		info->summary = "Scope Trace Tests";
		info->description = "Scope Trace Tests";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		{
			SCOPE_TRACE(1, "CASE statement\n");
			ast_trace(1, "%s\n", "test case");
		}
		break;
	}

	if (1) {
		SCOPE_TRACE(1, "IF block\n");

		test_scope();
	}

	ast_trace(1);

	ast_trace(1, "test no variables\n");




	ast_trace(1, "%s\n", "test variable");

 	return AST_TEST_PASS;
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
