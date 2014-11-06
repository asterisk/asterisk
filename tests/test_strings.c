/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief Dynamic string tests
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 * This module will run some dyanmic string tests.
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/test.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/module.h"

AST_TEST_DEFINE(str_test)
{
	struct ast_str *stack_str;
	struct ast_str *heap_str;
	const char short_string1[] = "apple";
	const char short_string2[] = "banana";
	char short_string_cat[30];
	const char long_string1[] = "applebananapeachmangocherrypeargrapeplumlimetangerinepomegranategravel";
	const char long_string2[] = "passionuglinectarinepineapplekiwilemonpaintthinner";
	char long_string_cat[200];
	char string_limit_cat[11];
	const int string_limit = 5;
	int current_size;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "str_test";
		info->category = "/main/strings/";
		info->summary = "Test dynamic string operations";
		info->description = "Test setting and appending stack and heap-allocated strings";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	snprintf(short_string_cat, sizeof(short_string_cat), "%s%s", short_string1, short_string2);
	snprintf(long_string_cat, sizeof(long_string_cat), "%s%s", long_string1, long_string2);
	snprintf(string_limit_cat, string_limit, "%s", long_string1);
	strncat(string_limit_cat, long_string2, string_limit);

	if (!(stack_str = ast_str_alloca(15))) {
		ast_test_status_update(test, "Failed to allocate an ast_str on the stack\n");
		return AST_TEST_FAIL;
	}

	if (!(heap_str = ast_str_create(15))) {
		ast_test_status_update(test, "Failed to allocate an ast_str on the heap\n");
	}

	/* Stack string tests:
	 * Part 1: Basic tests
	 * a. set a small string
	 * b. append a small string
	 * c. clear a string
	 * Part 2: Advanced tests
	 * a. Set a string that is larger than our allocation
	 * b. Append a string that is larger than our allocation
	 */

	/* Part 1a */
	if (ast_str_set(&stack_str, 0, "%s", short_string1) < 0) {
		ast_test_status_update(test, "Error setting stack string\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strcmp(ast_str_buffer(stack_str), short_string1)) {
		ast_test_status_update(test, "ast_str_set failed for stack string. Expected '%s' but"
				"instead got %s\n", short_string1, ast_str_buffer(stack_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 1b */
	if (ast_str_append(&stack_str, 0, "%s", short_string2) < 0) {
		ast_test_status_update(test, "Error appending to stack string\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strcmp(ast_str_buffer(stack_str), short_string_cat)) {
		ast_test_status_update(test, "ast_str_set failed for stack string. Expected '%s'"
				"but instead got %s\n", short_string_cat, ast_str_buffer(stack_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 1c */
	ast_str_reset(stack_str);
	if (ast_str_strlen(stack_str) != 0) {
		ast_test_status_update(test, "ast_str_reset resulted in non-zero length for stack_str\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Part 2a */
	if (ast_str_set(&stack_str, -1, "%s", long_string1) < 0) {
		ast_test_status_update(test, "Error setting stack string with long input\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strncmp(ast_str_buffer(stack_str), long_string1, ast_str_strlen(stack_str))) {
		ast_test_status_update(test, "Stack string not set to what is expected.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 2b */
	if (ast_str_append(&stack_str, -1, "%s", long_string2) < 0) {
		ast_test_status_update(test, "Error appending long string to full stack string buffer\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strncmp(ast_str_buffer(stack_str), long_string_cat, ast_str_strlen(stack_str))) {
		ast_test_status_update(test, "Stack string not set to what is expected.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Heap string tests
	 *
	 * All stack string tests from part 1.
	 * All stack string tests 2a and 2b.
	 * Tests 2a and 2b from stack string tests, passing 0 as max_len
	 * instead of -1. This allows for the buffer to grow.
	 */
	/* Part 1a */
	if (ast_str_set(&heap_str, 0, "%s", short_string1) < 0) {
		ast_test_status_update(test, "Error setting heap string\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strcmp(ast_str_buffer(heap_str), short_string1)) {
		ast_test_status_update(test, "ast_str_set failed for heap string. Expected '%s' but"
				"instead got %s\n", short_string1, ast_str_buffer(heap_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 1b */
	if (ast_str_append(&heap_str, 0, "%s", short_string2) < 0) {
		ast_test_status_update(test, "Error appending to heap string\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strcmp(ast_str_buffer(heap_str), short_string_cat)) {
		ast_test_status_update(test, "ast_str_set failed for stack string. Expected '%s'"
				"but instead got %s\n", short_string_cat, ast_str_buffer(stack_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 1c */
	ast_str_reset(heap_str);
	if (ast_str_strlen(heap_str) != 0) {
		ast_test_status_update(test, "ast_str_reset resulted in non-zero length for stack_str\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 2a with -1 arg */
	current_size = ast_str_size(heap_str);
	if (ast_str_set(&heap_str, -1, "%s", long_string1) < 0) {
		ast_test_status_update(test, "Error setting heap string with long input\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (current_size != ast_str_size(heap_str)) {
		ast_test_status_update(test, "Heap string changed size during ast_str_set when it was"
				"instructed not to. Was %d and now is %d\n", current_size, (int) ast_str_size(heap_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strncmp(ast_str_buffer(heap_str), long_string1, ast_str_strlen(heap_str))) {
		ast_test_status_update(test, "Heap string not set to what is expected.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 2b with -1 arg */
	current_size = ast_str_size(heap_str);
	if (ast_str_append(&heap_str, -1, "%s", long_string2) < 0) {
		ast_test_status_update(test, "Error appending long string to full heap string buffer\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (current_size != ast_str_size(heap_str)) {
		ast_test_status_update(test, "Heap string changed size during ast_str_append when it was"
				"instructed not to. Was %d and now is %d\n", current_size, (int) ast_str_size(heap_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strncmp(ast_str_buffer(heap_str), long_string_cat, ast_str_strlen(heap_str))) {
		ast_test_status_update(test, "Heap string not set to what is expected.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* reset string before continuing */
	ast_str_reset(heap_str);
	/* Part 2a with 0 arg */
	if (ast_str_set(&heap_str, 0, "%s", long_string1) < 0) {
		ast_test_status_update(test, "Error setting heap string with long input\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strcmp(ast_str_buffer(heap_str), long_string1)) {
		ast_test_status_update(test, "Heap string does not contain what was expected. Expected %s"
				"but have %s instead\n", long_string1, ast_str_buffer(heap_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Part 2b with 0 arg */
	if (ast_str_append(&heap_str, 0, "%s", long_string2) < 0) {
		ast_test_status_update(test, "Error setting heap string with long input\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (strcmp(ast_str_buffer(heap_str), long_string_cat)) {
		ast_test_status_update(test, "Heap string does not contain what was expected. Expected %s"
				"but have %s instead\n", long_string_cat, ast_str_buffer(heap_str));
		res = AST_TEST_FAIL;
		goto cleanup;
	}

cleanup:
	ast_free(heap_str);
	return res;
}

static int test_semi(char *string1, char *string2, int test_len)
{
	char *test2 = NULL;
	if (test_len >= 0) {
		test2 = ast_alloca(test_len);
		*test2 = '\0';
	}
	ast_escape_semicolons(string1, test2, test_len);
	if (test2 != NULL && strcmp(string2, test2) == 0) {
		return 1;
	} else {
		return 0;
	}
}

AST_TEST_DEFINE(escape_semicolons_test)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "escape_semicolons";
		info->category = "/main/strings/";
		info->summary = "Test ast_escape_semicolons";
		info->description = "Test ast_escape_semicolons";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}


	ast_test_validate(test, test_semi("this is a ;test", "this is a \\;test", 18));
	ast_test_validate(test, test_semi(";", "\\;", 3));

	/* The following tests should return empty because there's not enough room to output
	 * an escaped ; or even a single character.
	 */
	ast_test_validate(test, test_semi(";", "", 0));
	ast_test_validate(test, test_semi(";", "", 1));
	ast_test_validate(test, test_semi(";", "", 2));
	ast_test_validate(test, test_semi("x", "", 0));
	ast_test_validate(test, test_semi("x", "", 1));

	/* At least some output should be produced now. */
	ast_test_validate(test, test_semi("xx;xx", "x", 2));
	ast_test_validate(test, test_semi("xx;xx", "xx", 3));

	/* There's still not enough room to output \; so
	 * don't even print the \
	 */
	ast_test_validate(test, test_semi("xx;xx", "xx", 4));

	ast_test_validate(test, test_semi("xx;xx", "xx\\;", 5));
	ast_test_validate(test, test_semi("xx;xx", "xx\\;x", 6));
	ast_test_validate(test, test_semi("xx;xx", "xx\\;xx", 7));
	ast_test_validate(test, test_semi("xx;xx", "xx\\;xx", 8));

	/* Random stuff */
	ast_test_validate(test, test_semi("xx;xx;this is a test", "xx\\;xx\\;this is a test", 32));
	ast_test_validate(test, test_semi(";;;;;", "\\;\\;\\;\\;\\;", 32));
	ast_test_validate(test, test_semi(";;;;;", "\\;\\;\\;\\;", 10));
	ast_test_validate(test, test_semi(";;;;;", "\\;\\;\\;\\;\\;", 11));
	ast_test_validate(test, test_semi(";;\\;;;", "\\;\\;\\\\;\\;\\;", 32));

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(str_test);
	AST_TEST_UNREGISTER(escape_semicolons_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(str_test);
	AST_TEST_REGISTER(escape_semicolons_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dynamic string test module");
