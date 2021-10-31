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
 * \brief Generic AOC encode decode tests
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

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/aoc.h"


AST_TEST_DEFINE(aoc_event_generation_test)
{
	int res = AST_TEST_PASS;
	struct ast_aoc_decoded *decoded = NULL;
	struct ast_str *msg = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "aoc_event_test";
		info->category = "/main/aoc/";
		info->summary = "Advice of Charge event generation test";
		info->description =
			"Creates AOC messages, verify event string matches expected results";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(msg = ast_str_create(1024))) {
		goto cleanup_aoc_event_test;
	}

	/* ---- TEST 1, AOC-D event generation */
	if (!(decoded = ast_aoc_create(AST_AOC_D, AST_AOC_CHARGE_CURRENCY, 0))) {

		ast_test_status_update(test, "failed to create AOC-D message for event generation.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}
	/* Add billing id information */
	ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CREDIT_CARD);

	/* Set currency information, verify results */
	if ((ast_aoc_set_currency_info(decoded, 100, AST_AOC_MULT_ONE, "usd")) ||
		(ast_aoc_set_total_type(decoded, AST_AOC_SUBTOTAL))) {

		ast_test_status_update(test, "failed to set currency info in AOC-D msg\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}
	if (ast_aoc_decoded2str(decoded, &msg)) {

		ast_test_status_update(test, "failed to generate AOC-D msg string.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}
	if (strncmp(ast_str_buffer(msg),
		"AOC-D\r\n"
		"Type: Currency\r\n"
		"BillingID: CreditCard\r\n"
		"TypeOfCharging: SubTotal\r\n"
		"Currency: usd\r\n"
		"Currency/Amount/Cost: 100\r\n"
		"Currency/Amount/Multiplier: 1\r\n",
		strlen(ast_str_buffer(msg)))) {

		ast_test_status_update(test, "AOC-D msg event did not match expected results\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}
	decoded = ast_aoc_destroy_decoded(decoded);
	ast_str_reset(msg);


	/* ---- TEST 2, AOC-S event generation */
	if (!(decoded = ast_aoc_create(AST_AOC_S, 0, 0))) {
		ast_test_status_update(test, "failed to create AOC-S message for event generation.\n");

		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}

	ast_aoc_s_add_rate_flat(decoded,
		AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION,
		123,
		AST_AOC_MULT_TEN,
		"pineapple");

	ast_aoc_s_add_rate_volume(decoded,
		AST_AOC_CHARGED_ITEM_CALL_ATTEMPT,
		AST_AOC_VOLUME_UNIT_SEGMENT,
		937,
		AST_AOC_MULT_ONEHUNDREDTH,
		"oranges");

	ast_aoc_s_add_rate_duration(decoded,
		AST_AOC_CHARGED_ITEM_CALL_ATTEMPT,
		937,
		AST_AOC_MULT_ONETHOUSANDTH,
		"bananas",
		848,
		AST_AOC_TIME_SCALE_TENTH_SECOND,
		949,
		AST_AOC_TIME_SCALE_HOUR,
		1);

	ast_aoc_s_add_rate_duration(decoded,
		AST_AOC_CHARGED_ITEM_USER_USER_INFO,
		937,
		AST_AOC_MULT_THOUSAND,
		"bananas",
		1111,
		AST_AOC_TIME_SCALE_SECOND,
		2222,
		AST_AOC_TIME_SCALE_DAY,
		0);

	if (ast_aoc_decoded2str(decoded, &msg)) {

		ast_test_status_update(test, "failed to generate AOC-D msg string.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}


	if (strncmp(ast_str_buffer(msg),
		"AOC-S\r\n"
		"NumberRates: 4\r\n"
		"Rate(0)/Chargeable: BasicCommunication\r\n"
		"Rate(0)/Type: Flat\r\n"
		"Rate(0)/Flat/Currency: pineapple\r\n"
		"Rate(0)/Flat/Amount/Cost: 123\r\n"
		"Rate(0)/Flat/Amount/Multiplier: 10\r\n"
		"Rate(1)/Chargeable: CallAttempt\r\n"
		"Rate(1)/Type: Volume\r\n"
		"Rate(1)/Volume/Currency: oranges\r\n"
		"Rate(1)/Volume/Amount/Cost: 937\r\n"
		"Rate(1)/Volume/Amount/Multiplier: 1/100\r\n"
		"Rate(1)/Volume/Unit: Segment\r\n"
		"Rate(2)/Chargeable: CallAttempt\r\n"
		"Rate(2)/Type: Duration\r\n"
		"Rate(2)/Duration/Currency: bananas\r\n"
		"Rate(2)/Duration/Amount/Cost: 937\r\n"
		"Rate(2)/Duration/Amount/Multiplier: 1/1000\r\n"
		"Rate(2)/Duration/ChargingType: StepFunction\r\n"
		"Rate(2)/Duration/Time/Length: 848\r\n"
		"Rate(2)/Duration/Time/Scale: OneTenthSecond\r\n"
		"Rate(2)/Duration/Granularity/Length: 949\r\n"
		"Rate(2)/Duration/Granularity/Scale: Hour\r\n"
		"Rate(3)/Chargeable: UserUserInfo\r\n"
		"Rate(3)/Type: Duration\r\n"
		"Rate(3)/Duration/Currency: bananas\r\n"
		"Rate(3)/Duration/Amount/Cost: 937\r\n"
		"Rate(3)/Duration/Amount/Multiplier: 1000\r\n"
		"Rate(3)/Duration/ChargingType: ContinuousCharging\r\n"
		"Rate(3)/Duration/Time/Length: 1111\r\n"
		"Rate(3)/Duration/Time/Scale: Second\r\n"
		"Rate(3)/Duration/Granularity/Length: 2222\r\n"
		"Rate(3)/Duration/Granularity/Scale: Day\r\n",
		strlen(ast_str_buffer(msg)))) {

		ast_test_status_update(test, "AOC-S msg event did not match expected results\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}
	decoded = ast_aoc_destroy_decoded(decoded);
	ast_str_reset(msg);

	/* ---- TEST 3, AOC-E event generation with various charging association information*/
	if (!(decoded = ast_aoc_create(AST_AOC_E, AST_AOC_CHARGE_UNIT, 0))) {
		ast_test_status_update(test, "failed to create AOC-E message for event generation.\n");

		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}
	if ((ast_aoc_add_unit_entry(decoded, 1, 111, 1, 1)) ||
		(!ast_aoc_add_unit_entry(decoded, 0, 2222, 0, 2)) || /* we expect this to fail, and it should not be added to entry list */
		(ast_aoc_add_unit_entry(decoded, 1, 3333, 0, 3)) ||
		(ast_aoc_add_unit_entry(decoded, 0, 44444, 1, 4))) {

		ast_test_status_update(test, "failed to set unit info for AOC-E message\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}

	if (ast_aoc_decoded2str(decoded, &msg)) {
		ast_test_status_update(test, "failed to generate AOC-E msg string.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}

	if (strncmp(ast_str_buffer(msg),
		"AOC-E\r\n"
		"Type: Units\r\n"
		"BillingID: NotAvailable\r\n"
		"Units/NumberItems: 3\r\n"
		"Units/Item(0)/NumberOf: 111\r\n"
		"Units/Item(0)/TypeOf: 1\r\n"
		"Units/Item(1)/NumberOf: 3333\r\n"
		"Units/Item(2)/TypeOf: 4\r\n",
		strlen(ast_str_buffer(msg)))) {

		ast_test_status_update(test, "AOC-E msg event did not match expected results, with no charging association info\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}

	/* add AOC-E charging association number info */
	if (ast_aoc_set_association_number(decoded, "555-555-5555", 16)) {
			ast_test_status_update(test, "failed to set the charging association number info correctly, 3\n");
			res = AST_TEST_FAIL;
			goto cleanup_aoc_event_test;
	}

	ast_str_reset(msg);
	if (ast_aoc_decoded2str(decoded, &msg)) {
		ast_test_status_update(test, "failed to generate AOC-E msg string.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}

	if (strncmp(ast_str_buffer(msg),
		"AOC-E\r\n"
		"ChargingAssociation/Number: 555-555-5555\r\n"
		"ChargingAssociation/Number/Plan: 16\r\n"
		"Type: Units\r\n"
		"BillingID: NotAvailable\r\n"
		"Units/NumberItems: 3\r\n"
		"Units/Item(0)/NumberOf: 111\r\n"
		"Units/Item(0)/TypeOf: 1\r\n"
		"Units/Item(1)/NumberOf: 3333\r\n"
		"Units/Item(2)/TypeOf: 4\r\n",
		strlen(ast_str_buffer(msg)))) {

		ast_test_status_update(test, "AOC-E msg event did not match expected results, with charging association number\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}

	/* add AOC-E charging association id info */
	if (ast_aoc_set_association_id(decoded, 2222)) {
			ast_test_status_update(test, "failed to set the charging association number info correctly, 3\n");
			res = AST_TEST_FAIL;
			goto cleanup_aoc_event_test;
	}

	ast_str_reset(msg);
	if (ast_aoc_decoded2str(decoded, &msg)) {
		ast_test_status_update(test, "failed to generate AOC-E msg string.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}

	if (strncmp(ast_str_buffer(msg),
		"AOC-E\r\n"
		"ChargingAssociation/ID: 2222\r\n"
		"Type: Units\r\n"
		"BillingID: NotAvailable\r\n"
		"Units/NumberItems: 3\r\n"
		"Units/Item(0)/NumberOf: 111\r\n"
		"Units/Item(0)/TypeOf: 1\r\n"
		"Units/Item(1)/NumberOf: 3333\r\n"
		"Units/Item(2)/TypeOf: 4\r\n",
		strlen(ast_str_buffer(msg)))) {

		ast_test_status_update(test, "AOC-E msg event did not match expected results with charging association id.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_event_test;
	}


cleanup_aoc_event_test:

	decoded = ast_aoc_destroy_decoded(decoded);
	ast_free(msg);
	return res;
}

AST_TEST_DEFINE(aoc_encode_decode_test)
{
	int res = AST_TEST_PASS;
	struct ast_aoc_decoded *decoded;

	switch (cmd) {
	case TEST_INIT:
		info->name = "aoc_encode_decode_test";
		info->category = "/main/aoc/";
		info->summary = "Advice of Charge encode and decode test";
		info->description =
			"This tests the Advice of Charge encode and decode routines.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* ---- Test 1 ---- create AOC-D message, encode message, and decode message once again. */
	/* create AOC-D message */
	if (!(decoded = ast_aoc_create(AST_AOC_D, AST_AOC_CHARGE_CURRENCY, 0)) ||
		(ast_aoc_get_msg_type(decoded) != AST_AOC_D) ||
		(ast_aoc_get_charge_type(decoded) != AST_AOC_CHARGE_CURRENCY)) {

		ast_test_status_update(test, "Test 1: failed to create AOC-D message\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}

	/* Add billing id information */
	if ((ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_NORMAL) ||
		(ast_aoc_get_billing_id(decoded) != AST_AOC_BILLING_NORMAL))) {

		ast_test_status_update(test, "TEST 1, could not set billing id correctly\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;

	}

	/* Set currency information, verify results*/
	if ((ast_aoc_set_currency_info(decoded, 100, AST_AOC_MULT_ONE, "usd")) ||
		(ast_aoc_set_total_type(decoded, AST_AOC_SUBTOTAL)) ||
		(ast_aoc_get_total_type(decoded) != AST_AOC_SUBTOTAL) ||
		(ast_aoc_get_currency_amount(decoded) != 100) ||
		(ast_aoc_get_currency_multiplier(decoded) != AST_AOC_MULT_ONE) ||
		(strcmp(ast_aoc_get_currency_name(decoded), "usd"))) {

		ast_test_status_update(test, "Test 1: failed to set currency info\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}

	/* Set a currency name larger than 10 characters which is the maximum
	 * length allowed by the ETSI aoc standard.  The name is expected to truncate
	 * to 10 characters. */
	if ((ast_aoc_set_currency_info(decoded, 100, AST_AOC_MULT_ONE, "12345678901234567890")) ||
		(ast_aoc_get_currency_amount(decoded) != 100) ||
		(ast_aoc_get_currency_multiplier(decoded) != AST_AOC_MULT_ONE) ||
		(strcmp(ast_aoc_get_currency_name(decoded), "1234567890"))) {

		ast_test_status_update(test, "Test 1: failed to set currency info currency name exceeding limit\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}

	/* Encode the message */
	if (ast_aoc_test_encode_decode_match(decoded)) {
		ast_test_status_update(test, "Test1: encode decode routine did not match expected results \n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	/* cleanup decoded msg */
	decoded = ast_aoc_destroy_decoded(decoded);

	/* ---- Test 2 ---- create AOC-E message with charge type == unit */
	/* create AOC-E message */
	if (!(decoded = ast_aoc_create(AST_AOC_E, AST_AOC_CHARGE_UNIT, 0)) ||
		(ast_aoc_get_msg_type(decoded) != AST_AOC_E) ||
		(ast_aoc_get_charge_type(decoded) != AST_AOC_CHARGE_UNIT)) {

		ast_test_status_update(test, "Test 2: failed to create AOC-E message\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}

	/* Set unit information, verify results*/
	if ((ast_aoc_add_unit_entry(decoded, 1, 1, 1, 2)) ||
		(!ast_aoc_add_unit_entry(decoded, 0, 123, 0, 123)) || /* this entry should fail since either number nor type are present */
		(ast_aoc_add_unit_entry(decoded, 0, 2, 1, 3)) ||
		(ast_aoc_add_unit_entry(decoded, 1, 3, 0, 4))) {

		ast_test_status_update(test, "Test 2: failed to set unit info\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}

	/* verify unit list is correct */
	if (ast_aoc_get_unit_count(decoded) == 3) {
		int i;
		const struct ast_aoc_unit_entry *unit;
		for (i = 0; i < 3; i++) {
			if (!(unit = ast_aoc_get_unit_info(decoded, i)) ||
				((unit->valid_amount) && (unit->amount != (i+1))) ||
				((unit->valid_type) && (unit->type != (i+2)))) {
				ast_test_status_update(test, "TEST 2, invalid unit entry result, got %u,%u, expected %d,%d\n",
					unit->amount,
					unit->type,
					i+1,
					i+2);
				res = AST_TEST_FAIL;
				goto cleanup_aoc_test;
			}
		}
	} else {
		ast_test_status_update(test, "TEST 2, invalid unit list entry count \n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}


	/* Test charging association information */
	{
		const struct ast_aoc_charging_association *ca;
		if ((ast_aoc_set_association_id(decoded, 1234)) ||
		   (!(ca = ast_aoc_get_association_info(decoded)))) {
			ast_test_status_update(test, "TEST 2, could not set charging association id info correctly\n");
			res = AST_TEST_FAIL;
			goto cleanup_aoc_test;
		}

		if ((ca->charging_type != AST_AOC_CHARGING_ASSOCIATION_ID) || (ca->charge.id != 1234)) {
			ast_test_status_update(test, "TEST 2, could not get charging association id info correctly, 2\n");
		}

		if ((ast_aoc_set_association_number(decoded, "1234", 16)) ||
		   (!(ca = ast_aoc_get_association_info(decoded)))) {
			ast_test_status_update(test, "TEST 2, could not set charging association number info correctly, 3\n");
			res = AST_TEST_FAIL;
			goto cleanup_aoc_test;
		}

		if ((ca->charging_type != AST_AOC_CHARGING_ASSOCIATION_NUMBER) ||
			(ca->charge.number.plan != 16) ||
			(strcmp(ca->charge.number.number, "1234"))) {
			ast_test_status_update(test, "TEST 2, could not get charging association number info correctly\n");
		}
	}

	/* Test every billing id possibility */
	{
		int billid[9] = {
			AST_AOC_BILLING_NA,
			AST_AOC_BILLING_NORMAL,
			AST_AOC_BILLING_REVERSE_CHARGE,
			AST_AOC_BILLING_CREDIT_CARD,
			AST_AOC_BILLING_CALL_FWD_UNCONDITIONAL,
			AST_AOC_BILLING_CALL_FWD_BUSY,
			AST_AOC_BILLING_CALL_FWD_NO_REPLY,
			AST_AOC_BILLING_CALL_DEFLECTION,
			AST_AOC_BILLING_CALL_TRANSFER,
		};
		int i;

		/* these should fail */
		if (!(ast_aoc_set_billing_id(decoded, (AST_AOC_BILLING_NA - 1))) ||
			!(ast_aoc_set_billing_id(decoded, (AST_AOC_BILLING_CALL_TRANSFER + 1)))) {

				ast_test_status_update(test, "TEST 2, setting invalid billing id should fail\n");
				res = AST_TEST_FAIL;
				goto cleanup_aoc_test;
		}

		for (i = 0; i < ARRAY_LEN(billid); i++) {
			if ((ast_aoc_set_billing_id(decoded, billid[i]) ||
				(ast_aoc_get_billing_id(decoded) != billid[i]))) {

				ast_test_status_update(test, "TEST 2, could not set billing id correctly, iteration #%d\n", i);
				res = AST_TEST_FAIL;
				goto cleanup_aoc_test;
			}
		}
	}
	/* Encode the message */
	if (ast_aoc_test_encode_decode_match(decoded)) {
		ast_test_status_update(test, "Test2: encode decode routine did not match expected results \n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	/* cleanup decoded msg */
	decoded = ast_aoc_destroy_decoded(decoded);

	/* ---- Test 3 ---- create AOC-Request. test all possible combinations */
	{
		int request[7] = { /* all possible request combinations */
			AST_AOC_REQUEST_S,
			AST_AOC_REQUEST_D,
			AST_AOC_REQUEST_E,
			(AST_AOC_REQUEST_S | AST_AOC_REQUEST_D),
			(AST_AOC_REQUEST_S | AST_AOC_REQUEST_E),
			(AST_AOC_REQUEST_D | AST_AOC_REQUEST_E),
			(AST_AOC_REQUEST_D | AST_AOC_REQUEST_E | AST_AOC_REQUEST_S)
		};
		int i;

		for (i = 0; i < ARRAY_LEN(request); i++) {
			if (!(decoded = ast_aoc_create(AST_AOC_REQUEST, 0, request[i])) ||
				(ast_aoc_get_msg_type(decoded) != AST_AOC_REQUEST) ||
				(ast_aoc_get_termination_request(decoded)) ||
				(ast_aoc_get_request(decoded) != request[i])) {

				ast_test_status_update(test, "Test 3: failed to create AOC-Request message, iteration #%d\n", i);
				res = AST_TEST_FAIL;
				goto cleanup_aoc_test;
			}

			/* Encode the message */
			if (ast_aoc_test_encode_decode_match(decoded)) {
				ast_test_status_update(test, "Test3: encode decode routine did not match expected results, iteration #%d\n", i);
				res = AST_TEST_FAIL;
				goto cleanup_aoc_test;
			}
			/* cleanup decoded msg */
			decoded = ast_aoc_destroy_decoded(decoded);
		}


		/* Test termination Request Type */
		if (!(decoded = ast_aoc_create(AST_AOC_REQUEST, 0, AST_AOC_REQUEST_E)) ||
			(ast_aoc_set_termination_request(decoded)) ||
			(!ast_aoc_get_termination_request(decoded)) ||
			(ast_aoc_get_msg_type(decoded) != AST_AOC_REQUEST) ||
			(ast_aoc_get_request(decoded) != AST_AOC_REQUEST_E)) {

			ast_test_status_update(test, "Test 3: failed to create AOC-Request message with Termination Request set\n");
			res = AST_TEST_FAIL;
			goto cleanup_aoc_test;
		}

		/* Encode the message */
		if (ast_aoc_test_encode_decode_match(decoded)) {
			ast_test_status_update(test, "Test3: encode decode routine did not match expected results with termination request set\n");
			res = AST_TEST_FAIL;
			goto cleanup_aoc_test;
		}
		/* cleanup decoded msg */
		decoded = ast_aoc_destroy_decoded(decoded);
	}

	/* ---- Test 4 ----  Make stuff blow up */
	if ((decoded = ast_aoc_create(AST_AOC_D, 1234567, 0))) {

		ast_test_status_update(test, "Test 4: aoc-d creation with no valid charge type should fail\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	if ((decoded = ast_aoc_create(AST_AOC_REQUEST, 0, 0))) {

		ast_test_status_update(test, "Test 4: aoc request creation with no data should have failed\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	if ((decoded = ast_aoc_create(AST_AOC_REQUEST, -12345678, -23456789))) {

		ast_test_status_update(test, "Test 4: aoc request creation with random data should have failed\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}

	/* ---- Test 5 ---- create AOC-E message with charge type == FREE and charge type == NA */
	/* create AOC-E message */
	if (!(decoded = ast_aoc_create(AST_AOC_E, AST_AOC_CHARGE_FREE, 0)) ||
		(ast_aoc_get_msg_type(decoded) != AST_AOC_E) ||
		(ast_aoc_get_charge_type(decoded) != AST_AOC_CHARGE_FREE)) {

		ast_test_status_update(test, "Test 5: failed to create AOC-E message, charge type Free\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	if (ast_aoc_test_encode_decode_match(decoded)) {
		ast_test_status_update(test, "Test5: encode decode routine did not match expected results, charge type Free\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	/* cleanup decoded msg */
	decoded = ast_aoc_destroy_decoded(decoded);

	/* create AOC-E message */
	if (!(decoded = ast_aoc_create(AST_AOC_E, AST_AOC_CHARGE_NA, 0)) ||
		(ast_aoc_get_msg_type(decoded) != AST_AOC_E) ||
		(ast_aoc_get_charge_type(decoded) != AST_AOC_CHARGE_NA)) {

		ast_test_status_update(test, "Test 5: failed to create AOC-E message, charge type NA\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	if (ast_aoc_test_encode_decode_match(decoded)) {
		ast_test_status_update(test, "Test5: encode decode routine did not match expected results, charge type NA.\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	/* cleanup decoded msg */
	decoded = ast_aoc_destroy_decoded(decoded);


/* ---- TEST 6, AOC-S encode decode */
	if (!(decoded = ast_aoc_create(AST_AOC_S, 0, 0))) {
		ast_test_status_update(test, "failed to create AOC-S message for encode decode testing.\n");

		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}

	ast_aoc_s_add_rate_duration(decoded,
		AST_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE,
		937,
		AST_AOC_MULT_THOUSAND,
		"jkasdf",
		235328,
		AST_AOC_TIME_SCALE_SECOND,
		905423,
		AST_AOC_TIME_SCALE_DAY,
		1);

	ast_aoc_s_add_rate_flat(decoded,
		AST_AOC_CHARGED_ITEM_CALL_SETUP,
		1337,
		AST_AOC_MULT_ONEHUNDREDTH,
		"MONEYS");

	ast_aoc_s_add_rate_volume(decoded,
		AST_AOC_CHARGED_ITEM_CALL_ATTEMPT,
		AST_AOC_VOLUME_UNIT_SEGMENT,
		5555,
		AST_AOC_MULT_ONEHUNDREDTH,
		"pounds");

	ast_aoc_s_add_rate_duration(decoded,
		AST_AOC_CHARGED_ITEM_CALL_ATTEMPT,
		78923,
		AST_AOC_MULT_ONETHOUSANDTH,
		"SNAP",
		9354,
		AST_AOC_TIME_SCALE_HUNDREDTH_SECOND,
		234933,
		AST_AOC_TIME_SCALE_SECOND,
		0);

	ast_aoc_s_add_rate_free(decoded, AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT, 1);
	ast_aoc_s_add_rate_free(decoded, AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT, 0);
	ast_aoc_s_add_rate_na(decoded, AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT);

	if (ast_aoc_test_encode_decode_match(decoded)) {
		ast_test_status_update(test, "Test6: encode decode routine for AOC-S did not match expected results\n");
		res = AST_TEST_FAIL;
		goto cleanup_aoc_test;
	}
	/* cleanup decoded msg */
	decoded = ast_aoc_destroy_decoded(decoded);



cleanup_aoc_test:

	decoded = ast_aoc_destroy_decoded(decoded);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(aoc_encode_decode_test);
	AST_TEST_UNREGISTER(aoc_event_generation_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(aoc_encode_decode_test);
	AST_TEST_REGISTER(aoc_event_generation_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "AOC unit tests");
