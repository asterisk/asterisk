/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, David M. Lee, II
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

/*
 *! \file \brief Thrash a hash table, for fun and profit.
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * Inspired by the original hashtest.c by Steve Murphy <murf@digium.com>.  This test runs
 * several threads manipulating a concurrent hastab to see if they maintain
 * consistency. While the tests attempt to check consistency and error normally, threading
 * errors often result in segfaults.
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pthread.h>
#include "asterisk/hashtab.h"
#include "asterisk/lock.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"

#define MAX_HASH_ENTRIES 30000
#define MAX_TEST_SECONDS 60

struct hash_test {
	/*! Unit under test */
	struct ast_hashtab *to_be_thrashed;
	/*! Number of entries to insert in the grow thread. */
	int max_grow;
	/*! Number of entries added by the grow thread. */
	int grow_count;
	/*! Entries preloaded into the hashtab; to be deleted by the shrink thread */
	int preload;
	/*! When to give up on the tests */
	struct timeval deadline;
	/*! The actual test object */
	struct ast_test *test;
};

static int is_timed_out(struct hash_test const *data) {
	struct timeval now = ast_tvnow();
	int val = ast_tvdiff_us(data->deadline, now) < 0;
	if (val) {
		/* tv_usec is suseconds_t, which could be int or long */
		ast_test_status_update(data->test, "Now: %ld.%06ld Deadline: %ld.%06ld\n",
			now.tv_sec, (long)now.tv_usec,
			data->deadline.tv_sec, (long)data->deadline.tv_usec);
	}
	return val;
}

/*! /brief Create test element */
static char *ht_new(int i)
{
	const int buflen = 12;
	char *keybuf = ast_malloc(buflen);
	int needed;
	if (keybuf == NULL) {
		return NULL;
	}
	needed = snprintf(keybuf, buflen, "key%08x", (unsigned)i);
	ast_assert(needed + 1 <= buflen);
	return keybuf;
}

/*! /brief Free test element */
static void ht_delete(void *obj)
{
	ast_free(obj);
}

/*! /brief Grow the hash data as specified */
static void *hash_test_grow(void *d)
{
	struct hash_test *data = d;
	int i;

	for (i = 0; i < data->max_grow; ++i) {
		char *obj;
		if (is_timed_out(data)) {
			return "Growth timed out";
		}
		obj = ht_new(i);
		if (obj == NULL) {
			return "Allocation failed";
		}
		ast_hashtab_insert_immediate(data->to_be_thrashed, obj);
		ast_atomic_fetchadd_int(&data->grow_count, 1);
	}
	return NULL;
}

/*! Randomly lookup data in the hash */
static void *hash_test_lookup(void *d)
{
	struct hash_test *data = d;
	int max;
	unsigned seed = time(NULL);

	/* ast_atomic_fetchadd_int provide a memory fence so that the optimizer doesn't
	 * optimize away reads.
	 */
	while ((max = ast_atomic_fetchadd_int(&data->grow_count, 0)) < data->max_grow) {
		int i;
		char *obj;
		int is_in_hashtab;

		if (is_timed_out(data)) {
			return "Lookup timed out";
		}

		if (max == 0) {
			/* No data yet; yield and try again */
			sched_yield();
			continue;
		}

		/* Randomly lookup one object from the hash */
		i = rand_r(&seed) % max;
		obj = ht_new(i);
		if (obj == NULL) {
			return "Allocation failed.";
		}
		is_in_hashtab = (ast_hashtab_lookup(data->to_be_thrashed, obj) != NULL);
		ht_delete(obj);
		if (!is_in_hashtab) {
			return "key unexpectedly missing";
		}
	}

	return NULL;
}

/*! Delete entries from the hash */
static void *hash_test_shrink(void *d)
{
	const struct hash_test *data = d;
	int i;

	for (i = 1; i < data->preload; ++i) {
		char *obj = ht_new(-i);
		char *from_hashtab;
		int deleted;

		if (obj == NULL) {
			return "Allocation failed";
		}
		from_hashtab = ast_hashtab_remove_object_via_lookup(data->to_be_thrashed, obj);
		deleted = from_hashtab != NULL;

		ht_delete(obj);
		ht_delete(from_hashtab);
		if (!deleted) {
			return "could not delete object";
		}
		if (is_timed_out(data)) {
			return "Shrink timed out";
		}
	}
	return NULL;
}

