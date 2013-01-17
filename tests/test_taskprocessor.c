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

/*!
 * \brief userdata associated with baseline taskprocessor test
 */
struct task_data {
	/* Condition used to signal to queuing thread that task was executed */
	ast_cond_t cond;
	/* Lock protecting the condition */
	ast_mutex_t lock;
	/*! Boolean indicating that the task was run */
	int task_complete;
};

/*!
 * \brief Queued task for baseline test.
 *
 * The task simply sets a boolean to indicate the
 * task has been run and then signals a condition
 * saying it's complete
 */
static int task(void *data)
{
	struct task_data *task_data = data;
	SCOPED_MUTEX(lock, &task_data->lock);
	task_data->task_complete = 1;
	ast_cond_signal(&task_data->cond);
	return 0;
}

/*!
 * \brief Baseline test for default taskprocessor
 *
 * This test ensures that when a task is added to a taskprocessor that
 * has been allocated with a default listener that the task gets executed
 * as expected
 */
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
			"Ensures that a queued task gets executed.";
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
	ast_mutex_unlock(&task_data.lock);

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

#define NUM_TASKS 20000

/*!
 * \brief Relevant data associated with taskprocessor load test
 */
static struct load_task_data {
	/*! Condition used to indicate a task has completed executing */
	ast_cond_t cond;
	/*! Lock used to protect the condition */
	ast_mutex_t lock;
	/*! Counter of the number of completed tasks */
	int tasks_completed;
	/*! Storage for task-specific data */
	int task_rand[NUM_TASKS];
} load_task_results;

/*!
 * \brief a queued task to be used in the taskprocessor load test
 *
 * The task increments the number of tasks executed and puts the passed-in
 * data into the next slot in the array of random data.
 */
static int load_task(void *data)
{
	int *randdata = data;
	SCOPED_MUTEX(lock, &load_task_results.lock);
	load_task_results.task_rand[load_task_results.tasks_completed++] = *randdata;
	ast_cond_signal(&load_task_results.cond);
	return 0;
}

/*!
 * \brief Load test for taskprocessor with default listener
 *
 * This test queues a large number of tasks, each with random data associated.
 * The test ensures that all of the tasks are run and that the tasks are executed
 * in the same order that they were queued
 */
