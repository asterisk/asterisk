/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Device State Test Module
 *
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/devicestate.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_message_router.h"

#define UNIT_TEST_DEVICE_IDENTIFIER "unit_test_device_identifier"

/* These arrays are the result of the 'core show device2extenstate' output. */
static int combined_results[] = {
	AST_DEVICE_UNKNOWN,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_UNKNOWN,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_BUSY,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_UNKNOWN,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_INVALID,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_NOT_INUSE,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_UNAVAILABLE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGING,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_INUSE,
	AST_DEVICE_BUSY,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_ONHOLD,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_RINGINUSE,
	AST_DEVICE_ONHOLD,
};

static int exten_results[] = {
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_NOT_INUSE,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_UNAVAILABLE,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_INUSE,
	AST_EXTENSION_BUSY,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_ONHOLD,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_INUSE | AST_EXTENSION_RINGING,
	AST_EXTENSION_ONHOLD,
};

AST_TEST_DEFINE(device2extenstate_test)
{
	int res = AST_TEST_PASS;
	struct ast_devstate_aggregate agg;
	enum ast_device_state i, j, combined;
	enum ast_extension_states exten;
	int k = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "device2extenstate_test";
		info->category = "/main/devicestate/";
		info->summary = "Tests combined devstate mapping and device to extension state mapping.";
		info->description =
			"Verifies device state aggregate results match the expected combined "
			"devstate.  Then verifies the combined devstate maps to the expected "
			"extension state.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ARRAY_LEN(exten_results) != (AST_DEVICE_TOTAL * AST_DEVICE_TOTAL)) {
		ast_test_status_update(test, "Result array is %d long when it should be %d. "
			"Something has changed, this test must be updated.\n",
			(int) ARRAY_LEN(exten_results), (AST_DEVICE_TOTAL * AST_DEVICE_TOTAL));
		return AST_TEST_FAIL;
	}

	if (ARRAY_LEN(combined_results) != ARRAY_LEN(exten_results)) {
		ast_test_status_update(test, "combined_results and exten_results arrays do not match in length.\n");
		return AST_TEST_FAIL;
	}

	for (i = 0; i < AST_DEVICE_TOTAL; i++) {
		for (j = 0; j < AST_DEVICE_TOTAL; j++) {
			ast_devstate_aggregate_init(&agg);
			ast_devstate_aggregate_add(&agg, i);
			ast_devstate_aggregate_add(&agg, j);
			combined = ast_devstate_aggregate_result(&agg);
			if (combined_results[k] != combined) {
				ast_test_status_update(test, "Expected combined dev state %s "
					"does not match %s at combined_result[%d].\n",
					ast_devstate2str(combined_results[k]),
					ast_devstate2str(combined), k);
				res = AST_TEST_FAIL;
			}

			exten = ast_devstate_to_extenstate(combined);

			if (exten_results[k] != exten) {
				ast_test_status_update(test, "Expected exten state %s "
					"does not match %s at exten_result[%d]\n",
					ast_extension_state2str(exten_results[k]),
					ast_extension_state2str(exten), k);
				res = AST_TEST_FAIL;
			}
			k++;
		}
	}

	return res;
}

struct consumer {
	ast_cond_t out;
	int already_out;
	int sig_on_non_aggregate_state;
	int event_count;
	enum ast_device_state state;
	enum ast_device_state aggregate_state;
};

static void consumer_dtor(void *obj)
{
	struct consumer *consumer = obj;

	ast_cond_destroy(&consumer->out);
}

static void consumer_reset(struct consumer *consumer)
{
	consumer->already_out = 0;
	consumer->event_count = 0;
	consumer->state = AST_DEVICE_TOTAL;
	consumer->aggregate_state = AST_DEVICE_TOTAL;
}

static struct consumer *consumer_create(void)
{
	struct consumer *consumer;

	consumer = ao2_alloc(sizeof(*consumer), consumer_dtor);
	if (!consumer) {
		return NULL;
	}

	ast_cond_init(&consumer->out, NULL);
	consumer_reset(consumer);

