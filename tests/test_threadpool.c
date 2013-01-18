/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012-2013, Digium, Inc.
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
 * \brief threadpool unit tests
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
#include "asterisk/threadpool.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/astobj2.h"
#include "asterisk/logger.h"

struct test_listener_data {
	int num_active;
	int num_idle;
	int task_pushed;
	int num_tasks;
	int empty_notice;
	int was_empty;
	ast_mutex_t lock;
	ast_cond_t cond;
};

static struct test_listener_data *test_alloc(void)
{
	struct test_listener_data *tld = ast_calloc(1, sizeof(*tld));
	if (!tld) {
		return NULL;
	}
	ast_mutex_init(&tld->lock);
	ast_cond_init(&tld->cond, NULL);
	return tld;
}

static void test_state_changed(struct ast_threadpool *pool,
		struct ast_threadpool_listener *listener,
		int active_threads,
		int idle_threads)
{
	struct test_listener_data *tld = ast_threadpool_listener_get_user_data(listener);
	SCOPED_MUTEX(lock, &tld->lock);
	tld->num_active = active_threads;
	tld->num_idle = idle_threads;
	ast_log(LOG_NOTICE, "Thread state: %d active, %d idle\n", tld->num_active, tld->num_idle);
	ast_cond_signal(&tld->cond);
}

static void test_task_pushed(struct ast_threadpool *pool,
		struct ast_threadpool_listener *listener,
		int was_empty)
{
	struct test_listener_data *tld = ast_threadpool_listener_get_user_data(listener);
	SCOPED_MUTEX(lock, &tld->lock);
	tld->task_pushed = 1;
	++tld->num_tasks;
	tld->was_empty = was_empty;
	ast_cond_signal(&tld->cond);
}

static void test_emptied(struct ast_threadpool *pool,
		struct ast_threadpool_listener *listener)
{
	struct test_listener_data *tld = ast_threadpool_listener_get_user_data(listener);
	SCOPED_MUTEX(lock, &tld->lock);
	tld->empty_notice = 1;
	ast_cond_signal(&tld->cond);
}

static void test_shutdown(struct ast_threadpool_listener *listener)
{
	struct test_listener_data *tld = ast_threadpool_listener_get_user_data(listener);
	ast_cond_destroy(&tld->cond);
	ast_mutex_destroy(&tld->lock);
}

static const struct ast_threadpool_listener_callbacks test_callbacks = {
	.state_changed = test_state_changed,
	.task_pushed = test_task_pushed,
	.emptied = test_emptied,
	.shutdown = test_shutdown,
};

struct simple_task_data {
	int task_executed;
	ast_mutex_t lock;
	ast_cond_t cond;
};

static struct simple_task_data *simple_task_data_alloc(void)
{
	struct simple_task_data *std = ast_calloc(1, sizeof(*std));

	if (!std) {
		return NULL;
	}
	ast_mutex_init(&std->lock);
	ast_cond_init(&std->cond, NULL);
	return std;
}

static int simple_task(void *data)
{
	struct simple_task_data *std = data;
	SCOPED_MUTEX(lock, &std->lock);
	std->task_executed = 1;
	ast_cond_signal(&std->cond);
	return 0;
}

static enum ast_test_result_state wait_until_thread_state(struct ast_test *test, struct test_listener_data *tld, int num_active, int num_idle)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	SCOPED_MUTEX(lock, &tld->lock);

	while (!(tld->num_active == num_active && tld->num_idle == num_idle)) {
		if (ast_cond_timedwait(&tld->cond, &tld->lock, &end) == ETIMEDOUT) {
			break;
		}
	}

	if (tld->num_active != num_active && tld->num_idle != num_idle) {
		ast_test_status_update(test, "Number of active threads and idle threads not what was expected.\n");
		ast_test_status_update(test, "Expected %d active threads but got %d\n", num_active, tld->num_active);
		ast_test_status_update(test, "Expected %d idle threads but got %d\n", num_idle, tld->num_idle);
		res = AST_TEST_FAIL;
	}

	return res;
}