AST_TEST_DEFINE(default_taskprocessor_load)
{
	struct ast_taskprocessor *tps;
	struct timeval start;
	struct timespec ts;
	enum ast_test_result_state res = AST_TEST_PASS;
	int timedwait_res;
	int i;
	int rand_data[NUM_TASKS];

	switch (cmd) {
	case TEST_INIT:
		info->name = "default_taskprocessor_load";
		info->category = "/main/taskprocessor/";
		info->summary = "Load test of default taskproccesor";
		info->description =
			"Ensure that a large number of queued tasks are executed in the proper order.";
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

	ts.tv_sec = start.tv_sec + 60;
	ts.tv_nsec = start.tv_usec * 1000;

	ast_cond_init(&load_task_results.cond, NULL);
	ast_mutex_init(&load_task_results.lock);
	load_task_results.tasks_completed = 0;

	for (i = 0; i < NUM_TASKS; ++i) {
		rand_data[i] = ast_random();
		ast_taskprocessor_push(tps, load_task, &rand_data[i]);
	}

	ast_mutex_lock(&load_task_results.lock);
	while (load_task_results.tasks_completed < NUM_TASKS) {
		timedwait_res = ast_cond_timedwait(&load_task_results.cond, &load_task_results.lock, &ts);
		if (timedwait_res == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&load_task_results.lock);

	if (load_task_results.tasks_completed != NUM_TASKS) {
		ast_test_status_update(test, "Unexpected number of tasks executed. Expected %d but got %d\n",
				NUM_TASKS, load_task_results.tasks_completed);
		res = AST_TEST_FAIL;
		goto test_end;
	}

	for (i = 0; i < NUM_TASKS; ++i) {
		if (rand_data[i] != load_task_results.task_rand[i]) {
			ast_test_status_update(test, "Queued tasks did not execute in order\n");
			res = AST_TEST_FAIL;
			goto test_end;
		}
	}

test_end:
	tps = ast_taskprocessor_unreference(tps);
	ast_mutex_destroy(&load_task_results.lock);
	ast_cond_destroy(&load_task_results.cond);
	return res;
}

/*!
 * \brief Private data for the test taskprocessor listener
 */
struct test_listener_pvt {
	/* Counter of number of tasks pushed to the queue */
	int num_pushed;
	/* Counter of number of times the queue was emptied */
	int num_emptied;
	/* Counter of number of times that a pushed task occurred on an empty queue */
	int num_was_empty;
	/* Boolean indicating whether the shutdown callback was called */
	int shutdown;
};

/*!
 * \brief test taskprocessor listener's alloc callback
 */
static void *test_listener_pvt_alloc(void)
{
	struct test_listener_pvt *pvt;

	pvt = ast_calloc(1, sizeof(*pvt));
	return pvt;
}

/*!
 * \brief test taskprocessor listener's start callback
 */
static int test_start(struct ast_taskprocessor_listener *listener)
{
	return 0;
}

/*!
 * \brief test taskprocessor listener's task_pushed callback
 *
 * Adjusts private data's stats as indicated by the parameters.
 */
static void test_task_pushed(struct ast_taskprocessor_listener *listener, int was_empty)
{
	struct test_listener_pvt *pvt = ast_taskprocessor_listener_get_user_data(listener);
	++pvt->num_pushed;
	if (was_empty) {
		++pvt->num_was_empty;
	}
}

/*!
 * \brief test taskprocessor listener's emptied callback.
 */
static void test_emptied(struct ast_taskprocessor_listener *listener)
{
	struct test_listener_pvt *pvt = ast_taskprocessor_listener_get_user_data(listener);
	++pvt->num_emptied;
}

/*!
 * \brief test taskprocessor listener's shutdown callback.
 */
static void test_shutdown(struct ast_taskprocessor_listener *listener)
{
	struct test_listener_pvt *pvt = ast_taskprocessor_listener_get_user_data(listener);
	pvt->shutdown = 1;
}

static const struct ast_taskprocessor_listener_callbacks test_callbacks = {
	.start = test_start,
	.task_pushed = test_task_pushed,
	.emptied = test_emptied,
	.shutdown = test_shutdown,
};

/*!
 * \brief Queued task for taskprocessor listener test.
 *
 * Does nothing.
 */
static int listener_test_task(void *ignore)
{
	return 0;
}

/*!
 * \brief helper to ensure that statistics the listener is keeping are what we expect
 *
 * \param test The currently-running test
 * \param pvt The private data for the taskprocessor listener
 * \param num_pushed The expected current number of tasks pushed to the processor
 * \param num_emptied The expected current number of times the taskprocessor has become empty
 * \param num_was_empty The expected current number of times that tasks were pushed to an empty taskprocessor
 * \retval -1 Stats were not as expected
 * \retval 0 Stats were as expected
 */
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

/*!
 * \brief Test for a taskprocessor with custom listener.
 *
 * This test pushes tasks to a taskprocessor with a custom listener, executes the taskss,
 * and destroys the taskprocessor.
 *
 * The test ensures that the listener's callbacks are called when expected and that the data
 * being passed in is accurate.
 */
AST_TEST_DEFINE(taskprocessor_listener)
{
	struct ast_taskprocessor *tps = NULL;
	struct ast_taskprocessor_listener *listener = NULL;
	struct test_listener_pvt *pvt = NULL;
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

	pvt = test_listener_pvt_alloc();
	if (!pvt) {
		ast_test_status_update(test, "Unable to allocate test taskprocessor listener user data\n");
		return AST_TEST_FAIL;
	}

	listener = ast_taskprocessor_listener_alloc(&test_callbacks, pvt);
	if (!listener) {
		ast_test_status_update(test, "Unable to allocate test taskprocessor listener\n");
		res = AST_TEST_FAIL;
		goto test_exit;
	}

	tps = ast_taskprocessor_create_with_listener("test_listener", listener);
	if (!tps) {
		ast_test_status_update(test, "Unable to allocate test taskprocessor\n");
		res = AST_TEST_FAIL;
		goto test_exit;
	}

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
	ao2_cleanup(listener);
	/* This is safe even if tps is NULL */
	ast_taskprocessor_unreference(tps);
	ast_free(pvt);
	return res;
}

static int unload_module(void)
{
	ast_test_unregister(default_taskprocessor);
	ast_test_unregister(default_taskprocessor_load);
	ast_test_unregister(taskprocessor_listener);
	return 0;
}

static int load_module(void)
{
	ast_test_register(default_taskprocessor);
	ast_test_register(default_taskprocessor_load);
	ast_test_register(taskprocessor_listener);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "taskprocessor test module");
