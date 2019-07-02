/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/conversions.h"
#include "asterisk/module.h"
#include "asterisk/stasis_state.h"
#include "asterisk/test.h"

#define test_category "/stasis/core/state/"

#define TOPIC_COUNT 500

#define MANAGER_TOPIC "foo"

struct stasis_message_type *foo_type(void);

/*! foo stasis message type */
STASIS_MESSAGE_TYPE_DEFN(foo_type);

/*! foo_type data */
struct foo_data {
	size_t bar;
};

AST_VECTOR(subscriptions, struct stasis_state_subscriber *);
AST_VECTOR(publishers, struct stasis_state_publisher *);

/*!
 * For testing purposes each subscribed state's id is a number. This value is
 * the summation of all id's.
 */
static size_t sum_total;

/*! Test variable that tracks the running total of state ids */
static size_t running_total;

/*! This value is set to check if state data is NULL before publishing */
static int expect_null;

static int validate_data(const char *id, struct foo_data *foo)
{
	size_t num;

	if (ast_str_to_umax(id, &num)) {
		ast_log(LOG_ERROR, "Unable to convert the state's id '%s' to numeric\n", id);
		return -1;
	}

	running_total += num;

	if (!foo) {
		if (expect_null) {
			return 0;
		}

		ast_log(LOG_ERROR, "Expected state data for '%s'\n", id);
		return -1;
	}

	if (expect_null) {
		ast_log(LOG_ERROR, "Expected NULL state data for '%s'\n", id);
		return -1;
	}

	if (foo->bar != num) {
		ast_log(LOG_ERROR, "Unexpected state data for '%s'\n", id);
		return -1;
	}

	return 0;
}

static void handle_validate(const char *id, struct stasis_state_subscriber *sub)
{
	struct foo_data *foo = stasis_state_subscriber_data(sub);
	validate_data(id, foo);
	ao2_cleanup(foo);
}

struct stasis_state_observer foo_observer = {
	.on_subscribe = handle_validate,
	.on_unsubscribe = handle_validate
};

static void foo_type_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	/* No op since we are not really testing stasis topic handling here */
}

static int subscriptions_destroy(struct stasis_state_manager *manager, struct subscriptions *subs)
{
	running_total = expect_null = 0;

	AST_VECTOR_CALLBACK_VOID(subs, stasis_state_unsubscribe_and_join);
	AST_VECTOR_FREE(subs);

	stasis_state_remove_observer(manager, &foo_observer);

	if (running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed to destroy all subscriptions: running=%zu, sum=%zu\n",
				running_total, sum_total);
		return -1;
	}

	return 0;
}

static int subscriptions_create(struct stasis_state_manager *manager,
	struct subscriptions *subs)
{
	size_t i;

	if (stasis_state_add_observer(manager, &foo_observer) ||
		AST_VECTOR_INIT(subs, TOPIC_COUNT)) {
		return -1;
	}

	sum_total = running_total = 0;
	expect_null = 1;

	for (i = 0; i < TOPIC_COUNT; ++i) {
		struct stasis_state_subscriber *sub;
		char id[32];

		if (snprintf(id, 10, "%zu", i) == -1) {
			ast_log(LOG_ERROR, "Unable to convert subscriber id to string\n");
			break;
		}

		sub = stasis_state_subscribe_pool(manager, id, foo_type_cb, NULL);
		if (!sub) {
			ast_log(LOG_ERROR, "Failed to create a state subscriber for id '%s'\n", id);
			ao2_ref(sub, -1);
			break;
		}

		if (AST_VECTOR_APPEND(subs, sub)) {
			ast_log(LOG_ERROR, "Failed to add to foo_sub to vector for id '%s'\n", id);
			ao2_ref(sub, -1);
			break;
		}

		sum_total += i;
	}

	if (i != TOPIC_COUNT || running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed to create all subscriptions: running=%zu, sum=%zu\n",
				running_total, sum_total);
		subscriptions_destroy(manager, subs);
		return -1;
	}

	return 0;
}

