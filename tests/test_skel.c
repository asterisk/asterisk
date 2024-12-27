/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<Your Email Here>>
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
 * \brief Skeleton Test
 *
 * \author\verbatim <Your Name Here> <<Your Email Here>> \endverbatim
 *
 * This is a skeleton for development of an Asterisk test module
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

AST_TEST_DEFINE(sample_test)
{
	/* Retrieve the command line arguments used to invoke the test */
	struct ast_cli_args *cli_args = ast_test_get_cli_args(test);
	/* Set default values for the options */
	int test_option = 999;
	char test_option2[128] = { 0 };
	void *ptr = NULL;
	void *ptr2 = NULL;
	int i;
	enum ast_test_result_state rc = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sample_test";
		info->category = "/main/sample/";
		info->summary = "sample unit test";
		info->description =
			"This demonstrates what is required to implement "
			"a unit test.  You can pass in test-option and "
			"test-option2 as command line arguments to this "
			"test.  test-option is an integer and test-option2 "
			"is a string.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/*
	 * This is an example of how to get command line arguments
	 * from the test framework.  The arguments are "test-option"
	 * (expected to be an integer) and "test-option2" (expected
	 * to be a string).
	 *
	 * NOTES:
	 *
	 * cli_args will contain all of the command line arguments
	 * including "test execute", etc. so the location of the options
	 * will vary depending on how the test was invoked.
	 * For instance, this test could be run by either of the following:
	 *
	 * test execute category /main/sample/ options test-option=444
	 * test execute category /main/sample/ name sample_test options test-option=444
	 *
	 * You therefore need to test each of the items in the argv array
	 * to find the ones you are looking for.
	 *
	 * No special processing is done on string arguments so if your
	 * option value is a string, you must deal with the possibility
	 * of embedded spaces yourself.
	 */

	for (i = 0; i < cli_args->argc; i++) {
		ast_test_status_update(test, "Test argument: %d: %s\n", i, cli_args->argv[i]);
		if (ast_begins_with(cli_args->argv[i], "test-option=")) {
			sscanf(cli_args->argv[i], "test-option=%d", &test_option);
		}
		if (ast_begins_with(cli_args->argv[i], "test-option2=")) {
			sscanf(cli_args->argv[i], "test-option2=%s", test_option2);
		}
	}

	ast_test_status_update(test, "Executing sample test with test-option=%d and test-option2=%s\n",
		test_option, test_option2);

	if (!(ptr = ast_malloc(8))) {
		ast_test_status_update(test, "ast_malloc() failed\n");
		return AST_TEST_FAIL;
	}

	ptr2 = ast_malloc(8);
	/*
	 * This is an example of how to use the ast_test_validate_cleanup_custom
	 * macro to check a condition and cleanup if it fails.
	 * If ptr2 is NULL, rc will be set to AST_TEST_FAIL, the specified
	 * message will be printed, and the test will jump to the "done"
	 * label to perform cleanup.
	 */
	ast_test_validate_cleanup_custom(test, ptr2, rc, done, "ptr2 is NULL\n");

done:

	ast_free(ptr);
	ast_free(ptr2);

	return rc;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(sample_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(sample_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Skeleton (sample) Test");
