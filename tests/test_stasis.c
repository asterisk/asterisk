/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \file \brief Test Stasis message bus.
 *
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/test.h"

static const char *test_category = "/stasis/core/";

static struct ast_json *fake_json(struct stasis_message *message, const struct stasis_message_sanitizer *sanitize)
{
	const char *text = stasis_message_data(message);

	return ast_json_string_create(text);
}

static struct ast_manager_event_blob *fake_ami(struct stasis_message *message)
{
	RAII_VAR(struct ast_manager_event_blob *, res, NULL, ao2_cleanup);
	const char *text = stasis_message_data(message);

	res = ast_manager_event_blob_create(EVENT_FLAG_TEST, "FakeMI",
		"Message: %s\r\n", text);

	if (res == NULL) {
		return NULL;
	}

	ao2_ref(res, +1);
	return res;
}

static struct stasis_message_vtable fake_vtable = {
	.to_json = fake_json,
	.to_ami = fake_ami
};

AST_TEST_DEFINE(message_type)
{
	RAII_VAR(struct stasis_message_type *, uut, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test basic message_type functions";
		info->description = "Test basic message_type functions";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create(NULL, NULL, NULL) == STASIS_MESSAGE_TYPE_ERROR);
	ast_test_validate(test, stasis_message_type_create("SomeMessage", NULL, &uut) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, 0 == strcmp(stasis_message_type_name(uut), "SomeMessage"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(message)
{
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, uut1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, uut2, NULL, ao2_cleanup);
	RAII_VAR(char *, data, NULL, ao2_cleanup);
	char *expected = "SomeData";
	struct timeval expected_timestamp;
	struct timeval time_diff;
	struct ast_eid foreign_eid;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test basic message functions";
		info->description = "Test basic message functions";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}


	memset(&foreign_eid, 0xFF, sizeof(foreign_eid));

	ast_test_validate(test, stasis_message_type_create("SomeMessage", NULL, &type) == STASIS_MESSAGE_TYPE_SUCCESS);

	ast_test_validate(test, NULL == stasis_message_create_full(NULL, NULL, NULL));
	ast_test_validate(test, NULL == stasis_message_create_full(type, NULL, NULL));

	data = ao2_alloc(strlen(expected) + 1, NULL);
	strcpy(data, expected);/* Safe */
	expected_timestamp = ast_tvnow();
	uut1 = stasis_message_create_full(type, data, &foreign_eid);
	uut2 = stasis_message_create_full(type, data, NULL);

	ast_test_validate(test, NULL != uut1);
	ast_test_validate(test, NULL != uut2);
	ast_test_validate(test, type == stasis_message_type(uut1));
	ast_test_validate(test, type == stasis_message_type(uut2));
	ast_test_validate(test, 0 == strcmp(expected, stasis_message_data(uut1)));
	ast_test_validate(test, 0 == strcmp(expected, stasis_message_data(uut2)));
	ast_test_validate(test, NULL != stasis_message_eid(uut1));
	ast_test_validate(test, NULL == stasis_message_eid(uut2));
	ast_test_validate(test, !ast_eid_cmp(&foreign_eid, stasis_message_eid(uut1)));

	ast_test_validate(test, 3 == ao2_ref(data, 0)); /* uut1 and uut2 have ref to data */

	time_diff = ast_tvsub(*stasis_message_timestamp(uut1), expected_timestamp);
	/* 10ms is certainly long enough for the two calls to complete */
	ast_test_validate(test, time_diff.tv_sec == 0);
	ast_test_validate(test, time_diff.tv_usec < 10000);

	ao2_ref(uut1, -1);
	uut1 = NULL;
	ast_test_validate(test, 2 == ao2_ref(data, 0)); /* uut1 unreffed data */
	ao2_ref(uut2, -1);
	uut2 = NULL;
	ast_test_validate(test, 1 == ao2_ref(data, 0)); /* uut2 unreffed data */

	return AST_TEST_PASS;
}

struct consumer {
	ast_cond_t out;
	struct stasis_message **messages_rxed;
	size_t messages_rxed_len;
	int ignore_subscriptions;
	int complete;
};

static void consumer_dtor(void *obj)
{
	struct consumer *consumer = obj;

	ast_cond_destroy(&consumer->out);

	while (consumer->messages_rxed_len > 0) {
		ao2_cleanup(consumer->messages_rxed[--consumer->messages_rxed_len]);
	}
	ast_free(consumer->messages_rxed);
	consumer->messages_rxed = NULL;
}

static struct consumer *consumer_create(int ignore_subscriptions)
{
	struct consumer *consumer;

	consumer = ao2_alloc(sizeof(*consumer), consumer_dtor);
	if (!consumer) {
		return NULL;
	}

	consumer->ignore_subscriptions = ignore_subscriptions;
	consumer->messages_rxed = ast_malloc(sizeof(*consumer->messages_rxed));
	if (!consumer->messages_rxed) {
		ao2_cleanup(consumer);
		return NULL;
	}

	ast_cond_init(&consumer->out, NULL);

	return consumer;
}

static void consumer_exec(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct consumer *consumer = data;
	RAII_VAR(struct consumer *, consumer_needs_cleanup, NULL, ao2_cleanup);
	SCOPED_AO2LOCK(lock, consumer);

	if (!consumer->ignore_subscriptions || stasis_message_type(message) != stasis_subscription_change_type()) {
		++consumer->messages_rxed_len;
		consumer->messages_rxed = ast_realloc(consumer->messages_rxed, sizeof(*consumer->messages_rxed) * consumer->messages_rxed_len);
		ast_assert(consumer->messages_rxed != NULL);
		consumer->messages_rxed[consumer->messages_rxed_len - 1] = message;
		ao2_ref(message, +1);
	}

	if (stasis_subscription_final_message(sub, message)) {
		consumer->complete = 1;
		consumer_needs_cleanup = consumer;
	}

	ast_cond_signal(&consumer->out);
}

static void consumer_exec_sync(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct consumer *consumer = data;
	RAII_VAR(struct consumer *, consumer_needs_cleanup, NULL, ao2_cleanup);
	SCOPED_AO2LOCK(lock, consumer);

	if (!consumer->ignore_subscriptions || stasis_message_type(message) != stasis_subscription_change_type()) {
		++consumer->messages_rxed_len;
		consumer->messages_rxed = ast_realloc(consumer->messages_rxed, sizeof(*consumer->messages_rxed) * consumer->messages_rxed_len);
		ast_assert(consumer->messages_rxed != NULL);
		consumer->messages_rxed[consumer->messages_rxed_len - 1] = message;
		ao2_ref(message, +1);
	}

	if (stasis_subscription_final_message(sub, message)) {
		consumer->complete = 1;
		consumer_needs_cleanup = consumer;
	}
}

static int consumer_wait_for(struct consumer *consumer, size_t expected_len)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 30,
		.tv_nsec = start.tv_usec * 1000
	};

	SCOPED_AO2LOCK(lock, consumer);

	while (consumer->messages_rxed_len < expected_len) {
		int r = ast_cond_timedwait(&consumer->out, ao2_object_get_lockaddr(consumer), &end);

		if (r == ETIMEDOUT) {
			break;
		}
		ast_assert(r == 0); /* Not expecting any othet types of errors */
	}
	return consumer->messages_rxed_len;
}

