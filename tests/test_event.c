/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \brief Tests for the ast_event API
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * \ingroup tests
 *
 * \todo API Calls not yet touched by a test: XXX TODO
 *   - ast_event_queue_and_cache()
 *   - ast_event_get_cached()
 *   - ast_event_report_subs()
 *   - ast_event_dump_cache()
 *   - ast_event_get_ie_type_name()
 *   - ast_event_get_ie_pltype()
 *   - ast_event_str_to_event_type()
 *   - ast_event_str_to_ie_type()
 *   - ast_event_iterator_init()
 *   - ast_event_iterator_next()
 *   - ast_event_iterator_get_ie_type()
 *   - ast_event_iterator_get_ie_uint()
 *   - ast_event_iterator_get_ie_bitflags()
 *   - ast_event_iterator_get_ie_str()
 *   - ast_event_iterator_get_ie_raw()
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/test.h"
#include "asterisk/event.h"

static int check_event(struct ast_event *event, struct ast_test *test,
		enum ast_event_type expected_type, const char *type_name,
		const char *str, uint32_t uint, uint32_t bitflags)
{
	enum ast_event_type type;
	const void *foo;

	/* Check #1: Ensure event type is set properly. */
	type = ast_event_get_type(event);
	if (ast_event_get_type(event) != type) {
		ast_test_status_update(test, "Expected event type: '%d', got '%d'\n",
				expected_type, type);
		return -1;
	}

	/* Check #2: Check string representation of event type */
	if (strcmp(type_name, ast_event_get_type_name(event))) {
		ast_test_status_update(test, "Didn't get expected type name: '%s' != '%s'\n",
				type_name, ast_event_get_type_name(event));
		return -1;
	}

	/* Check #3: Check for automatically included EID */
	if (memcmp(&ast_eid_default, ast_event_get_ie_raw(event, AST_EVENT_IE_EID), sizeof(ast_eid_default))) {
		ast_test_status_update(test, "Failed to get EID\n");
		return -1;
	}

	/* Check #4: Check for the string IE */
	if (strcmp(str, ast_event_get_ie_str(event, AST_EVENT_IE_MAILBOX))) {
		ast_test_status_update(test, "Failed to get string IE.\n");
		return -1;
	}

	/* Check #5: Check for the uint IE */
	if (uint != ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS)) {
		ast_test_status_update(test, "Failed to get uint IE.\n");
		return -1;
	}

	/* Check #6: Check for the bitflags IE */
	if (bitflags != ast_event_get_ie_bitflags(event, AST_EVENT_IE_OLDMSGS)) {
		ast_test_status_update(test, "Failed to get bitflags IE.\n");
		return -1;
	}

	/* Check #7: Check if a check for a str IE that isn't there works */
	if ((foo = ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE))) {
		ast_test_status_update(test, "DEVICE IE check returned non-NULL %p\n", foo);
		return -1;
	}

	/* Check #8: Check if a check for a uint IE that isn't there returns 0 */
	if (ast_event_get_ie_uint(event, AST_EVENT_IE_STATE)) {
		ast_test_status_update(test, "OLDMSGS IE should be 0\n");
		return -1;
	}

	ast_test_status_update(test, "Event looks good.\n");

	return 0;
}

/*!
 * \internal
 */
