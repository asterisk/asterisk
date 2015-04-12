/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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
 * \brief Channel features unit tests
 *
 * \author Kinsey Moore <kmoore@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/channel.h"
#include "asterisk/time.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/features.h"

#define TEST_CATEGORY "/channels/features/"

#define CHANNEL_TECH_NAME "FeaturesTestChannel"

#define TEST_BACKEND_NAME "Features Test Logging"

/*! \brief A channel technology used for the unit tests */
static struct ast_channel_tech test_features_chan_tech = {
	.type = CHANNEL_TECH_NAME,
	.description = "Mock channel technology for Features tests",
};

static void test_nanosleep(int secs, long nanosecs)
{
	struct timespec sleep_time = {secs, nanosecs};

	while ((nanosleep(&sleep_time, &sleep_time) == -1) && (errno == EINTR)) {
	}
}

/*! \brief Wait until a channel is bridged */
static void wait_for_bridged(struct ast_channel *channel)
{
	ast_channel_lock(channel);
	while (!ast_channel_is_bridged(channel)) {
		ast_channel_unlock(channel);
		test_nanosleep(0, 1000000);
		ast_channel_lock(channel);
	}
	ast_channel_unlock(channel);
}

/*! \brief Wait until a channel is not bridged */
static void wait_for_unbridged(struct ast_channel *channel)
{
	ast_channel_lock(channel);
	while (ast_channel_is_bridged(channel)) {
		ast_channel_unlock(channel);
		test_nanosleep(0, 1000000);
		ast_channel_lock(channel);
	}
	ast_channel_unlock(channel);
}

/*! \brief Create a \ref test_features_chan_tech for Alice. */
#define START_ALICE(channel) START_CHANNEL(channel, "Alice", "100")

/*! \brief Create a \ref test_features_chan_tech for Bob. */
#define START_BOB(channel) START_CHANNEL(channel, "Bob", "200")

#define START_CHANNEL(channel, name, number) do { \
	channel = ast_channel_alloc(0, AST_STATE_UP, number, name, number, number, \
		"default", NULL, NULL, 0, CHANNEL_TECH_NAME "/" name); \
	ast_channel_unlock(channel); \
	} while (0)

/*! \brief Hang up a test channel safely */
#define HANGUP_CHANNEL(channel) do { \
	ao2_ref(channel, +1); \
	ast_hangup((channel)); \
	ao2_cleanup(channel); \
	channel = NULL; \
	} while (0)

static void safe_channel_release(struct ast_channel *chan)
{
	if (!chan) {
		return;
	}
	ast_channel_release(chan);
}

static void safe_bridge_destroy(struct ast_bridge *bridge)
{
	if (!bridge) {
		return;
	}
	ast_bridge_destroy(bridge, 0);
}

static int feature_callback(struct ast_bridge_channel *bridge_channel, void *obj)
{
	int *callback_executed = obj;
	(*callback_executed)++;
	return 0;
}

/* Need to post null frames periodically so DTMF emulation can work. */
static void stream_periodic_frames(struct ast_channel *chan, int ms, int interval_ms)
{
	long nanosecs;

	ast_assert(chan != NULL);
	ast_assert(0 < ms);
	ast_assert(0 < interval_ms);

	nanosecs = interval_ms * 1000000L;
	while (0 < ms) {
		ast_queue_frame(chan, &ast_null_frame);

		if (interval_ms < ms) {
			ms -= interval_ms;
		} else {
			nanosecs = ms * 1000000L;
			ms = 0;
		}
		test_nanosleep(0, nanosecs);
	}
}

