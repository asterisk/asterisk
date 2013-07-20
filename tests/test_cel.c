/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
 * \brief CEL unit tests
 *
 * \author Kinsey Moore <kmoore@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <math.h>
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/cel.h"
#include "asterisk/channel.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/time.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_basic.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridging.h"
#include "asterisk/json.h"
#include "asterisk/features.h"
#include "asterisk/core_local.h"

#define TEST_CATEGORY "/main/cel/"

#define CHANNEL_TECH_NAME "CELTestChannel"

/*! \brief A placeholder for Asterisk's 'real' CEL configuration */
static struct ast_cel_general_config *saved_config;

/*! \brief The CEL config used for CEL unit tests */
static struct ast_cel_general_config *cel_test_config;

/*! \brief A channel technology used for the unit tests */
static struct ast_channel_tech test_cel_chan_tech = {
	.type = CHANNEL_TECH_NAME,
	.description = "Mock channel technology for CEL tests",
};

/*! \brief A 1 second sleep */
static struct timespec to_sleep = {1, 0};

static void do_sleep(void)
{
	while ((nanosleep(&to_sleep, &to_sleep) == -1) && (errno == EINTR));
}

#define APPEND_EVENT(chan, ev_type, userevent, extra, peer) do { \
	if (append_expected_event(chan, ev_type, userevent, extra, peer)) { \
		return AST_TEST_FAIL; \
	} \
	} while (0)

#define APPEND_EVENT_SNAPSHOT(snapshot, ev_type, userevent, extra, peer) do { \
	if (append_expected_event_snapshot(snapshot, ev_type, userevent, extra, peer)) { \
		return AST_TEST_FAIL; \
	} \
	} while (0)

#define APPEND_DUMMY_EVENT() do { \
	if (append_dummy_event()) { \
		return AST_TEST_FAIL; \
	} \
	} while (0)

#define CONF_EXIT(channel, bridge) do { \
	ast_test_validate(test, 0 == ast_bridge_depart(channel)); \
	CONF_EXIT_EVENT(channel, bridge); \
	} while (0)

#define CONF_EXIT_EVENT(channel, bridge) do { \
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref); \
	extra = ast_json_pack("{s: s}", "bridge_id", bridge->uniqueid); \
	ast_test_validate(test, extra != NULL); \
	APPEND_EVENT(channel, AST_CEL_CONF_EXIT, NULL, extra, NULL); \
	} while (0)

#define CONF_EXIT_SNAPSHOT(channel, bridge) do { \
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref); \
	extra = ast_json_pack("{s: s}", "bridge_id", bridge->uniqueid); \
	ast_test_validate(test, extra != NULL); \
	APPEND_EVENT_SNAPSHOT(channel, AST_CEL_CONF_EXIT, NULL, extra, NULL); \
	} while (0)

#define CONF_ENTER_EVENT(channel, bridge) do { \
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref); \
	extra = ast_json_pack("{s: s}", "bridge_id", bridge->uniqueid); \
	ast_test_validate(test, extra != NULL); \
	APPEND_EVENT(channel, AST_CEL_CONF_ENTER, NULL, extra, NULL); \
	} while (0)

#define BRIDGE_TO_CONF(first, second, third, bridge) do { \
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref); \
	extra = ast_json_pack("{s: s, s: s}", \
		"channel_name", ast_channel_name(third), \
		"bridge_id", bridge->uniqueid); \
	ast_test_validate(test, extra != NULL); \
	APPEND_EVENT(first, AST_CEL_BRIDGE_TO_CONF, NULL, extra, ast_channel_name(second)); \
	} while (0)

#define BLINDTRANSFER_EVENT(channel, bridge, extension, context) do { \
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref); \
	extra = ast_json_pack("{s: s, s: s, s: s}", \
		"extension", extension, \
		"context", context, \
		"bridge_id", bridge->uniqueid); \
	ast_test_validate(test, extra != NULL); \
	APPEND_EVENT(channel, AST_CEL_BLINDTRANSFER, NULL, extra, NULL); \
	} while (0)

#define ATTENDEDTRANSFER_BRIDGE(channel1, bridge1, channel2, bridge2) do { \
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref); \
	extra = ast_json_pack("{s: s, s: s, s: s}", \
		"bridge1_id", bridge1->uniqueid, \
		"channel2_name", ast_channel_name(channel2), \
		"bridge2_id", bridge2->uniqueid); \
	ast_test_validate(test, extra != NULL); \
	APPEND_EVENT(channel1, AST_CEL_ATTENDEDTRANSFER, NULL, extra, NULL); \
	} while (0)

/*! \brief Alice's Caller ID */
#define ALICE_CALLERID { .id.name.str = "Alice", .id.name.valid = 1, .id.number.str = "100", .id.number.valid = 1, }

/*! \brief Bob's Caller ID */
#define BOB_CALLERID { .id.name.str = "Bob", .id.name.valid = 1, .id.number.str = "200", .id.number.valid = 1, }

/*! \brief Charlie's Caller ID */
#define CHARLIE_CALLERID { .id.name.str = "Charlie", .id.name.valid = 1, .id.number.str = "300", .id.number.valid = 1, }

/*! \brief David's Caller ID */
#define DAVID_CALLERID { .id.name.str = "David", .id.name.valid = 1, .id.number.str = "400", .id.number.valid = 1, }

/*! \brief Eve's Caller ID */
#define EVE_CALLERID { .id.name.str = "Eve", .id.name.valid = 1, .id.number.str = "500", .id.number.valid = 1, }

/*! \brief Fred's Caller ID */
#define FRED_CALLERID { .id.name.str = "Fred", .id.name.valid = 1, .id.number.str = "600", .id.number.valid = 1, }

/*! \brief Create a \ref test_cel_chan_tech for Alice. */
#define CREATE_ALICE_CHANNEL(channel_var, caller_id) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, (caller_id)->id.number.str, (caller_id)->id.name.str, "100", "100", "default", NULL, 0, CHANNEL_TECH_NAME "/Alice"); \
	APPEND_EVENT(channel_var, AST_CEL_CHANNEL_START, NULL, NULL, NULL); \
	} while (0)

/*! \brief Create a \ref test_cel_chan_tech for Bob. */
#define CREATE_BOB_CHANNEL(channel_var, caller_id) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, (caller_id)->id.number.str, (caller_id)->id.name.str, "200", "200", "default", NULL, 0, CHANNEL_TECH_NAME "/Bob"); \
	APPEND_EVENT(channel_var, AST_CEL_CHANNEL_START, NULL, NULL, NULL); \
	} while (0)

/*! \brief Create a \ref test_cel_chan_tech for Charlie. */
#define CREATE_CHARLIE_CHANNEL(channel_var, caller_id) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, (caller_id)->id.number.str, (caller_id)->id.name.str, "300", "300", "default", NULL, 0, CHANNEL_TECH_NAME "/Charlie"); \
	APPEND_EVENT(channel_var, AST_CEL_CHANNEL_START, NULL, NULL, NULL); \
	} while (0)