static int consumer_wait_for_completion(struct consumer *consumer)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 3,
		.tv_nsec = start.tv_usec * 1000
	};

	SCOPED_AO2LOCK(lock, consumer);

	while (!consumer->complete) {
		int r = ast_cond_timedwait(&consumer->out, ao2_object_get_lockaddr(consumer), &end);

		if (r == ETIMEDOUT) {
			break;
		}
		ast_assert(r == 0); /* Not expecting any othet types of errors */
	}
	return consumer->complete;
}

static int consumer_should_stay(struct consumer *consumer, size_t expected_len)
{
	struct timeval start = ast_tvnow();
	struct timeval diff = {
		.tv_sec = 0,
		.tv_usec = 100000 /* wait for 100ms */
	};
	struct timeval end_tv = ast_tvadd(start, diff);
	struct timespec end = {
		.tv_sec = end_tv.tv_sec,
		.tv_nsec = end_tv.tv_usec * 1000
	};

	SCOPED_AO2LOCK(lock, consumer);

	while (consumer->messages_rxed_len == expected_len) {
		int r = ast_cond_timedwait(&consumer->out, ao2_object_get_lockaddr(consumer), &end);

		if (r == ETIMEDOUT) {
			break;
		}
		ast_assert(r == 0); /* Not expecting any othet types of errors */
	}
	return consumer->messages_rxed_len;
}

