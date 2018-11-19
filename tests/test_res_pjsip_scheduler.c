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
 * \brief res_pjsip scheduler tests
 *
 * \author George Joseph <george.joseph@fairview5.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/utils.h"

#define CATEGORY "/res/res_pjsip/scheduler/"

struct test_data {
	ast_mutex_t lock;
	ast_cond_t cond;
	pthread_t tid;
	struct timeval test_start;
	struct timeval task_start;
	struct timeval task_end;
	int is_servant;
	int interval;
	int sleep;
	int done;
	int no_clear_done;
	struct ast_test *test;
};

#define S2U(x) (long int)(x * 1000 * 1000)
#define M2U(x) (long int)(x * 1000)

static int task_1(void *data)
{
	struct test_data *test = data;

	if (!test->no_clear_done) {
		test->done = 0;
	}
	test->task_start = ast_tvnow();
	test->tid = pthread_self();
	test->is_servant = ast_sip_thread_is_servant();
	usleep(M2U(test->sleep));
	test->task_end = ast_tvnow();

	ast_mutex_lock(&test->lock);
	test->done++;
	ast_mutex_unlock(&test->lock);
	ast_cond_signal(&test->cond);

	return test->interval;
}


static void data_cleanup(void *data)
{
	struct test_data *test_data = data;
	ast_mutex_destroy(&test_data->lock);
	ast_cond_destroy(&test_data->cond);
}

#define waitfor(x) \
{ \
	ast_mutex_lock(&(x)->lock); \
	while (!(x)->done) { \
		ast_cond_wait(&(x)->cond, &(x)->lock); \
	} \
	(x)->done = 0; \
	ast_mutex_unlock(&(x)->lock); \
}

