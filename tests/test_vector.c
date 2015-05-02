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

ASTERISK_REGISTER_FILE()

#include "asterisk/test.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/module.h"
#include "asterisk/vector.h"

#define test_validate_cleanup(condition) ({ \
	if (!(condition)) {	\
		ast_test_status_update((test), "%s: %s\n", "Condition failed", #condition); \
		rc = AST_TEST_FAIL; \
		goto cleanup; \
	} \
})

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
	test_validate_cleanup(sv1.max == 3);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 0);

	test_validate_cleanup(AST_VECTOR_APPEND(&sv1, AAA) == 0);
	test_validate_cleanup(AST_VECTOR_APPEND(&sv1, BBB) == 0);
	test_validate_cleanup(AST_VECTOR_APPEND(&sv1, CCC) == 0);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 3);
	test_validate_cleanup(sv1.max == 3);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == BBB);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 2) == CCC);

	test_validate_cleanup(AST_VECTOR_INSERT(&sv1, 1, ZZZ) == 0);
	test_validate_cleanup(sv1.max >= 4);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 4);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == ZZZ);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 2) == BBB);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 3) == CCC);

	test_validate_cleanup(*(char **)AST_VECTOR_GET_CMP(&sv1, "AAA", 0 == strcmp) == AAA);
	test_validate_cleanup(*(char **)AST_VECTOR_GET_CMP(&sv1, "ZZZ", 0 == strcmp) == ZZZ);

	AST_VECTOR_FREE(&sv1);
	ast_test_validate(test, sv1.elems == NULL);
	ast_test_validate(test, sv1.current == 0);
	ast_test_validate(test, sv1.max == 0);

	ast_test_validate(test, AST_VECTOR_INIT(&sv1, 0) == 0);
	test_validate_cleanup(sv1.max == 0);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 0);

	test_validate_cleanup(AST_VECTOR_APPEND(&sv1, AAA) == 0);
	test_validate_cleanup(AST_VECTOR_APPEND(&sv1, BBB) == 0);
	test_validate_cleanup(AST_VECTOR_APPEND(&sv1, CCC) == 0);
	test_validate_cleanup(sv1.max >= 3);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 3);

	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == BBB);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 2) == CCC);

	/* Overwrite index 1 */
	test_validate_cleanup(AST_VECTOR_REPLACE(&sv1, 1, ZZZ) == 0);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 3);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == ZZZ);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 2) == CCC);

	/* Remove index 0 and bring the last entry into it's empty slot */
	test_validate_cleanup(AST_VECTOR_REMOVE_UNORDERED(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 2);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == CCC);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == ZZZ);

	/* Replace 0 and 2 leaving 1 alone */
	test_validate_cleanup(AST_VECTOR_REPLACE(&sv1, 0, AAA) == 0);
	test_validate_cleanup(AST_VECTOR_REPLACE(&sv1, 2, CCC) == 0);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 3);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == ZZZ);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 2) == CCC);

	/* Remove 1 and compact preserving order */
	test_validate_cleanup(AST_VECTOR_REMOVE_ORDERED(&sv1, 1) == ZZZ);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 2);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == CCC);

	/* Equivalent of APPEND */
	test_validate_cleanup(AST_VECTOR_REPLACE(&sv1, 2, ZZZ) == 0);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 3);

	/* This should fail because comparison is by pointer */
	test_validate_cleanup(AST_VECTOR_REMOVE_ELEM_ORDERED(&sv1, "ZZZ", cleanup) != 0);

	/* This should work because we passing in the specific object to be removed */
	cleanup_count = 0;
	test_validate_cleanup(AST_VECTOR_REMOVE_ELEM_ORDERED(&sv1, ZZZ, cleanup) == 0);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 2);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == AAA);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 1) == CCC);
	test_validate_cleanup(cleanup_count == 1);

	/* If we want a comparison by value, we need to pass in a comparison
	 * function.  The comparison looks weird but that's what it takes.
	 */
	cleanup_count = 0;
	test_validate_cleanup(AST_VECTOR_REMOVE_CMP_ORDERED(&sv1, "AAA", 0 == strcmp, cleanup) == 0);
	test_validate_cleanup(AST_VECTOR_SIZE(&sv1) == 1);
	test_validate_cleanup(AST_VECTOR_GET(&sv1, 0) == CCC);
	test_validate_cleanup(cleanup_count == 1);

	/* This element is gone so we shouldn't be able to find it or delete it again. */
	test_validate_cleanup(AST_VECTOR_GET_CMP(&sv1, "AAA", 0 == strcmp) == NULL);
	test_validate_cleanup(AST_VECTOR_REMOVE_CMP_ORDERED(&sv1, "AAA", 0 == strcmp, cleanup) != 0);

	/* CCC should still be there though */
	test_validate_cleanup(*(char **)AST_VECTOR_GET_CMP(&sv1, "CCC", 0 == strcmp) == CCC);

cleanup:
	AST_VECTOR_FREE(&sv1);
	return rc;
}

static int cb(void *obj, void *arg, void *data, int flags)
{
	return strcmp(arg, "ARG") == 0 ? 0 : CMP_STOP;
}

static int cb_first(void *obj, void *arg, void *data, int flags)
{
	return data == arg ? CMP_STOP : 0;
}

AST_TEST_DEFINE(callbacks)
{
	AST_VECTOR(, char *) sv1;
	AST_RWVECTOR(, char *) sv2;
	int rc = AST_TEST_PASS;

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

	/* We're not actually checking that locking works, just the the APIs work. */

	AST_VECTOR_INIT(&sv1, 32);
	AST_RWVECTOR_INIT(&sv2, 32);

	AST_VECTOR_APPEND(&sv1, "AAA");
	AST_VECTOR_APPEND(&sv1, "BBB");
	AST_VECTOR_APPEND(&sv1, "CCC");

	AST_VECTOR_APPEND(&sv2, "AAA2");
	AST_VECTOR_APPEND(&sv2, "BBB2");
	AST_VECTOR_APPEND(&sv2, "CCC2");

	test_validate_cleanup(AST_VECTOR_CALLBACK_DATA(&sv1, cb, "ARG", test) == 3);

	test_validate_cleanup(AST_VECTOR_CALLBACK_DATA(&sv1, cb_first, test, test) == 1);

	test_validate_cleanup(AST_VECTOR_CALLBACK_DATA(&sv2, cb, "ARG", test) == 3);

	test_validate_cleanup(AST_VECTOR_CALLBACK_DATA(&sv2, cb_first, test, test) == 1);

	test_validate_cleanup(AST_RWVECTOR_CALLBACK_DATA_RDLOCK(&sv2, cb, "ARG", test) == 3);

	test_validate_cleanup(AST_RWVECTOR_CALLBACK_DATA_RDLOCK(&sv2, cb_first, test, test) == 1);

	test_validate_cleanup(AST_RWVECTOR_CALLBACK_DATA_WRLOCK(&sv2, cb, "ARG", test) == 3);

	test_validate_cleanup(AST_RWVECTOR_CALLBACK_DATA_WRLOCK(&sv2, cb_first, test, test) == 1);

cleanup:
	AST_VECTOR_FREE(&sv1);
	AST_RWVECTOR_FREE(&sv2);

	return rc;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(callbacks);
	AST_TEST_UNREGISTER(basic_ops);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(callbacks);
	AST_TEST_REGISTER(basic_ops);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Vector test module");
