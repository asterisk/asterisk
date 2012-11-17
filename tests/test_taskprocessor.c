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
 * \brief taskprocessor unit tests
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
#include "asterisk/taskprocessor.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"

struct task_data {
	ast_cond_t cond;
	ast_mutex_t lock;
	int task_complete;
};

static int task(void *data)
{
	struct task_data *task_data = data;
	SCOPED_MUTEX(lock, &task_data->lock);
	++task_data->task_complete;
	ast_cond_signal(&task_data->cond);
	return 0;
}

AST_TEST_DEFINE(default_taskprocessor)
{
	struct ast_taskprocessor *tps;
	struct task_data task_data;
	struct timeval start;
	struct timespec ts;
	enum ast_test_result_state res = AST_TEST_PASS;
	int timedwait_res;

	switch (cmd) {
	case TEST_INIT:
		info->name = "default_taskprocessor";
		info->category = "/main/taskprocessor/";
		info->summary = "Test of default taskproccesor";
		info->description =
			"Ensures that queued tasks are executed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	tps = ast_taskprocessor_get("test", TPS_REF_DEFAULT);

	if (!tps) {
		ast_test_status_update(test, "Unable to create test taskprocessor\n");
		return AST_TEST_FAIL;
	}

	start = ast_tvnow();

	ts.tv_sec = start.tv_sec + 30;
	ts.tv_nsec = start.tv_usec * 1000;

	ast_cond_init(&task_data.cond, NULL);
	ast_mutex_init(&task_data.lock);
	task_data.task_complete = 0;

	ast_taskprocessor_push(tps, task, &task_data);
	ast_mutex_lock(&task_data.lock);
	while (!task_data.task_complete) {
		timedwait_res = ast_cond_timedwait(&task_data.cond, &task_data.lock, &ts);
		if (timedwait_res == ETIMEDOUT) {
			break;
		}
	}

	if (!task_data.task_complete) {
		ast_test_status_update(test, "Queued task did not execute!\n");
		res = AST_TEST_FAIL;
		goto test_end;
	}

test_end:
	tps = ast_taskprocessor_unreference(tps);
	ast_mutex_destroy(&task_data.lock);
	ast_cond_destroy(&task_data.cond);
	return res;
}

struct test_listener_pvt {
	int num_pushed;
	int num_emptied;
	int num_was_empty;
	int shutdown;
};

static void *test_alloc(struct ast_taskprocessor_listener *listener)
{
	struct test_listener_pvt *pvt;

	pvt = ast_calloc(1, sizeof(*pvt));
	return pvt;
}

static void test_task_pushed(struct ast_taskprocessor_listener *listener, int was_empty)
{
	struct test_listener_pvt *pvt = listener->private_data;
	++pvt->num_pushed;
	if (was_empty) {
		++pvt->num_was_empty;
	}
}

static void test_emptied(struct ast_taskprocessor_listener *listener)
{
	struct test_listener_pvt *pvt = listener->private_data;
	++pvt->num_emptied;
}

static void test_shutdown(struct ast_taskprocessor_listener *listener)
{
	struct test_listener_pvt *pvt = listener->private_data;
	pvt->shutdown = 1;
}

static void test_destroy(void *private_data)
{
	struct test_listener_pvt *pvt = private_data;
	ast_free(pvt);
}

static const struct ast_taskprocessor_listener_callbacks test_callbacks = {
	.alloc = test_alloc,
	.task_pushed = test_task_pushed,
	.emptied = test_emptied,
	.shutdown = test_shutdown,
	.destroy = test_destroy,
};

static int listener_test_task(void *ignore)
{
	return 0;
}

static int check_stats(struct ast_test *test, const struct test_listener_pvt *pvt, int num_pushed, int num_emptied, int num_was_empty)
{
	if (pvt->num_pushed != num_pushed) {
		ast_test_status_update(test, "Unexpected number of tasks pushed. Expected %d but got %d\n",
				num_pushed, pvt->num_pushed);
		return -1;
	}

	if (pvt->num_emptied != num_emptied) {
		ast_test_status_update(test, "Unexpected number of empties. Expected %d but got %d\n",
				num_emptied, pvt->num_emptied);
		return -1;
	}

	if (pvt->num_was_empty != num_was_empty) {
		ast_test_status_update(test, "Unexpected number of empties. Expected %d but got %d\n",
				num_was_empty, pvt->num_emptied);
		return -1;
	}

	return 0;
}

AST_TEST_DEFINE(taskprocessor_listener)
{
	struct ast_taskprocessor *tps;
	struct ast_taskprocessor_listener *listener;
	struct test_listener_pvt *pvt;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "taskprocessor_listener";
		info->category = "/main/taskprocessor/";
		info->summary = "Test of taskproccesor listeners";
		info->description =
			"Ensures that listener callbacks are called when expected.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	listener = ast_taskprocessor_listener_alloc(&test_callbacks);
	if (!listener) {
		ast_test_status_update(test, "Unable to allocate test taskprocessor listener\n");
		return AST_TEST_FAIL;
	}

	tps = ast_taskprocessor_create_with_listener("test_listener", listener);
	if (!tps) {
		ast_test_status_update(test, "Unable to allocate test taskprocessor\n");
		res = AST_TEST_FAIL;
		goto test_exit;
	}

	pvt = listener->private_data;

	ast_taskprocessor_push(tps, listener_test_task, NULL);

	if (check_stats(test, pvt, 1, 0, 1) < 0) {
		res = AST_TEST_FAIL;
		goto test_exit;
	}

	ast_taskprocessor_push(tps, listener_test_task, NULL);

	if (check_stats(test, pvt, 2, 0, 1) < 0) {
		res = AST_TEST_FAIL;
		goto test_exit;
	}

	ast_taskprocessor_execute(tps);

	if (check_stats(test, pvt, 2, 0, 1) < 0) {
		res = AST_TEST_FAIL;
		goto test_exit;
	}

	ast_taskprocessor_execute(tps);

	if (check_stats(test, pvt, 2, 1, 1) < 0) {
		res = AST_TEST_FAIL;
		goto test_exit;
	}

	tps = ast_taskprocessor_unreference(tps);

	if (!pvt->shutdown) {
		res = AST_TEST_FAIL;
		goto test_exit;
	}

test_exit:
	ao2_ref(listener, -1);
	/* This is safe even if tps is NULL */
	ast_taskprocessor_unreference(tps);
	return res;
}

static int unload_module(void)
{
	ast_test_unregister(default_taskprocessor);
	ast_test_unregister(taskprocessor_listener);
	return 0;
}

static int load_module(void)
{
	ast_test_register(default_taskprocessor);
	ast_test_register(taskprocessor_listener);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "taskprocessor test module");
