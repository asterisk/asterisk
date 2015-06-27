/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Device State Test Module
 *
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/devicestate.h"
#include "asterisk/pbx.h"
#include "asterisk/event.h"
#include "asterisk/vector.h"

#define DEVICE_STATE_CHANNEL_TYPE "TestDeviceState"

#define DEVSTATE_PROVIDER "TestDevState"

#define DEVSTATE_PROVIDER_LC "testdevstate"

#define DEVSTATE_PROVIDER_LEN 12

/*! \brief Subscription to device state change events */
static struct ast_event_sub *device_state_sub;

/*! \brief Used to assign an increasing integer to channel name */
static unsigned int chan_idx;

/* These arrays are the result of the 'core show device2extenstate' output. */
static int combined_results[] = {
	AST_DEVICE_UNKNOWN,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_UNKNOWN,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_UNKNOWN,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_INVALID,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
};

static int exten_results[] = {
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
};

/*! \brief Mutex for \c update_cond */
AST_MUTEX_DEFINE_STATIC(update_lock);

/*! \brief Condition wait variable for device state updates */
static ast_cond_t update_cond;

/*! \brief Mutext for \c channel_cb_cond */
AST_MUTEX_DEFINE_STATIC(channel_cb_lock);

/*! \brief Condition wait variable for channel tech device state cb */
static ast_cond_t channel_cb_cond;

/*! \brief The resulting device state updates caused by some function call */
static AST_VECTOR(, enum ast_device_state) result_states;

/*! \brief The current device state for our device state provider */
static enum ast_device_state current_device_state;

/*! \brief Clear out all recorded device states in \ref result_states */
static void clear_result_states(void)
{
	ast_mutex_lock(&update_lock);
	while (AST_VECTOR_SIZE(&result_states) > 0) {
		AST_VECTOR_REMOVE_UNORDERED(&result_states, 0);
	}
	ast_mutex_unlock(&update_lock);
}

static void device_state_cb(const struct ast_event *event, void *userdata)
{
	enum ast_device_state state;
	const char *device;

	state = ast_event_get_ie_uint(event, AST_EVENT_IE_STATE);
	device = ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE);

	if (ast_strlen_zero(device)) {
		return;
	}

	if (strncasecmp(device, DEVSTATE_PROVIDER, DEVSTATE_PROVIDER_LEN)) {
		/* Not our device state change */
		return;
	}

	ast_mutex_lock(&update_lock);
	AST_VECTOR_APPEND(&result_states, state);
	ast_cond_signal(&update_cond);
	ast_mutex_unlock(&update_lock);
}

static enum ast_device_state devstate_prov_cb(const char *data)
{
	return current_device_state;
}

static int wait_for_device_state_updates(struct ast_test *test, int expected_updates)
{
	int error;
	struct timeval wait_now = ast_tvnow();
	struct timespec wait_time = { .tv_sec = wait_now.tv_sec + 1, .tv_nsec = wait_now.tv_usec * 1000 };

	ast_mutex_lock(&update_lock);
	while (AST_VECTOR_SIZE(&result_states) != expected_updates) {
		error = ast_cond_timedwait(&update_cond, &update_lock, &wait_time);
		if (error == ETIMEDOUT) {
			ast_test_status_update(test, "Test timed out while waiting for %d expected updates\n", expected_updates);
			break;
		}
	}
	ast_mutex_unlock(&update_lock);

	ast_test_status_update(test, "Received %zu of %d updates\n", AST_VECTOR_SIZE(&result_states), expected_updates);
	return !(AST_VECTOR_SIZE(&result_states) == expected_updates);
}

