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
	void *ptr;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sample_test";
		info->category = "/main/sample/";
		info->summary = "sample unit test";
		info->description =
			"This demonstrates what is required to implement "
			"a unit test.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing sample test...\n");

	if (!(ptr = ast_malloc(8))) {
		ast_test_status_update(test, "ast_malloc() failed\n");
		return AST_TEST_FAIL;
	}

	ast_free(ptr);

	return AST_TEST_PASS;
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
