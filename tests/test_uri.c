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
 * \brief URI Unit Tests
 *
 * \author Kevin Harwell <kharwell@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/uri.h"

#define CATEGORY "/main/uri/"

static const char *scenarios[][7] = {
	{"http://name:pass@localhost", "http", "name:pass", "localhost", NULL, NULL, NULL},
	{"http://localhost", "http", NULL, "localhost", NULL, NULL, NULL},
	{"http://localhost:80", "http", NULL, "localhost", "80", NULL, NULL},
	{"http://localhost/path/", "http", NULL, "localhost", NULL, "path/", NULL},
	{"http://localhost/?query", "http", NULL, "localhost", NULL, "", "query"},
	{"http://localhost:80/path", "http", NULL, "localhost", "80", "path", NULL},
	{"http://localhost:80/?query", "http", NULL, "localhost", "80", "", "query"},
	{"http://localhost:80/path?query", "http", NULL, "localhost", "80", "path", "query"},
};

AST_TEST_DEFINE(uri_parse)
{
#define VALIDATE(value, expected_value) \
	do { ast_test_validate(test, \
		     (value == expected_value) || \
		     (value && expected_value && \
		      !strcmp(value, expected_value)));	\
	} while (0)

	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Uri parsing scenarios";
		info->description = "For each scenario validate result(s)";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	for (i = 0; i < ARRAY_LEN(scenarios); ++i) {
		RAII_VAR(struct ast_uri *, uri, NULL, ao2_cleanup);
		const char **scenario = scenarios[i];

		ast_test_validate(test, (uri = ast_uri_parse(scenario[0])));
		VALIDATE(ast_uri_scheme(uri), scenario[1]);
		VALIDATE(ast_uri_user_info(uri), scenario[2]);
		VALIDATE(ast_uri_host(uri), scenario[3]);
		VALIDATE(ast_uri_port(uri), scenario[4]);
		VALIDATE(ast_uri_path(uri), scenario[5]);
		VALIDATE(ast_uri_query(uri), scenario[6]);
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(uri_default_http)
{
	RAII_VAR(struct ast_uri *, uri, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "parse an http uri with host only";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, (uri = ast_uri_parse_http("localhost")));
	ast_test_validate(test, !strcmp(ast_uri_scheme(uri), "http"));
	ast_test_validate(test, !strcmp(ast_uri_host(uri), "localhost"));
	ast_test_validate(test, !strcmp(ast_uri_port(uri), "80"));
	ast_test_validate(test, !ast_uri_is_secure(uri));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(uri_default_http_secure)
{
	RAII_VAR(struct ast_uri *, uri, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "parse an https uri with host only";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, (uri = ast_uri_parse_http("https://localhost")));
	ast_test_validate(test, !strcmp(ast_uri_scheme(uri), "https"));
	ast_test_validate(test, !strcmp(ast_uri_host(uri), "localhost"));
	ast_test_validate(test, !strcmp(ast_uri_port(uri), "443"));
	ast_test_validate(test, ast_uri_is_secure(uri));

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(uri_parse);
	AST_TEST_REGISTER(uri_default_http);
	AST_TEST_REGISTER(uri_default_http_secure);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(uri_default_http_secure);
	AST_TEST_UNREGISTER(uri_default_http);
	AST_TEST_UNREGISTER(uri_parse);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "URI test module");
