/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \file \brief Test endpoints.
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_stasis_test</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/endpoints.h"
#include "asterisk/module.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stasis_test.h"
#include "asterisk/test.h"

static const char *test_category = "/stasis/endpoints/";

/*! \brief Message matcher looking for cache update messages */
static int cache_update(struct stasis_message *msg, const void *data) {
	struct stasis_cache_update *update;
	struct ast_endpoint_snapshot *snapshot;
	const char *name = data;

	if (stasis_cache_update_type() != stasis_message_type(msg)) {
		return 0;
	}

	update = stasis_message_data(msg);
	if (ast_endpoint_snapshot_type() != update->type) {
		return 0;
	}

	snapshot = stasis_message_data(update->old_snapshot);
	if (!snapshot) {
		snapshot = stasis_message_data(update->new_snapshot);
	}

	return 0 == strcmp(name, snapshot->resource);
}

AST_TEST_DEFINE(state_changes)
{
	RAII_VAR(struct ast_endpoint *, uut, NULL, ast_endpoint_shutdown);
	RAII_VAR(struct ast_channel *, chan, NULL, ast_hangup);
	RAII_VAR(struct stasis_message_sink *, sink, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	struct stasis_message *msg;
	struct stasis_message_type *type;
	struct ast_endpoint_snapshot *actual_snapshot;
	int actual_count;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test endpoint updates as its state changes";
		info->description =
			"Test endpoint updates as its state changes";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_endpoint_create("TEST", __func__);
	ast_test_validate(test, NULL != uut);

	sink = stasis_message_sink_create();
	ast_test_validate(test, NULL != sink);

	sub = stasis_subscribe(ast_endpoint_topic(uut),
		stasis_message_sink_cb(), sink);
	ast_test_validate(test, NULL != sub);

	ast_endpoint_set_state(uut, AST_ENDPOINT_OFFLINE);
	actual_count = stasis_message_sink_wait_for_count(sink, 1,
		STASIS_SINK_DEFAULT_WAIT);
	ast_test_validate(test, 1 == actual_count);
	msg = sink->messages[0];
	type = stasis_message_type(msg);
	ast_test_validate(test, ast_endpoint_snapshot_type() == type);
	actual_snapshot = stasis_message_data(msg);
	ast_test_validate(test, AST_ENDPOINT_OFFLINE == actual_snapshot->state);

	ast_endpoint_set_max_channels(uut, 8675309);
	actual_count = stasis_message_sink_wait_for_count(sink, 2,
		STASIS_SINK_DEFAULT_WAIT);
	ast_test_validate(test, 2 == actual_count);
	msg = sink->messages[1];
	type = stasis_message_type(msg);
	ast_test_validate(test, ast_endpoint_snapshot_type() == type);
	actual_snapshot = stasis_message_data(msg);
	ast_test_validate(test, 8675309 == actual_snapshot->max_channels);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(cache_clear)
{
	RAII_VAR(struct ast_endpoint *, uut, NULL, ast_endpoint_shutdown);
	RAII_VAR(struct ast_channel *, chan, NULL, ast_hangup);
	RAII_VAR(struct stasis_message_sink *, sink, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	struct stasis_message *msg;
	struct stasis_message_type *type;
	struct ast_endpoint_snapshot *actual_snapshot;
	struct stasis_cache_update *update;
	int message_index;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test endpoint state change messages";
		info->description = "Test endpoint state change messages";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Subscribe to the cache topic */
	sink = stasis_message_sink_create();
	ast_test_validate(test, NULL != sink);

	sub = stasis_subscribe(
		ast_endpoint_topic_all_cached(),
		stasis_message_sink_cb(), sink);
	ast_test_validate(test, NULL != sub);

	uut = ast_endpoint_create("TEST", __func__);
	ast_test_validate(test, NULL != uut);

	/* Since the cache topic is a singleton (ew), it may have messages from
	 * elsewheres that it's processing, or maybe even some final messages
	 * from the prior test. We've got to wait_for our specific message,
	 * instead of wait_for_count.
	 */
	message_index = stasis_message_sink_wait_for(sink, 0,
		cache_update, __func__, STASIS_SINK_DEFAULT_WAIT);
	ast_test_validate(test, 0 <= message_index);

	/* First message should be a cache creation entry for our endpoint */
	msg = sink->messages[message_index];
	type = stasis_message_type(msg);
	ast_test_validate(test, stasis_cache_update_type() == type);
	update = stasis_message_data(msg);
	ast_test_validate(test, ast_endpoint_snapshot_type() == update->type);
	ast_test_validate(test, NULL == update->old_snapshot);
	actual_snapshot = stasis_message_data(update->new_snapshot);
	ast_test_validate(test, 0 == strcmp("TEST", actual_snapshot->tech));
	ast_test_validate(test,
		0 == strcmp(__func__, actual_snapshot->resource));

	ast_endpoint_shutdown(uut);
	uut = NULL;

	/* Note: there's a few messages between the creation and the clear.
	 * Wait for all of them... */
	message_index = stasis_message_sink_wait_for(sink, message_index + 4,
		cache_update, __func__, STASIS_SINK_DEFAULT_WAIT);
	ast_test_validate(test, 0 <= message_index);
	/* Now we should have a cache removal entry */
	msg = sink->messages[message_index];
	type = stasis_message_type(msg);
	ast_test_validate(test, stasis_cache_update_type() == type);
	update = stasis_message_data(msg);
	ast_test_validate(test, ast_endpoint_snapshot_type() == update->type);
	actual_snapshot = stasis_message_data(update->old_snapshot);
	ast_test_validate(test, 0 == strcmp("TEST", actual_snapshot->tech));
	ast_test_validate(test,
		0 == strcmp(__func__, actual_snapshot->resource));
	ast_test_validate(test, NULL == update->new_snapshot);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(channel_messages)
{
	RAII_VAR(struct ast_endpoint *, uut, NULL, ast_endpoint_shutdown);
	RAII_VAR(struct ast_channel *, chan, NULL, ast_hangup);
	RAII_VAR(struct stasis_message_sink *, sink, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	struct stasis_message *msg;
	struct stasis_message_type *type;
	struct ast_endpoint_snapshot *actual_snapshot;
	int actual_count;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test channel messages on an endpoint topic";
		info->description =
			"Test channel messages on an endpoint topic";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_endpoint_create("TEST", __func__);
	ast_test_validate(test, NULL != uut);

	sink = stasis_message_sink_create();
	ast_test_validate(test, NULL != sink);

	sub = stasis_subscribe(ast_endpoint_topic(uut),
		stasis_message_sink_cb(), sink);
	ast_test_validate(test, NULL != sub);

	chan = ast_channel_alloc(0, AST_STATE_DOWN, "100", __func__, "100",
		"100", "default", NULL, NULL, 0, "TEST/test_res");
	ast_test_validate(test, NULL != chan);

	ast_endpoint_add_channel(uut, chan);

	actual_count = stasis_message_sink_wait_for_count(sink, 1,
		STASIS_SINK_DEFAULT_WAIT);
	ast_test_validate(test, 1 == actual_count);

	msg = sink->messages[0];
	type = stasis_message_type(msg);
	ast_test_validate(test, ast_endpoint_snapshot_type() == type);
	actual_snapshot = stasis_message_data(msg);
	ast_test_validate(test, 1 == actual_snapshot->num_channels);

	ast_hangup(chan);
	chan = NULL;

	actual_count = stasis_message_sink_wait_for_count(sink, 6,
		STASIS_SINK_DEFAULT_WAIT);
	ast_test_validate(test, 6 == actual_count);

	msg = sink->messages[1];
	type = stasis_message_type(msg);
	ast_test_validate(test, stasis_cache_update_type() == type);

	msg = sink->messages[2];
	type = stasis_message_type(msg);
	ast_test_validate(test, ast_channel_snapshot_type() == type);

	msg = sink->messages[3];
	type = stasis_message_type(msg);
	ast_test_validate(test, stasis_cache_update_type() == type);

	/* The ordering of the cache clear and endpoint snapshot are
	 * unspecified */
	msg = sink->messages[4];
	if (stasis_message_type(msg) == stasis_cache_clear_type()) {
		/* Okay; the next message should be the endpoint snapshot */
		msg = sink->messages[5];
	}

	type = stasis_message_type(msg);
	ast_test_validate(test, ast_endpoint_snapshot_type() == type);
	actual_snapshot = stasis_message_data(msg);
	ast_test_validate(test, 0 == actual_snapshot->num_channels);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(state_changes);
	AST_TEST_UNREGISTER(cache_clear);
	AST_TEST_UNREGISTER(channel_messages);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(state_changes);
	AST_TEST_REGISTER(cache_clear);
	AST_TEST_REGISTER(channel_messages);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Endpoint stasis-related testing",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis_test",
);