/*! \brief Create a \ref test_cel_chan_tech for David. */
#define CREATE_DAVID_CHANNEL(channel_var, caller_id) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, (caller_id)->id.number.str, (caller_id)->id.name.str, "400", "400", "default", NULL, 0, CHANNEL_TECH_NAME "/David"); \
	APPEND_EVENT(channel_var, AST_CEL_CHANNEL_START, NULL, NULL, NULL); \
	} while (0)

/*! \brief Create a \ref test_cel_chan_tech for Eve. */
#define CREATE_EVE_CHANNEL(channel_var, caller_id) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, (caller_id)->id.number.str, (caller_id)->id.name.str, "500", "500", "default", NULL, 0, CHANNEL_TECH_NAME "/Eve"); \
	APPEND_EVENT(channel_var, AST_CEL_CHANNEL_START, NULL, NULL, NULL); \
	} while (0)

/*! \brief Create a \ref test_cel_chan_tech for Eve. */
#define CREATE_FRED_CHANNEL(channel_var, caller_id) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, (caller_id)->id.number.str, (caller_id)->id.name.str, "600", "600", "default", NULL, 0, CHANNEL_TECH_NAME "/Fred"); \
	APPEND_EVENT(channel_var, AST_CEL_CHANNEL_START, NULL, NULL, NULL); \
	} while (0)

/*! \brief Emulate a channel entering into an application */
#define EMULATE_APP_DATA(channel, priority, application, data) do { \
	if ((priority) > 0) { \
		ast_channel_priority_set((channel), (priority)); \
	} \
	ast_channel_appl_set((channel), (application)); \
	ast_channel_data_set((channel), (data)); \
	ast_channel_publish_snapshot((channel)); \
	} while (0)

#define ANSWER_CHANNEL(chan) do { \
	EMULATE_APP_DATA(chan, 1, "Answer", ""); \
	ANSWER_NO_APP(chan); \
	} while (0)

#define ANSWER_NO_APP(chan) do { \
	ast_setstate(chan, AST_STATE_UP); \
	APPEND_EVENT(chan, AST_CEL_ANSWER, NULL, NULL, NULL); \
	} while (0)

/*! \brief Hang up a test channel safely */
#define HANGUP_CHANNEL(channel, cause, dialstatus) do { \
	ast_channel_hangupcause_set((channel), (cause)); \
	ao2_ref(channel, +1); \
	ast_hangup((channel)); \
	HANGUP_EVENT(channel, cause, dialstatus); \
	APPEND_EVENT(channel, AST_CEL_CHANNEL_END, NULL, NULL, NULL); \
	ao2_cleanup(stasis_cache_get_extended(ast_channel_topic_all_cached(), \
		ast_channel_snapshot_type(), ast_channel_uniqueid(channel), 1)); \
	ao2_cleanup(channel); \
	channel = NULL; \
	} while (0)

#define HANGUP_EVENT(channel, cause, dialstatus) do { \
	RAII_VAR(struct ast_json *, extra, NULL, ast_json_unref); \
	extra = ast_json_pack("{s: i, s: s, s: s}", \
		"hangupcause", cause, \
		"hangupsource", "", \
		"dialstatus", dialstatus); \
	ast_test_validate(test, extra != NULL); \
	APPEND_EVENT(channel, AST_CEL_HANGUP, NULL, extra, NULL); \
	} while (0)

static int append_expected_event(
	struct ast_channel *chan,
	enum ast_cel_event_type type,
	const char *userdefevname,
	struct ast_json *extra, const char *peer);

static int append_expected_event_snapshot(
	struct ast_channel_snapshot *snapshot,
	enum ast_cel_event_type type,
	const char *userdefevname,
	struct ast_json *extra, const char *peer);

static int append_dummy_event(void);

static void safe_channel_release(struct ast_channel *chan)
{
	if (!chan) {
		return;
	}
	ast_channel_release(chan);
}

AST_TEST_DEFINE(test_cel_channel_creation)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test the CEL records created when a channel is created";
		info->description =
			"Test the CEL records created when a channel is created";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan, (&caller));

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_unanswered_inbound_call)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test inbound unanswered calls";
		info->description =
			"Test CEL records for a call that is\n"
			"inbound to Asterisk, executes some dialplan, but\n"
			"is never answered.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan, &caller);

	EMULATE_APP_DATA(chan, 1, "Wait", "1");

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_unanswered_outbound_call)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	struct ast_party_caller caller = {
			.id.name.str = "",
			.id.name.valid = 1,
			.id.number.str = "",
			.id.number.valid = 1, };

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test outbound unanswered calls";
		info->description =
			"Test CEL records for a call that is\n"
			"outbound to Asterisk but is never answered.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan, &caller);

	ast_channel_exten_set(chan, "s");
	ast_channel_context_set(chan, "default");
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_ORIGINATED);
	EMULATE_APP_DATA(chan, 0, "AppDial", "(Outgoing Line)");
	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_single_party)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a single party";
		info->description =
			"Test CEL records for a call that is\n"
			"answered, but only involves a single channel\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	CREATE_ALICE_CHANNEL(chan, &caller);

	ANSWER_CHANNEL(chan);
	EMULATE_APP_DATA(chan, 2, "VoiceMailMain", "1");

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_single_bridge)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a single party entering/leaving a bridge";
		info->description =
			"Test CEL records for a call that is\n"
			"answered, enters a bridge, and leaves it.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan, &caller);

	ANSWER_CHANNEL(chan);
	EMULATE_APP_DATA(chan, 2, "Bridge", "");

	do_sleep();
	ast_bridge_impart(bridge, chan, NULL, NULL, 0);

	do_sleep();

	ast_bridge_depart(chan);

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_single_bridge_continue)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a single party entering/leaving a bridge";
		info->description =
			"Test CEL records for a call that is\n"
			"answered, enters a bridge, and leaves it.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan, &caller);

	ANSWER_CHANNEL(chan);
	EMULATE_APP_DATA(chan, 2, "Bridge", "");

	do_sleep();
	ast_bridge_impart(bridge, chan, NULL, NULL, 0);

	do_sleep();

	ast_bridge_depart(chan);

	EMULATE_APP_DATA(chan, 3, "Wait", "");

	/* And then it hangs up */
	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_single_twoparty_bridge_a)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller caller_alice = ALICE_CALLERID;
	struct ast_party_caller caller_bob = BOB_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a single party entering/leaving a bridge";
		info->description =
			"Test CEL records for a call that is\n"
			"answered, enters a bridge, and leaves it. In this scenario, the\n"
			"Party A should answer the bridge first.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &caller_alice);

	CREATE_BOB_CHANNEL(chan_bob, &caller_bob);

	ANSWER_CHANNEL(chan_alice);
	EMULATE_APP_DATA(chan_alice, 2, "Bridge", "");

	ast_bridge_impart(bridge, chan_alice, NULL, NULL, 0);
	do_sleep();

	ANSWER_CHANNEL(chan_bob);
	EMULATE_APP_DATA(chan_bob, 2, "Bridge", "");

	ast_bridge_impart(bridge, chan_bob, NULL, NULL, 0);
	do_sleep();
	APPEND_EVENT(chan_alice, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_bob));

	ast_bridge_depart(chan_alice);
	ast_bridge_depart(chan_bob);
	APPEND_EVENT(chan_alice, AST_CEL_BRIDGE_END, NULL, NULL, ast_channel_name(chan_bob));

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_single_twoparty_bridge_b)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller caller_alice = ALICE_CALLERID;
	struct ast_party_caller caller_bob = BOB_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a single party entering/leaving a bridge";
		info->description =
			"Test CEL records for a call that is\n"
			"answered, enters a bridge, and leaves it. In this scenario, the\n"
			"Party B should answer the bridge first.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &caller_alice);

	CREATE_BOB_CHANNEL(chan_bob, &caller_bob);

	ANSWER_CHANNEL(chan_alice);
	EMULATE_APP_DATA(chan_alice, 2, "Bridge", "");

	ANSWER_CHANNEL(chan_bob);
	EMULATE_APP_DATA(chan_bob, 2, "Bridge", "");
	do_sleep();

	ast_bridge_impart(bridge, chan_bob, NULL, NULL, 0);
	do_sleep();

	ast_bridge_impart(bridge, chan_alice, NULL, NULL, 0);
	do_sleep();
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_alice));

	ast_bridge_depart(chan_alice);
	ast_bridge_depart(chan_bob);
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_END, NULL, NULL, ast_channel_name(chan_alice));

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_single_multiparty_bridge)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller caller_alice = ALICE_CALLERID;
	struct ast_party_caller caller_bob = BOB_CALLERID;
	struct ast_party_caller caller_charlie = CHARLIE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a single party entering/leaving a multi-party bridge";
		info->description =
			"Test CEL records for a call that is\n"
			"answered, enters a bridge, and leaves it. A total of three\n"
			"parties perform this action.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &caller_alice);
	CREATE_BOB_CHANNEL(chan_bob, &caller_bob);
	CREATE_CHARLIE_CHANNEL(chan_charlie, &caller_charlie);

	ANSWER_CHANNEL(chan_alice);
	EMULATE_APP_DATA(chan_alice, 2, "Bridge", "");

	do_sleep();

	ast_bridge_impart(bridge, chan_alice, NULL, NULL, 0);

	ANSWER_CHANNEL(chan_bob);
	EMULATE_APP_DATA(chan_bob, 2, "Bridge", "");
	do_sleep();

	ast_bridge_impart(bridge, chan_bob, NULL, NULL, 0);
	do_sleep();
	APPEND_EVENT(chan_alice, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_bob));

	ANSWER_CHANNEL(chan_charlie);
	EMULATE_APP_DATA(chan_charlie, 2, "Bridge", "");
	ast_bridge_impart(bridge, chan_charlie, NULL, NULL, 0);
	do_sleep();
	BRIDGE_TO_CONF(chan_alice, chan_bob, chan_charlie, bridge);

	CONF_EXIT(chan_alice, bridge);
	CONF_EXIT(chan_bob, bridge);
	CONF_EXIT(chan_charlie, bridge);

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

