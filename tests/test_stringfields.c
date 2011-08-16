/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Mark Michelson
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
 *
 * \brief String fields test
 *
 * \author\verbatim Mark Michelson <mmichelson@digium.com> \endverbatim
 *
 * Test module for string fields API
 * \ingroup tests
 * \todo need to test ast_calloc_with_stringfields
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/stringfields.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/test.h"

AST_TEST_DEFINE(string_field_test)
{
	const char *address_holder;
	struct ast_string_field_pool *field_pool1;
	struct ast_string_field_pool *field_pool2;
	struct ast_string_field_pool *field_pool3;
	static const char LONG_STRING[] = "A professional panoramic photograph of the majestic elephant bathing itself and its young by the shores of the raging Mississippi River";

	struct {
		AST_DECLARE_STRING_FIELDS (
			AST_STRING_FIELD(string1);
			AST_STRING_FIELD(string2);
		);
	} test_struct;

	struct {
		AST_DECLARE_STRING_FIELDS (
			AST_STRING_FIELD(string1);
			AST_STRING_FIELD(string2);
			AST_STRING_FIELD(string3);
		);
	} test_struct2;

	switch (cmd) {
	case TEST_INIT:
		info->name = "string_field_test";
		info->category = "/main/utils/";
		info->summary = "Test stringfield operations";
		info->description =
			"This tests the stringfield API";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "First things first. Let's see if we can actually allocate string fields\n");

	if (ast_string_field_init(&test_struct, 32)) {
		ast_test_status_update(test, "Failure to initialize string fields. They are totally messed up\n");
		return AST_TEST_FAIL;
	} else {
		ast_test_status_update(test, "All right! Successfully allocated! Now let's get down to business\n");
	}

	ast_test_status_update(test,"We're going to set some string fields and perform some checks\n");

	ast_string_field_set(&test_struct, string1, "elephant");
	ast_string_field_set(&test_struct, string2, "hippopotamus");

	ast_test_status_update(test, "First we're going to make sure that the strings are actually set to what we expect\n");

	if (strcmp(test_struct.string1, "elephant")) {
		ast_test_status_update(test, "We were expecting test_struct.string1 to have 'elephant' but it has %s\n", test_struct.string1);
		goto error;
	} else {
		ast_test_status_update(test, "test_struct.string1 appears to be all clear. It has '%s' and that's what we expect\n", test_struct.string1);
	}

	if (strcmp(test_struct.string2, "hippopotamus")) {
		ast_test_status_update(test, "We were expecting test_struct.string2 to have 'hippopotamus' but it has %s\n", test_struct.string2);
		goto error;
	} else {
		ast_test_status_update(test, "test_struct.string2 appears to be all clear. It has '%s' and that's what we expect\n", test_struct.string2);
	}

	ast_test_status_update(test, "Now let's make sure that our recorded capacities for these strings is what we expect\n");

	if (AST_STRING_FIELD_ALLOCATION(test_struct.string1) != strlen("elephant") + 1) {
		ast_test_status_update(test, "string1 has allocation area of %hu but we expect %lu\n",
				AST_STRING_FIELD_ALLOCATION(test_struct.string1), (unsigned long) strlen("elephant") + 1);
		goto error;
	} else {
		ast_test_status_update(test, "string1 has the allocation area we expect: %hu\n", AST_STRING_FIELD_ALLOCATION(test_struct.string1));
	}

	if (AST_STRING_FIELD_ALLOCATION(test_struct.string2) != strlen("hippopotamus") + 1) {
		ast_test_status_update(test, "string2 has allocation area of %hu but we expect %lu\n",
				AST_STRING_FIELD_ALLOCATION(test_struct.string2), (unsigned long) strlen("hippopotamus") + 1);
		goto error;
	} else {
		ast_test_status_update(test, "string2 has the allocation area we expect: %hu\n", AST_STRING_FIELD_ALLOCATION(test_struct.string2));
	}

	ast_test_status_update(test, "Now we're going to shrink string1 and see if it's in the same place in memory\n");

	address_holder = test_struct.string1;
	ast_string_field_set(&test_struct, string1, "rhino");

	if (strcmp(test_struct.string1, "rhino")) {
		ast_test_status_update(test, "string1 has the wrong value in it. We want 'rhino' but it has '%s'\n", test_struct.string1);
		goto error;
	} else {
		ast_test_status_update(test, "string1 successfully was changed to '%s'\n", test_struct.string1);
	}

	if (address_holder != test_struct.string1) {
		ast_test_status_update(test, "We shrunk string1, but it moved?!\n");
		goto error;
	} else {
		ast_test_status_update(test, "Shrinking string1 allowed it to stay in the same place in memory\n");
	}

	if (AST_STRING_FIELD_ALLOCATION(test_struct.string1) != strlen("elephant") + 1) {
		ast_test_status_update(test, "The allocation amount changed when we shrunk the string...\n");
		goto error;
	} else {
		ast_test_status_update(test, "Shrinking string1 did not change its allocation area (This is a good thing)\n");
	}

	ast_test_status_update(test, "Next, let's increase it a little but not all the way to its original size\n");

	address_holder = test_struct.string1;
	ast_string_field_set(&test_struct, string1, "mammoth");

	if (strcmp(test_struct.string1, "mammoth")) {
		ast_test_status_update(test, "string1 has the wrong value in it. We want 'mammoth' but it has '%s'\n", test_struct.string1);
		goto error;
	} else {
		ast_test_status_update(test, "string1 successfully was changed to '%s'\n", test_struct.string1);
	}

	if (address_holder != test_struct.string1) {
		ast_test_status_update(test, "We expanded string1, but it moved?!\n");
		goto error;
	} else {
		ast_test_status_update(test, "Expanding string1 allowed it to stay in the same place in memory\n");
	}

	if (AST_STRING_FIELD_ALLOCATION(test_struct.string1) != strlen("elephant") + 1) {
		ast_test_status_update(test, "The allocation amount changed when we expanded the string...\n");
		goto error;
	} else {
		ast_test_status_update(test, "Expanding string1 did not change its allocation area (This is a good thing)\n");
	}

	ast_test_status_update(test, "Cool, now let's bring it back to its original size and see what happens\n");

	ast_string_field_set(&test_struct, string1, "elephant");

	if (strcmp(test_struct.string1, "elephant")) {
		ast_test_status_update(test, "string1 has the wrong value in it. We want 'elephant' but it has '%s'\n", test_struct.string1);
		goto error;
	} else {
		ast_test_status_update(test, "string1 successfully changed to '%s'\n", test_struct.string1);
	}

	if (address_holder != test_struct.string1) {
		ast_test_status_update(test, "We restored string1 to its original size, but it moved?!\n");
		goto error;
	} else {
		ast_test_status_update(test, "Restoring string1 did not cause it to move (This is a good thing)\n");
	}

	if (AST_STRING_FIELD_ALLOCATION(test_struct.string1) != strlen("elephant") + 1) {
		ast_test_status_update(test, "The allocation amount changed when we re-expanded the string...\n");
		goto error;
	} else {
		ast_test_status_update(test, "The allocation amount for string1 is still holding steady\n");
	}

	ast_test_status_update(test, "All right, now we're going to expand string 2. It should stay in place since it was the last string allocated in this pool\n");

	address_holder = test_struct.string2;
	ast_string_field_set(&test_struct, string2, "hippopotamus face");

	if (strcmp(test_struct.string2, "hippopotamus face")) {
		ast_test_status_update(test, "string2 has the wrong value. We want 'hippopotamus face' but it has '%s'\n", test_struct.string2);
		goto error;
	} else {
		ast_test_status_update(test, "string2 successfully changed to '%s'\n", test_struct.string2);
	}

	if (AST_STRING_FIELD_ALLOCATION(test_struct.string2) != strlen("hippopotamus face") + 1) {
		ast_test_status_update(test, "The allocation amount is incorrect for string2. We expect %lu but it has %hu\n",
				(unsigned long) strlen("hippopotamus face"), AST_STRING_FIELD_ALLOCATION(test_struct.string2) + 1);
		goto error;
	} else {
		ast_test_status_update(test, "The allocation amount successfully increased for string2 when it grew\n");
	}

	if (test_struct.string2 != address_holder) {
		ast_test_status_update(test, "string2 has moved, but it should not have since it had room to grow\n");
		goto error;
	} else {
		ast_test_status_update(test, "string2 stayed in place when it grew. Good job!\n");
	}

	ast_test_status_update(test, "Now we're going to set string1 to a very long string so that a new string field pool must be allocated\n");

	address_holder = test_struct.string1;
	ast_string_field_set(&test_struct, string1, LONG_STRING);

	if (strcmp(test_struct.string1, LONG_STRING)) {
		ast_test_status_update(test, "We were expecting string1 to be '%s'\nbut it was actually '%s'\n", LONG_STRING, test_struct.string1);
		goto error;
	} else {
		ast_test_status_update(test, "string1 successfully changed to '%s'\n", test_struct.string1);
	}

	if (address_holder == test_struct.string1) {
		ast_test_status_update(test, "Uh oh, string1 didn't move when we set it to a long value\n");
		goto error;
	} else {
		ast_test_status_update(test, "Good. Setting string1 to a long value caused it to change addresses\n");
	}

	if (AST_STRING_FIELD_ALLOCATION(test_struct.string1) != strlen(LONG_STRING) + 1) {
		ast_test_status_update(test, "The string field allocation for string1 indicates a length of %hu instead of the expected %lu\n",
				AST_STRING_FIELD_ALLOCATION(test_struct.string1), (unsigned long) strlen(LONG_STRING) + 1);
		goto error;
	} else {
		ast_test_status_update(test, "The stored allocation size of string1 is what we expect\n");
	}

	ast_string_field_init(&test_struct2, 32);
	ast_test_status_update(test, "Now using a totally separate area of memory we're going to test a basic pool freeing scenario\n");

	ast_string_field_set(&test_struct2, string1, "first");
	ast_string_field_set(&test_struct2, string2, "second");
	ast_string_field_set(&test_struct2, string3, "third");

	/* This string is 208 characters long, which will surely exceed the initial pool size */
	ast_string_field_set(&test_struct2, string1, "Expanded first string to create new pool-----------------------------------------------------------------------------------------------------------------------------------------------------------------------");
	/* Pool size at this point is 976, so 1000 chars should do it */
	ast_string_field_set(&test_struct2, string2, "Expanded second string to create new pool----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------");

	field_pool3 = test_struct2.__field_mgr_pool;
	field_pool2 = test_struct2.__field_mgr_pool->prev;
	field_pool1 = test_struct2.__field_mgr_pool->prev->prev;

	if(field_pool3->prev != field_pool2 || field_pool2->prev != field_pool1) {
		ast_test_status_update(test, "Pools are not linked properly!\n");
		goto error;
	} else {
		ast_test_status_update(test, "Three different pools are linked as expected.\n");
	}

	ast_string_field_set(&test_struct2, string1, NULL);
	if (test_struct2.string1 != __ast_string_field_empty || field_pool3->prev != field_pool1) {
		ast_test_status_update(test, "Things did not work out when removing the middle pool!\n");
		goto error;
	} else {
		ast_test_status_update(test, "After removing a pool the remaining two are linked as expected.\n");
	}

	ast_string_field_free_memory(&test_struct2);
	ast_string_field_free_memory(&test_struct);
	return AST_TEST_PASS;

error:
	ast_string_field_free_memory(&test_struct);
	ast_string_field_free_memory(&test_struct2);
	return AST_TEST_FAIL;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(string_field_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(string_field_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "String Fields Test");
