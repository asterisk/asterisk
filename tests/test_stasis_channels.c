/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \file \brief Test Stasis Channel messages and objects
 *
 * \author\verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/test.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/channel.h"

static const char *test_category = "/stasis/channels/";

static void safe_channel_release(struct ast_channel *chan)
{
	if (!chan) {
		return;
	}
	ast_channel_release(chan);
}

AST_TEST_DEFINE(channel_blob_create)
{
	struct ast_channel_blob *blob;
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, bad_json, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test creation of ast_channel_blob objects";
		info->description = "Test creation of ast_channel_blob objects";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	chan = ast_channel_alloc(0, AST_STATE_DOWN, "100", "Alice", "100", "100", "default", NULL, 0, "TEST/Alice");
	json = ast_json_pack("{s: s}",
		     "type", "test");
	bad_json = ast_json_pack("{s: s}",
		     "bad_key", "test");

	/* Off nominal creation */
	ast_test_validate(test, NULL == ast_channel_blob_create(NULL, bad_json));
	ast_test_validate(test, NULL == ast_channel_blob_create(chan, bad_json));

	/* Test for single channel */
	msg = ast_channel_blob_create(chan, json);
	ast_test_validate(test, NULL != msg);
	blob = stasis_message_data(msg);
	ast_test_validate(test, NULL != blob);
	ast_test_validate(test, NULL != blob->snapshot);
	ast_test_validate(test, NULL != blob->blob);
	ast_test_validate(test, 0 == strcmp(ast_channel_blob_json_type(blob), "test"));

	ast_test_validate(test, 1 == ao2_ref(msg, 0));
	ao2_cleanup(msg);

	/* Test for global channels */
	msg = ast_channel_blob_create(NULL, json);
	ast_test_validate(test, NULL != msg);
	blob = stasis_message_data(msg);
	ast_test_validate(test, NULL != blob);
	ast_test_validate(test, NULL == blob->snapshot);
	ast_test_validate(test, NULL != blob->blob);
	ast_test_validate(test, 0 == strcmp(ast_channel_blob_json_type(blob), "test"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(multi_channel_blob_create)
{
	RAII_VAR(struct ast_multi_channel_blob *, blob, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, bad_json, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test creation of ast_multi_channel_blob objects";
		info->description = "Test creation of ast_multi_channel_blob objects";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	json = ast_json_pack("{s: s}",
		     "type", "test");
	bad_json = ast_json_pack("{s: s}",
		     "bad_key", "test");

	/* Off nominal creation */
	ast_test_validate(test, NULL == ast_multi_channel_blob_create(bad_json));

	/* Test for single channel */
	blob = ast_multi_channel_blob_create(json);
	ast_test_validate(test, NULL != blob);
	ast_test_validate(test, 0 == strcmp(ast_multi_channel_blob_get_type(blob), "test"));
	ast_test_validate(test, NULL != ast_multi_channel_blob_get_json(blob));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(multi_channel_blob_snapshots)
{
	RAII_VAR(struct ast_multi_channel_blob *, blob, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	struct ast_channel_snapshot *snapshot;
	struct ao2_container *matches;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test creation of ast_multi_channel_blob objects";
		info->description = "Test creation of ast_multi_channel_blob objects";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	json = ast_json_pack("{s: s}",
		     "type", "test");
	chan_alice = ast_channel_alloc(0, AST_STATE_DOWN, "100", "Alice", "100", "100", "default", NULL, 0, "TEST/Alice");
	chan_bob = ast_channel_alloc(0, AST_STATE_DOWN, "200", "Bob", "200", "200", "default", NULL, 0, "TEST/Bob");
	chan_charlie = ast_channel_alloc(0, AST_STATE_DOWN, "300", "Bob", "300", "300", "default", NULL, 0, "TEST/Charlie");

	blob = ast_multi_channel_blob_create(json);
	ast_multi_channel_blob_add_channel(blob, "Caller", ast_channel_snapshot_create(chan_alice));
	ast_multi_channel_blob_add_channel(blob, "Peer", ast_channel_snapshot_create(chan_bob));
	ast_multi_channel_blob_add_channel(blob, "Peer", ast_channel_snapshot_create(chan_charlie));

	/* Test for unknown role */
	ast_test_validate(test, NULL == ast_multi_channel_blob_get_channel(blob, "Foobar"));

	/* Test for single match */
	snapshot = ast_multi_channel_blob_get_channel(blob, "Caller");
	ast_test_validate(test, NULL != snapshot);
	ast_test_validate(test, 0 == strcmp("TEST/Alice", snapshot->name));

	/* Test for single match, multiple possibilities */
	snapshot = ast_multi_channel_blob_get_channel(blob, "Peer");
	ast_test_validate(test, NULL != snapshot);
	ast_test_validate(test, 0 != strcmp("TEST/Alice", snapshot->name));

	/* Multi-match */
	matches = ast_multi_channel_blob_get_channels(blob, "Peer");
	ast_test_validate(test, NULL != matches);
	ast_test_validate(test, 2 == ao2_container_count(matches));
	snapshot = ao2_find(matches, "TEST/Bob", OBJ_KEY);
	ast_test_validate(test, NULL != snapshot);
	ao2_cleanup(snapshot);
	snapshot = ao2_find(matches, "TEST/Charlie", OBJ_KEY);
	ast_test_validate(test, NULL != snapshot);
	ao2_cleanup(snapshot);
	ast_test_validate(test, 1 == ao2_ref(matches, 0));
	ao2_cleanup(matches);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(channel_blob_create);
	AST_TEST_UNREGISTER(multi_channel_blob_create);
	AST_TEST_UNREGISTER(multi_channel_blob_snapshots);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(channel_blob_create);
	AST_TEST_REGISTER(multi_channel_blob_create);
	AST_TEST_REGISTER(multi_channel_blob_snapshots);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, 0, "Stasis Channel Testing",
		.load = load_module,
		.unload = unload_module
	);