AST_TEST_DEFINE(event_new_test)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_event *event = NULL, *event2 = NULL;

	static const enum ast_event_type type = AST_EVENT_CUSTOM;
	static const char str[] = "SIP/alligatormittens";
	static const uint32_t uint = 0xb00bface;
	static const uint32_t bitflags = 0x12488421;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_event_new_test";
		info->category = "main/event/";
		info->summary = "Test event creation";
		info->description =
			"This test exercises the API calls that allow allocation "
			"of an ast_event.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/*
	 * Test 2 methods of event creation:
	 *
	 * 1) Dynamic via appending each IE individually.
	 * 2) Statically, with all IEs in ast_event_new().
	 */

	ast_test_status_update(test, "First, test dynamic event creation...\n");

	if (!(event = ast_event_new(type, AST_EVENT_IE_END))) {
		ast_test_status_update(test, "Failed to allocate ast_event object.\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_append_ie_str(&event, AST_EVENT_IE_MAILBOX, str)) {
		ast_test_status_update(test, "Failed to append str IE\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_append_ie_uint(&event, AST_EVENT_IE_NEWMSGS, uint)) {
		ast_test_status_update(test, "Failed to append uint IE\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_append_ie_bitflags(&event, AST_EVENT_IE_OLDMSGS, bitflags)) {
		ast_test_status_update(test, "Failed to append bitflags IE\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_append_eid(&event)) {
		ast_test_status_update(test, "Failed to append EID\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (check_event(event, test, type, "Custom", str, uint, bitflags)) {
		ast_test_status_update(test, "Dynamically generated event broken\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	event2 = ast_event_new(type,
			AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, str,
			AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, uint,
			AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_BITFLAGS, bitflags,
			AST_EVENT_IE_END);

	if (!event2) {
		ast_test_status_update(test, "Failed to allocate ast_event object.\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (check_event(event2, test, type, "Custom", str, uint, bitflags)) {
		ast_test_status_update(test, "Statically generated event broken\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_get_size(event) != ast_event_get_size(event2)) {
		ast_test_status_update(test, "Events expected to be identical have different size: %d != %d\n",
				(int) ast_event_get_size(event),
				(int) ast_event_get_size(event2));
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

return_cleanup:
	if (event) {
		ast_event_destroy(event);
		event = NULL;
	}

	if (event2) {
		ast_event_destroy(event2);
		event2 = NULL;
	}

	return res;
}

struct event_sub_data {
	unsigned int count;
};

static void event_sub_cb(const struct ast_event *event, void *d)
{
	struct event_sub_data *data = d;

	data->count++;
}

/*!
 * \internal
 * \brief Test event subscriptions
 *
 * - Query for existing Subscriptions:
 *   - ast_event_check_subscriber()
 */
AST_TEST_DEFINE(event_sub_test)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_event *event;
	int i;
	enum ast_event_subscriber_res sub_res;
	static struct {
		struct ast_event_sub *sub;
		struct event_sub_data data;
		const unsigned int expected_count;
	} test_subs[] = {
		[0] = {
			.expected_count = 3,
		},
		[1] = {
			.expected_count = 1,
		},
		[2] = {
			.expected_count = 2,
		},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_event_subscribe_test";
		info->category = "main/event/";
		info->summary = "Test event subscriptions";
		info->description =
			"This test exercises the API calls that allow subscriptions "
			"to events.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/*
	 * Subscription #1:
	 *  - allocate normally
	 *  - subscribe to all CUSTOM events
	 *
	 * Subscription #2:
	 *  - allocate dynamically
	 *  - subscribe to all CUSTOM events
	 *  - add payload checks
	 *
	 * Subscription #3:
	 *  - allocate normally
	 *  - subscribe to all events with an IE check
	 */

	test_subs[0].sub = ast_event_subscribe(AST_EVENT_CUSTOM, event_sub_cb, "test_sub", &test_subs[0].data,
			AST_EVENT_IE_END);
	if (!test_subs[0].sub) {
		ast_test_status_update(test, "Failed to create test_subs[0].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[0].sub), "test_sub")) {
		ast_test_status_update(test,
				"Unexpected subscription description on test_subs[0].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	test_subs[1].sub = ast_event_subscribe_new(AST_EVENT_CUSTOM, event_sub_cb, &test_subs[1].data);
	if (!test_subs[1].sub) {
		ast_test_status_update(test, "Failed to create test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/* For the sake of exercising destruction before activation */
	ast_event_sub_destroy(test_subs[1].sub);

	test_subs[1].sub = ast_event_subscribe_new(AST_EVENT_CUSTOM, event_sub_cb, &test_subs[1].data);
	if (!test_subs[1].sub) {
		ast_test_status_update(test, "Failed to create test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[1].sub), "")) {
		ast_event_sub_destroy(test_subs[1].sub);
		test_subs[1].sub = NULL;
		ast_test_status_update(test,
				"Unexpected subscription description on test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_uint(test_subs[1].sub, AST_EVENT_IE_NEWMSGS, 3)) {
		ast_event_sub_destroy(test_subs[1].sub);
		test_subs[1].sub = NULL;
		ast_test_status_update(test, "Failed to append uint IE to test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_bitflags(test_subs[1].sub, AST_EVENT_IE_NEWMSGS, 1)) {
		ast_event_sub_destroy(test_subs[1].sub);
		test_subs[1].sub = NULL;
		ast_test_status_update(test, "Failed to append bitflags IE to test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_str(test_subs[1].sub, AST_EVENT_IE_DEVICE, "FOO/bar")) {
		ast_event_sub_destroy(test_subs[1].sub);
		test_subs[1].sub = NULL;
		ast_test_status_update(test, "Failed to append str IE to test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_raw(test_subs[1].sub, AST_EVENT_IE_MAILBOX, "800 km",
			strlen("800 km"))) {
		ast_event_sub_destroy(test_subs[1].sub);
		test_subs[1].sub = NULL;
		ast_test_status_update(test, "Failed to append raw IE to test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_exists(test_subs[1].sub, AST_EVENT_IE_DEVICE)) {
		ast_event_sub_destroy(test_subs[1].sub);
		test_subs[1].sub = NULL;
		ast_test_status_update(test, "Failed to append exists IE to test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_activate(test_subs[1].sub)) {
		ast_event_sub_destroy(test_subs[1].sub);
		test_subs[1].sub = NULL;
		ast_test_status_update(test, "Failed to activate test_subs[1].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	test_subs[2].sub = ast_event_subscribe(AST_EVENT_ALL, event_sub_cb, "test_sub", &test_subs[2].data,
			AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
			AST_EVENT_IE_END);
	if (!test_subs[2].sub) {
		ast_test_status_update(test, "Failed to create test_subs[2].sub\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Exercise the API call to check for existing subscriptions.
	 */

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
			AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
			AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_EXISTS) {
		ast_test_status_update(test, "subscription did not exist\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
			AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "BOOBS",
			AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "Someone subscribed to updates on boobs, lol? (%d)\n", sub_res);
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Fire off some events and track what was received in the callback
	 *
	 * event #1:
	 *  - simple custom event (will match sub 1 and 3)
	 *
	 * event #2:
	 *  - custom event with payloads that satisfy every payload check
	 *    for sub #2 (will match sub 1, 2, and 3)
	 *
	 * event #3:
	 *  - custom event that should only match sub #1
	 */

	event = ast_event_new(AST_EVENT_CUSTOM,
			AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
			AST_EVENT_IE_END);
	if (!event) {
		ast_test_status_update(test, "Failed to create event\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}
	if (ast_event_queue(event)) {
		ast_event_destroy(event);
		event = NULL;
		ast_test_status_update(test, "Failed to queue event\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	event = ast_event_new(AST_EVENT_CUSTOM,
			AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
			AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "800 km", strlen("800 km"),
			AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 3,
			AST_EVENT_IE_END);
	if (!event) {
		ast_test_status_update(test, "Failed to create event\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}
	if (ast_event_queue(event)) {
		ast_event_destroy(event);
		event = NULL;
		ast_test_status_update(test, "Failed to queue event\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	event = ast_event_new(AST_EVENT_CUSTOM,
			AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "blah",
			AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "801 km", strlen("801 km"),
			AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 0,
			AST_EVENT_IE_END);
	if (!event) {
		ast_test_status_update(test, "Failed to create event\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}
	if (ast_event_queue(event)) {
		ast_event_destroy(event);
		event = NULL;
		ast_test_status_update(test, "Failed to queue event\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}
	event = NULL;

	/*
	 * Check the results of the test.
	 *
	 * First of all, event distribution is asynchronous from the event producer,
	 * so knowing when to continue from here and check results is an instance of
	 * the halting problem.  A few seconds really should be more than enough time.
	 * If something was actually blocking event distribution that long, I would call
	 * it a bug.
	 *
	 * Expected results:
	 *  - sub 1, 2 events
	 *  - sub 2, 1 event
	 *  - sub 3, 2 events
	 */

	ast_test_status_update(test, "Sleeping a few seconds to allow event propagation...\n");
	sleep(3);

	for (i = 0; i < ARRAY_LEN(test_subs); i++) {
		if (test_subs[i].data.count != test_subs[i].expected_count) {
			ast_test_status_update(test, "Unexpected callback count, %u != %u for #%d\n",
					test_subs[i].data.count, test_subs[i].expected_count, i);
			res = AST_TEST_FAIL;
		}
	}

return_cleanup:
	for (i = 0; i < ARRAY_LEN(test_subs); i++) {
		if (test_subs[i].sub) {
			test_subs[i].sub = ast_event_unsubscribe(test_subs[i].sub);
		}
	}

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(event_new_test);
	AST_TEST_UNREGISTER(event_sub_test);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(event_new_test);
	AST_TEST_REGISTER(event_sub_test);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ast_event API Tests");