/*! Continuously iterate through all the entries in the hash */
static void *hash_test_count(void *d)
{
	const struct hash_test *data = d;
	int count = 0;
	int last_count = 0;

	while (count < data->max_grow) {
		struct ast_hashtab_iter *it = ast_hashtab_start_write_traversal(data->to_be_thrashed);
		char *ht = ast_hashtab_next(it);
		last_count = count;
		count = 0;
		while (ht) {
			/* only count keys added by grow thread */
			if (strncmp(ht, "key0", 4) == 0) {
				++count;
			}
			ht = ast_hashtab_next(it);
		}
		ast_hashtab_end_traversal(it);

		if (last_count == count) {
			/* Give other threads ample chance to run, note that using sched_yield here does not
			 * provide enough of a chance and can cause this thread to starve others.
			 */
			usleep(1);
		} else if (last_count > count) {
			/* Make sure the hashtable never shrinks */
			return "hashtab unexpectedly shrank";
		}

		if (is_timed_out(data)) {
			return "Count timed out";
		}
	}

	/* Successfully iterated over all of the expected elements */
	return NULL;
}

AST_TEST_DEFINE(hash_test)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct hash_test data = {};
	pthread_t grow_thread, count_thread, lookup_thread, shrink_thread;
	void *thread_results;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "thrash";
		info->category = "/main/hashtab/";
		info->summary = "Testing hashtab concurrency";
		info->description = "Test hashtab concurrency correctness.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing hash concurrency test...\n");
	data.test = test;
	data.preload = MAX_HASH_ENTRIES / 2;
	data.max_grow = MAX_HASH_ENTRIES - data.preload;
	data.deadline = ast_tvadd(ast_tvnow(), ast_tv(MAX_TEST_SECONDS, 0));
	data.to_be_thrashed = ast_hashtab_create(MAX_HASH_ENTRIES / 100,
		ast_hashtab_compare_strings_nocase, ast_hashtab_resize_java,
		ast_hashtab_newsize_java, ast_hashtab_hash_string_nocase, 1);

	if (data.to_be_thrashed == NULL) {
		ast_test_status_update(test, "Allocation failed\n");
		/* Nothing needs to be freed; early return is fine */
		return AST_TEST_FAIL;
	}


	/* preload with data to delete */
	for (i = 1; i < data.preload; ++i) {
		char *obj = ht_new(-i);
		if (obj == NULL) {
			ast_test_status_update(test, "Allocation failed\n");
			ast_hashtab_destroy(data.to_be_thrashed, ht_delete);
			return AST_TEST_FAIL;
		}
		ast_hashtab_insert_immediate(data.to_be_thrashed, obj);
	}

	/* add data.max_grow entries to the hashtab */
	ast_pthread_create(&grow_thread, NULL, hash_test_grow, &data);
	/* continually count the keys added by the grow thread */
	ast_pthread_create(&count_thread, NULL, hash_test_count, &data);
	/* continually lookup keys added by the grow thread */
	ast_pthread_create(&lookup_thread, NULL, hash_test_lookup, &data);
	/* delete all keys preloaded into the hashtab */
	ast_pthread_create(&shrink_thread, NULL, hash_test_shrink, &data);

	pthread_join(grow_thread, &thread_results);
	if (thread_results != NULL) {
		ast_test_status_update(test, "Growth thread failed: %s\n",
			(char *)thread_results);
		res = AST_TEST_FAIL;
	}

	pthread_join(count_thread, &thread_results);
	if (thread_results != NULL) {
		ast_test_status_update(test, "Count thread failed: %s\n",
			(char *)thread_results);
		res = AST_TEST_FAIL;
	}

	pthread_join(lookup_thread, &thread_results);
	if (thread_results != NULL) {
		ast_test_status_update(test, "Lookup thread failed: %s\n",
			(char *)thread_results);
		res = AST_TEST_FAIL;
	}

	pthread_join(shrink_thread, &thread_results);
	if (thread_results != NULL) {
		ast_test_status_update(test, "Shrink thread failed: %s\n",
			(char *)thread_results);
		res = AST_TEST_FAIL;
	}

	if (ast_hashtab_size(data.to_be_thrashed) != data.max_grow) {
		ast_test_status_update(test,
			"Invalid hashtab size. Expected: %d, Actual: %d\n",
			data.max_grow, ast_hashtab_size(data.to_be_thrashed));
		res = AST_TEST_FAIL;
	}

	ast_hashtab_destroy(data.to_be_thrashed, ht_delete);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(hash_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(hash_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Hash test");
