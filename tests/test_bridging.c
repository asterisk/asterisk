/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Bridging unit tests
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/channel.h"
#include "asterisk/time.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/features.h"
#include "asterisk/format_cache.h"

#define TEST_CATEGORY "/main/bridging/"

#define CHANNEL_TECH_NAME "BridgingTestChannel"

#define TEST_CHANNEL_FORMAT		ast_format_slin

/*! \brief A private structure for the test channel */
struct test_bridging_chan_pvt {
	/* \brief The expected indication */
	int condition;
	/*! \brief The number of indicated things */
	unsigned int indicated;
};

/*! \brief Callback function for when a frame is written to a channel */
static int test_bridging_chan_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
	struct test_bridging_chan_pvt *test_pvt = ast_channel_tech_pvt(chan);

	if (condition == test_pvt->condition) {
		test_pvt->indicated++;
	}

	return 0;
}

/*! \brief Callback function for when a channel is hung up */
static int test_bridging_chan_hangup(struct ast_channel *chan)
{
	struct test_bridging_chan_pvt *test_pvt = ast_channel_tech_pvt(chan);

	ast_free(test_pvt);
	ast_channel_tech_pvt_set(chan, NULL);

	return 0;
}

/*! \brief A channel technology used for the unit tests */
static struct ast_channel_tech test_bridging_chan_tech = {
	.type = CHANNEL_TECH_NAME,
	.description = "Mock channel technology for bridge tests",
	.indicate = test_bridging_chan_indicate,
	.hangup = test_bridging_chan_hangup,
	.properties = AST_CHAN_TP_INTERNAL,
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

/*! \brief Wait until a channel has no frames on its read queue */
static void wait_for_empty_queue(struct ast_channel *channel)
{
	ast_channel_lock(channel);
	while (!AST_LIST_EMPTY(ast_channel_readq(channel))) {
		ast_channel_unlock(channel);
		test_nanosleep(0, 1000000);
		ast_channel_lock(channel);
	}
	ast_channel_unlock(channel);
}

/*! \brief Create a \ref test_bridging_chan_tech for Alice. */
#define START_ALICE(channel, pvt) START_CHANNEL(channel, pvt, "Alice", "100")

/*! \brief Create a \ref test_bridging_chan_tech for Bob. */
#define START_BOB(channel, pvt) START_CHANNEL(channel, pvt, "Bob", "200")

#define START_CHANNEL(channel, pvt, name, number) do { \
	channel = ast_channel_alloc(0, AST_STATE_UP, number, name, number, number, \
		"default", NULL, NULL, 0, CHANNEL_TECH_NAME "/" name); \
	pvt = ast_calloc(1, sizeof(*pvt)); \
	ast_channel_tech_pvt_set(channel, pvt); \
	ast_channel_nativeformats_set(channel, test_bridging_chan_tech.capabilities); \
	ast_channel_set_rawwriteformat(channel, TEST_CHANNEL_FORMAT); \
	ast_channel_set_rawreadformat(channel, TEST_CHANNEL_FORMAT); \
	ast_channel_set_writeformat(channel, TEST_CHANNEL_FORMAT); \
	ast_channel_set_readformat(channel, TEST_CHANNEL_FORMAT); \
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

AST_TEST_DEFINE(test_bridging_deferred_queue)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	struct test_bridging_chan_pvt *alice_pvt;
		struct ast_control_t38_parameters t38_parameters = {
			.request_response = AST_T38_REQUEST_NEGOTIATE,
		};
		struct ast_frame frame = {
			.frametype = AST_FRAME_CONTROL,
			.subclass.integer = AST_CONTROL_T38_PARAMETERS,
			.data.ptr = &t38_parameters,
			.datalen = sizeof(t38_parameters),
		};
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	struct test_bridging_chan_pvt *bob_pvt;
	RAII_VAR(struct ast_bridge *, bridge1, NULL, safe_bridge_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test that deferred frames from a channel in a bridge get written";
		info->description =
			"This test creates two channels, queues a deferrable frame on one, places it into\n"
			"a bridge, confirms the frame was read by the bridge, adds the second channel to the\n"
			"bridge, and makes sure the deferred frame is written to it.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Create the bridges */
	bridge1 = ast_bridge_basic_new();
	ast_test_validate(test, bridge1 != NULL);

	/* Create channels that will go into the bridge */
	START_ALICE(chan_alice, alice_pvt);
	START_BOB(chan_bob, bob_pvt);
	bob_pvt->condition = AST_CONTROL_T38_PARAMETERS;

	/* Bridge alice and wait for the frame to be deferred */
	ast_test_validate(test, !ast_bridge_impart(bridge1, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	wait_for_bridged(chan_alice);
	ast_queue_frame(chan_alice, &frame);
	wait_for_empty_queue(chan_alice);

	/* Bridge bob for a second so it can receive the deferred T.38 request negotiate frame */
	ast_test_validate(test, !ast_bridge_impart(bridge1, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	wait_for_bridged(chan_bob);
	stream_periodic_frames(chan_alice, 1000, 20);
	ast_test_validate(test, !ast_bridge_depart(chan_bob));
	wait_for_unbridged(chan_bob);

	/* Ensure that we received the expected indications while it was in there (request to negotiate, and to terminate) */
	ast_test_validate(test, bob_pvt->indicated == 2);

	/* Now remove alice since we are done */
	ast_test_validate(test, !ast_bridge_depart(chan_alice));
	wait_for_unbridged(chan_alice);

	/* Hangup the channels */
	HANGUP_CHANNEL(chan_alice);
	HANGUP_CHANNEL(chan_bob);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_bridging_deferred_queue);

	ast_channel_unregister(&test_bridging_chan_tech);
	ao2_cleanup(test_bridging_chan_tech.capabilities);
	test_bridging_chan_tech.capabilities = NULL;

	return 0;
}

static int load_module(void)
{
	test_bridging_chan_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!test_bridging_chan_tech.capabilities) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(test_bridging_chan_tech.capabilities, TEST_CHANNEL_FORMAT, 0);
	ast_channel_register(&test_bridging_chan_tech);

	AST_TEST_REGISTER(test_bridging_deferred_queue);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Bridging Unit Tests");
