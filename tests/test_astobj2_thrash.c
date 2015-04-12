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
 *! \file \brief Thrash a astobj2 container, for fun and profit.
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * Inspired by the original hashtest2.c by Steve Murphy <murf@digium.com>.  This test runs
 * several threads manipulatings a concurrent astobj2 container to see if they maintain
 * consistency. While the tests attempt to check consistency and error normally, threading
 * errors often result in segfaults.
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()
#include <pthread.h>
#include "asterisk/astobj2.h"
#include "asterisk/hashtab.h"
#include "asterisk/lock.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"

#define MAX_HASH_ENTRIES 15000
#define MAX_TEST_SECONDS 60

struct hash_test {
	/*! Unit under test */
	struct ao2_container *to_be_thrashed;
	/*! Number of entries to insert in the grow thread. */
	int max_grow;
	/*! Number of enteries added by the grow thread. */
	int grow_count;
	/*! Entries preloaded into the hashtab; to be deleted by the shrink thread */
	int preload;
	/*! When to give up on the tests */
	struct timeval deadline;
};

static int alloc_count = 0;

static int is_timed_out(struct hash_test const *data) {
	return ast_tvdiff_us(data->deadline, ast_tvnow()) < 0;
}

/*! /brief Free test element */
static void ht_delete(void *obj)
{
	ast_atomic_fetchadd_int(&alloc_count, -1);
}

/*! /brief Create test element */
static char *ht_new(int i)
{
	const int buflen = 12;
	char *keybuf = ao2_alloc(buflen, ht_delete);
	int needed;
	if (keybuf == NULL) {
		return NULL;
	}
	needed = snprintf(keybuf, buflen, "key%08x", (unsigned)i);
	ast_atomic_fetchadd_int(&alloc_count, 1);
	ast_assert(needed + 1 <= buflen);
	return keybuf;
}

/*! /brief Grow the hash data as specified */
static void *hash_test_grow(void *d)
{
	struct hash_test *data = d;
	int i;

	for (i = 0; i < data->max_grow; ++i) {
		char *ht;
		if (is_timed_out(data)) {
			printf("Growth timed out at %d\n", i);
			return "Growth timed out";
		}
		ht = ht_new(i);
		if (ht == NULL) {
			return "Allocation failed";
		}
		ao2_link(data->to_be_thrashed, ht);
		ao2_ref(ht, -1);
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
		char *from_ao2;

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
			return "Allocation failed";
		}
		from_ao2 = ao2_find(data->to_be_thrashed, obj, OBJ_POINTER);
		ao2_ref(obj, -1);
		ao2_ref(from_ao2, -1);
		if (from_ao2 == NULL) {
			return "Key unexpectedly missing";
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
		char *from_ao2;

		if (obj == NULL) {
			return "Allocation failed";
		}
		from_ao2 = ao2_find(data->to_be_thrashed, obj, OBJ_UNLINK | OBJ_POINTER);

		ao2_ref(obj, -1);
		if (from_ao2) {
			ao2_ref(from_ao2, -1);
		} else {
			return "Could not find object to delete";
		}

		if (is_timed_out(data)) {
			return "Shrink timed out";
		}
	}

	return NULL;
}

/*! ao2_callback for hash_test_count */
static int increment_count(void *obj, void *arg, int flags) {
	char *ht = obj;
	int *count = arg;
	if (strncmp(ht, "key0", 4) == 0) {
		++(*count);
	}
	return 0;
}

/*! Continuously iterate through all the entries in the hash */
static void *hash_test_count(void *d)
{
	const struct hash_test *data = d;
	int count = 0;
	int last_count = 0;

	while (count < data->max_grow) {
		last_count = count;
		count = 0;
		ao2_callback(data->to_be_thrashed, OBJ_MULTIPLE, increment_count, &count);

		if (last_count == count) {
			/* Allow other threads to run. */
			sched_yield();
		} else if (last_count > count) {
			/* Make sure the ao2 container never shrinks */
			return "ao2 container unexpectedly shrank";
		}

		if (is_timed_out(data)) {
			return "Count timed out";
		}
	}

	/* Successfully iterated over all of the expected elements */
	return NULL;
}

static int hash_string(const void *obj, const int flags)
{
	return ast_hashtab_hash_string_nocase(obj);
}

static int compare_strings(void *lhs, void *rhs, int flags)
{
	const char *lhs_str = lhs;
	const char *rhs_str = rhs;
	if (strcasecmp(lhs_str, rhs_str) == 0) {
		return CMP_MATCH | CMP_STOP;
	} else {
		return 0;
	}
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
		info->category = "/main/astobj2/";
		info->summary = "Testing astobj2 container concurrency";
		info->description = "Test astobj2 container concurrency correctness.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing hash concurrency test...\n");
	data.preload = MAX_HASH_ENTRIES / 2;
	data.max_grow = MAX_HASH_ENTRIES - data.preload;
	data.deadline = ast_tvadd(ast_tvnow(), ast_tv(MAX_TEST_SECONDS, 0));
	data.to_be_thrashed = ao2_container_alloc(MAX_HASH_ENTRIES / 100, hash_string,
		compare_strings);

	if (data.to_be_thrashed == NULL) {
		ast_test_status_update(test, "Allocation failed\n");
		/* Nothing needs to be freed; early return is fine */
		return AST_TEST_FAIL;
	}

	/* preload with data to delete */
	for (i = 1; i < data.preload; ++i) {
		char *ht = ht_new(-i);
		if (ht == NULL) {
			ast_test_status_update(test, "Allocation failed\n");
			ao2_ref(data.to_be_thrashed, -1);
			return AST_TEST_FAIL;
		}
		ao2_link(data.to_be_thrashed, ht);
		ao2_ref(ht, -1);
	}

	/* add data.max_grow entries to the ao2 container */
	ast_pthread_create(&grow_thread, NULL, hash_test_grow, &data);
	/* continually count the keys added by the grow thread */
	ast_pthread_create(&count_thread, NULL, hash_test_count, &data);
	/* continually lookup keys added by the grow thread */
	ast_pthread_create(&lookup_thread, NULL, hash_test_lookup, &data);
	/* delete all keys preloaded into the ao2 container */
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

	if (ao2_container_count(data.to_be_thrashed) != data.max_grow) {
		ast_test_status_update(test,
			"Invalid ao2 container size. Expected: %d, Actual: %d\n",
			data.max_grow, ao2_container_count(data.to_be_thrashed));
		res = AST_TEST_FAIL;
	}

	ao2_ref(data.to_be_thrashed, -1);

	/* check for object leaks */
	if (ast_atomic_fetchadd_int(&alloc_count, 0) != 0) {
		ast_test_status_update(test, "Leaked %d objects!\n",
			ast_atomic_fetchadd_int(&alloc_count, 0));
		res = AST_TEST_FAIL;
	}

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

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "astobj2 container thrash test");