	return consumer;
}

static void consumer_exec(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct consumer *consumer = data;
	struct stasis_cache_update *cache_update = stasis_message_data(message);
	struct ast_device_state_message *device_state;

	if (!cache_update->new_snapshot) {
		return;
	}

	device_state = stasis_message_data(cache_update->new_snapshot);

	if (strcmp(device_state->device, UNIT_TEST_DEVICE_IDENTIFIER)) {
		/* not a device state we're interested in */
		return;
	}

	{
		SCOPED_AO2LOCK(lock, consumer);

		++consumer->event_count;
		if (device_state->eid) {
			consumer->state = device_state->state;
			if (consumer->sig_on_non_aggregate_state) {
				consumer->sig_on_non_aggregate_state = 0;
				consumer->already_out = 1;
				ast_cond_signal(&consumer->out);
			}
		} else {
			consumer->aggregate_state = device_state->state;
			consumer->already_out = 1;
			ast_cond_signal(&consumer->out);
		}
	}
}

static void consumer_finalize(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct consumer *consumer = data;

	if (stasis_subscription_final_message(sub, message)) {
		ao2_cleanup(consumer);
	}
}

static void consumer_wait_for(struct consumer *consumer)
{
	int res;
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 10,
		.tv_nsec = start.tv_usec * 1000
	};

	SCOPED_AO2LOCK(lock, consumer);

	while (!consumer->already_out) {
		res = ast_cond_timedwait(&consumer->out, ao2_object_get_lockaddr(consumer), &end);
		if (!res || res == ETIMEDOUT) {
			break;
		}
	}
}

static int remove_device_states_cb(void *obj, void *arg, int flags)
{
	struct stasis_message *msg = obj;
	struct ast_device_state_message *device_state = stasis_message_data(msg);

	if (strcmp(UNIT_TEST_DEVICE_IDENTIFIER, device_state->device)) {
		/* Not a unit test device */
		return 0;
	}

	msg = stasis_cache_clear_create(msg);
	if (msg) {
		/* topic guaranteed to have been created by this point */
		stasis_publish(ast_device_state_topic(device_state->device), msg);
	}
	ao2_cleanup(msg);
	return 0;
}

static void cache_cleanup(int unused)
{
	struct ao2_container *cache_dump;

	/* remove all device states created during this test */
	cache_dump = stasis_cache_dump_all(ast_device_state_cache(), NULL);
	if (!cache_dump) {
		return;
	}
	ao2_callback(cache_dump, 0, remove_device_states_cb, NULL);
	ao2_cleanup(cache_dump);
}

