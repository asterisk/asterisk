/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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

/*! \file
 *
 * \brief Call Parking Unit Tests
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "res_parking.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/test.h"
#include "asterisk/stringfields.h"
#include "asterisk/time.h"
#include "asterisk/causes.h"
#include "asterisk/pbx.h"
#include "asterisk/format_cache.h"

#if defined(TEST_FRAMEWORK)

#define TEST_CATEGORY "/res/parking/"

#define CHANNEL_TECH_NAME "ParkingTestChannel"

static const struct ast_party_caller alice_callerid = {
	.id.name.str = "Alice",
	.id.name.valid = 1,
	.id.number.str = "100",
	.id.number.valid = 1,
};

static int parking_test_write(struct ast_channel *chan, struct ast_frame *frame)
{
	return 0;
}

static struct ast_frame *parking_test_read(struct ast_channel *chan)
{
	return &ast_null_frame;
}

static const struct ast_channel_tech parking_test_tech = {
	.type = CHANNEL_TECH_NAME,
	.description = "Parking unit test technology",
	.write = parking_test_write,
	.read = parking_test_read,
};

/*! \brief Set ulaw format on the channel */
static int set_test_formats(struct ast_channel *chan)
{
	struct ast_format_cap *caps;

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		return -1;
	}

	ast_format_cap_append(caps, ast_format_ulaw, 0);
	ast_channel_nativeformats_set(chan, caps);
	ast_channel_set_writeformat(chan, ast_format_ulaw);
	ast_channel_set_rawwriteformat(chan, ast_format_ulaw);
	ast_channel_set_readformat(chan, ast_format_ulaw);
	ast_channel_set_rawreadformat(chan, ast_format_ulaw);
	ao2_ref(caps, -1);

	return 0;
}

/*! \brief Create a \ref test_cdr_chan_tech for Alice */
static struct ast_channel *create_alice_channel(void)
{
	struct ast_channel *alice = ast_channel_alloc(0, AST_STATE_DOWN,
		"100", "Alice", "100", "100", "default", NULL, NULL, 0,
		CHANNEL_TECH_NAME "/Alice");

	if (!alice) {
		return NULL;
	}

	if (set_test_formats(alice)) {
		ast_channel_unlock(alice);
		ast_channel_release(alice);
		return NULL;
	}

	ast_channel_tech_set(alice, &parking_test_tech);

	ast_channel_set_caller(alice, &alice_callerid, NULL);

	ast_channel_unlock(alice);

	return alice;
}

/*! \brief Hang up a test channel safely */
static struct ast_channel *hangup_channel(struct ast_channel *chan, int hangup_cause)
{
	ast_channel_hangupcause_set(chan, hangup_cause);
	ast_hangup(chan);
	return NULL;
}

static void safe_channel_release(struct ast_channel *chan)
{
	if (!chan) {
		return;
	}
	ast_channel_release(chan);
}

static void do_sleep(struct timespec *to_sleep)
{
	while ((nanosleep(to_sleep, to_sleep) == -1) && (errno == EINTR)) {
	}
}

#define TEST_LOT_NAME "unit_tests_res_parking_test_lot"

static struct parking_lot *generate_test_parking_lot(const char *name, int low_space, int high_space, const char *park_exten, const char *park_context, struct ast_test *test)
{
	RAII_VAR(struct parking_lot_cfg *, test_cfg, NULL, ao2_cleanup);
	struct parking_lot *test_lot;

	test_cfg = parking_lot_cfg_create(name);
	if (!test_cfg) {
		return NULL;
	}

	test_cfg->parking_start = low_space;
	test_cfg->parking_stop = high_space;
	test_cfg->parkingtime = 10;
	test_cfg->comebackdialtime = 10;
	test_cfg->parkfindnext = 1;
	test_cfg->parkext_exclusive = 1;
	ast_string_field_set(test_cfg, parkext, park_exten);
	ast_string_field_set(test_cfg, parking_con, park_context);
	ast_string_field_set(test_cfg, comebackcontext, "unit_test_res_parking_create_lot_comeback");

	if (parking_lot_cfg_create_extensions(test_cfg)) {
		ast_test_status_update(test, "Extensions for parking lot '%s' could not be registered. Extension Creation failed.\n", name);
		return NULL;
	}

