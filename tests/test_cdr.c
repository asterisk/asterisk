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
 * \file
 * \brief CDR unit tests
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <math.h>
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/cdr.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/time.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/format_cache.h"

#define EPSILON 0.001

#define TEST_CATEGORY "/main/cdr/"

#define MOCK_CDR_BACKEND "mock_cdr_backend"

#define CHANNEL_TECH_NAME "CDRTestChannel"

/*! \brief A placeholder for Asterisk's 'real' CDR configuration */
static struct ast_cdr_config *saved_config;

/*! \brief A configuration suitable for 'normal' CDRs */
static struct ast_cdr_config debug_cdr_config = {
	.settings.flags = CDR_ENABLED | CDR_DEBUG,
};

/*! \brief A configuration suitable for CDRs with unanswered records */
static struct ast_cdr_config unanswered_cdr_config = {
	.settings.flags = CDR_ENABLED | CDR_UNANSWERED | CDR_DEBUG,
};

/*! \brief A configuration suitable for CDRs with congestion enabled */
static struct ast_cdr_config congestion_cdr_config = {
	.settings.flags = CDR_ENABLED | CDR_UNANSWERED | CDR_DEBUG | CDR_CONGESTION,
};

/*! \brief Macro to swap a configuration out from the CDR engine. This should be
 * used at the beginning of each test to set the needed configuration for that
 * test.
 */
#define SWAP_CONFIG(ao2_config, template) do { \
	*(ao2_config) = (template); \
	ast_cdr_set_config((ao2_config)); \
	} while (0)

/*! \brief A linked list of received CDR entries from the engine */
static AST_LIST_HEAD(, test_cdr_entry) actual_cdr_entries = AST_LIST_HEAD_INIT_VALUE;

/*! \brief The Mock CDR backend condition wait */
static ast_cond_t mock_cdr_cond;

/*! \brief A channel technology used for the unit tests */
static struct ast_channel_tech test_cdr_chan_tech = {
	.type = CHANNEL_TECH_NAME,
	.description = "Mock channel technology for CDR tests",
};

struct test_cdr_entry {
	struct ast_cdr *cdr;
	AST_LIST_ENTRY(test_cdr_entry) list;
};

/*! \brief The number of CDRs the mock backend has received */
static int global_mock_cdr_count;

/*! \internal
 * \brief Callback function for the mock CDR backend
 *
 * This function 'processes' a dispatched CDR record by adding it to the
 * \ref actual_cdr_entries list. When a test completes, it can verify the
 * expected records against this list of actual CDRs created by the engine.
 *
 * \param cdr The public CDR object created by the engine
 *
 * \retval -1 on error
 * \retval 0 on success
 */
static int mock_cdr_backend_cb(struct ast_cdr *cdr)
{
	struct ast_cdr *cdr_copy, *cdr_prev = NULL;
	struct ast_cdr *mock_cdr = NULL;
	struct test_cdr_entry *cdr_wrapper;

	cdr_wrapper = ast_calloc(1, sizeof(*cdr_wrapper));
	if (!cdr_wrapper) {
		return -1;
	}

	for (; cdr; cdr = cdr->next) {
		struct ast_var_t *var_entry, *var_copy;

		cdr_copy = ast_calloc(1, sizeof(*cdr_copy));
		if (!cdr_copy) {
			return -1;
		}
		*cdr_copy = *cdr;
		cdr_copy->varshead.first = NULL;
		cdr_copy->varshead.last = NULL;
		cdr_copy->next = NULL;

		AST_LIST_TRAVERSE(&cdr->varshead, var_entry, entries) {
			var_copy = ast_var_assign(var_entry->name, var_entry->value);
			if (!var_copy) {
				return -1;
			}
			AST_LIST_INSERT_TAIL(&cdr_copy->varshead, var_copy, entries);
		}

		if (!mock_cdr) {
			mock_cdr = cdr_copy;
		}
		if (cdr_prev) {
			cdr_prev->next = cdr_copy;
		}
		cdr_prev = cdr_copy;
	}
	cdr_wrapper->cdr = mock_cdr;

	AST_LIST_LOCK(&actual_cdr_entries);
	AST_LIST_INSERT_TAIL(&actual_cdr_entries, cdr_wrapper, list);
	global_mock_cdr_count++;
	ast_cond_signal(&mock_cdr_cond);
	AST_LIST_UNLOCK(&actual_cdr_entries);

	return 0;
}

/*! \internal
 * \brief Remove all entries from \ref actual_cdr_entries
 */
static void clear_mock_cdr_backend(void)
{
	struct test_cdr_entry *cdr_wrapper;

	AST_LIST_LOCK(&actual_cdr_entries);
	while ((cdr_wrapper = AST_LIST_REMOVE_HEAD(&actual_cdr_entries, list))) {
		ast_cdr_free(cdr_wrapper->cdr);
		ast_free(cdr_wrapper);
	}
	global_mock_cdr_count = 0;
	AST_LIST_UNLOCK(&actual_cdr_entries);
}

/*! \brief Verify a string field. This will set the test status result to fail;
 * as such, it assumes that (a) test is the test object variable, and (b) that
 * a return variable res exists.
 */
#define VERIFY_STRING_FIELD(field, actual, expected) do { \
	if (strcmp((actual)->field, (expected)->field)) { \
		ast_test_status_update(test, "Field %s failed: actual %s, expected %s\n", #field, (actual)->field, (expected)->field); \
		ast_test_set_result(test, AST_TEST_FAIL); \
		res = AST_TEST_FAIL; \
	} } while (0)

/*! \brief Verify a numeric field. This will set the test status result to fail;
 * as such, it assumes that (a) test is the test object variable, and (b) that
 * a return variable res exists.
 */
#define VERIFY_NUMERIC_FIELD(field, actual, expected) do { \
	if ((actual)->field != (expected)->field) { \
		ast_test_status_update(test, "Field %s failed: actual %ld, expected %ld\n", #field, (long)(actual)->field, (long)(expected)->field); \
		ast_test_set_result(test, AST_TEST_FAIL); \
		res = AST_TEST_FAIL; \
	} } while (0)

/*! \brief Verify a time field. This will set the test status result to fail;
 * as such, it assumes that (a) test is the test object variable, and (b) that
 * a return variable res exists.
 */
#define VERIFY_TIME_VALUE(field, actual) do { \
	if (ast_tvzero((actual)->field)) { \
		ast_test_status_update(test, "Field %s failed: should not be 0\n", #field); \
		ast_test_set_result(test, AST_TEST_FAIL); \
		res = AST_TEST_FAIL; \
	} } while (0)

/*! \brief Alice's Caller ID */
#define ALICE_CALLERID { .id.name.str = "Alice", .id.name.valid = 1, .id.number.str = "100", .id.number.valid = 1, }

/*! \brief Bob's Caller ID */
#define BOB_CALLERID { .id.name.str = "Bob", .id.name.valid = 1, .id.number.str = "200", .id.number.valid = 1, }

/*! \brief Charlie's Caller ID */
#define CHARLIE_CALLERID { .id.name.str = "Charlie", .id.name.valid = 1, .id.number.str = "300", .id.number.valid = 1, }

/*! \brief David's Caller ID */
#define DAVID_CALLERID { .id.name.str = "David", .id.name.valid = 1, .id.number.str = "400", .id.number.valid = 1, }

