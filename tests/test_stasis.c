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
#include "asterisk/test.h"

static const char *test_category = "/stasis/core/";

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

	ast_test_validate(test, NULL == stasis_message_type_create(NULL));
	uut = stasis_message_type_create("SomeMessage");
	ast_test_validate(test, 0 == strcmp(stasis_message_type_name(uut), "SomeMessage"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(message)
{
	RAII_VAR(struct stasis_message_type *, type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, uut, NULL, ao2_cleanup);
	RAII_VAR(char *, data, NULL, ao2_cleanup);
	char *expected = "SomeData";
	struct timeval expected_timestamp;
	struct timeval time_diff;

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


	type = stasis_message_type_create("SomeMessage");

	ast_test_validate(test, NULL == stasis_message_create(NULL, NULL));
	ast_test_validate(test, NULL == stasis_message_create(type, NULL));

	data = ao2_alloc(strlen(expected) + 1, NULL);
	strcpy(data, expected);
	expected_timestamp = ast_tvnow();
	uut = stasis_message_create(type, data);

	ast_test_validate(test, NULL != uut);
	ast_test_validate(test, type == stasis_message_type(uut));
	ast_test_validate(test, 0 == strcmp(expected, stasis_message_data(uut)));
	ast_test_validate(test, 2 == ao2_ref(data, 0)); /* uut has ref to data */

	time_diff = ast_tvsub(*stasis_message_timestamp(uut), expected_timestamp);
	/* 10ms is certainly long enough for the two calls to complete */
	ast_test_validate(test, time_diff.tv_sec == 0);
	ast_test_validate(test, time_diff.tv_usec < 10000);

	ao2_ref(uut, -1);
	uut = NULL;
	ast_test_validate(test, 1 == ao2_ref(data, 0)); /* uut unreffed data */

	return AST_TEST_PASS;
}

struct consumer {
	ast_mutex_t lock;
	ast_cond_t out;
	struct stasis_message **messages_rxed;
	size_t messages_rxed_len;
	int ignore_subscriptions;
	int complete;
};

static void consumer_dtor(void *obj) {
	struct consumer *consumer = obj;

	ast_mutex_destroy(&consumer->lock);
	ast_cond_destroy(&consumer->out);

	while (consumer->messages_rxed_len > 0) {
		ao2_cleanup(consumer->messages_rxed[--consumer->messages_rxed_len]);
	}
	ast_free(consumer->messages_rxed);
	consumer->messages_rxed = NULL;
}

static struct consumer *consumer_create(int ignore_subscriptions) {
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);

	consumer = ao2_alloc(sizeof(*consumer), consumer_dtor);

	if (!consumer) {
		return NULL;
	}

	consumer->ignore_subscriptions = ignore_subscriptions;
	consumer->messages_rxed = ast_malloc(0);
	if (!consumer->messages_rxed) {
		return NULL;
	}

	ast_mutex_init(&consumer->lock);
	ast_cond_init(&consumer->out, NULL);

	ao2_ref(consumer, +1);
	return consumer;
}

static void consumer_exec(void *data, struct stasis_subscription *sub, struct stasis_topic *topic, struct stasis_message *message)
{
	struct consumer *consumer = data;
	RAII_VAR(struct consumer *, consumer_needs_cleanup, NULL, ao2_cleanup);
	SCOPED_MUTEX(lock, &consumer->lock);

	if (!consumer->ignore_subscriptions || stasis_message_type(message) != stasis_subscription_change()) {

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

static int consumer_wait_for(struct consumer *consumer, size_t expected_len)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + 30,
		.tv_nsec = start.tv_usec * 1000
	};

	SCOPED_MUTEX(lock, &consumer->lock);

	while (consumer->messages_rxed_len < expected_len) {
		int r = ast_cond_timedwait(&consumer->out, &consumer->lock, &end);
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
		.tv_sec = start.tv_sec + 30,
		.tv_nsec = start.tv_usec * 1000
	};

	SCOPED_MUTEX(lock, &consumer->lock);

	while (!consumer->complete) {
		int r = ast_cond_timedwait(&consumer->out, &consumer->lock, &end);
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

	SCOPED_MUTEX(lock, &consumer->lock);

	while (consumer->messages_rxed_len == expected_len) {
		int r = ast_cond_timedwait(&consumer->out, &consumer->lock, &end);
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

	stasis_unsubscribe(uut);
	uut = NULL;
	complete = consumer_wait_for_completion(consumer);
	ast_test_validate(test, 1 == complete);

	ast_test_validate(test, 2 == consumer->messages_rxed_len);
	ast_test_validate(test, stasis_subscription_change() == stasis_message_type(consumer->messages_rxed[0]));
	ast_test_validate(test, stasis_subscription_change() == stasis_message_type(consumer->messages_rxed[1]));

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

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	test_message_type = stasis_message_type_create("TestMessage");
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

	stasis_unsubscribe(uut);
	uut = NULL;

	test_data = ao2_alloc(1, NULL);
	ast_test_validate(test, NULL != test_data);
	test_message_type = stasis_message_type_create("TestMessage");
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

	RAII_VAR(struct stasis_subscription *, forward_sub, NULL, stasis_unsubscribe);
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
	test_message_type = stasis_message_type_create("TestMessage");
	test_message = stasis_message_create(test_message_type, test_data);

	stasis_publish(topic, test_message);

	actual_len = consumer_wait_for(consumer, 1);
	ast_test_validate(test, 1 == actual_len);
	actual_len = consumer_wait_for(parent_consumer, 1);
	ast_test_validate(test, 1 == actual_len);

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

static struct stasis_message *cache_test_message_create(struct stasis_message_type *type, const char *name, const char *value)
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

	return stasis_message_create(type, data);
}

static const char *cache_test_data_id(struct stasis_message *message) {
	struct cache_test_data *cachable = stasis_message_data(message);

	if (0 != strcmp("Cacheable", stasis_message_type_name(stasis_message_type(message)))) {
		return NULL;
	}
	return cachable->id;
}

AST_TEST_DEFINE(cache_passthrough)
{
	RAII_VAR(struct stasis_message_type *, non_cache_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, stasis_caching_unsubscribe);
	RAII_VAR(struct consumer *, consumer, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	RAII_VAR(struct stasis_message *, test_message, NULL, ao2_cleanup);
	int actual_len;
	struct stasis_message_type *actual_type;

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

	non_cache_type = stasis_message_type_create("NonCacheable");
	ast_test_validate(test, NULL != non_cache_type);
	topic = stasis_topic_create("SomeTopic");
	ast_test_validate(test, NULL != topic);
	caching_topic = stasis_caching_topic_create(topic, cache_test_data_id);
	ast_test_validate(test, NULL != caching_topic);
	consumer = consumer_create(1);
	ast_test_validate(test, NULL != consumer);
	sub = stasis_subscribe(stasis_caching_get_topic(caching_topic), consumer_exec, consumer);
	ast_test_validate(test, NULL != sub);
	ao2_ref(consumer, +1);

	test_message = cache_test_message_create(non_cache_type, "1", "1");
	ast_test_validate(test, NULL != test_message);

	stasis_publish(topic, test_message);

	actual_len = consumer_wait_for(consumer, 1);
	ast_test_validate(test, 1 == actual_len);

	actual_type = stasis_message_type(consumer->messages_rxed[0]);
	ast_test_validate(test, non_cache_type == actual_type);

	ast_test_validate(test, test_message == consumer->messages_rxed[0]);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(cache)
{
	RAII_VAR(struct stasis_message_type *, cache_type, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_topic *, topic, NULL, ao2_cleanup);
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

	cache_type = stasis_message_type_create("Cacheable");
	ast_test_validate(test, NULL != cache_type);
	topic = stasis_topic_create("SomeTopic");
	ast_test_validate(test, NULL != topic);
	caching_topic = stasis_caching_topic_create(topic, cache_test_data_id);
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
	ast_test_validate(test, stasis_cache_update() == stasis_message_type(consumer->messages_rxed[0]));
	actual_update = stasis_message_data(consumer->messages_rxed[0]);
	ast_test_validate(test, topic == actual_update->topic);
	ast_test_validate(test, NULL == actual_update->old_snapshot);
	ast_test_validate(test, test_message1_1 == actual_update->new_snapshot);
	ast_test_validate(test, test_message1_1 == stasis_cache_get(caching_topic, cache_type, "1"));
	/* stasis_cache_get returned a ref, so unref test_message1_1 */
	ao2_ref(test_message1_1, -1);

	ast_test_validate(test, stasis_cache_update() == stasis_message_type(consumer->messages_rxed[1]));
	actual_update = stasis_message_data(consumer->messages_rxed[1]);
	ast_test_validate(test, topic == actual_update->topic);
	ast_test_validate(test, NULL == actual_update->old_snapshot);
	ast_test_validate(test, test_message2_1 == actual_update->new_snapshot);
	ast_test_validate(test, test_message2_1 == stasis_cache_get(caching_topic, cache_type, "2"));
	/* stasis_cache_get returned a ref, so unref test_message2_1 */
	ao2_ref(test_message2_1, -1);

	/* Update snapshot 2 */
	test_message2_2 = cache_test_message_create(cache_type, "2", "2");
	ast_test_validate(test, NULL != test_message2_2);
	stasis_publish(topic, test_message2_2);

	actual_len = consumer_wait_for(consumer, 3);
	ast_test_validate(test, 3 == actual_len);

	actual_update = stasis_message_data(consumer->messages_rxed[2]);
	ast_test_validate(test, topic == actual_update->topic);
	ast_test_validate(test, test_message2_1 == actual_update->old_snapshot);
	ast_test_validate(test, test_message2_2 == actual_update->new_snapshot);
	ast_test_validate(test, test_message2_2 == stasis_cache_get(caching_topic, cache_type, "2"));
	/* stasis_cache_get returned a ref, so unref test_message2_2 */
	ao2_ref(test_message2_2, -1);

	/* Clear snapshot 1 */
	test_message1_clear = stasis_cache_clear_create(cache_type, "1");
	ast_test_validate(test, NULL != test_message1_clear);
	stasis_publish(topic, test_message1_clear);

	actual_len = consumer_wait_for(consumer, 4);
	ast_test_validate(test, 4 == actual_len);

	actual_update = stasis_message_data(consumer->messages_rxed[3]);
	ast_test_validate(test, topic == actual_update->topic);
	ast_test_validate(test, test_message1_1 == actual_update->old_snapshot);
	ast_test_validate(test, NULL == actual_update->new_snapshot);
	ast_test_validate(test, NULL == stasis_cache_get(caching_topic, cache_type, "1"));

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(message_type);
	AST_TEST_UNREGISTER(message);
	AST_TEST_UNREGISTER(subscription_messages);
	AST_TEST_UNREGISTER(publish);
	AST_TEST_UNREGISTER(unsubscribe_stops_messages);
	AST_TEST_UNREGISTER(forward);
	AST_TEST_UNREGISTER(cache_passthrough);
	AST_TEST_UNREGISTER(cache);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(message_type);
	AST_TEST_REGISTER(message);
	AST_TEST_REGISTER(subscription_messages);
	AST_TEST_REGISTER(publish);
	AST_TEST_REGISTER(unsubscribe_stops_messages);
	AST_TEST_REGISTER(forward);
	AST_TEST_REGISTER(cache_passthrough);
	AST_TEST_REGISTER(cache);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, 0, "Stasis testing",
		.load = load_module,
		.unload = unload_module
	);
