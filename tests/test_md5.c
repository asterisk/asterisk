/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \brief MD5 test
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/md5.h"

AST_TEST_DEFINE(md5_test)
{
	static const struct {
		const char *input;
		const char *expected_output;
	} tests[] = {
		{ "apples",                          "daeccf0ad3c1fc8c8015205c332f5b42" },
		{ "bananas",                         "ec121ff80513ae58ed478d5c5787075b" },
		{ "reallylongstringaboutgoatcheese", "0a2d9280d37e2e37545cfef6e7e4e890" },
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "md5_test";
		info->category = "main/";
		info->summary = "MD5 test";
		info->description =
			"This test exercises MD5 calculations.\n"
			"";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(&args->status_update, "Testing MD5 ...\n");

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		char md5_hash[32];
		ast_md5_hash(md5_hash, tests[i].input);
		if (strcasecmp(md5_hash, tests[i].expected_output)) {
			ast_test_status_update(&args->status_update,
					"input: '%s'  hash: '%s'  expected hash: '%s'\n",
					tests[i].input, md5_hash, tests[i].expected_output);
			ast_str_append(&args->ast_test_error_str, 0,
					"input: '%s'  hash: '%s'  expected hash: '%s'\n",
					tests[i].input, md5_hash, tests[i].expected_output);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(md5_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(md5_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "MD5 Test");