/*! \brief Copy the linkedid and uniqueid from a channel to an expected CDR */
#define COPY_IDS(channel_var, expected_record) do { \
	ast_copy_string((expected_record)->uniqueid, ast_channel_uniqueid((channel_var)), sizeof((expected_record)->uniqueid)); \
	ast_copy_string((expected_record)->linkedid, ast_channel_linkedid((channel_var)), sizeof((expected_record)->linkedid)); \
	} while (0)

/*! \brief Set ulaw format on channel */
#define SET_FORMATS(chan) do {\
	struct ast_format_cap *caps;\
	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);\
	ast_format_cap_append(caps, ast_format_ulaw, 0);\
	ast_channel_nativeformats_set((chan), caps);\
	ast_channel_set_writeformat((chan), ast_format_ulaw);\
	ast_channel_set_rawwriteformat((chan), ast_format_ulaw);\
	ast_channel_set_readformat((chan), ast_format_ulaw);\
	ast_channel_set_rawreadformat((chan), ast_format_ulaw);\
	ao2_ref(caps, -1);\
} while (0)

/*! \brief Create a \ref test_cdr_chan_tech for Alice, and set the expected
 * CDR records' linkedid and uniqueid. */
#define CREATE_ALICE_CHANNEL(channel_var, caller_id, expected_record) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, "100", "Alice", "100", "100", "default", NULL, NULL, 0, CHANNEL_TECH_NAME "/Alice"); \
	SET_FORMATS((channel_var));\
	ast_channel_set_caller((channel_var), (caller_id), NULL); \
	ast_copy_string((expected_record)->uniqueid, ast_channel_uniqueid((channel_var)), sizeof((expected_record)->uniqueid)); \
	ast_copy_string((expected_record)->linkedid, ast_channel_linkedid((channel_var)), sizeof((expected_record)->linkedid)); \
	ast_channel_unlock((channel_var)); \
	} while (0)

/*! \brief Create a \ref test_cdr_chan_tech for Bob, and set the expected
 * CDR records' linkedid and uniqueid. */
#define CREATE_BOB_CHANNEL(channel_var, caller_id, expected_record) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, "200", "Bob", "200", "200", "default", NULL, NULL, 0, CHANNEL_TECH_NAME "/Bob"); \
	SET_FORMATS((channel_var));\
	ast_channel_set_caller((channel_var), (caller_id), NULL); \
	ast_copy_string((expected_record)->uniqueid, ast_channel_uniqueid((channel_var)), sizeof((expected_record)->uniqueid)); \
	ast_copy_string((expected_record)->linkedid, ast_channel_linkedid((channel_var)), sizeof((expected_record)->linkedid)); \
	ast_channel_unlock((channel_var)); \
	} while (0)

/*! \brief Create a \ref test_cdr_chan_tech for Charlie, and set the expected
 * CDR records' linkedid and uniqueid. */
#define CREATE_CHARLIE_CHANNEL(channel_var, caller_id, expected_record) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, "300", "Charlie", "300", "300", "default", NULL, NULL, 0, CHANNEL_TECH_NAME "/Charlie"); \
	SET_FORMATS((channel_var));\
	ast_channel_set_caller((channel_var), (caller_id), NULL); \
	ast_copy_string((expected_record)->uniqueid, ast_channel_uniqueid((channel_var)), sizeof((expected_record)->uniqueid)); \
	ast_copy_string((expected_record)->linkedid, ast_channel_linkedid((channel_var)), sizeof((expected_record)->linkedid)); \
	ast_channel_unlock((channel_var)); \
	} while (0)

/*! \brief Create a \ref test_cdr_chan_tech for Charlie, and set the expected
 * CDR records' linkedid and uniqueid. */
#define CREATE_DAVID_CHANNEL(channel_var, caller_id, expected_record) do { \
	(channel_var) = ast_channel_alloc(0, AST_STATE_DOWN, "400", "David", "400", "400", "default", NULL, NULL, 0, CHANNEL_TECH_NAME "/David"); \
	SET_FORMATS((channel_var));\
	ast_channel_set_caller((channel_var), (caller_id), NULL); \
	ast_copy_string((expected_record)->uniqueid, ast_channel_uniqueid((channel_var)), sizeof((expected_record)->uniqueid)); \
	ast_copy_string((expected_record)->linkedid, ast_channel_linkedid((channel_var)), sizeof((expected_record)->linkedid)); \
	ast_channel_unlock((channel_var)); \
	} while (0)

/*! \brief Emulate a channel entering into an application */
#define EMULATE_APP_DATA(channel, priority, application, data) do { \
	if ((priority) > 0) { \
		ast_channel_priority_set((channel), (priority)); \
	} \
	ast_channel_lock((channel)); \
	ast_channel_appl_set((channel), (application)); \
	ast_channel_data_set((channel), (data)); \
	ast_channel_publish_snapshot((channel)); \
	ast_channel_unlock((channel)); \
	} while (0)

/*! \brief Hang up a test channel safely */
#define HANGUP_CHANNEL(channel, cause) \
	do { \
		ast_channel_hangupcause_set((channel), (cause)); \
		ast_hangup(channel); \
		channel = NULL; \
	} while (0)

static enum ast_test_result_state verify_mock_cdr_record(struct ast_test *test, struct ast_cdr *expected, int record)
{
	struct ast_cdr *actual = NULL;
	struct test_cdr_entry *cdr_wrapper;
	int count = 0;
	struct timeval wait_now = ast_tvnow();
	struct timespec wait_time = { .tv_sec = wait_now.tv_sec + 5, .tv_nsec = wait_now.tv_usec * 1000 };
	enum ast_test_result_state res = AST_TEST_PASS;

	while (count < record) {
		AST_LIST_LOCK(&actual_cdr_entries);
		if (global_mock_cdr_count < record) {
			ast_cond_timedwait(&mock_cdr_cond, &actual_cdr_entries.lock, &wait_time);
		}
		cdr_wrapper = AST_LIST_REMOVE_HEAD(&actual_cdr_entries, list);
		AST_LIST_UNLOCK(&actual_cdr_entries);

		if (!cdr_wrapper) {
			ast_test_status_update(test, "Unable to find actual CDR record at %d\n", count);
			return AST_TEST_FAIL;
		}
		actual = cdr_wrapper->cdr;

		if (!expected && actual) {
			ast_test_status_update(test, "CDRs recorded where no record expected\n");
			return AST_TEST_FAIL;
		}
		ast_test_debug(test, "Verifying expected record %s, %s\n",
			expected->channel, S_OR(expected->dstchannel, "<none>"));
		VERIFY_STRING_FIELD(accountcode, actual, expected);
		VERIFY_NUMERIC_FIELD(amaflags, actual, expected);
		VERIFY_STRING_FIELD(channel, actual, expected);
		VERIFY_STRING_FIELD(clid, actual, expected);
		VERIFY_STRING_FIELD(dcontext, actual, expected);
		VERIFY_NUMERIC_FIELD(disposition, actual, expected);
		VERIFY_STRING_FIELD(dst, actual, expected);
		VERIFY_STRING_FIELD(dstchannel, actual, expected);
		VERIFY_STRING_FIELD(lastapp, actual, expected);
		VERIFY_STRING_FIELD(lastdata, actual, expected);
		VERIFY_STRING_FIELD(linkedid, actual, expected);
		VERIFY_STRING_FIELD(peeraccount, actual, expected);
		VERIFY_STRING_FIELD(src, actual, expected);
		VERIFY_STRING_FIELD(uniqueid, actual, expected);
		VERIFY_STRING_FIELD(userfield, actual, expected);
		VERIFY_TIME_VALUE(start, actual);
		VERIFY_TIME_VALUE(end, actual);
		/* Note: there's no way we can really calculate a duration or
		 * billsec - the unit tests are too short. However, if billsec is
		 * non-zero in the expected, then make sure we have an answer time
		 */
		if (expected->billsec) {
			VERIFY_TIME_VALUE(answer, actual);
		}
		ast_test_debug(test, "Finished expected record %s, %s\n",
				expected->channel, S_OR(expected->dstchannel, "<none>"));
		expected = expected->next;
		++count;
	}
	return res;
}

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

