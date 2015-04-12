/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \file \brief Test JSON API.
 *
 * While some of these tests are actually testing our JSON library wrapper, the bulk of
 * them are exploratory tests to determine what the behavior of the underlying JSON
 * library is. This also gives us a good indicator if that behavior changes between
 * Jansson revisions.
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)
#include "asterisk/json.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

#include <stdio.h>
#include <unistd.h>

#define CATEGORY "/main/json/"

/*!
 * Number of allocations from JSON library that have not yet been freed.
 */
static size_t alloc_count;

/*!@{*/
/*!
 * JSON library has its own reference counting, so we'll provide our own allocators to
 * test that everything gets freed as expected.
 */
static void *json_debug_malloc(size_t size)
{
	void *p = ast_json_malloc(size);
	if (p) {
		++alloc_count;
	}
	return p;
}

static void json_debug_free(void *p)
{
	if (p) {
		--alloc_count;
	}
	ast_json_free(p);
}

static int json_test_init(struct ast_test_info *info, struct ast_test *test)
{
	ast_json_set_alloc_funcs(json_debug_malloc, json_debug_free);
	alloc_count = 0;
	return 0;
}

static int json_test_cleanup(struct ast_test_info *info, struct ast_test *test)
{
	ast_json_reset_alloc_funcs();
	if (0 != alloc_count) {
		ast_test_status_update(test,
			"JSON test leaked %zu allocations!\n", alloc_count);
		return -1;
	}
	return 0;
}

/*!@}*/

