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
 * \brief SHA1 test
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

AST_TEST_DEFINE(sha1_test)
{
	static const struct {
		const char *input;
		const char *expected_output;
	} tests[] = {
		{ "giraffe",
			"fac8f1a31d2998734d6a5253e49876b8e6a08239" },
		{ "platypus",
			"1dfb21b7a4d35e90d943e3a16107ccbfabd064d5" },
		{ "ParastratiosphecomyiaStratiosphecomyioides",
			"58af4e8438676f2bd3c4d8df9e00ee7fe06945bb" },
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sha1_test";
		info->category = "main/";
		info->summary = "SHA1 test";
		info->description =
			"This test exercises SHA1 calculations.\n"
			"";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(&args->status_update, "Testing SHA1 ...\n");

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		char sha1_hash[64];
		ast_sha1_hash(sha1_hash, tests[i].input);
		if (strcasecmp(sha1_hash, tests[i].expected_output)) {
			ast_test_status_update(&args->status_update,
					"input: '%s'  hash: '%s'  expected hash: '%s'\n",
					tests[i].input, sha1_hash, tests[i].expected_output);
			ast_str_append(&args->ast_test_error_str, 0,
					"input: '%s'  hash: '%s'  expected hash: '%s'\n",
					tests[i].input, sha1_hash, tests[i].expected_output);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(sha1_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(sha1_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SHA1 Test");