static void do_sleep(struct timespec *to_sleep)
{
	while ((nanosleep(to_sleep, to_sleep) == -1) && (errno == EINTR)) {
	}
}

AST_TEST_DEFINE(test_cdr_channel_creation)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_NOANSWER,
		.accountcode = "100",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test that a CDR is created when a channel is created";
		info->description =
			"Test that a CDR is created when a channel is created";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan, (&caller), &expected);

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_unanswered_inbound_call)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Wait",
		.lastdata = "1",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_NOANSWER,
		.accountcode = "100",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test inbound unanswered calls";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"inbound to Asterisk, executes some dialplan, but\n"
			"is never answered.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan, &caller, &expected);

	EMULATE_APP_DATA(chan, 1, "Wait", "1");

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_unanswered_outbound_call)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = {
			.id.name.str = "",
			.id.name.valid = 1,
			.id.number.str = "",
			.id.number.valid = 1, };
	struct ast_cdr expected = {
		.clid = "\"\" <>",
		.dst = "s",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "AppDial",
		.lastdata = "(Outgoing Line)",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_NOANSWER,
		.accountcode = "100",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test outbound unanswered calls";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"outbound to Asterisk but is never answered.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan, &caller, &expected);

	ast_channel_exten_set(chan, "s");
	ast_channel_context_set(chan, "default");
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_ORIGINATED);
	EMULATE_APP_DATA(chan, 0, "AppDial", "(Outgoing Line)");
	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_outbound_bridged_call)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr alice_expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "",
		.lastdata = "",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
	};
	struct ast_cdr bob_expected = {
		.clid = "\"\" <>",
		.src = "",
		.dst = "s",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Bob",
		.dstchannel = "",
		.lastapp = "AppDial",
		.lastdata = "(Outgoing Line)",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "200",
		.peeraccount = "",
		.next = &alice_expected,
	};

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

	SWAP_CONFIG(config, debug_cdr_config);

	CREATE_ALICE_CHANNEL(chan_alice, &caller, &alice_expected);
	ast_channel_state_set(chan_alice, AST_STATE_UP);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);
	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	chan_bob = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_alice, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_bob);
	ast_channel_unlock(chan_bob);
	ast_copy_string(bob_expected.linkedid, ast_channel_linkedid(chan_bob), sizeof(bob_expected.linkedid));
	ast_copy_string(bob_expected.uniqueid, ast_channel_uniqueid(chan_bob), sizeof(bob_expected.uniqueid));
	ast_set_flag(ast_channel_flags(chan_bob), AST_FLAG_OUTGOING);
	ast_set_flag(ast_channel_flags(chan_bob), AST_FLAG_ORIGINATED);
	EMULATE_APP_DATA(chan_bob, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(NULL, chan_bob, "Bob", NULL);
	ast_channel_state_set(chan_bob, AST_STATE_RINGING);
	ast_channel_publish_dial(NULL, chan_bob, NULL, "ANSWER");

	ast_channel_state_set(chan_bob, AST_STATE_UP);

	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	do_sleep(&to_sleep);

	ast_bridge_depart(chan_bob);
	ast_bridge_depart(chan_alice);

	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &bob_expected, 2);
	return result;
}


AST_TEST_DEFINE(test_cdr_single_party)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = "",
		.lastapp = "VoiceMailMain",
		.lastdata = "1",
		.billsec = 1,
	.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test cdrs for a single party";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"answered, but only involves a single channel\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	SWAP_CONFIG(config, debug_cdr_config);
	CREATE_ALICE_CHANNEL(chan, &caller, &expected);

	ast_channel_lock(chan);
	EMULATE_APP_DATA(chan, 1, "Answer", "");
	ast_setstate(chan, AST_STATE_UP);
	EMULATE_APP_DATA(chan, 2, "VoiceMailMain", "1");
	ast_channel_unlock(chan);

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_single_bridge)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test cdrs for a single party entering/leaving a bridge";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"answered, enters a bridge, and leaves it.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	SWAP_CONFIG(config, debug_cdr_config);
	CREATE_ALICE_CHANNEL(chan, &caller, &expected);

	ast_channel_lock(chan);
	EMULATE_APP_DATA(chan, 1, "Answer", "");
	ast_setstate(chan, AST_STATE_UP);
	EMULATE_APP_DATA(chan, 2, "Bridge", "");
	ast_channel_unlock(chan);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	do_sleep(&to_sleep);

	ast_bridge_depart(chan);

	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_single_bridge_continue)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected_two = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Wait",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
	};
	struct ast_cdr expected_one = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.next = &expected_two,
	};

	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test cdrs for a single party entering/leaving a bridge";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"answered, enters a bridge, and leaves it.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	SWAP_CONFIG(config, debug_cdr_config);
	CREATE_ALICE_CHANNEL(chan, &caller, &expected_one);
	COPY_IDS(chan, &expected_two);

	ast_channel_lock(chan);
	EMULATE_APP_DATA(chan, 1, "Answer", "");
	ast_setstate(chan, AST_STATE_UP);
	EMULATE_APP_DATA(chan, 2, "Bridge", "");
	ast_channel_unlock(chan);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);
	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	do_sleep(&to_sleep);

	ast_bridge_depart(chan);

	EMULATE_APP_DATA(chan, 3, "Wait", "");

	/* And then it hangs up */
	HANGUP_CHANNEL(chan, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected_one, 2);

	return result;
}

AST_TEST_DEFINE(test_cdr_single_twoparty_bridge_a)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};

	struct ast_party_caller caller_alice = ALICE_CALLERID;
	struct ast_party_caller caller_bob = BOB_CALLERID;
	struct ast_cdr bob_expected = {
		.clid = "\"Bob\" <200>",
		.src = "200",
		.dst = "200",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "200",
	};
	struct ast_cdr alice_expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
		.next = &bob_expected,
	};

	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test cdrs for a single party entering/leaving a bridge";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"answered, enters a bridge, and leaves it. In this scenario, the\n"
			"Party A should answer the bridge first.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	SWAP_CONFIG(config, debug_cdr_config);
	CREATE_ALICE_CHANNEL(chan_alice, &caller_alice, &alice_expected);

	CREATE_BOB_CHANNEL(chan_bob, &caller_bob, &bob_expected);
	ast_copy_string(bob_expected.linkedid, ast_channel_linkedid(chan_alice), sizeof(bob_expected.linkedid));

	ast_channel_lock(chan_alice);
	EMULATE_APP_DATA(chan_alice, 1, "Answer", "");
	ast_setstate(chan_alice, AST_STATE_UP);
	EMULATE_APP_DATA(chan_alice, 2, "Bridge", "");
	ast_channel_unlock(chan_alice);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);

	ast_channel_lock(chan_bob);
	EMULATE_APP_DATA(chan_bob, 1, "Answer", "");
	ast_setstate(chan_bob, AST_STATE_UP);
	EMULATE_APP_DATA(chan_bob, 2, "Bridge", "");
	ast_channel_unlock(chan_bob);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);

	ast_bridge_depart(chan_alice);
	ast_bridge_depart(chan_bob);

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &alice_expected, 2);

	return result;
}