#define EMULATE_DIAL(channel, dialstring) do { \
	EMULATE_APP_DATA(channel, 1, "Dial", dialstring); \
	if (append_expected_event(channel, AST_CEL_APP_START, NULL, NULL, NULL)) { \
		return AST_TEST_FAIL; \
	} \
	} while (0)

#define START_DIALED(caller, callee) \
	START_DIALED_FULL(caller, callee, "200", "Bob")

#define START_DIALED_FULL(caller, callee, number, name) do { \
	callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, number, NULL, NULL, ast_channel_linkedid(caller), 0, CHANNEL_TECH_NAME "/" name); \
	if (append_expected_event(callee, AST_CEL_CHANNEL_START, NULL, NULL, NULL)) { \
		return AST_TEST_FAIL; \
	} \
	ast_set_flag(ast_channel_flags(callee), AST_FLAG_OUTGOING); \
	EMULATE_APP_DATA(callee, 0, "AppDial", "(Outgoing Line)"); \
	ast_channel_publish_dial(caller, callee, name, NULL); \
	} while (0)

AST_TEST_DEFINE(test_cel_dial_unanswered)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a dial that isn't answered";
		info->description =
			"Test CEL records for a channel that\n"
			"performs a dial operation that isn't answered\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "NOANSWER");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NO_ANSWER, "NOANSWER");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NO_ANSWER, "");

	return AST_TEST_PASS;
}


