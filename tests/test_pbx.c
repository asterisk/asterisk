/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief PBX Tests
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 * This module will run some PBX tests.
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/test.h"

/*!
 * If we determine that we really need
 * to be able to register more than 10
 * priorities for a single extension, then
 * fine, we can do that later.
 */
#define MAX_PRIORITIES 10

/*!
 * \brief an extension to add to our context
 */
struct exten_info {
	/*!
	 * \brief Context
	 *
	 * \details
	 * The extension specified will be added to
	 * this context when it is created.
	 */
	const char *context;
	/*!
	 * \brief Extension pattern
	 *
	 * \details
	 * The extension pattern to use. This can be
	 * anything you would normally find in a dialplan,
	 * such as "1000" or "NXXNXXX" or whatever you
	 * wish it to be. If, however, you want a CID match
	 * to be part of the extension, do not include that
	 * here.
	 */
	const char *exten;
	/*!
	 * \brief CID match
	 *
	 * \details
	 * If your extension requires a specific caller ID in
	 * order to match, place that in this field. Note that
	 * a NULL and an empty CID match are two very different
	 * things. If you want no CID match, leave this NULL. If
	 * you want to explicitly match a blank CID, then put
	 * an empty string here.
	 */
	const char *cid;
	/*!
	 * \brief Number of priorities
	 *
	 * \details
	 * Tell the number of priorities to register for this
	 * extension. All priorities registered will just have a
	 * Noop application with the extension pattern as its
	 * data.
	 */
	const int num_priorities;
	/*!
	 * \brief The priorities to register
	 *
	 * \details
	 * In most cases, when registering multiple priorities for
	 * an extension, we'll be starting at priority 1 and going
	 * sequentially until we've read num_priorities. However,
	 * for some tests, it may be beneficial to start at a higher
	 * priority or skip certain priorities. This is why you have
	 * the freedom here to specify which priorities to register
	 * for the extension.
	 */
	const int priorities[MAX_PRIORITIES];
};

struct pbx_test_pattern {
	/*!
	 * \brief Test context
	 *
	 * \details
	 * This is the context to look in for a specific extension.
	 */
	const char *context;
	/*!
	 * \brief Test extension number
	 *
	 * \details
	 * This should be in the form of a specific number or string.
	 * For instance, if you were trying to match an extension defined
	 * with the pattern "_2." you might have as the test_exten one of
	 * "2000" , "2legit2quit" or some other specific match for the pattern.
	 */
	const char *test_exten;
	/*!
	 * \brief Test CID match
	 *
	 * \details
	 * If a specific CID match is required for pattern matching, then specify
	 * it in this parameter. Remember that a NULL CID and an empty CID are
	 * interpreted differently. For no CID match, leave this NULL. If you wish
	 * to explicitly match an empty CID, then use an empty string here.
	 */
	const char *test_cid;
	/*!
	 * \brief The priority to find
	 */
	const int priority;
	/*!
	 * \brief Expected extension match.
	 *
	 * \details
	 * This struct corresponds to an extension that was previously
	 * added to our test context. Once we have used all the above data
	 * to find an extension in the dialplan. We compare the data from that
	 * extension to the data that we have stored in this structure to be
	 * sure that what was matched was what we expected to match.
	 */
	const struct exten_info *exten;
};

static int test_exten(const struct pbx_test_pattern *test_pattern, struct ast_test *test, int new_engine)
{
	struct pbx_find_info pfi = { { 0 }, };
	struct ast_exten *exten;
	if (!(exten = pbx_find_extension(NULL, NULL, &pfi, test_pattern->context,
					test_pattern->test_exten, test_pattern->priority, NULL,
					test_pattern->test_cid, E_MATCH))) {
		ast_test_status_update(test, "Cannot find extension %s in context %s with the %s pattern match engine. "
				"Test failed.\n", test_pattern->test_exten, test_pattern->context, (new_engine ? "new" : "old"));
		return -1;
	}
	if (strcmp(ast_get_extension_name(exten), test_pattern->exten->exten)) {
		ast_test_status_update(test, "Expected extension %s but got extension %s instead with the %s pattern match engine. "
				"Test failed.\n", test_pattern->exten->exten, ast_get_extension_name(exten), (new_engine ? "new" : "old"));
		return -1;
	}
	if (test_pattern->test_cid && strcmp(ast_get_extension_cidmatch(exten), test_pattern->test_cid)) {
		ast_test_status_update(test, "Expected CID match %s but got CID match %s instead with the %s pattern match engine. "
				"Test failed.\n", test_pattern->exten->cid, ast_get_extension_cidmatch(exten), (new_engine ? "new" : "old"));
		return -1;
	}
	if (!ast_canmatch_extension(NULL, test_pattern->context, test_pattern->test_exten,
					test_pattern->priority, test_pattern->test_cid)) {
		ast_test_status_update(test, "Partial match failed for extension %s in context %s with the %s pattern match engine. "
				"Test failed.\n", test_pattern->test_exten, test_pattern->context, (new_engine ? "new" : "old"));
		return -1;
	}
	ast_test_status_update(test, "Successfully matched %s to exten %s in context %s with the %s pattern match engine\n",
			test_pattern->test_exten, test_pattern->exten->exten, test_pattern->context, (new_engine ? "new" : "old"));
	return 0;
}