AST_TEST_DEFINE(test_cdr_single_twoparty_bridge_b)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};

	struct ast_party_caller caller_alice = ALICE_CALLERID;
	struct ast_party_caller caller_bob = BOB_CALLERID;
	struct ast_cdr bob_expected = {
		.clid = "\"Bob\" <200>",
		.src = "200",
		.dst = "200",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "200",
	};
	struct ast_cdr alice_expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
		.next = &bob_expected,
	};

	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test cdrs for a single party entering/leaving a bridge";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"answered, enters a bridge, and leaves it. In this scenario, the\n"
			"Party B should answer the bridge first.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	SWAP_CONFIG(config, debug_cdr_config);
	CREATE_ALICE_CHANNEL(chan_alice, &caller_alice, &alice_expected);

	CREATE_BOB_CHANNEL(chan_bob, &caller_bob, &bob_expected);
	ast_copy_string(bob_expected.linkedid, ast_channel_linkedid(chan_alice), sizeof(bob_expected.linkedid));

	ast_channel_unlock(chan_alice);
	EMULATE_APP_DATA(chan_alice, 1, "Answer", "");
	ast_setstate(chan_alice, AST_STATE_UP);
	EMULATE_APP_DATA(chan_alice, 2, "Bridge", "");
	ast_channel_unlock(chan_alice);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	ast_channel_lock(chan_bob);
	EMULATE_APP_DATA(chan_bob, 1, "Answer", "");
	ast_setstate(chan_bob, AST_STATE_UP);
	EMULATE_APP_DATA(chan_bob, 2, "Bridge", "");
	ast_channel_unlock(chan_bob);
	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);

	ast_bridge_depart(chan_alice);
	ast_bridge_depart(chan_bob);

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &alice_expected, 2);

	return result;
}

AST_TEST_DEFINE(test_cdr_single_multiparty_bridge)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};

	struct ast_party_caller caller_alice = ALICE_CALLERID;
	struct ast_party_caller caller_bob = BOB_CALLERID;
	struct ast_party_caller caller_charlie = CHARLIE_CALLERID;
	struct ast_cdr charlie_expected = {
		.clid = "\"Charlie\" <300>",
		.src = "300",
		.dst = "300",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Charlie",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "300",
	};
	struct ast_cdr bob_expected = {
		.clid = "\"Bob\" <200>",
		.src = "200",
		.dst = "200",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Bob",
		.dstchannel = CHANNEL_TECH_NAME "/Charlie",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "200",
		.peeraccount = "300",
		.next = &charlie_expected,
	};
	struct ast_cdr alice_expected_two = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Charlie",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "300",
		.next = &bob_expected,
	};
	struct ast_cdr alice_expected_one = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Bridge",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
		.next = &alice_expected_two,
	};

	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test cdrs for a single party entering/leaving a multi-party bridge";
		info->description =
			"Test the properties of a CDR for a call that is\n"
			"answered, enters a bridge, and leaves it. A total of three\n"
			"parties perform this action.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	SWAP_CONFIG(config, debug_cdr_config);
	CREATE_ALICE_CHANNEL(chan_alice, &caller_alice, &alice_expected_one);
	COPY_IDS(chan_alice, &alice_expected_two);
	CREATE_BOB_CHANNEL(chan_bob, &caller_bob, &bob_expected);
	ast_copy_string(bob_expected.linkedid, ast_channel_linkedid(chan_alice), sizeof(bob_expected.linkedid));
	CREATE_CHARLIE_CHANNEL(chan_charlie, &caller_charlie, &charlie_expected);
	ast_copy_string(charlie_expected.linkedid, ast_channel_linkedid(chan_alice), sizeof(charlie_expected.linkedid));

	ast_channel_lock(chan_alice);
	EMULATE_APP_DATA(chan_alice, 1, "Answer", "");
	ast_setstate(chan_alice, AST_STATE_UP);
	EMULATE_APP_DATA(chan_alice, 2, "Bridge", "");
	ast_channel_unlock(chan_alice);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);
	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	ast_channel_lock(chan_bob);
	EMULATE_APP_DATA(chan_bob, 1, "Answer", "");
	ast_setstate(chan_bob, AST_STATE_UP);
	EMULATE_APP_DATA(chan_bob, 2, "Bridge", "");
	ast_channel_unlock(chan_bob);
	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	do_sleep(&to_sleep);

	ast_channel_lock(chan_charlie);
	EMULATE_APP_DATA(chan_charlie, 1, "Answer", "");
	ast_setstate(chan_charlie, AST_STATE_UP);
	EMULATE_APP_DATA(chan_charlie, 2, "Bridge", "");
	ast_channel_unlock(chan_charlie);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_charlie, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	do_sleep(&to_sleep);

	ast_bridge_depart(chan_alice);
	ast_bridge_depart(chan_bob);
	ast_bridge_depart(chan_charlie);

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &alice_expected_one, 4);

	return result;
}

AST_TEST_DEFINE(test_cdr_dial_unanswered)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_NOANSWER,
		.accountcode = "100",
		.peeraccount = "200",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CDRs for a dial that isn't answered";
		info->description =
			"Test the properties of a CDR for a channel that\n"
			"performs a dial operation that isn't answered\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &expected);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", "CDRTestChannel/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_callee, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "NOANSWER");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NO_ANSWER);
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NO_ANSWER);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}


