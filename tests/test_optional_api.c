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
 * \brief Test optional API.
 *
 * This tests exercise the underlying implementation functions. Acutal usage
 * won't look anything like this; it would use the wrapper macros.
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>OPTIONAL_API</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/optional_api.h"
#include "asterisk/test.h"

#define CATEGORY "/main/optional_api/"

enum was_called {
	NONE,
	STUB,
	IMPL
};

enum was_called was_called_result;

ast_optional_fn test_optional_ref;

static void test_optional_stub(void)
{
	was_called_result = STUB;
}

static void test_optional_impl(void)
{
	was_called_result = IMPL;
}

static void test_optional(void)
{
	was_called_result = NONE;
	if (test_optional_ref) {
		test_optional_ref();
	}
}

#define SYMNAME "test_option"

AST_TEST_DEFINE(test_provide_first)
{
	enum ast_test_result_state res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test optional API publishing.";
		info->description = "Test optional API publishing.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = AST_TEST_FAIL;
	test_optional_ref = 0;

	ast_optional_api_provide(SYMNAME, test_optional_impl);

	ast_optional_api_use(SYMNAME, &test_optional_ref, test_optional_stub,
		AST_MODULE);

	test_optional();

	if (was_called_result != IMPL) {
		ast_test_status_update(test, "Expected %d, was %u",
			IMPL, was_called_result);
		goto done;
	}

	res = AST_TEST_PASS;

 done:
	ast_optional_api_unuse(SYMNAME, &test_optional_ref, AST_MODULE);
	ast_optional_api_unprovide(SYMNAME, test_optional_impl);
	return res;
}

AST_TEST_DEFINE(test_provide_last)
{
	enum ast_test_result_state res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test optional API publishing.";
		info->description = "Test optional API publishing.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = AST_TEST_FAIL;
	test_optional_ref = 0;

	ast_optional_api_use(SYMNAME, &test_optional_ref, test_optional_stub,
		AST_MODULE);

	test_optional();
	if (was_called_result != STUB) {
		ast_test_status_update(test, "Expected %d, was %u",
			STUB, was_called_result);
		goto done;
	}

	ast_optional_api_provide(SYMNAME, test_optional_impl);

	test_optional();
	if (was_called_result != IMPL) {
		ast_test_status_update(test, "Expected %d, was %u",
			IMPL, was_called_result);
		ast_optional_api_unprovide(SYMNAME, test_optional_impl);
		goto done;
	}

	ast_optional_api_unprovide(SYMNAME, test_optional_impl);

	test_optional();
	if (was_called_result != STUB) {
		ast_test_status_update(test, "Expected %d, was %u",
			STUB, was_called_result);
		ast_optional_api_unprovide(SYMNAME, test_optional_impl);
		goto done;
	}

	res = AST_TEST_PASS;

 done:
	ast_optional_api_unuse(SYMNAME, &test_optional_ref, AST_MODULE);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_provide_first);
	AST_TEST_UNREGISTER(test_provide_last);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_provide_first);
	AST_TEST_REGISTER(test_provide_last);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "ARI testing",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
);