AST_TEST_DEFINE(test_cel_dial_busy)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a dial that results in a busy";
		info->description =
			"Test CEL records for a channel that\n"
			"performs a dial operation to an endpoint that's busy\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "BUSY");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_BUSY, "BUSY");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_BUSY, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_congestion)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a dial that results in congestion";
		info->description =
			"Test CEL records for a channel that\n"
			"performs a dial operation to an endpoint that's congested\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "CONGESTION");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_CONGESTION, "CONGESTION");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_CONGESTION, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_unavailable)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a dial that results in unavailable";
		info->description =
			"Test CEL records for a channel that\n"
			"performs a dial operation to an endpoint that's unavailable\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "CHANUNAVAIL");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NO_ROUTE_DESTINATION, "CHANUNAVAIL");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NO_ROUTE_DESTINATION, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_caller_cancel)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CEL for a dial where the caller cancels";
		info->description =
			"Test CEL records for a channel that\n"
			"performs a dial operation to an endpoint but then decides\n"
			"to hang up, cancelling the dial\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "CANCEL");

	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL, "CANCEL");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_parallel_failed)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_david, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test a parallel dial where all channels fail to answer";
		info->description =
			"This tests dialing three parties: Bob, Charlie, David. Charlie\n"
			"returns BUSY; David returns CONGESTION; Bob fails to answer and\n"
			"Alice hangs up. Three records are created for Alice as a result.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	/* Channel enters Dial app */
	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob&" CHANNEL_TECH_NAME "/Charlie&" CHANNEL_TECH_NAME "/David");

	/* Outbound channels are created */
	START_DIALED_FULL(chan_caller, chan_bob, "200", "Bob");
	START_DIALED_FULL(chan_caller, chan_charlie, "300", "Charlie");
	START_DIALED_FULL(chan_caller, chan_david, "400", "David");

	/* Dial starts */
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);

	/* Charlie is busy */
	ast_channel_publish_dial(chan_caller, chan_charlie, NULL, "BUSY");
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_BUSY, "");

	/* David is congested */
	ast_channel_publish_dial(chan_caller, chan_david, NULL, "CONGESTION");
	HANGUP_CHANNEL(chan_david, AST_CAUSE_CONGESTION, "");

	/* Bob is canceled */
	ast_channel_publish_dial(chan_caller, chan_bob, NULL, "CANCEL");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");

	/* Alice hangs up */
	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL, "BUSY");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_answer_no_bridge)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test dialing, answering, and not going into a bridge.";
		info->description =
			"This is a weird one, but theoretically possible. You can perform\n"
			"a dial, then bounce both channels to different priorities and\n"
			"never have them enter a bridge together. Ew. This makes sure that\n"
			"when we answer, we get a CEL, it gets ended at that point, and\n"
			"that it gets finalized appropriately.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "ANSWER");

	ANSWER_NO_APP(chan_caller);
	ast_clear_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	ANSWER_NO_APP(chan_callee);

	EMULATE_APP_DATA(chan_caller, 2, "Wait", "1");
	EMULATE_APP_DATA(chan_callee, 1, "Wait", "1");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL, "ANSWER");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_answer_twoparty_bridge_a)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test dialing, answering, and going into a 2-party bridge";
		info->description =
			"The most 'basic' of scenarios\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "ANSWER");

	ANSWER_NO_APP(chan_caller);
	ANSWER_NO_APP(chan_callee);

	do_sleep();

	ast_bridge_impart(bridge, chan_caller, NULL, NULL, 0);
	do_sleep();
	ast_bridge_impart(bridge, chan_callee, NULL, NULL, 0);
	do_sleep();
	APPEND_EVENT(chan_caller, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_callee));

	ast_bridge_depart(chan_caller);
	ast_bridge_depart(chan_callee);
	APPEND_EVENT(chan_caller, AST_CEL_BRIDGE_END, NULL, NULL, ast_channel_name(chan_callee));

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL, "ANSWER");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_answer_twoparty_bridge_b)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test dialing, answering, and going into a 2-party bridge";
		info->description =
			"The most 'basic' of scenarios\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "ANSWER");

	ANSWER_NO_APP(chan_caller);
	ANSWER_NO_APP(chan_callee);

	do_sleep();
	ast_bridge_impart(bridge, chan_callee, NULL, NULL, 0);
	do_sleep();
	ast_bridge_impart(bridge, chan_caller, NULL, NULL, 0);
	do_sleep();
	APPEND_EVENT(chan_callee, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_caller));

	ast_bridge_depart(chan_caller);
	ast_bridge_depart(chan_callee);
	APPEND_EVENT(chan_callee, AST_CEL_BRIDGE_END, NULL, NULL, ast_channel_name(chan_caller));

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL, "ANSWER");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_answer_multiparty)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_david, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_party_caller charlie_caller = CHARLIE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test dialing, answering, and going into a multi-party bridge";
		info->description =
			"A little tricky to get to do, but possible with some redirects.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller);

	EMULATE_DIAL(chan_alice, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_alice, chan_bob);

	CREATE_CHARLIE_CHANNEL(chan_charlie, &charlie_caller);
	EMULATE_DIAL(chan_charlie, CHANNEL_TECH_NAME "/Bob");

	START_DIALED_FULL(chan_charlie, chan_david, "400", "David");

	ast_channel_state_set(chan_alice, AST_STATE_RINGING);
	ast_channel_state_set(chan_charlie, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_alice, chan_bob, NULL, "ANSWER");
	ast_channel_publish_dial(chan_charlie, chan_david, NULL, "ANSWER");

	ANSWER_NO_APP(chan_alice);
	ANSWER_NO_APP(chan_bob);
	ANSWER_NO_APP(chan_charlie);
	ANSWER_NO_APP(chan_david);

	do_sleep();
	ast_test_validate(test, 0 == ast_bridge_impart(bridge, chan_charlie, NULL, NULL, 0));
	do_sleep();
	ast_test_validate(test, 0 == ast_bridge_impart(bridge, chan_david, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_charlie, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_david));

	ast_test_validate(test, 0 == ast_bridge_impart(bridge, chan_bob, NULL, NULL, 0));
	do_sleep();
	BRIDGE_TO_CONF(chan_charlie, chan_david, chan_bob, bridge);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge, chan_alice, NULL, NULL, 0));
	do_sleep();
	CONF_ENTER_EVENT(chan_alice, bridge);

	CONF_EXIT(chan_alice, bridge);
	CONF_EXIT(chan_bob, bridge);
	CONF_EXIT(chan_charlie, bridge);
	CONF_EXIT(chan_david, bridge);

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "ANSWER");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL, "ANSWER");
	HANGUP_CHANNEL(chan_david, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_blind_transfer)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_party_caller bob_caller = BOB_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test blind transfers to an extension";
		info->description =
			"This test creates two channels, bridges them, and then"
			" blind transfers the bridge to an extension.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller);
	CREATE_BOB_CHANNEL(chan_bob, &bob_caller);

	ANSWER_NO_APP(chan_alice);
	ANSWER_NO_APP(chan_bob);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge, chan_bob, NULL, NULL, 0));
	do_sleep();

	ast_test_validate(test, 0 == ast_bridge_impart(bridge, chan_alice, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_alice));

	BLINDTRANSFER_EVENT(chan_alice, bridge, "transfer_extension", "transfer_context");
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_END, NULL, NULL, ast_channel_name(chan_alice));

	ast_bridge_transfer_blind(1, chan_alice, "transfer_extension", "transfer_context", NULL, NULL);

	ast_test_validate(test, 0 == ast_bridge_depart(chan_bob));


	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_attended_transfer_bridges_swap)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_fred, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, bridge2, NULL, ao2_cleanup);
	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_party_caller bob_caller = BOB_CALLERID;
	struct ast_party_caller charlie_caller = CHARLIE_CALLERID;
	struct ast_party_caller fred_caller = ALICE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test attended transfers between two pairs of bridged parties";
		info->description =
			"This test creates four channels, places each pair in"
			" a bridge, and then attended transfers the bridges"
			" together.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	/* Create first set of bridged parties */
	bridge1 = ast_bridge_basic_new();
	ast_test_validate(test, bridge1 != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller);
	CREATE_BOB_CHANNEL(chan_bob, &bob_caller);
	ANSWER_NO_APP(chan_alice);
	ANSWER_NO_APP(chan_bob);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_bob, NULL, NULL, 0));
	do_sleep();

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_alice, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_alice));

	/* Create second set of bridged parties */
	bridge2 = ast_bridge_basic_new();
	ast_test_validate(test, bridge2 != NULL);

	CREATE_FRED_CHANNEL(chan_fred, &fred_caller);
	CREATE_CHARLIE_CHANNEL(chan_charlie, &charlie_caller);
	ANSWER_NO_APP(chan_fred);
	ANSWER_NO_APP(chan_charlie);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_charlie, NULL, NULL, 0));
	do_sleep();

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_fred, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_charlie, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_fred));

	/* Perform attended transfer */
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_END, NULL, NULL, ast_channel_name(chan_alice));

	ast_bridge_transfer_attended(chan_alice, chan_fred);
	do_sleep();
	BRIDGE_TO_CONF(chan_charlie, chan_fred, chan_bob, bridge2);
	CONF_EXIT_EVENT(chan_fred, bridge2);

	ATTENDEDTRANSFER_BRIDGE(chan_alice, bridge1, chan_fred, bridge2);

	CONF_EXIT(chan_bob, bridge2);
	CONF_EXIT(chan_charlie, bridge2);

	do_sleep();
	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_fred, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_attended_transfer_bridges_merge)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_david, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_eve, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_fred, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, bridge2, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, eve_tmp_snapshot, NULL, ao2_cleanup);
	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_party_caller bob_caller = BOB_CALLERID;
	struct ast_party_caller charlie_caller = CHARLIE_CALLERID;
	struct ast_party_caller david_caller = DAVID_CALLERID;
	struct ast_party_caller eve_caller = EVE_CALLERID;
	struct ast_party_caller fred_caller = EVE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test attended transfers between two pairs of"
			" bridged parties that results in a bridge merge";
		info->description =
			"This test creates six channels, places each triplet"
			" in a bridge, and then attended transfers the bridges"
			" together causing a bridge merge.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	/* Create first set of bridged parties */
	bridge1 = ast_bridge_basic_new();
	ast_test_validate(test, bridge1 != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller);
	CREATE_BOB_CHANNEL(chan_bob, &bob_caller);
	CREATE_DAVID_CHANNEL(chan_david, &david_caller);
	ANSWER_NO_APP(chan_alice);
	ANSWER_NO_APP(chan_bob);
	ANSWER_NO_APP(chan_david);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_bob, NULL, NULL, 0));
	do_sleep();

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_alice, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_alice));

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_david, NULL, NULL, 0));
	do_sleep();
	BRIDGE_TO_CONF(chan_bob, chan_alice, chan_david, bridge1);

	/* Create second set of bridged parties */
	bridge2 = ast_bridge_basic_new();
	ast_test_validate(test, bridge2 != NULL);

	CREATE_FRED_CHANNEL(chan_fred, &fred_caller);
	CREATE_CHARLIE_CHANNEL(chan_charlie, &charlie_caller);
	CREATE_EVE_CHANNEL(chan_eve, &eve_caller);
	ANSWER_NO_APP(chan_fred);
	ANSWER_NO_APP(chan_charlie);
	ANSWER_NO_APP(chan_eve);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_charlie, NULL, NULL, 0));
	do_sleep();

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_fred, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_charlie, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_fred));

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_eve, NULL, NULL, 0));
	do_sleep();
	BRIDGE_TO_CONF(chan_charlie, chan_fred, chan_eve, bridge2);

	/* Perform attended transfer */
	CONF_EXIT_EVENT(chan_charlie, bridge2);
	eve_tmp_snapshot = ast_channel_snapshot_create(chan_eve);
	ast_bridge_transfer_attended(chan_alice, chan_fred);
	do_sleep();
	CONF_ENTER_EVENT(chan_charlie, bridge1);

	/* Fred goes away */
	CONF_EXIT_EVENT(chan_fred, bridge2);
	CONF_EXIT_SNAPSHOT(eve_tmp_snapshot, bridge2);
	CONF_ENTER_EVENT(chan_eve, bridge1);

	/* Alice goes away */
	CONF_EXIT_EVENT(chan_alice, bridge1);

	ATTENDEDTRANSFER_BRIDGE(chan_alice, bridge1, chan_fred, bridge2);

	CONF_EXIT(chan_bob, bridge1);
	CONF_EXIT(chan_charlie, bridge1);
	CONF_EXIT(chan_david, bridge1);
	CONF_EXIT(chan_eve, bridge1);

	do_sleep();
	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_fred, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_david, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_eve, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_attended_transfer_bridges_link)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_david, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_eve, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_fred, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, bridge2, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, eve_tmp_snapshot, NULL, ao2_cleanup);
	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_party_caller bob_caller = BOB_CALLERID;
	struct ast_party_caller charlie_caller = CHARLIE_CALLERID;
	struct ast_party_caller david_caller = DAVID_CALLERID;
	struct ast_party_caller eve_caller = EVE_CALLERID;
	struct ast_party_caller fred_caller = EVE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test attended transfers between two pairs of"
			" bridged parties that results in a bridge merge";
		info->description =
			"This test creates six channels, places each triplet"
			" in a bridge, and then attended transfers the bridges"
			" together causing a bridge merge.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	/* Create first set of bridged parties */
	bridge1 = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_1TO1MIX | AST_BRIDGE_CAPABILITY_NATIVE | AST_BRIDGE_CAPABILITY_MULTIMIX,
		AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM
		| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_PROHIBITED | AST_BRIDGE_FLAG_SMART);
	ast_test_validate(test, bridge1 != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller);
	CREATE_BOB_CHANNEL(chan_bob, &bob_caller);
	CREATE_DAVID_CHANNEL(chan_david, &david_caller);
	ANSWER_NO_APP(chan_alice);
	ANSWER_NO_APP(chan_bob);
	ANSWER_NO_APP(chan_david);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_bob, NULL, NULL, 0));
	do_sleep();

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_alice, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_bob, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_alice));

	ast_test_validate(test, 0 == ast_bridge_impart(bridge1, chan_david, NULL, NULL, 0));
	do_sleep();
	BRIDGE_TO_CONF(chan_bob, chan_alice, chan_david, bridge1);

	/* Create second set of bridged parties */
	bridge2 = ast_bridge_basic_new();
	ast_test_validate(test, bridge2 != NULL);

	CREATE_FRED_CHANNEL(chan_fred, &fred_caller);
	CREATE_CHARLIE_CHANNEL(chan_charlie, &charlie_caller);
	CREATE_EVE_CHANNEL(chan_eve, &eve_caller);
	ANSWER_NO_APP(chan_fred);
	ANSWER_NO_APP(chan_charlie);
	ANSWER_NO_APP(chan_eve);

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_charlie, NULL, NULL, 0));
	do_sleep();

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_fred, NULL, NULL, 0));
	do_sleep();
	APPEND_EVENT(chan_charlie, AST_CEL_BRIDGE_START, NULL, NULL, ast_channel_name(chan_fred));

	ast_test_validate(test, 0 == ast_bridge_impart(bridge2, chan_eve, NULL, NULL, 0));
	do_sleep();
	BRIDGE_TO_CONF(chan_charlie, chan_fred, chan_eve, bridge2);

	/* Perform attended transfer */
	ast_bridge_transfer_attended(chan_alice, chan_fred);
	do_sleep();

	/* Append dummy event for the link channel ;1 start */
	APPEND_DUMMY_EVENT();

	/* Append dummy event for the link channel ;2 start */
	APPEND_DUMMY_EVENT();

	/* Append dummy event for the link channel ;2 answer */
	APPEND_DUMMY_EVENT();

	ATTENDEDTRANSFER_BRIDGE(chan_alice, bridge1, chan_fred, bridge2);

	/* Append dummy event for the link channel ;1 enter */
	APPEND_DUMMY_EVENT();

	/* Append dummy events for the link channel ;2 enter and Alice's exit,
	 * must both be dummies since they're racing */
	APPEND_DUMMY_EVENT();
	APPEND_DUMMY_EVENT();

	/* Append dummy events for the link channel ;1 answer and Fred's exit,
	 * must both be dummies since they're racing */
	APPEND_DUMMY_EVENT();
	APPEND_DUMMY_EVENT();

	CONF_EXIT(chan_bob, bridge1);
	CONF_EXIT(chan_charlie, bridge2);
	CONF_EXIT(chan_david, bridge1);
	CONF_EXIT(chan_eve, bridge2);

	do_sleep();
	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_fred, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_david, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_eve, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_dial_pickup)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_party_caller charlie_caller = CHARLIE_CALLERID;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test call pickup";
		info->description =
			"Test CEL records for a call that is\n"
			"inbound to Asterisk, executes some dialplan, and\n"
			"is picked up.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	CREATE_ALICE_CHANNEL(chan_caller, &caller);

	EMULATE_DIAL(chan_caller, CHANNEL_TECH_NAME "/Bob");

	START_DIALED(chan_caller, chan_callee);

	ast_channel_state_set(chan_caller, AST_STATE_RINGING);

	CREATE_CHARLIE_CHANNEL(chan_charlie, &charlie_caller);

	{
		SCOPED_CHANNELLOCK(lock, chan_callee);
		APPEND_EVENT(chan_callee, AST_CEL_PICKUP, NULL, NULL, ast_channel_name(chan_charlie));
		ast_test_validate(test, 0 == ast_do_pickup(chan_charlie, chan_callee));
	}

	/* Hang up the masqueraded zombie */
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL, "");

	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "ANSWER");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL, "ANSWER");
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_cel_local_optimize)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_party_caller bob_caller = BOB_CALLERID;
	RAII_VAR(struct ast_multi_channel_blob *, mc_blob, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, alice_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, bob_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, local_opt_begin, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, local_opt_end, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test local channel optimization record generation";
		info->description =
			"Test CEL records for two local channels being optimized\n"
			"out by sending a messages indicating local optimization\n"
			"begin and end\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	mc_blob = ast_multi_channel_blob_create(ast_json_null());
	ast_test_validate(test, mc_blob != NULL);

	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller);
	CREATE_BOB_CHANNEL(chan_bob, &bob_caller);

	alice_snapshot = ast_channel_snapshot_create(chan_alice);
	ast_test_validate(test, alice_snapshot != NULL);

	bob_snapshot = ast_channel_snapshot_create(chan_bob);
	ast_test_validate(test, bob_snapshot != NULL);

	ast_multi_channel_blob_add_channel(mc_blob, "1", alice_snapshot);
	ast_multi_channel_blob_add_channel(mc_blob, "2", bob_snapshot);

	local_opt_begin = stasis_message_create(ast_local_optimization_begin_type(), mc_blob);
	ast_test_validate(test, local_opt_begin != NULL);

	local_opt_end = stasis_message_create(ast_local_optimization_end_type(), mc_blob);
	ast_test_validate(test, local_opt_end != NULL);

	stasis_publish(ast_channel_topic(chan_alice), local_opt_begin);
	stasis_publish(ast_channel_topic(chan_alice), local_opt_end);
	APPEND_EVENT_SNAPSHOT(alice_snapshot, AST_CEL_LOCAL_OPTIMIZE, NULL, NULL, bob_snapshot->name);

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL, "");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL, "");

	return AST_TEST_PASS;
}

