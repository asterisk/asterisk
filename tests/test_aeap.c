/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_aeap</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/file.h"
#include "asterisk/http_websocket.h"
#include "asterisk/json.h"

#include "asterisk/res_aeap.h"
#include "asterisk/res_aeap_message.h"

#define CATEGORY "/res/aeap/"

#define ADDR "127.0.0.1:8088"
#define AEAP_TRANSPORT_TYPE "ws"
#define AEAP_REMOTE_URL "ws://" ADDR "/ws"
#define AEAP_REMOTE_PROTOCOL "echo"
#define AEAP_MESSAGE_ID "foo"
#define AEAP_CONNECTION_TIMEOUT 2000

AST_TEST_DEFINE(create_and_connect)
{
	RAII_VAR(struct ast_aeap *, aeap, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test creating and connecting to an AEAP application";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, (aeap = ast_aeap_create_and_connect(AEAP_TRANSPORT_TYPE,
		NULL, AEAP_REMOTE_URL, AEAP_REMOTE_PROTOCOL, AEAP_CONNECTION_TIMEOUT)));

	return AST_TEST_PASS;
}

static void handle_string(struct ast_aeap *aeap, const char *buf, intmax_t size)
{
	int *passed = ast_aeap_user_data_object_by_id(aeap, AEAP_MESSAGE_ID);

	if (strstr(buf, AEAP_MESSAGE_ID)) {
		++*passed;
	}
}

static void handle_timeout(struct ast_aeap *aeap, struct ast_aeap_message *message, void *data)
{
	int *passed = ast_aeap_user_data_object_by_id(aeap, AEAP_MESSAGE_ID);

	++*passed;
}

AST_TEST_DEFINE(send_msg_handle_string)
{
	int passed = 0;
	RAII_VAR(struct ast_aeap *, aeap, NULL, ao2_cleanup);
	struct ast_aeap_tsx_params tsx_params = {0};
	struct ast_aeap_params aeap_params = {
		.on_string = handle_string,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test an AEAP application string handler";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tsx_params.timeout = 2000; /* Test will end by timing out */
	tsx_params.on_timeout = handle_timeout;
	tsx_params.wait = 1;

	ast_test_validate(test, (aeap = ast_aeap_create_and_connect(AEAP_TRANSPORT_TYPE,
		&aeap_params, AEAP_REMOTE_URL, AEAP_REMOTE_PROTOCOL, AEAP_CONNECTION_TIMEOUT)));

	ast_test_validate(test, (!ast_aeap_user_data_register(aeap, AEAP_MESSAGE_ID, &passed, NULL)));
	ast_test_validate(test, (tsx_params.msg = ast_aeap_message_create_request(
		ast_aeap_message_type_json, "foo", AEAP_MESSAGE_ID, NULL)));
	ast_test_validate(test, ast_aeap_send_msg_tsx(aeap, &tsx_params)); /* Returns fail on timeout */
	ast_aeap_user_data_unregister(aeap, AEAP_MESSAGE_ID);

	return passed == 2 ? AST_TEST_PASS : AST_TEST_FAIL;
}

static int handle_msg(struct ast_aeap *aeap, struct ast_aeap_message *message, void *data)
{
	int *passed = ast_aeap_user_data_object_by_id(aeap, AEAP_MESSAGE_ID);

	*passed = !strcmp(ast_aeap_message_id(message), AEAP_MESSAGE_ID) &&
		ast_aeap_message_is_named(message, data);

	if (!*passed) {
		ast_log(LOG_ERROR, "Name '%s' did not equal '%s' for message '%s'",
			ast_aeap_message_name(message), (char *)data, ast_aeap_message_id(message));
	}

	return 0;
}

static const struct ast_aeap_message_handler handlers[] = {
	{ "foo", handle_msg },
};

AST_TEST_DEFINE(send_msg_handle_response)
{
	int passed = 0;
	RAII_VAR(struct ast_aeap *, aeap, NULL, ao2_cleanup);
	char *name = "foo";
	struct ast_aeap_params aeap_params = {
		.response_handlers = handlers,
		.response_handlers_size = ARRAY_LEN(handlers),
	};
	struct ast_aeap_tsx_params tsx_params = {0};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test an AEAP application response handler";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	aeap_params.msg_type = ast_aeap_message_type_json;

	tsx_params.timeout = 2000;
	tsx_params.wait = 1;
	tsx_params.obj = name;

	ast_test_validate(test, (aeap = ast_aeap_create_and_connect(AEAP_TRANSPORT_TYPE,
		&aeap_params, AEAP_REMOTE_URL, AEAP_REMOTE_PROTOCOL, AEAP_CONNECTION_TIMEOUT)));
	ast_test_validate(test, (!ast_aeap_user_data_register(aeap, AEAP_MESSAGE_ID, &passed, NULL)));
	ast_test_validate(test, (tsx_params.msg = ast_aeap_message_create_response(
		ast_aeap_message_type_json, name, AEAP_MESSAGE_ID, NULL)));
	ast_test_validate(test, !ast_aeap_send_msg_tsx(aeap, &tsx_params));
	ast_aeap_user_data_unregister(aeap, AEAP_MESSAGE_ID);

	return passed ? AST_TEST_PASS : AST_TEST_FAIL;
}

AST_TEST_DEFINE(send_msg_handle_request)
{
	int passed = 0;
	RAII_VAR(struct ast_aeap *, aeap, NULL, ao2_cleanup);
	char *name = "foo";
	struct ast_aeap_params aeap_params = {
		.request_handlers = handlers,
		.request_handlers_size = ARRAY_LEN(handlers),
	};
	struct ast_aeap_tsx_params tsx_params = {0};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test an AEAP application request handler";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	aeap_params.msg_type = ast_aeap_message_type_json;

	tsx_params.timeout = 2000;
	tsx_params.wait = 1;
	tsx_params.obj = name;

	ast_test_validate(test, (aeap = ast_aeap_create_and_connect(AEAP_TRANSPORT_TYPE,
		&aeap_params, AEAP_REMOTE_URL, AEAP_REMOTE_PROTOCOL, AEAP_CONNECTION_TIMEOUT)));
	ast_test_validate(test, (!ast_aeap_user_data_register(aeap, AEAP_MESSAGE_ID, &passed, NULL)));
	ast_test_validate(test, (tsx_params.msg = ast_aeap_message_create_request(
		ast_aeap_message_type_json, name, AEAP_MESSAGE_ID, NULL)));
	ast_test_validate(test, !ast_aeap_send_msg_tsx(aeap, &tsx_params));
	ast_aeap_user_data_unregister(aeap, AEAP_MESSAGE_ID);

	return passed ? AST_TEST_PASS : AST_TEST_FAIL;
}

static struct ast_http_server *http_server;

static int load_module(void)
{
	if (!(http_server = ast_http_test_server_get("aeap transport http server", NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(create_and_connect);
	AST_TEST_REGISTER(send_msg_handle_string);
	AST_TEST_REGISTER(send_msg_handle_response);
	AST_TEST_REGISTER(send_msg_handle_request);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(send_msg_handle_request);
	AST_TEST_UNREGISTER(send_msg_handle_response);
	AST_TEST_UNREGISTER(send_msg_handle_string);
	AST_TEST_UNREGISTER(create_and_connect);

	ast_http_test_server_discard(http_server);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk External Application Protocol Object Tests",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_aeap",
);
