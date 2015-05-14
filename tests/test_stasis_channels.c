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

ASTERISK_REGISTER_FILE()

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
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
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

	ast_test_validate(test, stasis_message_type_create("test-type", NULL, &type) == STASIS_MESSAGE_TYPE_SUCCESS);
	chan = ast_channel_alloc(0, AST_STATE_DOWN, "100", "Alice", "100", "100", "default", NULL, NULL, 0, "TEST/Alice");
	ast_channel_unlock(chan);
	json = ast_json_pack("{s: s}",
		     "foo", "bar");

	/* Off nominal creation */
	ast_channel_lock(chan);
	ast_test_validate(test, NULL == ast_channel_blob_create(chan, NULL, json));

	/* Test for single channel */
	msg = ast_channel_blob_create(chan, type, json);
	ast_channel_unlock(chan);
	ast_test_validate(test, NULL != msg);
	blob = stasis_message_data(msg);
	ast_test_validate(test, NULL != blob);
	ast_test_validate(test, NULL != blob->snapshot);
	ast_test_validate(test, NULL != blob->blob);
	ast_test_validate(test, type == stasis_message_type(msg));

	ast_test_validate(test, 1 == ao2_ref(msg, 0));
	ao2_cleanup(msg);

	/* Test for global channels */
	msg = ast_channel_blob_create(NULL, type, json);
	ast_test_validate(test, NULL != msg);
	blob = stasis_message_data(msg);
	ast_test_validate(test, NULL != blob);
	ast_test_validate(test, NULL == blob->snapshot);
	ast_test_validate(test, NULL != blob->blob);
	ast_test_validate(test, type == stasis_message_type(msg));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(null_blob)
{
	struct ast_channel_blob *blob;
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
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

	ast_test_validate(test, stasis_message_type_create("test-type", NULL, &type) == STASIS_MESSAGE_TYPE_SUCCESS);
	chan = ast_channel_alloc(0, AST_STATE_DOWN, "100", "Alice", "100", "100", "default", NULL, NULL, 0, "TEST/Alice");
	ast_channel_unlock(chan);
	json = ast_json_pack("{s: s}",
		     "foo", "bar");

	/* Test for single channel */
	ast_channel_lock(chan);
	msg = ast_channel_blob_create(chan, type, NULL);
	ast_channel_unlock(chan);
	ast_test_validate(test, NULL != msg);
	blob = stasis_message_data(msg);
	ast_test_validate(test, NULL != blob);
	ast_test_validate(test, NULL != blob->snapshot);
	ast_test_validate(test, ast_json_null() == blob->blob);
	ast_test_validate(test, type == stasis_message_type(msg));

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
		     "foo", "bar");

	/* Test for single channel */
	blob = ast_multi_channel_blob_create(json);
	ast_test_validate(test, NULL != blob);
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
	chan_alice = ast_channel_alloc(0, AST_STATE_DOWN, "100", "Alice", "100", "100", "default", NULL, NULL, 0, "TEST/Alice");
	ast_channel_unlock(chan_alice);
	chan_bob = ast_channel_alloc(0, AST_STATE_DOWN, "200", "Bob", "200", "200", "default", NULL, NULL, 0, "TEST/Bob");
	ast_channel_unlock(chan_bob);
	chan_charlie = ast_channel_alloc(0, AST_STATE_DOWN, "300", "Bob", "300", "300", "default", NULL, NULL, 0, "TEST/Charlie");
	ast_channel_unlock(chan_charlie);

	blob = ast_multi_channel_blob_create(json);
	ast_channel_lock(chan_alice);
	ast_multi_channel_blob_add_channel(blob, "Caller", ast_channel_snapshot_create(chan_alice));
	ast_channel_unlock(chan_alice);
	ast_channel_lock(chan_bob);
	ast_multi_channel_blob_add_channel(blob, "Peer", ast_channel_snapshot_create(chan_bob));
	ast_channel_unlock(chan_bob);
	ast_channel_lock(chan_charlie);
	ast_multi_channel_blob_add_channel(blob, "Peer", ast_channel_snapshot_create(chan_charlie));
	ast_channel_unlock(chan_charlie);

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

AST_TEST_DEFINE(channel_snapshot_json)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, actual, NULL, ast_json_unref);

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

	ast_test_validate(test, NULL == ast_channel_snapshot_to_json(NULL, NULL));

	chan = ast_channel_alloc(0, AST_STATE_DOWN, "cid_num", "cid_name", "acctcode", "exten", "context", NULL, NULL, 0, "TEST/name");
	ast_channel_unlock(chan);
	ast_test_validate(test, NULL != chan);
	ast_channel_lock(chan);
	snapshot = ast_channel_snapshot_create(chan);
	ast_channel_unlock(chan);
	ast_test_validate(test, NULL != snapshot);

	actual = ast_channel_snapshot_to_json(snapshot, NULL);
	expected = ast_json_pack("{ s: s, s: s, s: s, s: s,"
				 "  s: { s: s, s: s, s: i },"
				 "  s: { s: s, s: s },"
				 "  s: { s: s, s: s },"
				 "  s: s"
				 "  s: o"
				 "}",
				 "name", "TEST/name",
				 "state", "Down",
				 "accountcode", "acctcode",
				 "id", ast_channel_uniqueid(chan),
				 "dialplan",
				 "context", "context",
				 "exten", "exten",
				 "priority", 1,
				 "caller",
				 "name", "cid_name",
				 "number", "cid_num",
				 "connected",
				 "name", "",
				 "number", "",
				 "language", "en",
				 "creationtime",
				 ast_json_timeval(
					 ast_channel_creationtime(chan), NULL));

	ast_test_validate(test, ast_json_equal(expected, actual));

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(channel_blob_create);
	AST_TEST_UNREGISTER(null_blob);
	AST_TEST_UNREGISTER(multi_channel_blob_create);
	AST_TEST_UNREGISTER(multi_channel_blob_snapshots);
	AST_TEST_UNREGISTER(channel_snapshot_json);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(channel_blob_create);
	AST_TEST_REGISTER(null_blob);
	AST_TEST_REGISTER(multi_channel_blob_create);
	AST_TEST_REGISTER(multi_channel_blob_snapshots);
	AST_TEST_REGISTER(channel_snapshot_json);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, 0, "Stasis Channel Testing",
	.load = load_module,
	.unload = unload_module
);