AST_TEST_DEFINE(subscription_messages)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, uut, NULL, stasis_unsubscribe);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(char *, expected_uniqueid, NULL, ast_free);
	int complete;
	struct stasis_subscription_change *change;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test subscribe/unsubscribe messages";
		info->description = "Test subscribe/unsubscribe messages";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer = consumer_create(0);
	ast_test_validate(test, NULL != consumer);

	uut = stasis_subscribe(topic, consumer_exec, consumer);
	ast_test_validate(test, NULL != uut);
	ao2_ref(consumer, +1);
	expected_uniqueid = ast_strdup(stasis_subscription_uniqueid(uut));

	uut = stasis_unsubscribe(uut);
	complete = consumer_wait_for_completion(consumer);
	ast_test_validate(test, 1 == complete);

	ast_test_validate(test, 2 == consumer->messages_rxed_len);
	ast_test_validate(test, stasis_subscription_change_type() == stasis_message_type(consumer->messages_rxed[0]));
	ast_test_validate(test, stasis_subscription_change_type() == stasis_message_type(consumer->messages_rxed[1]));

	change = stasis_message_data(consumer->messages_rxed[0]);
	ast_test_validate(test, topic == change->topic);
	ast_test_validate(test, 0 == strcmp("Subscribe", change->description));
	ast_test_validate(test, 0 == strcmp(expected_uniqueid, change->uniqueid));

	change = stasis_message_data(consumer->messages_rxed[1]);
	ast_test_validate(test, topic == change->topic);
	ast_test_validate(test, 0 == strcmp("Unsubscribe", change->description));
	ast_test_validate(test, 0 == strcmp(expected_uniqueid, change->uniqueid));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(subscription_pool_messages)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, uut, NULL, stasis_unsubscribe);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(char *, expected_uniqueid, NULL, ast_free);
	int complete;
	struct stasis_subscription_change *change;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test subscribe/unsubscribe messages using a threadpool subscription";
		info->description = "Test subscribe/unsubscribe messages using a threadpool subscription";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer = consumer_create(0);
	ast_test_validate(test, NULL != consumer);

	uut = stasis_subscribe_pool(topic, consumer_exec, consumer);
	ast_test_validate(test, NULL != uut);
	ao2_ref(consumer, +1);
	expected_uniqueid = ast_strdup(stasis_subscription_uniqueid(uut));

	uut = stasis_unsubscribe(uut);
	complete = consumer_wait_for_completion(consumer);
	ast_test_validate(test, 1 == complete);

	ast_test_validate(test, 2 == consumer->messages_rxed_len);
	ast_test_validate(test, stasis_subscription_change_type() == stasis_message_type(consumer->messages_rxed[0]));
	ast_test_validate(test, stasis_subscription_change_type() == stasis_message_type(consumer->messages_rxed[1]));

	change = stasis_message_data(consumer->messages_rxed[0]);
	ast_test_validate(test, topic == change->topic);
	ast_test_validate(test, 0 == strcmp("Subscribe", change->description));
	ast_test_validate(test, 0 == strcmp(expected_uniqueid, change->uniqueid));

	change = stasis_message_data(consumer->messages_rxed[1]);
	ast_test_validate(test, topic == change->topic);
	ast_test_validate(test, 0 == strcmp("Unsubscribe", change->description));
	ast_test_validate(test, 0 == strcmp(expected_uniqueid, change->uniqueid));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(publish)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, uut, NULL, stasis_unsubscribe);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	int actual_len;
	const char *actual;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test publishing";
		info->description = "Test publishing";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);

	uut = stasis_subscribe(topic, consumer_exec, consumer);
	ast_test_validate(test, NULL != uut);
	ao2_ref(consumer, +1);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	ast_test_validate(test, stasis_message_type_create("TestMessage", NULL, &test_message_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	test_message = stasis_message_create(test_message_type, test_data);

	stasis_publish(topic, test_message);

	actual_len = consumer_wait_for(consumer, 1);
	ast_test_validate(test, 1 == actual_len);
	actual = stasis_message_data(consumer->messages_rxed[0]);
	ast_test_validate(test, test_data == actual);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(publish_sync)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, uut, NULL, stasis_unsubscribe);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	int actual_len;
	const char *actual;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test synchronous publishing";
		info->description = "Test synchronous publishing";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);

	uut = stasis_subscribe(topic, consumer_exec_sync, consumer);
	ast_test_validate(test, NULL != uut);
	ao2_ref(consumer, +1);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	ast_test_validate(test, stasis_message_type_create("TestMessage", NULL, &test_message_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	test_message = stasis_message_create(test_message_type, test_data);

	stasis_publish_sync(uut, test_message);

	actual_len = consumer->messages_rxed_len;
	ast_test_validate(test, 1 == actual_len);
	actual = stasis_message_data(consumer->messages_rxed[0]);
	ast_test_validate(test, test_data == actual);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(publish_pool)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, uut, NULL, stasis_unsubscribe);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	int actual_len;
	const char *actual;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test publishing with a threadpool";
		info->description = "Test publishing to a subscriber whose\n"
			"subscription dictates messages are received through a\n"
			"threadpool.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);

	uut = stasis_subscribe_pool(topic, consumer_exec, consumer);
	ast_test_validate(test, NULL != uut);
	ao2_ref(consumer, +1);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	ast_test_validate(test, stasis_message_type_create("TestMessage", NULL, &test_message_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	test_message = stasis_message_create(test_message_type, test_data);

	stasis_publish(topic, test_message);

	actual_len = consumer_wait_for(consumer, 1);
	ast_test_validate(test, 1 == actual_len);
	actual = stasis_message_data(consumer->messages_rxed[0]);
	ast_test_validate(test, test_data == actual);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(unsubscribe_stops_messages)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, uut, NULL, stasis_unsubscribe);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	int actual_len;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test simple subscriptions";
		info->description = "Test simple subscriptions";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);

	uut = stasis_subscribe(topic, consumer_exec, consumer);
	ast_test_validate(test, NULL != uut);
	ao2_ref(consumer, +1);

	uut = stasis_unsubscribe(uut);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	ast_test_validate(test, stasis_message_type_create("TestMessage", NULL, &test_message_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	test_message = stasis_message_create(test_message_type, test_data);

	stasis_publish(topic, test_message);

	actual_len = consumer_should_stay(consumer, 0);
	ast_test_validate(test, 0 == actual_len);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(forward)
{
	RAII_VAR(struct stasis_topic *, parent_topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);

	RAII_VAR(struct consumer *, parent_consumer, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);

	RAII_VAR(struct stasis_forward *, forward_sub, NULL, stasis_forward_cancel);
	RAII_VAR(struct stasis_subscription *, parent_sub, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);

	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	int actual_len;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test sending events to a parent topic";
		info->description = "Test sending events to a parent topic.\n"
			"This test creates three topics (one parent, two children)\n"
			"and publishes a message to one child, and verifies it's\n"
			"only seen by that child and the parent";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	parent_topic = stasis_topic_create("ParentTestTopic");
	ast_test_validate(test, NULL != parent_topic);
	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	forward_sub = stasis_forward_all(topic, parent_topic);
	ast_test_validate(test, NULL != forward_sub);

	parent_consumer = consumer_create(1);
	ast_test_validate(test, NULL != parent_consumer);
	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);

	parent_sub = stasis_subscribe(parent_topic, consumer_exec, parent_consumer);
	ast_test_validate(test, NULL != parent_sub);
	ao2_ref(parent_consumer, +1);
	sub = stasis_subscribe(topic, consumer_exec, consumer);
	ast_test_validate(test, NULL != sub);
	ao2_ref(consumer, +1);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	ast_test_validate(test, stasis_message_type_create("TestMessage", NULL, &test_message_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	test_message = stasis_message_create(test_message_type, test_data);

	stasis_publish(topic, test_message);

	actual_len = consumer_wait_for(consumer, 1);
	ast_test_validate(test, 1 == actual_len);
	actual_len = consumer_wait_for(parent_consumer, 1);
	ast_test_validate(test, 1 == actual_len);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(interleaving)
{
	RAII_VAR(struct stasis_topic *, parent_topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic2, NULL, ao2_cleanup);

	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);

	RAII_VAR(char *, test_data, NULL, ao2_cleanup);

	RAII_VAR(struct stasis_message *, test_message1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message3, NULL, ao2_cleanup);

	RAII_VAR(struct stasis_forward *, forward_sub1, NULL, stasis_forward_cancel);
	RAII_VAR(struct stasis_forward *, forward_sub2, NULL, stasis_forward_cancel);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);

	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);

	int actual_len;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test sending interleaved events to a parent topic";
		info->description = "Test sending events to a parent topic.\n"
			"This test creates three topics (one parent, two children)\n"
			"and publishes messages alternately between the children.\n"
			"It verifies that the messages are received in the expected\n"
			"order.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create("test", NULL, &test_message_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);

	test_message1 = stasis_message_create(test_message_type, test_data);
	ast_test_validate(test, NULL != test_message1);
	test_message2 = stasis_message_create(test_message_type, test_data);
	ast_test_validate(test, NULL != test_message2);
	test_message3 = stasis_message_create(test_message_type, test_data);
	ast_test_validate(test, NULL != test_message3);

	parent_topic = stasis_topic_create("ParentTestTopic");
	ast_test_validate(test, NULL != parent_topic);
	topic1 = stasis_topic_create("Topic1");
	ast_test_validate(test, NULL != topic1);
	topic2 = stasis_topic_create("Topic2");
	ast_test_validate(test, NULL != topic2);

	forward_sub1 = stasis_forward_all(topic1, parent_topic);
	ast_test_validate(test, NULL != forward_sub1);
	forward_sub2 = stasis_forward_all(topic2, parent_topic);
	ast_test_validate(test, NULL != forward_sub2);

	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);

	sub = stasis_subscribe(parent_topic, consumer_exec, consumer);
	ast_test_validate(test, NULL != sub);
	ao2_ref(consumer, +1);

	stasis_publish(topic1, test_message1);
	stasis_publish(topic2, test_message2);
	stasis_publish(topic1, test_message3);

	actual_len = consumer_wait_for(consumer, 3);
	ast_test_validate(test, 3 == actual_len);

	ast_test_validate(test, test_message1 == consumer->messages_rxed[0]);
	ast_test_validate(test, test_message2 == consumer->messages_rxed[1]);
	ast_test_validate(test, test_message3 == consumer->messages_rxed[2]);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(subscription_interleaving)
{
	RAII_VAR(struct stasis_topic *, parent_topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic2, NULL, ao2_cleanup);

	RAII_VAR(struct stasis_message_type *, test_message_type, NULL, ao2_cleanup);

	RAII_VAR(char *, test_data, NULL, ao2_cleanup);

	RAII_VAR(struct stasis_message *, test_message1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message3, NULL, ao2_cleanup);

	RAII_VAR(struct stasis_forward *, forward_sub1, NULL, stasis_forward_cancel);
	RAII_VAR(struct stasis_forward *, forward_sub2, NULL, stasis_forward_cancel);
	RAII_VAR(struct stasis_subscription *, sub1, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_subscription *, sub2, NULL, stasis_unsubscribe);

	RAII_VAR(struct consumer *, consumer1, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer2, NULL, ao2_cleanup);

	int actual_len;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test sending interleaved events to a parent topic with different subscribers";
		info->description = "Test sending events to a parent topic.\n"
			"This test creates three topics (one parent, two children)\n"
			"and publishes messages alternately between the children.\n"
			"It verifies that the messages are received in the expected\n"
			"order, for different subscription types: one with a dedicated\n"
			"thread, the other on the Stasis threadpool.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create("test", NULL, &test_message_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);

	test_message1 = stasis_message_create(test_message_type, test_data);
	ast_test_validate(test, NULL != test_message1);
	test_message2 = stasis_message_create(test_message_type, test_data);
	ast_test_validate(test, NULL != test_message2);
	test_message3 = stasis_message_create(test_message_type, test_data);
	ast_test_validate(test, NULL != test_message3);

	parent_topic = stasis_topic_create("ParentTestTopic");
	ast_test_validate(test, NULL != parent_topic);
	topic1 = stasis_topic_create("Topic1");
	ast_test_validate(test, NULL != topic1);
	topic2 = stasis_topic_create("Topic2");
	ast_test_validate(test, NULL != topic2);

	forward_sub1 = stasis_forward_all(topic1, parent_topic);
	ast_test_validate(test, NULL != forward_sub1);
	forward_sub2 = stasis_forward_all(topic2, parent_topic);
	ast_test_validate(test, NULL != forward_sub2);

	consumer1 = consumer_create(1);
	ast_test_validate(test, NULL != consumer1);

	consumer2 = consumer_create(1);
	ast_test_validate(test, NULL != consumer2);

	sub1 = stasis_subscribe(parent_topic, consumer_exec, consumer1);
	ast_test_validate(test, NULL != sub1);
	ao2_ref(consumer1, +1);

	sub2 = stasis_subscribe_pool(parent_topic, consumer_exec, consumer2);
	ast_test_validate(test, NULL != sub2);
	ao2_ref(consumer2, +1);

	stasis_publish(topic1, test_message1);
	stasis_publish(topic2, test_message2);
	stasis_publish(topic1, test_message3);

	actual_len = consumer_wait_for(consumer1, 3);
	ast_test_validate(test, 3 == actual_len);

	actual_len = consumer_wait_for(consumer2, 3);
	ast_test_validate(test, 3 == actual_len);

	ast_test_validate(test, test_message1 == consumer1->messages_rxed[0]);
	ast_test_validate(test, test_message2 == consumer1->messages_rxed[1]);
	ast_test_validate(test, test_message3 == consumer1->messages_rxed[2]);

	ast_test_validate(test, test_message1 == consumer2->messages_rxed[0]);
	ast_test_validate(test, test_message2 == consumer2->messages_rxed[1]);
	ast_test_validate(test, test_message3 == consumer2->messages_rxed[2]);

	return AST_TEST_PASS;
}

struct cache_test_data {
	char *id;
	char *value;
};

static void cache_test_data_dtor(void *obj)
{
	struct cache_test_data *data = obj;

	ast_free(data->id);
	ast_free(data->value);
}

static struct stasis_message *cache_test_message_create_full(struct stasis_message_type *type, const char *name, const char *value, struct ast_eid *eid)
{
	RAII_VAR(struct cache_test_data *, data, NULL, ao2_cleanup);

	data = ao2_alloc(sizeof(*data), cache_test_data_dtor);
	if (data == NULL) {
		return NULL;
	}

	ast_assert(name != NULL);
	ast_assert(value != NULL);

	data->id = ast_strdup(name);
	data->value = ast_strdup(value);
	if (!data->id || !data->value) {
		return NULL;
	}

	return stasis_message_create_full(type, data, eid);
}

static struct stasis_message *cache_test_message_create(struct stasis_message_type *type, const char *name, const char *value)
{
	return cache_test_message_create_full(type, name, value, &ast_eid_default);
}

static const char *cache_test_data_id(struct stasis_message *message)
{
	struct cache_test_data *cachable = stasis_message_data(message);

	if (0 != strcmp("Cacheable", stasis_message_type_name(stasis_message_type(message)))) {
		return NULL;
	}
	return cachable->id;
}

static struct stasis_message *cache_test_aggregate_calc_fn(struct stasis_cache_entry *entry, struct stasis_message *new_snapshot)
{
	struct stasis_message *aggregate_snapshot;
	struct stasis_message *snapshot;
	struct stasis_message_type *type = NULL;
	struct cache_test_data *test_data = NULL;
	int idx;
	int accumulated = 0;
	char aggregate_str[30];

	/* Accumulate the aggregate value. */
	snapshot = stasis_cache_entry_get_local(entry);
	if (snapshot) {
		type = stasis_message_type(snapshot);
		test_data = stasis_message_data(snapshot);
		accumulated += atoi(test_data->value);
	}
	for (idx = 0; ; ++idx) {
		snapshot = stasis_cache_entry_get_remote(entry, idx);
		if (!snapshot) {
			break;
		}

		type = stasis_message_type(snapshot);
		test_data = stasis_message_data(snapshot);
		accumulated += atoi(test_data->value);
	}

	if (!test_data) {
		/* There are no test entries cached.  Delete the aggregate. */
		return NULL;
	}

	snapshot = stasis_cache_entry_get_aggregate(entry);
	if (snapshot) {
		type = stasis_message_type(snapshot);
		test_data = stasis_message_data(snapshot);
		if (accumulated == atoi(test_data->value)) {
			/* Aggregate test entry did not change. */
			return ao2_bump(snapshot);
		}
	}

	snprintf(aggregate_str, sizeof(aggregate_str), "%d", accumulated);
	aggregate_snapshot = cache_test_message_create_full(type, test_data->id, aggregate_str, NULL);
	if (!aggregate_snapshot) {
		/* Bummer.  We have to keep the old aggregate snapshot. */
		ast_log(LOG_ERROR, "Could not create aggregate snapshot.\n");
		return ao2_bump(snapshot);
	}

	return aggregate_snapshot;
}

static void cache_test_aggregate_publish_fn(struct stasis_topic *topic, struct stasis_message *aggregate)
{
	stasis_publish(topic, aggregate);
}

static int check_cache_aggregate(struct stasis_cache *cache, struct stasis_message_type *cache_type, const char *id, const char *value)
{
	RAII_VAR(struct stasis_message *, aggregate, NULL, ao2_cleanup);
	struct cache_test_data *test_data;

	aggregate = stasis_cache_get_by_eid(cache, cache_type, id, NULL);
	if (!aggregate) {
		/* No aggregate, return true if given no value. */
		return !value;
	}

	/* Return true if the given value matches the aggregate value. */
	test_data = stasis_message_data(aggregate);
	return value && !strcmp(value, test_data->value);
}

AST_TEST_DEFINE(cache_filter)
{
	RAII_VAR(struct stasis_message_type *, non_cache_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, stasis_caching_unsubscribe);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	int actual_len;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test caching topics only forward cache_update messages.";
		info->description = "Test caching topics only forward cache_update messages.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create("NonCacheable", NULL, &non_cache_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != non_cache_type);
	topic = stasis_topic_create("SomeTopic");
	ast_test_validate(test, NULL != topic);
	cache = stasis_cache_create(cache_test_data_id);
	ast_test_validate(test, NULL != cache);
	caching_topic = stasis_caching_topic_create(topic, cache);
	ast_test_validate(test, NULL != caching_topic);
	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);
	sub = stasis_subscribe(stasis_caching_get_topic(caching_topic), consumer_exec, consumer);
	ast_test_validate(test, NULL != sub);
	ao2_ref(consumer, +1);

	test_message = cache_test_message_create(non_cache_type, "1", "1");
	ast_test_validate(test, NULL != test_message);

	stasis_publish(topic, test_message);

	actual_len = consumer_should_stay(consumer, 0);
	ast_test_validate(test, 0 == actual_len);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(cache)
{
	RAII_VAR(struct stasis_message_type *, cache_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, stasis_caching_unsubscribe);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_message *, test_message1_1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message1_clear, NULL, ao2_cleanup);
	int actual_len;
	struct stasis_cache_update *actual_update;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test passing messages through cache topic unscathed.";
		info->description = "Test passing messages through cache topic unscathed.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create("Cacheable", NULL, &cache_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != cache_type);
	topic = stasis_topic_create("SomeTopic");
	ast_test_validate(test, NULL != topic);
	cache = stasis_cache_create(cache_test_data_id);
	ast_test_validate(test, NULL != cache);
	caching_topic = stasis_caching_topic_create(topic, cache);
	ast_test_validate(test, NULL != caching_topic);
	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);
	sub = stasis_subscribe(stasis_caching_get_topic(caching_topic), consumer_exec, consumer);
	ast_test_validate(test, NULL != sub);
	ao2_ref(consumer, +1);

	test_message1_1 = cache_test_message_create(cache_type, "1", "1");
	ast_test_validate(test, NULL != test_message1_1);
	test_message2_1 = cache_test_message_create(cache_type, "2", "1");
	ast_test_validate(test, NULL != test_message2_1);

	/* Post a couple of snapshots */
	stasis_publish(topic, test_message1_1);
	stasis_publish(topic, test_message2_1);
	actual_len = consumer_wait_for(consumer, 2);
	ast_test_validate(test, 2 == actual_len);

	/* Check for new snapshot messages */
	ast_test_validate(test, stasis_cache_update_type() == stasis_message_type(consumer->messages_rxed[0]));
	actual_update = stasis_message_data(consumer->messages_rxed[0]);
	ast_test_validate(test, NULL == actual_update->old_snapshot);
	ast_test_validate(test, test_message1_1 == actual_update->new_snapshot);
	ast_test_validate(test, test_message1_1 == stasis_cache_get(cache, cache_type, "1"));
	/* stasis_cache_get returned a ref, so unref test_message1_1 */
	ao2_ref(test_message1_1, -1);

	ast_test_validate(test, stasis_cache_update_type() == stasis_message_type(consumer->messages_rxed[1]));
	actual_update = stasis_message_data(consumer->messages_rxed[1]);
	ast_test_validate(test, NULL == actual_update->old_snapshot);
	ast_test_validate(test, test_message2_1 == actual_update->new_snapshot);
	ast_test_validate(test, test_message2_1 == stasis_cache_get(cache, cache_type, "2"));
	/* stasis_cache_get returned a ref, so unref test_message2_1 */
	ao2_ref(test_message2_1, -1);

	/* Update snapshot 2 */
	test_message2_2 = cache_test_message_create(cache_type, "2", "2");
	ast_test_validate(test, NULL != test_message2_2);
	stasis_publish(topic, test_message2_2);

	actual_len = consumer_wait_for(consumer, 3);
	ast_test_validate(test, 3 == actual_len);

	actual_update = stasis_message_data(consumer->messages_rxed[2]);
	ast_test_validate(test, test_message2_1 == actual_update->old_snapshot);
	ast_test_validate(test, test_message2_2 == actual_update->new_snapshot);
	ast_test_validate(test, test_message2_2 == stasis_cache_get(cache, cache_type, "2"));
	/* stasis_cache_get returned a ref, so unref test_message2_2 */
	ao2_ref(test_message2_2, -1);

	/* Clear snapshot 1 */
	test_message1_clear = stasis_cache_clear_create(test_message1_1);
	ast_test_validate(test, NULL != test_message1_clear);
	stasis_publish(topic, test_message1_clear);

	actual_len = consumer_wait_for(consumer, 4);
	ast_test_validate(test, 4 == actual_len);

	actual_update = stasis_message_data(consumer->messages_rxed[3]);
	ast_test_validate(test, test_message1_1 == actual_update->old_snapshot);
	ast_test_validate(test, NULL == actual_update->new_snapshot);
	ast_test_validate(test, NULL == stasis_cache_get(cache, cache_type, "1"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(cache_dump)
{
	RAII_VAR(struct stasis_message_type *, cache_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, stasis_caching_unsubscribe);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_message *, test_message1_1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message1_clear, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, cache_dump, NULL, ao2_cleanup);
	int actual_len;
	struct ao2_iterator i;
	void *obj;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test cache dump routines.";
		info->description = "Test cache dump routines.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create("Cacheable", NULL, &cache_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != cache_type);
	topic = stasis_topic_create("SomeTopic");
	ast_test_validate(test, NULL != topic);
	cache = stasis_cache_create(cache_test_data_id);
	ast_test_validate(test, NULL != cache);
	caching_topic = stasis_caching_topic_create(topic, cache);
	ast_test_validate(test, NULL != caching_topic);
	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);
	sub = stasis_subscribe(stasis_caching_get_topic(caching_topic), consumer_exec, consumer);
	ast_test_validate(test, NULL != sub);
	ao2_ref(consumer, +1);

	test_message1_1 = cache_test_message_create(cache_type, "1", "1");
	ast_test_validate(test, NULL != test_message1_1);
	test_message2_1 = cache_test_message_create(cache_type, "2", "1");
	ast_test_validate(test, NULL != test_message2_1);

	/* Post a couple of snapshots */
	stasis_publish(topic, test_message1_1);
	stasis_publish(topic, test_message2_1);
	actual_len = consumer_wait_for(consumer, 2);
	ast_test_validate(test, 2 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 2 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);
		ast_test_validate(test, actual_cache_entry == test_message1_1 || actual_cache_entry == test_message2_1);
	}
	ao2_iterator_destroy(&i);

	/* Update snapshot 2 */
	test_message2_2 = cache_test_message_create(cache_type, "2", "2");
	ast_test_validate(test, NULL != test_message2_2);
	stasis_publish(topic, test_message2_2);

	actual_len = consumer_wait_for(consumer, 3);
	ast_test_validate(test, 3 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 2 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);
		ast_test_validate(test, actual_cache_entry == test_message1_1 || actual_cache_entry == test_message2_2);
	}
	ao2_iterator_destroy(&i);

	/* Clear snapshot 1 */
	test_message1_clear = stasis_cache_clear_create(test_message1_1);
	ast_test_validate(test, NULL != test_message1_clear);
	stasis_publish(topic, test_message1_clear);

	actual_len = consumer_wait_for(consumer, 4);
	ast_test_validate(test, 4 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 1 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);
		ast_test_validate(test, actual_cache_entry == test_message2_2);
	}
	ao2_iterator_destroy(&i);

	/* Dump the cache to ensure that it has no subscription change items in it since those aren't cached */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump(cache, stasis_subscription_change_type());
	ast_test_validate(test, 0 == ao2_container_count(cache_dump));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(cache_eid_aggregate)
{
	RAII_VAR(struct stasis_message_type *, cache_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, stasis_caching_unsubscribe);
	RAII_VAR(struct consumer *, cache_consumer, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, topic_consumer, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, topic_sub, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_subscription *, cache_sub, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_message *, test_message1_1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_3, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_4, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message1_clear, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2_clear, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, cache_dump, NULL, ao2_cleanup);
	int actual_len;
	struct ao2_iterator i;
	void *obj;
	struct ast_eid foreign_eid1;
	struct ast_eid foreign_eid2;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test cache eid and aggregate support.";
		info->description = "Test cache eid and aggregate support.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	memset(&foreign_eid1, 0xAA, sizeof(foreign_eid1));
	memset(&foreign_eid2, 0xBB, sizeof(foreign_eid2));

	ast_test_validate(test, stasis_message_type_create("Cacheable", NULL, &cache_type) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != cache_type);

	topic = stasis_topic_create("SomeTopic");
	ast_test_validate(test, NULL != topic);

	/* To consume events published to the topic. */
	topic_consumer = consumer_create(1);
	ast_test_validate(test, NULL != topic_consumer);

	topic_sub = stasis_subscribe(topic, consumer_exec, topic_consumer);
	ast_test_validate(test, NULL != topic_sub);
	ao2_ref(topic_consumer, +1);

	cache = stasis_cache_create_full(cache_test_data_id,
		cache_test_aggregate_calc_fn, cache_test_aggregate_publish_fn);
	ast_test_validate(test, NULL != cache);

	caching_topic = stasis_caching_topic_create(topic, cache);
	ast_test_validate(test, NULL != caching_topic);

	/* To consume update events published to the caching_topic. */
	cache_consumer = consumer_create(1);
	ast_test_validate(test, NULL != cache_consumer);

	cache_sub = stasis_subscribe(stasis_caching_get_topic(caching_topic), consumer_exec, cache_consumer);
	ast_test_validate(test, NULL != cache_sub);
	ao2_ref(cache_consumer, +1);

	/* Create test messages. */
	test_message1_1 = cache_test_message_create_full(cache_type, "1", "1", &ast_eid_default);
	ast_test_validate(test, NULL != test_message1_1);
	test_message2_1 = cache_test_message_create_full(cache_type, "2", "1", &ast_eid_default);
	ast_test_validate(test, NULL != test_message2_1);
	test_message2_2 = cache_test_message_create_full(cache_type, "2", "2", &foreign_eid1);
	ast_test_validate(test, NULL != test_message2_2);
	test_message2_3 = cache_test_message_create_full(cache_type, "2", "3", &foreign_eid2);
	ast_test_validate(test, NULL != test_message2_3);
	test_message2_4 = cache_test_message_create_full(cache_type, "2", "4", &foreign_eid2);
	ast_test_validate(test, NULL != test_message2_4);

	/* Post some snapshots */
	stasis_publish(topic, test_message1_1);
	ast_test_validate(test, check_cache_aggregate(cache, cache_type, "1", "1"));
	stasis_publish(topic, test_message2_1);
	ast_test_validate(test, check_cache_aggregate(cache, cache_type, "2", "1"));
	stasis_publish(topic, test_message2_2);
	ast_test_validate(test, check_cache_aggregate(cache, cache_type, "2", "3"));

	actual_len = consumer_wait_for(cache_consumer, 6);
	ast_test_validate(test, 6 == actual_len);
	actual_len = consumer_wait_for(topic_consumer, 6);
	ast_test_validate(test, 6 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump_all(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 3 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test,
			actual_cache_entry == test_message1_1
			|| actual_cache_entry == test_message2_1
			|| actual_cache_entry == test_message2_2);
	}
	ao2_iterator_destroy(&i);

	/* Check the local cached items */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump_by_eid(cache, NULL, &ast_eid_default);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 2 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test,
			actual_cache_entry == test_message1_1
			|| actual_cache_entry == test_message2_1);
	}
	ao2_iterator_destroy(&i);

	/* Post snapshot 2 from another eid. */
	stasis_publish(topic, test_message2_3);
	ast_test_validate(test, check_cache_aggregate(cache, cache_type, "2", "6"));

	actual_len = consumer_wait_for(cache_consumer, 8);
	ast_test_validate(test, 8 == actual_len);
	actual_len = consumer_wait_for(topic_consumer, 8);
	ast_test_validate(test, 8 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump_all(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 4 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test,
			actual_cache_entry == test_message1_1
			|| actual_cache_entry == test_message2_1
			|| actual_cache_entry == test_message2_2
			|| actual_cache_entry == test_message2_3);
	}
	ao2_iterator_destroy(&i);

	/* Check the remote cached items */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump_by_eid(cache, NULL, &foreign_eid1);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 1 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test, actual_cache_entry == test_message2_2);
	}
	ao2_iterator_destroy(&i);

	/* Post snapshot 2 from a repeated eid. */
	stasis_publish(topic, test_message2_4);
	ast_test_validate(test, check_cache_aggregate(cache, cache_type, "2", "7"));

	actual_len = consumer_wait_for(cache_consumer, 10);
	ast_test_validate(test, 10 == actual_len);
	actual_len = consumer_wait_for(topic_consumer, 10);
	ast_test_validate(test, 10 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump_all(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 4 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test,
			actual_cache_entry == test_message1_1
			|| actual_cache_entry == test_message2_1
			|| actual_cache_entry == test_message2_2
			|| actual_cache_entry == test_message2_4);
	}
	ao2_iterator_destroy(&i);

	/* Check all snapshot 2 cache entries. */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_get_all(cache, cache_type, "2");
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 3 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test,
			actual_cache_entry == test_message2_1
			|| actual_cache_entry == test_message2_2
			|| actual_cache_entry == test_message2_4);
	}
	ao2_iterator_destroy(&i);

	/* Clear snapshot 1 */
	test_message1_clear = stasis_cache_clear_create(test_message1_1);
	ast_test_validate(test, NULL != test_message1_clear);
	stasis_publish(topic, test_message1_clear);
	ast_test_validate(test, check_cache_aggregate(cache, cache_type, "1", NULL));

	actual_len = consumer_wait_for(cache_consumer, 12);
	ast_test_validate(test, 12 == actual_len);
	actual_len = consumer_wait_for(topic_consumer, 11);
	ast_test_validate(test, 11 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump_all(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 3 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test,
			actual_cache_entry == test_message2_1
			|| actual_cache_entry == test_message2_2
			|| actual_cache_entry == test_message2_4);
	}
	ao2_iterator_destroy(&i);

	/* Clear snapshot 2 from a remote eid */
	test_message2_clear = stasis_cache_clear_create(test_message2_2);
	ast_test_validate(test, NULL != test_message2_clear);
	stasis_publish(topic, test_message2_clear);
	ast_test_validate(test, check_cache_aggregate(cache, cache_type, "2", "5"));

	actual_len = consumer_wait_for(cache_consumer, 14);
	ast_test_validate(test, 14 == actual_len);
	actual_len = consumer_wait_for(topic_consumer, 13);
	ast_test_validate(test, 13 == actual_len);

	/* Check the cache */
	ao2_cleanup(cache_dump);
	cache_dump = stasis_cache_dump_all(cache, NULL);
	ast_test_validate(test, NULL != cache_dump);
	ast_test_validate(test, 2 == ao2_container_count(cache_dump));
	i = ao2_iterator_init(cache_dump, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, actual_cache_entry, obj, ao2_cleanup);

		ast_test_validate(test,
			actual_cache_entry == test_message2_1
			|| actual_cache_entry == test_message2_4);
	}
	ao2_iterator_destroy(&i);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(router)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, uut, NULL, stasis_message_router_unsubscribe_and_join);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type3, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer1, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer2, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer3, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message3, NULL, ao2_cleanup);
	int actual_len, ret;
	struct stasis_message *actual;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test simple message routing";
		info->description = "Test simple message routing";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer1 = consumer_create(1);
	ast_test_validate(test, NULL != consumer1);
	consumer2 = consumer_create(1);
	ast_test_validate(test, NULL != consumer2);
	consumer3 = consumer_create(1);
	ast_test_validate(test, NULL != consumer3);

	ast_test_validate(test, stasis_message_type_create("TestMessage1", NULL, &test_message_type1) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type1);
	ast_test_validate(test, stasis_message_type_create("TestMessage2", NULL, &test_message_type2) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type2);
	ast_test_validate(test, stasis_message_type_create("TestMessage3", NULL, &test_message_type3) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type3);

	uut = stasis_message_router_create(topic);
	ast_test_validate(test, NULL != uut);

	ret = stasis_message_router_add(
		uut, test_message_type1, consumer_exec, consumer1);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer1, +1);
	ret = stasis_message_router_add(
		uut, test_message_type2, consumer_exec, consumer2);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer2, +1);
	ret = stasis_message_router_set_default(uut, consumer_exec, consumer3);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer3, +1);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	test_message1 = stasis_message_create(test_message_type1, test_data);
	ast_test_validate(test, NULL != test_message1);
	test_message2 = stasis_message_create(test_message_type2, test_data);
	ast_test_validate(test, NULL != test_message2);
	test_message3 = stasis_message_create(test_message_type3, test_data);
	ast_test_validate(test, NULL != test_message3);

	stasis_publish(topic, test_message1);
	stasis_publish(topic, test_message2);
	stasis_publish(topic, test_message3);

	actual_len = consumer_wait_for(consumer1, 1);
	ast_test_validate(test, 1 == actual_len);
	actual_len = consumer_wait_for(consumer2, 1);
	ast_test_validate(test, 1 == actual_len);
	actual_len = consumer_wait_for(consumer3, 1);
	ast_test_validate(test, 1 == actual_len);

	actual = consumer1->messages_rxed[0];
	ast_test_validate(test, test_message1 == actual);

	actual = consumer2->messages_rxed[0];
	ast_test_validate(test, test_message2 == actual);

	actual = consumer3->messages_rxed[0];
	ast_test_validate(test, test_message3 == actual);

	/* consumer1 and consumer2 do not get the final message. */
	ao2_cleanup(consumer1);
	ao2_cleanup(consumer2);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(router_pool)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, uut, NULL, stasis_message_router_unsubscribe_and_join);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type3, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer1, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer2, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer3, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message3, NULL, ao2_cleanup);
	int actual_len, ret;
	struct stasis_message *actual;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test message routing via threadpool";
		info->description = "Test simple message routing when\n"
			"the subscriptions dictate usage of the Stasis\n"
			"threadpool.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	consumer1 = consumer_create(1);
	ast_test_validate(test, NULL != consumer1);
	consumer2 = consumer_create(1);
	ast_test_validate(test, NULL != consumer2);
	consumer3 = consumer_create(1);
	ast_test_validate(test, NULL != consumer3);

	ast_test_validate(test, stasis_message_type_create("TestMessage1", NULL, &test_message_type1) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type1);
	ast_test_validate(test, stasis_message_type_create("TestMessage2", NULL, &test_message_type2) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type2);
	ast_test_validate(test, stasis_message_type_create("TestMessage3", NULL, &test_message_type3) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type3);

	uut = stasis_message_router_create_pool(topic);
	ast_test_validate(test, NULL != uut);

	ret = stasis_message_router_add(
		uut, test_message_type1, consumer_exec, consumer1);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer1, +1);
	ret = stasis_message_router_add(
		uut, test_message_type2, consumer_exec, consumer2);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer2, +1);
	ret = stasis_message_router_set_default(uut, consumer_exec, consumer3);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer3, +1);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	test_message1 = stasis_message_create(test_message_type1, test_data);
	ast_test_validate(test, NULL != test_message1);
	test_message2 = stasis_message_create(test_message_type2, test_data);
	ast_test_validate(test, NULL != test_message2);
	test_message3 = stasis_message_create(test_message_type3, test_data);
	ast_test_validate(test, NULL != test_message3);

	stasis_publish(topic, test_message1);
	stasis_publish(topic, test_message2);
	stasis_publish(topic, test_message3);

	actual_len = consumer_wait_for(consumer1, 1);
	ast_test_validate(test, 1 == actual_len);
	actual_len = consumer_wait_for(consumer2, 1);
	ast_test_validate(test, 1 == actual_len);
	actual_len = consumer_wait_for(consumer3, 1);
	ast_test_validate(test, 1 == actual_len);

	actual = consumer1->messages_rxed[0];
	ast_test_validate(test, test_message1 == actual);

	actual = consumer2->messages_rxed[0];
	ast_test_validate(test, test_message2 == actual);

	actual = consumer3->messages_rxed[0];
	ast_test_validate(test, test_message3 == actual);

	/* consumer1 and consumer2 do not get the final message. */
	ao2_cleanup(consumer1);
	ao2_cleanup(consumer2);

	return AST_TEST_PASS;
}

