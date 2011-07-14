/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Jeff Peeler <jpeeler@digium.com>
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
 * \brief App unit test
 *
 * \author Jeff Peeler <jpeeler@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"

#define BASE_GROUP "a group"

AST_TEST_DEFINE(options_parsing)
{
	enum test_option_flags {
		OPT_SIMPLE,
		OPT_WITHQUOTES,
		OPT_WITHBACKSLASH,
	};
	enum test_option_args {
		OPT_ARG_SIMPLE,
		OPT_ARG_WITHQUOTES,
		OPT_ARG_WITHBACKSLASH,
		OPT_ARG_ARRAY_SIZE,
	};
	AST_APP_OPTIONS(test_options, {
		AST_APP_OPTION_ARG('a', OPT_SIMPLE,        OPT_ARG_SIMPLE),
		AST_APP_OPTION_ARG('b', OPT_WITHQUOTES,    OPT_ARG_WITHQUOTES),
		AST_APP_OPTION_ARG('c', OPT_WITHBACKSLASH, OPT_ARG_WITHBACKSLASH),
	});
	struct ast_flags opts = { 0, };
	struct ast_flags64 opts64 = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct {
		const char *string;
		const char *parse[3];
	} options[] = {
		{ "a(simple)b(\"quoted\")c(back\\slash)", { "simple", "quoted", "backslash", }, },
		{ "b(\"((())))\")a(simple)c(back\\)slash)", { "simple", "((())))", "back)slash", }, },
		{ "b(\"((\\\"\\)\\(\")a(simple)c(back\\\"\\)\\(\\\"slash)", { "simple", "((\"\\)\\(", "back\")(\"slash", }, },
	};
	int i, j, res = AST_TEST_PASS;
	char buffer[256];

	switch (cmd) {
	case TEST_INIT:
		info->name = "options_parsing";
		info->category = "/main/app/";
		info->summary = "App options unit test";
		info->description =
			"This tests the options parsing code to ensure that it behaves as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(options); i++) {
		ast_copy_string(buffer, options[i].string, sizeof(buffer));

		if (ast_app_parse_options(test_options, &opts, opt_args, buffer)) {
			ast_test_status_update(test, "ast_app_parse_options() of '%s' failed\n", options[i].string);
			res = AST_TEST_FAIL;
		} else {
			/* Check arguments for success */
			for (j = 0; j < 3; j++) {
				if (strcmp(opt_args[j], options[i].parse[j])) {
					ast_test_status_update(test, "Parse of option %c from '%s' produced '%s', "
						"but it should have produced '%s'\n",
						'a' + j, options[i].string, opt_args[j], options[i].parse[j]);
					res = AST_TEST_FAIL;
				}
			}
		}

		ast_copy_string(buffer, options[i].string, sizeof(buffer));
		if (ast_app_parse_options64(test_options, &opts64, opt_args, buffer)) {
			ast_test_status_update(test, "ast_app_parse_options64() of '%s' failed\n", options[i].string);
			res = AST_TEST_FAIL;
		} else {
			/* Check arguments for success */
			for (j = 0; j < 3; j++) {
				if (strcmp(opt_args[j], options[i].parse[j])) {
					ast_test_status_update(test, "Parse of option %c from '%s' produced '%s', "
						"but it should have produced '%s'\n",
						'a' + j, options[i].string, opt_args[j], options[i].parse[j]);
					res = AST_TEST_FAIL;
				}
			}
		}
	}

	return res;
}

AST_TEST_DEFINE(app_group)
{
	struct ast_channel *test_channel1 = NULL;
	struct ast_channel *test_channel2 = NULL;
	struct ast_channel *test_channel3 = NULL;
	struct ast_channel *test_channel4 = NULL;

	static const char group1_full[] = BASE_GROUP "groupgroup";
	static const char group2_full[] = BASE_GROUP "Groupgroup";
	static const char regex1[] = "gr"; /* matches everything */
	static const char regex2[] = "(group){2}$"; /* matches only group1_full */
	static const char regex3[] = "[:ascii:]"; /* matches everything */
	static const char regex4[] = "^(NOMATCH)"; /* matches nothing */
	static const char category1_full[] = BASE_GROUP "@a_category"; /* categories shouldn't have spaces */
	static const char category2_full[] = BASE_GROUP "@another!Category";
	static const char regex5[] = "(gory)$"; /* matches both categories */
	static const char regex6[] = "[A-Z]+"; /* matches only category2_full */
	static const char regex7[] = "[["; /* not valid syntax, yes an expected warning will be displayed */
	static enum ast_test_result_state res = AST_TEST_PASS;
	static const struct group_test_params {
		const char *groupmatch;
		const char *category;
		int expected;
	} subtests[] = {
		{ regex1, "", 4 },
		{ regex2, "", 1 },
		{ regex3, "", 4 },
		{ regex4, "", 0 },
		{ BASE_GROUP, regex5, 2 },
		{ BASE_GROUP, regex6, 1 },
		/* this test is expected to generate a warning message from the invalid regex */
		{ BASE_GROUP, regex7, 0 }
	};
	int i;
	int returned_count;

	switch (cmd) {
	case TEST_INIT:
		info->name = "app_group";
		info->category = "/main/app/";
		info->summary = "App group unit test";
		info->description =
			"This tests various app group functionality";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating test channels with the following groups:\n"
		"'%s', '%s', '%s', '%s'\n", group1_full, group2_full, category1_full, category2_full);

	if (!(test_channel1 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
        NULL, NULL, 0, 0, "TestChannel1"))) {
		goto exit_group_test;
	}
	if (!(test_channel2 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
        NULL, NULL, 0, 0, "TestChannel2"))) {
		goto exit_group_test;
	}
	if (!(test_channel3 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
        NULL, NULL, 0, 0, "TestChannel3"))) {
		goto exit_group_test;
	}
	if (!(test_channel4 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
        NULL, NULL, 0, 0, "TestChannel4"))) {
		goto exit_group_test;
	}

	ast_app_group_set_channel(test_channel1, group1_full);
	ast_app_group_set_channel(test_channel2, group2_full);
	ast_app_group_set_channel(test_channel3, category1_full);
	ast_app_group_set_channel(test_channel4, category2_full);

	for (i = 0; i < ARRAY_LEN(subtests); i++) {
		ast_assert(subtests[i].groupmatch != NULL || subtests[i].category != NULL);
		returned_count = ast_app_group_match_get_count(subtests[i].groupmatch, subtests[i].category);

		if (subtests[i].expected != returned_count) {
			ast_test_status_update(test, "(Subtest %d) Expected %d matches but found %d when examining group:'%s' category:'%s'\n",
				i + 1, subtests[i].expected, returned_count, subtests[i].groupmatch, subtests[i].category);
			res = AST_TEST_FAIL;
			goto exit_group_test;
		} else {
			ast_test_status_update(test, "(Subtest %d) Found %d matches as expected when examining group:'%s' category:'%s'\n",
				i + 1, subtests[i].expected, subtests[i].groupmatch, subtests[i].category);
		}
	}

exit_group_test:
	if (test_channel1) {
		ast_hangup(test_channel1);
	}
	if (test_channel2) {
		ast_hangup(test_channel2);
	}
	if (test_channel3) {
		ast_hangup(test_channel3);
	}
	if (test_channel4) {
		ast_hangup(test_channel4);
	}
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(app_group);
	AST_TEST_UNREGISTER(options_parsing);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(app_group);
	AST_TEST_REGISTER(options_parsing);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "App unit tests");
