/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Vector tests
 *
 * \author George Joseph <george.joseph@fairview5.com>
 *
 * This module will run some vector tests.
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/module.h"
#include "asterisk/vector.h"

static int cleanup_count;

static void cleanup(char *element)
{
	cleanup_count++;
}

AST_TEST_DEFINE(basic_ops)
{
	AST_VECTOR(test_struct, char *) sv1;
	int rc = AST_TEST_PASS;

	char *AAA = "AAA";
	char *BBB = "BBB";
	char *CCC = "CCC";
	char *ZZZ = "ZZZ";

	switch (cmd) {
	case TEST_INIT:
		info->name = "basic";
		info->category = "/main/vector/";
		info->summary = "Test vector basic ops";
		info->description = "Test vector basic ops";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, AST_VECTOR_INIT(&sv1, 3) == 0);
	ast_test_validate_cleanup(test, sv1.max == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 0, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, AAA) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, BBB) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, CCC) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);
	ast_test_validate_cleanup(test, sv1.max == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == BBB, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_INSERT_AT(&sv1, 1, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, sv1.max >= 4, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 4, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == BBB, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 3) == CCC, rc, cleanup);

	ast_test_validate_cleanup(test, *(char **)AST_VECTOR_GET_CMP(&sv1, "AAA", 0 == strcmp) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, *(char **)AST_VECTOR_GET_CMP(&sv1, "ZZZ", 0 == strcmp) == ZZZ, rc, cleanup);

	AST_VECTOR_FREE(&sv1);
	ast_test_validate(test, sv1.elems == NULL);
	ast_test_validate(test, sv1.current == 0);
	ast_test_validate(test, sv1.max == 0);

	ast_test_validate(test, AST_VECTOR_INIT(&sv1, 0) == 0);
	ast_test_validate_cleanup(test, sv1.max == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 0, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, AAA) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, BBB) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, CCC) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, sv1.max >= 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == BBB, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	/* Overwrite index 1 */
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 1, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	/* Remove index 0 and bring the last entry into it's empty slot */
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_UNORDERED(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 2, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);

	/* Replace 0 and 2 leaving 1 alone */
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 0, AAA) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 2, CCC) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	/* Remove 1 and compact preserving order */
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_ORDERED(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 2, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == CCC, rc, cleanup);

	/* Equivalent of APPEND */
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 2, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);

	/* This should fail because comparison is by pointer */
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_ELEM_ORDERED(&sv1, "ZZZ", cleanup) != 0, rc, cleanup);

	/* This should work because we passing in the specific object to be removed */
	cleanup_count = 0;
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_ELEM_ORDERED(&sv1, ZZZ, cleanup) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 2, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, cleanup_count == 1, rc, cleanup);

	/* If we want a comparison by value, we need to pass in a comparison
	 * function.  The comparison looks weird but that's what it takes.
	 */
	cleanup_count = 0;
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_CMP_ORDERED(&sv1, "AAA", 0 == strcmp, cleanup) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 1, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, cleanup_count == 1, rc, cleanup);

	/* This element is gone so we shouldn't be able to find it or delete it again. */
	ast_test_validate_cleanup(test, AST_VECTOR_GET_CMP(&sv1, "AAA", 0 == strcmp) == NULL, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_CMP_ORDERED(&sv1, "AAA", 0 == strcmp, cleanup) != 0, rc, cleanup);

	/* CCC should still be there though */
	ast_test_validate_cleanup(test, *(char **)AST_VECTOR_GET_CMP(&sv1, "CCC", 0 == strcmp) == CCC, rc, cleanup);

	/* Make sure we can insert at beginning and end and that growing works */
	ast_test_validate_cleanup(test, AST_VECTOR_INSERT_AT(&sv1, 0, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_INSERT_AT(&sv1, AST_VECTOR_SIZE(&sv1), BBB) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == BBB, rc, cleanup);

	AST_VECTOR_FREE(&sv1);
	ast_test_validate(test, AST_VECTOR_INIT(&sv1, 0) == 0);
	ast_test_validate_cleanup(test, AST_VECTOR_INSERT_AT(&sv1, 0, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == ZZZ, rc, cleanup);

	AST_VECTOR_FREE(&sv1);
	ast_test_validate(test, AST_VECTOR_INIT(&sv1, 0) == 0);
	ast_test_validate_cleanup(test, AST_VECTOR_INSERT_AT(&sv1, AST_VECTOR_SIZE(&sv1), ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_INSERT_AT(&sv1, AST_VECTOR_SIZE(&sv1), CCC) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == CCC, rc, cleanup);

cleanup:
	AST_VECTOR_FREE(&sv1);
	return rc;
}

static void cleanup_int(int element)
{
	cleanup_count++;
}

AST_TEST_DEFINE(basic_ops_integer)
{
	AST_VECTOR(test_struct, int) sv1;
	int rc = AST_TEST_PASS;

	int AAA = 1;
	int BBB = 2;
	int CCC = 3;
	int ZZZ = 26;

	switch (cmd) {
	case TEST_INIT:
		info->name = "basic integer";
		info->category = "/main/vector/";
		info->summary = "Test integer vector basic ops";
		info->description = "Test integer vector basic ops";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, AST_VECTOR_INIT(&sv1, 3) == 0);
	ast_test_validate_cleanup(test, sv1.max == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 0, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, AAA) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, BBB) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, CCC) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);
	ast_test_validate_cleanup(test, sv1.max == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == BBB, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_INSERT_AT(&sv1, 1, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, sv1.max >= 4, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 4, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == BBB, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 3) == CCC, rc, cleanup);

	ast_test_validate_cleanup(test, *(int *)AST_VECTOR_GET_CMP(&sv1, AAA,  AST_VECTOR_ELEM_DEFAULT_CMP) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, *(int *)AST_VECTOR_GET_CMP(&sv1, ZZZ, AST_VECTOR_ELEM_DEFAULT_CMP) == ZZZ, rc, cleanup);

	AST_VECTOR_FREE(&sv1);
	ast_test_validate(test, sv1.elems == NULL);
	ast_test_validate(test, sv1.current == 0);
	ast_test_validate(test, sv1.max == 0);

	ast_test_validate(test, AST_VECTOR_INIT(&sv1, 0) == 0);
	ast_test_validate_cleanup(test, sv1.max == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 0, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, AAA) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, BBB) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_APPEND(&sv1, CCC) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, sv1.max >= 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == BBB, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	/* Overwrite index 1 */
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 1, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	/* Remove index 0 and bring the last entry into it's empty slot */
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_UNORDERED(&sv1, 0) == 1, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 2, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);

	/* Replace 0 and 2 leaving 1 alone */
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 0, AAA) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 2, CCC) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 2) == CCC, rc, cleanup);

	/* Remove 1 and compact preserving order */
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_ORDERED(&sv1, 1) == ZZZ, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 2, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == CCC, rc, cleanup);

	/* Equivalent of APPEND */
	ast_test_validate_cleanup(test, AST_VECTOR_REPLACE(&sv1, 2, ZZZ) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 3, rc, cleanup);

	/* This should work because we passing in the specific object to be removed */
	cleanup_count = 0;
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_ELEM_ORDERED(&sv1, ZZZ, cleanup_int) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 2, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 1) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, cleanup_count == 1, rc, cleanup);

	/* If we want a comparison by value, we need to pass in a comparison
	 * function.
	 */
	cleanup_count = 0;
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_CMP_ORDERED(&sv1, AAA, AST_VECTOR_ELEM_DEFAULT_CMP, cleanup_int) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&sv1) == 1, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&sv1, 0) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, cleanup_count == 1, rc, cleanup);

	/* This element is gone so we shouldn't be able to find it or delete it again. */
	ast_test_validate_cleanup(test, AST_VECTOR_GET_CMP(&sv1, AAA, AST_VECTOR_ELEM_DEFAULT_CMP) == NULL, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_REMOVE_CMP_ORDERED(&sv1, AAA, AST_VECTOR_ELEM_DEFAULT_CMP, cleanup_int) != 0, rc, cleanup);

	/* CCC should still be there though */
	ast_test_validate_cleanup(test, *(int *)AST_VECTOR_GET_CMP(&sv1, CCC, AST_VECTOR_ELEM_DEFAULT_CMP) == CCC, rc, cleanup);

