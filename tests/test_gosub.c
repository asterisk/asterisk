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
 * \brief Gosub tests
 *
 * \author\verbatim Tilghman Lesher <tlesher AT digium DOT com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"

AST_TEST_DEFINE(test_gosub)
{
#define CONTEXT_NAME "tests_test_gosub_virtual_context"
	int res = AST_TEST_PASS, i;
	struct ast_channel *chan;
	struct ast_str *str;
	struct testplan {
		const char *app;
		const char *args;
		const char *expected_value;
	} testplan[] = {
		{ NULL, "${STACK_PEEK(1,e,1)}", "" },         /* Stack is empty */
		{ "Gosub", "tests_test_gosub_virtual_context,s,1" },
		{ NULL, "${PRIORITY}", "1" },
		{ NULL, "${EXTEN}", "s" },
		{ NULL, "${STACK_PEEK(1,e,1)}", "" },         /* No extension originally */
		{ "Gosub", "test,dne,1", (const char *) -1 }, /* This is the only invocation that should fail. */
		{ NULL, "${PRIORITY}", "1" },
		{ NULL, "${EXTEN}", "s" },
		{ "Gosub", "tests_test_gosub_virtual_context,s,1(5,5,5,5,5)" },
		{ NULL, "${PRIORITY}", "1" },
		{ NULL, "$[0${ARG1} + 0${ARG5}]", "10" },
		{ NULL, "${STACK_PEEK(1,e)}", "s" },
		{ NULL, "${STACK_PEEK(1,c)}", "tests_test_gosub_virtual_context" },
		{ NULL, "${STACK_PEEK(1,p)}", "1" },
		{ NULL, "${STACK_PEEK(1,l)}", "tests_test_gosub_virtual_context,s,1" },
		{ "StackPop", "" },
		{ NULL, "${STACK_PEEK(1,e,1)}", "" },         /* Only 1 frame deep, my caller is top-level */
		{ "Gosub", "tests_test_gosub_virtual_context,s,1(5,5,5,5,5)" },
		{ "Gosub", "tests_test_gosub_virtual_context,s,1(4,4,4,4)" },
		{ NULL, "$[0${ARG1} + 0${ARG5}]", "4" },
		{ NULL, "$[0${ARG1} + 0${ARG4}]", "8" },
		{ "Gosub", "tests_test_gosub_virtual_context,s,1(3,3,3)" },
		{ NULL, "$[0${ARG1} + 0${ARG4}]", "3" },
		{ NULL, "$[0${ARG1} + 0${ARG3}]", "6" },
		{ "Gosub", "tests_test_gosub_virtual_context,s,1(2,2)" },
		{ NULL, "$[0${ARG1} + 0${ARG3}]", "2" },
		{ NULL, "$[0${ARG1} + 0${ARG2}]", "4" },
		{ "Gosub", "tests_test_gosub_virtual_context,s,1(1)" },
		{ NULL, "$[0${ARG1} + 0${ARG2}]", "1" },
		{ NULL, "$[0${ARG1} + 0${ARG1}]", "2" },
		{ "Gosub", "tests_test_gosub_virtual_context,s,1" },
		{ NULL, "$[0${ARG1} + 0${ARG1}]", "0" }, /* All arguments are correctly masked */
		{ "Set", "LOCAL(foo)=5" },
		{ NULL, "${foo}", "5" },                 /* LOCAL() set a variable correctly */
		{ NULL, "${LOCAL_PEEK(0,ARG1)}", "" },   /* LOCAL_PEEK() arguments work correctly */
		{ NULL, "${LOCAL_PEEK(4,ARG1)}", "4" },  /* LOCAL_PEEK() arguments work correctly */
		{ NULL, "$[0${LOCAL_PEEK(3,ARG1)} + 0${LOCAL_PEEK(5,ARG1)}]", "8" },
		{ "StackPop", "" },
		{ NULL, "${foo}", "" },                  /* StackPop removed the variable set with LOCAL() */
		{ "Return", "7" },
		{ NULL, "${GOSUB_RETVAL}", "7" },              /* Return sets a return value correctly */
		{ NULL, "$[0${GOSUB_RETVAL} + 0${ARG1}]", "9" }, /* Two frames less means ARG1 should have 2 */
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "gosub application";
		info->category = "/apps/app_gosub/";
		info->summary = "Verify functionality of gosub application";
		info->description =
			"Verify functionality of gosub application";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(chan = ast_dummy_channel_alloc())) {
		ast_test_status_update(test, "Unable to allocate dummy channel\n");
		return AST_TEST_FAIL;
	}

	if (!(str = ast_str_create(16))) {
		ast_test_status_update(test, "Unable to allocate dynamic string buffer\n");
		ast_channel_unref(chan);
		return AST_TEST_FAIL;
	}

	/* Create our test dialplan */
	if (!ast_context_find_or_create(NULL, NULL, CONTEXT_NAME, "test_gosub")) {
		ast_test_status_update(test, "Unable to create test dialplan context");
		ast_free(str);
		ast_channel_unref(chan);
		return AST_TEST_FAIL;
	}

	ast_add_extension(CONTEXT_NAME, 1, "s", 1, NULL, NULL, "NoOp", ast_strdup(""), ast_free_ptr, "test_gosub");

	for (i = 0; i < ARRAY_LEN(testplan); i++) {
		if (testplan[i].app == NULL) {
			/* Evaluation */
			ast_str_substitute_variables(&str, 0, chan, testplan[i].args);
			if (strcmp(ast_str_buffer(str), testplan[i].expected_value)) {
				ast_test_status_update(test, "Evaluation of '%s' returned '%s' instead of the expected value '%s'\n",
					testplan[i].args, ast_str_buffer(str), testplan[i].expected_value);
				res = AST_TEST_FAIL;
			}
		} else {
			/* Run application */
			intptr_t exec_res;
			struct ast_app *app = pbx_findapp(testplan[i].app);
			if (!app) {
				ast_test_status_update(test, "Could not find '%s' in application listing!\n", testplan[i].app);
				res = AST_TEST_FAIL;
				break;
			}

			if ((exec_res = pbx_exec(chan, app, testplan[i].args)) && ((const char *) exec_res != testplan[i].expected_value)) {
				ast_test_status_update(test, "Application '%s' exited abnormally (with code %d)\n", testplan[i].app, (int) exec_res);
				res = AST_TEST_FAIL;
				break;
			}
		}
	}

	ast_free(str);
	ast_channel_unref(chan);
	ast_context_remove_extension(CONTEXT_NAME, "s", 1, NULL);
	ast_context_destroy(NULL, "test_gosub");

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_gosub);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_gosub);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Gosub Tests");
