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
	<depend>res_json</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")
#include "asterisk/json.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

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
	void *p = ast_malloc(size);
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
	ast_free(p);
}
/*!@}*/

AST_TEST_DEFINE(json_test)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_json *uut = NULL;
	struct ast_json *expected = NULL;
	struct ast_json *tail = NULL;
	struct ast_json *merge = NULL;
	struct ast_json *inner_child = NULL;
	struct ast_json_iter *iter = NULL;
	int uut_res = 0;
	int count = 0;
	char *str = NULL;
	FILE *file = NULL;
	char *filename = NULL;
	struct ast_str *astr = NULL;

	auto void clean_vars(void);
	/*!
	 * Free all local variables and set to NULL. This is an inner function to give a
	 * decent backtrace if a free() fails.
	 */
	void clean_vars(void) {
		ast_json_unref(uut);
		ast_json_unref(expected);
		ast_json_unref(tail);
		ast_json_unref(merge);
		ast_json_unref(inner_child);
		uut = expected = tail = merge = inner_child = NULL;
		iter = NULL;
		uut_res = count = 0;
		json_debug_free(str);
		str = NULL;
		if (file) fclose(file);
		file = NULL;
		free(filename);
		filename = NULL;
		astr = NULL;
	}

	/*!
	 * Macro to free all local variables, and check for memory leaks. This is a macro
	 * so that ast_test_check() can get correct line numbers.
	 */
