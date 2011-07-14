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
	<support_level>extended</support_level>
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
		info->category = "/main/event/";
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

enum test_subs_class_type {
	TEST_SUBS_ALL_STR,
	TEST_SUBS_CUSTOM_STR,
	TEST_SUBS_CUSTOM_RAW,
	TEST_SUBS_CUSTOM_UINT,
	TEST_SUBS_CUSTOM_BITFLAGS,
	TEST_SUBS_CUSTOM_EXISTS,
	TEST_SUBS_CUSTOM_DYNAMIC,
	TEST_SUBS_CUSTOM_ANY,

	/* Must be last. */
	TEST_SUBS_TOTAL,
};

/*!
 * \internal
 * \brief Convert enum test_subs_class_type to string.
 *
 * \param val Enum value to convert to string.
 *
 * \return String equivalent of enum value.
 */
static const char *test_subs_class_type_str(enum test_subs_class_type val)
{
	switch (val) {
	case TEST_SUBS_ALL_STR:
		return "TEST_SUBS_ALL_STR";
	case TEST_SUBS_CUSTOM_STR:
		return "TEST_SUBS_CUSTOM_STR";
	case TEST_SUBS_CUSTOM_RAW:
		return "TEST_SUBS_CUSTOM_RAW";
	case TEST_SUBS_CUSTOM_UINT:
		return "TEST_SUBS_CUSTOM_UINT";
	case TEST_SUBS_CUSTOM_BITFLAGS:
		return "TEST_SUBS_CUSTOM_BITFLAGS";
	case TEST_SUBS_CUSTOM_EXISTS:
		return "TEST_SUBS_CUSTOM_EXISTS";
	case TEST_SUBS_CUSTOM_DYNAMIC:
		return "TEST_SUBS_CUSTOM_DYNAMIC";
	case TEST_SUBS_CUSTOM_ANY:
		return "TEST_SUBS_CUSTOM_ANY";
	case TEST_SUBS_TOTAL:
		break;
	}
	return "Unknown";
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
	struct {
		struct ast_event_sub *sub;
		struct event_sub_data data;
		const unsigned int expected_count;
	} test_subs[TEST_SUBS_TOTAL] = {
		[TEST_SUBS_ALL_STR] = {
			.expected_count = 2,
		},
		[TEST_SUBS_CUSTOM_STR] = {
			.expected_count = 2,
		},
		[TEST_SUBS_CUSTOM_RAW] = {
			.expected_count = 2,
		},
		[TEST_SUBS_CUSTOM_UINT] = {
			.expected_count = 1,
		},
		[TEST_SUBS_CUSTOM_BITFLAGS] = {
			.expected_count = 4,
		},
		[TEST_SUBS_CUSTOM_EXISTS] = {
			.expected_count = 2,
		},
		[TEST_SUBS_CUSTOM_DYNAMIC] = {
			.expected_count = 1,
		},
		[TEST_SUBS_CUSTOM_ANY] = {
			.expected_count = 6,
		},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_event_subscribe_test";
		info->category = "/main/event/";
		info->summary = "Test event subscriptions";
		info->description =
			"This test exercises the API calls that allow subscriptions "
			"to events.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Check that NO CUSTOM subscribers exist\n");
	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "CUSTOM subscriptions should not exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	/*
	 * Subscription TEST_SUBS_CUSTOM_STR:
	 *  - allocate normally
	 *  - subscribe to CUSTOM events with a DEVICE STR IE check
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_CUSTOM_STR subscription\n");
	test_subs[TEST_SUBS_CUSTOM_STR].sub = ast_event_subscribe(AST_EVENT_CUSTOM, event_sub_cb,
		test_subs_class_type_str(TEST_SUBS_CUSTOM_STR), &test_subs[TEST_SUBS_CUSTOM_STR].data,
		AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
		AST_EVENT_IE_END);
	if (!test_subs[TEST_SUBS_CUSTOM_STR].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_STR subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_CUSTOM_STR].sub),
		test_subs_class_type_str(TEST_SUBS_CUSTOM_STR))) {
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_CUSTOM_STR subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	ast_test_status_update(test, "Check that a CUSTOM subscriber exists\n");
	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_EXISTS) {
		ast_test_status_update(test, "A CUSTOM subscription should exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	/*
	 * Subscription TEST_SUBS_ALL_STR:
	 *  - allocate normally
	 *  - subscribe to ALL events with a DEVICE STR IE check
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_ALL_STR subscription\n");
	test_subs[TEST_SUBS_ALL_STR].sub = ast_event_subscribe(AST_EVENT_ALL, event_sub_cb,
		test_subs_class_type_str(TEST_SUBS_ALL_STR), &test_subs[TEST_SUBS_ALL_STR].data,
		AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
		AST_EVENT_IE_END);
	if (!test_subs[TEST_SUBS_ALL_STR].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_ALL_STR subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_ALL_STR].sub),
		test_subs_class_type_str(TEST_SUBS_ALL_STR))) {
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_ALL_STR subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Subscription TEST_SUBS_CUSTOM_RAW:
	 *  - allocate normally
	 *  - subscribe to CUSTOM events with a MAILBOX RAW IE check
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_CUSTOM_RAW subscription\n");
	test_subs[TEST_SUBS_CUSTOM_RAW].sub = ast_event_subscribe(AST_EVENT_CUSTOM, event_sub_cb,
		test_subs_class_type_str(TEST_SUBS_CUSTOM_RAW), &test_subs[TEST_SUBS_CUSTOM_RAW].data,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "FOO/bar", sizeof("FOO/bar"),
		AST_EVENT_IE_END);
	if (!test_subs[TEST_SUBS_CUSTOM_RAW].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_RAW subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_CUSTOM_RAW].sub),
		test_subs_class_type_str(TEST_SUBS_CUSTOM_RAW))) {
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_CUSTOM_RAW subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Subscription TEST_SUBS_CUSTOM_UINT:
	 *  - allocate normally
	 *  - subscribe to CUSTOM events with a NEWMSGS UINT IE check
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_CUSTOM_UINT subscription\n");
	test_subs[TEST_SUBS_CUSTOM_UINT].sub = ast_event_subscribe(AST_EVENT_CUSTOM, event_sub_cb,
		test_subs_class_type_str(TEST_SUBS_CUSTOM_UINT), &test_subs[TEST_SUBS_CUSTOM_UINT].data,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 5,
		AST_EVENT_IE_END);
	if (!test_subs[TEST_SUBS_CUSTOM_UINT].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_UINT subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_CUSTOM_UINT].sub),
		test_subs_class_type_str(TEST_SUBS_CUSTOM_UINT))) {
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_CUSTOM_UINT subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Subscription TEST_SUBS_CUSTOM_BITFLAGS:
	 *  - allocate normally
	 *  - subscribe to CUSTOM events with a NEWMSGS BITFLAGS IE check
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_CUSTOM_BITFLAGS subscription\n");
	test_subs[TEST_SUBS_CUSTOM_BITFLAGS].sub = ast_event_subscribe(AST_EVENT_CUSTOM, event_sub_cb,
		test_subs_class_type_str(TEST_SUBS_CUSTOM_BITFLAGS), &test_subs[TEST_SUBS_CUSTOM_BITFLAGS].data,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_BITFLAGS, 0x06,
		AST_EVENT_IE_END);
	if (!test_subs[TEST_SUBS_CUSTOM_BITFLAGS].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_BITFLAGS subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_CUSTOM_BITFLAGS].sub),
		test_subs_class_type_str(TEST_SUBS_CUSTOM_BITFLAGS))) {
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_CUSTOM_BITFLAGS subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Subscription TEST_SUBS_CUSTOM_EXISTS:
	 *  - allocate normally
	 *  - subscribe to CUSTOM events with a NEWMSGS UINT and OLDMSGS EXISTS IE check
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_CUSTOM_EXISTS subscription\n");
	test_subs[TEST_SUBS_CUSTOM_EXISTS].sub = ast_event_subscribe(AST_EVENT_CUSTOM, event_sub_cb,
		test_subs_class_type_str(TEST_SUBS_CUSTOM_EXISTS), &test_subs[TEST_SUBS_CUSTOM_EXISTS].data,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 4,
		AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_EXISTS,
		AST_EVENT_IE_END);
	if (!test_subs[TEST_SUBS_CUSTOM_EXISTS].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_EXISTS subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_CUSTOM_EXISTS].sub),
		test_subs_class_type_str(TEST_SUBS_CUSTOM_EXISTS))) {
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_CUSTOM_EXISTS subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/* For the sake of exercising destruction before activation */
	test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = ast_event_subscribe_new(AST_EVENT_CUSTOM,
		event_sub_cb, &test_subs[TEST_SUBS_CUSTOM_DYNAMIC].data);
	if (!test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}
	ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);

	/*
	 * Subscription TEST_SUBS_CUSTOM_DYNAMIC:
	 *  - allocate dynamically
	 *  - subscribe to all CUSTOM events
	 *  - add IE checks for all types
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
	test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = ast_event_subscribe_new(AST_EVENT_CUSTOM,
		event_sub_cb, &test_subs[TEST_SUBS_CUSTOM_DYNAMIC].data);
	if (!test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub),
		"")) {
		ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);
		test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = NULL;
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_uint(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub, AST_EVENT_IE_NEWMSGS, 4)) {
		ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);
		test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = NULL;
		ast_test_status_update(test, "Failed to append UINT IE to TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_bitflags(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub, AST_EVENT_IE_OLDMSGS, 1)) {
		ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);
		test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = NULL;
		ast_test_status_update(test, "Failed to append BITFLAGS IE to TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_str(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub, AST_EVENT_IE_DEVICE, "FOO/bar")) {
		ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);
		test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = NULL;
		ast_test_status_update(test, "Failed to append STR IE to TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_raw(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub, AST_EVENT_IE_MAILBOX, "800 km",
			strlen("800 km"))) {
		ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);
		test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = NULL;
		ast_test_status_update(test, "Failed to append RAW IE to TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_append_ie_exists(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub, AST_EVENT_IE_STATE)) {
		ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);
		test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = NULL;
		ast_test_status_update(test, "Failed to append EXISTS IE to TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_sub_activate(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub)) {
		ast_event_sub_destroy(test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub);
		test_subs[TEST_SUBS_CUSTOM_DYNAMIC].sub = NULL;
		ast_test_status_update(test, "Failed to activate TEST_SUBS_CUSTOM_DYNAMIC subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Exercise the API call to check for existing subscriptions.
	 */
	ast_test_status_update(test, "Checking for subscribers to specific events\n");

	/* Check STR matching. */
	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_EXISTS) {
		ast_test_status_update(test, "Str FOO/bar subscription did not exist\n");
		res = AST_TEST_FAIL;
	}

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "Money",
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "Str Money subscription should not exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	/* Check RAW matching. */
	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "FOO/bar", sizeof("FOO/bar"),
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_EXISTS) {
		ast_test_status_update(test, "Raw FOO/bar subscription did not exist\n");
		res = AST_TEST_FAIL;
	}

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "FOO/bar", sizeof("FOO/bar") - 1,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "Raw FOO/bar-1 subscription should not exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "Monkeys", sizeof("Monkeys"),
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "Raw Monkeys subscription should not exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	/* Check UINT matching. */
	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 5,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_EXISTS) {
		ast_test_status_update(test, "UINT=5 subscription did not exist\n");
		res = AST_TEST_FAIL;
	}

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 1,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "UINT=1 subscription should not exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	/* Check BITFLAGS matching. */
	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_BITFLAGS, 2,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_EXISTS) {
		ast_test_status_update(test, "BITFLAGS=2 subscription did not exist\n");
		res = AST_TEST_FAIL;
	}

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_BITFLAGS, 8,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "BITFLAGS=8 subscription should not exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	/* Check EXISTS matching. */
	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 4,
		AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, 100,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_EXISTS) {
		ast_test_status_update(test, "EXISTS subscription did not exist\n");
		res = AST_TEST_FAIL;
	}

	sub_res = ast_event_check_subscriber(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 4,
		AST_EVENT_IE_END);
	if (sub_res != AST_EVENT_SUB_NONE) {
		ast_test_status_update(test, "EXISTS subscription should not exist! (%d)\n",
			sub_res);
		res = AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Special event posting test\n");

	/*
	 * Event to check if event is even posted.
	 *
	 * Matching subscriptions:
	 * TEST_SUBS_CUSTOM_RAW
	 */
	event = ast_event_new(AST_EVENT_CUSTOM,
		AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "Mula",
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "FOO/bar", sizeof("FOO/bar"),
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

	ast_test_status_update(test, "Sleeping a few seconds to allow event propagation...\n");
	sleep(3);

	/*
	 * Subscription TEST_SUBS_CUSTOM_ANY:
	 *  - allocate normally
	 *  - subscribe to all CUSTOM events
	 */
	ast_test_status_update(test, "Adding TEST_SUBS_CUSTOM_ANY subscription\n");
	test_subs[TEST_SUBS_CUSTOM_ANY].sub = ast_event_subscribe(AST_EVENT_CUSTOM, event_sub_cb,
		test_subs_class_type_str(TEST_SUBS_CUSTOM_ANY), &test_subs[TEST_SUBS_CUSTOM_ANY].data,
		AST_EVENT_IE_END);
	if (!test_subs[TEST_SUBS_CUSTOM_ANY].sub) {
		ast_test_status_update(test, "Failed to create TEST_SUBS_CUSTOM_ANY subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (strcmp(ast_event_subscriber_get_description(test_subs[TEST_SUBS_CUSTOM_ANY].sub),
		test_subs_class_type_str(TEST_SUBS_CUSTOM_ANY))) {
		ast_test_status_update(test,
			"Unexpected subscription description on TEST_SUBS_CUSTOM_ANY subscription\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	/*
	 * Fire off some events and track what was received in the callback
	 */
	ast_test_status_update(test, "Posting test events\n");

	/*
	 * Event to check STR matching.
	 *
	 * Matching subscriptions:
	 * TEST_SUBS_ALL_STR
	 * TEST_SUBS_CUSTOM_STR
	 * TEST_SUBS_CUSTOM_ANY
	 */
	event = ast_event_new(AST_EVENT_CUSTOM,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "FOO/bar", sizeof("FOO/bar") - 1,
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

	/*
	 * Event to check RAW matching.
	 *
	 * Matching subscriptions:
	 * TEST_SUBS_CUSTOM_RAW
	 * TEST_SUBS_CUSTOM_ANY
	 */
	event = ast_event_new(AST_EVENT_CUSTOM,
		AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "Misery",
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "FOO/bar", sizeof("FOO/bar"),
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

	/*
	 * Event to check UINT matching.
	 *
	 * Matching subscriptions:
	 * TEST_SUBS_CUSTOM_UINT
	 * TEST_SUBS_CUSTOM_BITFLAGS
	 * TEST_SUBS_CUSTOM_ANY
	 */
	event = ast_event_new(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 5,
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

	/*
	 * Event to check BITFLAGS matching.
	 *
	 * Matching subscriptions:
	 * TEST_SUBS_CUSTOM_BITFLAGS
	 * TEST_SUBS_CUSTOM_ANY
	 */
	event = ast_event_new(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 4,
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

	/*
	 * Event to check EXISTS matching.
	 *
	 * Matching subscriptions:
	 * TEST_SUBS_CUSTOM_EXISTS
	 * TEST_SUBS_CUSTOM_BITFLAGS
	 * TEST_SUBS_CUSTOM_ANY
	 */
	event = ast_event_new(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 4,
		AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, 4,
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

	/*
	 * Event to get dynamic subscription to have an event.
	 *
	 * Matching subscriptions:
	 * TEST_SUBS_CUSTOM_DYNAMIC
	 * TEST_SUBS_CUSTOM_BITFLAGS
	 * TEST_SUBS_CUSTOM_EXISTS
	 * TEST_SUBS_ALL_STR
	 * TEST_SUBS_CUSTOM_STR
	 * TEST_SUBS_CUSTOM_ANY
	 */
	event = ast_event_new(AST_EVENT_CUSTOM,
		AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, 4,
		AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, 5,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_RAW, "800 km", strlen("800 km"),
		AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, "FOO/bar",
		AST_EVENT_IE_STATE, AST_EVENT_IE_PLTYPE_UINT, 5,
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
	 * See test_subs[] initialization for expected results.
	 */

	ast_test_status_update(test, "Sleeping a few seconds to allow event propagation...\n");
	sleep(3);

	for (i = 0; i < ARRAY_LEN(test_subs); i++) {
		if (!test_subs[i].sub) {
			ast_test_status_update(test, "Missing a test subscription for %s\n",
				test_subs_class_type_str(i));
			res = AST_TEST_FAIL;
		}
		if (test_subs[i].data.count != test_subs[i].expected_count) {
			ast_test_status_update(test,
				"Unexpected callback count, got %u expected %u for %s\n",
				test_subs[i].data.count, test_subs[i].expected_count,
				test_subs_class_type_str(i));
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