AST_TEST_DEFINE(test_cdr_dial_busy)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_BUSY,
		.accountcode = "100",
		.peeraccount = "200",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CDRs for a dial that results in a busy";
		info->description =
			"Test the properties of a CDR for a channel that\n"
			"performs a dial operation to an endpoint that's busy\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &expected);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_callee, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "BUSY");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_BUSY);
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_BUSY);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_dial_congestion)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_CONGESTION,
		.accountcode = "100",
		.peeraccount = "200",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CDRs for a dial that results in congestion";
		info->description =
			"Test the properties of a CDR for a channel that\n"
			"performs a dial operation to an endpoint that's congested\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, congestion_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &expected);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_callee, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "CONGESTION");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_CONGESTION);
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_CONGESTION);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_dial_unavailable)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_FAILED,
		.accountcode = "100",
		.peeraccount = "200",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CDRs for a dial that results in unavailable";
		info->description =
			"Test the properties of a CDR for a channel that\n"
			"performs a dial operation to an endpoint that's unavailable\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &expected);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_callee, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "CHANUNAVAIL");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NO_ROUTE_DESTINATION);
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NO_ROUTE_DESTINATION);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_dial_caller_cancel)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_NOANSWER,
		.accountcode = "100",
		.peeraccount = "200",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test CDRs for a dial where the caller cancels";
		info->description =
			"Test the properties of a CDR for a channel that\n"
			"performs a dial operation to an endpoint but then decides\n"
			"to hang up, cancelling the dial\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &expected);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_callee, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "CANCEL");

	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_dial_parallel_failed)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_david, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr bob_expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob&" CHANNEL_TECH_NAME "/Charlie&" CHANNEL_TECH_NAME "/David",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_NOANSWER,
		.accountcode = "100",
		.peeraccount = "200",
	};
	struct ast_cdr charlie_expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Charlie",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob&" CHANNEL_TECH_NAME "/Charlie&" CHANNEL_TECH_NAME "/David",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_BUSY,
		.accountcode = "100",
		.peeraccount = "300",
	};
	struct ast_cdr david_expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/David",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob&" CHANNEL_TECH_NAME "/Charlie&" CHANNEL_TECH_NAME "/David",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_CONGESTION,
		.accountcode = "100",
		.peeraccount = "400",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	struct ast_cdr *expected = &bob_expected;
	bob_expected.next = &charlie_expected;
	charlie_expected.next = &david_expected;

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

	SWAP_CONFIG(config, congestion_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &bob_expected);
	COPY_IDS(chan_caller, &charlie_expected);
	COPY_IDS(chan_caller, &david_expected);

	/* Channel enters Dial app */
	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob&" CHANNEL_TECH_NAME "/Charlie&" CHANNEL_TECH_NAME "/David");

	/* Outbound channels are created */
	chan_bob = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_bob);
	ast_channel_unlock(chan_bob);
	ast_set_flag(ast_channel_flags(chan_bob), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_bob, 0, "AppDial", "(Outgoing Line)");

	chan_charlie = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "300", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Charlie");
	SET_FORMATS(chan_charlie);
	ast_channel_unlock(chan_charlie);
	ast_set_flag(ast_channel_flags(chan_charlie), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_charlie, 0, "AppDial", "(Outgoing Line)");

	chan_david = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "400", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/David");
	SET_FORMATS(chan_charlie);
	ast_channel_unlock(chan_david);
	ast_set_flag(ast_channel_flags(chan_david), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_david, 0, "AppDial", "(Outgoing Line)");

	/* Dial starts */
	ast_channel_publish_dial(chan_caller, chan_bob, "Bob", NULL);
	ast_channel_publish_dial(chan_caller, chan_charlie, "Charlie", NULL);
	ast_channel_publish_dial(chan_caller, chan_david, "David", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);

	/* Charlie is busy */
	ast_channel_publish_dial(chan_caller, chan_charlie, NULL, "BUSY");
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_BUSY);

	/* David is congested */
	ast_channel_publish_dial(chan_caller, chan_david, NULL, "CONGESTION");
	HANGUP_CHANNEL(chan_david, AST_CAUSE_CONGESTION);

	/* Bob is canceled */
	ast_channel_publish_dial(chan_caller, chan_bob, NULL, "CANCEL");
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL);

	/* Alice hangs up */
	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, expected, 3);

	return result;
}

AST_TEST_DEFINE(test_cdr_dial_answer_no_bridge)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr bob_expected_one = {
		.clid = "\"\" <>",
		.src = "",
		.dst = "s",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Wait",
		.lastdata = "1",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "200",
	};
	struct ast_cdr alice_expected_two = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Wait",
		.lastdata = "1",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.next = &bob_expected_one,
	};
	struct ast_cdr alice_expected_one = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
		.next = &alice_expected_two,
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test dialing, answering, and not going into a bridge.";
		info->description =
			"This is a weird one, but theoretically possible. You can perform\n"
			"a dial, then bounce both channels to different priorities and\n"
			"never have them enter a bridge together. Ew. This makes sure that\n"
			"when we answer, we get a CDR, it gets ended at that point, and\n"
			"that it gets finalized appropriately. We should get three CDRs in\n"
			"the end - one for the dial, and one for each CDR as they continued\n"
			"on.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, debug_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &alice_expected_one);
	COPY_IDS(chan_caller, &alice_expected_two);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	COPY_IDS(chan_callee, &bob_expected_one);

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "ANSWER");

	ast_channel_state_set(chan_caller, AST_STATE_UP);
	ast_clear_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	ast_channel_state_set(chan_callee, AST_STATE_UP);

	EMULATE_APP_DATA(chan_caller, 2, "Wait", "1");
	EMULATE_APP_DATA(chan_callee, 1, "Wait", "1");

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &alice_expected_one, 3);
	return result;
}

AST_TEST_DEFINE(test_cdr_dial_answer_twoparty_bridge_a)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
	};

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

	SWAP_CONFIG(config, debug_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &expected);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_callee, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "ANSWER");

	ast_channel_state_set(chan_caller, AST_STATE_UP);
	ast_channel_state_set(chan_callee, AST_STATE_UP);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);
	do_sleep(&to_sleep);

	ast_test_validate(test, !ast_bridge_impart(bridge, chan_caller, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_callee, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));

	do_sleep(&to_sleep);

	ast_bridge_depart(chan_caller);
	ast_bridge_depart(chan_callee);

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);
	return result;
}

AST_TEST_DEFINE(test_cdr_dial_answer_twoparty_bridge_b)
{
	RAII_VAR(struct ast_channel *, chan_caller, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_callee, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
	};

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

	SWAP_CONFIG(config, debug_cdr_config);

	CREATE_ALICE_CHANNEL(chan_caller, &caller, &expected);

	EMULATE_APP_DATA(chan_caller, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_callee = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, "200", NULL, NULL, NULL, chan_caller, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_callee);
	ast_channel_unlock(chan_callee);
	ast_set_flag(ast_channel_flags(chan_callee), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_callee, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_caller, chan_callee, "Bob", NULL);
	ast_channel_state_set(chan_caller, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_caller, chan_callee, NULL, "ANSWER");

	ast_channel_state_set(chan_caller, AST_STATE_UP);
	ast_channel_state_set(chan_callee, AST_STATE_UP);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);
	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_callee, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_caller, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_bridge_depart(chan_caller);
	ast_bridge_depart(chan_callee);

	HANGUP_CHANNEL(chan_caller, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_callee, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &expected, 1);
	return result;
}