static int publishers_destroy(struct stasis_state_manager *manager, struct publishers *pubs)
{
	size_t i;

	if (pubs) {
		/* Remove explicit publishers */
		AST_VECTOR_CALLBACK_VOID(pubs, ao2_cleanup);
		AST_VECTOR_FREE(pubs);
		return 0;
	}

	for (i = 0; i < TOPIC_COUNT; ++i) {
		char id[32];

		/* Remove implicit publishers */
		if (snprintf(id, 10, "%zu", i) == -1) {
			ast_log(LOG_ERROR, "Unable to convert publisher id to string\n");
			return -1;
		}

		stasis_state_remove_publish_by_id(manager, id, NULL, NULL);
	}

	return 0;
}

static int publishers_create(struct stasis_state_manager *manager,
	struct publishers *pubs)
{
	size_t i;

	if (AST_VECTOR_INIT(pubs, TOPIC_COUNT)) {
		return -1;
	}

	for (i = 0; i < TOPIC_COUNT; ++i) {
		struct stasis_state_publisher *pub;
		char id[32];

		if (snprintf(id, 10, "%zu", i) == -1) {
			ast_log(LOG_ERROR, "Unable to convert publisher id to string\n");
			break;
		}

		/* Create the state publisher */
		pub = stasis_state_add_publisher(manager, id);
		if (!pub) {
			ast_log(LOG_ERROR, "Failed to create a state publisher for id '%s'\n", id);
			break;
		}

		if (AST_VECTOR_APPEND(pubs, pub)) {
			ast_log(LOG_ERROR, "Failed to add to publisher to vector for id '%s'\n", id);
			ao2_ref(pub, -1);
			break;
		}
	}

	if (i != TOPIC_COUNT) {
		ast_log(LOG_ERROR, "Failed to create all publishers: count=%zu\n", i);
		publishers_destroy(manager, pubs);
		return -1;
	}

	return 0;
}

static struct stasis_message *create_foo_type_message(const char *id)
{
	struct stasis_message *msg;
	struct foo_data *foo;

	foo = ao2_alloc(sizeof(*foo), NULL);
	if (!foo) {
		ast_log(LOG_ERROR, "Failed to allocate foo data for '%s'\n", id);
		return NULL;
	}

	if (ast_str_to_umax(id, &foo->bar)) {
		ast_log(LOG_ERROR, "Unable to convert the state's id '%s' to numeric\n", id);
		ao2_ref(foo, -1);
		return NULL;
	}

	msg = stasis_message_create_full(foo_type(), foo, NULL);
	if (!msg) {
		ast_log(LOG_ERROR, "Failed to create stasis message for '%s'\n", id);
	}

	ao2_ref(foo, -1);
	return msg;
}

static int implicit_publish_cb(const char *id, struct stasis_message *msg, void *user_data)
{
	/* For each state object create and publish new state data */
	struct foo_data *foo = stasis_message_data(msg);

	if (validate_data(id, foo)) {
		return CMP_STOP;
	}

	msg = create_foo_type_message(id);
	if (!msg) {
		return CMP_STOP;
	}

	/* Now publish it on the managed state object */
	stasis_state_publish_by_id(user_data, id, NULL, msg);
	ao2_ref(msg, -1);

	return 0;
}

static int explicit_publish_cb(const char *id, struct stasis_message *msg, void *user_data)
{
	/* For each state object create and publish new state data */
	struct publishers *pubs = user_data;
	struct stasis_state_publisher *pub = NULL;
	struct foo_data *foo = stasis_message_data(msg);
	size_t i;

	if (validate_data(id, foo)) {
		return CMP_STOP;
	}

	msg = create_foo_type_message(id);
	if (!msg) {
		return CMP_STOP;
	}

	for (i = 0; i < AST_VECTOR_SIZE(pubs); ++i) {
		if (!strcmp(stasis_state_publisher_id(AST_VECTOR_GET(pubs, i)), id)) {
			pub = AST_VECTOR_GET(pubs, i);
			break;
		}
	}

	if (!pub) {
		ast_log(LOG_ERROR, "Unable to locate publisher for id '%s'\n", id);
		return CMP_STOP;
	}

	stasis_state_publish(pub, msg);
	ao2_ref(msg, -1);

	return 0;
}

