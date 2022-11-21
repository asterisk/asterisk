/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_aeap</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pthread.h>

#include "asterisk/lock.h"
#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/res_aeap.h"
#include "asterisk/res_aeap_message.h"

#include "../res/res_aeap/general.h"
#include "../res/res_aeap/transaction.h"

#define CATEGORY "/res/aeap/transaction/"

#define AEAP_TRANSACTION_ID "foo"

static void handle_timeout(struct ast_aeap *aeap, struct ast_aeap_message *msg, void *obj)
{
	int *passed = obj;

	++*passed;
}

static void *end_transaction(void *data)
{
	/* Delay a second before ending transaction */
	struct timespec delay = { 1, 0 };
	int *passed = aeap_transaction_user_obj(data);

	while (nanosleep(&delay, &delay));

	++*passed;
	aeap_transaction_end(data, 0);

	return NULL;
}

static enum ast_test_result_state exec(struct ast_test *test,
	struct ast_aeap_tsx_params *params)
{
	pthread_t thread_id = AST_PTHREADT_NULL;
	struct ao2_container *tsxs = NULL;
	struct aeap_transaction *tsx = NULL;
	enum ast_test_result_state res = AST_TEST_FAIL;
	int passed = 0;

	tsxs = aeap_transactions_create();
	if (!tsxs) {
		ast_test_status_update(test, "Failed to create transactions object\n");
		goto exec_cleanup;
	}

	params->wait = 1;
	params->obj = &passed;

	tsx = aeap_transaction_create_and_add(tsxs, AEAP_TRANSACTION_ID, params, NULL);
	if (!tsx) {
		ast_test_status_update(test, "Failed to create transaction object\n");
		goto exec_cleanup;
	}

	if (ast_pthread_create(&thread_id, NULL, end_transaction, ao2_bump(tsx))) {
		ast_test_status_update(test, "Failed to create response thread\n");
		ao2_ref(tsx, -1);
		goto exec_cleanup;
	}

	if (aeap_transaction_start(tsx)) {
		ast_test_status_update(test, "Failed to start transaction request\n");
		goto exec_cleanup;
	}

	if (passed == 1) {
		res = AST_TEST_PASS;
	}

exec_cleanup:

	if (thread_id != AST_PTHREADT_NULL) {
		pthread_cancel(thread_id);
		pthread_join(thread_id, NULL);
	}

	aeap_transaction_end(tsx, 0);
	ao2_cleanup(tsxs);

	return res;
}

AST_TEST_DEFINE(transaction_exec)
{
	struct ast_aeap_tsx_params params = {
		.timeout = 5000, /* Give plenty of time for test thread to end */
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test creating a basic AEAP transaction request";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return exec(test, &params);
}

AST_TEST_DEFINE(transaction_exec_timeout)
{
	struct ast_aeap_tsx_params params = {
		.timeout = 100, /* Ensure timeout occurs before test thread ends */
		.on_timeout = handle_timeout,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = CATEGORY;
		info->summary = "test creating a AEAP transaction request that times out";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return exec(test, &params);
}

static int load_module(void)
{
	AST_TEST_REGISTER(transaction_exec);
	AST_TEST_REGISTER(transaction_exec_timeout);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(transaction_exec_timeout);
	AST_TEST_UNREGISTER(transaction_exec);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk External Application Protocol Transaction Tests",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_aeap",
);