AST_TEST_DEFINE(test_cdr_dial_answer_multiparty)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_charlie, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_david, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_party_caller charlie_caller = CHARLIE_CALLERID;
	struct ast_cdr charlie_expected_two = {
		.clid = "\"Charlie\" <300>",
		.src = "300",
		.dst = "300",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Charlie",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/David",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "300",
		.peeraccount = "200",
	};
	struct ast_cdr charlie_expected_one = {
		.clid = "\"Charlie\" <300>",
		.src = "300",
		.dst = "300",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Charlie",
		.dstchannel = CHANNEL_TECH_NAME "/David",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/David",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "300",
		.peeraccount = "400",
		.next = &charlie_expected_two,
	};
	struct ast_cdr bob_expected_one = {
		.clid = "\"Bob\" <200>",
		.src = "200",
		.dst = "200",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Bob",
		.dstchannel = CHANNEL_TECH_NAME "/David",
		.lastapp = "AppDial",
		.lastdata = "(Outgoing Line)",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "200",
		.peeraccount = "400",
		.next = &charlie_expected_one,
	};
	struct ast_cdr alice_expected_three = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/David",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "400",
		.next = &bob_expected_one,
	};
	struct ast_cdr alice_expected_two = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Charlie",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "300",
		.next = &alice_expected_three,
	};
	struct ast_cdr alice_expected_one = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.dstchannel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Dial",
		.lastdata = CHANNEL_TECH_NAME "/Bob",
		.amaflags = AST_AMA_DOCUMENTATION,
		.billsec = 1,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.peeraccount = "200",
		.next = &alice_expected_two,
	};

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

	SWAP_CONFIG(config, debug_cdr_config);

	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller, &alice_expected_one);
	COPY_IDS(chan_alice, &alice_expected_two);
	COPY_IDS(chan_alice, &alice_expected_three);

	EMULATE_APP_DATA(chan_alice, 1, "Dial", CHANNEL_TECH_NAME "/Bob");

	chan_bob = ast_channel_alloc(0, AST_STATE_DOWN, "200", "Bob", "200", "200", "default", NULL, NULL, 0, CHANNEL_TECH_NAME "/Bob");
	SET_FORMATS(chan_bob);
	ast_channel_unlock(chan_bob);
	ast_set_flag(ast_channel_flags(chan_bob), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_bob, 0, "AppDial", "(Outgoing Line)");
	ast_copy_string(bob_expected_one.uniqueid, ast_channel_uniqueid(chan_bob), sizeof(bob_expected_one.uniqueid));
	ast_copy_string(bob_expected_one.linkedid, ast_channel_linkedid(chan_alice), sizeof(bob_expected_one.linkedid));

	CREATE_CHARLIE_CHANNEL(chan_charlie, &charlie_caller, &charlie_expected_one);
	EMULATE_APP_DATA(chan_charlie, 1, "Dial", CHANNEL_TECH_NAME "/David");
	ast_copy_string(charlie_expected_one.uniqueid, ast_channel_uniqueid(chan_charlie), sizeof(charlie_expected_one.uniqueid));
	ast_copy_string(charlie_expected_one.linkedid, ast_channel_linkedid(chan_alice), sizeof(charlie_expected_one.linkedid));
	ast_copy_string(charlie_expected_two.uniqueid, ast_channel_uniqueid(chan_charlie), sizeof(charlie_expected_two.uniqueid));
	ast_copy_string(charlie_expected_two.linkedid, ast_channel_linkedid(chan_alice), sizeof(charlie_expected_two.linkedid));

	chan_david = ast_channel_alloc(0, AST_STATE_DOWN, "400", "David", "400", "400", "default", NULL, NULL, 0, CHANNEL_TECH_NAME "/David");
	SET_FORMATS(chan_david);
	ast_channel_unlock(chan_david);
	ast_set_flag(ast_channel_flags(chan_david), AST_FLAG_OUTGOING);
	EMULATE_APP_DATA(chan_david, 0, "AppDial", "(Outgoing Line)");

	ast_channel_publish_dial(chan_alice, chan_bob, "Bob", NULL);
	ast_channel_state_set(chan_alice, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_charlie, chan_david, "David", NULL);
	ast_channel_state_set(chan_charlie, AST_STATE_RINGING);
	ast_channel_publish_dial(chan_alice, chan_bob, NULL, "ANSWER");
	ast_channel_publish_dial(chan_charlie, chan_david, NULL, "ANSWER");

	ast_channel_state_set(chan_alice, AST_STATE_UP);
	ast_channel_state_set(chan_bob, AST_STATE_UP);
	ast_channel_state_set(chan_charlie, AST_STATE_UP);
	ast_channel_state_set(chan_david, AST_STATE_UP);

	bridge = ast_bridge_basic_new();
	ast_test_validate(test, bridge != NULL);

	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_charlie, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_david, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_depart(chan_alice));
	ast_test_validate(test, !ast_bridge_depart(chan_bob));
	ast_test_validate(test, !ast_bridge_depart(chan_charlie));
	ast_test_validate(test, !ast_bridge_depart(chan_david));

	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_charlie, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_david, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &alice_expected_one, 6);

	return result;
}

AST_TEST_DEFINE(test_cdr_park)
{
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_channel *, chan_bob, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, bridge, NULL, safe_bridge_destroy);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct timespec to_sleep = {1, 0};

	struct ast_party_caller bob_caller = BOB_CALLERID;
	struct ast_party_caller alice_caller = ALICE_CALLERID;
	struct ast_cdr bob_expected = {
		.clid = "\"Bob\" <200>",
		.src = "200",
		.dst = "200",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Bob",
		.lastapp = "Park",
		.lastdata = "701",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "200",
	};
	struct ast_cdr alice_expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Park",
		.lastdata = "700",
		.billsec = 1,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
		.next = &bob_expected,
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test cdrs for a single party entering Park";
		info->description =
			"Test the properties of a CDR for calls that are\n"
			"answered, enters Park, and leaves it.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	SWAP_CONFIG(config, debug_cdr_config);
	CREATE_ALICE_CHANNEL(chan_alice, &alice_caller, &alice_expected);
	CREATE_BOB_CHANNEL(chan_bob, &bob_caller, &bob_expected);

	ast_channel_lock(chan_alice);
	EMULATE_APP_DATA(chan_alice, 1, "Park", "700");
	ast_setstate(chan_alice, AST_STATE_UP);
	ast_channel_unlock(chan_alice);

	ast_channel_lock(chan_bob);
	EMULATE_APP_DATA(chan_bob, 1, "Park", "701");
	ast_setstate(chan_bob, AST_STATE_UP);
	ast_channel_unlock(chan_bob);

	bridge = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_HOLDING,
		AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM
			| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_PROHIBITED,
		"test_cdr", "test_cdr_park", NULL);
	ast_test_validate(test, bridge != NULL);

	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_alice, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_test_validate(test, !ast_bridge_impart(bridge, chan_bob, NULL, NULL, AST_BRIDGE_IMPART_CHAN_DEPARTABLE));
	do_sleep(&to_sleep);
	ast_bridge_depart(chan_alice);
	ast_bridge_depart(chan_bob);

	/* And then it hangs up */
	HANGUP_CHANNEL(chan_alice, AST_CAUSE_NORMAL);
	HANGUP_CHANNEL(chan_bob, AST_CAUSE_NORMAL);

	result = verify_mock_cdr_record(test, &alice_expected, 2);

	return result;
}


