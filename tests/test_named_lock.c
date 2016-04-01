/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Fairview 5 Engineering, LLC
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
 * \brief Named Lock unit tests
 *
 * \author George Joseph <george.joseph@fairview5.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include <signal.h>

#include "asterisk/test.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/named_locks.h"

static void *lock_thread(void *data)
{
	struct ast_named_lock *lock = ast_named_lock_get(AST_NAMED_LOCK_TYPE_MUTEX, "lock_test", data);

	if (!lock) {
		return NULL;
	}

	ao2_lock(lock);
	usleep(3000000);
	ao2_unlock(lock);

	ast_named_lock_put(lock);

	return NULL;
}

AST_TEST_DEFINE(named_lock_test)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_named_lock *lock1 = NULL;
	struct ast_named_lock *lock2 = NULL;
	pthread_t thread1;
	pthread_t thread2;
	struct timeval start_time;
	int64_t duration;

	switch(cmd) {
	case TEST_INIT:
		info->name = "named_lock_test";
		info->category = "/main/lock/";
		info->summary = "Named Lock test";
		info->description =
			"Tests that named locks operate as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "This test should take about 3 seconds\n");

	/* 2 locks/threads to make sure they're independent */
	ast_pthread_create(&thread1, NULL, lock_thread, "lock_1");
	ast_pthread_create(&thread2, NULL, lock_thread, "lock_2");

	lock1 = ast_named_lock_get(AST_NAMED_LOCK_TYPE_MUTEX, "lock_test", "lock_1");
	ast_test_validate_cleanup(test, lock1 != NULL, res, fail);

	lock2 = ast_named_lock_get(AST_NAMED_LOCK_TYPE_MUTEX, "lock_test", "lock_2");
	ast_test_validate_cleanup(test, lock2 != NULL, res, fail);

	usleep(1000000);

	/* These should both fail */
	if (!ao2_trylock(lock1)) {
		ast_test_status_update(test, "ao2_trylock on lock1 succeeded when it should have failed\n");
		ao2_unlock(lock1);
		goto fail;
	}

	if (!ao2_trylock(lock2)) {
		ast_test_status_update(test, "ao2_trylock on lock2 succeeded when it should have failed\n");
		ao2_unlock(lock2);
		goto fail;
	}

	start_time = ast_tvnow();

	/* These should both succeed eventually */
	if (ao2_lock(lock1)) {
		ast_test_status_update(test, "ao2_lock on lock1 failed\n");
		goto fail;
	}
	ao2_unlock(lock1);

	if (ao2_lock(lock2)) {
		ast_test_status_update(test, "ao2_lock on lock2 failed\n");
		goto fail;
	}
	ao2_unlock(lock2);

	duration = ast_tvdiff_ms(ast_tvnow(), start_time);
	ast_test_validate_cleanup(test, duration > 1500 && duration < 3500, res, fail);

	res = AST_TEST_PASS;

fail:

	ast_named_lock_put(lock1);
	ast_named_lock_put(lock2);

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);

	return res;
}


static int unload_module(void)
{
	AST_TEST_UNREGISTER(named_lock_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(named_lock_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Named Lock test module");