AST_TEST_DEFINE(test_features_channel_dtmf)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge1, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_bridge *, bridge2, NULL, safe_bridge_destroy);
	struct ast_bridge_features features;
	int callback_executed = 0;
	struct ast_frame f = { AST_FRAME_DTMF, };

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test running DTMF hooks on a channel via the feature hooks mechanism";
		info->description =
			"This test creates two channels, adds a DTMF hook to one, places them into\n"
			"a bridge, and verifies that the DTMF hook added to the channel feature\n"
			"hooks can be triggered once the channel is bridged.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Create the bridges */
	bridge1 = ast_bridge_basic_new();
	ast_test_validate(test, bridge1 != NULL);
	bridge2 = ast_bridge_basic_new();
	ast_test_validate(test, bridge2 != NULL);

	/* Create channels that will go into the bridge */
	START_ALICE(chan_alice);
	START_BOB(chan_bob);

	/* Setup the features and add them to alice */
	ast_bridge_features_init(&features);
	ast_test_validate(test, !ast_bridge_dtmf_hook(&features, "##**", feature_callback, &callback_executed, NULL, 0));
	ast_test_validate(test, !ast_channel_feature_hooks_append(chan_alice, &features));
	ast_bridge_features_cleanup(&features);

	/* Bridge the channels */
	ast_test_validate(test, !ast_bridge_impart(bridge1, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	ast_test_validate(test, !ast_bridge_impart(bridge1, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	wait_for_bridged(chan_alice);

	/* Execute the feature */
	f.len = 100;
	f.subclass.integer = '#';
	ast_queue_frame(chan_alice, &f);
	ast_queue_frame(chan_alice, &f);
	f.subclass.integer = '*';
	ast_queue_frame(chan_alice, &f);
	ast_queue_frame(chan_alice, &f);

	stream_periodic_frames(chan_alice, 1000, 20);

	/* Remove the channels from the bridge */
	ast_test_validate(test, !ast_bridge_depart(chan_alice));
	ast_test_validate(test, !ast_bridge_depart(chan_bob));

	wait_for_unbridged(chan_alice);

	/* Bridge the channels again to ensure that the feature hook remains on the channel */
	ast_test_validate(test, !ast_bridge_impart(bridge2, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	ast_test_validate(test, !ast_bridge_impart(bridge2, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	wait_for_bridged(chan_alice);

	/* Execute the feature */
	f.len = 100;
	f.subclass.integer = '#';
	ast_queue_frame(chan_alice, &f);
	ast_queue_frame(chan_alice, &f);
	f.subclass.integer = '*';
	ast_queue_frame(chan_alice, &f);
	ast_queue_frame(chan_alice, &f);

	stream_periodic_frames(chan_alice, 1000, 20);

	/* Remove the channels from the bridge */
	ast_test_validate(test, !ast_bridge_depart(chan_alice));
	ast_test_validate(test, !ast_bridge_depart(chan_bob));

	/* Hangup the channels */
	HANGUP_CHANNEL(chan_alice);
	HANGUP_CHANNEL(chan_bob);

	ast_test_validate(test, callback_executed == 2);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_features_channel_interval)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge1, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_bridge *, bridge2, NULL, safe_bridge_destroy);
	struct ast_bridge_features features;
	int callback_executed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test running interval hooks on a channel via the feature hooks mechanism";
		info->description =
			"This test creates two channels, adds an interval hook to one, places them\n"
			"into a bridge, and verifies that the interval hook added to the channel\n"
			"feature hooks is triggered once the channel is bridged.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Create the bridges */
	bridge1 = ast_bridge_basic_new();
	ast_test_validate(test, bridge1 != NULL);
	bridge2 = ast_bridge_basic_new();
	ast_test_validate(test, bridge2 != NULL);

	/* Create channels that will go into the bridge */
	START_ALICE(chan_alice);
	START_BOB(chan_bob);

	/* Setup the features and add them to alice */
	ast_bridge_features_init(&features);
	ast_test_validate(test, !ast_bridge_interval_hook(&features, 0, 1000, feature_callback, &callback_executed, NULL, 0));
	ast_test_validate(test, !ast_channel_feature_hooks_append(chan_alice, &features));
	ast_bridge_features_cleanup(&features);

	/* Bridge the channels */
	ast_test_validate(test, !ast_bridge_impart(bridge1, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	ast_test_validate(test, !ast_bridge_impart(bridge1, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	wait_for_bridged(chan_alice);

	/* Let the interval hook execute once */
	test_nanosleep(1, 500000000);

	/* Remove the channels from the bridge */
	ast_test_validate(test, !ast_bridge_depart(chan_alice));
	ast_test_validate(test, !ast_bridge_depart(chan_bob));

	wait_for_unbridged(chan_alice);

	ast_test_validate(test, callback_executed >= 1);
	callback_executed = 0;

	/* Bridge the channels again to ensure that the feature hook remains on the channel */
	ast_test_validate(test, !ast_bridge_impart(bridge2, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	ast_test_validate(test, !ast_bridge_impart(bridge2, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	wait_for_bridged(chan_alice);

	/* Let the interval hook execute once */
	test_nanosleep(1, 500000000);

	/* Remove the channels from the bridge */
	ast_test_validate(test, !ast_bridge_depart(chan_alice));
	ast_test_validate(test, !ast_bridge_depart(chan_bob));

	/* Hangup the channels */
	HANGUP_CHANNEL(chan_alice);
	HANGUP_CHANNEL(chan_bob);

	ast_test_validate(test, callback_executed >= 1);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_features_channel_dtmf);
	AST_TEST_UNREGISTER(test_features_channel_interval);

	ast_channel_unregister(&test_features_chan_tech);

	return 0;
}

static int load_module(void)
{
	ast_channel_register(&test_features_chan_tech);

	AST_TEST_REGISTER(test_features_channel_dtmf);
	AST_TEST_REGISTER(test_features_channel_interval);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Bridge Features Unit Tests");