AST_TEST_DEFINE(device2extenstate_test)
{
	int res = AST_TEST_PASS;
	struct ast_devstate_aggregate agg;
	enum ast_device_state i, j, combined;
	enum ast_extension_states exten;
	int k = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "device2extenstate_test";
		info->category = "/main/devicestate/";
		info->summary = "Tests combined devstate mapping and device to extension state mapping.";
		info->description =
			"Verifies device state aggregate results match the expected combined "
			"devstate.  Then verifies the combined devstate maps to the expected "
			"extension state.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ARRAY_LEN(exten_results) != (AST_DEVICE_TOTAL * AST_DEVICE_TOTAL)) {
		ast_test_status_update(test, "Result array is %d long when it should be %d. "
			"Something has changed, this test must be updated.\n",
			(int) ARRAY_LEN(exten_results), (AST_DEVICE_TOTAL * AST_DEVICE_TOTAL));
		return AST_TEST_FAIL;
	}

	if (ARRAY_LEN(combined_results) != ARRAY_LEN(exten_results)) {
		ast_test_status_update(test, "combined_results and exten_results arrays do not match in length.\n");
		return AST_TEST_FAIL;
	}

	for (i = 0; i < AST_DEVICE_TOTAL; i++) {
		for (j = 0; j < AST_DEVICE_TOTAL; j++) {
			ast_devstate_aggregate_init(&agg);
			ast_devstate_aggregate_add(&agg, i);
			ast_devstate_aggregate_add(&agg, j);
			combined = ast_devstate_aggregate_result(&agg);
			if (combined_results[k] != combined) {
				ast_test_status_update(test, "Expected combined dev state %s "
					"does not match %s at combined_result[%d].\n",
					ast_devstate2str(combined_results[k]),
					ast_devstate2str(combined), k);
				res = AST_TEST_FAIL;
			}

			exten = ast_devstate_to_extenstate(combined);

			if (exten_results[k] != exten) {
				ast_test_status_update(test, "Expected exten state %s "
					"does not match %s at exten_result[%d]\n",
					ast_extension_state2str(exten_results[k]),
					ast_extension_state2str(exten), k);
				res = AST_TEST_FAIL;
			}
			k++;
		}
	}

	return res;
}