AST_TEST_DEFINE(device_state_aggregation_test)
{
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, device_msg_router, NULL, stasis_message_router_unsubscribe);
	RAII_VAR(struct ast_eid *, foreign_eid, NULL, ast_free);
	RAII_VAR(int, cleanup_cache, 0, cache_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	int res;
	struct ast_device_state_message *device_state;

	switch (cmd) {
	case TEST_INIT:
		info->name = "device_state_aggregation_test";
		info->category = "/main/devicestate/";
		info->summary = "Tests message routing and aggregation through the Stasis device state system.";
		info->description =
			"Verifies that the device state system passes "
			"messages appropriately, that the aggregator is "
			"working properly, that the aggregate results match "
			"the expected combined devstate, and that the cached "
			"aggregate devstate is correct.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	foreign_eid = ast_malloc(sizeof(*foreign_eid));
	ast_test_validate(test, NULL != foreign_eid);
	memset(foreign_eid, 0xFF, sizeof(*foreign_eid));

	consumer = consumer_create();
	ast_test_validate(test, NULL != consumer);

	device_msg_router = stasis_message_router_create(ast_device_state_topic_cached());
	ast_test_validate(test, NULL != device_msg_router);

	ao2_ref(consumer, +1);
	res = stasis_message_router_add(device_msg_router, stasis_cache_update_type(), consumer_exec, consumer);
	ast_test_validate(test, !res);

	res = stasis_message_router_add(device_msg_router, stasis_subscription_change_type(), consumer_finalize, consumer);
	ast_test_validate(test, !res);

	/* push local state */
	ast_publish_device_state(UNIT_TEST_DEVICE_IDENTIFIER, AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE);

	/* Check cache aggregate state immediately */
	ao2_cleanup(msg);
	msg = stasis_cache_get_by_eid(ast_device_state_cache(), ast_device_state_message_type(), UNIT_TEST_DEVICE_IDENTIFIER, NULL);
	device_state = stasis_message_data(msg);
	ast_test_validate(test, AST_DEVICE_NOT_INUSE == device_state->state);

	consumer_wait_for(consumer);
	ast_test_validate(test, AST_DEVICE_NOT_INUSE == consumer->state);
	ast_test_validate(test, AST_DEVICE_NOT_INUSE == consumer->aggregate_state);
	ast_test_validate(test, 2 == consumer->event_count);
	consumer_reset(consumer);

	/* push remote state */
	/* this will not produce a new aggregate state message since the aggregate state does not change */
	consumer->sig_on_non_aggregate_state = 1;
	ast_publish_device_state_full(UNIT_TEST_DEVICE_IDENTIFIER, AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, foreign_eid);

	/* Check cache aggregate state immediately */
	ao2_cleanup(msg);
	msg = stasis_cache_get_by_eid(ast_device_state_cache(), ast_device_state_message_type(), UNIT_TEST_DEVICE_IDENTIFIER, NULL);
	device_state = stasis_message_data(msg);
	ast_test_validate(test, AST_DEVICE_NOT_INUSE == device_state->state);

	/* Check for expected events. */
	consumer_wait_for(consumer);
	ast_test_validate(test, AST_DEVICE_NOT_INUSE == consumer->state);
	ast_test_validate(test, AST_DEVICE_TOTAL == consumer->aggregate_state);
	ast_test_validate(test, 1 == consumer->event_count);
	consumer_reset(consumer);

	/* push remote state different from local state */
	ast_publish_device_state_full(UNIT_TEST_DEVICE_IDENTIFIER, AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, foreign_eid);

	/* Check cache aggregate state immediately */
	ao2_cleanup(msg);
	msg = stasis_cache_get_by_eid(ast_device_state_cache(), ast_device_state_message_type(), UNIT_TEST_DEVICE_IDENTIFIER, NULL);
	device_state = stasis_message_data(msg);
	ast_test_validate(test, AST_DEVICE_INUSE == device_state->state);

	/* Check for expected events. */
	consumer_wait_for(consumer);
	ast_test_validate(test, AST_DEVICE_INUSE == consumer->state);
	ast_test_validate(test, AST_DEVICE_INUSE == consumer->aggregate_state);
	ast_test_validate(test, 2 == consumer->event_count);
	consumer_reset(consumer);

	/* push local state that will cause aggregated state different from local non-aggregate state */
	ast_publish_device_state(UNIT_TEST_DEVICE_IDENTIFIER, AST_DEVICE_RINGING, AST_DEVSTATE_CACHABLE);

	/* Check cache aggregate state immediately */
	ao2_cleanup(msg);
	msg = stasis_cache_get_by_eid(ast_device_state_cache(), ast_device_state_message_type(), UNIT_TEST_DEVICE_IDENTIFIER, NULL);
	device_state = stasis_message_data(msg);
	ast_test_validate(test, AST_DEVICE_RINGINUSE == device_state->state);

	/* Check for expected events. */
	consumer_wait_for(consumer);
	ast_test_validate(test, AST_DEVICE_RINGING == consumer->state);
	ast_test_validate(test, AST_DEVICE_RINGINUSE == consumer->aggregate_state);
	ast_test_validate(test, 2 == consumer->event_count);
	consumer_reset(consumer);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(device2extenstate_test);
	AST_TEST_UNREGISTER(device_state_aggregation_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(device_state_aggregation_test);
	AST_TEST_REGISTER(device2extenstate_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Device State Test");
