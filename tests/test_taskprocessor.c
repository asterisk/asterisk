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

struct task_data {
	ast_cond_t cond;
	ast_mutex_t lock;
	int task_complete;
};

static int task(void *data)
{
	struct task_data *task_data = data;
	SCOPED_MUTEX(lock, &task_data->lock);
	task_data->task_complete = 1;
	ast_cond_signal(&task_data->cond);
	return 0;
}

AST_TEST_DEFINE(default_taskprocessor)
{
	struct ast_taskprocessor *tps;
	struct task_data task_data;
	enum ast_test_result_state res = AST_TEST_PASS;

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

	ast_cond_init(&task_data.cond, NULL);
	ast_mutex_init(&task_data.lock);
	task_data.task_complete = 0;

	ast_taskprocessor_push(tps, task, &task_data);
	ast_mutex_lock(&task_data.lock);
	while (!task_data.task_complete) {
		ast_cond_wait(&task_data.cond, &task_data.lock);
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

static int unload_module(void)
{
	ast_test_unregister(default_taskprocessor);
	return 0;
}

static int load_module(void)
{
	ast_test_register(default_taskprocessor);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "taskprocessor test module");