AST_TEST_DEFINE(json_test_false)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "false";
		info->category = CATEGORY;
		info->summary = "Testing fundamental JSON false value.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_false();
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_FALSE == ast_json_typeof(uut));
	ast_test_validate(test, !ast_json_is_null(uut));
	ast_test_validate(test, !ast_json_is_true(uut));
	ast_test_validate(test, ast_json_is_false(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_true)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "true";
		info->category = CATEGORY;
		info->summary = "Testing JSON true value.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_true();
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_TRUE == ast_json_typeof(uut));
	ast_test_validate(test, !ast_json_is_null(uut));
	ast_test_validate(test, ast_json_is_true(uut));
	ast_test_validate(test, !ast_json_is_false(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_bool0)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bool0";
		info->category = CATEGORY;
		info->summary = "Testing JSON boolean function (false).";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_boolean(0);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_FALSE == ast_json_typeof(uut));
	ast_test_validate(test, !ast_json_is_null(uut));
	ast_test_validate(test, !ast_json_is_true(uut));
	ast_test_validate(test, ast_json_is_false(uut));
	ast_test_validate(test, ast_json_equal(uut, ast_json_false()));
	ast_test_validate(test, !ast_json_equal(uut, ast_json_true()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_bool1)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bool1";
		info->category = CATEGORY;
		info->summary = "Testing JSON boolean function (true).";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_boolean(1);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_TRUE == ast_json_typeof(uut));
	ast_test_validate(test, !ast_json_is_null(uut));
	ast_test_validate(test, ast_json_is_true(uut));
	ast_test_validate(test, !ast_json_is_false(uut));
	ast_test_validate(test, !ast_json_equal(uut, ast_json_false()));
	ast_test_validate(test, ast_json_equal(uut, ast_json_true()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_null)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "null";
		info->category = CATEGORY;
		info->summary = "Testing JSON null value.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_null();
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_NULL == ast_json_typeof(uut));
	ast_test_validate(test, ast_json_is_null(uut));
	ast_test_validate(test, !ast_json_is_true(uut));
	ast_test_validate(test, !ast_json_is_false(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_null_val)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "null_val";
		info->category = CATEGORY;
		info->summary = "Testing JSON handling of NULL.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* NULL isn't null, true or false */
	ast_test_validate(test, !ast_json_is_null(NULL));
	ast_test_validate(test, !ast_json_is_false(NULL));
	ast_test_validate(test, !ast_json_is_true(NULL));

	/* ref and unref should be NULL safe */
	ast_json_ref(NULL);
	ast_json_unref(NULL);
	/* no segfault; we're good. le sigh. */

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_string)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "string";
		info->category = CATEGORY;
		info->summary = "Basic string tests.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_string_create("Hello, json");
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_STRING == ast_json_typeof(uut));
	ast_test_validate(test, 0 == strcmp("Hello, json", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, NULL);
	ast_test_validate(test, -1 == uut_res);
	ast_test_validate(test, 0 == strcmp("Hello, json", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, "Not UTF-8 - \xff");
	ast_test_validate(test, -1 == uut_res);
	ast_test_validate(test, 0 == strcmp("Hello, json", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, "Is UTF-8 - \xE2\x98\xBA");
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 0 == strcmp("Is UTF-8 - \xE2\x98\xBA", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, "Goodbye, json");
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 0 == strcmp("Goodbye, json", ast_json_string_get(uut)));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_string_null)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "string_null";
		info->category = CATEGORY;
		info->summary = "JSON string NULL tests.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* NULL string */
	uut = ast_json_string_create(NULL);
	ast_test_validate(test, NULL == uut);

	/* NULL JSON strings */
	ast_test_validate(test, NULL == ast_json_string_create(NULL));
	ast_test_validate(test, NULL == ast_json_string_get(NULL));
	ast_test_validate(test, -1 == ast_json_string_set(NULL, "not null"));

	/* string_value from non-string elements should return NULL */
	ast_test_validate(test, NULL == ast_json_string_get(ast_json_null()));
	ast_test_validate(test, NULL == ast_json_string_get(ast_json_false()));
	ast_test_validate(test, NULL == ast_json_string_get(ast_json_true()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_stringf)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stringf";
		info->category = CATEGORY;
		info->summary = "Basic string formatting tests.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* NULL format string */
	uut = ast_json_stringf(NULL);
	ast_test_validate(test, NULL == uut);

	/* Non-UTF-8 strings are invalid */
	uut = ast_json_stringf("Not UTF-8 - %s", "\xff");
	ast_test_validate(test, NULL == uut);

	/* formatted string */
	uut = ast_json_stringf("Hello, %s", "json");
	expected = ast_json_string_create("Hello, json");
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_int)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "int";
		info->category = CATEGORY;
		info->summary = "Basic JSON integer tests.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Integer tests */
	uut = ast_json_integer_create(0);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_INTEGER == ast_json_typeof(uut));
	ast_test_validate(test, 0 == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, 1);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 1 == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, -1);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, -1 == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, LLONG_MAX);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, LLONG_MAX == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, LLONG_MIN);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, LLONG_MIN == ast_json_integer_get(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_non_int)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "non_int";
		info->category = CATEGORY;
		info->summary = "Testing integer functions with non-integer types.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Non-ints return 0 integer value */
	ast_test_validate(test, 0 == ast_json_integer_get(ast_json_null()));
	ast_test_validate(test, 0 == ast_json_integer_get(ast_json_true()));
	ast_test_validate(test, 0 == ast_json_integer_get(ast_json_false()));

	/* JSON NULL integers */
	ast_test_validate(test, 0 == ast_json_integer_get(NULL));
	ast_test_validate(test, -1 == ast_json_integer_set(NULL, 911));
	ast_test_validate(test, 0 == ast_json_array_size(NULL));

	/* No magical parsing of strings into ints */
	uut = ast_json_string_create("314");
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, 0 == ast_json_integer_get(uut));

	/* Or vice-versa */
	ast_json_unref(uut);
	uut = ast_json_integer_create(314);
	ast_test_validate(test, NULL == ast_json_string_get(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_create)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_create";
		info->category = CATEGORY;
		info->summary = "Testing creating JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array creation */
	uut = ast_json_array_create();
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_ARRAY == ast_json_typeof(uut));
	ast_test_validate(test, 0 == ast_json_array_size(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_append)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_append";
		info->category = CATEGORY;
		info->summary = "Testing appending to JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array append */
	uut = ast_json_array_create();
	uut_res = ast_json_array_append(uut, ast_json_string_create("one"));
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 1 == ast_json_array_size(uut));
	ast_test_validate(test, 0 == strcmp("one", ast_json_string_get(ast_json_array_get(uut, 0))));
	/* index out of range */
	ast_test_validate(test, NULL == ast_json_array_get(uut, 1));
	ast_test_validate(test, NULL == ast_json_array_get(uut, -1));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_inset)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_insert";
		info->category = CATEGORY;
		info->summary = "Testing inserting into JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array insert */
	uut = ast_json_pack("[s]", "one");
	uut_res = ast_json_array_insert(uut, 0, ast_json_string_create("zero"));
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 2 == ast_json_array_size(uut));
	ast_test_validate(test, 0 == strcmp("zero", ast_json_string_get(ast_json_array_get(uut, 0))));
	ast_test_validate(test, 0 == strcmp("one", ast_json_string_get(ast_json_array_get(uut, 1))));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_set)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_set";
		info->category = CATEGORY;
		info->summary = "Testing setting a value in JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array set */
	uut = ast_json_pack("[s, s]", "zero", "one");
	uut_res = ast_json_array_set(uut, 1, ast_json_integer_create(1));
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 2 == ast_json_array_size(uut));
	ast_test_validate(test, 0 == strcmp("zero", ast_json_string_get(ast_json_array_get(uut, 0))));
	ast_test_validate(test, 1 == ast_json_integer_get(ast_json_array_get(uut, 1)));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_remove)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_remove";
		info->category = CATEGORY;
		info->summary = "Testing removing a value from JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array remove */
	uut = ast_json_pack("[s, i]", "zero", 1);
	expected = ast_json_pack("[i]", 1);
	uut_res = ast_json_array_remove(uut, 0);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_clear)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_clear";
		info->category = CATEGORY;
		info->summary = "Testing clearing JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array clear */
	uut = ast_json_pack("[s, s]", "zero", "one");
	uut_res = ast_json_array_clear(uut);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 0 == ast_json_array_size(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_extend)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, tail, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_extend";
		info->category = CATEGORY;
		info->summary = "Testing extending JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array extending */
	expected = ast_json_array_create();
	ast_json_array_append(expected, ast_json_string_create("a"));
	ast_json_array_append(expected, ast_json_string_create("b"));
	ast_json_array_append(expected, ast_json_string_create("c"));
	ast_json_array_append(expected, ast_json_integer_create(1));
	ast_json_array_append(expected, ast_json_integer_create(2));
	ast_json_array_append(expected, ast_json_integer_create(3));

	uut = ast_json_array_create();
	ast_json_array_append(uut, ast_json_string_create("a"));
	ast_json_array_append(uut, ast_json_string_create("b"));
	ast_json_array_append(uut, ast_json_string_create("c"));

	tail = ast_json_array_create();
	ast_json_array_append(tail, ast_json_integer_create(1));
	ast_json_array_append(tail, ast_json_integer_create(2));
	ast_json_array_append(tail, ast_json_integer_create(3));

	uut_res = ast_json_array_extend(uut, tail);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, ast_json_equal(expected, uut));
	/* tail is preserved */
	ast_test_validate(test, 3 == ast_json_array_size(tail));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_array_null)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "array_null";
		info->category = CATEGORY;
		info->summary = "Testing NULL conditions for JSON arrays.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* array NULL checks */
	ast_test_validate(test, 0 == ast_json_array_size(NULL));
	ast_test_validate(test, NULL == ast_json_array_get(NULL, 0));
	ast_test_validate(test, -1 == ast_json_array_set(NULL, 0, ast_json_null()));
	ast_test_validate(test, -1 == ast_json_array_append(NULL, ast_json_null()));
	ast_test_validate(test, -1 == ast_json_array_insert(NULL, 0, ast_json_null()));
	ast_test_validate(test, -1 == ast_json_array_remove(NULL, 0));
	ast_test_validate(test, -1 == ast_json_array_clear(NULL));
	uut = ast_json_array_create();
	ast_test_validate(test, -1 == ast_json_array_extend(uut, NULL));
	ast_test_validate(test, -1 == ast_json_array_extend(NULL, uut));
	ast_test_validate(test, -1 == ast_json_array_extend(NULL, NULL));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_alloc)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_alloc";
		info->category = CATEGORY;
		info->summary = "Testing creating JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object allocation */
	uut = ast_json_object_create();
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, AST_JSON_OBJECT == ast_json_typeof(uut));
	ast_test_validate(test, 0 == ast_json_object_size(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_set)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_set";
		info->category = CATEGORY;
		info->summary = "Testing setting values in JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object set */
	expected = ast_json_pack("{s: i, s: i, s: i}", "one", 1, "two", 2, "three", 3);
	uut = ast_json_object_create();
	uut_res = ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_test_validate(test, 0 == uut_res);
	uut_res = ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_test_validate(test, 0 == uut_res);
	uut_res = ast_json_object_set(uut, "three", ast_json_integer_create(3));
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, ast_json_equal(expected, uut));
	ast_test_validate(test, NULL == ast_json_object_get(uut, "dne"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_set_overwrite)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_set_overwriting";
		info->category = CATEGORY;
		info->summary = "Testing changing values in JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object set existing */
	uut = ast_json_pack("{s: i, s: i, s: i}", "one", 1, "two", 2, "three", 3);
	uut_res = ast_json_object_set(uut, "two", ast_json_integer_create(-2));
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, -2 == ast_json_integer_get(ast_json_object_get(uut, "two")));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_get)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_get";
		info->category = CATEGORY;
		info->summary = "Testing getting values from JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object get */
	uut = ast_json_pack("{s: i, s: i, s: i}", "one", 1, "two", 2, "three", 3);
	ast_test_validate(test, 2 == ast_json_integer_get(ast_json_object_get(uut, "two")));
	ast_test_validate(test, NULL == ast_json_object_get(uut, "dne"));
	ast_test_validate(test, NULL == ast_json_object_get(uut, NULL));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_del)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_del";
		info->category = CATEGORY;
		info->summary = "Testing deleting values from JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object del */
	expected = ast_json_object_create();
	uut = ast_json_pack("{s: i}", "one", 1);
	uut_res = ast_json_object_del(uut, "one");
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, ast_json_equal(expected, uut));
	uut_res = ast_json_object_del(uut, "dne");
	ast_test_validate(test, -1 == uut_res);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_clear)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_clear";
		info->category = CATEGORY;
		info->summary = "Testing clearing values from JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object clear */
	uut = ast_json_object_create();
	ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_json_object_set(uut, "three", ast_json_integer_create(3));
	uut_res = ast_json_object_clear(uut);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, 0 == ast_json_object_size(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_merge_all)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, merge, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_alloc";
		info->category = CATEGORY;
		info->summary = "Testing merging JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object merging - all */
	uut = ast_json_object_create();
	ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_json_object_set(uut, "three", ast_json_integer_create(3));

	merge = ast_json_object_create();
	ast_json_object_set(merge, "three", ast_json_integer_create(-3));
	ast_json_object_set(merge, "four", ast_json_integer_create(-4));
	ast_json_object_set(merge, "five", ast_json_integer_create(-5));

	expected = ast_json_object_create();
	ast_json_object_set(expected, "one", ast_json_integer_create(1));
	ast_json_object_set(expected, "two", ast_json_integer_create(2));
	ast_json_object_set(expected, "three", ast_json_integer_create(-3));
	ast_json_object_set(expected, "four", ast_json_integer_create(-4));
	ast_json_object_set(expected, "five", ast_json_integer_create(-5));

	uut_res = ast_json_object_update(uut, merge);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, ast_json_equal(expected, uut));
	/* merge object is untouched */
	ast_test_validate(test, 3 == ast_json_object_size(merge));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_merge_existing)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, merge, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_alloc";
		info->category = CATEGORY;
		info->summary = "Testing merging JSON objects, updating only existing fields.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object merging - existing */
	uut = ast_json_object_create();
	ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_json_object_set(uut, "three", ast_json_integer_create(3));

	merge = ast_json_object_create();
	ast_json_object_set(merge, "three", ast_json_integer_create(-3));
	ast_json_object_set(merge, "four", ast_json_integer_create(-4));
	ast_json_object_set(merge, "five", ast_json_integer_create(-5));

	expected = ast_json_object_create();
	ast_json_object_set(expected, "one", ast_json_integer_create(1));
	ast_json_object_set(expected, "two", ast_json_integer_create(2));
	ast_json_object_set(expected, "three", ast_json_integer_create(-3));

	uut_res = ast_json_object_update_existing(uut, merge);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, ast_json_equal(expected, uut));
	/* merge object is untouched */
	ast_test_validate(test, 3 == ast_json_object_size(merge));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_merge_missing)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, merge, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_merge_missing";
		info->category = CATEGORY;
		info->summary = "Testing merging JSON objects, adding only missing fields.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* object merging - missing */
	uut = ast_json_object_create();
	ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_json_object_set(uut, "three", ast_json_integer_create(3));

	merge = ast_json_object_create();
	ast_json_object_set(merge, "three", ast_json_integer_create(-3));
	ast_json_object_set(merge, "four", ast_json_integer_create(-4));
	ast_json_object_set(merge, "five", ast_json_integer_create(-5));

	expected = ast_json_object_create();
	ast_json_object_set(expected, "one", ast_json_integer_create(1));
	ast_json_object_set(expected, "two", ast_json_integer_create(2));
	ast_json_object_set(expected, "three", ast_json_integer_create(3));
	ast_json_object_set(expected, "four", ast_json_integer_create(-4));
	ast_json_object_set(expected, "five", ast_json_integer_create(-5));

	uut_res = ast_json_object_update_missing(uut, merge);
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, ast_json_equal(expected, uut));
	/* merge object is untouched */
	ast_test_validate(test, 3 == ast_json_object_size(merge));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_null)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_null";
		info->category = CATEGORY;
		info->summary = "Testing JSON object NULL behavior.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Object NULL testing */
	ast_test_validate(test, 0 == ast_json_object_size(NULL));
	ast_test_validate(test, NULL == ast_json_object_get(NULL, "not null"));
	ast_test_validate(test, -1 == ast_json_object_set(NULL, "not null", ast_json_null()));
	ast_test_validate(test, -1 == ast_json_object_del(NULL, "not null"));
	ast_test_validate(test, -1 == ast_json_object_clear(NULL));
	uut = ast_json_object_create();
	ast_test_validate(test, -1 == ast_json_object_update(NULL, uut));
	ast_test_validate(test, -1 == ast_json_object_update(uut, NULL));
	ast_test_validate(test, -1 == ast_json_object_update(NULL, NULL));
	ast_test_validate(test, -1 == ast_json_object_update_existing(NULL, uut));
	ast_test_validate(test, -1 == ast_json_object_update_existing(uut, NULL));
	ast_test_validate(test, -1 == ast_json_object_update_existing(NULL, NULL));
	ast_test_validate(test, -1 == ast_json_object_update_missing(NULL, uut));
	ast_test_validate(test, -1 == ast_json_object_update_missing(uut, NULL));
	ast_test_validate(test, -1 == ast_json_object_update_missing(NULL, NULL));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_iter)
{
	struct ast_json_iter *iter;
	int count;
	int uut_res;
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_iter";
		info->category = CATEGORY;
		info->summary = "Testing iterating through JSON objects.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Object iterator testing */
	uut = ast_json_pack("{s: i, s: i, s: i, s: i, s: i}", "one", 1, "two", 2, "three", 3, "four", 4, "five", 5);

	/* Iterate through the object; be aware that order isn't specified */
	iter = ast_json_object_iter(uut);
	ast_test_validate(test, NULL != iter);
	count = 0;
	while (NULL != iter) {
		if (0 == strcmp("one", ast_json_object_iter_key(iter))) {
			ast_test_validate(test, 1 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("two", ast_json_object_iter_key(iter))) {
			ast_test_validate(test, 2 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("three", ast_json_object_iter_key(iter))) {
			ast_test_validate(test, 3 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("four", ast_json_object_iter_key(iter))) {
			ast_test_validate(test, 4 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("five", ast_json_object_iter_key(iter))) {
			ast_test_validate(test, 5 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else {
			/* Unexpected key */
			ast_test_validate(test, 0);
		}
		iter = ast_json_object_iter_next(uut, iter);
		++count;
	}
	ast_test_validate(test, 5 == count);

	/* iterator non-existing key */
	iter = ast_json_object_iter_at(uut, "dne");
	ast_test_validate(test, NULL == iter);

	/* iterator specific key */
	iter = ast_json_object_iter_at(uut, "three");
	ast_test_validate(test, NULL != iter);
	ast_test_validate(test, 3 == ast_json_integer_get(ast_json_object_iter_value(iter)));

	/* set via iter */
	iter = ast_json_object_iter_at(uut, "three");
	uut_res = ast_json_object_iter_set(uut, iter, ast_json_integer_create(-3));
	ast_test_validate(test, 0 == uut_res);
	ast_test_validate(test, -3 == ast_json_integer_get(ast_json_object_get(uut, "three")));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_object_iter_null)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_iter_null";
		info->category = CATEGORY;
		info->summary = "Testing JSON object iterator NULL testings.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* iterator NULL tests */
	uut = ast_json_object_create();
	ast_test_validate(test, NULL == ast_json_object_iter(NULL));
	ast_test_validate(test, NULL == ast_json_object_iter_at(NULL, "not null"));
	ast_test_validate(test, NULL == ast_json_object_iter_next(NULL, NULL));
	ast_test_validate(test, NULL == ast_json_object_iter_next(uut, NULL));
	ast_test_validate(test, NULL == ast_json_object_iter_key(NULL));
	ast_test_validate(test, NULL == ast_json_object_iter_value(NULL));
	ast_test_validate(test, -1 == ast_json_object_iter_set(NULL, NULL, ast_json_null()));
	ast_test_validate(test, -1 == ast_json_object_iter_set(uut, NULL, ast_json_null()));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_dump_load_string)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	RAII_VAR(char *, str, NULL, json_debug_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "dump_load_string";
		info->category = CATEGORY;
		info->summary = "Testing dumping strings from JSON.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	expected = ast_json_pack("{ s: i }", "one", 1);
	str = ast_json_dump_string(expected);
	ast_test_validate(test, NULL != str);
	uut = ast_json_load_string(str, NULL);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_json_equal(expected, uut));

	/* dump_string NULL */
	ast_test_validate(test, NULL == ast_json_dump_string(NULL));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_dump_load_str)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	RAII_VAR(struct ast_str *, astr, NULL, ast_free);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "dump_load_str";
		info->category = CATEGORY;
		info->summary = "Testing dumping ast_str from JSON.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* dump/load ast_str */
	expected = ast_json_pack("{ s: i }", "one", 1);
	astr = ast_str_create(1); /* should expand to hold output */
	uut_res = ast_json_dump_str(expected, &astr);
	ast_test_validate(test, 0 == uut_res);
	uut = ast_json_load_str(astr, NULL);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_dump_str_fail)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	struct ast_str *astr;
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "dump_str_fail";
		info->category = CATEGORY;
		info->summary = "Testing dumping to ast_str when it can't grow.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* dump ast_str growth failure */
	expected = ast_json_pack("{ s: i }", "one", 1);
	astr = ast_str_alloca(1); /* cannot grow */
	uut_res = ast_json_dump_str(expected, &astr);
	ast_test_validate(test, 0 != uut_res);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_load_buffer)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	const char *str;

	switch (cmd) {
	case TEST_INIT:
		info->name = "load_buffer";
		info->category = CATEGORY;
		info->summary = "Testing loading JSON from buffer.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* load buffer */
	str = "{ \"one\": 1 } trailing garbage";
	uut = ast_json_load_string(str, NULL);
	ast_test_validate(test, NULL == uut);
	uut = ast_json_load_buf(str, strlen("{ \"one\": 1 }"), NULL);
	ast_test_validate(test, NULL != uut);

	return AST_TEST_PASS;
}

/*! \brief \a fclose isn't NULL safe. */
static int safe_fclose(FILE *f)
{
	if (f) {
		return fclose(f);
	}
	return 0;
}

static FILE *mkstemp_file(char *template, const char *mode)
{
	int fd = mkstemp(template);
	FILE *file;

	if (fd < 0) {
		ast_log(LOG_ERROR, "Failed to create temp file: %s\n",
			strerror(errno));
		return NULL;
	}

	file = fdopen(fd, mode);
	if (!file) {
		ast_log(LOG_ERROR, "Failed to create temp file: %s\n",
			strerror(errno));
		return NULL;
	}

	return file;
}

AST_TEST_DEFINE(json_test_dump_load_file)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	char filename[] = "/tmp/ast_json.XXXXXX";
	RAII_VAR(char *, rm_on_exit, filename, unlink);
	RAII_VAR(FILE *, file, NULL, safe_fclose);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "dump_load_file";
		info->category = CATEGORY;
		info->summary = "Testing dumping/loading JSON to/from file by FILE *.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* dump/load file */
	expected = ast_json_pack("{ s: i }", "one", 1);
	file = mkstemp_file(filename, "w");
	ast_test_validate(test, NULL != file);
	uut_res = ast_json_dump_file(expected, file);
	ast_test_validate(test, 0 == uut_res);
	fclose(file);
	file = fopen(filename, "r");
	ast_test_validate(test, NULL != file);
	uut = ast_json_load_file(file, NULL);
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_dump_load_new_file)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	char filename[] = "/tmp/ast_json.XXXXXX";
	RAII_VAR(char *, rm_on_exit, filename, unlink);
	RAII_VAR(FILE *, file, NULL, safe_fclose);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "dump_load_new_file";
		info->category = CATEGORY;
		info->summary = "Testing dumping/load JSON to/from file by filename.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* dump/load filename */
	expected = ast_json_pack("{ s: i }", "one", 1);
	file = mkstemp_file(filename, "w");
	ast_test_validate(test, NULL != file);
	uut_res = ast_json_dump_new_file(expected, filename);
	ast_test_validate(test, 0 == uut_res);
	uut = ast_json_load_new_file(filename, NULL);
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_dump_load_null)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	char filename[] = "/tmp/ast_json.XXXXXX";
	RAII_VAR(char *, rm_on_exit, filename, unlink);
	RAII_VAR(FILE *, file, NULL, safe_fclose);

	switch (cmd) {
	case TEST_INIT:
		info->name = "dump_load_null";
		info->category = CATEGORY;
		info->summary = "Testing NULL handling of dump/load functions.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* dump/load NULL tests */
	uut = ast_json_load_string("{ \"one\": 1 }", NULL);
	ast_test_validate(test, NULL != uut);
	file = mkstemp_file(filename, "w");
	ast_test_validate(test, NULL != file);
	ast_test_validate(test, NULL == ast_json_dump_string(NULL));
	ast_test_validate(test, -1 == ast_json_dump_file(NULL, file));
	ast_test_validate(test, -1 == ast_json_dump_file(uut, NULL));
	ast_test_validate(test, -1 == ast_json_dump_file(NULL, NULL));
	ast_test_validate(test, -1 == ast_json_dump_new_file(uut, NULL));
	ast_test_validate(test, -1 == ast_json_dump_new_file(NULL, filename));
	ast_test_validate(test, -1 == ast_json_dump_new_file(NULL, NULL));
	ast_test_validate(test, NULL == ast_json_load_string(NULL, NULL));
	ast_test_validate(test, NULL == ast_json_load_buf(NULL, 0, NULL));
	ast_test_validate(test, NULL == ast_json_load_file(NULL, NULL));
	ast_test_validate(test, NULL == ast_json_load_new_file(NULL, NULL));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_parse_errors)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "parse_errors";
		info->category = CATEGORY;
		info->summary = "Testing various parse errors.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* parse errors */
	ast_test_validate(test, NULL == ast_json_load_string("'singleton'", NULL));
	ast_test_validate(test, NULL == ast_json_load_string("{ no value }", NULL));
	ast_test_validate(test, NULL == ast_json_load_string("{ 'no': 'curly' ", NULL));
	ast_test_validate(test, NULL == ast_json_load_string("[ 'no', 'square'", NULL));
	ast_test_validate(test, NULL == ast_json_load_string("{ 1: 'int key' }", NULL));
	ast_test_validate(test, NULL == ast_json_load_string("", NULL));
	ast_test_validate(test, NULL == ast_json_load_string("{ 'missing' 'colon' }", NULL));
	ast_test_validate(test, NULL == ast_json_load_string("[ 'missing' 'comma' ]", NULL));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_pack)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "pack";
		info->category = CATEGORY;
		info->summary = "Testing json_pack function.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* pack test */
	expected = ast_json_array_create();
	ast_json_array_append(expected, ast_json_array_create());
	ast_json_array_append(expected, ast_json_object_create());
	ast_json_array_append(ast_json_array_get(expected, 0), ast_json_integer_create(1));
	ast_json_array_append(ast_json_array_get(expected, 0), ast_json_integer_create(2));
	ast_json_object_set(ast_json_array_get(expected, 1), "cool", ast_json_true());
	uut = ast_json_pack("[[i,i],{s:b}]", 1, 2, "cool", 1);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_pack_ownership)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "pack_ownership";
		info->category = CATEGORY;
		info->summary = "Testing json_pack failure conditions.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_pack("[o]", ast_json_string_create("Am I freed?"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_pack_errors)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_alloc";
		info->category = CATEGORY;
		info->summary = "Testing json_pack failure conditions.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* pack errors */
	ast_test_validate(test, NULL == ast_json_pack(NULL));
	ast_test_validate(test, NULL == ast_json_pack("{s:i", "no curly", 911));
	ast_test_validate(test, NULL == ast_json_pack("[s, s", "no", "square"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_copy)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "copy";
		info->category = CATEGORY;
		info->summary = "Testing copying JSON.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* copy test */
	expected = ast_json_pack("{s: {s: i}}", "outer", "inner", 8675309);
	uut = ast_json_copy(expected);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_json_equal(expected, uut));
	ast_test_validate(test, ast_json_object_get(expected, "outer") == ast_json_object_get(uut, "outer"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_deep_copy)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "deep_copy";
		info->category = CATEGORY;
		info->summary = "Testing deep copying of JSON.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* deep copy test */
	expected = ast_json_pack("{s: {s: i}}", "outer", "inner", 8675309);
	uut = ast_json_deep_copy(expected);
	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, ast_json_equal(expected, uut));
	ast_test_validate(test, ast_json_object_get(expected, "outer") != ast_json_object_get(uut, "outer"));
	/* Changing the inner value of one should not change the other */
	ast_json_integer_set(ast_json_object_get(ast_json_object_get(uut, "outer"), "inner"), 411);
	ast_test_validate(test, !ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_copy_null)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "copy_null";
		info->category = CATEGORY;
		info->summary = "Testing NULL handling of copy functions.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* copy NULL */
	ast_test_validate(test, NULL == ast_json_copy(NULL));
	ast_test_validate(test, NULL == ast_json_deep_copy(NULL));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_circular_object)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "circular_object";
		info->category = CATEGORY;
		info->summary = "Object cannot be added to itself.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* circular reference testing */
	/* Cannot add self */
	uut = ast_json_object_create();
	uut_res = ast_json_object_set(uut, "myself", ast_json_ref(uut));
	ast_test_validate(test, -1 == uut_res);
	ast_test_validate(test, 0 == ast_json_object_size(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_circular_array)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "circular_array";
		info->category = CATEGORY;
		info->summary = "Array cannot be added to itself.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	uut = ast_json_array_create();
	ast_test_validate(test, 0 == ast_json_array_size(uut));
	uut_res = ast_json_array_append(uut, ast_json_ref(uut));
	ast_test_validate(test, -1 == uut_res);
	ast_test_validate(test, 0 == ast_json_array_size(uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_clever_circle)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, inner_child, NULL, ast_json_unref);
	RAII_VAR(char *, str, NULL, json_debug_free);
	int uut_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "clever_circle";
		info->category = CATEGORY;
		info->summary = "JSON with circular references cannot be encoded.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* can add to self if you're clever enough, but it should not encode */
	uut = ast_json_object_create();
	inner_child = ast_json_object_create();
	uut_res = ast_json_object_set(uut, "inner_child", ast_json_ref(inner_child));   /* incref to keep a reference */
	ast_test_validate(test, 0 == uut_res);
	uut_res = ast_json_object_set(inner_child, "parent", ast_json_ref(uut));   /* incref to keep a reference */
	ast_test_validate(test, 0 == uut_res);
	str = ast_json_dump_string(uut);
	ast_test_validate(test, NULL == str);
	/* Circular refs screw up reference counting, so break the cycle */
	ast_json_object_clear(inner_child);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_name_number)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "name_number";
		info->category = CATEGORY;
		info->summary = "JSON encoding of name/number pair.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, NULL == ast_json_name_number("name", NULL));
	ast_test_validate(test, NULL == ast_json_name_number(NULL, "1234"));
	ast_test_validate(test, NULL == ast_json_name_number(NULL, NULL));

	expected = ast_json_pack("{s: s, s: s}",
				 "name", "Jenny",
				 "number", "867-5309");
	uut = ast_json_name_number("Jenny", "867-5309");
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_timeval)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	struct timeval tv = {};

	switch (cmd) {
	case TEST_INIT:
		info->name = "timeval";
		info->category = CATEGORY;
		info->summary = "JSON encoding of timevals.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	expected = ast_json_string_create("2013-02-07T09:32:34.314-0600");

	tv.tv_sec = 1360251154;
	tv.tv_usec = 314159;
	uut = ast_json_timeval(tv, "America/Chicago");

	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(json_test_cep)
{
	RAII_VAR(struct ast_json *, uut, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "cep";
		info->category = CATEGORY;
		info->summary = "JSON with circular references cannot be encoded.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	expected = ast_json_pack("{s: o, s: o, s: o}",
				 "context", ast_json_null(),
				 "exten", ast_json_null(),
				 "priority", ast_json_null());
	uut = ast_json_dialplan_cep(NULL, NULL, -1);
	ast_test_validate(test, ast_json_equal(expected, uut));

	ast_json_unref(expected);
	ast_json_unref(uut);
	expected = ast_json_pack("{s: s, s: s, s: i}",
				 "context", "main",
				 "exten", "4321",
				 "priority", 7);
	uut = ast_json_dialplan_cep("main", "4321", 7);
	ast_test_validate(test, ast_json_equal(expected, uut));

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(json_test_false);
	AST_TEST_UNREGISTER(json_test_true);
	AST_TEST_UNREGISTER(json_test_bool0);
	AST_TEST_UNREGISTER(json_test_bool1);
	AST_TEST_UNREGISTER(json_test_null);
	AST_TEST_UNREGISTER(json_test_null_val);
	AST_TEST_UNREGISTER(json_test_string);
	AST_TEST_UNREGISTER(json_test_string_null);
	AST_TEST_UNREGISTER(json_test_stringf);
	AST_TEST_UNREGISTER(json_test_int);
	AST_TEST_UNREGISTER(json_test_non_int);
	AST_TEST_UNREGISTER(json_test_array_create);
	AST_TEST_UNREGISTER(json_test_array_append);
	AST_TEST_UNREGISTER(json_test_array_inset);
	AST_TEST_UNREGISTER(json_test_array_set);
	AST_TEST_UNREGISTER(json_test_array_remove);
	AST_TEST_UNREGISTER(json_test_array_clear);
	AST_TEST_UNREGISTER(json_test_array_extend);
	AST_TEST_UNREGISTER(json_test_array_null);
	AST_TEST_UNREGISTER(json_test_object_alloc);
	AST_TEST_UNREGISTER(json_test_object_set);
	AST_TEST_UNREGISTER(json_test_object_set_overwrite);
	AST_TEST_UNREGISTER(json_test_object_get);
	AST_TEST_UNREGISTER(json_test_object_del);
	AST_TEST_UNREGISTER(json_test_object_clear);
	AST_TEST_UNREGISTER(json_test_object_merge_all);
	AST_TEST_UNREGISTER(json_test_object_merge_existing);
	AST_TEST_UNREGISTER(json_test_object_merge_missing);
	AST_TEST_UNREGISTER(json_test_object_null);
	AST_TEST_UNREGISTER(json_test_object_iter);
	AST_TEST_UNREGISTER(json_test_object_iter_null);
	AST_TEST_UNREGISTER(json_test_dump_load_string);
	AST_TEST_UNREGISTER(json_test_dump_load_str);
	AST_TEST_UNREGISTER(json_test_dump_str_fail);
	AST_TEST_UNREGISTER(json_test_load_buffer);
	AST_TEST_UNREGISTER(json_test_dump_load_file);
	AST_TEST_UNREGISTER(json_test_dump_load_new_file);
	AST_TEST_UNREGISTER(json_test_dump_load_null);
	AST_TEST_UNREGISTER(json_test_parse_errors);
	AST_TEST_UNREGISTER(json_test_pack);
	AST_TEST_UNREGISTER(json_test_pack_ownership);
	AST_TEST_UNREGISTER(json_test_pack_errors);
	AST_TEST_UNREGISTER(json_test_copy);
	AST_TEST_UNREGISTER(json_test_deep_copy);
	AST_TEST_UNREGISTER(json_test_copy_null);
	AST_TEST_UNREGISTER(json_test_circular_object);
	AST_TEST_UNREGISTER(json_test_circular_array);
	AST_TEST_UNREGISTER(json_test_clever_circle);
	AST_TEST_UNREGISTER(json_test_name_number);
	AST_TEST_UNREGISTER(json_test_timeval);
	AST_TEST_UNREGISTER(json_test_cep);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(json_test_false);
	AST_TEST_REGISTER(json_test_true);
	AST_TEST_REGISTER(json_test_bool0);
	AST_TEST_REGISTER(json_test_bool1);
	AST_TEST_REGISTER(json_test_null);
	AST_TEST_REGISTER(json_test_null_val);
	AST_TEST_REGISTER(json_test_string);
	AST_TEST_REGISTER(json_test_string_null);
	AST_TEST_REGISTER(json_test_stringf);
	AST_TEST_REGISTER(json_test_int);
	AST_TEST_REGISTER(json_test_non_int);
	AST_TEST_REGISTER(json_test_array_create);
	AST_TEST_REGISTER(json_test_array_append);
	AST_TEST_REGISTER(json_test_array_inset);
	AST_TEST_REGISTER(json_test_array_set);
	AST_TEST_REGISTER(json_test_array_remove);
	AST_TEST_REGISTER(json_test_array_clear);
	AST_TEST_REGISTER(json_test_array_extend);
	AST_TEST_REGISTER(json_test_array_null);
	AST_TEST_REGISTER(json_test_object_alloc);
	AST_TEST_REGISTER(json_test_object_set);
	AST_TEST_REGISTER(json_test_object_set_overwrite);
	AST_TEST_REGISTER(json_test_object_get);
	AST_TEST_REGISTER(json_test_object_del);
	AST_TEST_REGISTER(json_test_object_clear);
	AST_TEST_REGISTER(json_test_object_merge_all);
	AST_TEST_REGISTER(json_test_object_merge_existing);
	AST_TEST_REGISTER(json_test_object_merge_missing);
	AST_TEST_REGISTER(json_test_object_null);
	AST_TEST_REGISTER(json_test_object_iter);
	AST_TEST_REGISTER(json_test_object_iter_null);
	AST_TEST_REGISTER(json_test_dump_load_string);
	AST_TEST_REGISTER(json_test_dump_load_str);
	AST_TEST_REGISTER(json_test_dump_str_fail);
	AST_TEST_REGISTER(json_test_load_buffer);
	AST_TEST_REGISTER(json_test_dump_load_file);
	AST_TEST_REGISTER(json_test_dump_load_new_file);
	AST_TEST_REGISTER(json_test_dump_load_null);
	AST_TEST_REGISTER(json_test_parse_errors);
	AST_TEST_REGISTER(json_test_pack);
	AST_TEST_REGISTER(json_test_pack_ownership);
	AST_TEST_REGISTER(json_test_pack_errors);
	AST_TEST_REGISTER(json_test_copy);
	AST_TEST_REGISTER(json_test_deep_copy);
	AST_TEST_REGISTER(json_test_copy_null);
	AST_TEST_REGISTER(json_test_circular_object);
	AST_TEST_REGISTER(json_test_circular_array);
	AST_TEST_REGISTER(json_test_clever_circle);
	AST_TEST_REGISTER(json_test_name_number);
	AST_TEST_REGISTER(json_test_timeval);
	AST_TEST_REGISTER(json_test_cep);

	ast_test_register_init(CATEGORY, json_test_init);
	ast_test_register_cleanup(CATEGORY, json_test_cleanup);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, 0, "JSON testing",
		.load = load_module,
		.unload = unload_module);
