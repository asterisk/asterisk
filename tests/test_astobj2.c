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
 * \brief astobj2 test module
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/astobj2.h"

struct test_obj {
	int i;
	int *destructor_count;
};

static void test_obj_destructor(void *obj)
{
	struct test_obj *test_obj = (struct test_obj *) obj;
	*test_obj->destructor_count = *test_obj->destructor_count - 1;
}

static int increment_cb(void *obj, void *arg, int flag)
{
	int *i = (int *) arg;

	*i = *i + 1;
	return 0;
}

static int all_but_one_cb(void *obj, void *arg, int flag)
{
	struct test_obj *test_obj = (struct test_obj *) obj;

	return (test_obj->i > 1) ? CMP_MATCH : 0;
}

static int multiple_cb(void *obj, void *arg, int flag)
{
	int *i = (int *) arg;
	struct test_obj *test_obj = (struct test_obj *) obj;

	return (test_obj->i <= *i) ? CMP_MATCH : 0;
}

static int test_cmp_cb(void *obj, void *arg, int flags)
{
	struct test_obj *cmp_obj = (struct test_obj *) obj;

	if (!arg) {
		return 0;
	}

	if (flags & OBJ_KEY) {
		int *i = (int *) arg;
		return (cmp_obj->i == *i) ? CMP_MATCH | CMP_STOP : 0;
	} else {
		struct test_obj *test_obj = (struct test_obj *) arg;
		return (cmp_obj->i == test_obj->i) ? CMP_MATCH | CMP_STOP : 0;
	}
}

static int test_hash_cb(const void *obj, const int flags)
{
	if (!obj) {
		return 0;
	}

	if (flags & OBJ_KEY) {
		const int *i = obj;

		return *i;
	} else {
		const struct test_obj *test_obj = obj;

		return test_obj->i;
	}
}