static const char *cache_simple(struct stasis_message *message)
{
	const char *type_name =
		stasis_message_type_name(stasis_message_type(message));
	if (!ast_begins_with(type_name, "Cache")) {
		return NULL;
	}

	return "cached";
}

AST_TEST_DEFINE(router_cache_updates)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, stasis_caching_unsubscribe_and_join);
	RAII_VAR(struct stasis_message_type *, test_message_type1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_type *, test_message_type3, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message_router *, uut, NULL, stasis_message_router_unsubscribe_and_join);
	RAII_VAR(char *, test_data, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message2, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, test_message3, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer1, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer2, NULL, ao2_cleanup);
	RAII_VAR(struct consumer *, consumer3, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message1, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message2, NULL, ao2_cleanup);
	struct stasis_cache_update *update;
	int actual_len, ret;
	struct stasis_message *actual;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test special handling cache_update messages";
		info->description = "Test special handling cache_update messages";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("TestTopic");
	ast_test_validate(test, NULL != topic);

	cache = stasis_cache_create(cache_simple);
	ast_test_validate(test, NULL != cache);
	caching_topic = stasis_caching_topic_create(topic, cache);
	ast_test_validate(test, NULL != caching_topic);

	consumer1 = consumer_create(1);
	ast_test_validate(test, NULL != consumer1);
	consumer2 = consumer_create(1);
	ast_test_validate(test, NULL != consumer2);
	consumer3 = consumer_create(1);
	ast_test_validate(test, NULL != consumer3);

	ast_test_validate(test, stasis_message_type_create("Cache1", NULL, &test_message_type1) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type1);
	ast_test_validate(test, stasis_message_type_create("Cache2", NULL, &test_message_type2) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type2);
	ast_test_validate(test, stasis_message_type_create("NonCache", NULL, &test_message_type3) == STASIS_MESSAGE_TYPE_SUCCESS);
	ast_test_validate(test, NULL != test_message_type3);

	uut = stasis_message_router_create(
		stasis_caching_get_topic(caching_topic));
	ast_test_validate(test, NULL != uut);

	ret = stasis_message_router_add_cache_update(
		uut, test_message_type1, consumer_exec, consumer1);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer1, +1);
	ret = stasis_message_router_add(
		uut, stasis_cache_update_type(), consumer_exec, consumer2);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer2, +1);
	ret = stasis_message_router_set_default(uut, consumer_exec, consumer3);
	ast_test_validate(test, 0 == ret);
	ao2_ref(consumer3, +1);

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	test_message1 = stasis_message_create(test_message_type1, test_data);
	ast_test_validate(test, NULL != test_message1);
	test_message2 = stasis_message_create(test_message_type2, test_data);
	ast_test_validate(test, NULL != test_message2);
	test_message3 = stasis_message_create(test_message_type3, test_data);
	ast_test_validate(test, NULL != test_message3);

	stasis_publish(topic, test_message1);
	stasis_publish(topic, test_message2);
	stasis_publish(topic, test_message3);

	actual_len = consumer_wait_for(consumer1, 1);
	ast_test_validate(test, 1 == actual_len);
	actual_len = consumer_wait_for(consumer2, 1);
	ast_test_validate(test, 1 == actual_len);
	/* Uncacheable message should not be passed through */
	actual_len = consumer_should_stay(consumer3, 0);
	ast_test_validate(test, 0 == actual_len);

	actual = consumer1->messages_rxed[0];
	ast_test_validate(test, stasis_cache_update_type() == stasis_message_type(actual));
	update = stasis_message_data(actual);
	ast_test_validate(test, test_message_type1 == update->type);
	ast_test_validate(test, test_message1 == update->new_snapshot);

	actual = consumer2->messages_rxed[0];
	ast_test_validate(test, stasis_cache_update_type() == stasis_message_type(actual));
	update = stasis_message_data(actual);
	ast_test_validate(test, test_message_type2 == update->type);
	ast_test_validate(test, test_message2 == update->new_snapshot);

	/* consumer1 and consumer2 do not get the final message. */
	ao2_cleanup(consumer1);
	ao2_cleanup(consumer2);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(no_to_json)
{
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, uut, NULL, ao2_cleanup);
	RAII_VAR(char *, data, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, actual, NULL, ast_json_unref);
	char *expected = "SomeData";

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test message to_json function";
		info->description = "Test message to_json function";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Test NULL */
	actual = stasis_message_to_json(NULL, NULL);
	ast_test_validate(test, NULL == actual);

	/* Test message with NULL to_json function */
	ast_test_validate(test, stasis_message_type_create("SomeMessage", NULL, &type) == STASIS_MESSAGE_TYPE_SUCCESS);

	data = ao2_alloc(strlen(expected) + 1, NULL);
	strcpy(data, expected);
	uut = stasis_message_create(type, data);
	ast_test_validate(test, NULL != uut);

	actual = stasis_message_to_json(uut, NULL);
	ast_test_validate(test, NULL == actual);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(to_json)
{
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, uut, NULL, ao2_cleanup);
	RAII_VAR(char *, data, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, actual, NULL, ast_json_unref);
	const char *expected_text = "SomeData";
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test message to_json function when NULL";
		info->description = "Test message to_json function when NULL";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create("SomeMessage", &fake_vtable, &type) == STASIS_MESSAGE_TYPE_SUCCESS);

	data = ao2_alloc(strlen(expected_text) + 1, NULL);
	strcpy(data, expected_text);
	uut = stasis_message_create(type, data);
	ast_test_validate(test, NULL != uut);

	expected = ast_json_string_create(expected_text);
	actual = stasis_message_to_json(uut, NULL);
	ast_test_validate(test, ast_json_equal(expected, actual));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(no_to_ami)
{
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, uut, NULL, ao2_cleanup);
	RAII_VAR(char *, data, NULL, ao2_cleanup);
	RAII_VAR(struct ast_manager_event_blob *, actual, NULL, ao2_cleanup);
	char *expected = "SomeData";

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test message to_ami function when NULL";
		info->description = "Test message to_ami function when NULL";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Test NULL */
	actual = stasis_message_to_ami(NULL);
	ast_test_validate(test, NULL == actual);

	/* Test message with NULL to_ami function */
	ast_test_validate(test, stasis_message_type_create("SomeMessage", NULL, &type) == STASIS_MESSAGE_TYPE_SUCCESS);

	data = ao2_alloc(strlen(expected) + 1, NULL);
	strcpy(data, expected);
	uut = stasis_message_create(type, data);
	ast_test_validate(test, NULL != uut);

	actual = stasis_message_to_ami(uut);
	ast_test_validate(test, NULL == actual);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(to_ami)
{
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, uut, NULL, ao2_cleanup);
	RAII_VAR(char *, data, NULL, ao2_cleanup);
	RAII_VAR(struct ast_manager_event_blob *, actual, NULL, ao2_cleanup);
	const char *expected_text = "SomeData";
	const char *expected = "Message: SomeData\r\n";

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test message to_ami function";
		info->description = "Test message to_ami function";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, stasis_message_type_create("SomeMessage", &fake_vtable, &type) == STASIS_MESSAGE_TYPE_SUCCESS);

	data = ao2_alloc(strlen(expected_text) + 1, NULL);
	strcpy(data, expected_text);
	uut = stasis_message_create(type, data);
	ast_test_validate(test, NULL != uut);

	actual = stasis_message_to_ami(uut);
	ast_test_validate(test, strcmp(expected, actual->extra_fields) == 0);

	return AST_TEST_PASS;
}