static void wait_for_task_pushed(struct ast_threadpool_listener *listener)
{
	struct test_listener_data *tld = ast_threadpool_listener_get_user_data(listener);
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	SCOPED_MUTEX(lock, &tld->lock);

	while (!tld->task_pushed) {
		if (ast_cond_timedwait(&tld->cond, lock, &end) == ETIMEDOUT) {
			break;
		}
	}
}

static enum ast_test_result_state wait_for_completion(struct ast_test *test, struct simple_task_data *std)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	SCOPED_MUTEX(lock, &std->lock);

	while (!std->task_executed) {
		if (ast_cond_timedwait(&std->cond, lock, &end) == ETIMEDOUT) {
			break;
		}
	}

	if (!std->task_executed) {
		ast_test_status_update(test, "Task execution did not occur\n");
		res = AST_TEST_FAIL;
	}
	return res;
}

static enum ast_test_result_state wait_for_empty_notice(struct ast_test *test, struct test_listener_data *tld)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	SCOPED_MUTEX(lock, &tld->lock);

	while (!tld->empty_notice) {
		if (ast_cond_timedwait(&tld->cond, lock, &end) == ETIMEDOUT) {
			break;
		}
	}

	if (!tld->empty_notice) {
		ast_test_status_update(test, "Test listener not notified that threadpool is empty\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

static enum ast_test_result_state listener_check(
		struct ast_test *test,
		struct ast_threadpool_listener *listener,
		int task_pushed,
		int was_empty,
		int num_tasks,
		int num_active,
		int num_idle,
		int empty_notice)
{
	struct test_listener_data *tld = ast_threadpool_listener_get_user_data(listener);
	enum ast_test_result_state res = AST_TEST_PASS;

	if (tld->task_pushed != task_pushed) {
		ast_test_status_update(test, "Expected task %sto be pushed, but it was%s\n",
				task_pushed ? "" : "not ", tld->task_pushed ? "" : " not");
		res = AST_TEST_FAIL;
	}
	if (tld->was_empty != was_empty) {
		ast_test_status_update(test, "Expected %sto be empty, but it was%s\n",
				was_empty ? "" : "not ", tld->was_empty ? "" : " not");
		res = AST_TEST_FAIL;
	}
	if (tld->num_tasks!= num_tasks) {
		ast_test_status_update(test, "Expected %d tasks to be pushed, but got %d\n",
				num_tasks, tld->num_tasks);
		res = AST_TEST_FAIL;
	}
	if (tld->num_active != num_active) {
		ast_test_status_update(test, "Expected %d active threads, but got %d\n",
				num_active, tld->num_active);
		res = AST_TEST_FAIL;
	}
	if (tld->num_idle != num_idle) {
		ast_test_status_update(test, "Expected %d idle threads, but got %d\n",
				num_idle, tld->num_idle);
		res = AST_TEST_FAIL;
	}
	if (tld->empty_notice != empty_notice) {
		ast_test_status_update(test, "Expected %s empty notice, but got %s\n",
				was_empty ? "an" : "no", tld->task_pushed ? "one" : "none");
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(threadpool_push)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std = NULL;
	struct test_listener_data *tld = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "push";
		info->category = "/main/threadpool/";
		info->summary = "Test task";
		info->description =
			"Basic threadpool test";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	std = simple_task_data_alloc();
	if (!std) {
		goto end;
	}

	ast_threadpool_push(pool, simple_task, std);

	wait_for_task_pushed(listener);

	res = listener_check(test, listener, 1, 1, 1, 0, 0, 0);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(std);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_initial_threads)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 3,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "initial_threads";
		info->category = "/main/threadpool/";
		info->summary = "Test threadpool initialization state";
		info->description =
			"Ensure that a threadpool created with a specific size contains the\n"
			"proper number of idle threads.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 3);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(tld);
	return res;
}


AST_TEST_DEFINE(threadpool_thread_creation)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "thread_creation";
		info->category = "/main/threadpool/";
		info->summary = "Test threadpool thread creation";
		info->description =
			"Ensure that threads can be added to a threadpool";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	/* Now let's create a thread. It should start active, then go
	 * idle immediately
	 */
	ast_threadpool_set_size(pool, 1);

	res = wait_until_thread_state(test, tld, 0, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_thread_destruction)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "thread_destruction";
		info->category = "/main/threadpool/";
		info->summary = "Test threadpool thread destruction";
		info->description =
			"Ensure that threads are properly destroyed in a threadpool";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	ast_threadpool_set_size(pool, 3);

	res = wait_until_thread_state(test, tld, 0, 3);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 0, 0, 0, 0, 3, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	ast_threadpool_set_size(pool, 2);

	res = wait_until_thread_state(test, tld, 0, 2);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_thread_timeout)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 2,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "thread_timeout";
		info->category = "/main/threadpool/";
		info->summary = "Test threadpool thread timeout";
		info->description =
			"Ensure that a thread with a two second timeout dies as expected.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	ast_threadpool_set_size(pool, 1);

	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 0, 0, 0, 0, 1, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 0, 0, 0, 0, 0, 0);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_one_task_one_thread)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "one_task_one_thread";
		info->category = "/main/threadpool/";
		info->summary = "Test a single task with a single thread";
		info->description =
			"Push a task into an empty threadpool, then add a thread to the pool.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	std = simple_task_data_alloc();
	if (!std) {
		goto end;
	}

	ast_threadpool_push(pool, simple_task, std);

	ast_threadpool_set_size(pool, 1);

	/* Threads added to the pool are active when they start,
	 * so the newly-created thread should immediately execute
	 * the waiting task.
	 */
	res = wait_for_completion(test, std);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(test, tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	/* After completing the task, the thread should go idle */
	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 1, 1, 0, 1, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(std);
	ast_free(tld);
	return res;

}