static int astobj2_test_helper(int use_hash, int use_cmp, unsigned int lim, struct ast_test *test)
{
	struct ao2_container *c1;
	struct ao2_container *c2;
	struct ao2_iterator it;
	struct ao2_iterator *mult_it;
	struct test_obj *obj;
	struct test_obj tmp_obj;
	int bucket_size;
	int increment = 0;
	int destructor_count = 0;
	int num;
	int  res = AST_TEST_PASS;

	/* This test needs at least 5 objects */
	if (lim < 5) {
		lim = 5;
	}

	bucket_size = (ast_random() % ((lim / 4) + 1)) + 1;
	c1 = ao2_t_container_alloc(bucket_size, use_hash ? test_hash_cb : NULL, use_cmp ? test_cmp_cb : NULL, "test");
	c2 = ao2_t_container_alloc(bucket_size, test_hash_cb, test_cmp_cb, "test");

	if (!c1 || !c2) {
		ast_test_status_update(test, "ao2_container_alloc failed.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Create objects and link into container */
	destructor_count = lim;
	for (num = 1; num <= lim; num++) {
		if (!(obj = ao2_t_alloc(sizeof(struct test_obj), test_obj_destructor, "making zombies"))) {
			ast_test_status_update(test, "ao2_alloc failed.\n");
			res = AST_TEST_FAIL;
			goto cleanup;
		}
		obj->destructor_count = &destructor_count;
		obj->i = num;
		ao2_link(c1, obj);
		ao2_t_ref(obj, -1, "test");
		if (ao2_container_count(c1) != num) {
			ast_test_status_update(test, "container did not link correctly\n");
			res = AST_TEST_FAIL;
		}
	}

	ast_test_status_update(test, "Container created: random bucket size %d: number of items: %d\n", bucket_size, lim);

	/* Testing ao2_find with no flags */
	num = 100;
	for (; num; num--) {
		int i = (ast_random() % ((lim / 2)) + 1); /* find a random object */
		tmp_obj.i = i;
		if (!(obj = ao2_find(c1, &tmp_obj, 0))) {
			res = AST_TEST_FAIL;
			ast_test_status_update(test, "COULD NOT FIND:%d, ao2_find() with no flags failed.\n", i);
		} else {
			/* a correct match will only take place when the custom cmp function is used */
			if (use_cmp && obj->i != i) {
				ast_test_status_update(test, "object %d does not match object %d\n", obj->i, tmp_obj.i);
				res = AST_TEST_FAIL;
			}
			ao2_t_ref(obj, -1, "test");
		}
	}

	/* Testing ao2_find with OBJ_POINTER */
	num = 75;
	for (; num; num--) {
		int i = (ast_random() % ((lim / 2)) + 1); /* find a random object */
		tmp_obj.i = i;
		if (!(obj = ao2_find(c1, &tmp_obj, OBJ_POINTER))) {
			res = AST_TEST_FAIL;
			ast_test_status_update(test, "COULD NOT FIND:%d, ao2_find() with OBJ_POINTER flag failed.\n", i);
		} else {
			/* a correct match will only take place when the custom cmp function is used */
			if (use_cmp && obj->i != i) {
				ast_test_status_update(test, "object %d does not match object %d\n", obj->i, tmp_obj.i);
				res = AST_TEST_FAIL;
			}
			ao2_t_ref(obj, -1, "test");
		}
	}

	/* Testing ao2_find with OBJ_KEY */
	num = 75;
	for (; num; num--) {
		int i = (ast_random() % ((lim / 2)) + 1); /* find a random object */
		if (!(obj = ao2_find(c1, &i, OBJ_KEY))) {
			res = AST_TEST_FAIL;
			ast_test_status_update(test, "COULD NOT FIND:%d, ao2_find() with OBJ_KEY flag failed.\n", i);
		} else {
			/* a correct match will only take place when the custom cmp function is used */
			if (use_cmp && obj->i != i) {
				ast_test_status_update(test, "object %d does not match object %d\n", obj->i, tmp_obj.i);
				res = AST_TEST_FAIL;
			}
			ao2_t_ref(obj, -1, "test");
		}
	}

	/* Testing ao2_find with OBJ_POINTER | OBJ_UNLINK | OBJ_CONTINUE.
	 * In this test items are unlinked from c1 and placed in c2.  Then
	 * unlinked from c2 and placed back into c1.
	 *
	 * For this module and set of custom hash/cmp functions, an object
	 * should only be found if the astobj2 default cmp function is used.
	 * This test is designed to mimic the chan_iax.c call number use case. */
	num = lim < 25 ? lim : 25;
	for (; num; num--) {
		if (!(obj = ao2_find(c1, NULL, OBJ_POINTER | OBJ_UNLINK | OBJ_CONTINUE))) {
			if (!use_cmp) {
				ast_test_status_update(test, "ao2_find with OBJ_POINTER | OBJ_UNLINK | OBJ_CONTINUE failed with default hash function.\n");
				res = AST_TEST_FAIL;
			}
		} else {
			if (use_cmp) {
				ast_test_status_update(test, "ao2_find with OBJ_POINTER | OBJ_UNLINK | OBJ_CONTINUE failed with custom hash function.\n");
				res = AST_TEST_FAIL;
			}
			ao2_link(c2, obj);
			ao2_t_ref(obj, -1, "test");
		}
	}
	it = ao2_iterator_init(c2, 0);
	while ((obj = ao2_t_iterator_next(&it, "test"))) {
		ao2_t_unlink(c2, obj, "test");
		ao2_t_link(c1, obj, "test");
		ao2_t_ref(obj, -1, "test");
	}
	ao2_iterator_destroy(&it);

	/* Test Callback with no flags. */
	increment = 0;
	ao2_t_callback(c1, 0, increment_cb, &increment, "test callback");
	if (increment != lim) {
		ast_test_status_update(test, "callback with no flags failed. Increment is %d\n", increment);
		res = AST_TEST_FAIL;
	}

	/* Test Callback with OBJ_NODATA. This should do nothing different than with no flags here. */
	increment = 0;
	ao2_t_callback(c1, OBJ_NODATA, increment_cb, &increment, "test callback");
	if (increment != lim) {
		ast_test_status_update(test, "callback with OBJ_NODATA failed. Increment is %d\n", increment);
		res = AST_TEST_FAIL;
	}

	/* Test OBJ_MULTIPLE with OBJ_UNLINK*/
	num = lim < 25 ? lim : 25;
	if (!(mult_it = ao2_t_callback(c1, OBJ_MULTIPLE | OBJ_UNLINK, multiple_cb, &num, "test multiple"))) {
		ast_test_status_update(test, "OBJ_MULTIPLE iwth OBJ_UNLINK test failed.\n");
		res = AST_TEST_FAIL;
	} else {
		/* make sure num items unlinked is as expected */
		if ((lim - ao2_container_count(c1)) != num) {
			ast_test_status_update(test, "OBJ_MULTIPLE | OBJ_UNLINK test failed, did not unlink correct number of objects.\n");
			res = AST_TEST_FAIL;
		}

		/* link what was unlinked back into c1 */
		while ((obj = ao2_t_iterator_next(mult_it, "test"))) {
			ao2_t_link(c1, obj, "test");
			ao2_t_ref(obj, -1, "test"); /* remove ref from iterator */
		}
		ao2_iterator_destroy(mult_it);
	}

	/* Test OBJ_MULTIPLE without unlink, add items back afterwards */
	num = lim < 25 ? lim : 25;
	if (!(mult_it = ao2_t_callback(c1, OBJ_MULTIPLE, multiple_cb, &num, "test multiple"))) {
		ast_test_status_update(test, "OBJ_MULTIPLE without OBJ_UNLINK test failed.\n");
		res = AST_TEST_FAIL;
	} else {
		while ((obj = ao2_t_iterator_next(mult_it, "test"))) {
			ao2_t_ref(obj, -1, "test"); /* remove ref from iterator */
		}
		ao2_iterator_destroy(mult_it);
	}

	/* Test OBJ_MULTIPLE without unlink and no iterating */
	num = lim < 5 ? lim : 5;
	if (!(mult_it = ao2_t_callback(c1, OBJ_MULTIPLE, multiple_cb, &num, "test multiple"))) {
		ast_test_status_update(test, "OBJ_MULTIPLE with no OBJ_UNLINK and no iterating failed.\n");
		res = AST_TEST_FAIL;
	} else {
		ao2_iterator_destroy(mult_it);
	}

	/* Is the container count what we expect after all the finds and unlinks?*/
	if (ao2_container_count(c1) != lim) {
		ast_test_status_update(test, "container count does not match what is expected after ao2_find tests.\n");
		res = AST_TEST_FAIL;
	}

	/* Testing iterator.  Unlink a single object and break. do not add item back */
	it = ao2_iterator_init(c1, 0);
	num = (lim / 4) + 1;
	while ((obj = ao2_t_iterator_next(&it, "test"))) {
		if (obj->i == num) {
			ao2_t_ref(obj, -1, "test");
			ao2_t_unlink(c1, obj, "test");
			break;
		}
		ao2_t_ref(obj, -1, "test");
	}
	ao2_iterator_destroy(&it);

	/* Is the container count what we expect after removing a single item? */
	if (ao2_container_count(c1) != (lim - 1)) {
		ast_test_status_update(test, "unlink during iterator failed. Number %d was not removed.\n", num);
		res = AST_TEST_FAIL;
	}

	/* Test unlink all with OBJ_MULTIPLE, leave a single object for the container to destroy */
	ao2_t_callback(c1, OBJ_MULTIPLE | OBJ_UNLINK | OBJ_NODATA, all_but_one_cb, NULL, "test multiple");
	/* check to make sure all test_obj destructors were called except for 1 */
	if (destructor_count != 1) {
		ast_test_status_update(test, "OBJ_MULTIPLE | OBJ_UNLINK | OBJ_NODATA failed. destructor count %d\n", destructor_count);
		res = AST_TEST_FAIL;
	}

cleanup:
	/* destroy containers */
	if (c1) {
		ao2_t_ref(c1, -1, "bye c1");
	}
	if (c2) {
		ao2_t_ref(c2, -1, "bye c2");
	}

	if (destructor_count > 0) {
		ast_test_status_update(test, "all destructors were not called, destructor count is %d\n", destructor_count);
		res = AST_TEST_FAIL;
	} else if (destructor_count < 0) {
		ast_test_status_update(test, "Destructor was called too many times, destructor count is %d\n", destructor_count);
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(astobj2_test_1)
{
	int res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "astobj2_test1";
		info->category = "/main/astobj2/";
		info->summary = "astobj2 test using ao2 objects, containers, callbacks, and iterators";
		info->description =
			"Builds ao2_containers with various item numbers, bucket sizes, cmp and hash "
			"functions. Runs a series of tests to manipulate the container using callbacks "
			"and iterators.  Verifies expected behavior.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}


	/* Test 1, 500 items with custom hash and cmp functions */
	ast_test_status_update(test, "Test 1, astobj2 test with 500 items.\n");
	if ((res = astobj2_test_helper(1, 1, 500, test)) == AST_TEST_FAIL) {
		return res;
	}

	/* Test 2, 1000 items with custom hash and default cmp functions */
	ast_test_status_update(test, "Test 2, astobj2 test with 1000 items.\n");
	if ((res = astobj2_test_helper(1, 0, 1000, test)) == AST_TEST_FAIL) {
		return res;
	}

	/* Test 3, 10000 items with default hash and custom cmp functions */
	ast_test_status_update(test, "Test 3, astobj2 test with 10000 items.\n");
	if ((res = astobj2_test_helper(0, 1, 10000, test)) == AST_TEST_FAIL) {
		return res;
	}

	/* Test 4, 100000 items with default hash and cmp functions */
	ast_test_status_update(test, "Test 4, astobj2 test with 100000 items.\n");
	if ((res = astobj2_test_helper(0, 0, 100000, test)) == AST_TEST_FAIL) {
		return res;
	}

	return res;
}

AST_TEST_DEFINE(astobj2_test_2)
{
	int res = AST_TEST_PASS;
	struct ao2_container *c;
	struct ao2_iterator i;
	struct test_obj *obj;
	int num;
	static const int NUM_OBJS = 5;
	int destructor_count = NUM_OBJS;
	struct test_obj tmp_obj = { 0, };

	switch (cmd) {
	case TEST_INIT:
		info->name = "astobj2_test2";
		info->category = "/main/astobj2/";
		info->summary = "Test a certain scenario using ao2 iterators";
		info->description =
			"This test is aimed at testing for a specific regression that occurred. "
			"Add some objects into a container.  Mix finds and iteration and make "
			"sure that the iterator still sees all objects.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	c = ao2_container_alloc(1, NULL, test_cmp_cb);
	if (!c) {
		ast_test_status_update(test, "ao2_container_alloc failed.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	for (num = 1; num <= NUM_OBJS; num++) {
		if (!(obj = ao2_alloc(sizeof(struct test_obj), test_obj_destructor))) {
			ast_test_status_update(test, "ao2_alloc failed.\n");
			res = AST_TEST_FAIL;
			goto cleanup;
		}
		obj->destructor_count = &destructor_count;
		obj->i = num;
		ao2_link(c, obj);
		ao2_ref(obj, -1);
		if (ao2_container_count(c) != num) {
			ast_test_status_update(test, "container did not link correctly\n");
			res = AST_TEST_FAIL;
		}
	}

	/*
	 * Iteration take 1.  Just make sure we see all NUM_OBJS objects.
	 */
	num = 0;
	i = ao2_iterator_init(c, 0);
	while ((obj = ao2_iterator_next(&i))) {
		num++;
		ao2_ref(obj, -1);
	}
	ao2_iterator_destroy(&i);

	if (num != NUM_OBJS) {
		ast_test_status_update(test, "iterate take 1, expected '%d', only saw '%d' objects\n",
				NUM_OBJS, num);
		res = AST_TEST_FAIL;
	}

	/*
	 * Iteration take 2.  Do a find for the last object, then iterate and make
	 * sure we find all NUM_OBJS objects.
	 */
	tmp_obj.i = NUM_OBJS;
	obj = ao2_find(c, &tmp_obj, OBJ_POINTER);
	if (!obj) {
		ast_test_status_update(test, "ao2_find() failed.\n");
		res = AST_TEST_FAIL;
	} else {
		ao2_ref(obj, -1);
	}

	num = 0;
	i = ao2_iterator_init(c, 0);
	while ((obj = ao2_iterator_next(&i))) {
		num++;
		ao2_ref(obj, -1);
	}
	ao2_iterator_destroy(&i);

	if (num != NUM_OBJS) {
		ast_test_status_update(test, "iterate take 2, expected '%d', only saw '%d' objects\n",
				NUM_OBJS, num);
		res = AST_TEST_FAIL;
	}

	/*
	 * Iteration take 3.  Do a find for an object while in the middle
	 * of iterating;
	 */
	num = 0;
	i = ao2_iterator_init(c, 0);
	while ((obj = ao2_iterator_next(&i))) {
		if (num == 1) {
			struct test_obj *obj2;
			tmp_obj.i = NUM_OBJS - 1;
			obj2 = ao2_find(c, &tmp_obj, OBJ_POINTER);
			if (!obj2) {
				ast_test_status_update(test, "ao2_find() failed.\n");
				res = AST_TEST_FAIL;
			} else {
				ao2_ref(obj2, -1);
			}
		}
		num++;
		ao2_ref(obj, -1);
	}
	ao2_iterator_destroy(&i);

	if (num != NUM_OBJS) {
		ast_test_status_update(test, "iterate take 3, expected '%d', only saw '%d' objects\n",
				NUM_OBJS, num);
		res = AST_TEST_FAIL;
	}


cleanup:
	if (c) {
		ao2_ref(c, -1);
	}

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(astobj2_test_1);
	AST_TEST_UNREGISTER(astobj2_test_2);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(astobj2_test_1);
	AST_TEST_REGISTER(astobj2_test_2);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ASTOBJ2 Unit Tests");