AST_TEST_DEFINE(devstate_prov_add)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/main/devicestate/";
		info->summary = "Test adding a device state provider";
		info->description =
			"Test that a custom device state provider can be added, and that\n"
			"it cannot be added if already added.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_devstate_prov_add(DEVSTATE_PROVIDER, devstate_prov_cb) == 0);
	ast_test_validate(test, ast_devstate_prov_add(DEVSTATE_PROVIDER, devstate_prov_cb) != 0);
	ast_test_validate(test, ast_devstate_prov_del(DEVSTATE_PROVIDER) == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(devstate_prov_del)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/main/devicestate/";
		info->summary = "Test removing a device state provider";
		info->description =
			"Test that a custom device state provider can be removed, and that\n"
			"it cannot be removed if already removed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_devstate_prov_add(DEVSTATE_PROVIDER, devstate_prov_cb) == 0);
	ast_test_validate(test, ast_devstate_prov_del(DEVSTATE_PROVIDER) == 0);
	ast_test_validate(test, ast_devstate_prov_del(DEVSTATE_PROVIDER) != 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(devstate_changed)
{
	int i;
	enum ast_device_state expected_results[] = {
		AST_DEVICE_NOT_INUSE,
		AST_DEVICE_INUSE,
		AST_DEVICE_BUSY,
		AST_DEVICE_INVALID,
		AST_DEVICE_UNAVAILABLE,
		AST_DEVICE_RINGING,
		AST_DEVICE_RINGINUSE,
		AST_DEVICE_ONHOLD,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/main/devicestate/";
		info->summary = "Test updates coming from a device state provider";
		info->description =
			"This unit test checks that a custom device state provider can\n"
			"have updates published for it. This includes both cacheable and\n"
			"non-cacheable events. In the case of non-cacheable events, the\n"
			"device state provider's callback function is queried for the\n"
			"device state when AST_DEVICE_UNKNOWN is published.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	clear_result_states();
	current_device_state = AST_DEVICE_BUSY;

	ast_test_validate(test, ast_devstate_prov_add(DEVSTATE_PROVIDER, devstate_prov_cb) == 0);

	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_INUSE, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_BUSY, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_INVALID, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_UNAVAILABLE, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_RINGING, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_RINGINUSE, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_ONHOLD, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);

	ast_test_validate(test, wait_for_device_state_updates(test, 8) == 0);
	for (i = 0; i < AST_VECTOR_SIZE(&result_states); i++) {
		ast_test_status_update(test, "Testing update %d: actual is %d; expected is %d\n",
			i,
			AST_VECTOR_GET(&result_states, i),
			expected_results[i]);
		ast_test_validate(test, AST_VECTOR_GET(&result_states, i) == expected_results[i]);
	}

	clear_result_states();

	/*
	 * Since an update of AST_DEVICE_UNKNOWN will cause a different thread to retrieve
	 * the update from the custom device state provider, check it separately from the
	 * updates above.
	 */
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_UNKNOWN, AST_DEVSTATE_NOT_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, wait_for_device_state_updates(test, 1) == 0);
	ast_test_validate(test, AST_VECTOR_GET(&result_states, 0) == AST_DEVICE_BUSY);
	ast_test_validate(test, ast_device_state(DEVSTATE_PROVIDER ":foo") == AST_DEVICE_BUSY);
	ast_test_validate(test, ast_device_state(DEVSTATE_PROVIDER_LC ":foo") == AST_DEVICE_BUSY);

	clear_result_states();

	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_BUSY, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_INVALID, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_UNAVAILABLE, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_RINGING, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_RINGINUSE, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);
	ast_test_validate(test, ast_devstate_changed_literal(AST_DEVICE_ONHOLD, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo") == 0);

	ast_test_validate(test, wait_for_device_state_updates(test, 8) == 0);
	for (i = 0; i < AST_VECTOR_SIZE(&result_states); i++) {
		ast_test_status_update(test, "Testing update %d: actual is %d; expected is %d\n",
			i,
			AST_VECTOR_GET(&result_states, i),
			expected_results[i]);
		ast_test_validate(test, AST_VECTOR_GET(&result_states, i) == expected_results[i]);
	}

	/*
	 * Check the last value in the cache. Note that this should not hit
	 * the value of current_device_state.
	 */
	ast_test_validate(test, ast_device_state(DEVSTATE_PROVIDER ":foo") == AST_DEVICE_ONHOLD);
	/*
	 * This will miss on the cache, as it is case sensitive. It should go
	 * hit our device state callback however.
	 */
	ast_test_validate(test, ast_device_state(DEVSTATE_PROVIDER_LC ":foo") == AST_DEVICE_BUSY);

	/* Generally, this test can't be run twice in a row, as you can't remove an
	 * item from the cache. Hence, subsequent runs won't hit the device state provider,
	 * and will merely return the cached value.
	 *
	 * To avoid annoying errors, set the last state to BUSY here.
	 */
	ast_devstate_changed_literal(AST_DEVICE_BUSY, AST_DEVSTATE_CACHABLE, DEVSTATE_PROVIDER ":foo");

	ast_test_validate(test, ast_devstate_prov_del(DEVSTATE_PROVIDER) == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(devstate_conversions)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/main/devicestate/";
		info->summary = "Test ast_device_state conversions";
		info->description =
			"Test various transformations of ast_device_state values.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_UNKNOWN), "UNKNOWN"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_NOT_INUSE), "NOT_INUSE"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_INUSE), "INUSE"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_BUSY), "BUSY"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_INVALID), "INVALID"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_UNAVAILABLE), "UNAVAILABLE"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_RINGING), "RINGING"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_RINGINUSE), "RINGINUSE"));
	ast_test_validate(test, !strcmp(ast_devstate_str(AST_DEVICE_ONHOLD), "ONHOLD"));

	ast_test_validate(test, ast_devstate_val("UNKNOWN") == AST_DEVICE_UNKNOWN);
	ast_test_validate(test, ast_devstate_val("NOT_INUSE") == AST_DEVICE_NOT_INUSE);
	ast_test_validate(test, ast_devstate_val("INUSE") == AST_DEVICE_INUSE);
	ast_test_validate(test, ast_devstate_val("BUSY") == AST_DEVICE_BUSY);
	ast_test_validate(test, ast_devstate_val("INVALID") == AST_DEVICE_INVALID);
	ast_test_validate(test, ast_devstate_val("UNAVAILABLE") == AST_DEVICE_UNAVAILABLE);
	ast_test_validate(test, ast_devstate_val("RINGING") == AST_DEVICE_RINGING);
	ast_test_validate(test, ast_devstate_val("RINGINUSE") == AST_DEVICE_RINGINUSE);
	ast_test_validate(test, ast_devstate_val("ONHOLD") == AST_DEVICE_ONHOLD);
	ast_test_validate(test, ast_devstate_val("onhold") == AST_DEVICE_ONHOLD);
	ast_test_validate(test, ast_devstate_val("FOO") == AST_DEVICE_UNKNOWN);

	ast_test_validate(test, ast_state_chan2dev(AST_STATE_DOWN) == AST_DEVICE_NOT_INUSE);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_RESERVED) == AST_DEVICE_INUSE);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_OFFHOOK) == AST_DEVICE_INUSE);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_DIALING) == AST_DEVICE_INUSE);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_RING) == AST_DEVICE_INUSE);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_RINGING) == AST_DEVICE_RINGING);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_UP) == AST_DEVICE_INUSE);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_BUSY) == AST_DEVICE_BUSY);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_DIALING_OFFHOOK) == AST_DEVICE_INUSE);
	ast_test_validate(test, ast_state_chan2dev(AST_STATE_PRERING) == AST_DEVICE_RINGING);

	return AST_TEST_PASS;
}

