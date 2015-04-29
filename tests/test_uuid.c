/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Mark Michelson <mmmichelson@digium.com>
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
 * \brief Universally unique identifier tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/test.h"
#include "asterisk/uuid.h"
#include "asterisk/module.h"

AST_TEST_DEFINE(uuid)
{
	struct ast_uuid *uuid1 = NULL;
	struct ast_uuid *uuid2 = NULL;
	struct ast_uuid *uuid3 = NULL;
	char uuid_str[AST_UUID_STR_LEN];
	enum ast_test_result_state res = AST_TEST_FAIL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "uuid";
		info->category = "/main/uuid/";
		info->summary = "UUID unit test";
		info->description =
			"This tests basic UUID operations to ensure they work properly";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Use method of generating UUID directly as a string. */
	ast_uuid_generate_str(uuid_str, sizeof(uuid_str));
	if (strlen(uuid_str) != (AST_UUID_STR_LEN - 1)) {
		ast_test_status_update(test, "Failed to directly generate UUID string\n");
		goto end;
	}
	ast_test_status_update(test, "Generate UUID direct to string, got %s\n", uuid_str);

	/* Now convert the direct UUID string to a UUID */
	uuid1 = ast_str_to_uuid(uuid_str);
	if (!uuid1) {
		ast_test_status_update(test, "Unable to convert direct UUID string %s to UUID\n", uuid_str);
		goto end;
	}
	ast_free(uuid1);

	/* Make sure that we can generate a UUID */
	uuid1 = ast_uuid_generate();
	if (!uuid1) {
		ast_test_status_update(test, "Unable to generate a UUID\n");
		goto end;
	}

	/* Make sure we're not generating nil UUIDs */
	if (ast_uuid_is_nil(uuid1)) {
		ast_test_status_update(test, "We generated a nil UUID. Something is wrong\n");
		goto end;
	}

	/* Convert it to a string */
	ast_uuid_to_str(uuid1, uuid_str, sizeof(uuid_str));

	if (strlen(uuid_str) != (AST_UUID_STR_LEN - 1)) {
		ast_test_status_update(test, "Failed to convert the UUID to a string\n");
		goto end;
	}

	ast_test_status_update(test, "Second generated UUID converted to string, got %s\n", uuid_str);

	/* Now convert the string back to a UUID */
	uuid2 = ast_str_to_uuid(uuid_str);
	if (!uuid2) {
		ast_test_status_update(test, "Unable to convert string %s to UUID\n", uuid_str);
		goto end;
	}

	/* Make sure the UUIDs are identical */
	if (ast_uuid_compare(uuid1, uuid2) != 0) {
		ast_test_status_update(test, "UUIDs that should be identical are different\n");
		goto end;
	}

	/* Try copying a UUID */
	uuid3 = ast_uuid_copy(uuid1);
	if (!uuid3) {
		ast_test_status_update(test, "Unable to copy UUID\n");
		goto end;
	}

	/* Make sure copied UUIDs are identical */
	if (ast_uuid_compare(uuid1, uuid3) != 0) {
		ast_test_status_update(test, "UUIDs that should be identical are different\n");
		goto end;
	}

	if (ast_uuid_compare(uuid2, uuid3) != 0) {
		ast_test_status_update(test, "UUIDs that should be identical are different\n");
		goto end;
	}

	/* Clear a UUID and ensure that it registers as nil */
	ast_uuid_clear(uuid1);

	if (!ast_uuid_is_nil(uuid1)) {
		ast_test_status_update(test, "UUID that was cleared does not appear to be nil\n");
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_free(uuid1);
	ast_free(uuid2);
	ast_free(uuid3);
	return res;
}

static int load_module(void)
{
	AST_TEST_REGISTER(uuid);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "UUID test module");