	test_lot = parking_lot_build_or_update(test_cfg, 1);
	if (!test_lot) {
		return NULL;
	}

	return test_lot;
}

static int dispose_test_lot(struct parking_lot *test_lot, int expect_destruction)
{
	RAII_VAR(struct parking_lot *, found_lot, NULL, ao2_cleanup);

	test_lot->mode = PARKINGLOT_DISABLED;
	parking_lot_remove_if_unused(test_lot);

	found_lot = parking_lot_find_by_name(test_lot->name);

	if ((expect_destruction && !found_lot) || (!expect_destruction && found_lot)) {
		return 0;
	}

	return -1;
}

AST_TEST_DEFINE(create_lot)
{
	RAII_VAR(struct parking_lot *, test_lot, NULL, ao2_cleanup);
	RAII_VAR(struct parking_lot *, found_copy, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "create_lot";
		info->category = TEST_CATEGORY;
		info->summary = "Parking lot creation";
		info->description =
			"Creates a parking lot and then disposes of it.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating test parking lot '%s'\n", TEST_LOT_NAME);

	test_lot = generate_test_parking_lot(TEST_LOT_NAME, 701, 703, NULL, "unit_test_res_parking_create_lot_con", test);
	if (!test_lot) {
		ast_test_status_update(test, "Failed to create test parking lot. Test Failed\n");
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Successfully created parking lot. Retrieving test parking lot from container.\n");

	found_copy = parking_lot_find_by_name(TEST_LOT_NAME);
	if (!found_copy) {
		ast_test_status_update(test, "Failed to find parking lot in the parking lot container. Test failed.\n");
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Successfully retrieved parking lot. Removing test parking lot from container.\n");

	if (dispose_test_lot(found_copy, 1)) {
		ast_test_status_update(test, "Found parking lot in container after attempted removal. Test failed.\n");
	}

	ast_test_status_update(test, "Parking lot was successfully removed from the container. Test complete.\n");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(park_call)
{
	RAII_VAR(struct parking_lot *, test_lot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, parking_bridge, NULL, ao2_cleanup);

	struct timespec to_sleep = {1, 0};

	switch (cmd) {
	case TEST_INIT:
		info->name = "park_channel";
		info->category = TEST_CATEGORY;
		info->summary = "Park a Channel";
		info->description =
			"Creates a parking lot, parks a channel in it, then removes it from the parking lot bridge.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating test parking lot '%s'\n", TEST_LOT_NAME);

	test_lot = generate_test_parking_lot(TEST_LOT_NAME, 701, 703, NULL, "unit_test_res_parking_create_lot_con", test);
	if (!test_lot) {
		ast_test_status_update(test, "Failed to create test parking lot. Test failed.\n");
		return AST_TEST_FAIL;
	}

	chan_alice = create_alice_channel();
	if (!chan_alice) {
		ast_test_status_update(test, "Failed to create test channel to park. Test failed.\n");
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	ast_channel_state_set(chan_alice, AST_STATE_UP);
	pbx_builtin_setvar_helper(chan_alice, "BLINDTRANSFER", ast_channel_name(chan_alice));

	parking_bridge = park_application_setup(chan_alice, chan_alice, TEST_LOT_NAME, NULL);
	if (!parking_bridge) {
		ast_test_status_update(test, "Failed to get the parking bridge for '%s'. Test failed.\n", TEST_LOT_NAME);
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	if (ast_bridge_impart(parking_bridge, chan_alice, NULL, NULL,
		AST_BRIDGE_IMPART_CHAN_DEPARTABLE)) {
		ast_test_status_update(test, "Failed to impart alice into parking lot. Test failed.\n");
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	do_sleep(&to_sleep);

	ast_bridge_depart(chan_alice);

	chan_alice = hangup_channel(chan_alice, AST_CAUSE_NORMAL);

	if (dispose_test_lot(test_lot, 1)) {
		ast_test_status_update(test, "Found parking lot in container after attempted removal. Test failed.\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;

}

static int parked_users_match(const struct parked_user *actual, const struct parked_user *expected, struct ast_test *test)
{
	if (expected->parking_space != actual->parking_space) {
		ast_test_status_update(test, "parking_space expected: %d - got: %d\n", expected->parking_space, actual->parking_space);
		return 0;
	}

	if (strcmp(expected->parker_dial_string, actual->parker_dial_string)) {
		ast_test_status_update(test, "parker_dial_string expected: %s - got: %s\n", expected->parker_dial_string, actual->parker_dial_string);
		return 0;
	}

	if (expected->time_limit != actual->time_limit) {
		ast_test_status_update(test, "time_limit expected: %u - got: %u\n", expected->time_limit, actual->time_limit);
		return 0;
	}

	if (expected->resolution != actual->resolution) {
		ast_test_status_update(test, "resolution expected: %u - got: %u\n", expected->resolution, actual->resolution);
		return 0;
	}

	return 1;
}

static int parking_lot_cfgs_match(const struct parking_lot_cfg *actual, const struct parking_lot_cfg *expected, struct ast_test *test)
{
	if (expected->parking_start != actual->parking_start) {
		ast_test_status_update(test, "parking_start expected: %d - got: %d\n", expected->parking_start, actual->parking_start);
		return 0;
	}

	if (expected->parking_stop != actual->parking_stop) {
		ast_test_status_update(test, "parking_stop expected: %d - got: %d\n", expected->parking_stop, actual->parking_stop);
		return 0;
	}

	if (expected->parkingtime != actual->parkingtime) {
		ast_test_status_update(test, "parkingtime expected: %u - got: %u\n", expected->parkingtime, actual->parkingtime);
		return 0;
	}

	if (expected->comebackdialtime != actual->comebackdialtime) {
		ast_test_status_update(test, "comebackdialtime expected: %u - got: %u\n", expected->comebackdialtime, actual->comebackdialtime);
		return 0;
	}

	if (expected->parkfindnext != actual->parkfindnext) {
		ast_test_status_update(test, "parkfindnext expected: %u - got: %u\n", expected->parkfindnext, actual->parkfindnext);
		return 0;
	}

	if (expected->parkext_exclusive != actual->parkext_exclusive) {
		ast_test_status_update(test, "parkext_exclusive expected: %u - got: %u\n", expected->parkext_exclusive, actual->parkext_exclusive);
		return 0;
	}

	if (strcmp(expected->parkext, actual->parkext)) {
		ast_test_status_update(test, "parkext expected: %s - got: %s\n", expected->parkext, actual->parkext);
		return 0;
	}

	if (strcmp(expected->parking_con, actual->parking_con)) {
		ast_test_status_update(test, "parking_con expected: %s - got: %s\n", expected->parking_con, actual->parking_con);
		return 0;
	}

	if (strcmp(expected->comebackcontext, actual->comebackcontext)) {
		ast_test_status_update(test, "comebackcontext expected: %s - got: %s\n", expected->comebackcontext, actual->comebackcontext);
		return 0;
	}

	return 1;
}

AST_TEST_DEFINE(retrieve_call)
{
	RAII_VAR(struct parking_lot *, test_lot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct ast_bridge *, parking_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct parked_user *, retrieved_user, NULL, ao2_cleanup);

	struct timespec to_sleep = {1, 0};
	int failure = 0;

	static const struct parked_user expected_user = {
		.parking_space = 701,
		.parker_dial_string = "ParkingTestChannel/Alice",
		.time_limit = 10,
		.resolution = PARK_ANSWERED,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "park_retrieve";
		info->category = TEST_CATEGORY;
		info->summary = "Retrieve a parked channel";
		info->description =
			"Creates a parking lot, parks a channel in it, then removes it from the parking lot bridge.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating test parking lot '%s'\n", TEST_LOT_NAME);

	test_lot = generate_test_parking_lot(TEST_LOT_NAME, 701, 703, NULL, "unit_test_res_parking_create_lot_con", test);
	if (!test_lot) {
		ast_test_status_update(test, "Failed to create test parking lot. Test failed.\n");
		return AST_TEST_FAIL;
	}

	chan_alice = create_alice_channel();
	if (!chan_alice) {
		ast_test_status_update(test, "Failed to create test channel to park. Test failed.\n");
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	ast_channel_state_set(chan_alice, AST_STATE_UP);
	pbx_builtin_setvar_helper(chan_alice, "BLINDTRANSFER", ast_channel_name(chan_alice));

	parking_bridge = park_application_setup(chan_alice, chan_alice, TEST_LOT_NAME, NULL);
	if (!parking_bridge) {
		ast_test_status_update(test, "Failed to get the parking bridge for '%s'. Test failed.\n", TEST_LOT_NAME);
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	if (ast_bridge_impart(parking_bridge, chan_alice, NULL, NULL,
		AST_BRIDGE_IMPART_CHAN_DEPARTABLE)) {
		ast_test_status_update(test, "Failed to impart alice into parking lot. Test failed.\n");
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	do_sleep(&to_sleep);

	retrieved_user = parking_lot_retrieve_parked_user(test_lot, 701);
	if (!retrieved_user) {
		ast_test_status_update(test, "Failed to retrieve the parked user from the expected parking space. Test failed.\n");
		failure = 1;
		goto test_cleanup;
	}

	ast_test_status_update(test, "Successfully retrieved parked user from the parking lot. Validating user data.\n");

	if (!parked_users_match(retrieved_user, &expected_user, test)) {
		ast_test_status_update(test, "Parked user validation failed\n");
		failure = 1;
		goto test_cleanup;
	}

	if (retrieved_user->chan != chan_alice) {
		ast_test_status_update(test, "The retrieved parked channel didn't match the expected channel. Test failed.\n");
		failure = 1;
		goto test_cleanup;
	}

test_cleanup:
	ast_bridge_depart(chan_alice);
	chan_alice = hangup_channel(chan_alice, AST_CAUSE_NORMAL);
	if (dispose_test_lot(test_lot, 1)) {
		ast_test_status_update(test, "Found parking lot in container after attempted removal. Test failed.\n");
		failure = 1;
	}

	return failure ? AST_TEST_FAIL : AST_TEST_PASS;
}

static int check_retrieve_call_extensions(struct ast_test *test, int expected)
{
	struct ast_exten *check;
	struct pbx_find_info find_info = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	int extens;
	char search_buffer[4];

	/* Check the parking extensions */
	check = pbx_find_extension(NULL, NULL, &find_info, "unit_test_res_parking_create_lot_con", "700", 1, NULL, NULL, E_MATCH);

	if (check ? !expected : expected) {
		/* extension isn't present when it should be or is present when it shouldn't be. Automatic failure. */
		ast_test_status_update(test, "An extension '700' was %s when it %s have been. Test failed.\n",
			expected ? "not present" : "present",
			expected ? "should" : "should not");
		return -1;
	} else if (check && expected) {
		if (strcmp(ast_get_extension_app(check), "Park")) {
			ast_test_status_update(test, "An extension '700' has the wrong application associated with it. Got '%s' expected 'Park'.\n",
				ast_get_extension_app(check));
			return -1;
		}
	}


	/* Check the parking space extensions 701-703 */
	for (extens = 701; extens <= 703; extens++) {
		sprintf(search_buffer, "%d", extens);
		find_info.stacklen = 0; /* reset for pbx_find_extension */

		check = pbx_find_extension(NULL, NULL, &find_info, "unit_test_res_parking_create_lot_con", search_buffer, 1, NULL, NULL, E_MATCH);

		if (check ? !expected : expected) {
			/* extension isn't present when it should be or is present when it shouldn't be. Automatic failure. */
			ast_test_status_update(test, "An extension '%s' was %s when it %s have been. Test failed.\n",
				search_buffer,
				expected ? "not present" : "present",
				expected ? "should" : "should not");
			return -1;
		} else if (check && expected) {
			if (strcmp(ast_get_extension_app(check), "ParkedCall")) {
				ast_test_status_update(test, "An extension '%s' has the wrong application associated with it. Got '%s', expected 'ParkedCall'.\n",
					search_buffer,
					ast_get_extension_app(check));
				return -1;
			}
		}
	}

	return 0;

}

AST_TEST_DEFINE(park_extensions)
{
	RAII_VAR(struct parking_lot *, test_lot, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "park_extensions";
		info->category = TEST_CATEGORY;
		info->summary = "Parking lot extension creation tests";
		info->description =
			"Creates parking lots and checks that they registered the expected extensions, then removes them.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_lot = generate_test_parking_lot(TEST_LOT_NAME, 701, 703, "700", "unit_test_res_parking_create_lot_con", test);
	if (!test_lot) {
		ast_test_status_update(test, "Failed to create test parking lot. Test Failed.\n");
		return AST_TEST_FAIL;
	}

	if (check_retrieve_call_extensions(test, 1)) {
		dispose_test_lot(test_lot, 1);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Extensions for the test parking lot were verified. Cleaning up and verifying their removal.\n");

	if (dispose_test_lot(test_lot, 1)) {
		ast_test_status_update(test, "Found parking lot in container after attempted removal. Test failed.\n");
		return AST_TEST_FAIL;
	}
	ao2_cleanup(test_lot);
	test_lot = NULL;

	if (check_retrieve_call_extensions(test, 0)) {
		ast_log(LOG_ERROR, "Test 'park_extensions' failed to clean up after itself properly.\n");
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Extensions for the test parking lot verified as removed. Test completed successfully.\n");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(extension_conflicts)
{
	RAII_VAR(struct parking_lot *, base_lot, NULL, ao2_cleanup);
	RAII_VAR(struct parking_lot *, expect_fail1, NULL, ao2_cleanup); /* Failure due to overlapping parkexten */
	RAII_VAR(struct parking_lot *, expect_fail2, NULL, ao2_cleanup); /* Failure due to overlapping spaces */
	RAII_VAR(struct parking_lot *, expect_fail3, NULL, ao2_cleanup); /* parkexten overlaps parking spaces */
	RAII_VAR(struct parking_lot *, expect_fail4, NULL, ao2_cleanup); /* parking spaces overlap parkexten */
	RAII_VAR(struct parking_lot *, expect_success1, NULL, ao2_cleanup); /* Success due to being in a different context */
	RAII_VAR(struct parking_lot *, expect_success2, NULL, ao2_cleanup); /* Success due to not having overlapping extensions */
	RAII_VAR(struct parking_lot *, expect_success3, NULL, ao2_cleanup); /* Range of parking spaces differs by one above */
	RAII_VAR(struct parking_lot *, expect_success4, NULL, ao2_cleanup); /* Range of parking spaces differs by one below */
	char *cur_lot_name;

	int failed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "extension_conflicts";
		info->category = TEST_CATEGORY;
		info->summary = "Tests the addition of parking lot extensions to make sure conflicts are detected";
		info->description =
			"Creates parking lots with overlapping extensions to test for conflicts";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating the base lot. This should pass.\n");
	base_lot = generate_test_parking_lot(TEST_LOT_NAME, 701, 703, "700", "unit_test_res_parking_create_lot_con", test);

	if (!base_lot) {
		ast_test_status_update(test, "Failed to create the base parking lot. Test failed.\n");
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_fail1";
	ast_test_status_update(test, "Creating a test lot which will overlap.\n");
	expect_fail1 = generate_test_parking_lot(cur_lot_name,
		801, 803, "700", "unit_test_res_parking_create_lot_con", /* The parkexten overlaps the parkexten of the base */
		test);

	if (expect_fail1) {
		ast_test_status_update(test, "%s was successfully created when it was expected to fail. Test failed.\n", cur_lot_name);
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_fail2";
	expect_fail2 = generate_test_parking_lot(cur_lot_name,
		702, 705, "800", "unit_test_res_parking_create_lot_con", /* The range overlaps the range of the base */
		test);
	if (expect_fail2) {
		ast_test_status_update(test, "%s was successfully created when it was expected to fail. Test failed.\n", cur_lot_name);
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_fail3";
	expect_fail3 = generate_test_parking_lot(cur_lot_name,
		698, 700, "testfail3", "unit_test_res_parking_create_lot_con", /* The range overlaps the parkexten of the base */
		test);
	if (expect_fail3) {
		ast_test_status_update(test, "%s was successfully created when it was expected to fail. Test failed.\n", cur_lot_name);
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_fail4";
	expect_fail4 = generate_test_parking_lot(cur_lot_name,
		704, 706, "703", "unit_test_res_parking_create_lot_con", /* The parkexten overlaps the range of the base */
		test);
	if (expect_fail4) {
		ast_test_status_update(test, "%s was successfully created when it was expected to fail. Test failed.\n", cur_lot_name);
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_success1";
	expect_success1 = generate_test_parking_lot(cur_lot_name,
		701, 703, "700", "unit_test_res_parking_create_lot_con_2", /* no overlap due to different context */
		test);
	if (!expect_success1) {
		ast_test_status_update(test, "%s failed to be created. Success was expected. Test failed.\n", cur_lot_name);
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_success2";
	expect_success2 = generate_test_parking_lot(cur_lot_name,
		601, 605, "600", "unit_test_res_parking_create_lot_con", /* no overlap due to different extensions and ranges */
		test);
	if (!expect_success2) {
		ast_test_status_update(test, "%s failed to be created. Success was expected. Test failed.\n", cur_lot_name);
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_success3";
	expect_success3 = generate_test_parking_lot(cur_lot_name,
		704, 706, "testsuccess3", "unit_test_res_parking_create_lot_con", /* no overlap because the parking spaces start 1 above existing ranges */
		test);
	if (!expect_success3) {
		ast_test_status_update(test, "%s failed to be created. Success was expected. Test failed.\n", cur_lot_name);
		failed = 1;
		goto cleanup;
	}

	cur_lot_name = "unit_tests_res_parking_test_lot_success4";
	expect_success4 = generate_test_parking_lot(cur_lot_name,
		697, 699, "testsuccess4", "unit_test_res_parking_create_lot_con", /* no overlap because the parking spaces end 1 below existing ranges */
		test);
	if (!expect_success4) {
		failed = 1;
		goto cleanup;
	}

cleanup:
	if (base_lot && dispose_test_lot(base_lot, 1)) {
		ast_test_status_update(test, "Found base parking lot in container after attempted removal. Test failed.\n");
		failed = 1;
	}

	if (expect_fail1) {
		dispose_test_lot(expect_fail1, 1);
		failed = 1;
	}

	if (expect_fail2) {
		dispose_test_lot(expect_fail2, 1);
		failed = 1;
	}

	if (expect_fail3) {
		dispose_test_lot(expect_fail3, 1);
		failed = 1;
	}

	if (expect_fail4) {
		dispose_test_lot(expect_fail4, 1);
		failed = 1;
	}

	if (expect_success1 && dispose_test_lot(expect_success1, 1)) {
		ast_test_status_update(test, "Found expect_success1 parking lot in container after attempted removal. Test failed.\n");
		failed = 1;
	}

	if (expect_success2 && dispose_test_lot(expect_success2, 1)) {
		ast_test_status_update(test, "Found expect_success2 parking lot in container after attempted removal. Test failed.\n");
		failed = 1;
	}

	if (expect_success3 && dispose_test_lot(expect_success3, 1)) {
		ast_test_status_update(test, "Found expect_success3 parking lot in container after attempted removal. Test failed.\n");
		failed = 1;
	}

	if (expect_success4 && dispose_test_lot(expect_success4, 1)) {
		ast_test_status_update(test, "Found expect_success4 parking lot in container after attempted removal. Test failed.\n");
	}

	return failed ? AST_TEST_FAIL : AST_TEST_PASS;
}

AST_TEST_DEFINE(dynamic_parking_variables)
{
	RAII_VAR(struct parking_lot *, template_lot, NULL, ao2_cleanup);
	RAII_VAR(struct parking_lot *, dynamic_lot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan_alice, NULL, safe_channel_release);
	RAII_VAR(struct parking_lot_cfg *, expected_cfg, NULL, ao2_cleanup);

	int failed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "dynamic_parking_variables";
		info->category = TEST_CATEGORY;
		info->summary = "Tests whether dynamic parking lot creation respects channel variables";
		info->description =
			"Creates a template parking lot, creates a channel, sets dynamic parking variables, and then creates a parking lot for that channel";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Creating expected configuration for dynamic parking lot\n");

	expected_cfg = parking_lot_cfg_create("unit_tests_res_parking_test_lot_dynamic");

	if (!expected_cfg) {
		ast_test_status_update(test, "Failed to create expected configuration. Test failed.\n");
		return AST_TEST_FAIL;
	}

	expected_cfg->parking_start = 751;
	expected_cfg->parking_stop = 760;
	expected_cfg->parkingtime = 10;
	expected_cfg->comebackdialtime = 10;
	expected_cfg->parkfindnext = 1;
	expected_cfg->parkext_exclusive = 1;
	ast_string_field_set(expected_cfg, parkext, "750");
	ast_string_field_set(expected_cfg, parking_con, "unit_test_res_parking_create_lot_dynamic");
	ast_string_field_set(expected_cfg, comebackcontext, "unit_test_res_parking_create_lot_comeback");

	ast_test_status_update(test, "Creating template lot\n");

	template_lot = generate_test_parking_lot(TEST_LOT_NAME, 701, 703, "700", "unit_test_res_parking_create_lot_con", test);

	if (!template_lot) {
		ast_test_status_update(test, "Failed to generate template lot. Test failed.\n");
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Creating Alice channel to test dynamic parking lot creation.\n");

	chan_alice = create_alice_channel();

	if (!chan_alice) {
		ast_test_status_update(test, "Failed to create Alice channel. Test failed.\n");
		failed = 1;
		goto cleanup;
	}

	ast_test_status_update(test, "Setting Dynamic Parking channel variables on Alice.\n");

	pbx_builtin_setvar_helper(chan_alice, "PARKINGDYNAMIC", TEST_LOT_NAME);
	pbx_builtin_setvar_helper(chan_alice, "PARKINGLOT", "unit_test_res_parking_create_lot_dynamic");
	pbx_builtin_setvar_helper(chan_alice, "PARKINGDYNCONTEXT", "unit_test_res_parking_create_lot_dynamic");
	pbx_builtin_setvar_helper(chan_alice, "PARKINGDYNEXTEN", "750");
	pbx_builtin_setvar_helper(chan_alice, "PARKINGDYNPOS", "751-760");

	ast_test_status_update(test, "Generating dynamic parking lot based on Alice's channel variables.\n");

	dynamic_lot = parking_create_dynamic_lot_forced("unit_tests_res_parking_test_lot_dynamic", chan_alice);

	if (!dynamic_lot) {
		ast_test_status_update(test, "Failed to create dynamic parking lot. Test failed.\n");
		failed = 1;
		goto cleanup;
	}

	/* Check stats */
	if (!parking_lot_cfgs_match(dynamic_lot->cfg, expected_cfg, test)) {
		ast_test_status_update(test, "Dynamic parking lot configuration did not match Expectations.\n");
		failed = 1;
		goto cleanup;
	}

	ast_test_status_update(test, "Dynamic parking lot created successfully and matches expectations. Test passed.\n");

cleanup:
	if (template_lot && dispose_test_lot(template_lot, 1)) {
		ast_test_status_update(test, "Found template parking lot in container after attempted removal. Test failed.\n");
		failed = 1;
	}

	if (dynamic_lot && dispose_test_lot(dynamic_lot, 1)) {
		ast_test_status_update(test, "Found dynamic parking lot in container after attempted removal. Test failed.\n");
		failed = 1;
	}

	return failed ? AST_TEST_FAIL : AST_TEST_PASS;
}

#endif /* TEST_FRAMEWORK */


void unload_parking_tests(void)
{
/* NOOP without test framework */
#if defined(TEST_FRAMEWORK)
	AST_TEST_UNREGISTER(create_lot);
	AST_TEST_UNREGISTER(park_call);
	AST_TEST_UNREGISTER(retrieve_call);
	AST_TEST_UNREGISTER(park_extensions);
	AST_TEST_UNREGISTER(extension_conflicts);
	AST_TEST_UNREGISTER(dynamic_parking_variables);
#endif
}

int load_parking_tests(void)
{
	int res = 0;

/* NOOP without test framework */
#if defined(TEST_FRAMEWORK)
	res |= AST_TEST_REGISTER(create_lot);
	res |= AST_TEST_REGISTER(park_call);
	res |= AST_TEST_REGISTER(retrieve_call);
	res |= AST_TEST_REGISTER(park_extensions);
	res |= AST_TEST_REGISTER(extension_conflicts);
	res |= AST_TEST_REGISTER(dynamic_parking_variables);
#endif

	return res;
}
