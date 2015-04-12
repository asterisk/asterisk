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
 *   - ast_event_get_ie_type_name()
 *   - ast_event_get_ie_pltype()
 *   - ast_event_iterator_init()
 *   - ast_event_iterator_next()
 *   - ast_event_iterator_get_ie_type()
 *   - ast_event_iterator_get_ie_uint()
 *   - ast_event_iterator_get_ie_str()
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/test.h"
#include "asterisk/event.h"

static int check_event(struct ast_event *event, struct ast_test *test,
		enum ast_event_type expected_type, const char *type_name,
		const char *str, uint32_t uint)
{
	enum ast_event_type type;
	const void *foo;

	/* Check #1: Ensure event type is set properly. */
	type = ast_event_get_type(event);
	if (ast_event_get_type(event) != type) {
		ast_test_status_update(test, "Expected event type: '%u', got '%u'\n",
				expected_type, type);
		return -1;
	}

	/* Check #4: Check for the string IE */
	if (strcmp(str, ast_event_get_ie_str(event, AST_EVENT_IE_CEL_USEREVENT_NAME))) {
		ast_test_status_update(test, "Failed to get string IE.\n");
		return -1;
	}

	/* Check #5: Check for the uint IE */
	if (uint != ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_AMAFLAGS)) {
		ast_test_status_update(test, "Failed to get uint IE.\n");
		return -1;
	}

	/* Check #6: Check if a check for a str IE that isn't there works */
	if ((foo = ast_event_get_ie_str(event, AST_EVENT_IE_CEL_CIDNAME))) {
		ast_test_status_update(test, "CEL_CIDNAME IE check returned non-NULL %p\n", foo);
		return -1;
	}

	/* Check #7: Check if a check for a uint IE that isn't there returns 0 */
	if (ast_event_get_ie_uint(event, AST_EVENT_IE_CEL_EVENT_TIME_USEC)) {
		ast_test_status_update(test, "UNIQUEID IE should be 0\n");
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

	if (ast_event_append_ie_str(&event, AST_EVENT_IE_CEL_USEREVENT_NAME, str)) {
		ast_test_status_update(test, "Failed to append str IE\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (ast_event_append_ie_uint(&event, AST_EVENT_IE_CEL_AMAFLAGS, uint)) {
		ast_test_status_update(test, "Failed to append uint IE\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (check_event(event, test, type, "Custom", str, uint)) {
		ast_test_status_update(test, "Dynamically generated event broken\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	event2 = ast_event_new(type,
			AST_EVENT_IE_CEL_USEREVENT_NAME, AST_EVENT_IE_PLTYPE_STR, str,
			AST_EVENT_IE_CEL_AMAFLAGS, AST_EVENT_IE_PLTYPE_UINT, uint,
			AST_EVENT_IE_END);

	if (!event2) {
		ast_test_status_update(test, "Failed to allocate ast_event object.\n");
		res = AST_TEST_FAIL;
		goto return_cleanup;
	}

	if (check_event(event2, test, type, "Custom", str, uint)) {
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

static int unload_module(void)
{
	AST_TEST_UNREGISTER(event_new_test);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(event_new_test);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ast_event API Tests");
