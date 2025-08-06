/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Sangoma Technologies Inc
 *
 * Joshua Colp <jcolp@sangoma.com>
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
 * \brief taskpool unit tests
 *
 * \author Joshua Colp <jcolp@sangoma.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/test.h"
#include "asterisk/taskpool.h"
#include "asterisk/cli.h"

struct test_data {
	ast_mutex_t lock;
	ast_cond_t cond;
	int executed;
	struct ast_taskprocessor *taskprocessor;
};

static struct test_data *test_alloc(void)
{
	struct test_data *td = ast_calloc(1, sizeof(*td));
	if (!td) {
		return NULL;
	}
	ast_mutex_init(&td->lock);
	ast_cond_init(&td->cond, NULL);
	return td;
}

static void test_destroy(struct test_data *td)
{
	ast_mutex_destroy(&td->lock);
	ast_cond_destroy(&td->cond);
	ast_free(td);
}

static int simple_task(void *data)
{
	struct test_data *td = data;
	SCOPED_MUTEX(lock, &td->lock);
	td->taskprocessor = ast_taskpool_serializer_get_current();
	td->executed = 1;
	ast_cond_signal(&td->cond);
	return 0;
}

AST_TEST_DEFINE(taskpool_push)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.minimum_size = 1,
		.initial_size = 1,
		.max_size = 1,
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	struct timeval start;
	struct timespec end;

	switch (cmd) {
	case TEST_INIT:
		info->name = "push";
		info->category = "/main/taskpool/";
		info->summary = "Taskpool pushing test";
		info->description =
			"Pushes a single task into a taskpool asynchronously and ensures it is executed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	td = test_alloc();
	if (!td) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
	if (!pool) {
		goto end;
	}

	if (ast_taskpool_push(pool, simple_task, td)) {
		goto end;
	}

	/* It should not take more than 5 seconds for a single simple task to execute */
	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 5;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&td->lock);
	while (!td->executed && ast_cond_timedwait(&td->cond, &td->lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&td->lock);

	if (!td->executed) {
		ast_test_status_update(test, "Expected simple task to be executed but it was not\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return res;
}

AST_TEST_DEFINE(taskpool_push_synchronous)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.minimum_size = 1,
		.initial_size = 1,
		.max_size = 1,
	};
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "push_synchronous";
		info->category = "/main/taskpool/";
		info->summary = "Taskpool synchronous pushing test";
		info->description =
			"Pushes a single task into a taskpool synchronously and ensures it is executed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	td = test_alloc();
	if (!td) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
	if (!pool) {
		goto end;
	}

	if (ast_taskpool_push_wait(pool, simple_task, td)) {
		goto end;
	}

	if (!td->executed) {
		ast_test_status_update(test, "Expected simple task to be executed but it was not\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return res;
}

AST_TEST_DEFINE(taskpool_push_serializer)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.minimum_size = 1,
		.initial_size = 1,
		.max_size = 1,
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_taskprocessor *serializer = NULL;
	struct timeval start;
	struct timespec end;

	switch (cmd) {
	case TEST_INIT:
		info->name = "push_serializer";
		info->category = "/main/taskpool/";
		info->summary = "Taskpool serializer pushing test";
		info->description =
			"Pushes a single task into a taskpool serializer and ensures it is executed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	td = test_alloc();
	if (!td) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
	if (!pool) {
		goto end;
	}

	serializer = ast_taskpool_serializer("serializer", pool);
	if (!serializer) {
		goto end;
	}

	if (ast_taskprocessor_push(serializer, simple_task, td)) {
		goto end;
	}

	/* It should not take more than 5 seconds for a single simple task to execute */
	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 5;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&td->lock);
	while (!td->executed && ast_cond_timedwait(&td->cond, &td->lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&td->lock);

	if (!td->executed) {
		ast_test_status_update(test, "Expected simple task to be executed but it was not\n");
		res = AST_TEST_FAIL;
	}

	if (td->taskprocessor != serializer) {
		ast_test_status_update(test, "Expected taskprocessor to be same as serializer but it was not\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_taskprocessor_unreference(serializer);
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return res;
}

AST_TEST_DEFINE(taskpool_push_serializer_synchronous)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.minimum_size = 1,
		.initial_size = 1,
		.max_size = 1,
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_taskprocessor *serializer = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "push_serializer_synchronous";
		info->category = "/main/taskpool/";
		info->summary = "Taskpool serializer synchronous pushing test";
		info->description =
			"Pushes a single task into a taskpool serializer synchronously and ensures it is executed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	td = test_alloc();
	if (!td) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
	if (!pool) {
		goto end;
	}

	serializer = ast_taskpool_serializer("serializer", pool);
	if (!serializer) {
		goto end;
	}

	if (ast_taskpool_serializer_push_wait(serializer, simple_task, td)) {
		goto end;
	}

	if (!td->executed) {
		ast_test_status_update(test, "Expected simple task to be executed but it was not\n");
		res = AST_TEST_FAIL;
	}

	if (td->taskprocessor != serializer) {
		ast_test_status_update(test, "Expected taskprocessor to be same as serializer but it was not\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_taskprocessor_unreference(serializer);
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return res;
}

static int requeue_task(void *data)
{
	return ast_taskpool_serializer_push_wait(ast_taskpool_serializer_get_current(), simple_task, data);
}

AST_TEST_DEFINE(taskpool_push_serializer_synchronous_requeue)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.minimum_size = 1,
		.initial_size = 1,
		.max_size = 1,
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_taskprocessor *serializer = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "push_serializer_synchronous_requeue";
		info->category = "/main/taskpool/";
		info->summary = "Taskpool serializer synchronous requeueing test";
		info->description =
			"Pushes a single task into a taskpool serializer synchronously and ensures it is requeued and executed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	td = test_alloc();
	if (!td) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
	if (!pool) {
		goto end;
	}

	serializer = ast_taskpool_serializer("serializer", pool);
	if (!serializer) {
		goto end;
	}

	if (ast_taskpool_serializer_push_wait(serializer, requeue_task, td)) {
		goto end;
	}

	if (!td->executed) {
		ast_test_status_update(test, "Expected simple task to be executed but it was not\n");
		res = AST_TEST_FAIL;
	}

	if (td->taskprocessor != serializer) {
		ast_test_status_update(test, "Expected taskprocessor to be same as serializer but it was not\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_taskprocessor_unreference(serializer);
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return res;
}

AST_TEST_DEFINE(taskpool_push_grow)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 1,
		.minimum_size = 0,
		.initial_size = 0,
		.max_size = 1,
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	struct timeval start;
	struct timespec end;

	switch (cmd) {
	case TEST_INIT:
		info->name = "push_grow";
		info->category = "/main/taskpool/";
		info->summary = "Taskpool pushing test with auto-grow enabled";
		info->description =
			"Pushes a single task into a taskpool asynchronously, ensures it is executed and the pool grows.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	td = test_alloc();
	if (!td) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
	if (!pool) {
		goto end;
	}

	if (ast_taskpool_taskprocessors_count(pool) != 0) {
		ast_test_status_update(test, "Expected taskpool to have 0 taskprocessors but it has %zu\n", ast_taskpool_taskprocessors_count(pool));
		res = AST_TEST_FAIL;
		goto end;
	}

	if (ast_taskpool_push(pool, simple_task, td)) {
		goto end;
	}

	if (ast_taskpool_taskprocessors_count(pool) != 1) {
		ast_test_status_update(test, "Expected taskpool to have 1 taskprocessor but it has %zu\n", ast_taskpool_taskprocessors_count(pool));
		res = AST_TEST_FAIL;
		goto end;
	}

	/* It should not take more than 5 seconds for a single simple task to execute */
	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 5;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&td->lock);
	while (!td->executed && ast_cond_timedwait(&td->cond, &td->lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&td->lock);

	if (!td->executed) {
		ast_test_status_update(test, "Expected simple task to be executed but it was not\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return res;
}

AST_TEST_DEFINE(taskpool_push_shrink)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 1,
		.auto_increment = 1,
		.minimum_size = 0,
		.initial_size = 0,
		.max_size = 1,
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	struct timeval start;
	struct timespec end;
	int iterations = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "push_shrink";
		info->category = "/main/taskpool/";
		info->summary = "Taskpool pushing test with auto-shrink enabled";
		info->description =
			"Pushes a single task into a taskpool asynchronously, ensures it is executed and the pool shrinks.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	td = test_alloc();
	if (!td) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
	if (!pool) {
		goto end;
	}

	if (ast_taskpool_taskprocessors_count(pool) != 0) {
		ast_test_status_update(test, "Expected taskpool to have 0 taskprocessors but it has %zu\n", ast_taskpool_taskprocessors_count(pool));
		res = AST_TEST_FAIL;
		goto end;
	}

	if (ast_taskpool_push(pool, simple_task, td)) {
		res = AST_TEST_FAIL;
		goto end;
	}

	if (ast_taskpool_taskprocessors_count(pool) != 1) {
		ast_test_status_update(test, "Expected taskpool to have 1 taskprocessor but it has %zu\n", ast_taskpool_taskprocessors_count(pool));
		res = AST_TEST_FAIL;
		goto end;
	}

	/* We give 10 seconds for the pool to shrink back to normal, but if it happens earlier we
	 * stop our check early.
	 */
	ast_mutex_lock(&td->lock);
	do {
		start = ast_tvnow();
		end.tv_sec = start.tv_sec + 1;
		end.tv_nsec = start.tv_usec * 1000;

		if (ast_cond_timedwait(&td->cond, &td->lock, &end) == ETIMEDOUT) {
			iterations++;
		}
	} while (ast_taskpool_taskprocessors_count(pool) != 0 && iterations != 10);

	if (!td->executed) {
		ast_test_status_update(test, "Expected simple task to be executed but it was not\n");
		res = AST_TEST_FAIL;
	}

	if (ast_taskpool_taskprocessors_count(pool) != 0) {
		ast_test_status_update(test, "Expected taskpool to have 0 taskprocessors but it has %zu\n", ast_taskpool_taskprocessors_count(pool));
		res = AST_TEST_FAIL;
		goto end;
	}

end:
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return res;
}

struct efficiency_task_data {
	struct ast_taskpool *pool;
	int num_tasks_executed;
	int shutdown;
};

static int efficiency_task(void *data)
{
	struct efficiency_task_data *etd = data;

	if (etd->shutdown) {
		ao2_ref(etd->pool, -1);
		return 0;
	}

	ast_atomic_fetchadd_int(&etd->num_tasks_executed, +1);

	if (ast_taskpool_push(etd->pool, efficiency_task, etd)) {
		ao2_ref(etd->pool, -1);
		return -1;
	}

	return 0;
}

static char *handle_cli_taskpool_push_efficiency(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.minimum_size = 5,
		.initial_size = 5,
		.max_size = 5,
	};
	struct efficiency_task_data etd = {
		.pool = NULL,
		.num_tasks_executed = 0,
		.shutdown = 0,
	};
	struct timeval start;
	struct timespec end;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "taskpool push efficiency";
		e->usage =
			"Usage: taskpool push efficiency\n"
			"       Pushes 200 tasks to a taskpool and measures\n"
			"       the number of tasks executed within 30 seconds.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	td = test_alloc();
	if (!td) {
		return CLI_SUCCESS;
	}

	pool = ast_taskpool_create("taskpool_push_efficiency", &options);
	if (!pool) {
		goto end;
	}

	etd.pool = pool;

	/* Push in 200 tasks, cause why not */
	for (i = 0; i < 200; i++) {
		/* Ensure that the task has a reference to the pool */
		ao2_bump(pool);
		if (ast_taskpool_push(pool, efficiency_task, &etd)) {
			goto end;
		}
	}

	/* Wait for 30 seconds */
	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 30;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&td->lock);
	while (ast_cond_timedwait(&td->cond, &td->lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&td->lock);

	/* Give the total tasks executed, and tell each task to not requeue */
	ast_cli(a->fd, "Total tasks executed in 30 seconds: %d\n", etd.num_tasks_executed);

end:
	etd.shutdown = 1;
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return CLI_SUCCESS;
}

struct serializer_efficiency_task_data {
	struct ast_taskprocessor *serializer[2];
	int *num_tasks_executed;
	int *shutdown;
};

static int serializer_efficiency_task(void *data)
{
	struct serializer_efficiency_task_data *etd = data;
	struct ast_taskprocessor *taskprocessor = etd->serializer[0];

	if (*etd->shutdown) {
		return 0;
	}

	ast_atomic_fetchadd_int(etd->num_tasks_executed, +1);

	/* We ping pong a task between a pair of taskprocessors to ensure that
	 * a single taskprocessor does not receive a thread from the threadpool
	 * exclusively.
	 */
	if (taskprocessor == ast_taskpool_serializer_get_current()) {
		taskprocessor = etd->serializer[1];
	}

	if (ast_taskprocessor_push(taskprocessor,
		serializer_efficiency_task, etd)) {
		return -1;
	}

	return 0;
}

static char *handle_cli_taskpool_push_serializer_efficiency(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_taskpool *pool = NULL;
	struct test_data *td = NULL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
		.minimum_size = 5,
		.initial_size = 5,
		.max_size = 5,
	};
	struct serializer_efficiency_task_data etd[200];
	struct timeval start;
	struct timespec end;
	int i;
	int num_tasks_executed = 0;
	int shutdown = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "taskpool push serializer efficiency";
		e->usage =
			"Usage: taskpool push serializer efficiency\n"
			"       Pushes 200 tasks to a taskpool in serializers and measures\n"
			"       the number of tasks executed within 30 seconds.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	td = test_alloc();
	if (!td) {
		return CLI_SUCCESS;
	}

	memset(&etd, 0, sizeof(etd));

	pool = ast_taskpool_create("taskpool_push_serializer_efficiency", &options);
	if (!pool) {
		goto end;
	}

	/* We create 400 (200 pairs) of serializers */
	for (i = 0; i < 200; i++) {
		char serializer_name[AST_TASKPROCESSOR_MAX_NAME + 1];

		ast_taskprocessor_build_name(serializer_name, sizeof(serializer_name), "serializer%d", i);
		etd[i].serializer[0] = ast_taskpool_serializer(serializer_name, pool);
		if (!etd[i].serializer[0]) {
			goto end;
		}

		ast_taskprocessor_build_name(serializer_name, sizeof(serializer_name), "serializer%d", i);
		etd[i].serializer[1] = ast_taskpool_serializer(serializer_name, pool);
		if (!etd[i].serializer[1]) {
			goto end;
		}

		etd[i].num_tasks_executed = &num_tasks_executed;
		etd[i].shutdown = &shutdown;
	}

	/* And once created we push in 200 tasks */
	for (i = 0; i < 200; i++) {
		if (ast_taskprocessor_push(etd[i].serializer[0], serializer_efficiency_task, &etd[i])) {
			goto end;
		}
	}

	/* Wait for 30 seconds */
	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 30;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&td->lock);
	while (ast_cond_timedwait(&td->cond, &td->lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&td->lock);

	/* Give the total tasks executed, and tell each task to not requeue */
	ast_cli(a->fd, "Total tasks executed in 30 seconds: %d\n", num_tasks_executed);
	shutdown = 1;

end:
	/* We need to unreference each serializer */
	for (i = 0; i < 200; i++) {
		ast_taskprocessor_unreference(etd[i].serializer[0]);
		ast_taskprocessor_unreference(etd[i].serializer[1]);
	}
	ast_taskpool_shutdown(pool);
	test_destroy(td);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(handle_cli_taskpool_push_efficiency, "Push tasks to a taskpool and measure efficiency"),
	AST_CLI_DEFINE(handle_cli_taskpool_push_serializer_efficiency, "Push tasks to a taskpool in serializers and measure efficiency"),
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli, ARRAY_LEN(cli));
	AST_TEST_UNREGISTER(taskpool_push);
	AST_TEST_UNREGISTER(taskpool_push_synchronous);
	AST_TEST_UNREGISTER(taskpool_push_serializer);
	AST_TEST_UNREGISTER(taskpool_push_serializer_synchronous);
	AST_TEST_UNREGISTER(taskpool_push_serializer_synchronous_requeue);
	AST_TEST_UNREGISTER(taskpool_push_grow);
	AST_TEST_UNREGISTER(taskpool_push_shrink);
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli, ARRAY_LEN(cli));
	AST_TEST_REGISTER(taskpool_push);
	AST_TEST_REGISTER(taskpool_push_synchronous);
	AST_TEST_REGISTER(taskpool_push_serializer);
	AST_TEST_REGISTER(taskpool_push_serializer_synchronous);
	AST_TEST_REGISTER(taskpool_push_serializer_synchronous_requeue);
	AST_TEST_REGISTER(taskpool_push_grow);
	AST_TEST_REGISTER(taskpool_push_shrink);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "taskpool test module");