AST_TEST_DEFINE(threadpool_one_thread_one_task)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "one_thread_one_task";
		info->category = "/main/threadpool/";
		info->summary = "Test a single thread with a single task";
		info->description =
			"Add a thread to the pool and then push a task to it.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	std = simple_task_data_alloc();
	if (!std) {
		goto end;
	}

	ast_threadpool_set_size(pool, 1);

	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	ast_threadpool_push(pool, simple_task, std);

	res = wait_for_completion(test, std);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(test, tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	/* After completing the task, the thread should go idle */
	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 1, 1, 0, 1, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(std);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_one_thread_multiple_tasks)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std1 = NULL;
	struct simple_task_data *std2 = NULL;
	struct simple_task_data *std3 = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "one_thread_multiple_tasks";
		info->category = "/main/threadpool/";
		info->summary = "Test a single thread with multiple tasks";
		info->description =
			"Add a thread to the pool and then push three tasks to it.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	std1 = simple_task_data_alloc();
	std2 = simple_task_data_alloc();
	std3 = simple_task_data_alloc();
	if (!std1 || !std2 || !std3) {
		goto end;
	}

	ast_threadpool_set_size(pool, 1);

	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	ast_threadpool_push(pool, simple_task, std1);
	ast_threadpool_push(pool, simple_task, std2);
	ast_threadpool_push(pool, simple_task, std3);

	res = wait_for_completion(test, std1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_completion(test, std2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_completion(test, std3);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(test, tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 0, 3, 0, 1, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(std1);
	ast_free(std2);
	ast_free(std3);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_auto_increment)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std1 = NULL;
	struct simple_task_data *std2 = NULL;
	struct simple_task_data *std3 = NULL;
	struct simple_task_data *std4 = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 3,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "auto_increment";
		info->category = "/main/threadpool/";
		info->summary = "Test that the threadpool grows as tasks are added";
		info->description =
			"Create an empty threadpool and push a task to it. Once the task is\n"
			"pushed, the threadpool should add three threads and be able to\n"
			"handle the task. The threads should then go idle\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	std1 = simple_task_data_alloc();
	std2 = simple_task_data_alloc();
	std3 = simple_task_data_alloc();
	std4 = simple_task_data_alloc();
	if (!std1 || !std2 || !std3 || !std4) {
		goto end;
	}

	ast_threadpool_push(pool, simple_task, std1);

	/* Pushing the task should result in the threadpool growing
	 * by three threads. This will allow the task to actually execute
	 */
	res = wait_for_completion(test, std1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(test, tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 3);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	/* Now push three tasks into the pool and ensure the pool does not
	 * grow.
	 */
	ast_threadpool_push(pool, simple_task, std2);
	ast_threadpool_push(pool, simple_task, std3);
	ast_threadpool_push(pool, simple_task, std4);

	res = wait_for_completion(test, std2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_completion(test, std3);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_completion(test, std4);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(test, tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 3);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = listener_check(test, listener, 1, 0, 4, 0, 3, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(std1);
	ast_free(std2);
	ast_free(std3);
	ast_free(std4);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_max_size)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 3,
		.initial_size = 0,
		.max_size = 2,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "max_size";
		info->category = "/main/threadpool/";
		info->summary = "Test that the threadpool does not exceed its maximum size restriction";
		info->description =
			"Create an empty threadpool and push a task to it. Once the task is\n"
			"pushed, the threadpool should attempt to grow by three threads, but the\n"
			"pool's restrictions should only allow two threads to be added.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	std = simple_task_data_alloc();
	if (!std) {
		goto end;
	}

	ast_threadpool_push(pool, simple_task, std);

	res = wait_for_completion(test, std);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 1, 1, 0, 2, 1);
end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(std);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_reactivation)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std1 = NULL;
	struct simple_task_data *std2 = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "reactivation";
		info->category = "/main/threadpool/";
		info->summary = "Test that a threadpool reactivates when work is added";
		info->description =
			"Push a task into a threadpool. Make sure the task executes and the\n"
			"thread goes idle. Then push a second task and ensure that the thread\n"
			"awakens and executes the second task.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	std1 = simple_task_data_alloc();
	std2 = simple_task_data_alloc();
	if (!std1 || !std2) {
		goto end;
	}

	ast_threadpool_push(pool, simple_task, std1);

	ast_threadpool_set_size(pool, 1);

	res = wait_for_completion(test, std1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(test, tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 1, 1, 0, 1, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	/* Now make sure the threadpool reactivates when we add a second task */
	ast_threadpool_push(pool, simple_task, std2);

	res = wait_for_completion(test, std2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(test, tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 1, 2, 0, 1, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(std1);
	ast_free(std2);
	ast_free(tld);
	return res;

}

struct complex_task_data {
	int task_executed;
	int continue_task;
	ast_mutex_t lock;
	ast_cond_t stall_cond;
	ast_cond_t done_cond;
};

static struct complex_task_data *complex_task_data_alloc(void)
{
	struct complex_task_data *ctd = ast_calloc(1, sizeof(*ctd));

	if (!ctd) {
		return NULL;
	}
	ast_mutex_init(&ctd->lock);
	ast_cond_init(&ctd->stall_cond, NULL);
	ast_cond_init(&ctd->done_cond, NULL);
	return ctd;
}

static int complex_task(void *data)
{
	struct complex_task_data *ctd = data;
	SCOPED_MUTEX(lock, &ctd->lock);
	while (!ctd->continue_task) {
		ast_cond_wait(&ctd->stall_cond, lock);
	}
	/* We got poked. Finish up */
	ctd->task_executed = 1;
	ast_cond_signal(&ctd->done_cond);
	return 0;
}

static void poke_worker(struct complex_task_data *ctd)
{
	SCOPED_MUTEX(lock, &ctd->lock);
	ctd->continue_task = 1;
	ast_cond_signal(&ctd->stall_cond);
}

static enum ast_test_result_state wait_for_complex_completion(struct complex_task_data *ctd)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	SCOPED_MUTEX(lock, &ctd->lock);

	while (!ctd->task_executed) {
		if (ast_cond_timedwait(&ctd->done_cond, lock, &end) == ETIMEDOUT) {
			break;
		}
	}

	if (!ctd->task_executed) {
		res = AST_TEST_FAIL;
	}
	return res;
}

AST_TEST_DEFINE(threadpool_task_distribution)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct complex_task_data *ctd1 = NULL;
	struct complex_task_data *ctd2 = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "task_distribution";
		info->category = "/main/threadpool/";
		info->summary = "Test that tasks are evenly distributed to threads";
		info->description =
			"Push two tasks into a threadpool. Ensure that each is handled by\n"
			"a separate thread\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	ctd1 = complex_task_data_alloc();
	ctd2 = complex_task_data_alloc();
	if (!ctd1 || !ctd2) {
		goto end;
	}

	ast_threadpool_push(pool, complex_task, ctd1);
	ast_threadpool_push(pool, complex_task, ctd2);

	ast_threadpool_set_size(pool, 2);

	res = wait_until_thread_state(test, tld, 2, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 0, 2, 2, 0, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	/* The tasks are stalled until we poke them */
	poke_worker(ctd1);
	poke_worker(ctd2);

	res = wait_for_complex_completion(ctd1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_complex_completion(ctd2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 0, 2, 0, 2, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(ctd1);
	ast_free(ctd2);
	ast_free(tld);
	return res;
}

AST_TEST_DEFINE(threadpool_more_destruction)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct complex_task_data *ctd1 = NULL;
	struct complex_task_data *ctd2 = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld = NULL;
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.initial_size = 0,
		.max_size = 0,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "more_destruction";
		info->category = "/main/threadpool/";
		info->summary = "Test that threads are destroyed as expected";
		info->description =
			"Push two tasks into a threadpool. Set the threadpool size to 4\n"
			"Ensure that there are 2 active and 2 idle threads. Then shrink the\n"
			"threadpool down to 1 thread. Ensure that the thread leftover is active\n"
			"and ensure that both tasks complete.\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks, tld);
	if (!listener) {
		goto end;
	}

	pool = ast_threadpool_create(info->name, listener, &options);
	if (!pool) {
		goto end;
	}

	ctd1 = complex_task_data_alloc();
	ctd2 = complex_task_data_alloc();
	if (!ctd1 || !ctd2) {
		goto end;
	}

	ast_threadpool_push(pool, complex_task, ctd1);
	ast_threadpool_push(pool, complex_task, ctd2);

	ast_threadpool_set_size(pool, 4);

	res = wait_until_thread_state(test, tld, 2, 2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 0, 2, 2, 2, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	ast_threadpool_set_size(pool, 1);

	/* Shrinking the threadpool should kill off the two idle threads
	 * and one of the active threads.
	 */
	res = wait_until_thread_state(test, tld, 1, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 0, 2, 1, 0, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	/* The tasks are stalled until we poke them */
	poke_worker(ctd1);
	poke_worker(ctd2);

	res = wait_for_complex_completion(ctd1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_complex_completion(ctd2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_until_thread_state(test, tld, 0, 1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = listener_check(test, listener, 1, 0, 2, 0, 1, 1);

end:
	ast_threadpool_shutdown(pool);
	ao2_cleanup(listener);
	ast_free(ctd1);
	ast_free(ctd2);
	ast_free(tld);
	return res;
}

static int unload_module(void)
{
	ast_test_unregister(threadpool_push);
	ast_test_unregister(threadpool_initial_threads);
	ast_test_unregister(threadpool_thread_creation);
	ast_test_unregister(threadpool_thread_destruction);
	ast_test_unregister(threadpool_thread_timeout);
	ast_test_unregister(threadpool_one_task_one_thread);
	ast_test_unregister(threadpool_one_thread_one_task);
	ast_test_unregister(threadpool_one_thread_multiple_tasks);
	ast_test_unregister(threadpool_auto_increment);
	ast_test_unregister(threadpool_max_size);
	ast_test_unregister(threadpool_reactivation);
	ast_test_unregister(threadpool_task_distribution);
	ast_test_unregister(threadpool_more_destruction);
	return 0;
}

static int load_module(void)
{
	ast_test_register(threadpool_push);
	ast_test_register(threadpool_initial_threads);
	ast_test_register(threadpool_thread_creation);
	ast_test_register(threadpool_thread_destruction);
	ast_test_register(threadpool_thread_timeout);
	ast_test_register(threadpool_one_task_one_thread);
	ast_test_register(threadpool_one_thread_one_task);
	ast_test_register(threadpool_one_thread_multiple_tasks);
	ast_test_register(threadpool_auto_increment);
	ast_test_register(threadpool_max_size);
	ast_test_register(threadpool_reactivation);
	ast_test_register(threadpool_task_distribution);
	ast_test_register(threadpool_more_destruction);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "threadpool test module");