static int scheduler(struct ast_test *test, int serialized)
{
	RAII_VAR(struct ast_taskprocessor *, tp1, NULL, ast_taskprocessor_unreference);
	RAII_VAR(struct test_data *, test_data1, ao2_alloc(sizeof(*test_data1), data_cleanup), ao2_cleanup);
	RAII_VAR(struct test_data *, test_data2, ao2_alloc(sizeof(*test_data2), data_cleanup), ao2_cleanup);
	RAII_VAR(struct ast_sip_sched_task *, task1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_sched_task *, task2, NULL, ao2_cleanup);
	int duration;
	int delay;
	struct timeval task1_start;

	ast_test_validate(test, test_data1 != NULL);
	ast_test_validate(test, test_data2 != NULL);

	test_data1->test = test;
	test_data1->test_start = ast_tvnow();
	test_data1->interval = 2000;
	test_data1->sleep = 1000;
	ast_mutex_init(&test_data1->lock);
	ast_cond_init(&test_data1->cond, NULL);

	test_data2->test = test;
	test_data2->test_start = ast_tvnow();
	test_data2->interval = 2000;
	test_data2->sleep = 1000;
	ast_mutex_init(&test_data2->lock);
	ast_cond_init(&test_data2->cond, NULL);

	if (serialized) {
		ast_test_status_update(test, "This test will take about %3.1f seconds\n",
			(test_data1->interval + test_data1->sleep + (MAX(test_data1->interval - test_data2->interval, 0)) + test_data2->sleep) / 1000.0);
		tp1 = ast_sip_create_serializer("test-scheduler-serializer");
		ast_test_validate(test, (tp1 != NULL));
	} else {
		ast_test_status_update(test, "This test will take about %3.1f seconds\n",
			((MAX(test_data1->interval, test_data2->interval) + MAX(test_data1->sleep, test_data2->sleep)) / 1000.0));
	}

	task1 = ast_sip_schedule_task(tp1, test_data1->interval, task_1, NULL, test_data1, AST_SIP_SCHED_TASK_FIXED);
	ast_test_validate(test, task1 != NULL);

	task2 = ast_sip_schedule_task(tp1, test_data2->interval, task_1, NULL, test_data2, AST_SIP_SCHED_TASK_FIXED);
	ast_test_validate(test, task2 != NULL);

	waitfor(test_data1);
	ast_sip_sched_task_cancel(task1);
	ast_test_validate(test, test_data1->is_servant);

	duration = ast_tvdiff_ms(test_data1->task_end, test_data1->test_start);
	ast_test_validate(test, (duration > ((test_data1->interval + test_data1->sleep) * 0.9))
		&& (duration < ((test_data1->interval + test_data1->sleep) * 1.1)));

	ast_sip_sched_task_get_times(task1, NULL, &task1_start, NULL);
	delay = ast_tvdiff_ms(task1_start, test_data1->test_start);
	ast_test_validate(test, (delay > (test_data1->interval * 0.9)
		&& (delay < (test_data1->interval * 1.1))));

	waitfor(test_data2);
	ast_sip_sched_task_cancel(task2);
	ast_test_validate(test, test_data2->is_servant);

	if (serialized) {
		ast_test_validate(test, test_data1->tid == test_data2->tid);
		ast_test_validate(test, ast_tvdiff_ms(test_data2->task_start, test_data1->task_end) >= 0);
	} else {
		ast_test_validate(test, test_data1->tid != test_data2->tid);
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(serialized_scheduler)
{

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test res_pjsip serialized scheduler";
		info->description = "Test res_pjsip serialized scheduler";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return scheduler(test, 1);
}

AST_TEST_DEFINE(unserialized_scheduler)
{

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test res_pjsip unserialized scheduler";
		info->description = "Test res_pjsip unserialized scheduler";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return scheduler(test, 0);
}

static int run_count;
static int destruct_count;

static int dummy_task(void *data)
{
	int *sleep = data;

	usleep(M2U(*sleep));
	run_count++;

	return 0;
}

static void test_destructor(void *data)
{
	destruct_count++;
}

AST_TEST_DEFINE(scheduler_cleanup)
{
	RAII_VAR(int *, sleep, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_sched_task *, task, NULL, ao2_cleanup);
	int interval;
	int when;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test res_pjsip scheduler cleanup";
		info->description = "Test res_pjsip scheduler cleanup";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	destruct_count = 0;
	interval = 1000;

	sleep = ao2_alloc(sizeof(*sleep), test_destructor);
	ast_test_validate(test, sleep != NULL);
	*sleep = 500;

	ast_test_status_update(test, "This test will take about %3.1f seconds\n",
		((interval * 1.1) + *sleep) / 1000.0);

	task = ast_sip_schedule_task(NULL, interval, dummy_task, "dummy", sleep,
		AST_SIP_SCHED_TASK_DATA_AO2 | AST_SIP_SCHED_TASK_DATA_FREE);
	ast_test_validate(test, task != NULL);
	usleep(M2U(interval * 0.5));
	when = ast_sip_sched_task_get_next_run(task);
	ast_test_validate(test, (when > (interval * 0.4) && when < (interval * 0.6)));
	usleep(M2U(interval * 0.6));
	ast_test_validate(test, ast_sip_sched_is_task_running(task));

	usleep(M2U(*sleep));

	ast_test_validate(test, (ast_sip_sched_is_task_running(task) == 0));
	when = ast_sip_sched_task_get_next_run(task);
	ast_test_validate(test, (when < 0), res, error);
	ast_test_validate(test, (ao2_ref(task, 0) == 1));
	ao2_ref(task, -1);
	task = NULL;
	ast_test_validate(test, (destruct_count == 1));
	sleep = NULL;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(scheduler_cancel)
{
	RAII_VAR(int *, sleep, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_sched_task *, task, NULL, ao2_cleanup);
	int interval;
	int when;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test res_pjsip scheduler cancel task";
		info->description = "Test res_pjsip scheduler cancel task";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	destruct_count = 0;
	interval = 1000;

	sleep = ao2_alloc(sizeof(*sleep), test_destructor);
	ast_test_validate(test, sleep != NULL);
	*sleep = 500;

	ast_test_status_update(test, "This test will take about %3.1f seconds\n",
		(interval + *sleep) / 1000.0);

	task = ast_sip_schedule_task(NULL, interval, dummy_task, "dummy", sleep, AST_SIP_SCHED_TASK_DATA_NO_CLEANUP);
	ast_test_validate(test, task != NULL);

	usleep(M2U(interval * 0.5));
	when = ast_sip_sched_task_get_next_run_by_name("dummy");
	ast_test_validate(test, (when > (interval * 0.4) && when < (interval * 0.6)));
	ast_test_validate(test, !ast_sip_sched_is_task_running_by_name("dummy"));
	ast_test_validate(test, ao2_ref(task, 0) == 2);

	ast_sip_sched_task_cancel_by_name("dummy");

	when = ast_sip_sched_task_get_next_run(task);
	ast_test_validate(test, when < 0);

	usleep(M2U(interval));
	ast_test_validate(test, run_count == 0);
	ast_test_validate(test, destruct_count == 0);
	ast_test_validate(test, ao2_ref(task, 0) == 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(scheduler_policy)
{
	RAII_VAR(struct test_data *, test_data1, ao2_alloc(sizeof(*test_data1), data_cleanup), ao2_cleanup);
	RAII_VAR(struct ast_sip_sched_task *, task, NULL, ao2_cleanup);
	int when;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test res_pjsip scheduler cancel task";
		info->description = "Test res_pjsip scheduler cancel task";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, test_data1 != NULL);

	destruct_count = 0;
	run_count = 0;
	test_data1->test = test;
	test_data1->test_start = ast_tvnow();
	test_data1->interval = 1000;
	test_data1->sleep = 500;
	test_data1->no_clear_done = 1;
	ast_mutex_init(&test_data1->lock);
	ast_cond_init(&test_data1->cond, NULL);

	ast_test_status_update(test, "This test will take about %3.1f seconds\n",
		((test_data1->interval * 4) + test_data1->sleep) / 1000.0);

	task = ast_sip_schedule_task(NULL, test_data1->interval, task_1, "test_1", test_data1,
		AST_SIP_SCHED_TASK_DATA_NO_CLEANUP | AST_SIP_SCHED_TASK_PERIODIC);
	ast_test_validate(test, task != NULL);

	waitfor(test_data1);
	when = ast_tvdiff_ms(test_data1->task_start, test_data1->test_start);
	ast_test_validate(test, when > test_data1->interval * 0.9 && when < test_data1->interval * 1.1);

	waitfor(test_data1);
	when = ast_tvdiff_ms(test_data1->task_start, test_data1->test_start);
	ast_test_validate(test, when > test_data1->interval * 2 * 0.9 && when < test_data1->interval * 2 * 1.1);

	waitfor(test_data1);
	when = ast_tvdiff_ms(test_data1->task_start, test_data1->test_start);
	ast_test_validate(test, when > test_data1->interval * 3 * 0.9 && when < test_data1->interval * 3 * 1.1);

	ast_sip_sched_task_cancel(task);

	/* Wait a full interval in case a 4th call to test_1 happened before the cancel */
	usleep(M2U(test_data1->interval));

	ast_mutex_lock(&test_data1->lock);
	if (test_data1->done) {
		int done = test_data1->done;

		test_data1->done = 0;
		ast_mutex_unlock(&test_data1->lock);

		ast_test_validate(test, done == 1);

		/* Wait two full intervals to be certain no further calls to test_1. */
		usleep(M2U(test_data1->interval * 2));

		ast_mutex_lock(&test_data1->lock);
		if (test_data1->done != 0) {
			ast_mutex_unlock(&test_data1->lock);
			/* The cancelation failed so we need to prevent cleanup of
			 * test_data1 to prevent a crash from write-after-free. */
			test_data1 = NULL;
			ast_test_status_update(test, "Failed to cancel task");
			return AST_TEST_FAIL;
		}
	}
	ast_mutex_unlock(&test_data1->lock);

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(serialized_scheduler);
	AST_TEST_REGISTER(unserialized_scheduler);
	AST_TEST_REGISTER(scheduler_cleanup);
	AST_TEST_REGISTER(scheduler_cancel);
	AST_TEST_REGISTER(scheduler_policy);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(scheduler_cancel);
	AST_TEST_UNREGISTER(scheduler_cleanup);
	AST_TEST_UNREGISTER(unserialized_scheduler);
	AST_TEST_UNREGISTER(serialized_scheduler);
	AST_TEST_UNREGISTER(scheduler_policy);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "res_pjsip scheduler test module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_pjsip",
);