/*! \brief Whether or not the channel device state callback was called */
static int chan_callback_called;

/*! \brief Wait until the test channel driver's devicestate callback is called */
static int wait_for_channel_callback(struct ast_test *test)
{
	int error;
	struct timeval wait_now = ast_tvnow();
	struct timespec wait_time = { .tv_sec = wait_now.tv_sec + 1, .tv_nsec = wait_now.tv_usec * 1000 };

	ast_mutex_lock(&channel_cb_lock);
	while (!chan_callback_called) {
		error = ast_cond_timedwait(&channel_cb_cond, &channel_cb_lock, &wait_time);
		if (error == ETIMEDOUT) {
			ast_test_status_update(test, "Test timed out while waiting channel callback\n");
			break;
		}
	}
	ast_mutex_unlock(&channel_cb_lock);

	return chan_callback_called;
}

static void safe_hangup(void *object)
{
	struct ast_channel *chan = object;

	if (!chan) {
		return;
	}
	ast_hangup(chan);
}

AST_TEST_DEFINE(devstate_channels)
{
	RAII_VAR(struct ast_channel *, chan, NULL, safe_hangup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/main/devicestate/";
		info->summary = "Test deriving device state from a channel's state";
		info->description =
			"Test querying a channel's state to derive a device state.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	chan_callback_called = 0;

	chan = ast_channel_alloc(0, AST_STATE_RINGING, "", "", "", "s", "default",
		NULL, 0, DEVICE_STATE_CHANNEL_TYPE "/foo-%08x",
		(unsigned) ast_atomic_fetchadd_int((int *) &chan_idx, +1));
	ast_test_validate(test, chan != NULL);

	ast_test_validate(test, ast_parse_device_state(DEVICE_STATE_CHANNEL_TYPE "/foo") == AST_DEVICE_RINGING);
	ast_test_validate(test, ast_parse_device_state(DEVICE_STATE_CHANNEL_TYPE "/bad") == AST_DEVICE_UNKNOWN);

	ast_setstate(chan, AST_STATE_UP);

	ast_test_validate(test, wait_for_channel_callback(test) == 1);
	ast_test_validate(test, ast_parse_device_state(DEVICE_STATE_CHANNEL_TYPE "/foo") == AST_DEVICE_INUSE);

	chan_callback_called = 0;

	return AST_TEST_PASS;
}

static int chan_test_devicestate_cb(const char *device_number)
{
	/* Simply record that we were called when expected */
	chan_callback_called = 1;

	ast_mutex_lock(&channel_cb_lock);
	ast_cond_signal(&channel_cb_cond);
	ast_mutex_unlock(&channel_cb_lock);

	return AST_DEVICE_INUSE;
}

struct ast_channel_tech chan_test_devicestate = {
	.type = DEVICE_STATE_CHANNEL_TYPE,
	.description = "Device State Unit Test Channel Driver",
	.devicestate = chan_test_devicestate_cb,
};

static int unload_module(void)
{
	if (device_state_sub) {
		ast_event_unsubscribe(device_state_sub);
	}
	AST_VECTOR_FREE(&result_states);
	ast_channel_unregister(&chan_test_devicestate);

	AST_TEST_UNREGISTER(device2extenstate_test);

	AST_TEST_UNREGISTER(devstate_prov_add);
	AST_TEST_UNREGISTER(devstate_prov_del);

	AST_TEST_UNREGISTER(devstate_changed);
	AST_TEST_UNREGISTER(devstate_conversions);

	AST_TEST_UNREGISTER(devstate_channels);

	return 0;
}

static int load_module(void)
{

	device_state_sub = ast_event_subscribe(AST_EVENT_DEVICE_STATE, device_state_cb,
		"Device State Unit Tests", NULL, AST_EVENT_IE_END);
	if (!device_state_sub) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (AST_VECTOR_INIT(&result_states, 8) == -1) {
		ast_event_unsubscribe(device_state_sub);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_channel_register(&chan_test_devicestate)) {
		ast_event_unsubscribe(device_state_sub);
		AST_VECTOR_FREE(&result_states);
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(device2extenstate_test);

	AST_TEST_REGISTER(devstate_prov_add);
	AST_TEST_REGISTER(devstate_prov_del);

	AST_TEST_REGISTER(devstate_changed);
	AST_TEST_REGISTER(devstate_conversions);

	AST_TEST_REGISTER(devstate_channels);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Device State Test");
