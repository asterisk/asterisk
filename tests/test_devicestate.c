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
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/devicestate.h"
#include "asterisk/pbx.h"


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
	AST_DEVICE_UNKNOWN,
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
	AST_EXTENSION_NOT_INUSE,
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

static int unload_module(void)
{
	AST_TEST_UNREGISTER(device2extenstate_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(device2extenstate_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Device State Test");