/*! Subscription for CEL events */
static struct ast_event_sub *event_sub = NULL;

/*! Container for astobj2 duplicated ast_events */
static struct ao2_container *cel_received_events = NULL;

/*! Container for expected CEL events */
static struct ao2_container *cel_expected_events = NULL;

static struct ast_event *ao2_dup_event(const struct ast_event *event)
{
	struct ast_event *event_dup;
	uint16_t event_len;

	event_len = ast_event_get_size(event);

	event_dup = ao2_alloc(event_len, NULL);
	if (!event_dup) {
		return NULL;
	}

	memcpy(event_dup, event, event_len);

	return event_dup;
}

static int append_event(struct ast_event *ev)
{
	RAII_VAR(struct ast_event *, ao2_ev, NULL, ao2_cleanup);
	ao2_ev = ao2_dup_event(ev);
	if (!ao2_ev) {
		return -1;
	}

	ao2_link(cel_expected_events, ao2_ev);
	return 0;
}

static int append_dummy_event(void)
{
	RAII_VAR(struct ast_event *, ev, NULL, ast_free);
	RAII_VAR(struct ast_event *, ao2_ev, NULL, ao2_cleanup);

	ev = ast_event_new(AST_EVENT_CUSTOM, AST_EVENT_IE_END);
	if (!ev) {
		return -1;
	}

	return append_event(ev);
}