cleanup:
	AST_VECTOR_FREE(&sv1);
	return rc;
}


static int cb(void *obj, void *arg, void *data)
{
	return strcmp(arg, "ARG") == 0 ? CMP_MATCH : CMP_STOP;
}

static int cb_first(void *obj, void *arg, void *data)
{
	return data == arg ? CMP_MATCH | CMP_STOP : 0;
}

AST_TEST_DEFINE(callbacks)
{
	AST_VECTOR(, char *) sv1;
	typeof(sv1) *sv2 = NULL;

	int rc = AST_TEST_PASS;
	char *AAA = "AAA";
	char *BBB = "BBB";
	char *CCC = "CCC";

	switch (cmd) {
	case TEST_INIT:
		info->name = "callbacks";
		info->category = "/main/vector/";
		info->summary = "Test vector callback ops";
		info->description = "Test vector callback ops";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	AST_VECTOR_INIT(&sv1, 32);

	AST_VECTOR_APPEND(&sv1, AAA);
	AST_VECTOR_APPEND(&sv1, BBB);
	AST_VECTOR_APPEND(&sv1, CCC);

	ast_test_validate_cleanup(test, AST_VECTOR_CALLBACK(&sv1, cb, "ARG", test) == CCC, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_CALLBACK(&sv1, cb_first, test, test) == AAA, rc, cleanup);

	sv2 = AST_VECTOR_CALLBACK_MULTIPLE(&sv1, cb, "ARG", test);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(sv2) == 3, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(sv2, 0) == AAA, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(sv2, 1) == BBB, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(sv2, 2) == CCC, rc, cleanup);

	AST_VECTOR_PTR_FREE(sv2);
	sv2 = AST_VECTOR_CALLBACK_MULTIPLE(&sv1, cb_first, test, test);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(sv2) == 1, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(sv2, 0) == AAA, rc, cleanup);

