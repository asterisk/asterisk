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

static void *test_alloc(struct ast_threadpool_listener *listener)
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
	struct test_listener_data *tld = listener->private_data;
	SCOPED_MUTEX(lock, &tld->lock);
	ast_log(LOG_NOTICE, "State changed: num_active: %d, num_idle: %d\n", active_threads, idle_threads);
	tld->num_active = active_threads;
	tld->num_idle = idle_threads;
	ast_cond_signal(&tld->cond);
}

static void test_task_pushed(struct ast_threadpool *pool,
		struct ast_threadpool_listener *listener,
		int was_empty)
{
	struct test_listener_data *tld = listener->private_data;
	SCOPED_MUTEX(lock, &tld->lock);
	tld->task_pushed = 1;
	++tld->num_tasks;
	tld->was_empty = was_empty;
	ast_cond_signal(&tld->cond);
}

static void test_emptied(struct ast_threadpool *pool,
		struct ast_threadpool_listener *listener)
{
	struct test_listener_data *tld = listener->private_data;
	SCOPED_MUTEX(lock, &tld->lock);
	tld->empty_notice = 1;
	ast_cond_signal(&tld->cond);
}

static void test_destroy(void *private_data)
{
	struct test_listener_data *tld = private_data;
	ast_debug(1, "Poop\n");
	ast_cond_destroy(&tld->cond);
	ast_mutex_destroy(&tld->lock);
	ast_free(tld);
}

static const struct ast_threadpool_listener_callbacks test_callbacks = {
	.alloc = test_alloc,
	.state_changed = test_state_changed,
	.task_pushed = test_task_pushed,
	.emptied = test_emptied,
	.destroy = test_destroy,
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

#define WAIT_WHILE(tld, condition) \
{\
	ast_mutex_lock(&tld->lock);\
	while ((condition)) {\
		ast_cond_wait(&tld->cond, &tld->lock);\
	}\
	ast_mutex_unlock(&tld->lock);\
}\

static void wait_for_task_pushed(struct ast_threadpool_listener *listener)
{
	struct test_listener_data *tld = listener->private_data;
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	SCOPED_MUTEX(lock, &tld->lock);

	while (!tld->task_pushed) {
		ast_cond_timedwait(&tld->cond, lock, &end);
	}
}

static enum ast_test_result_state wait_for_completion(struct simple_task_data *std)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	SCOPED_MUTEX(lock, &std->lock);

	while (!std->task_executed) {
		ast_cond_timedwait(&std->cond, lock, &end);
	}

	if (!std->task_executed) {
		res = AST_TEST_FAIL;
	}
	return res;
}

static enum ast_test_result_state wait_for_empty_notice(struct test_listener_data *tld)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 5,
		.tv_nsec = start.tv_usec * 1000
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	SCOPED_MUTEX(lock, &tld->lock);

	while (!tld->empty_notice) {
		ast_cond_timedwait(&tld->cond, lock, &end);
	}

	if (!tld->empty_notice) {
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
	struct test_listener_data *tld = listener->private_data;
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
	enum ast_test_result_state res = AST_TEST_FAIL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "threadpool_push";
		info->category = "/main/threadpool/";
		info->summary = "Test task";
		info->description =
			"Basic threadpool test";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks);
	if (!listener) {
		return AST_TEST_FAIL;
	}

	pool = ast_threadpool_create(listener, 0);
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
	if (pool) {
		ast_threadpool_shutdown(pool);
	}
	ao2_cleanup(listener);
	ast_free(std);
	return res;
}

AST_TEST_DEFINE(threadpool_thread_creation)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld;

	switch (cmd) {
	case TEST_INIT:
		info->name = "threadpool_thread_creation";
		info->category = "/main/threadpool/";
		info->summary = "Test threadpool thread creation";
		info->description =
			"Ensure that threads can be added to a threadpool";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks);
	if (!listener) {
		return AST_TEST_FAIL;
	}
	tld = listener->private_data;

	pool = ast_threadpool_create(listener, 0);
	if (!pool) {
		goto end;
	}

	/* Now let's create a thread. It should start active, then go
	 * idle immediately
	 */
	ast_threadpool_set_size(pool, 1);

	WAIT_WHILE(tld, tld->num_idle == 0);

	res = listener_check(test, listener, 0, 0, 0, 0, 1, 0);

end:
	if (pool) {
		ast_threadpool_shutdown(pool);
	}
	ao2_cleanup(listener);
	return res;
}

AST_TEST_DEFINE(threadpool_thread_destruction)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld;

	switch (cmd) {
	case TEST_INIT:
		info->name = "threadpool_thread_destruction";
		info->category = "/main/threadpool/";
		info->summary = "Test threadpool thread destruction";
		info->description =
			"Ensure that threads are properly destroyed in a threadpool";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks);
	if (!listener) {
		return AST_TEST_FAIL;
	}
	tld = listener->private_data;

	pool = ast_threadpool_create(listener, 0);
	if (!pool) {
		goto end;
	}

	ast_threadpool_set_size(pool, 3);

	WAIT_WHILE(tld, tld->num_idle < 3);

	res = listener_check(test, listener, 0, 0, 0, 0, 3, 0);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	ast_threadpool_set_size(pool, 2);

	WAIT_WHILE(tld, tld->num_idle > 2);

	res = listener_check(test, listener, 0, 0, 0, 0, 2, 0);

