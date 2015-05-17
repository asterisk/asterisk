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
 * \brief Test Stasis Application API.
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_stasis</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/stasis_app.h"

static const char *test_category = "/stasis/res/";

AST_TEST_DEFINE(app_invoke_dne)
{
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test stasis app invocation.";
		info->description = "Test stasis app invocation.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = stasis_app_send("i-am-not-an-app", ast_json_null());
	ast_test_validate(test, -1 == res);

	return AST_TEST_PASS;
}

struct app_data {
	int invocations;
	struct ast_json *messages;
};

static void app_data_dtor(void *obj)
{
	struct app_data *actual = obj;

	ast_json_unref(actual->messages);
	actual->messages = NULL;
}

static struct app_data *app_data_create(void)
{
	struct app_data *res = ao2_alloc(sizeof(struct app_data), app_data_dtor);

	if (!res) {
		return NULL;
	}

	res->messages = ast_json_array_create();
	return res;
}

static void test_handler(void *data, const char *app_name, struct ast_json *message)
{
	struct app_data *actual = data;
	int res;
	++(actual->invocations);
	res = ast_json_array_append(actual->messages, ast_json_copy(message));
	ast_assert(res == 0);
}

AST_TEST_DEFINE(app_invoke_one)
{
	RAII_VAR(struct app_data *, app_data, NULL, ao2_cleanup);
	RAII_VAR(char *, app_name, NULL, stasis_app_unregister);
	RAII_VAR(struct ast_json *, expected_message, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test stasis app invocation.";
		info->description = "Test stasis app invocation.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	app_name = "test-handler";

	app_data = app_data_create();

	stasis_app_register(app_name, test_handler, app_data);
	message = ast_json_pack("{ s: o }", "test-message", ast_json_null());
	expected_message = ast_json_pack("[o]", ast_json_ref(message));

	res = stasis_app_send(app_name, message);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, 1 == app_data->invocations);
	ast_test_validate(test, ast_json_equal(expected_message, app_data->messages));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(app_replaced)
{
	RAII_VAR(struct app_data *, app_data1, NULL, ao2_cleanup);
	RAII_VAR(struct app_data *, app_data2, NULL, ao2_cleanup);
	RAII_VAR(char *, app_name, NULL, stasis_app_unregister);
	RAII_VAR(struct ast_json *, expected_message1, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected_message2, NULL, ast_json_unref);
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test stasis app invocation.";
		info->description = "Test stasis app invocation.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	app_name = "test-handler";

	app_data1 = app_data_create();
	app_data2 = app_data_create();

	stasis_app_register(app_name, test_handler, app_data1);
	stasis_app_register(app_name, test_handler, app_data2);
	expected_message1 = ast_json_pack("[{s: s, s: s}]",
		"type", "ApplicationReplaced",
		"application", app_name);
	message = ast_json_pack("{ s: o }", "test-message", ast_json_null());
	expected_message2 = ast_json_pack("[o]", ast_json_ref(message));

	res = stasis_app_send(app_name, message);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, 1 == app_data1->invocations);
	ast_test_validate(test, ast_json_equal(expected_message1, app_data1->messages));
	ast_test_validate(test, 1 == app_data2->invocations);
	ast_test_validate(test, ast_json_equal(expected_message2, app_data2->messages));

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(app_invoke_dne);
	AST_TEST_UNREGISTER(app_invoke_one);
	AST_TEST_UNREGISTER(app_replaced);
	stasis_app_unref();
	return 0;
}

static int load_module(void)
{
	stasis_app_ref();
	AST_TEST_REGISTER(app_replaced);
	AST_TEST_REGISTER(app_invoke_one);
	AST_TEST_REGISTER(app_invoke_dne);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Stasis Core testing",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis",
);
