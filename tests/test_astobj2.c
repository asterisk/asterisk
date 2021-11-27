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

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/astobj2.h"

/* Uncomment the following line to dump the container contents during tests. */
//#define TEST_CONTAINER_DEBUG_DUMP		1

enum test_container_type {
	TEST_CONTAINER_LIST,
	TEST_CONTAINER_HASH,
	TEST_CONTAINER_RBTREE,
};

/*!
 * \internal
 * \brief Convert the container type enum to string.
 * \since 12.0.0
 *
 * \param type Container type value to convert to string.
 *
 * \return String value of container type.
 */
static const char *test_container2str(enum test_container_type type)
{
	const char *c_type;

	c_type = "Unknown";
	switch (type) {
	case TEST_CONTAINER_LIST:
		c_type = "List";
		break;
	case TEST_CONTAINER_HASH:
		c_type = "Hash";
		break;
	case TEST_CONTAINER_RBTREE:
		c_type = "RBTree";
		break;
	}
	return c_type;
}

struct test_obj {
	/*! What to decrement when object is destroyed. */
	int *destructor_count;
	/*! Container object key */
	int i;
	/*! Identifier for duplicate object key tests. */
	int dup_number;
};

/*! Partial search key +/- matching range. */
int partial_key_match_range;

static void test_obj_destructor(void *v_obj)
{
	struct test_obj *obj = (struct test_obj *) v_obj;

	if (obj->destructor_count) {
		--*obj->destructor_count;
	}
}

static int increment_cb(void *obj, void *arg, int flag)
{
	int *i = (int *) arg;

	*i = *i + 1;
	return 0;
}

static int all_but_one_cb(void *obj, void *arg, int flag)
{
	struct test_obj *cmp_obj = (struct test_obj *) obj;

	return (cmp_obj->i) ? CMP_MATCH : 0;
}

static int multiple_cb(void *obj, void *arg, int flag)
{
	int *i = (int *) arg;
	struct test_obj *cmp_obj = (struct test_obj *) obj;

	return (cmp_obj->i < *i) ? CMP_MATCH : 0;
}

static int test_cmp_cb(void *obj, void *arg, int flags)
{
	struct test_obj *cmp_obj = (struct test_obj *) obj;

	if (flags & OBJ_KEY) {
		int *i = (int *) arg;

		return (cmp_obj->i == *i) ? CMP_MATCH : 0;
	} else if (flags & OBJ_PARTIAL_KEY) {
		int *i = (int *) arg;

		return (*i - partial_key_match_range <= cmp_obj->i
			&& cmp_obj->i <= *i + partial_key_match_range) ? CMP_MATCH : 0;
	} else {
		struct test_obj *arg_obj = (struct test_obj *) arg;

		return (cmp_obj->i == arg_obj->i) ? CMP_MATCH : 0;
	}
}

static int test_hash_cb(const void *obj, const int flags)
{
	if (flags & OBJ_KEY) {
		const int *i = obj;

		return *i;
	} else if (flags & OBJ_PARTIAL_KEY) {
		/* This is absolutely wrong to be called with this flag value. */
		abort();
		/* Just in case abort() doesn't work or something else super silly */
		*((int *) 0) = 0;
		return 0;
	} else {
		const struct test_obj *hash_obj = obj;

		return hash_obj->i;
	}
}

static int test_sort_cb(const void *obj_left, const void *obj_right, int flags)
{
	const struct test_obj *test_left = obj_left;

	if (flags & OBJ_KEY) {
		const int *i = obj_right;

		return test_left->i - *i;
	} else if (flags & OBJ_PARTIAL_KEY) {
		int *i = (int *) obj_right;

		if (*i - partial_key_match_range <= test_left->i
			&& test_left->i <= *i + partial_key_match_range) {
			return 0;
		}

		return test_left->i - *i;
	} else {
		const struct test_obj *test_right = obj_right;

		return test_left->i - test_right->i;
	}
}

#if defined(TEST_CONTAINER_DEBUG_DUMP)
/*!
 * \internal
 * \brief Print test object key.
 * \since 12.0.0
 *
 * \param v_obj A pointer to the object we want the key printed.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 */
static void test_prnt_obj(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct test_obj *obj = v_obj;

	if (!obj) {
		return;
	}
	prnt(where, "%6d-%d", obj->i, obj->dup_number);
}
#endif	/* defined(TEST_CONTAINER_DEBUG_DUMP) */

/*!
 * \internal
 * \brief Test container cloning.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param orig Container to clone.
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_container_clone(int res, struct ao2_container *orig, struct ast_test *test)
{
	struct ao2_container *clone;
	struct test_obj *obj;
	struct test_obj *obj2;
	struct ao2_iterator iter;

	clone = ao2_container_clone(orig, 0);
	if (!clone) {
		ast_test_status_update(test, "ao2_container_clone failed.\n");
		return AST_TEST_FAIL;
	}
	if (ao2_container_check(clone, 0)) {
		ast_test_status_update(test, "container integrity check failed\n");
		res = AST_TEST_FAIL;
	} else if (ao2_container_count(orig) != ao2_container_count(clone)) {
		ast_test_status_update(test, "Cloned container does not have the same number of objects.\n");
		res = AST_TEST_FAIL;
	} else {
		iter = ao2_iterator_init(orig, 0);
		for (; (obj = ao2_t_iterator_next(&iter, "test orig")); ao2_t_ref(obj, -1, "test orig")) {
			/*
			 * Unlink the matching object from the cloned container to make
			 * the next search faster.  This is a big speed optimization!
			 */
			obj2 = ao2_t_callback(clone, OBJ_POINTER | OBJ_UNLINK, ao2_match_by_addr, obj,
				"test clone");
			if (obj2) {
				ao2_t_ref(obj2, -1, "test clone");
				continue;
			}
			ast_test_status_update(test,
				"Orig container has an object %p not in the clone container.\n", obj);
			res = AST_TEST_FAIL;
		}
		ao2_iterator_destroy(&iter);
		if (ao2_container_count(clone)) {
			ast_test_status_update(test, "Cloned container still has objects.\n");
			res = AST_TEST_FAIL;
		}
		if (ao2_container_check(clone, 0)) {
			ast_test_status_update(test, "container integrity check failed\n");
			res = AST_TEST_FAIL;
		}
	}
	ao2_t_ref(clone, -1, "bye clone");

	return res;
}

/*!
 * \internal
 * \brief Test ao2_find with no flags.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param look_in Container to search.
 * \param limit Container contains objects 0 - (limit - 1).
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_ao2_find_w_no_flags(int res, struct ao2_container *look_in, int limit, struct ast_test *test)
{
	int i;
	int num;
	struct test_obj tmp_obj = { 0, };
	struct test_obj *obj;

	for (num = 100; num--;) {
		i = ast_random() % limit; /* find a random object */

		tmp_obj.i = i;
		obj = ao2_find(look_in, &tmp_obj, 0);
		if (!obj) {
			ast_test_status_update(test, "COULD NOT FIND:%d, ao2_find() with no flags failed.\n", i);
			res = AST_TEST_FAIL;
		} else {
			if (obj->i != i) {
				ast_test_status_update(test, "object %d does not match %d\n", obj->i, i);
				res = AST_TEST_FAIL;
			}
			ao2_t_ref(obj, -1, "test");
		}
	}

	return res;
}