end:
	if (pool) {
		ast_threadpool_shutdown(pool);
	}
	ao2_cleanup(listener);
	return res;
}

AST_TEST_DEFINE(threadpool_one_task_one_thread)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld;

	switch (cmd) {
	case TEST_INIT:
		info->name = "threadpool_one_task_one_thread";
		info->category = "/main/threadpool/";
		info->summary = "Test a single task with a single thread";
		info->description =
			"Push a task into an empty threadpool, then add a thread to the pool.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks);
	if (!listener) {
		return AST_TEST_FAIL;
	}
	tld = listener->private_data;

	pool = ast_threadpool_create(listener, 0);
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
	res = wait_for_completion(std);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	
	/* After completing the task, the thread should go idle */
	WAIT_WHILE(tld, tld->num_idle == 0);

	res = listener_check(test, listener, 1, 1, 1, 0, 1, 1);

end:
	if (pool) {
		ast_threadpool_shutdown(pool);
	}
	ao2_cleanup(listener);
	ast_free(std);
	return res;

}

AST_TEST_DEFINE(threadpool_one_thread_one_task)
{
	struct ast_threadpool *pool = NULL;
	struct ast_threadpool_listener *listener = NULL;
	struct simple_task_data *std = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct test_listener_data *tld;

	switch (cmd) {
	case TEST_INIT:
		info->name = "threadpool_one_thread_one_task";
		info->category = "/main/threadpool/";
		info->summary = "Test a single thread with a single task";
		info->description =
			"Add a thread to the pool and then push a task to it.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks);
	if (!listener) {
		return AST_TEST_FAIL;
	}
	tld = listener->private_data;

	pool = ast_threadpool_create(listener, 0);
	if (!pool) {
		goto end;
	}

	std = simple_task_data_alloc();
	if (!std) {
		goto end;
	}

	ast_threadpool_set_size(pool, 1);

	WAIT_WHILE(tld, tld->num_idle == 0);

	ast_threadpool_push(pool, simple_task, std);

	res = wait_for_completion(std);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	/* After completing the task, the thread should go idle */
	WAIT_WHILE(tld, tld->num_idle == 0);

	res = listener_check(test, listener, 1, 1, 1, 0, 1, 1);

end:
	if (pool) {
		ast_threadpool_shutdown(pool);
	}
	ao2_cleanup(listener);
	ast_free(std);
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
	struct test_listener_data *tld;

	switch (cmd) {
	case TEST_INIT:
		info->name = "threadpool_one_thread_multiple_tasks";
		info->category = "/main/threadpool/";
		info->summary = "Test a single thread with multiple tasks";
		info->description =
			"Add a thread to the pool and then push three tasks to it.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	listener = ast_threadpool_listener_alloc(&test_callbacks);
	if (!listener) {
		return AST_TEST_FAIL;
	}
	tld = listener->private_data;

	pool = ast_threadpool_create(listener, 0);
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

	WAIT_WHILE(tld, tld->num_idle == 0);

	ast_threadpool_push(pool, simple_task, std1);
	ast_threadpool_push(pool, simple_task, std2);
	ast_threadpool_push(pool, simple_task, std3);

	res = wait_for_completion(std1);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_completion(std2);
	if (res == AST_TEST_FAIL) {
		goto end;
	}
	res = wait_for_completion(std3);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	res = wait_for_empty_notice(tld);
	if (res == AST_TEST_FAIL) {
		goto end;
	}

	WAIT_WHILE(tld, tld->num_idle == 0);

	res = listener_check(test, listener, 1, 0, 3, 0, 1, 1);

end:
	if (pool) {
		ast_threadpool_shutdown(pool);
	}
	ao2_cleanup(listener);
	ast_free(std1);
	ast_free(std2);
	ast_free(std3);
	return res;

}

static int unload_module(void)
{
	ast_test_unregister(threadpool_push);
	ast_test_unregister(threadpool_thread_creation);
	ast_test_unregister(threadpool_thread_destruction);
	ast_test_unregister(threadpool_one_task_one_thread);
	ast_test_unregister(threadpool_one_thread_one_task);
	ast_test_unregister(threadpool_one_thread_multiple_tasks);
	return 0;
}

static int load_module(void)
{
	ast_test_register(threadpool_push);
	ast_test_register(threadpool_thread_creation);
	ast_test_register(threadpool_thread_destruction);
	ast_test_register(threadpool_one_task_one_thread);
	ast_test_register(threadpool_one_thread_one_task);
	ast_test_register(threadpool_one_thread_multiple_tasks);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "threadpool test module");
