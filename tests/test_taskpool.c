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
		return -1;
	}

	return 0;
}

AST_TEST_DEFINE(taskpool_push_efficiency)
{
	struct ast_taskpool *pool = NULL;
	struct test_listener_data *tld = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
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
	case TEST_INIT:
		info->name = "push_efficiency";
		info->category = "/main/taskpool/";
		info->summary = "Test efficiency";
		info->description =
			"Taskpool test for efficiency";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	pool = ast_taskpool_create(info->name, &options);
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

	ast_mutex_lock(&tld->lock);
	while (ast_cond_timedwait(&tld->cond, &tld->lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&tld->lock);

	/* Give the total tasks executed, and tell each task to not requeue */
	ast_log(LOG_NOTICE, "Total tasks executed in 30 seconds: %d\n", etd.num_tasks_executed);

	res = AST_TEST_PASS;

end:
	etd.shutdown = 1;
	ast_taskpool_shutdown(pool);
	ast_free(tld);
	return res;
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

AST_TEST_DEFINE(taskpool_push_serializer_efficiency)
{
	struct ast_taskpool *pool = NULL;
	struct test_listener_data *tld = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_taskpool_options options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.idle_timeout = 0,
		.auto_increment = 0,
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
	case TEST_INIT:
		info->name = "push_serializer_efficiency";
		info->category = "/main/taskpool/";
		info->summary = "Test serializer efficiency";
		info->description =
			"Taskpool test for serializer efficiency";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}
	tld = test_alloc();
	if (!tld) {
		return AST_TEST_FAIL;
	}

	memset(&etd, 0, sizeof(etd));

	pool = ast_taskpool_create(info->name, &options);
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

	ast_mutex_lock(&tld->lock);
	while (ast_cond_timedwait(&tld->cond, &tld->lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&tld->lock);

	/* Give the total tasks executed, and tell each task to not requeue */
	ast_log(LOG_NOTICE, "Total tasks executed in 30 seconds: %d\n", num_tasks_executed);
	shutdown = 1;

	res = AST_TEST_PASS;

end:
	/* We need to unreference each serializer */
	for (i = 0; i < 200; i++) {
		ast_taskprocessor_unreference(etd[i].serializer[0]);
		ast_taskprocessor_unreference(etd[i].serializer[1]);
	}
	ast_taskpool_shutdown(pool);
	ast_free(tld);
	return res;
}

static int unload_module(void)
{
	ast_test_unregister(taskpool_push_efficiency);
	ast_test_unregister(taskpool_push_serializer_efficiency);
	return 0;
}

static int load_module(void)
{
	ast_test_register(taskpool_push_efficiency);
	ast_test_register(taskpool_push_serializer_efficiency);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "taskpool test module");
