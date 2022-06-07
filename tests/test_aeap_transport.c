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

#include "asterisk/http.h"
#include "asterisk/test.h"
#include "asterisk/module.h"

#include "../res/res_aeap/transport.h"

#define CATEGORY "/res/aeap/transport/"

#define ADDR "127.0.0.1:8088"
#define TRANSPORT_URL "ws://" ADDR "/ws"
#define TRANSPORT_URL_INVALID "ws://" ADDR "/invalid"
#define TRANSPORT_PROTOCOL "echo"
#define TRANSPORT_PROTOCOL_INVALID "invalid"
#define TRANSPORT_TIMEOUT 2000

AST_TEST_DEFINE(transport_create_invalid)
{
	RAII_VAR(struct aeap_transport *, transport, NULL, aeap_transport_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test creating an AEAP invalid transport type";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Transport is expected to be NULL here */
	ast_test_validate(test, !(transport = aeap_transport_create("invalid")));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(transport_create)
{
	RAII_VAR(struct aeap_transport *, transport, NULL, aeap_transport_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test creating an AEAP transport";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Type is based off the scheme, so just pass in the URL here */
	ast_test_validate(test, (transport = aeap_transport_create(TRANSPORT_URL)));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(transport_connect)
{
	RAII_VAR(struct aeap_transport *, transport, NULL, aeap_transport_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test connecting to an AEAP transport";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Type is based off the scheme, so just pass in the URL for the type */
	ast_test_validate(test, (transport = aeap_transport_create_and_connect(
		TRANSPORT_URL, TRANSPORT_URL, TRANSPORT_PROTOCOL, TRANSPORT_TIMEOUT)));

	ast_test_validate(test, aeap_transport_is_connected(transport));
	ast_test_validate(test, !aeap_transport_disconnect(transport));
	ast_test_validate(test, !aeap_transport_is_connected(transport));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(transport_connect_fail)
{
	RAII_VAR(struct aeap_transport *, transport, NULL, aeap_transport_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test connecting failure for an AEAP transport";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Test invalid address */
	ast_test_validate(test, (transport = aeap_transport_create(TRANSPORT_URL)));

	ast_test_validate(test, aeap_transport_connect(transport,
		TRANSPORT_URL_INVALID, TRANSPORT_PROTOCOL, TRANSPORT_TIMEOUT));

	ast_test_validate(test, !aeap_transport_is_connected(transport));

	/*
	 * The following section of code has been disabled as it may be the cause
	 * of subsequent test failures.
	 *
	 * See ASTERISK-30099 for more information
	 */

	/* aeap_transport_destroy(transport); */

	/* /\* Test invalid protocol *\/ */
	/* ast_test_validate(test, (transport = aeap_transport_create(TRANSPORT_URL))); */

	/* ast_test_validate(test, aeap_transport_connect(transport, */
	/* 	TRANSPORT_URL, TRANSPORT_PROTOCOL_INVALID, TRANSPORT_TIMEOUT)); */

	/* ast_test_validate(test, !aeap_transport_is_connected(transport)); */

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(transport_binary)
{
	RAII_VAR(struct aeap_transport *, transport, NULL, aeap_transport_destroy);
	int num = 38;
	enum AST_AEAP_DATA_TYPE rtype;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test binary I/O from an AEAP transport";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, (transport = aeap_transport_create_and_connect(
		TRANSPORT_URL, TRANSPORT_URL, TRANSPORT_PROTOCOL, TRANSPORT_TIMEOUT)));

	ast_test_validate(test, aeap_transport_write(transport, &num, sizeof(num),
		AST_AEAP_DATA_TYPE_BINARY) == sizeof(num));
	ast_test_validate(test, aeap_transport_read(transport, &num,
		sizeof(num), &rtype) == sizeof(num));
	ast_test_validate(test, rtype == AST_AEAP_DATA_TYPE_BINARY);
	ast_test_validate(test, num == 38);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(transport_string)
{
	RAII_VAR(struct aeap_transport *, transport, NULL, aeap_transport_destroy);
	char buf[16];
	enum AST_AEAP_DATA_TYPE rtype;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test string I/O from an AEAP transport";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, (transport = aeap_transport_create_and_connect(
		TRANSPORT_URL, TRANSPORT_URL, TRANSPORT_PROTOCOL, TRANSPORT_TIMEOUT)));

	ast_test_validate(test, aeap_transport_write(transport, "foo bar baz", 11,
		AST_AEAP_DATA_TYPE_STRING) == 11);
	ast_test_validate(test, aeap_transport_read(transport, buf,
		sizeof(buf) / sizeof(char), &rtype) == 11);
	ast_test_validate(test, rtype == AST_AEAP_DATA_TYPE_STRING);
	ast_test_validate(test, !strcmp(buf, "foo bar baz"));

	return AST_TEST_PASS;
}

static struct ast_http_server *http_server;

static int load_module(void)
{
	if (!(http_server = ast_http_test_server_get("aeap transport http server", NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(transport_string);
	AST_TEST_REGISTER(transport_binary);
	AST_TEST_REGISTER(transport_connect_fail);
	AST_TEST_REGISTER(transport_connect);
	AST_TEST_REGISTER(transport_create);
	AST_TEST_REGISTER(transport_create_invalid);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(transport_string);
	AST_TEST_UNREGISTER(transport_binary);
	AST_TEST_UNREGISTER(transport_connect_fail);
	AST_TEST_UNREGISTER(transport_connect);
	AST_TEST_UNREGISTER(transport_create);
	AST_TEST_UNREGISTER(transport_create_invalid);

	ast_http_test_server_discard(http_server);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk External Application Protocol Transport Tests",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_aeap",
);
