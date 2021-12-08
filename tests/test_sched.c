/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*! \file
 *
 * \brief ast_sched performance test module
 *
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <inttypes.h>

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/sched.h"
#include "asterisk/test.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"

static int sched_cb(const void *data)
{
	return 0;
}

static int order_check;
static int order_check_failed;

static void sched_order_check(struct ast_test *test, int order)
{
	++order_check;
	if (order_check != order) {
		ast_test_status_update(test, "Unexpected execution order: expected:%d got:%d\n",
			order, order_check);
		order_check_failed = 1;
	}
}

static int sched_order_1_cb(const void *data)
{
	sched_order_check((void *) data, 1);
	return 0;
}

static int sched_order_2_cb(const void *data)
{
	sched_order_check((void *) data, 2);
	return 0;
}

static int sched_order_3_cb(const void *data)
{
	sched_order_check((void *) data, 3);
	return 0;
}

static int sched_order_4_cb(const void *data)
{
	sched_order_check((void *) data, 4);
	return 0;
}

static int sched_order_5_cb(const void *data)
{
	sched_order_check((void *) data, 5);
	return 0;
}

static int sched_order_6_cb(const void *data)
{
	sched_order_check((void *) data, 6);
	return 0;
}

static int sched_order_7_cb(const void *data)
{
	sched_order_check((void *) data, 7);
	return 0;
}

static int sched_order_8_cb(const void *data)
{
	sched_order_check((void *) data, 8);
	return 0;
}