AST_TEST_DEFINE(pattern_match_test)
{
	static const char registrar[] = "test_pbx";
	enum ast_test_result_state res = AST_TEST_PASS;
	static const char TEST_PATTERN[] = "test_pattern";
	static const char TEST_PATTERN_INCLUDE[] = "test_pattern_include";
	int i, j;

	/* The array of contexts to register for our test.
	 * To add more contexts, just add more rows to this array.
	 */
	struct {
		const char * context_string;
	} contexts[] = {
		{ TEST_PATTERN, },
		{ TEST_PATTERN_INCLUDE, },
	};

	/*
	 * Map to indicate which contexts should be included inside
	 * other contexts. The first context listed will include
	 * the second context listed.
	 *
	 * To add more inclusions, add new rows to this array.
	 */
	const struct {
		const char *outer_context;
		const char *inner_context;
	} context_includes[] = {
		{ TEST_PATTERN, TEST_PATTERN_INCLUDE },
	};

	/* The array of extensions to add to our test context.
	 * For more information about the individual fields, see
	 * the doxygen for struct exten_info.
	 *
	 * To add new extensions to the test, simply add new rows
	 * to this array. All extensions will automatically be
	 * added when the test is run.
	 */
	const struct exten_info extens[] = {
		[0] = { TEST_PATTERN, "_2.", NULL, 1, { 1 } },
		[1] = { TEST_PATTERN, "2000", NULL, 1, { 1 } },
		[2] = { TEST_PATTERN_INCLUDE, "2000", NULL, 1, { 2 } },
	};

	/* This array contains our test material. See the doxygen
	 * for struct pbx_test_pattern for more information on each
	 * component.
	 *
	 * To add more test cases, add more lines to this array. Each
	 * case will be tested automatically when the test is run.
	 */
	const struct pbx_test_pattern tests[] = {
		{ TEST_PATTERN, "200", NULL, 1, &extens[0] },
		{ TEST_PATTERN, "2000", NULL, 1, &extens[1] },
		{ TEST_PATTERN, "2000", NULL, 2, &extens[2] },
		{ TEST_PATTERN_INCLUDE, "2000", NULL, 2, &extens[2] },
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "pattern_match_test";
		info->category = "/main/pbx/";
		info->summary = "Test pattern matching";
		info->description = "Create a context with a bunch of extensions within. Then attempt\n"
			"to match some strings to the extensions.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Step one is to build the dialplan.
	 *
	 * We iterate first through the contexts array to build
	 * all the contexts we'll need. Then, we iterate over the
	 * extens array to add all the extensions to the appropriate
	 * contexts.
	 */

	for (i = 0; i < ARRAY_LEN(contexts); ++i) {
		if (!ast_context_find_or_create(NULL, NULL, contexts[i].context_string, registrar)) {
			ast_test_status_update(test, "Failed to create context %s\n", contexts[i].context_string);
			res = AST_TEST_FAIL;
			goto cleanup;
		}
	}

	for (i = 0; i < ARRAY_LEN(context_includes); ++i) {
		if (ast_context_add_include(context_includes[i].outer_context,
					context_includes[i].inner_context, registrar)) {
			ast_test_status_update(test, "Failed to include context %s inside context %s\n",
					context_includes[i].inner_context, context_includes[i].outer_context);
			res = AST_TEST_FAIL;
			goto cleanup;
		}
	}

	for (i = 0; i < ARRAY_LEN(extens); ++i) {
		int priority;
		if (extens[i].num_priorities > MAX_PRIORITIES) {
			ast_test_status_update(test, "Invalid number of priorities specified for extension %s."
					"Max is %d, but we requested %d. Test failed\n",
					extens[i].exten, MAX_PRIORITIES, extens[i].num_priorities);
			res = AST_TEST_FAIL;
			goto cleanup;
		}
		for (priority = 0; priority < extens[i].num_priorities; ++priority) {
			if (ast_add_extension(extens[i].context, 0, extens[i].exten, extens[i].priorities[priority],
						NULL, extens[i].cid, "Noop", (void *) extens[i].exten, NULL, registrar)) {
				ast_test_status_update(test, "Failed to add extension %s, priority %d, to context %s."
						"Test failed\n", extens[i].exten, extens[i].priorities[priority], extens[i].context);
				res = AST_TEST_FAIL;
				goto cleanup;
			}
		}
	}

	/* At this stage, the dialplan is built. Now we iterate over
	 * the tests array to attempt to find each of the specified
	 * extensions with the old and new pattern matching engines.
	 */
	for (j = 0; j < 2; j++) {
		pbx_set_extenpatternmatchnew(j);
		for (i = 0; i < ARRAY_LEN(tests); ++i) {
			if (test_exten(&tests[i], test, j)) {
				res = AST_TEST_FAIL;
				break;
			}
		}
	}

cleanup:
	ast_context_destroy(NULL, registrar);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(pattern_match_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(pattern_match_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "PBX test module");