static int publish(struct stasis_state_manager *manager, on_stasis_state cb,
	void *user_data)
{
	/* First time there is no state data */
	expect_null = 1;

	running_total = 0;
	stasis_state_callback_all(manager, cb, user_data);

	if (running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed manager_callback (1): running=%zu, sum=%zu\n",
				running_total, sum_total);
		return -1;
	}

	/* Second time check valid state data exists */
	running_total = expect_null = 0;
	stasis_state_callback_all(manager, cb, user_data);

	if (running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed manager_callback (2): running=%zu, sum=%zu\n",
				running_total, sum_total);
		return -1;
	}

	return 0;
}

AST_TEST_DEFINE(implicit_publish)
{
	RAII_VAR(struct stasis_state_manager *, manager, NULL, ao2_cleanup);
	struct subscriptions subs;
	int rc = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test implicit publishing of stasis state";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	manager = stasis_state_manager_create(MANAGER_TOPIC);
	ast_test_validate(test, manager != NULL);

	ast_test_validate(test, !subscriptions_create(manager, &subs));

	ast_test_validate_cleanup(test, !publish(manager, implicit_publish_cb, manager),
		rc, cleanup);

cleanup:
	if (subscriptions_destroy(manager, &subs) || publishers_destroy(manager, NULL)) {
		return AST_TEST_FAIL;
	}

	/*
	 * State subscriptions add a ref a state. The state in turn adds a ref
	 * to the manager. So if more than one ref is held on the manager before
	 * exiting, there is a ref leak some place.
	 */
	if (ao2_ref(manager, 0) != 1) {
		ast_log(LOG_ERROR, "Memory leak - Too many references held on manager\n");
		return AST_TEST_FAIL;
	}

	return rc;
}

AST_TEST_DEFINE(explicit_publish)
{
	RAII_VAR(struct stasis_state_manager *, manager, NULL, ao2_cleanup);
	struct subscriptions subs;
	struct publishers pubs;
	int rc = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test explicit publishing of stasis state";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	manager = stasis_state_manager_create(MANAGER_TOPIC);
	ast_test_validate(test, manager != NULL);

	ast_test_validate(test, !subscriptions_create(manager, &subs));
	ast_test_validate_cleanup(test, !publishers_create(manager, &pubs), rc, cleanup);

	ast_test_validate_cleanup(test, !publish(manager, explicit_publish_cb, &pubs),
		rc, cleanup);

cleanup:
	if (subscriptions_destroy(manager, &subs) || publishers_destroy(manager, &pubs)) {
		return AST_TEST_FAIL;
	}

	/*
	 * State subscriptions add a ref a state. The state in turn adds a ref
	 * to the manager. So if more than one ref is held on the manager before
	 * exiting, there is a ref leak some place.
	 */
	if (ao2_ref(manager, 0) != 1) {
		ast_log(LOG_ERROR, "Memory leak - Too many references held on manager\n");
		return AST_TEST_FAIL;
	}

	return rc;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(implicit_publish);
	AST_TEST_UNREGISTER(explicit_publish);

	STASIS_MESSAGE_TYPE_CLEANUP(foo_type);

	return 0;
}

static int load_module(void)
{
	if (STASIS_MESSAGE_TYPE_INIT(foo_type) != 0) {
		return -1;
	}

	AST_TEST_REGISTER(implicit_publish);
	AST_TEST_REGISTER(explicit_publish);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Stasis state testing");