static void noop(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	/* no-op */
}

AST_TEST_DEFINE(dtor_order)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test that destruction order doesn't bomb stuff";
		info->description = "Test that destruction order doesn't bomb stuff";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topic = stasis_topic_create("test-topic");
	ast_test_validate(test, NULL != topic);

	sub = stasis_subscribe(topic, noop, NULL);
	ast_test_validate(test, NULL != sub);

	/* With any luck, this won't completely blow everything up */
	ao2_cleanup(topic);
	stasis_unsubscribe(sub);

	/* These refs were cleaned up manually */
	topic = NULL;
	sub = NULL;

	return AST_TEST_PASS;
}

static const char *noop_get_id(struct stasis_message *message)
{
	return NULL;
}

AST_TEST_DEFINE(caching_dtor_order)
{
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL,
		stasis_caching_unsubscribe);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test that destruction order doesn't bomb stuff";
		info->description = "Test that destruction order doesn't bomb stuff";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = stasis_cache_create(noop_get_id);
	ast_test_validate(test, NULL != cache);

	topic = stasis_topic_create("test-topic");
	ast_test_validate(test, NULL != topic);

	caching_topic = stasis_caching_topic_create(topic, cache);
	ast_test_validate(test, NULL != caching_topic);

	sub = stasis_subscribe(stasis_caching_get_topic(caching_topic), noop,
		NULL);
	ast_test_validate(test, NULL != sub);

	/* With any luck, this won't completely blow everything up */
	ao2_cleanup(cache);
	ao2_cleanup(topic);
	stasis_caching_unsubscribe(caching_topic);
	stasis_unsubscribe(sub);

	/* These refs were cleaned up manually */
	cache = NULL;
	topic = NULL;
	caching_topic = NULL;
	sub = NULL;

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(message_type);
	AST_TEST_UNREGISTER(message);
	AST_TEST_UNREGISTER(subscription_messages);
	AST_TEST_UNREGISTER(subscription_pool_messages);
	AST_TEST_UNREGISTER(publish);
	AST_TEST_UNREGISTER(publish_sync);
	AST_TEST_UNREGISTER(publish_pool);
	AST_TEST_UNREGISTER(unsubscribe_stops_messages);
	AST_TEST_UNREGISTER(forward);
	AST_TEST_UNREGISTER(cache_filter);
	AST_TEST_UNREGISTER(cache);
	AST_TEST_UNREGISTER(cache_dump);
	AST_TEST_UNREGISTER(cache_eid_aggregate);
	AST_TEST_UNREGISTER(router);
	AST_TEST_UNREGISTER(router_pool);
	AST_TEST_UNREGISTER(router_cache_updates);
	AST_TEST_UNREGISTER(interleaving);
	AST_TEST_UNREGISTER(subscription_interleaving);
	AST_TEST_UNREGISTER(no_to_json);
	AST_TEST_UNREGISTER(to_json);
	AST_TEST_UNREGISTER(no_to_ami);
	AST_TEST_UNREGISTER(to_ami);
	AST_TEST_UNREGISTER(dtor_order);
	AST_TEST_UNREGISTER(caching_dtor_order);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(message_type);
	AST_TEST_REGISTER(message);
	AST_TEST_REGISTER(subscription_messages);
	AST_TEST_REGISTER(subscription_pool_messages);
	AST_TEST_REGISTER(publish);
	AST_TEST_REGISTER(publish_sync);
	AST_TEST_REGISTER(publish_pool);
	AST_TEST_REGISTER(unsubscribe_stops_messages);
	AST_TEST_REGISTER(forward);
	AST_TEST_REGISTER(cache_filter);
	AST_TEST_REGISTER(cache);
	AST_TEST_REGISTER(cache_dump);
	AST_TEST_REGISTER(cache_eid_aggregate);
	AST_TEST_REGISTER(router);
	AST_TEST_REGISTER(router_pool);
	AST_TEST_REGISTER(router_cache_updates);
	AST_TEST_REGISTER(interleaving);
	AST_TEST_REGISTER(subscription_interleaving);
	AST_TEST_REGISTER(no_to_json);
	AST_TEST_REGISTER(to_json);
	AST_TEST_REGISTER(no_to_ami);
	AST_TEST_REGISTER(to_ami);
	AST_TEST_REGISTER(dtor_order);
	AST_TEST_REGISTER(caching_dtor_order);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, 0, "Stasis testing",
		.load = load_module,
		.unload = unload_module
	);
