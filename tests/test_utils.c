/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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

/*! \file
 *
 * \brief Unit Tests for utils API
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/utils.h"
#include "asterisk/test.h"
#include "asterisk/module.h"

AST_TEST_DEFINE(uri_encode_decode_test)
{
	int res = AST_TEST_PASS;
	const char *in = "abcdefghijklmnopurstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 1234567890 ~`!@#$%^&*()_-+={[}]|\\:;\"'<,>.?/";
	const char *expected1 = "abcdefghijklmnopurstuvwxyz%20ABCDEFGHIJKLMNOPQRSTUVWXYZ%201234567890%20~%60!%40%23%24%25%5E%26*()_-%2B%3D%7B%5B%7D%5D%7C%5C%3A%3B%22'%3C%2C%3E.%3F%2F";
	const char *expected2 = "abcdefghijklmnopurstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 1234567890 ~`!@#$%25^&*()_-+={[}]|\\:;\"'<,>.?/";
	char out[256] = { 0 };

	switch (cmd) {
	case TEST_INIT:
		info->name = "uri_encode_decode_test";
		info->category = "main/utils/";
		info->summary = "encode and decode a hex escaped string";
		info->description = "encode a string, verify encoded string matches what we expect.  Decode the encoded string, verify decoded string matches the original string.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(&args->status_update, "Input before executing ast_uri_encode:\n%s\n", in);
	ast_test_status_update(&args->status_update, "Output expected for ast_uri_encode with enabling do_special_char:\n%s\n", expected1);
	ast_test_status_update(&args->status_update, "Output expected for ast_uri_encode with out enabling do_special_char:\n%s\n\n", expected2);

	/* Test with do_special_char enabled */
	ast_uri_encode(in, out, sizeof(out), 1);
	ast_test_status_update(&args->status_update, "Output after enabling do_special_char:\n%s\n", out);
	if (strcmp(expected1, out)) {
		ast_test_status_update(&args->status_update, "ENCODE DOES NOT MATCH EXPECTED, FAIL\n");
		ast_str_append(&args->ast_test_error_str, 0, "enable do_special_char test encode failed: \n");
		res = AST_TEST_FAIL;
	}

	/* Verify uri decode matches original */
	ast_uri_decode(out);
	if (strcmp(in, out)) {
		ast_test_status_update(&args->status_update, "Decoded string did not match original input\n\n");
		ast_str_append(&args->ast_test_error_str, 0, "enable do_special_char test decode failed: \n");
		res = AST_TEST_FAIL;
	} else {
		ast_test_status_update(&args->status_update, "Decoded string matched original input\n\n");
	}

	/* Test with do_special_char disabled */
	out[0] = '\0';
	ast_uri_encode(in, out, sizeof(out), 0);
	ast_test_status_update(&args->status_update, "Output after disabling do_special_char:\n%s\n", out);
	if (strcmp(expected2, out)) {
		ast_test_status_update(&args->status_update, "ENCODE DOES NOT MATCH EXPECTED, FAIL\n");
		ast_str_append(&args->ast_test_error_str, 0, "no do_special_char test encode failed: \n");
		res = AST_TEST_FAIL;
	}

	/* Verify uri decode matches original */
	ast_uri_decode(out);
	if (strcmp(in, out)) {
		ast_test_status_update(&args->status_update, "Decoded string did not match original input\n\n");
		ast_str_append(&args->ast_test_error_str, 0, "no do_special_char test decode failed\n");
		res = AST_TEST_FAIL;
	} else {
		ast_test_status_update(&args->status_update, "Decoded string matched original input\n\n");
	}
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(uri_encode_decode_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(uri_encode_decode_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Utils test module");