AST_TEST_DEFINE(sched_test_order)
{
	struct ast_sched_context *con;
	enum ast_test_result_state res = AST_TEST_FAIL;
	int id1, id2, id3, wait;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sched_test_order";
		info->category = "/main/sched/";
		info->summary = "Test ordering of events in the scheduler API";
		info->description =
			"This test ensures that events are properly ordered by the "
			"time they are scheduled to execute in the scheduler API.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(con = ast_sched_context_create())) {
		ast_test_status_update(test,
				"Test failed - could not create scheduler context\n");
		return AST_TEST_FAIL;
	}

	/* Add 3 scheduler entries, and then remove them, ensuring that the result
	 * of ast_sched_wait() looks appropriate at each step along the way. */

	if ((wait = ast_sched_wait(con)) != -1) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned -1, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if ((id1 = ast_sched_add(con, 100000, sched_cb, NULL)) == -1) {
		ast_test_status_update(test, "Failed to add scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) > 100000) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned <= 100000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if ((id2 = ast_sched_add(con, 10000, sched_cb, NULL)) == -1) {
		ast_test_status_update(test, "Failed to add scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) > 10000) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned <= 10000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if ((id3 = ast_sched_add(con, 1000, sched_cb, NULL)) == -1) {
		ast_test_status_update(test, "Failed to add scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) > 1000) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned <= 1000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if (ast_sched_del(con, id3) == -1) {
		ast_test_status_update(test, "Failed to remove scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) <= 1000) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned > 1000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if (ast_sched_del(con, id2) == -1) {
		ast_test_status_update(test, "Failed to remove scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) <= 10000) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned > 10000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if (ast_sched_del(con, id1) == -1) {
		ast_test_status_update(test, "Failed to remove scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) != -1) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned -1, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	/*
	 * Schedule immediate and delayed entries to check the order
	 * that they get executed.  They must get executed at the
	 * time they expire in the order they were added.
	 */
#define DELAYED_SAME_EXPIRE		300 /* ms */
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_1_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, 0, sched_order_1_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_2_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, 0, sched_order_2_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_3_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, 0, sched_order_3_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_4_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, 0, sched_order_4_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_5_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, 0, sched_order_5_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_6_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, 0, sched_order_6_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_7_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, 0, sched_order_7_cb, test), res, return_cleanup);
	ast_test_validate_cleanup(test, -1 < ast_sched_add(con, DELAYED_SAME_EXPIRE, sched_order_8_cb, test), res, return_cleanup);

	/* Check order of scheduled immediate entries. */
	order_check = 0;
	order_check_failed = 0;
	usleep(50 * 1000);/* Ensure that all the immediate entries are ready to expire */
	ast_test_validate_cleanup(test, 7 == ast_sched_runq(con), res, return_cleanup);
	ast_test_validate_cleanup(test, !order_check_failed, res, return_cleanup);

	/* Check order of scheduled entries expiring at the same time. */
	order_check = 0;
	order_check_failed = 0;
	usleep((DELAYED_SAME_EXPIRE + 50) * 1000);/* Ensure that all the delayed entries are ready to expire */
	ast_test_validate_cleanup(test, 8 == ast_sched_runq(con), res, return_cleanup);
	ast_test_validate_cleanup(test, !order_check_failed, res, return_cleanup);

	if ((wait = ast_sched_wait(con)) != -1) {
		ast_test_status_update(test,
				"ast_sched_wait() should have returned -1, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	res = AST_TEST_PASS;

return_cleanup:
	ast_sched_context_destroy(con);

	return res;
}

static char *handle_cli_sched_bench(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_sched_context *con;
	struct timeval start;
	unsigned int num, i;
	int *sched_ids = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sched benchmark";
		e->usage = ""
			"Usage: sched benchmark <num>\n"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	if (sscanf(a->argv[e->args], "%u", &num) != 1) {
		return CLI_SHOWUSAGE;
	}

	if (!(con = ast_sched_context_create())) {
		ast_cli(a->fd, "Test failed - could not create scheduler context\n");
		return CLI_FAILURE;
	}

	if (!(sched_ids = ast_malloc(sizeof(*sched_ids) * num))) {
		ast_cli(a->fd, "Test failed - memory allocation failure\n");
		goto return_cleanup;
	}

	ast_cli(a->fd, "Testing ast_sched_add() performance - timing how long it takes "
			"to add %u entries at random time intervals from 0 to 60 seconds\n", num);

	start = ast_tvnow();

	for (i = 0; i < num; i++) {
		long when = labs(ast_random()) % 60000;
		if ((sched_ids[i] = ast_sched_add(con, when, sched_cb, NULL)) == -1) {
			ast_cli(a->fd, "Test failed - sched_add returned -1\n");
			goto return_cleanup;
		}
	}

	ast_cli(a->fd, "Test complete - %" PRIi64 " us\n", ast_tvdiff_us(ast_tvnow(), start));

	ast_cli(a->fd, "Testing ast_sched_del() performance - timing how long it takes "
			"to delete %u entries with random time intervals from 0 to 60 seconds\n", num);

	start = ast_tvnow();

	for (i = 0; i < num; i++) {
		if (ast_sched_del(con, sched_ids[i]) == -1) {
			ast_cli(a->fd, "Test failed - sched_del returned -1\n");
			goto return_cleanup;
		}
	}

	ast_cli(a->fd, "Test complete - %" PRIi64 " us\n", ast_tvdiff_us(ast_tvnow(), start));

return_cleanup:
	ast_sched_context_destroy(con);
	if (sched_ids) {
		ast_free(sched_ids);
	}

	return CLI_SUCCESS;
}

struct test_obj {
	ast_mutex_t lock;
	ast_cond_t cond;
	int scheduledCBstarted;
	int id;
};

static void test_obj_cleanup(void *data)
{
	struct test_obj *obj = data;
	ast_mutex_destroy(&obj->lock);
	ast_cond_destroy(&obj->cond);
}

static int lockingcb(const void *data)
{
	struct test_obj *obj = (struct test_obj *)data;
	struct timespec delay = {3,0};

	ast_mutex_lock(&obj->lock);

	obj->scheduledCBstarted = 1;
	ast_cond_signal(&obj->cond);

	ast_mutex_unlock(&obj->lock);

	ao2_ref(obj, -1);
	while (nanosleep(&delay, &delay));
	/* sleep to force this scheduled event to remain running long
	 * enough for the scheduling thread to unlock and call
	 * AST_SCHED_DEL_UNREF
	 */

	return 0;
}

AST_TEST_DEFINE(sched_test_freebird)
{
	struct test_obj * obj;
	struct ast_sched_context * con;
	enum ast_test_result_state res = AST_TEST_FAIL;
	int refs;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sched_test_freebird";
		info->category = "/main/sched/";
		info->summary = "Test deadlock avoidance and double-unref";
		info->description =
			"This tests a call to AST_SCHED_DEL_UNREF on a running event.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		res = AST_TEST_PASS;
		break;
	}

	obj = ao2_alloc(sizeof(struct test_obj), test_obj_cleanup);
	if (!obj) {
		ast_test_status_update(test,
				"ao2_alloc() did not return an object\n");
		return AST_TEST_FAIL;
	}

	obj->scheduledCBstarted = 0;

	con = ast_sched_context_create();
	if (!con) {
		ast_test_status_update(test,
				"ast_sched_context_create() did not return a context\n");
		ao2_cleanup(obj);
		return AST_TEST_FAIL;
	}

	if (ast_sched_start_thread(con)) {
		ast_test_status_update(test, "Failed to start test thread\n");
		ao2_cleanup(obj);
		ast_sched_context_destroy(con);
		return AST_TEST_FAIL;
	}

	/* This double reference is to ensure that the object isn't destroyed prematurely
	 * in a case where it is unreffed an additional time.
	 */
	ao2_ref(obj, +2);
	if ((obj->id = ast_sched_add(con, 0, lockingcb, obj)) == -1) {
		ast_test_status_update(test, "Failed to add scheduler entry\n");
		ao2_ref(obj, -3);
		ast_sched_context_destroy(con);
		return AST_TEST_FAIL;
	}

	ast_mutex_lock(&obj->lock);
	while(obj->scheduledCBstarted == 0) {
		/* Wait for the scheduled thread to indicate that it has started so we can
		 * then call the AST_SCHED_DEL_UNREF macro
		 */
		ast_cond_wait(&obj->cond,&obj->lock);
	}
	ast_mutex_unlock(&obj->lock);

	ast_test_status_update(test, "Received signal, calling Scedule and UNREF\n");
	ast_test_status_update(test, "ID: %d\n", obj->id);
	AST_SCHED_DEL_UNREF(con, obj->id, ao2_ref(obj, -1));

	refs = ao2_ref(obj, 0);

	switch(refs){
		case 2:
			ast_test_status_update(test, "Correct number of references '2'\n");
			break;
		default:
			ast_test_status_update(test, "Incorrect number of references '%d'\n",
				refs);
			res = AST_TEST_FAIL;
			break;
	}

	/* Based on success or failure, the refcount could change
	 */
	while(ao2_ref(obj, -1) > 1);

	ast_sched_context_destroy(con);

	return res;
}

static struct ast_cli_entry cli_sched[] = {
	AST_CLI_DEFINE(handle_cli_sched_bench, "Benchmark ast_sched add/del performance"),
};

static int unload_module(void)
{
	AST_TEST_UNREGISTER(sched_test_order);
	AST_TEST_UNREGISTER(sched_test_freebird);
	ast_cli_unregister_multiple(cli_sched, ARRAY_LEN(cli_sched));
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(sched_test_order);
	AST_TEST_REGISTER(sched_test_freebird);
	ast_cli_register_multiple(cli_sched, ARRAY_LEN(cli_sched));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ast_sched performance test module");
