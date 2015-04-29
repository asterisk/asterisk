/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
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
 * \brief SCOPED_LOCK unit tests
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"

static int indicator;
static struct ast_test *current_test;
AST_MUTEX_DEFINE_STATIC(the_lock);

static void lock_it(ast_mutex_t *lock)
{
	indicator = 1;
	ast_mutex_lock(lock);
}

static void unlock_it(ast_mutex_t *lock)
{
	indicator = 0;
	ast_mutex_unlock(lock);
}

AST_TEST_DEFINE(lock_test)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	switch(cmd) {
	case TEST_INIT:
		info->name = "lock_test";
		info->category = "/main/lock/";
		info->summary = "SCOPED_LOCK test";
		info->description =
			"Tests that scoped locks are scoped as they are expected to be";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	current_test = test;
	indicator = 0;
	{
		SCOPED_LOCK(lock, &the_lock, lock_it, unlock_it);
		if (indicator != 1) {
			ast_log(LOG_ERROR, "The lock was not acquired via RAII");
			res = AST_TEST_FAIL;
		}
	}
	if (indicator != 0) {
		ast_log(LOG_ERROR, "The lock was not released when the variable went out of scope");
		res = AST_TEST_FAIL;
	}

	for (i = 0; i < 10; ++i) {
		SCOPED_LOCK(lock, &the_lock, lock_it, unlock_it);
		if (indicator != 1) {
			ast_log(LOG_ERROR, "The lock was not acquired via RAII");
			res = AST_TEST_FAIL;
		}
	}

	if (indicator != 0) {
		ast_log(LOG_ERROR, "The lock was not released when the variable went out of scope");
		res = AST_TEST_FAIL;
	}

	return res;
}

struct test_struct
{
	int locked;
	int reffed;
};

/*!
 * \brief lock callback function
 *
 * Locks the object passed in. Only sets the locked
 * flag if the object is reffed. This allows us to check
 * that locking is always occurring after reffing.
 */
static void test_lock(struct test_struct *test)
{
	ast_test_status_update(current_test, "Lock is occurring\n");
	ao2_lock(test);
	if (test->reffed) {
		test->locked = 1;
	}
}

/*!
 * \brief unlock callback function
 *
 * Unlocks the object passed in. Only clears the locked
 * flag if the object is still reffed. This allows us to
 * ensure that unlocking is always occurring before unreffing.
 */
static void test_unlock(struct test_struct *test)
{
	ast_test_status_update(current_test, "Unlock is occurring\n");
	ao2_unlock(test);
	if (test->reffed) {
		test->locked = 0;
	}
}

/*!
 * \brief ref callback function
 *
 * Refs the object passed in. Only sets the reffed flag if
 * the object is not locked. This allows us to ensure that
 * reffing always occurs before locking.
 */
static struct test_struct *test_ref(struct test_struct *test)
{
	ast_test_status_update(current_test, "Ref is occurring\n");
	ao2_ref(test, +1);
	if (!test->locked) {
		test->reffed = 1;
	}
	return test;
}

/*!
 * \brief unref callback function
 *
 * Unrefs the object passed in. Only sets the unreffed flag if
 * the object is not locked. This allows us to ensure that
 * unreffing always occurs after unlocking.
 */
static void test_unref(struct test_struct *test)
{
	ast_test_status_update(current_test, "Unref is occurring\n");
	ao2_ref(test, -1);
	if (!test->locked) {
		test->reffed = 0;
	}
}

/*!
 * \brief wrapper for ao2_iterator_next
 *
 * Grabs the next item in the container and replaces the ref acquired
 * from ao2_iterator_next() with a call to test_ref().
 */
static struct test_struct *test_iterator_next(struct ao2_iterator *iter)
{
	struct test_struct *test = ao2_iterator_next(iter);

	if (!test) {
		return NULL;
	}

	/* Remove ref from ao2_iterator_next() and replace it with
	 * a test_ref() call. The order here is safe since we can guarantee
	 * the container still has a ref to the test structure.
	 */
	ao2_ref(test, -1);
	test_ref(test);

	return test;
}

AST_TEST_DEFINE(cleanup_order)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ao2_iterator iter;
	struct test_struct *object_iter;
	RAII_VAR(struct ao2_container*, container, ao2_container_alloc(13, NULL, NULL), ao2_cleanup);
	RAII_VAR(struct test_struct *, object, ao2_alloc(sizeof(*object), NULL), ao2_cleanup);

	switch(cmd) {
	case TEST_INIT:
		info->name = "cleanup_order_test";
		info->category = "/main/lock/";
		info->summary = "cleanup order test";
		info->description =
			"Tests that variables with cleanup attributes are cleaned up\n"
			"in the reverse order they are declared.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	current_test = test;

	if (!object || !container) {
		/* Allocation failure. We can't even pretend to do this test properly */
		return AST_TEST_FAIL;
	}

	{
		/* Purpose of this block is to make sure that the cleanup operations
		 * run in the reverse order that they were created here.
		 */
		RAII_VAR(struct test_struct *, object2, test_ref(object), test_unref);
		SCOPED_LOCK(lock, object, test_lock, test_unlock);
		if (!object->reffed || !object->locked) {
			ast_log(LOG_ERROR, "Test failed due to out of order initializations");
			res = AST_TEST_FAIL;
		}
	}

	if (object->reffed || object->locked) {
		ast_log(LOG_ERROR, "Test failed due to out of order cleanups\n");
		res = AST_TEST_FAIL;
	}

	/* Now link the object into the container for a little experiment ... */
	ao2_link(container, object);

	/* This loop is to ensure that unrefs in a for loop occur after the cleanup
	 * operations of items inside the loop. If we hope to be able to mix scoped locks
	 * and ao2 refs, this is the way to go about it.
	 */
	for (iter = ao2_iterator_init(container, 0);
			(object_iter = test_iterator_next(&iter));
			test_unref(object_iter)) {
		SCOPED_LOCK(lock, object_iter, test_lock, test_unlock);
		if (!object->reffed || !object->locked) {
			ast_log(LOG_ERROR, "Test failed due to out of order initializations");
			res = AST_TEST_FAIL;
		}
	}
	ao2_iterator_destroy(&iter);

	if (object->reffed || object->locked) {
		ast_log(LOG_ERROR, "Test failed due to out of order cleanups\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

static int load_module(void)
{
	AST_TEST_REGISTER(lock_test);
	AST_TEST_REGISTER(cleanup_order);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "SCOPED_LOCK test module");