static int append_expected_event_snapshot(
	struct ast_channel_snapshot *snapshot,
	enum ast_cel_event_type type,
	const char *userdefevname,
	struct ast_json *extra, const char *peer)
{
	RAII_VAR(struct ast_event *, ev, NULL, ast_free);
	ev = ast_cel_create_event(snapshot, type, userdefevname, extra, peer);
	if (!ev) {
		return -1;
	}

	return append_event(ev);
}

static int append_expected_event(
	struct ast_channel *chan,
	enum ast_cel_event_type type,
	const char *userdefevname,
	struct ast_json *extra, const char *peer)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	snapshot = ast_channel_snapshot_create(chan);
	if (!snapshot) {
		return -1;
	}

	return append_expected_event_snapshot(snapshot, type, userdefevname, extra, peer);
}

ast_mutex_t sync_lock;
ast_cond_t sync_out;

static void test_sub(const struct ast_event *event, void *data)
{
	struct ast_event *event_dup = ao2_dup_event(event);
	const char *sync_tag;
	if (!event_dup) {
		return;
	}

	sync_tag = ast_event_get_ie_str(event, AST_EVENT_IE_SERVICE);
	if (sync_tag) {
		if (!strcmp(sync_tag, "SYNC")) {
			/* trigger things */
			SCOPED_MUTEX(lock, &sync_lock);
			ast_cond_signal(&sync_out);
			return;
		}
	}
	/* save the event for later processing */
	ao2_link(cel_received_events, event_dup);
}

/*!
 * \internal \brief Callback function called before each test executes
 */
static int test_cel_init_cb(struct ast_test_info *info, struct ast_test *test)
{
	ast_assert(event_sub == NULL);
	ast_assert(cel_received_events == NULL);
	ast_assert(cel_expected_events == NULL);

	ast_mutex_init(&sync_lock);
	ast_cond_init(&sync_out, NULL);

	/* Back up the real CEL config and insert the test's config */
	saved_config = ast_cel_get_config();
	ast_cel_set_config(cel_test_config);

	/* init CEL event storage (degenerate hash table becomes a linked list) */
	cel_received_events = ao2_container_alloc(1, NULL, NULL);
	cel_expected_events = ao2_container_alloc(1, NULL, NULL);

	/* start the CEL event callback */
	event_sub = ast_event_subscribe(AST_EVENT_CEL, test_sub, "CEL Test Logging",
		NULL, AST_EVENT_IE_END);
	return 0;
}

/*! \brief Check an IE value from two events,  */
static int match_ie_val(
	const struct ast_event *event1,
	const struct ast_event *event2,
	enum ast_event_ie_type type)
{
	enum ast_event_ie_pltype pltype = ast_event_get_ie_pltype(type);