#define CLEAN_VARS() do {					\
		clean_vars();					\
		ast_test_check(res, 0 == alloc_count);		\
		/* reset alloc_count to prevent false positives */	\
		alloc_count = 0;				\
	} while (0)

	switch (cmd) {
	case TEST_INIT:
		info->name = "json";
		info->category = "/main/json/";
		info->summary = "Testing JSON abstraction library.";
		info->description = "Test JSON abstraction library.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Setup some special malloc tracking functions. */
	ast_json_set_alloc_funcs(json_debug_malloc, json_debug_free);

	/* incref and decref should be NULL safe */
	ast_json_ref(NULL);
	ast_json_unref(NULL);
	CLEAN_VARS();
	CLEAN_VARS();

	/* Ridiculous looking tests for the fundamentals */
	uut = ast_json_false();
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_FALSE == ast_json_typeof(uut));
	ast_test_check(res, !ast_json_is_null(uut));
	ast_test_check(res, !ast_json_is_true(uut));
	ast_test_check(res, ast_json_is_false(uut));
	CLEAN_VARS();

	uut = ast_json_true();
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_TRUE == ast_json_typeof(uut));
	ast_test_check(res, !ast_json_is_null(uut));
	ast_test_check(res, ast_json_is_true(uut));
	ast_test_check(res, !ast_json_is_false(uut));
	CLEAN_VARS();

	uut = ast_json_boolean(0);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_FALSE == ast_json_typeof(uut));
	ast_test_check(res, !ast_json_is_null(uut));
	ast_test_check(res, !ast_json_is_true(uut));
	ast_test_check(res, ast_json_is_false(uut));
	ast_test_check(res, ast_json_equal(uut, ast_json_false()));
	ast_test_check(res, !ast_json_equal(uut, ast_json_true()));
	CLEAN_VARS();

	uut = ast_json_boolean(1);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_TRUE == ast_json_typeof(uut));
	ast_test_check(res, !ast_json_is_null(uut));
	ast_test_check(res, ast_json_is_true(uut));
	ast_test_check(res, !ast_json_is_false(uut));
	ast_test_check(res, !ast_json_equal(uut, ast_json_false()));
	ast_test_check(res, ast_json_equal(uut, ast_json_true()));
	CLEAN_VARS();

	uut = ast_json_null();
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_NULL == ast_json_typeof(uut));
	ast_test_check(res, ast_json_is_null(uut));
	ast_test_check(res, !ast_json_is_true(uut));
	ast_test_check(res, !ast_json_is_false(uut));
	CLEAN_VARS();

	/* NULL isn't null, true or false */
	ast_test_check(res, !ast_json_is_null(NULL));
	ast_test_check(res, !ast_json_is_false(NULL));
	ast_test_check(res, !ast_json_is_true(NULL));

	/* Basic string tests */
	uut = ast_json_string_create("Hello, json");
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_STRING == ast_json_typeof(uut));
	ast_test_check(res, 0 == strcmp("Hello, json", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, NULL);
	ast_test_check(res, -1 == uut_res);
	ast_test_check(res, 0 == strcmp("Hello, json", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, "Not UTF-8 - \xff");
	ast_test_check(res, -1 == uut_res);
	ast_test_check(res, 0 == strcmp("Hello, json", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, "Is UTF-8 - \xE2\x98\xBA");
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 0 == strcmp("Is UTF-8 - \xE2\x98\xBA", ast_json_string_get(uut)));

	uut_res = ast_json_string_set(uut, "Goodbye, json");
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 0 == strcmp("Goodbye, json", ast_json_string_get(uut)));
	CLEAN_VARS();

	/* formatted string */
	uut = ast_json_stringf("Hello, %s", "json");
	expected = ast_json_string_create("Hello, json");
	ast_test_check(res, NULL != uut);
	ast_test_check(res, ast_json_equal(expected, uut));
	CLEAN_VARS();

	/* Non-UTF-8 strings are invalid */
	uut = ast_json_stringf("Not UTF-8 - %s", "\xff");
	ast_test_check(res, NULL == uut);
	CLEAN_VARS();

	/* NULL string */
	uut = ast_json_string_create(NULL);
	ast_test_check(res, NULL == uut);
	CLEAN_VARS();

	/* NULL format string */
	uut = ast_json_stringf(NULL);
	ast_test_check(res, NULL == uut);
	CLEAN_VARS();

	/* NULL JSON strings */
	ast_test_check(res, NULL == ast_json_string_create(NULL));
	ast_test_check(res, NULL == ast_json_string_get(NULL));
	ast_test_check(res, -1 == ast_json_string_set(NULL, "not null"));

	/* string_value from non-string elements should return NULL */
	ast_test_check(res, NULL == ast_json_string_get(ast_json_null()));
	ast_test_check(res, NULL == ast_json_string_get(ast_json_false()));
	ast_test_check(res, NULL == ast_json_string_get(ast_json_true()));

	/* Integer tests */
	uut = ast_json_integer_create(0);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_INTEGER == ast_json_typeof(uut));
	ast_test_check(res, 0 == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, 1);
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 1 == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, -1);
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, -1 == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, LLONG_MAX);
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, LLONG_MAX == ast_json_integer_get(uut));

	uut_res = ast_json_integer_set(uut, LLONG_MIN);
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, LLONG_MIN == ast_json_integer_get(uut));
	CLEAN_VARS();

	/* Non-ints return 0 integer value */
	ast_test_check(res, 0 == ast_json_integer_get(ast_json_null()));
	ast_test_check(res, 0 == ast_json_integer_get(ast_json_true()));
	ast_test_check(res, 0 == ast_json_integer_get(ast_json_false()));

	/* JSON NULL integers */
	ast_test_check(res, 0 == ast_json_integer_get(NULL));
	ast_test_check(res, -1 == ast_json_integer_set(NULL, 911));
	ast_test_check(res, 0 == ast_json_array_size(NULL));

	/* No magical parsing of strings into ints */
	uut = ast_json_string_create("314");
	ast_test_check(res, 0 == ast_json_integer_get(uut));
	CLEAN_VARS();

	/* Or vice-versa */
	uut = ast_json_integer_create(314);
	ast_test_check(res, NULL == ast_json_string_get(uut));
	CLEAN_VARS();

	/* array creation */
	uut = ast_json_array_create();
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_ARRAY == ast_json_typeof(uut));
	ast_test_check(res, 0 == ast_json_array_size(uut));
	CLEAN_VARS();

	/* array append */
	uut = ast_json_array_create();
	uut_res = ast_json_array_append(uut, ast_json_string_create("one"));
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 1 == ast_json_array_size(uut));
	ast_test_check(res, 0 == strcmp("one", ast_json_string_get(ast_json_array_get(uut, 0))));
	/* index out of range */
	ast_test_check(res, NULL == ast_json_array_get(uut, 1));
	ast_test_check(res, NULL == ast_json_array_get(uut, -1));
	CLEAN_VARS();

	/* array insert */
	uut = ast_json_pack("[s]", "one");
	uut_res = ast_json_array_insert(uut, 0, ast_json_string_create("zero"));
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 2 == ast_json_array_size(uut));
	ast_test_check(res, 0 == strcmp("zero", ast_json_string_get(ast_json_array_get(uut, 0))));
	ast_test_check(res, 0 == strcmp("one", ast_json_string_get(ast_json_array_get(uut, 1))));
	CLEAN_VARS();

	/* array set */
	uut = ast_json_pack("[s, s]", "zero", "one");
	uut_res = ast_json_array_set(uut, 1, ast_json_integer_create(1));
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 2 == ast_json_array_size(uut));
	ast_test_check(res, 0 == strcmp("zero", ast_json_string_get(ast_json_array_get(uut, 0))));
	ast_test_check(res, 1 == ast_json_integer_get(ast_json_array_get(uut, 1)));
	CLEAN_VARS();

	/* array remove */
	uut = ast_json_array_create();
	uut_res = ast_json_array_append(uut, ast_json_string_create("zero"));
	uut_res = ast_json_array_append(uut, ast_json_integer_create(1));
	uut_res = ast_json_array_remove(uut, 0);
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 1 == ast_json_array_size(uut));
	ast_test_check(res, 1 == ast_json_integer_get(ast_json_array_get(uut, 0)));
	CLEAN_VARS();

	/* array clear */
	uut = ast_json_array_create();
	uut_res = ast_json_array_append(uut, ast_json_string_create("zero"));
	uut_res = ast_json_array_append(uut, ast_json_integer_create(1));
	uut_res = ast_json_array_clear(uut);
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 0 == ast_json_array_size(uut));
	CLEAN_VARS();

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
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, ast_json_equal(expected, uut));
	/* tail is preserved */
	ast_test_check(res, 3 == ast_json_array_size(tail));
	CLEAN_VARS();

	/* array NULL checks */
	ast_test_check(res, 0 == ast_json_array_size(NULL));
	ast_test_check(res, NULL == ast_json_array_get(NULL, 0));
	ast_test_check(res, -1 == ast_json_array_set(NULL, 0, ast_json_null()));
	ast_test_check(res, -1 == ast_json_array_append(NULL, ast_json_null()));
	ast_test_check(res, -1 == ast_json_array_insert(NULL, 0, ast_json_null()));
	ast_test_check(res, -1 == ast_json_array_remove(NULL, 0));
	ast_test_check(res, -1 == ast_json_array_clear(NULL));
	uut = ast_json_array_create();
	ast_test_check(res, -1 == ast_json_array_extend(uut, NULL));
	ast_test_check(res, -1 == ast_json_array_extend(NULL, uut));
	ast_test_check(res, -1 == ast_json_array_extend(NULL, NULL));
	CLEAN_VARS();

	/* object allocation */
	uut = ast_json_object_create();
	ast_test_check(res, NULL != uut);
	ast_test_check(res, AST_JSON_OBJECT == ast_json_typeof(uut));
	ast_test_check(res, 0 == ast_json_object_size(uut));
	CLEAN_VARS();

	/* object set */
	expected = ast_json_pack("{s: i, s: i, s: i}", "one", 1, "two", 2, "three", 3);
	uut = ast_json_object_create();
	uut_res = ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_test_check(res, 0 == uut_res);
	uut_res = ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_test_check(res, 0 == uut_res);
	uut_res = ast_json_object_set(uut, "three", ast_json_integer_create(3));
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, ast_json_equal(expected, uut));
	ast_test_check(res, NULL == ast_json_object_get(uut, "dne"));
	CLEAN_VARS();

	/* object set existing */
	uut = ast_json_pack("{s: i, s: i, s: i}", "one", 1, "two", 2, "three", 3);
	uut_res = ast_json_object_set(uut, "two", ast_json_integer_create(-2));
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, -2 == ast_json_integer_get(ast_json_object_get(uut, "two")));
	CLEAN_VARS();

	/* object get */
	uut = ast_json_pack("{s: i, s: i, s: i}", "one", 1, "two", 2, "three", 3);
	ast_test_check(res, 2 == ast_json_integer_get(ast_json_object_get(uut, "two")));
	ast_test_check(res, NULL == ast_json_object_get(uut, "dne"));
	ast_test_check(res, NULL == ast_json_object_get(uut, NULL));
	CLEAN_VARS();

	/* object del */
	expected = ast_json_object_create();
	uut = ast_json_object_create();
	uut_res = ast_json_object_set(uut, "one", ast_json_integer_create(1));
	uut_res = ast_json_object_del(uut, "one");
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, ast_json_equal(expected, uut));
	uut_res = ast_json_object_del(uut, "dne");
	ast_test_check(res, -1 == uut_res);
	CLEAN_VARS();

	/* object clear */
	uut = ast_json_object_create();
	ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_json_object_set(uut, "three", ast_json_integer_create(3));
	uut_res = ast_json_object_clear(uut);
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, 0 == ast_json_object_size(uut));
	CLEAN_VARS();

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
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, ast_json_equal(expected, uut));
	/* merge object is untouched */
	ast_test_check(res, 3 == ast_json_object_size(merge));
	CLEAN_VARS();

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
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, ast_json_equal(expected, uut));
	/* merge object is untouched */
	ast_test_check(res, 3 == ast_json_object_size(merge));
	CLEAN_VARS();

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
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, ast_json_equal(expected, uut));
	/* merge object is untouched */
	ast_test_check(res, 3 == ast_json_object_size(merge));
	CLEAN_VARS();

	/* Object NULL testing */
	ast_test_check(res, 0 == ast_json_object_size(NULL));
	ast_test_check(res, NULL == ast_json_object_get(NULL, "not null"));
	ast_test_check(res, -1 == ast_json_object_set(NULL, "not null", ast_json_null()));
	ast_test_check(res, -1 == ast_json_object_del(NULL, "not null"));
	ast_test_check(res, -1 == ast_json_object_clear(NULL));
	uut = ast_json_object_create();
	ast_test_check(res, -1 == ast_json_object_update(NULL, uut));
	ast_test_check(res, -1 == ast_json_object_update(uut, NULL));
	ast_test_check(res, -1 == ast_json_object_update(NULL, NULL));
	ast_test_check(res, -1 == ast_json_object_update_existing(NULL, uut));
	ast_test_check(res, -1 == ast_json_object_update_existing(uut, NULL));
	ast_test_check(res, -1 == ast_json_object_update_existing(NULL, NULL));
	ast_test_check(res, -1 == ast_json_object_update_missing(NULL, uut));
	ast_test_check(res, -1 == ast_json_object_update_missing(uut, NULL));
	ast_test_check(res, -1 == ast_json_object_update_missing(NULL, NULL));
	CLEAN_VARS();

	/* Object iterator testing */
	uut = ast_json_object_create();
	ast_json_object_set(uut, "one", ast_json_integer_create(1));
	ast_json_object_set(uut, "two", ast_json_integer_create(2));
	ast_json_object_set(uut, "three", ast_json_integer_create(3));
	ast_json_object_set(uut, "four", ast_json_integer_create(4));
	ast_json_object_set(uut, "five", ast_json_integer_create(5));

	/* Iterate through the object; be aware that order isn't specified */
	iter = ast_json_object_iter(uut);
	ast_test_check(res, NULL != iter);
	while (NULL != iter) {
		if (0 == strcmp("one", ast_json_object_iter_key(iter))) {
			ast_test_check(res, 1 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("two", ast_json_object_iter_key(iter))) {
			ast_test_check(res, 2 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("three", ast_json_object_iter_key(iter))) {
			ast_test_check(res, 3 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("four", ast_json_object_iter_key(iter))) {
			ast_test_check(res, 4 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else if (0 == strcmp("five", ast_json_object_iter_key(iter))) {
			ast_test_check(res, 5 == ast_json_integer_get(ast_json_object_iter_value(iter)));
		} else {
			/* Unexpected key */
			ast_test_check(res, 0);
		}
		iter = ast_json_object_iter_next(uut, iter);
		++count;
	}
	ast_test_check(res, 5 == count);

	/* iterator non-existing key */
	iter = ast_json_object_iter_at(uut, "dne");
	ast_test_check(res, NULL == iter);

	/* iterator specific key */
	iter = ast_json_object_iter_at(uut, "three");
	ast_test_check(res, NULL != iter);
	ast_test_check(res, 3 == ast_json_integer_get(ast_json_object_iter_value(iter)));

	/* set via iter */
	iter = ast_json_object_iter_at(uut, "three");
	uut_res = ast_json_object_iter_set(uut, iter, ast_json_integer_create(-3));
	ast_test_check(res, 0 == uut_res);
	ast_test_check(res, -3 == ast_json_integer_get(ast_json_object_get(uut, "three")));
	CLEAN_VARS();

	/* iterator NULL tests */
	uut = ast_json_object_create();
	ast_test_check(res, NULL == ast_json_object_iter(NULL));
	ast_test_check(res, NULL == ast_json_object_iter_at(NULL, "not null"));
	ast_test_check(res, NULL == ast_json_object_iter_next(NULL, NULL));
	ast_test_check(res, NULL == ast_json_object_iter_next(uut, NULL));
	ast_test_check(res, NULL == ast_json_object_iter_key(NULL));
	ast_test_check(res, NULL == ast_json_object_iter_value(NULL));
	ast_test_check(res, -1 == ast_json_object_iter_set(NULL, NULL, ast_json_null()));
	ast_test_check(res, -1 == ast_json_object_iter_set(uut, NULL, ast_json_null()));
	CLEAN_VARS();

	/* dump/load string */
	expected = ast_json_pack("{ s: i }", "one", 1);
	str = ast_json_dump_string(expected);
	ast_test_check(res, NULL != str);
	uut = ast_json_load_string(str, NULL);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, ast_json_equal(expected, uut));
	CLEAN_VARS();

	/* dump_string NULL */
	ast_test_check(res, NULL == ast_json_dump_string(NULL));

	/* dump/load ast_str */
	expected = ast_json_pack("{ s: i }", "one", 1);
	astr = ast_str_create(1); /* should expand to hold output */
	uut_res = ast_json_dump_str(expected, &astr);
	ast_test_check(res, 0 == uut_res);
	uut = ast_json_load_str(astr, NULL);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, ast_json_equal(expected, uut));
	ast_free(astr);
	CLEAN_VARS();

	/* dump ast_str growth failure */
	expected = ast_json_pack("{ s: i }", "one", 1);
	astr = ast_str_alloca(1); /* cannot grow */
	uut_res = ast_json_dump_str(expected, &astr);
	ast_test_check(res, 0 != uut_res);
	CLEAN_VARS();

	/* load buffer */
	str = "{ \"one\": 1 } trailing garbage";
	uut = ast_json_load_string(str, NULL);
	ast_test_check(res, NULL == uut);
	uut = ast_json_load_buf(str, strlen("{ \"one\": 1 }"), NULL);
	ast_test_check(res, NULL != uut);
	str = NULL;
	CLEAN_VARS();

	/* dump/load file */
	expected = ast_json_pack("{ s: i }", "one", 1);
	filename = tempnam(NULL, "ast-json");
	file = fopen(filename, "w");
	uut_res = ast_json_dump_file(expected, file);
	ast_test_check(res, 0 == uut_res);
	fclose(file);
	file = fopen(filename, "r");
	uut = ast_json_load_file(file, NULL);
	ast_test_check(res, ast_json_equal(expected, uut));
	CLEAN_VARS();

	/* dump/load filename */
	expected = ast_json_pack("{ s: i }", "one", 1);
	filename = tempnam(NULL, "ast-json");
	uut_res = ast_json_dump_new_file(expected, filename);
	ast_test_check(res, 0 == uut_res);
	uut = ast_json_load_new_file(filename, NULL);
	ast_test_check(res, ast_json_equal(expected, uut));
	CLEAN_VARS();

	/* dump/load NULL tests */
	uut = ast_json_load_string("{ \"one\": 1 }", NULL);
	ast_test_check(res, NULL != uut);
	filename = tempnam(NULL, "ast-json");
	file = fopen(filename, "w");
	ast_test_check(res, NULL == ast_json_dump_string(NULL));
	ast_test_check(res, -1 == ast_json_dump_file(NULL, file));
	ast_test_check(res, -1 == ast_json_dump_file(uut, NULL));
	ast_test_check(res, -1 == ast_json_dump_file(NULL, NULL));
	ast_test_check(res, -1 == ast_json_dump_new_file(uut, NULL));
	ast_test_check(res, -1 == ast_json_dump_new_file(NULL, filename));
	ast_test_check(res, -1 == ast_json_dump_new_file(NULL, NULL));
	ast_test_check(res, NULL == ast_json_load_string(NULL, NULL));
	ast_test_check(res, NULL == ast_json_load_buf(NULL, 0, NULL));
	ast_test_check(res, NULL == ast_json_load_file(NULL, NULL));
	ast_test_check(res, NULL == ast_json_load_new_file(NULL, NULL));
	CLEAN_VARS();

	/* parse errors */
	ast_test_check(res, NULL == ast_json_load_string("'singleton'", NULL));
	ast_test_check(res, NULL == ast_json_load_string("{ no value }", NULL));
	ast_test_check(res, NULL == ast_json_load_string("{ 'no': 'curly' ", NULL));
	ast_test_check(res, NULL == ast_json_load_string("[ 'no', 'square'", NULL));
	ast_test_check(res, NULL == ast_json_load_string("{ 1: 'int key' }", NULL));
	ast_test_check(res, NULL == ast_json_load_string("", NULL));
	ast_test_check(res, NULL == ast_json_load_string("{ 'missing' 'colon' }", NULL));
	ast_test_check(res, NULL == ast_json_load_string("[ 'missing' 'comma' ]", NULL));

	/* pack test */
	expected = ast_json_array_create();
	ast_json_array_append(expected, ast_json_array_create());
	ast_json_array_append(expected, ast_json_object_create());
	ast_json_array_append(ast_json_array_get(expected, 0), ast_json_integer_create(1));
	ast_json_array_append(ast_json_array_get(expected, 0), ast_json_integer_create(2));
	ast_json_object_set(ast_json_array_get(expected, 1), "cool", ast_json_true());
	uut = ast_json_pack("[[i,i],{s:b}]", 1, 2, "cool", 1);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, ast_json_equal(expected, uut));
	CLEAN_VARS();

	/* pack errors */
	ast_test_check(res, NULL == ast_json_pack(NULL));
	ast_test_check(res, NULL == ast_json_pack("{s:i", "no curly", 911));
	ast_test_check(res, NULL == ast_json_pack("[s, s", "no", "square"));

	/* copy test */
	expected = ast_json_pack("{s: {s: i}}", "outer", "inner", 8675309);
	uut = ast_json_copy(expected);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, ast_json_equal(expected, uut));
	ast_test_check(res, ast_json_object_get(expected, "outer") == ast_json_object_get(uut, "outer"));
	CLEAN_VARS();

	/* deep copy test */
	expected = ast_json_pack("{s: {s: i}}", "outer", "inner", 8675309);
	uut = ast_json_deep_copy(expected);
	ast_test_check(res, NULL != uut);
	ast_test_check(res, ast_json_equal(expected, uut));
	ast_test_check(res, ast_json_object_get(expected, "outer") != ast_json_object_get(uut, "outer"));
	/* Changing the inner value of one should not change the other */
	ast_json_integer_set(ast_json_object_get(ast_json_object_get(uut, "outer"), "inner"), 411);
	ast_test_check(res, !ast_json_equal(expected, uut));
	CLEAN_VARS();

	/* copy NULL */
	ast_test_check(res, NULL == ast_json_copy(NULL));
	ast_test_check(res, NULL == ast_json_deep_copy(NULL));

	/* circular reference testing */
	/* Cannot add self */
	uut = ast_json_object_create();
	uut_res = ast_json_object_set(uut, "myself", uut);
	ast_test_check(res, -1 == uut_res);
	ast_test_check(res, 0 == ast_json_object_size(uut));
	CLEAN_VARS();

	uut = ast_json_array_create();
	uut_res = ast_json_object_set(uut, "myself", uut);
	ast_test_check(res, -1 == uut_res);
	ast_test_check(res, 0 == ast_json_array_size(uut));
	CLEAN_VARS();

	/* can add to self if you're clever enough, but it should not encode */
	uut = ast_json_object_create();
	inner_child = ast_json_object_create();
	uut_res = ast_json_object_set(uut, "inner_child", ast_json_ref(inner_child));   /* incref to keep a reference */
	ast_test_check(res, 0 == uut_res);
	uut_res = ast_json_object_set(inner_child, "parent", ast_json_ref(uut));   /* incref to keep a reference */
	ast_test_check(res, 0 == uut_res);
	str = ast_json_dump_string(uut);
	ast_test_check(res, NULL == str);
	/* Circular refs screw up reference counting, so break the cycle */
	ast_json_object_clear(inner_child);
	CLEAN_VARS();

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(json_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(json_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "JSON testing.");