AST_TEST_DEFINE(test_cdr_fields)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	char varbuffer[128];
	int int_buffer;
	double db_buffer;
	struct timespec to_sleep = {2, 0};
	struct ast_flags fork_options = { 0, };

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr original = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Wait",
		.lastdata = "10",
		.billsec = 0,
		.amaflags = AST_AMA_OMIT,
		.disposition = AST_CDR_FAILED,
		.accountcode = "XXX",
		.userfield = "yackity",
	};
	struct ast_cdr fork_expected_one = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Wait",
		.lastdata = "10",
		.billsec = 0,
		.amaflags = AST_AMA_OMIT,
		.disposition = AST_CDR_FAILED,
		.accountcode = "XXX",
		.userfield = "yackity",
	};
	struct ast_cdr fork_expected_two = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.lastapp = "Answer",
		.billsec = 0,
		.amaflags = AST_AMA_OMIT,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "ZZZ",
		.userfield = "schmackity",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	struct ast_cdr *expected = &original;
	original.next = &fork_expected_one;
	fork_expected_one.next = &fork_expected_two;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test field access CDRs";
		info->description =
			"This tests setting/retrieving data on CDR records.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan, &caller, &original);
	ast_copy_string(fork_expected_one.uniqueid, ast_channel_uniqueid(chan), sizeof(fork_expected_one.uniqueid));
	ast_copy_string(fork_expected_one.linkedid, ast_channel_linkedid(chan), sizeof(fork_expected_one.linkedid));
	ast_copy_string(fork_expected_two.uniqueid, ast_channel_uniqueid(chan), sizeof(fork_expected_two.uniqueid));
	ast_copy_string(fork_expected_two.linkedid, ast_channel_linkedid(chan), sizeof(fork_expected_two.linkedid));

	/* Channel enters Wait app */
	ast_channel_lock(chan);
	ast_channel_appl_set(chan, "Wait");
	ast_channel_data_set(chan, "10");
	ast_channel_priority_set(chan, 1);
	ast_channel_publish_snapshot(chan);

	/* Set properties on the channel that propagate to the CDR */
	ast_channel_amaflags_set(chan, AST_AMA_OMIT);
	ast_channel_accountcode_set(chan, "XXX");
	ast_channel_unlock(chan);

	/* Wait one second so we get a duration. */
	do_sleep(&to_sleep);

	ast_cdr_setuserfield(ast_channel_name(chan), "foobar");
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "test_variable", "record_1") == 0);

	/* Verify that we can't set read-only fields or other fields directly */
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "clid", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "src", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "dst", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "dcontext", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "channel", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "dstchannel", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "lastapp", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "lastdata", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "start", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "answer", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "end", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "duration", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "billsec", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "disposition", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "amaflags", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "accountcode", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "uniqueid", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "linkedid", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "userfield", "junk") != 0);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "sequence", "junk") != 0);

	/* Verify the values */
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "userfield", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "foobar") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "test_variable", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "record_1") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "amaflags", varbuffer, sizeof(varbuffer)) == 0);
	sscanf(varbuffer, "%d", &int_buffer);
	ast_test_validate(test, int_buffer == AST_AMA_OMIT);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "accountcode", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "XXX") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "clid", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "\"Alice\" <100>") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "src", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "100") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "dst", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "100") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "dcontext", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "default") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "channel", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, CHANNEL_TECH_NAME "/Alice") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "dstchannel", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "lastapp", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "Wait") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "lastdata", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "10") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "start", varbuffer, sizeof(varbuffer)) == 0);
	sscanf(varbuffer, "%lf", &db_buffer);
	ast_test_validate(test, fabs(db_buffer) > 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "answer", varbuffer, sizeof(varbuffer)) == 0);
	sscanf(varbuffer, "%lf", &db_buffer);
	ast_test_validate(test, fabs(db_buffer) < EPSILON);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "end", varbuffer, sizeof(varbuffer)) == 0);
	sscanf(varbuffer, "%lf", &db_buffer);
	ast_test_validate(test, fabs(db_buffer) < EPSILON);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "duration", varbuffer, sizeof(varbuffer)) == 0);
	sscanf(varbuffer, "%lf", &db_buffer);
	ast_test_validate(test, fabs(db_buffer) > 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "billsec", varbuffer, sizeof(varbuffer)) == 0);
	sscanf(varbuffer, "%lf", &db_buffer);
	ast_test_validate(test, fabs(db_buffer) < EPSILON);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "disposition", varbuffer, sizeof(varbuffer)) == 0);
	sscanf(varbuffer, "%d", &int_buffer);
	ast_test_validate(test, int_buffer == AST_CDR_NULL);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "uniqueid", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, ast_channel_uniqueid(chan)) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "linkedid", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, ast_channel_linkedid(chan)) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "sequence", varbuffer, sizeof(varbuffer)) == 0);

	/* Fork the CDR, and check that we change the properties on both CDRs. */
	ast_set_flag(&fork_options, AST_CDR_FLAG_KEEP_VARS);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);

	/* Change some properties */
	ast_cdr_setuserfield(ast_channel_name(chan), "yackity");
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "test_variable", "record_1b") == 0);

	/* Fork the CDR again, finalizing all current CDRs */
	ast_set_flag(&fork_options, AST_CDR_FLAG_KEEP_VARS | AST_CDR_FLAG_FINALIZE);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);

	/* Channel enters Answer app */
	ast_channel_lock(chan);
	ast_channel_appl_set(chan, "Answer");
	ast_channel_data_set(chan, "");
	ast_channel_priority_set(chan, 1);
	ast_channel_publish_snapshot(chan);
	ast_setstate(chan, AST_STATE_UP);

	/* Set properties on the last record */
	ast_channel_accountcode_set(chan, "ZZZ");
	ast_channel_unlock(chan);
	ast_cdr_setuserfield(ast_channel_name(chan), "schmackity");
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "test_variable", "record_2") == 0);

	/* Hang up and verify */
	ast_channel_hangupcause_set(chan, AST_CAUSE_NORMAL);
	ast_hangup(chan);
	chan = NULL;
	result = verify_mock_cdr_record(test, expected, 3);

	return result;
}

AST_TEST_DEFINE(test_cdr_no_reset_cdr)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	struct ast_flags fork_options = { 0, };
	struct timespec to_sleep = {1, 0};

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr expected = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.billsec = 0,
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_FAILED,
		.accountcode = "100",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test field access CDRs";
		info->description =
			"This tests setting/retrieving data on CDR records.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, unanswered_cdr_config);

	CREATE_ALICE_CHANNEL(chan, &caller, &expected);

	do_sleep(&to_sleep);

	/* Disable the CDR */
	ast_test_validate(test, ast_cdr_set_property(ast_channel_name(chan), AST_CDR_FLAG_DISABLE) == 0);

	/* Fork the CDR. This should be enabled */
	ast_set_flag(&fork_options, AST_CDR_FLAG_FINALIZE);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);

	/* Disable and enable the forked CDR */
	ast_test_validate(test, ast_cdr_set_property(ast_channel_name(chan), AST_CDR_FLAG_DISABLE) == 0);
	ast_test_validate(test, ast_cdr_clear_property(ast_channel_name(chan), AST_CDR_FLAG_DISABLE) == 0);

	/* Fork and finalize again. This CDR should be propagated */
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);

	/* Disable all future CDRs */
	ast_test_validate(test, ast_cdr_set_property(ast_channel_name(chan), AST_CDR_FLAG_DISABLE_ALL) == 0);

	/* Fork a few more */
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);

	ast_channel_hangupcause_set(chan, AST_CAUSE_NORMAL);
	ast_hangup(chan);
	chan = NULL;
	result = verify_mock_cdr_record(test, &expected, 1);

	return result;
}