	switch (pltype) {
	case AST_EVENT_IE_PLTYPE_UINT:
	{
		uint32_t val = ast_event_get_ie_uint(event2, type);

		return (val == ast_event_get_ie_uint(event1, type)) ? 1 : 0;
	}
	case AST_EVENT_IE_PLTYPE_STR:
	{
		const char *str;
		uint32_t hash;

		hash = ast_event_get_ie_str_hash(event2, type);
		if (hash != ast_event_get_ie_str_hash(event1, type)) {
			return 0;
		}

		str = ast_event_get_ie_str(event2, type);
		if (str) {
			const char *e1str, *e2str;
			e1str = ast_event_get_ie_str(event1, type);
			e2str = str;

			if (type == AST_EVENT_IE_DEVICE) {
				e1str = ast_tech_to_upper(ast_strdupa(e1str));
				e2str = ast_tech_to_upper(ast_strdupa(e2str));
			}

			if (!strcmp(e1str, e2str)) {
				return 1;
			}
		}

		return 0;
	}
	default:
		break;
	}
	return 0;
}

static int events_are_equal(struct ast_event *received, struct ast_event *expected)
{
	struct ast_event_iterator iterator;
	int res;

	if (ast_event_get_type(expected) == AST_EVENT_CUSTOM) {
		/* this event is flagged as a wildcard match */
		return 1;
	}

	for (res = ast_event_iterator_init(&iterator, received); !res; res = ast_event_iterator_next(&iterator)) {
		/* XXX ignore sec/usec for now */
		/* ignore EID */
		int ie_type = ast_event_iterator_get_ie_type(&iterator);
		if (ie_type != AST_EVENT_IE_CEL_EVENT_TIME_USEC
			&& ie_type != AST_EVENT_IE_EID
			&& ie_type != AST_EVENT_IE_CEL_EVENT_TIME
			&& !match_ie_val(received, expected, ie_type)) {
			ast_log(LOG_ERROR, "Failed matching on field %s\n", ast_event_get_ie_type_name(ie_type));
			return 0;
		}
	}

	return 1;
}

static int dump_event(struct ast_event *event)
{
	struct ast_event_iterator i;

	if (ast_event_iterator_init(&i, event)) {
		ast_log(LOG_ERROR, "Failed to initialize event iterator.  :-(\n");
		return 0;
	}

	ast_log(LOG_ERROR, "Event: %s %s\n", ast_event_get_type_name(event),
		ast_cel_get_type_name(ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TYPE)));

	do {
		enum ast_event_ie_type ie_type;
		enum ast_event_ie_pltype ie_pltype;
		const char *ie_type_name;

		ie_type = ast_event_iterator_get_ie_type(&i);
		ie_type_name = ast_event_get_ie_type_name(ie_type);
		ie_pltype = ast_event_get_ie_pltype(ie_type);

		switch (ie_pltype) {
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
		case AST_EVENT_IE_PLTYPE_EXISTS:
			ast_log(LOG_ERROR, "%s\n", ie_type_name);
			break;
		case AST_EVENT_IE_PLTYPE_STR:
			ast_log(LOG_ERROR, "%.30s: %s\n", ie_type_name,
					ast_event_iterator_get_ie_str(&i));
			break;
		case AST_EVENT_IE_PLTYPE_UINT:
			ast_log(LOG_ERROR, "%.30s: %u\n", ie_type_name,
					ast_event_iterator_get_ie_uint(&i));
			break;
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
			ast_log(LOG_ERROR, "%.30s: %u\n", ie_type_name,
					ast_event_iterator_get_ie_bitflags(&i));
		default:
			break;
		}
	} while (!ast_event_iterator_next(&i));

	ast_log(LOG_ERROR, "\n");

	return 0;
}

static int check_events(struct ao2_container *local_expected, struct ao2_container *local_received)
{
	struct ao2_iterator expected_it, received_it;
	struct ast_event *rx_event, *ex_event;
	int debug = 0;

	if (ao2_container_count(local_expected) != ao2_container_count(local_received)) {
		ast_log(LOG_ERROR, "Increasing verbosity since the number of expected events (%d)"
			" did not match number of received events (%d).\n",
			ao2_container_count(local_expected),
			ao2_container_count(local_received));
		debug = 1;
	}

	expected_it = ao2_iterator_init(local_expected, 0);
	received_it = ao2_iterator_init(local_received, 0);
	rx_event = ao2_iterator_next(&received_it);
	ex_event = ao2_iterator_next(&expected_it);
	while (rx_event && ex_event) {
		if (!events_are_equal(rx_event, ex_event)) {
			ast_log(LOG_ERROR, "Received event:\n");
			dump_event(rx_event);
			ast_log(LOG_ERROR, "Expected event:\n");
			dump_event(ex_event);
			return -1;
		}
		if (debug) {
			ast_log(LOG_ERROR, "Compared events successfully%s\n", ast_event_get_type(ex_event) == AST_EVENT_CUSTOM ? " (wildcard match)" : "");
			dump_event(rx_event);
		}
		ao2_cleanup(rx_event);
		ao2_cleanup(ex_event);
		rx_event = ao2_iterator_next(&received_it);
		ex_event = ao2_iterator_next(&expected_it);
	}

	if (rx_event) {
		ast_log(LOG_ERROR, "Received event:\n");
		dump_event(rx_event);
		ao2_cleanup(rx_event);
		return -1;
	}
	if (ex_event) {
		ast_log(LOG_ERROR, "Expected event:\n");
		dump_event(ex_event);
		ao2_cleanup(ex_event);
		return -1;
	}
	return 0;
}

static struct ast_event *create_sync_event(void)
{
	struct ast_event *event_dup;
	RAII_VAR(struct ast_event *, event, ao2_callback(cel_expected_events, 0, NULL, NULL), ao2_cleanup);
	uint16_t event_len;

	if (!event) {
		return NULL;
	}

	event_len = ast_event_get_size(event);

	event_dup = ast_calloc(1, event_len);
	if (!event_dup) {
		return NULL;
	}

	memcpy(event_dup, event, event_len);
	ast_event_append_ie_str(&event_dup, AST_EVENT_IE_SERVICE, "SYNC");

	return event_dup;
}

/*!
 * \internal \brief Callback function called after each test executes.
 * In addition to cleanup, this function also performs verification
 * that the events received during a test match the events that were
 * expected to have been generated during the test.
 */