cleanup:
	AST_VECTOR_FREE(&sv1);
	AST_VECTOR_PTR_FREE(sv2);

	return rc;
}

AST_TEST_DEFINE(locks)
{
	AST_VECTOR_RW(, char *) sv1;
	int rc = AST_TEST_PASS;
	struct timespec ts;

	switch (cmd) {
	case TEST_INIT:
		info->name = "locks";
		info->category = "/main/vector/";
		info->summary = "Test vector locking ops";
		info->description = "Test vector locking ops";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* We're not actually checking that locking works,
	 * just that the macro expansions work
	 */

	AST_VECTOR_RW_INIT(&sv1, 0);

	ast_test_validate_cleanup(test, AST_VECTOR_RW_RDLOCK(&sv1) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_UNLOCK(&sv1) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_WRLOCK(&sv1) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_UNLOCK(&sv1) == 0, rc, cleanup);

	ast_test_validate_cleanup(test, AST_VECTOR_RW_RDLOCK_TRY(&sv1) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_WRLOCK_TRY(&sv1) != 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_UNLOCK(&sv1) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_WRLOCK_TRY(&sv1) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_UNLOCK(&sv1) == 0, rc, cleanup);

	ts.tv_nsec = 0;
	ts.tv_sec = 2;

	ast_test_validate_cleanup(test, AST_VECTOR_RW_RDLOCK_TIMED(&sv1, &ts) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_WRLOCK_TIMED(&sv1, &ts) != 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_UNLOCK(&sv1) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_WRLOCK_TIMED(&sv1, &ts) == 0, rc, cleanup);
	ast_test_validate_cleanup(test, AST_VECTOR_RW_UNLOCK(&sv1) == 0, rc, cleanup);

cleanup:
	AST_VECTOR_RW_FREE(&sv1);

	return rc;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(locks);
	AST_TEST_UNREGISTER(callbacks);
	AST_TEST_UNREGISTER(basic_ops_integer);
	AST_TEST_UNREGISTER(basic_ops);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(locks);
	AST_TEST_REGISTER(callbacks);
	AST_TEST_REGISTER(basic_ops_integer);
	AST_TEST_REGISTER(basic_ops);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Vector test module");
