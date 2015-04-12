/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Terry Wilson <twilson@digium.com>
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
 * \brief Netsock2 Unit Tests
 *
 * \author Terry Wilson <twilson@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__, "")

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"
#include "asterisk/logger.h"
#include "asterisk/config.h" /* PARSE_PORT_* */
struct parse_test {
	const char *address;
	int expected_result;
};

AST_TEST_DEFINE(parsing)
{
	int res = AST_TEST_PASS;
	struct parse_test test_vals[] = {
		{ "192.168.1.0", 1 },
		{ "10.255.255.254", 1 },
		{ "172.18.5.4", 1 },
		{ "8.8.4.4", 1 },
		{ "0.0.0.0", 1 },
		{ "127.0.0.1", 1 },
		{ "1.256.3.4", 0 },
		{ "256.0.0.1", 0 },
		{ "1.2.3.4:5060", 1 },
		{ "::ffff:5.6.7.8", 1 },
		{ "fdf8:f53b:82e4::53", 1 },
		{ "fe80::200:5aee:feaa:20a2", 1 },
		{ "2001::1", 1 },
		{ "2001:0000:4136:e378:8000:63bf:3fff:fdd2", 1 },
		{ "2001:0002:6c::430", 1 },
		{ "2001:10:240:ab::a", 1 },
		{ "2002:cb0a:3cdd:1::1", 1 },
		{ "2001:db8:8:4::2", 1 }, /* Documentation only, should never be used */
		{ "ff01:0:0:0:0:0:0:2", 1 }, /* Multicast */
		{ "[fdf8:f53b:82e4::53]", 1 },
		{ "[fe80::200:5aee:feaa:20a2]", 1 },
		{ "[2001::1]", 1 },
		{ "[2001:0000:4136:e378:8000:63bf:3fff:fdd2]:5060", 1 },
		{ "2001:0000:4136:e378:8000:63bf:3fff:fdd2:5060", 0 }, /* port, but no brackets */
		{ "fe80::200::abcd", 0 }, /* multiple zero expansions */
	};

	size_t x;
	struct ast_sockaddr addr = { { 0, 0, } };
	int parse_result;

	switch (cmd) {
	case TEST_INIT:
		info->name = "parsing";
		info->category = "/main/netsock2/";
		info->summary = "netsock2 parsing unit test";
		info->description =
			"Test parsing of IPv4 and IPv6 network addresses";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (x = 0; x < ARRAY_LEN(test_vals); x++) {
		if ((parse_result = ast_sockaddr_parse(&addr, test_vals[x].address, 0)) != test_vals[x].expected_result) {
			ast_test_status_update(test, "On '%s' expected %d but got %d\n", test_vals[x].address, test_vals[x].expected_result, parse_result);
			res = AST_TEST_FAIL;
		}
		if (parse_result) {
			struct ast_sockaddr tmp_addr = { { 0, 0, } };
			const char *tmp;

			tmp = ast_sockaddr_stringify(&addr);
			ast_sockaddr_parse(&tmp_addr, tmp, 0);
			if (ast_sockaddr_cmp_addr(&addr, &tmp_addr)) {
				char buf[64];
				ast_copy_string(buf, ast_sockaddr_stringify(&addr), sizeof(buf));
				ast_test_status_update(test, "Re-parsed stringification of '%s' did not match: '%s' vs '%s'\n", test_vals[x].address, buf, ast_sockaddr_stringify(&tmp_addr));
				res = AST_TEST_FAIL;
			}
		}
	}

	return res;
}

AST_TEST_DEFINE(split_hostport)
{
	int res = AST_TEST_PASS;
	char *host, *port, buf[128];

	switch (cmd) {
	case TEST_INIT:
		info->name = "split_hostport";
		info->category = "/main/netsock2/";
		info->summary = "netsock2 ast_sockaddr_split_hostport() unit test";
		info->description =
			"Test splitting of IPv4 and IPv6 host:port strings";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

/* Assumes res, host, and port variables */
#define TEST_SPLIT_HOSTPORT(str, flags, expected_host, expected_port, expected_result) { \
	int __res; \
	ast_copy_string(buf, str, sizeof(buf)); \
	if ((__res = ast_sockaddr_split_hostport(buf, &host, &port, flags)) != expected_result || ( \
			expected_result && ( \
				strcmp(host, expected_host) || ( \
					ast_strlen_zero(expected_port) ? !ast_strlen_zero(port) : (!ast_strlen_zero(port) && strcmp(port, expected_port)) \
				) \
			) \
		) \
	) { \
		res = AST_TEST_FAIL; \
		if (__res != expected_result) { \
			ast_test_status_update(test, "Expected %d, got %d\n", expected_result, __res); \
		} else { \
			ast_test_status_update(test, "Failed parsing '%s' into expected host '%s' (got '%s') and port '%s' (got '%s')\n", \
				str, S_OR(expected_host, ""), host, expected_port, S_OR(port, "")); \
		} \
	} \
}

	/* Test various situations with flags = 0 */
	TEST_SPLIT_HOSTPORT("192.168.1.1", 0, "192.168.1.1", "", 1);
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", 0, "192.168.1.1", "5060", 1);
	TEST_SPLIT_HOSTPORT("::ffff:5.6.7.8", 0, "::ffff:5.6.7.8", "", 1);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", 0, "::ffff:5.6.7.8", "5060", 1);
	TEST_SPLIT_HOSTPORT("fdf8:f53b:82e4::53", 0, "fdf8:f53b:82e4::53", "", 1);
	TEST_SPLIT_HOSTPORT("fe80::200:5aee:feaa:20a2", 0, "fe80::200:5aee:feaa:20a2", "", 1);
	TEST_SPLIT_HOSTPORT("[fdf8:f53b:82e4::53]", 0, "fdf8:f53b:82e4::53", "", 1);
	TEST_SPLIT_HOSTPORT("[fe80::200:5aee:feaa:20a2]:5060", 0, "fe80::200:5aee:feaa:20a2", "5060", 1);
	TEST_SPLIT_HOSTPORT("host:port", 0, "host", "port", 1);
	TEST_SPLIT_HOSTPORT("host", 0, "host", "", 1);

	/* Make sure that flag conditions work when they should */
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", PARSE_PORT_IGNORE, "192.168.1.1", "", 1);
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", PARSE_PORT_REQUIRE, "192.168.1.1", "5060", 1);
	TEST_SPLIT_HOSTPORT("192.168.1.1", PARSE_PORT_FORBID, "192.168.1.1", "", 1);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", PARSE_PORT_IGNORE, "::ffff:5.6.7.8", "", 1);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", PARSE_PORT_REQUIRE, "::ffff:5.6.7.8", "5060", 1);
	TEST_SPLIT_HOSTPORT("::ffff:5.6.7.8", PARSE_PORT_FORBID, "::ffff:5.6.7.8", "", 1);

	/* Make sure it fails when flag requirements are not met */
	TEST_SPLIT_HOSTPORT("192.168.1.1", PARSE_PORT_REQUIRE, "<undefined>", "<undefined>", 0);
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", PARSE_PORT_FORBID, "<undefined>", "<undefined>", 0);
	TEST_SPLIT_HOSTPORT("::ffff:5.6.7.8", PARSE_PORT_REQUIRE, "<undefined>", "<undefined>", 0);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", PARSE_PORT_FORBID, "<undefined>", "<undefined>", 0);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(parsing);
	AST_TEST_UNREGISTER(split_hostport);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(parsing);
	AST_TEST_REGISTER(split_hostport);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Netsock2 test module");