static int cel_verify_and_cleanup_cb(struct ast_test_info *info, struct ast_test *test)
{
	struct ast_event *sync;
	RAII_VAR(struct ao2_container *, local_expected, cel_expected_events, ao2_cleanup);
	RAII_VAR(struct ao2_container *, local_received, cel_received_events, ao2_cleanup);
	ast_assert(event_sub != NULL);
	ast_assert(cel_received_events != NULL);
	ast_assert(cel_expected_events != NULL);

	do_sleep();

	/* sync with the event system */
	sync = create_sync_event();
	ast_test_validate(test, sync != NULL);
	if (ast_event_queue(sync)) {
		ast_event_destroy(sync);
		ast_test_validate(test, NULL);
	} else {
		struct timeval start = ast_tvnow();
		struct timespec end = {
			.tv_sec = start.tv_sec + 30,
			.tv_nsec = start.tv_usec * 1000
		};

		SCOPED_MUTEX(lock, &sync_lock);
		ast_cond_timedwait(&sync_out, &sync_lock, &end);
	}

	/* stop the CEL event callback and clean up storage structures*/
	ast_event_unsubscribe(event_sub);
	event_sub = NULL;

	/* cleaned up by RAII_VAR's */
	cel_expected_events = NULL;
	cel_received_events = NULL;

	/* check events */
	ast_test_validate(test, !check_events(local_expected, local_received));

	/* Restore the real CEL config */
	ast_cel_set_config(saved_config);
	ao2_cleanup(saved_config);
	saved_config = NULL;

	/* clean up the locks */
	ast_mutex_destroy(&sync_lock);
	ast_cond_destroy(&sync_out);
	return 0;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_cel_channel_creation);
	AST_TEST_UNREGISTER(test_cel_unanswered_inbound_call);
	AST_TEST_UNREGISTER(test_cel_unanswered_outbound_call);
	AST_TEST_UNREGISTER(test_cel_single_party);
	AST_TEST_UNREGISTER(test_cel_single_bridge);
	AST_TEST_UNREGISTER(test_cel_single_bridge_continue);
	AST_TEST_UNREGISTER(test_cel_single_twoparty_bridge_a);
	AST_TEST_UNREGISTER(test_cel_single_twoparty_bridge_b);
	AST_TEST_UNREGISTER(test_cel_single_multiparty_bridge);

	AST_TEST_UNREGISTER(test_cel_dial_unanswered);
	AST_TEST_UNREGISTER(test_cel_dial_congestion);
	AST_TEST_UNREGISTER(test_cel_dial_busy);
	AST_TEST_UNREGISTER(test_cel_dial_unavailable);
	AST_TEST_UNREGISTER(test_cel_dial_caller_cancel);
	AST_TEST_UNREGISTER(test_cel_dial_parallel_failed);
	AST_TEST_UNREGISTER(test_cel_dial_answer_no_bridge);
	AST_TEST_UNREGISTER(test_cel_dial_answer_twoparty_bridge_a);
	AST_TEST_UNREGISTER(test_cel_dial_answer_twoparty_bridge_b);
	AST_TEST_UNREGISTER(test_cel_dial_answer_multiparty);

	AST_TEST_UNREGISTER(test_cel_blind_transfer);
	AST_TEST_UNREGISTER(test_cel_attended_transfer_bridges_swap);
	AST_TEST_UNREGISTER(test_cel_attended_transfer_bridges_merge);
	AST_TEST_UNREGISTER(test_cel_attended_transfer_bridges_link);

	AST_TEST_UNREGISTER(test_cel_dial_pickup);

	AST_TEST_UNREGISTER(test_cel_local_optimize);

	ast_channel_unregister(&test_cel_chan_tech);

	ao2_cleanup(cel_test_config);
	cel_test_config = NULL;

	return 0;
}

static int load_module(void)
{
	/* build the test config */
	cel_test_config = ast_cel_general_config_alloc();
	if (!cel_test_config) {
		return -1;
	}
	cel_test_config->enable = 1;
	if (ast_str_container_add(cel_test_config->apps, "dial")) {
		return -1;
	}
	if (ast_str_container_add(cel_test_config->apps, "park")) {
		return -1;
	}
	if (ast_str_container_add(cel_test_config->apps, "queue")) {
		return -1;
	}
	cel_test_config->events |= 1<<AST_CEL_APP_START;
	cel_test_config->events |= 1<<AST_CEL_CHANNEL_START;
	cel_test_config->events |= 1<<AST_CEL_CHANNEL_END;
	cel_test_config->events |= 1<<AST_CEL_ANSWER;
	cel_test_config->events |= 1<<AST_CEL_HANGUP;
	cel_test_config->events |= 1<<AST_CEL_BRIDGE_START;
	cel_test_config->events |= 1<<AST_CEL_BRIDGE_END;
	cel_test_config->events |= 1<<AST_CEL_BRIDGE_TO_CONF;
	cel_test_config->events |= 1<<AST_CEL_CONF_ENTER;
	cel_test_config->events |= 1<<AST_CEL_CONF_EXIT;
	cel_test_config->events |= 1<<AST_CEL_BLINDTRANSFER;
	cel_test_config->events |= 1<<AST_CEL_ATTENDEDTRANSFER;
	cel_test_config->events |= 1<<AST_CEL_PICKUP;
	cel_test_config->events |= 1<<AST_CEL_LOCAL_OPTIMIZE;

	ast_channel_register(&test_cel_chan_tech);

	AST_TEST_REGISTER(test_cel_channel_creation);
	AST_TEST_REGISTER(test_cel_unanswered_inbound_call);
	AST_TEST_REGISTER(test_cel_unanswered_outbound_call);

	AST_TEST_REGISTER(test_cel_single_party);
	AST_TEST_REGISTER(test_cel_single_bridge);
	AST_TEST_REGISTER(test_cel_single_bridge_continue);
	AST_TEST_REGISTER(test_cel_single_twoparty_bridge_a);
	AST_TEST_REGISTER(test_cel_single_twoparty_bridge_b);
	AST_TEST_REGISTER(test_cel_single_multiparty_bridge);

	AST_TEST_REGISTER(test_cel_dial_unanswered);
	AST_TEST_REGISTER(test_cel_dial_congestion);
	AST_TEST_REGISTER(test_cel_dial_busy);
	AST_TEST_REGISTER(test_cel_dial_unavailable);
	AST_TEST_REGISTER(test_cel_dial_caller_cancel);
	AST_TEST_REGISTER(test_cel_dial_parallel_failed);
	AST_TEST_REGISTER(test_cel_dial_answer_no_bridge);
	AST_TEST_REGISTER(test_cel_dial_answer_twoparty_bridge_a);
	AST_TEST_REGISTER(test_cel_dial_answer_twoparty_bridge_b);
	AST_TEST_REGISTER(test_cel_dial_answer_multiparty);

	AST_TEST_REGISTER(test_cel_blind_transfer);
	AST_TEST_REGISTER(test_cel_attended_transfer_bridges_swap);
	AST_TEST_REGISTER(test_cel_attended_transfer_bridges_merge);
	AST_TEST_REGISTER(test_cel_attended_transfer_bridges_link);

	AST_TEST_REGISTER(test_cel_dial_pickup);

	AST_TEST_REGISTER(test_cel_local_optimize);

	/* ast_test_register_* has to happen after AST_TEST_REGISTER */
	/* Verify received vs expected events and clean things up after every test */
	ast_test_register_init(TEST_CATEGORY, test_cel_init_cb);
	ast_test_register_cleanup(TEST_CATEGORY, cel_verify_and_cleanup_cb);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "CEL unit tests");