/*!
 * \internal
 * \brief Test ao2_find with OBJ_POINTER.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param look_in Container to search.
 * \param limit Container contains objects 0 - (limit - 1).
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_ao2_find_w_OBJ_POINTER(int res, struct ao2_container *look_in, int limit, struct ast_test *test)
{
	int i;
	int num;
	struct test_obj tmp_obj = { 0, };
	struct test_obj *obj;

	for (num = 75; num--;) {
		i = ast_random() % limit; /* find a random object */

		tmp_obj.i = i;
		obj = ao2_find(look_in, &tmp_obj, OBJ_POINTER);
		if (!obj) {
			ast_test_status_update(test, "COULD NOT FIND:%d, ao2_find() with OBJ_POINTER flag failed.\n", i);
			res = AST_TEST_FAIL;
		} else {
			if (obj->i != i) {
				ast_test_status_update(test, "object %d does not match %d\n", obj->i, i);
				res = AST_TEST_FAIL;
			}
			ao2_t_ref(obj, -1, "test");
		}
	}

	return res;
}

/*!
 * \internal
 * \brief Test ao2_find with OBJ_KEY.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param look_in Container to search.
 * \param limit Container contains objects 0 - (limit - 1).
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_ao2_find_w_OBJ_KEY(int res, struct ao2_container *look_in, int limit, struct ast_test *test)
{
	int i;
	int num;
	struct test_obj *obj;

	for (num = 75; num--;) {
		i = ast_random() % limit; /* find a random object */

		obj = ao2_find(look_in, &i, OBJ_KEY);
		if (!obj) {
			ast_test_status_update(test, "COULD NOT FIND:%d, ao2_find() with OBJ_KEY flag failed.\n", i);
			res = AST_TEST_FAIL;
		} else {
			if (obj->i != i) {
				ast_test_status_update(test, "object %d does not match %d\n", obj->i, i);
				res = AST_TEST_FAIL;
			}
			ao2_t_ref(obj, -1, "test");
		}
	}

	return res;
}

/*!
 * \internal
 * \brief Test ao2_find with OBJ_PARTIAL_KEY.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param look_in Container to search.
 * \param limit Container contains objects 0 - (limit - 1).
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_ao2_find_w_OBJ_PARTIAL_KEY(int res, struct ao2_container *look_in, int limit, struct ast_test *test)
{
	int i;
	int num;
	struct test_obj *obj;

	/* Set partial match to find exactly. */
	partial_key_match_range = 0;

	for (num = 100; num--;) {
		i = ast_random() % limit; /* find a random object */

		obj = ao2_find(look_in, &i, OBJ_PARTIAL_KEY);
		if (!obj) {
			ast_test_status_update(test, "COULD NOT FIND:%d, ao2_find() with OBJ_PARTIAL_KEY flag failed.\n", i);
			res = AST_TEST_FAIL;
		} else {
			if (obj->i != i) {
				ast_test_status_update(test, "object %d does not match %d\n", obj->i, i);
				res = AST_TEST_FAIL;
			}
			ao2_t_ref(obj, -1, "test");
		}
	}

	return res;
}