AST_TEST_DEFINE(test_cdr_fork_cdr)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_channel_release);
	RAII_VAR(struct ast_cdr_config *, config, ao2_alloc(sizeof(*config), NULL),
			ao2_cleanup);
	char varbuffer[128];
	char fork_varbuffer[128];
	char answer_time[128];
	char fork_answer_time[128];
	char start_time[128];
	char fork_start_time[128];
	struct ast_flags fork_options = { 0, };
	struct timespec to_sleep = {1, 10000};

	struct ast_party_caller caller = ALICE_CALLERID;
	struct ast_cdr original = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
	};
	struct ast_cdr fork_expected_one = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
	};
	struct ast_cdr fork_expected_two = {
		.clid = "\"Alice\" <100>",
		.src = "100",
		.dst = "100",
		.dcontext = "default",
		.channel = CHANNEL_TECH_NAME "/Alice",
		.amaflags = AST_AMA_DOCUMENTATION,
		.disposition = AST_CDR_ANSWERED,
		.accountcode = "100",
	};
	enum ast_test_result_state result = AST_TEST_NOT_RUN;
	struct ast_cdr *expected = &original;
	original.next = &fork_expected_one;
	fork_expected_one.next = &fork_expected_two;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test field access CDRs";
		info->description =
			"This tests setting/retrieving data on CDR records.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	SWAP_CONFIG(config, debug_cdr_config);

	CREATE_ALICE_CHANNEL(chan, &caller, &original);
	ast_copy_string(fork_expected_one.uniqueid, ast_channel_uniqueid(chan), sizeof(fork_expected_one.uniqueid));
	ast_copy_string(fork_expected_one.linkedid, ast_channel_linkedid(chan), sizeof(fork_expected_one.linkedid));
	ast_copy_string(fork_expected_two.uniqueid, ast_channel_uniqueid(chan), sizeof(fork_expected_two.uniqueid));
	ast_copy_string(fork_expected_two.linkedid, ast_channel_linkedid(chan), sizeof(fork_expected_two.linkedid));

	do_sleep(&to_sleep);

	/* Test blowing away variables */
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "test_variable", "record_1") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "test_variable", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "record_1") == 0);
	ast_copy_string(varbuffer, "", sizeof(varbuffer));

	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "test_variable", fork_varbuffer, sizeof(fork_varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "record_1") != 0);

	/* Test finalizing previous CDRs */
	ast_set_flag(&fork_options, AST_CDR_FLAG_FINALIZE);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);

	/* Test keep variables; setting a new answer time */
	ast_channel_lock(chan);
	ast_setstate(chan, AST_STATE_UP);
	ast_channel_unlock(chan);
	do_sleep(&to_sleep);
	ast_test_validate(test, ast_cdr_setvar(ast_channel_name(chan), "test_variable", "record_2") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "test_variable", varbuffer, sizeof(varbuffer)) == 0);
	ast_test_validate(test, strcmp(varbuffer, "record_2") == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "answer", answer_time, sizeof(answer_time)) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "start", start_time, sizeof(start_time)) == 0);

	ast_set_flag(&fork_options, AST_CDR_FLAG_FINALIZE);
	ast_set_flag(&fork_options, AST_CDR_FLAG_KEEP_VARS);
	ast_set_flag(&fork_options, AST_CDR_FLAG_SET_ANSWER);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "answer", fork_answer_time, sizeof(fork_answer_time)) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "start", fork_start_time, sizeof(fork_start_time)) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "test_variable", fork_varbuffer, sizeof(fork_varbuffer)) == 0);
	ast_test_validate(test, strcmp(fork_varbuffer, varbuffer) == 0);
	ast_test_validate(test, strcmp(fork_start_time, start_time) == 0);
	ast_test_validate(test, strcmp(fork_answer_time, answer_time) != 0);

	ast_clear_flag(&fork_options, AST_CDR_FLAG_SET_ANSWER);
	ast_set_flag(&fork_options, AST_CDR_FLAG_RESET);
	ast_test_validate(test, ast_cdr_fork(ast_channel_name(chan), &fork_options) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "answer", fork_answer_time, sizeof(fork_answer_time)) == 0);
	ast_test_validate(test, ast_cdr_getvar(ast_channel_name(chan), "start", fork_start_time, sizeof(fork_start_time)) == 0);
	ast_test_validate(test, strcmp(fork_start_time, start_time) != 0);
	ast_test_validate(test, strcmp(fork_answer_time, answer_time) != 0);

	ast_channel_hangupcause_set(chan, AST_CAUSE_NORMAL);
	ast_hangup(chan);
	chan = NULL;
	result = verify_mock_cdr_record(test, expected, 3);

	return result;
}

/*!
 * \internal
 * \brief Callback function called before each test executes
 */
static int test_cdr_init_cb(struct ast_test_info *info, struct ast_test *test)
{
	/* Back up the real config */
	saved_config = ast_cdr_get_config();
	clear_mock_cdr_backend();
	return 0;
}

/*!
 * \internal
 * \brief Callback function called after each test executes
 */
static int test_cdr_cleanup_cb(struct ast_test_info *info, struct ast_test *test)
{
	/* Restore the real config */
	ast_cdr_set_config(saved_config);
	ao2_cleanup(saved_config);
	saved_config = NULL;
	clear_mock_cdr_backend();

	return 0;
}


static void unload_module(void)
{
	ast_cdr_unregister(MOCK_CDR_BACKEND);
	clear_mock_cdr_backend();
}

static int load_module(void)
{
	ast_cond_init(&mock_cdr_cond, NULL);

	AST_TEST_REGISTER(test_cdr_channel_creation);
	AST_TEST_REGISTER(test_cdr_unanswered_inbound_call);
	AST_TEST_REGISTER(test_cdr_unanswered_outbound_call);

	AST_TEST_REGISTER(test_cdr_single_party);
	AST_TEST_REGISTER(test_cdr_single_bridge);
	AST_TEST_REGISTER(test_cdr_single_bridge_continue);
	AST_TEST_REGISTER(test_cdr_single_twoparty_bridge_a);
	AST_TEST_REGISTER(test_cdr_single_twoparty_bridge_b);
	AST_TEST_REGISTER(test_cdr_single_multiparty_bridge);

	AST_TEST_REGISTER(test_cdr_outbound_bridged_call);

	AST_TEST_REGISTER(test_cdr_dial_unanswered);
	AST_TEST_REGISTER(test_cdr_dial_congestion);
	AST_TEST_REGISTER(test_cdr_dial_busy);
	AST_TEST_REGISTER(test_cdr_dial_unavailable);
	AST_TEST_REGISTER(test_cdr_dial_caller_cancel);
	AST_TEST_REGISTER(test_cdr_dial_parallel_failed);
	AST_TEST_REGISTER(test_cdr_dial_answer_no_bridge);
	AST_TEST_REGISTER(test_cdr_dial_answer_twoparty_bridge_a);
	AST_TEST_REGISTER(test_cdr_dial_answer_twoparty_bridge_b);
	AST_TEST_REGISTER(test_cdr_dial_answer_multiparty);

	AST_TEST_REGISTER(test_cdr_park);

	AST_TEST_REGISTER(test_cdr_fields);
	AST_TEST_REGISTER(test_cdr_no_reset_cdr);
	AST_TEST_REGISTER(test_cdr_fork_cdr);

	ast_test_register_init(TEST_CATEGORY, test_cdr_init_cb);
	ast_test_register_cleanup(TEST_CATEGORY, test_cdr_cleanup_cb);

	ast_channel_register(&test_cdr_chan_tech);
	ast_cdr_register(MOCK_CDR_BACKEND, "Mock CDR backend", mock_cdr_backend_cb);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "CDR unit tests");
