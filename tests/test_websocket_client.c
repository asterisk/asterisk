/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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
 * \brief Websocket Client Unit Tests
 *
 * \author Kevin Harwell <kharwell@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_http_websocket</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__, "")

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/http_websocket.h"

#define CATEGORY "/res/websocket/"
#define REMOTE_URL "ws://127.0.0.1:8088/ws"

AST_TEST_DEFINE(websocket_client_create_and_connect)
{
	RAII_VAR(struct ast_websocket *, client, NULL, ao2_cleanup);

	enum ast_websocket_result result;
	const char write_buf[] = "this is only a test";
	RAII_VAR(char *, read_buf, NULL, ast_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "test creation and connection of a client websocket";
		info->description = "test creation and connection of a client websocket";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, (client = ast_websocket_client_create(
					 REMOTE_URL, "echo", NULL, &result)));

	ast_test_validate(test, !ast_websocket_write_string(client, write_buf));
	ast_test_validate(test, ast_websocket_read_string(client, &read_buf) > 0);
	ast_test_validate(test, !strcmp(write_buf, read_buf));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(websocket_client_bad_url)
{
	RAII_VAR(struct ast_websocket *, client, NULL, ao2_cleanup);
	enum ast_websocket_result result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "websocket client - test bad url";
		info->description = "pass a bad url and make sure it fails";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, !(client = ast_websocket_client_create(
					  "invalid", NULL, NULL, &result)));
	return AST_TEST_PASS;
}

AST_TEST_DEFINE(websocket_client_unsupported_protocol)
{
	RAII_VAR(struct ast_websocket *, client, NULL, ao2_cleanup);
	enum ast_websocket_result result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "websocket client - unsupported protocol";
		info->description = "fails on an unsupported protocol";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, !(client = ast_websocket_client_create(
					  REMOTE_URL, "unsupported", NULL, &result)));
	return AST_TEST_PASS;
}

AST_TEST_DEFINE(websocket_client_multiple_protocols)
{
	RAII_VAR(struct ast_websocket *, client, NULL, ao2_cleanup);
	const char *accept_protocol;
	enum ast_websocket_result result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "websocket client - test multiple protocols";
		info->description = "test multi-protocol client";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, (client = ast_websocket_client_create(
					 REMOTE_URL, "echo,unsupported", NULL, &result)));

	accept_protocol = ast_websocket_client_accept_protocol(client);
	ast_test_validate(test, accept_protocol && !strcmp(accept_protocol, "echo"));

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(websocket_client_create_and_connect);
	AST_TEST_REGISTER(websocket_client_bad_url);
	AST_TEST_REGISTER(websocket_client_unsupported_protocol);
	AST_TEST_REGISTER(websocket_client_multiple_protocols);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(websocket_client_multiple_protocols);
	AST_TEST_UNREGISTER(websocket_client_unsupported_protocol);
	AST_TEST_UNREGISTER(websocket_client_bad_url);
	AST_TEST_UNREGISTER(websocket_client_create_and_connect);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Websocket client test module");