static int astobj2_test_1_helper(int tst_num, enum test_container_type type, int use_sort, unsigned int lim, struct ast_test *test)
{
	const char *c_type;
	struct ao2_container *c1;
	struct ao2_container *c2;
	struct ao2_iterator it;
	struct ao2_iterator *mult_it;
	struct test_obj *obj;
	int n_buckets = 0;
	int increment = 0;
	int destructor_count = 0;
	int num;
	int res = AST_TEST_PASS;

	c_type = test_container2str(type);
	ast_test_status_update(test, "Test %d, %s containers (%s).\n",
		tst_num, c_type, use_sort ? "sorted" : "non-sorted");

	c1 = NULL;
	switch (type) {
	case TEST_CONTAINER_LIST:
		/* Lists just have one bucket. */
		n_buckets = 1;
		c1 = ao2_t_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
			use_sort ? test_sort_cb : NULL, test_cmp_cb, "test");
		break;
	case TEST_CONTAINER_HASH:
		n_buckets = (ast_random() % ((lim / 4) + 1)) + 1;
		c1 = ao2_t_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, n_buckets,
			test_hash_cb, use_sort ? test_sort_cb : NULL, test_cmp_cb, "test");
		break;
	case TEST_CONTAINER_RBTREE:
		/* RBTrees just have one bucket. */
		n_buckets = 1;
		c1 = ao2_t_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
			test_sort_cb, test_cmp_cb, "test");
		break;
	}
	c2 = ao2_t_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL, "test");

	if (!c1 || !c2) {
		ast_test_status_update(test, "ao2_container_alloc failed.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Create objects and link into container */
	for (num = 0; num < lim; ++num) {
		if (!(obj = ao2_t_alloc(sizeof(struct test_obj), test_obj_destructor, "making zombies"))) {
			ast_test_status_update(test, "ao2_alloc failed.\n");
			res = AST_TEST_FAIL;
			goto cleanup;
		}
		++destructor_count;
		obj->destructor_count = &destructor_count;
		obj->i = num;
		ao2_link(c1, obj);
		ao2_t_ref(obj, -1, "test");
		if (ao2_container_check(c1, 0)) {
			ast_test_status_update(test, "container integrity check failed linking obj num:%d\n", num);
			res = AST_TEST_FAIL;
			goto cleanup;
		}
		if (ao2_container_count(c1) != num + 1) {
			ast_test_status_update(test, "container did not link correctly\n");
			res = AST_TEST_FAIL;
		}
	}

	ast_test_status_update(test, "%s container created: buckets: %d, items: %u\n",
		c_type, n_buckets, lim);

	/* Testing ao2_container_clone */
	res = test_container_clone(res, c1, test);

	/* Testing ao2_find with no flags */
	res = test_ao2_find_w_no_flags(res, c1, lim, test);

	/* Testing ao2_find with OBJ_POINTER */
	res = test_ao2_find_w_OBJ_POINTER(res, c1, lim, test);

	/* Testing ao2_find with OBJ_KEY */
	res = test_ao2_find_w_OBJ_KEY(res, c1, lim, test);

	/* Testing ao2_find with OBJ_PARTIAL_KEY */
	res = test_ao2_find_w_OBJ_PARTIAL_KEY(res, c1, lim, test);

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

	/* Test OBJ_MULTIPLE with OBJ_UNLINK, add items back afterwards */
	num = lim < 25 ? lim : 25;
	if (!(mult_it = ao2_t_callback(c1, OBJ_MULTIPLE | OBJ_UNLINK, multiple_cb, &num, "test multiple"))) {
		ast_test_status_update(test, "OBJ_MULTIPLE with OBJ_UNLINK test failed.\n");
		res = AST_TEST_FAIL;
	} else {
		/* make sure num items unlinked is as expected */
		if ((lim - ao2_container_count(c1)) != num) {
			ast_test_status_update(test, "OBJ_MULTIPLE | OBJ_UNLINK test failed, did not unlink correct number of objects.\n");
			res = AST_TEST_FAIL;
		}
		if (ao2_container_check(c1, 0)) {
			ast_test_status_update(test, "container integrity check failed\n");
			res = AST_TEST_FAIL;
			goto cleanup;
		}

		/* link what was unlinked back into c1 */
		while ((obj = ao2_t_iterator_next(mult_it, "test"))) {
			ao2_t_link(c1, obj, "test");
			ao2_t_ref(obj, -1, "test"); /* remove ref from iterator */
		}
		ao2_iterator_destroy(mult_it);
		if (ao2_container_check(c1, 0)) {
			ast_test_status_update(test, "container integrity check failed\n");
			res = AST_TEST_FAIL;
			goto cleanup;
		}
	}

	/* Test OBJ_MULTIPLE without unlink and iterate the returned container */
	num = 5;
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
	num = 5;
	if (!(mult_it = ao2_t_callback(c1, OBJ_MULTIPLE, multiple_cb, &num, "test multiple"))) {
		ast_test_status_update(test, "OBJ_MULTIPLE with no OBJ_UNLINK and no iterating failed.\n");
		res = AST_TEST_FAIL;
	} else {
		ao2_iterator_destroy(mult_it);
	}

	/* Is the container count what we expect after all the finds and unlinks? */
	if (ao2_container_count(c1) != lim) {
		ast_test_status_update(test, "container count does not match what is expected after ao2_find tests.\n");
		res = AST_TEST_FAIL;
	}

	/* Testing iterator.  Unlink a single object and break. do not add item back */
	it = ao2_iterator_init(c1, 0);
	num = ast_random() % lim; /* remove a random object */
	if (!num) {
		/*
		 * Well we cannot remove object zero because of test with
		 * all_but_one_cb later.
		 */
		num = 1;
	}
	while ((obj = ao2_t_iterator_next(&it, "test"))) {
		if (obj->i == num) {
			ao2_t_unlink(c1, obj, "test");
			ao2_t_ref(obj, -1, "test");
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
	if (ao2_container_check(c1, 0)) {
		ast_test_status_update(test, "container integrity check failed\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Test unlink all with OBJ_MULTIPLE, leave a single object for the container to destroy */
	ao2_t_callback(c1, OBJ_MULTIPLE | OBJ_UNLINK | OBJ_NODATA, all_but_one_cb, NULL, "test multiple");
	/* check to make sure all test_obj destructors were called except for 1 */
	if (destructor_count != 1) {
		ast_test_status_update(test, "OBJ_MULTIPLE | OBJ_UNLINK | OBJ_NODATA failed. destructor count %d\n", destructor_count);
		res = AST_TEST_FAIL;
	}
	if (ao2_container_check(c1, 0)) {
		ast_test_status_update(test, "container integrity check failed\n");
		res = AST_TEST_FAIL;
	}
#if defined(TEST_CONTAINER_DEBUG_DUMP)
	ao2_container_dump(c1, 0, "test_1 c1", (void *) test, (ao2_prnt_fn *) ast_test_debug, test_prnt_obj);
	ao2_container_stats(c1, 0, "test_1 c1", (void *) test, (ao2_prnt_fn *) ast_test_debug);
#endif	/* defined(TEST_CONTAINER_DEBUG_DUMP) */

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
		info->summary = "Test ao2 objects, containers, callbacks, and iterators";
		info->description =
			"Builds ao2_containers with various item numbers, bucket sizes, cmp and hash "
			"functions. Runs a series of tests to manipulate the container using callbacks "
			"and iterators.  Verifies expected behavior.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Test number, container_type, use_sort, number of objects. */
	if ((res = astobj2_test_1_helper(1, TEST_CONTAINER_LIST, 0, 50, test)) == AST_TEST_FAIL) {
		return res;
	}

	if ((res = astobj2_test_1_helper(2, TEST_CONTAINER_LIST, 1, 50, test)) == AST_TEST_FAIL) {
		return res;
	}

	if ((res = astobj2_test_1_helper(3, TEST_CONTAINER_HASH, 0, 1000, test)) == AST_TEST_FAIL) {
		return res;
	}

	if ((res = astobj2_test_1_helper(4, TEST_CONTAINER_HASH, 1, 1000, test)) == AST_TEST_FAIL) {
		return res;
	}

	if ((res = astobj2_test_1_helper(4, TEST_CONTAINER_RBTREE, 1, 1000, test)) == AST_TEST_FAIL) {
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

	c = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, test_cmp_cb);
	if (!c) {
		ast_test_status_update(test, "ao2_container_alloc_list failed.\n");
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
	if (ao2_container_check(c, 0)) {
		ast_test_status_update(test, "container integrity check failed\n");
		res = AST_TEST_FAIL;
		goto cleanup;
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

static AO2_GLOBAL_OBJ_STATIC(astobj2_holder);

AST_TEST_DEFINE(astobj2_test_3)
{
	int res = AST_TEST_PASS;
	int destructor_count = 0;
	int num_objects = 0;
	struct test_obj *obj = NULL;
	struct test_obj *obj2 = NULL;
	struct test_obj *obj3 = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "astobj2_test3";
		info->category = "/main/astobj2/";
		info->summary = "Test global ao2 holder";
		info->description =
			"This test is to see if the global ao2 holder works as intended.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Put an object in the holder */
	obj = ao2_alloc(sizeof(struct test_obj), test_obj_destructor);
	if (!obj) {
		ast_test_status_update(test, "ao2_alloc failed.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	obj->destructor_count = &destructor_count;
	obj->i = ++num_objects;
	obj2 = ao2_t_global_obj_replace(astobj2_holder, obj, "Save object in the holder");
	if (obj2) {
		ast_test_status_update(test, "Returned object not expected.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Save object for next check. */
	obj3 = obj;

	/* Replace an object in the holder */
	obj = ao2_alloc(sizeof(struct test_obj), test_obj_destructor);
	if (!obj) {
		ast_test_status_update(test, "ao2_alloc failed.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	obj->destructor_count = &destructor_count;
	obj->i = ++num_objects;
	obj2 = ao2_t_global_obj_replace(astobj2_holder, obj, "Replace object in the holder");
	if (!obj2) {
		ast_test_status_update(test, "Expected an object.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (obj2 != obj3) {
		ast_test_status_update(test, "Replaced object not expected object.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	ao2_ref(obj3, -1);
	obj3 = NULL;
	ao2_ref(obj2, -1);
	obj2 = NULL;
	ao2_ref(obj, -1);

	/* Replace with unref of an object in the holder */
	obj = ao2_alloc(sizeof(struct test_obj), test_obj_destructor);
	if (!obj) {
		ast_test_status_update(test, "ao2_alloc failed.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	obj->destructor_count = &destructor_count;
	obj->i = ++num_objects;
	if (!ao2_t_global_obj_replace_unref(astobj2_holder, obj, "Replace w/ unref object in the holder")) {
		ast_test_status_update(test, "Expected an object to be replaced.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	/* Save object for next check. */
	obj3 = obj;

	/* Get reference to held object. */
	obj = ao2_t_global_obj_ref(astobj2_holder, "Get a held object reference");
	if (!obj) {
		ast_test_status_update(test, "Expected an object.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	if (obj != obj3) {
		ast_test_status_update(test, "Referenced object not expected object.\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}
	ao2_ref(obj3, -1);
	obj3 = NULL;
	ao2_ref(obj, -1);
	obj = NULL;

	/* Release the object in the global holder. */
	ao2_t_global_obj_release(astobj2_holder, "Check release all objects");
	destructor_count += num_objects;
	if (0 < destructor_count) {
		ast_test_status_update(test,
			"all destructors were not called, destructor count is %d\n",
			destructor_count);
		res = AST_TEST_FAIL;
	} else if (destructor_count < 0) {
		ast_test_status_update(test,
			"Destructor was called too many times, destructor count is %d\n",
			destructor_count);
		res = AST_TEST_FAIL;
	}

cleanup:
	if (obj) {
		ao2_t_ref(obj, -1, "Test cleanup external object 1");
	}
	if (obj2) {
		ao2_t_ref(obj2, -1, "Test cleanup external object 2");
	}
	if (obj3) {
		ao2_t_ref(obj3, -1, "Test cleanup external object 3");
	}
	ao2_t_global_obj_release(astobj2_holder, "Test cleanup holder");

	return res;
}

/*!
 * \internal
 * \brief Make a nonsorted container for astobj2 testing.
 * \since 12.0.0
 *
 * \param type Container type to create.
 * \param options Container options
 *
 * \retval container on success.
 * \retval NULL on error.
 */
static struct ao2_container *test_make_nonsorted(enum test_container_type type, int options)
{
	struct ao2_container *container;

	container = NULL;
	switch (type) {
	case TEST_CONTAINER_LIST:
		container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, options,
			NULL, test_cmp_cb);
		break;
	case TEST_CONTAINER_HASH:
		container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, options, 5,
			test_hash_cb, NULL, test_cmp_cb);
		break;
	case TEST_CONTAINER_RBTREE:
		/* Container type must be sorted. */
		break;
	}

	return container;
}

/*!
 * \internal
 * \brief Make a sorted container for astobj2 testing.
 * \since 12.0.0
 *
 * \param type Container type to create.
 * \param options Container options
 *
 * \retval container on success.
 * \retval NULL on error.
 */
static struct ao2_container *test_make_sorted(enum test_container_type type, int options)
{
	struct ao2_container *container;

	container = NULL;
	switch (type) {
	case TEST_CONTAINER_LIST:
		container = ao2_t_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, options,
			test_sort_cb, test_cmp_cb, "test");
		break;
	case TEST_CONTAINER_HASH:
		container = ao2_t_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, options, 5,
			test_hash_cb, test_sort_cb, test_cmp_cb, "test");
		break;
	case TEST_CONTAINER_RBTREE:
		container = ao2_t_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX, options,
			test_sort_cb, test_cmp_cb, "test");
		break;
	}

	return container;
}

/*!
 * \internal
 * \brief Insert the given test vector into the given container.
 * \since 12.0.0
 *
 * \note The given test vector must not have any duplicates.
 *
 * \param container Container to insert the test vector.
 * \param destroy_counter What to increment when the object is destroyed.
 * \param vector Test vector to insert.
 * \param count Number of objects in the vector.
 * \param prefix Test output prefix string.
 * \param test Test output controller.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int insert_test_vector(struct ao2_container *container, int *destroy_counter, const int *vector, int count, const char *prefix, struct ast_test *test)
{
	int idx;
	struct test_obj *obj;

	for (idx = 0; idx < count; ++idx) {
		obj = ao2_alloc(sizeof(struct test_obj), test_obj_destructor);
		if (!obj) {
			ast_test_status_update(test, "%s: ao2_alloc failed.\n", prefix);
			return -1;
		}
		if (destroy_counter) {
			/* This object ultimately needs to be destroyed. */
			++*destroy_counter;
		}
		obj->destructor_count = destroy_counter;
		obj->i = vector[idx];
		ao2_link(container, obj);
		ao2_t_ref(obj, -1, "test");
		if (ao2_container_check(container, 0)) {
			ast_test_status_update(test, "%s: Container integrity check failed linking vector[%d]:%d\n",
				prefix, idx, vector[idx]);
			return -1;
		}

		if (ao2_container_count(container) != idx + 1) {
			ast_test_status_update(test,
				"%s: Unexpected container count.  Expected:%d Got:%d\n",
				prefix, idx + 1, ao2_container_count(container));
			return -1;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Insert duplicates of number into the given container.
 * \since 12.0.0
 *
 * \note The given container must not already have the number in it.
 *
 * \param container Container to insert the duplicates.
 * \param destroy_counter What to increment when the object is destroyed.
 * \param number Number of object to duplicate.
 * \param prefix Test output prefix string.
 * \param test Test output controller.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int insert_test_duplicates(struct ao2_container *container, int *destroy_counter, int number, const char *prefix, struct ast_test *test)
{
	int count;
	struct test_obj *obj;
	struct test_obj *obj_dup;

	/* Check if object already exists in the container. */
	obj = ao2_find(container, &number, OBJ_KEY);
	if (obj) {
		ast_test_status_update(test, "%s: Object %d already exists.\n", prefix, number);
		ao2_t_ref(obj, -1, "test");
		return -1;
	}

	/* Add three duplicate keyed objects. */
	obj_dup = NULL;
	for (count = 0; count < 4; ++count) {
		obj = ao2_alloc(sizeof(struct test_obj), test_obj_destructor);
		if (!obj) {
			ast_test_status_update(test, "%s: ao2_alloc failed.\n", prefix);
			if (obj_dup) {
				ao2_t_ref(obj_dup, -1, "test");
			}
			return -1;
		}
		if (destroy_counter) {
			/* This object ultimately needs to be destroyed. */
			++*destroy_counter;
		}
		obj->destructor_count = destroy_counter;
		obj->i = number;
		obj->dup_number = count;
		ao2_link(container, obj);

		if (count == 2) {
			/* Duplicate this object. */
			obj_dup = obj;
		} else {
			ao2_t_ref(obj, -1, "test");
		}

		if (ao2_container_check(container, 0)) {
			ast_test_status_update(test, "%s: Container integrity check failed linking num:%d dup:%d\n",
				prefix, number, count);
			if (obj_dup) {
				ao2_t_ref(obj_dup, -1, "test");
			}
			return -1;
		}
	}

	/* Add the duplicate object. */
	ao2_link(container, obj_dup);
	ao2_t_ref(obj_dup, -1, "test");

	if (ao2_container_check(container, 0)) {
		ast_test_status_update(test, "%s: Container integrity check failed linking obj_dup\n",
			prefix);
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Iterate over the container and compare the objects with the given vector.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param container Container to iterate.
 * \param flags Flags controlling the iteration.
 * \param vector Expected vector to find.
 * \param count Number of objects in the vector.
 * \param prefix Test output prefix string.
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_ao2_iteration(int res, struct ao2_container *container,
	enum ao2_iterator_flags flags,
	const int *vector, int count, const char *prefix, struct ast_test *test)
{
	struct ao2_iterator iter;
	struct test_obj *obj;
	int idx;

	if (ao2_container_count(container) != count) {
		ast_test_status_update(test, "%s: Container count doesn't match vector count.\n",
			prefix);
		res = AST_TEST_FAIL;
	}

	iter = ao2_iterator_init(container, flags);

	/* Check iterated objects against the given vector. */
	for (idx = 0; idx < count; ++idx) {
		obj = ao2_iterator_next(&iter);
		if (!obj) {
			ast_test_status_update(test, "%s: Too few objects found.\n", prefix);
			res = AST_TEST_FAIL;
			break;
		}
		if (vector[idx] != obj->i) {
			ast_test_status_update(test, "%s: Object %d != vector[%d] %d.\n",
				prefix, obj->i, idx, vector[idx]);
			res = AST_TEST_FAIL;
		}
		ao2_ref(obj, -1); /* remove ref from iterator */
	}
	obj = ao2_iterator_next(&iter);
	if (obj) {
		ast_test_status_update(test, "%s: Too many objects found.  Object %d\n",
			prefix, obj->i);
		ao2_ref(obj, -1); /* remove ref from iterator */
		res = AST_TEST_FAIL;
	}

	ao2_iterator_destroy(&iter);

	return res;
}

/*!
 * \internal
 * \brief Run an ao2_callback() and compare the returned vector with the given vector.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param container Container to traverse.
 * \param flags Callback flags controlling the traversal.
 * \param cmp_fn Compare function to select objects.
 * \param arg Optional argument.
 * \param vector Expected vector to find.
 * \param count Number of objects in the vector.
 * \param prefix Test output prefix string.
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_ao2_callback_traversal(int res, struct ao2_container *container,
	enum search_flags flags, ao2_callback_fn *cmp_fn, void *arg,
	const int *vector, int count, const char *prefix, struct ast_test *test)
{
	struct ao2_iterator *mult_iter;
	struct test_obj *obj;
	int idx;

	mult_iter = ao2_callback(container, flags | OBJ_MULTIPLE, cmp_fn, arg);
	if (!mult_iter) {
		ast_test_status_update(test, "%s: Did not return iterator.\n", prefix);
		return AST_TEST_FAIL;
	}

	/* Check matching objects against the given vector. */
	for (idx = 0; idx < count; ++idx) {
		obj = ao2_iterator_next(mult_iter);
		if (!obj) {
			ast_test_status_update(test, "%s: Too few objects found.\n", prefix);
			res = AST_TEST_FAIL;
			break;
		}
		if (vector[idx] != obj->i) {
			ast_test_status_update(test, "%s: Object %d != vector[%d] %d.\n",
				prefix, obj->i, idx, vector[idx]);
			res = AST_TEST_FAIL;
		}
		ao2_ref(obj, -1); /* remove ref from iterator */
	}
	obj = ao2_iterator_next(mult_iter);
	if (obj) {
		ast_test_status_update(test, "%s: Too many objects found.  Object %d\n",
			prefix, obj->i);
		ao2_ref(obj, -1); /* remove ref from iterator */
		res = AST_TEST_FAIL;
	}
	ao2_iterator_destroy(mult_iter);

	return res;
}

/*!
 * \internal
 * \brief Run an ao2_find() for duplicates and compare the returned vector with the given vector.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param container Container to traverse.
 * \param flags Callback flags controlling the traversal.
 * \param number Number of object to find all duplicates.
 * \param vector Expected vector to find.
 * \param count Number of objects in the vector.
 * \param prefix Test output prefix string.
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_expected_duplicates(int res, struct ao2_container *container,
	enum search_flags flags, int number,
	const int *vector, int count, const char *prefix, struct ast_test *test)
{
	struct ao2_iterator *mult_iter;
	struct test_obj *obj;
	int idx;

	mult_iter = ao2_find(container, &number, flags | OBJ_MULTIPLE | OBJ_KEY);
	if (!mult_iter) {
		ast_test_status_update(test, "%s: Did not return iterator.\n", prefix);
		return AST_TEST_FAIL;
	}

	/* Check matching objects against the given vector. */
	for (idx = 0; idx < count; ++idx) {
		obj = ao2_iterator_next(mult_iter);
		if (!obj) {
			ast_test_status_update(test, "%s: Too few objects found.\n", prefix);
			res = AST_TEST_FAIL;
			break;
		}
		if (number != obj->i) {
			ast_test_status_update(test, "%s: Object %d != %d.\n",
				prefix, obj->i, number);
			res = AST_TEST_FAIL;
		}
		if (vector[idx] != obj->dup_number) {
			ast_test_status_update(test, "%s: Object dup id %d != vector[%d] %d.\n",
				prefix, obj->dup_number, idx, vector[idx]);
			res = AST_TEST_FAIL;
		}
		ao2_ref(obj, -1); /* remove ref from iterator */
	}
	obj = ao2_iterator_next(mult_iter);
	if (obj) {
		ast_test_status_update(test,
			"%s: Too many objects found.  Object %d, dup id %d\n",
			prefix, obj->i, obj->dup_number);
		ao2_ref(obj, -1); /* remove ref from iterator */
		res = AST_TEST_FAIL;
	}
	ao2_iterator_destroy(mult_iter);

	return res;
}

/*!
 * \internal
 * \brief Test nonsorted container traversal.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param tst_num Test number.
 * \param type Container type to test.
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_traversal_nonsorted(int res, int tst_num, enum test_container_type type, struct ast_test *test)
{
	struct ao2_container *c1;
	struct ao2_container *c2 = NULL;
	int partial;
	int destructor_count = 0;

	/*! Container object insertion vector. */
	static const int test_initial[] = {
		1, 0, 2, 6, 4, 7, 5, 3, 9, 8
	};

	/*! Container object insertion vector reversed. */
	static const int test_reverse[] = {
		8, 9, 3, 5, 7, 4, 6, 2, 0, 1
	};
	static const int test_list_partial_forward[] = {
		6, 7, 5
	};
	static const int test_list_partial_backward[] = {
		5, 7, 6
	};

	/* The hash orders assume that there are 5 buckets. */
	static const int test_hash_end_forward[] = {
		0, 5, 1, 6, 2, 7, 3, 8, 4, 9
	};
	static const int test_hash_end_backward[] = {
		9, 4, 8, 3, 7, 2, 6, 1, 5, 0
	};
	static const int test_hash_begin_forward[] = {
		5, 0, 6, 1, 7, 2, 8, 3, 9, 4
	};
	static const int test_hash_begin_backward[] = {
		4, 9, 3, 8, 2, 7, 1, 6, 0, 5
	};
	static const int test_hash_partial_forward[] = {
		5, 6, 7
	};
	static const int test_hash_partial_backward[] = {
		7, 6, 5
	};

	ast_test_status_update(test, "Test %d, %s containers.\n",
		tst_num, test_container2str(type));

	/* Create container that inserts objects at the end. */
	c1 = test_make_nonsorted(type, 0);
	if (!c1) {
		ast_test_status_update(test, "Container c1 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c1, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c1", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

	/* Create container that inserts objects at the beginning. */
	c2 = test_make_nonsorted(type, AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN);
	if (!c2) {
		ast_test_status_update(test, "Container c2 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c2, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c2", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

	/* Check container iteration directions */
	switch (type) {
	case TEST_CONTAINER_LIST:
		res = test_ao2_iteration(res, c1, 0,
			test_initial, ARRAY_LEN(test_initial),
			"Iteration (ascending, insert end)", test);
		res = test_ao2_iteration(res, c1, AO2_ITERATOR_DESCENDING,
			test_reverse, ARRAY_LEN(test_reverse),
			"Iteration (descending, insert end)", test);

		res = test_ao2_iteration(res, c2, 0,
			test_reverse, ARRAY_LEN(test_reverse),
			"Iteration (ascending, insert begin)", test);
		res = test_ao2_iteration(res, c2, AO2_ITERATOR_DESCENDING,
			test_initial, ARRAY_LEN(test_initial),
			"Iteration (descending, insert begin)", test);
		break;
	case TEST_CONTAINER_HASH:
		res = test_ao2_iteration(res, c1, 0,
			test_hash_end_forward, ARRAY_LEN(test_hash_end_forward),
			"Iteration (ascending, insert end)", test);
		res = test_ao2_iteration(res, c1, AO2_ITERATOR_DESCENDING,
			test_hash_end_backward, ARRAY_LEN(test_hash_end_backward),
			"Iteration (descending, insert end)", test);

		res = test_ao2_iteration(res, c2, 0,
			test_hash_begin_forward, ARRAY_LEN(test_hash_begin_forward),
			"Iteration (ascending, insert begin)", test);
		res = test_ao2_iteration(res, c2, AO2_ITERATOR_DESCENDING,
			test_hash_begin_backward, ARRAY_LEN(test_hash_begin_backward),
			"Iteration (descending, insert begin)", test);
		break;
	case TEST_CONTAINER_RBTREE:
		break;
	}

	/* Check container traversal directions */
	switch (type) {
	case TEST_CONTAINER_LIST:
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_ASCENDING, NULL, NULL,
			test_initial, ARRAY_LEN(test_initial),
			"Traversal (ascending, insert end)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_DESCENDING, NULL, NULL,
			test_reverse, ARRAY_LEN(test_reverse),
			"Traversal (descending, insert end)", test);

		res = test_ao2_callback_traversal(res, c2, OBJ_ORDER_ASCENDING, NULL, NULL,
			test_reverse, ARRAY_LEN(test_reverse),
			"Traversal (ascending, insert begin)", test);
		res = test_ao2_callback_traversal(res, c2, OBJ_ORDER_DESCENDING, NULL, NULL,
			test_initial, ARRAY_LEN(test_initial),
			"Traversal (descending, insert begin)", test);
		break;
	case TEST_CONTAINER_HASH:
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_ASCENDING, NULL, NULL,
			test_hash_end_forward, ARRAY_LEN(test_hash_end_forward),
			"Traversal (ascending, insert end)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_DESCENDING, NULL, NULL,
			test_hash_end_backward, ARRAY_LEN(test_hash_end_backward),
			"Traversal (descending, insert end)", test);

		res = test_ao2_callback_traversal(res, c2, OBJ_ORDER_ASCENDING, NULL, NULL,
			test_hash_begin_forward, ARRAY_LEN(test_hash_begin_forward),
			"Traversal (ascending, insert begin)", test);
		res = test_ao2_callback_traversal(res, c2, OBJ_ORDER_DESCENDING, NULL, NULL,
			test_hash_begin_backward, ARRAY_LEN(test_hash_begin_backward),
			"Traversal (descending, insert begin)", test);
		break;
	case TEST_CONTAINER_RBTREE:
		break;
	}

	/* Check traversal with OBJ_PARTIAL_KEY search range. */
	partial = 6;
	partial_key_match_range = 1;
	switch (type) {
	case TEST_CONTAINER_LIST:
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_ASCENDING,
			test_cmp_cb, &partial,
			test_list_partial_forward, ARRAY_LEN(test_list_partial_forward),
			"Traversal OBJ_PARTIAL_KEY (ascending)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_DESCENDING,
			test_cmp_cb, &partial,
			test_list_partial_backward, ARRAY_LEN(test_list_partial_backward),
			"Traversal OBJ_PARTIAL_KEY (descending)", test);
		break;
	case TEST_CONTAINER_HASH:
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_ASCENDING,
			test_cmp_cb, &partial,
			test_hash_partial_forward, ARRAY_LEN(test_hash_partial_forward),
			"Traversal OBJ_PARTIAL_KEY (ascending)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_DESCENDING,
			test_cmp_cb, &partial,
			test_hash_partial_backward, ARRAY_LEN(test_hash_partial_backward),
			"Traversal OBJ_PARTIAL_KEY (descending)", test);
		break;
	case TEST_CONTAINER_RBTREE:
		break;
	}

test_cleanup:
	/* destroy containers */
	if (c1) {
		ao2_t_ref(c1, -1, "bye c1");
	}
	if (c2) {
		ao2_t_ref(c2, -1, "bye c2");
	}

	if (destructor_count > 0) {
		ast_test_status_update(test,
			"all destructors were not called, destructor count is %d\n",
			destructor_count);
		res = AST_TEST_FAIL;
	} else if (destructor_count < 0) {
		ast_test_status_update(test,
			"Destructor was called too many times, destructor count is %d\n",
			destructor_count);
		res = AST_TEST_FAIL;
	}

	return res;
}

/*!
 * \internal
 * \brief Test sorted container traversal.
 * \since 12.0.0
 *
 * \param res Passed in enum ast_test_result_state.
 * \param tst_num Test number.
 * \param type Container type to test.
 * \param test Test output controller.
 *
 * \return enum ast_test_result_state
 */
static int test_traversal_sorted(int res, int tst_num, enum test_container_type type, struct ast_test *test)
{
	struct ao2_container *c1;
	struct ao2_container *c2 = NULL;
	int partial;
	int destructor_count = 0;
	int duplicate_number = 100;

	/*! Container object insertion vector. */
	static const int test_initial[] = {
		1, 0, 2, 6, 4, 7, 5, 3, 9, 8
	};

	/*! Container forward traversal/iteration. */
	static const int test_forward[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9
	};
	/*! Container backward traversal/iteration. */
	static const int test_backward[] = {
		9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	};

	static const int test_partial_forward[] = {
		5, 6, 7
	};
	static const int test_partial_backward[] = {
		7, 6, 5
	};

	/* The hash orders assume that there are 5 buckets. */
	static const int test_hash_forward[] = {
		0, 5, 1, 6, 2, 7, 3, 8, 4, 9
	};
	static const int test_hash_backward[] = {
		9, 4, 8, 3, 7, 2, 6, 1, 5, 0
	};
	static const int test_hash_partial_forward[] = {
		5, 6, 7
	};
	static const int test_hash_partial_backward[] = {
		7, 6, 5
	};

	/* Duplicate identifier order */
	static const int test_dup_allow_forward[] = {
		0, 1, 2, 3, 2
	};
	static const int test_dup_allow_backward[] = {
		2, 3, 2, 1, 0
	};
	static const int test_dup_reject[] = {
		0
	};
	static const int test_dup_obj_reject_forward[] = {
		0, 1, 2, 3
	};
	static const int test_dup_obj_reject_backward[] = {
		3, 2, 1, 0
	};
	static const int test_dup_replace[] = {
		2
	};

	ast_test_status_update(test, "Test %d, %s containers.\n",
		tst_num, test_container2str(type));

	/* Create container that inserts duplicate objects after matching objects. */
	c1 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW);
	if (!c1) {
		ast_test_status_update(test, "Container c1 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c1, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c1(DUPS_ALLOW)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

	/* Create container that inserts duplicate objects before matching objects. */
	c2 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN | AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW);
	if (!c2) {
		ast_test_status_update(test, "Container c2 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c2, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c2(DUPS_ALLOW)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

#if defined(TEST_CONTAINER_DEBUG_DUMP)
	ao2_container_dump(c1, 0, "c1(DUPS_ALLOW)", (void *) test, (ao2_prnt_fn *) ast_test_debug, test_prnt_obj);
	ao2_container_stats(c1, 0, "c1(DUPS_ALLOW)", (void *) test, (ao2_prnt_fn *) ast_test_debug);
	ao2_container_dump(c2, 0, "c2(DUPS_ALLOW)", (void *) test, (ao2_prnt_fn *) ast_test_debug, test_prnt_obj);
	ao2_container_stats(c2, 0, "c2(DUPS_ALLOW)", (void *) test, (ao2_prnt_fn *) ast_test_debug);
#endif	/* defined(TEST_CONTAINER_DEBUG_DUMP) */

	/* Check container iteration directions */
	switch (type) {
	case TEST_CONTAINER_RBTREE:
	case TEST_CONTAINER_LIST:
		res = test_ao2_iteration(res, c1, 0,
			test_forward, ARRAY_LEN(test_forward),
			"Iteration (ascending)", test);
		res = test_ao2_iteration(res, c1, AO2_ITERATOR_DESCENDING,
			test_backward, ARRAY_LEN(test_backward),
			"Iteration (descending)", test);
		break;
	case TEST_CONTAINER_HASH:
		res = test_ao2_iteration(res, c1, 0,
			test_hash_forward, ARRAY_LEN(test_hash_forward),
			"Iteration (ascending)", test);
		res = test_ao2_iteration(res, c1, AO2_ITERATOR_DESCENDING,
			test_hash_backward, ARRAY_LEN(test_hash_backward),
			"Iteration (descending)", test);
		break;
	}

	/* Check container traversal directions */
	switch (type) {
	case TEST_CONTAINER_RBTREE:
	case TEST_CONTAINER_LIST:
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_ASCENDING, NULL, NULL,
			test_forward, ARRAY_LEN(test_forward),
			"Traversal (ascending)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_DESCENDING, NULL, NULL,
			test_backward, ARRAY_LEN(test_backward),
			"Traversal (descending)", test);
		break;
	case TEST_CONTAINER_HASH:
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_ASCENDING, NULL, NULL,
			test_hash_forward, ARRAY_LEN(test_hash_forward),
			"Traversal (ascending, insert end)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_ORDER_DESCENDING, NULL, NULL,
			test_hash_backward, ARRAY_LEN(test_hash_backward),
			"Traversal (descending)", test);
		break;
	}

	/* Check traversal with OBJ_PARTIAL_KEY search range. */
	partial = 6;
	partial_key_match_range = 1;
	switch (type) {
	case TEST_CONTAINER_RBTREE:
	case TEST_CONTAINER_LIST:
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_ASCENDING,
			test_cmp_cb, &partial,
			test_partial_forward, ARRAY_LEN(test_partial_forward),
			"Traversal OBJ_PARTIAL_KEY (ascending)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_DESCENDING,
			test_cmp_cb, &partial,
			test_partial_backward, ARRAY_LEN(test_partial_backward),
			"Traversal OBJ_PARTIAL_KEY (descending)", test);
		break;
	case TEST_CONTAINER_HASH:
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_ASCENDING,
			test_cmp_cb, &partial,
			test_hash_partial_forward, ARRAY_LEN(test_hash_partial_forward),
			"Traversal OBJ_PARTIAL_KEY (ascending)", test);
		res = test_ao2_callback_traversal(res, c1, OBJ_PARTIAL_KEY | OBJ_ORDER_DESCENDING,
			test_cmp_cb, &partial,
			test_hash_partial_backward, ARRAY_LEN(test_hash_partial_backward),
			"Traversal OBJ_PARTIAL_KEY (descending)", test);
		break;
	}

	/* Add duplicates to initial containers that allow duplicates */
	if (insert_test_duplicates(c1, &destructor_count, duplicate_number, "c1(DUPS_ALLOW)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_duplicates(c2, &destructor_count, duplicate_number, "c2(DUPS_ALLOW)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

#if defined(TEST_CONTAINER_DEBUG_DUMP)
	ao2_container_dump(c1, 0, "c1(DUPS_ALLOW) w/ dups", (void *) test, (ao2_prnt_fn *) ast_test_debug, test_prnt_obj);
	ao2_container_stats(c1, 0, "c1(DUPS_ALLOW) w/ dups", (void *) test, (ao2_prnt_fn *) ast_test_debug);
	ao2_container_dump(c2, 0, "c2(DUPS_ALLOW) w/ dups", (void *) test, (ao2_prnt_fn *) ast_test_debug, test_prnt_obj);
	ao2_container_stats(c2, 0, "c2(DUPS_ALLOW) w/ dups", (void *) test, (ao2_prnt_fn *) ast_test_debug);
#endif	/* defined(TEST_CONTAINER_DEBUG_DUMP) */

	/* Check duplicates in containers that allow duplicates. */
	res = test_expected_duplicates(res, c1, OBJ_ORDER_ASCENDING, duplicate_number,
		test_dup_allow_forward, ARRAY_LEN(test_dup_allow_forward),
		"Duplicates (ascending, DUPS_ALLOW)", test);
	res = test_expected_duplicates(res, c1, OBJ_ORDER_DESCENDING, duplicate_number,
		test_dup_allow_backward, ARRAY_LEN(test_dup_allow_backward),
		"Duplicates (descending, DUPS_ALLOW)", test);

	ao2_t_ref(c1, -1, "bye c1");
	c1 = NULL;
	ao2_t_ref(c2, -1, "bye c2");
	c2 = NULL;

	/* Create containers that reject duplicate keyed objects. */
	c1 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT);
	if (!c1) {
		ast_test_status_update(test, "Container c1 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c1, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c1(DUPS_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_duplicates(c1, &destructor_count, duplicate_number, "c1(DUPS_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	c2 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN | AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT);
	if (!c2) {
		ast_test_status_update(test, "Container c2 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c2, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c2(DUPS_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_duplicates(c2, &destructor_count, duplicate_number, "c2(DUPS_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

	/* Check duplicates in containers that reject duplicate keyed objects. */
	res = test_expected_duplicates(res, c1, OBJ_ORDER_ASCENDING, duplicate_number,
		test_dup_reject, ARRAY_LEN(test_dup_reject),
		"Duplicates (ascending, DUPS_REJECT)", test);
	res = test_expected_duplicates(res, c1, OBJ_ORDER_DESCENDING, duplicate_number,
		test_dup_reject, ARRAY_LEN(test_dup_reject),
		"Duplicates (descending, DUPS_REJECT)", test);

	ao2_t_ref(c1, -1, "bye c1");
	c1 = NULL;
	ao2_t_ref(c2, -1, "bye c2");
	c2 = NULL;

	/* Create containers that reject duplicate objects. */
	c1 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT);
	if (!c1) {
		ast_test_status_update(test, "Container c1 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c1, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c1(DUPS_OBJ_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_duplicates(c1, &destructor_count, duplicate_number, "c1(DUPS_OBJ_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	c2 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN | AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT);
	if (!c2) {
		ast_test_status_update(test, "Container c2 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c2, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c2(DUPS_OBJ_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_duplicates(c2, &destructor_count, duplicate_number, "c2(DUPS_OBJ_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

	/* Check duplicates in containers that reject duplicate objects. */
	res = test_expected_duplicates(res, c1, OBJ_ORDER_ASCENDING, duplicate_number,
		test_dup_obj_reject_forward, ARRAY_LEN(test_dup_obj_reject_forward),
		"Duplicates (ascending, DUPS_OBJ_REJECT)", test);
	res = test_expected_duplicates(res, c1, OBJ_ORDER_DESCENDING, duplicate_number,
		test_dup_obj_reject_backward, ARRAY_LEN(test_dup_obj_reject_backward),
		"Duplicates (descending, DUPS_OBJ_REJECT)", test);

	ao2_t_ref(c1, -1, "bye c1");
	c1 = NULL;
	ao2_t_ref(c2, -1, "bye c2");
	c2 = NULL;

	/* Create container that replaces duplicate keyed objects. */
	c1 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE);
	if (!c1) {
		ast_test_status_update(test, "Container c1 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c1, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c1(DUPS_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_duplicates(c1, &destructor_count, duplicate_number, "c1(DUPS_REJECT)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	c2 = test_make_sorted(type, AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN | AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE);
	if (!c2) {
		ast_test_status_update(test, "Container c2 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_vector(c2, &destructor_count, test_initial, ARRAY_LEN(test_initial), "c2(DUPS_REPLACE)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}
	if (insert_test_duplicates(c2, &destructor_count, duplicate_number, "c2(DUPS_REPLACE)", test)) {
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

	/* Check duplicates in containers that replaces duplicate keyed objects. */
	res = test_expected_duplicates(res, c1, OBJ_ORDER_ASCENDING, duplicate_number,
		test_dup_replace, ARRAY_LEN(test_dup_replace),
		"Duplicates (ascending, DUPS_REPLACE)", test);
	res = test_expected_duplicates(res, c1, OBJ_ORDER_DESCENDING, duplicate_number,
		test_dup_replace, ARRAY_LEN(test_dup_replace),
		"Duplicates (descending, DUPS_REPLACE)", test);

test_cleanup:
	/* destroy containers */
	if (c1) {
		ao2_t_ref(c1, -1, "bye c1");
	}
	if (c2) {
		ao2_t_ref(c2, -1, "bye c2");
	}

	if (destructor_count > 0) {
		ast_test_status_update(test,
			"all destructors were not called, destructor count is %d\n",
			destructor_count);
		res = AST_TEST_FAIL;
	} else if (destructor_count < 0) {
		ast_test_status_update(test,
			"Destructor was called too many times, destructor count is %d\n",
			destructor_count);
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(astobj2_test_4)
{
	int res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "astobj2_test4";
		info->category = "/main/astobj2/";
		info->summary = "Test container traversal/iteration";
		info->description =
			"This test is to see if the container traversal/iteration works "
			"as intended for each supported container type.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = test_traversal_nonsorted(res, 1, TEST_CONTAINER_LIST, test);
	res = test_traversal_nonsorted(res, 2, TEST_CONTAINER_HASH, test);

	res = test_traversal_sorted(res, 3, TEST_CONTAINER_LIST, test);
	res = test_traversal_sorted(res, 4, TEST_CONTAINER_HASH, test);
	res = test_traversal_sorted(res, 5, TEST_CONTAINER_RBTREE, test);

	return res;
}

static enum ast_test_result_state test_performance(struct ast_test *test,
	enum test_container_type type, unsigned int copt)
{
/*!
 * \brief The number of objects inserted and searched for in the container under test.
 */
#define OBJS 73
	int res = AST_TEST_PASS;
	struct ao2_container *c1 = NULL;
	struct test_obj *tobj[OBJS];
	struct test_obj *tobj2;
	int i;

	switch (type) {
	case TEST_CONTAINER_HASH:
		c1 = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, copt, 17,
			test_hash_cb, test_sort_cb, test_cmp_cb);
		break;
	case TEST_CONTAINER_LIST:
		c1 = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, copt,
			test_sort_cb, test_cmp_cb);
		break;
	case TEST_CONTAINER_RBTREE:
		c1 = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX, copt,
			test_sort_cb, test_cmp_cb);
		break;
	}

	for (i = 0; i < OBJS; i++) {
		tobj[i] = NULL;
	}

	if (!c1) {
		ast_test_status_update(test, "Container c1 creation failed.\n");
		res = AST_TEST_FAIL;
		goto test_cleanup;
	}

	for (i = 0; i < OBJS; i++) {
		tobj[i] = ao2_alloc(sizeof(struct test_obj), test_obj_destructor);
		if (!tobj[i]) {
			ast_test_status_update(test, "test object creation failed.\n");
			res = AST_TEST_FAIL;
			goto test_cleanup;
		}
		tobj[i]->i = i;
		ao2_link(c1, tobj[i]);
	}

	for (i = 0; i < OBJS; i++) {
		if ((!(tobj2 = ao2_find(c1, &i, OBJ_KEY)))) {
			ast_test_status_update(test, "Should have found object %d in container.\n", i);
			res = AST_TEST_FAIL;
			goto test_cleanup;
		}
		ao2_ref(tobj2, -1);
		tobj2 = NULL;
	}

test_cleanup:
	for (i = 0; i < OBJS ; i++) {
		ao2_cleanup(tobj[i]);
	}
	ao2_cleanup(c1);
	return res;
}

static enum ast_test_result_state testloop(struct ast_test *test,
	enum test_container_type type, int copt, int iterations)
{
	int res = AST_TEST_PASS;
	int i;
	int reportcount = iterations / 5;
	struct timeval start;

	start = ast_tvnow();
	for (i = 1 ; i <= iterations && res == AST_TEST_PASS ; i++) {
		if (i % reportcount == 0 && i != iterations) {
			ast_test_status_update(test, "%5.2fK traversals, %9s\n",
				i / 1000.0, test_container2str(type));
		}
		res = test_performance(test, type, copt);
	}
	ast_test_status_update(test, "%5.2fK traversals, %9s : %5lu ms\n",
		iterations / 1000.0, test_container2str(type), (unsigned long)ast_tvdiff_ms(ast_tvnow(), start));
	return res;
}

AST_TEST_DEFINE(astobj2_test_perf)
{
/*!
 * \brief The number of iteration of testloop to be performed.
 * \note
 * In order to keep the elapsed time sane, if AO2_DEBUG is defined in menuselect,
 * only 25000 iterations are performed.   Otherwise 100000.
 */
#ifdef AO2_DEBUG
#define ITERATIONS 25000
#else
#define ITERATIONS 100000
#endif

	int res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "astobj2_test_perf";
		info->category = "/main/astobj2/perf/";
		info->summary = "Test container performance";
		info->description =
			"Runs container traversal tests.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = testloop(test, TEST_CONTAINER_LIST, 0, ITERATIONS);
	if (!res) {
		return res;
	}
	res = testloop(test, TEST_CONTAINER_HASH, 0, ITERATIONS);
	if (!res) {
		return res;
	}
	res = testloop(test, TEST_CONTAINER_RBTREE, 0, ITERATIONS);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(astobj2_test_1);
	AST_TEST_UNREGISTER(astobj2_test_2);
	AST_TEST_UNREGISTER(astobj2_test_3);
	AST_TEST_UNREGISTER(astobj2_test_4);
	AST_TEST_UNREGISTER(astobj2_test_perf);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(astobj2_test_1);
	AST_TEST_REGISTER(astobj2_test_2);
	AST_TEST_REGISTER(astobj2_test_3);
	AST_TEST_REGISTER(astobj2_test_4);
	AST_TEST_REGISTER(astobj2_test_perf);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ASTOBJ2 Unit Tests");
