/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
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
 * \brief Test custom control frame encode and decode functions.
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/custom_control_frame.h"
#include "asterisk/test.h"

AST_TEST_DEFINE(sipinfo_encode_decode_test)
{
	struct ast_variable *headers = NULL;
	struct ast_variable *var = NULL;
	struct ast_variable **cur = NULL;
	struct ast_custom_payload *pl = NULL;
	char *out_content = NULL;
	char *out_content_type = NULL;
	char *useragent_filter = NULL;
	int res = AST_TEST_FAIL;
	struct {
		int num_headers_set;
		char *header1;
		char *header_val1;
		char *header2;
		char *header_val2;
		char *header3;
		char *header_val3;
		char *content;
		char *content_type;
		char *useragent_filter;
	} test_cases[] = {
		{
			3,
			"X-blah-header",
			"blah-value",
			"X-blah2-header",
			"blah2-value",
			"X-blah3-header",
			"blah3-value",
			"{ 'jsonjunk': hooray }",
			"application/json",
			NULL,
		},
		{
			2,
			"X-blah-header",
			"blah-value",
			"X-blah2-header",
			"blah2-value",
			NULL,
			NULL,
			"{ 'jsonjunk': hooray }",
			"application/json",
			NULL,
		},
		{
			2,
			"X-blah-header",
			"blah-value",
			"X-blah2-header",
			"blah2-value",
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
		},
		{
			3,
			"X-blah-header",
			"blah-value",
			"X-blah2-header",
			"blah2-value",
			"X-blah3-header",
			"blah3-value",
			"{ 'jsonjunk': hooray }",
			"application/json",
			"Digium",
		},
		{
			2,
			"X-blah-header",
			"blah-value",
			"X-blah2-header",
			"blah2-value",
			NULL,
			NULL,
			"{ 'jsonjunk': hooray }",
			"application/json",
			"Digium",
		},
		{
			2,
			"X-blah-header",
			"blah-value",
			"X-blah2-header",
			"blah2-value",
			NULL,
			NULL,
			NULL,
			NULL,
			"Digium",
		},
	};
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sipinfo_encode_decode_test";
		info->category = "/main/custom_control_frame/";
		info->summary = "encode and decode sip info custom control frames.";
		info->description = "Verifies the encode and decode routines for AST_CONTROL_CUSTOM sip info payloads.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(test_cases); i++) {
		int num_headers = 0;
		cur = &headers;
		if (test_cases[i].header1) {
			*cur = ast_variable_new(test_cases[i].header1, test_cases[i].header_val1, "");
			cur = &(*cur)->next;
		}
		if (test_cases[i].header2) {
			*cur = ast_variable_new(test_cases[i].header2, test_cases[i].header_val2, "");
			cur = &(*cur)->next;
		}
		if (test_cases[i].header3) {
			*cur = ast_variable_new(test_cases[i].header3, test_cases[i].header_val3, "");
			cur = &(*cur)->next;
		}
		if (!(pl = ast_custom_payload_sipinfo_encode(headers, test_cases[i].content, test_cases[i].content_type, test_cases[i].useragent_filter))) {
			goto sipinfo_cleanup;
		}
		ast_variables_destroy(headers);
		headers = NULL;

		if (ast_custom_payload_sipinfo_decode(pl, &headers, &out_content, &out_content_type, &useragent_filter)) {
			goto sipinfo_cleanup;
		}

		for (var = headers; var; var = var->next) {
			num_headers++;
			if (num_headers == 1) {
				if (strcmp(var->name, test_cases[i].header1) || strcmp(var->value, test_cases[i].header_val1)) {
					goto sipinfo_cleanup;
				}
			} else if (num_headers == 2) {
				if (strcmp(var->name, test_cases[i].header2) || strcmp(var->value, test_cases[i].header_val2)) {
					goto sipinfo_cleanup;
				}

			} else if (num_headers == 3) {
				if (strcmp(var->name, test_cases[i].header3) || strcmp(var->value, test_cases[i].header_val3)) {
					goto sipinfo_cleanup;
				}
			}
		}
		if (num_headers != test_cases[i].num_headers_set) {
			goto sipinfo_cleanup;
		}
		if (test_cases[i].content && strcmp(test_cases[i].content, out_content)) {
			goto sipinfo_cleanup;
		}
		if (test_cases[i].content_type && strcmp(test_cases[i].content_type, out_content_type)) {
			goto sipinfo_cleanup;
		}
		if (test_cases[i].useragent_filter && strcmp(test_cases[i].useragent_filter, useragent_filter)) {
			goto sipinfo_cleanup;
		}
		ast_variables_destroy(headers);
		ast_free(pl);
		ast_free(out_content);
		ast_free(out_content_type);
		ast_free(useragent_filter);
		headers = NULL;
		pl = NULL;
		out_content = out_content_type = useragent_filter = NULL;
	}
	res = AST_TEST_PASS;

sipinfo_cleanup:

	ast_free(pl);
	ast_free(out_content);
	ast_free(out_content_type);
	ast_variables_destroy(headers);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(sipinfo_encode_decode_test);
	return 0;
}

static int load_module(void)
{

	AST_TEST_REGISTER(sipinfo_encode_decode_test);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Custom control frames test module");
