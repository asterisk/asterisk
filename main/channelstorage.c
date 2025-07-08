/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#include "asterisk.h"
#include "asterisk/options.h"
#include "channelstorage.h"

static AST_VECTOR(, const struct ast_channelstorage_driver *) storage_drivers;

int ast_channelstorage_register_driver(
	const struct ast_channelstorage_driver *driver_type)
{
	if (storage_drivers.elems == NULL) {
		AST_VECTOR_INIT(&storage_drivers, 10);
	}
	return AST_VECTOR_APPEND(&storage_drivers, driver_type);
}

const struct ast_channelstorage_driver *ast_channelstorage_get_driver(
	const char *driver_name)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&storage_drivers); i++) {
		const struct ast_channelstorage_driver *dt =
			AST_VECTOR_GET(&storage_drivers, i);
		if (strcasecmp(driver_name, dt->driver_name) == 0) {
			return dt;
		}
	}
	return NULL;
}

struct ast_channelstorage_instance *ast_channelstorage_open(
	const struct ast_channelstorage_driver *storage_driver,
	const char *instance_name)
{
	struct ast_channelstorage_instance *storage_instance = NULL;

	storage_instance = storage_driver->open_instance(instance_name);
	if (!storage_instance) {
		ast_log(LOG_ERROR, "Failed to open channel storage driver '%s'\n",
			storage_driver->driver_name);
		return NULL;
	}

	return storage_instance;
};

void ast_channelstorage_close(struct ast_channelstorage_instance *storage_instance)
{
	CHANNELSTORAGE_API(storage_instance, close_instance);
};

int channelstorage_exten_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *chan = (struct ast_channel *)obj;
	const char *context = (const char *)arg;
	const char *exten = (const char *)data;
	int ret = 0;

	ao2_lock(chan);
	if (strcasecmp(ast_channel_context(chan), context) == 0 &&
		strcasecmp(ast_channel_exten(chan), exten) == 0) {
		ret = CMP_MATCH | ((flags & OBJ_MULTIPLE) ? 0 : CMP_STOP);
	}
	ao2_unlock(chan);

	return ret;
}

struct ast_channel *channelstorage_by_exten(struct ast_channelstorage_instance *driver,
	const char *exten, const char *context)
{
	char *l_exten = (char *) exten;
	char *l_context = (char *) context;

	return CHANNELSTORAGE_API(driver, callback, channelstorage_exten_cb, l_context, l_exten, 0);
}

int channelstorage_name_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *chan = obj;
	const char *name = arg;
	size_t name_len = *(size_t *) data;
	int ret = 0;

	if (name_len == 0) {
		if(strcasecmp(ast_channel_name(chan), name) == 0) {
			ret = CMP_MATCH | ((flags & OBJ_MULTIPLE) ? 0 : CMP_STOP);
		}
	} else {
		if (strncasecmp(ast_channel_name(chan), name, name_len) == 0) {
			ret = CMP_MATCH | ((flags & OBJ_MULTIPLE) ? 0 : CMP_STOP);
		}
	}

	return ret;
}

struct ast_channel *channelstorage_by_name_or_uniqueid(struct ast_channelstorage_instance *driver,
	const char *name)
{
	return CHANNELSTORAGE_API(driver, get_by_name_prefix_or_uniqueid, name, 0);
}

struct ast_channel *channelstorage_by_name_prefix_or_uniqueid(struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len)
{
	struct ast_channel *chan = NULL;

	chan = CHANNELSTORAGE_API(driver, get_by_name_prefix, name, name_len);
	if (chan) {
		return chan;
	}

	if (name_len == 0) {
		chan = CHANNELSTORAGE_API(driver, get_by_uniqueid, name);
	}

	return chan;
}

int channelstorage_uniqueid_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *chan = obj;
	char *uniqueid = arg;
	int ret = 0;

	if(strcasecmp(ast_channel_uniqueid(chan), uniqueid) == 0) {
		ret = CMP_MATCH | CMP_STOP;
	}

	return ret;
}

struct ast_channel *channelstorage_by_uniqueid(struct ast_channelstorage_instance *driver,
	const char *uniqueid)
{
	return CHANNELSTORAGE_API(driver, callback, channelstorage_uniqueid_cb, (char *)uniqueid, NULL, 0);
}

#ifdef TEST_FRAMEWORK
#include "asterisk/test.h"
#include "channel_private.h"

static void mock_channel_destructor(void *obj)
{
	struct ast_channel *chan = obj;
	ast_string_field_free_memory(chan);
}

struct test_info {
	struct ast_test *test;
	struct ast_channelstorage_instance *storage_instance;
	enum ast_test_result_state res;
};

static void *test_storage_thread(void *data)
{
	struct test_info *test_info = data;
	struct ast_test *test = test_info->test;
	struct ast_channelstorage_instance *storage_instance = test_info->storage_instance;
	struct ast_channel *mock_channel;
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;
	struct timeval start;
	struct timeval end;
	int64_t elapsed;
	char search1[128];
	char search2[128];
	int rc = 0;
	long int rand = ast_random();
	struct ast_channel_iterator *iter;
	int collen = 25;
	int CHANNEL_COUNT = 500;
	struct ast_cli_args *cli_args = ast_test_get_cli_args(test);
	struct ast_channel **test_channels;

	for (i = 0; i < cli_args->argc; i++) {
		if (ast_begins_with(cli_args->argv[i], "channel-count=")) {
			sscanf(cli_args->argv[i], "channel-count=%d", &CHANNEL_COUNT);
		}
	}
	test_channels = ast_calloc(CHANNEL_COUNT, sizeof(*test_channels));
	ast_test_status_update(test, "%*s: %8d\n", collen, "Channel Count", CHANNEL_COUNT);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		test_channels[i] = ao2_alloc(sizeof(*mock_channel), mock_channel_destructor);
		ast_test_validate_cleanup(test, test_channels[i], res, done);
		ast_string_field_init(test_channels[i], 128);
		ast_string_field_build(test_channels[i], name, "TestChannel-%ld-%04d-something", rand, i);
		snprintf(test_channels[i]->context, AST_MAX_CONTEXT, "TestContext-%ld-%04d", rand, i % 100);
		snprintf(test_channels[i]->exten, AST_MAX_EXTENSION, "TestExten-%ld-%04d", rand, i % 10);
		snprintf(test_channels[i]->uniqueid.unique_id, AST_MAX_UNIQUEID, "TestUniqueid-%ld-%04d-something", rand, i);
		rc = CHANNELSTORAGE_API(storage_instance, insert, test_channels[i], 0, 1);
		ast_test_validate_cleanup_custom(test, rc == 0, res, done, "Unable to insert channel %s\n", test_channels[i]->name);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	i = CHANNELSTORAGE_API(storage_instance, active_channels);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "create channels", elapsed);
	ast_test_validate_cleanup(test, i == CHANNEL_COUNT, res, done);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "testchannel-%ld-%04d-something", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_name_prefix_or_uniqueid, search1, 0);
		ast_test_validate_cleanup(test, mock_channel, res, done);
		ast_test_validate_cleanup(test, mock_channel == test_channels[i], res, done);
		ast_test_validate_cleanup(test,
			strcasecmp(ast_channel_name(mock_channel), search1) == 0, res, done);
		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "by name exact", elapsed);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestUniqueid-%ld-%04d-something", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_uniqueid, search1);
		ast_test_validate_cleanup(test, mock_channel, res, done);
		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "by uniqueid exact", elapsed);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestUniqueid-%ld-%04d-something", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_name_prefix_or_uniqueid, search1, 0);
		ast_test_validate_cleanup(test, mock_channel, res, done);
		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "by uniqueid via nm", elapsed);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestChannel-%ld-%04d", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_name_prefix_or_uniqueid, search1, strlen(search1));
		ast_test_validate_cleanup(test, mock_channel, res, done);
		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "by name prefix", elapsed);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestContext-%ld-%04d", rand, i % 100);
		sprintf(search2, "TestExten-%ld-%04d", rand, i % 10);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_exten, search2, search1);
		ast_test_validate_cleanup(test, mock_channel, res, done);
		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "by context/exten", elapsed);

#if 0
	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestChannel-%ld-%04d-something", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_name_or_uniqueid, search1);
		ast_test_validate_cleanup(test, mock_channel, res, done);

		CHANNELSTORAGE_API(storage_instance, wrlock);

		sprintf(mock_channel->context, "TestXXContext-%ld-%04d", rand, i);
		sprintf(search1, "TestContext-%ld-%04d", rand, i);

		rc = CHANNELSTORAGE_API(storage_instance, update, mock_channel,
			AST_CHANNELSTORAGE_UPDATE_CONTEXT, search1, mock_channel->context, 0);
		ast_test_validate_cleanup(test, rc == 0, res, done);

		sprintf(mock_channel->exten, "TestXXExten-%ld-%04d", rand, i);
		sprintf(search2, "TestExten-%ld-%04d", rand, i);

		rc = CHANNELSTORAGE_API(storage_instance, update, mock_channel,
			AST_CHANNELSTORAGE_UPDATE_EXTEN, search2, mock_channel->exten, 0);
		CHANNELSTORAGE_API(storage_instance, unlock);

		ast_test_validate_cleanup(test, rc == 0, res, done);

		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "update", elapsed);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestXXContext-%ld-%04d", rand, i);
		sprintf(search2, "TestXXExten-%ld-%04d", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_exten, search2, search1);
		ast_test_validate_cleanup(test, mock_channel, res, done);
		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "by context/exten2", elapsed);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestChannel-%ld-%04d-something", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_name_or_uniqueid, search1);
		ast_test_validate_cleanup(test, mock_channel, res, done);
		sprintf(search2, "TestXXChannel-%ld-%04d", rand, i);
		rc = CHANNELSTORAGE_API(storage_instance, update, mock_channel,
			AST_CHANNELSTORAGE_UPDATE_NAME, search1, search2, 1);
		ast_channel_unref(mock_channel);
		ast_test_validate_cleanup(test, rc == 0, res, done);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "change name", elapsed);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		sprintf(search1, "TestXXChannel-%ld-%04d", rand, i);
		mock_channel = CHANNELSTORAGE_API(storage_instance, get_by_name_or_uniqueid, search1);
		ast_test_validate_cleanup_custom(test, mock_channel, res, done,"Channel %s not found\n", search1);
		ast_channel_unref(mock_channel);
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "by name exact2", elapsed);
#endif

	i = 0;
	start = ast_tvnow();
	iter = CHANNELSTORAGE_API(storage_instance, iterator_all_new);
	for (; (mock_channel = CHANNELSTORAGE_API(storage_instance, iterator_next, iter));
		ast_channel_unref(mock_channel)) {
		i++;
	}
	CHANNELSTORAGE_API(storage_instance, iterator_destroy, iter);
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "iter all chan", elapsed);
	ast_test_validate_cleanup_custom(test, i == CHANNEL_COUNT, res, done,
		"Expected %d channels, got %d, in container: %d\n", CHANNEL_COUNT, i,
		CHANNELSTORAGE_API(storage_instance, active_channels));

	i = 0;
	start = ast_tvnow();
	sprintf(search1, "TestChannel-%ld-%03d", rand, (CHANNEL_COUNT - 11) / 10);
	iter = CHANNELSTORAGE_API(storage_instance, iterator_by_name_new, search1, strlen(search1));
	ast_test_validate_cleanup(test, iter != NULL, res, done);
	for (; (mock_channel = CHANNELSTORAGE_API(storage_instance, iterator_next, iter));
		ast_channel_unref(mock_channel)) {
		ast_test_validate_cleanup_custom(test, strncmp(search1,
			ast_channel_name(mock_channel), strlen(search1)) == 0, res, done, "Expected %s got %s\n",
			search1, ast_channel_name(mock_channel));
		i++;
	}
	CHANNELSTORAGE_API(storage_instance, iterator_destroy, iter);
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "iter 10 partial name", elapsed);
	ast_test_validate_cleanup_custom(test, i == 10, res, done,
		"Expected %d channels, got %d, in container: %d\n", 10, i,
		CHANNELSTORAGE_API(storage_instance, active_channels));

	i = 0;
	start = ast_tvnow();
	sprintf(search1, "TestContext-%ld-%04d", rand, 50);
	sprintf(search2, "TestExten-%ld-%04d", rand, 0);
	iter = CHANNELSTORAGE_API(storage_instance, iterator_by_exten_new, search2, search1);
	ast_test_validate_cleanup(test, iter != NULL, res, done);
	for (; (mock_channel = CHANNELSTORAGE_API(storage_instance, iterator_next, iter));
		ast_channel_unref(mock_channel)) {
		ast_test_validate_cleanup_custom(test,
			(strcmp(search1, mock_channel->context) == 0 &&
			strcmp(search2, mock_channel->exten) == 0), res, done, "Expected %s-%s got %s-%s\n",
			search1, search2, mock_channel->context, mock_channel->exten);
		i++;
	}
	CHANNELSTORAGE_API(storage_instance, iterator_destroy, iter);
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "iter context/exten", elapsed);
	ast_test_validate_cleanup_custom(test, i == (CHANNEL_COUNT / 100), res, done,
		"Expected %d channels, got %d, in container: %d\n", (CHANNEL_COUNT / 100), i,
		CHANNEL_COUNT);

done:
	CHANNELSTORAGE_API(storage_instance, unlock);

	start = ast_tvnow();
	for (i = 0; i < CHANNEL_COUNT; i++) {
		if (test_channels[i]) {
			rc = CHANNELSTORAGE_API(storage_instance, remove, test_channels[i], 0);
			ast_channel_unref(test_channels[i]);
			test_channels[i] = NULL;
		}
	}
	end = ast_tvnow();
	elapsed = ast_tvdiff_us(end, start);
	ast_test_status_update(test, "%*s: %8ld\n", collen, "del all channels", elapsed);
	ast_test_validate_cleanup(test, i == CHANNEL_COUNT, res, done);
	rc = CHANNELSTORAGE_API(storage_instance, active_channels);
	ast_test_validate_cleanup_custom(test, rc == 0, res, final,
		"There are still %d channels in the container\n", rc);

	test_info->res = res;
	return NULL;

final:
	iter = CHANNELSTORAGE_API(storage_instance, iterator_all_new);
	for (; (mock_channel = CHANNELSTORAGE_API(storage_instance, iterator_next, iter));
		ast_channel_unref(mock_channel)) {
		ast_test_status_update(test, "%p %s\n", mock_channel, ast_channel_name(mock_channel));
		i++;
	}
	CHANNELSTORAGE_API(storage_instance, iterator_destroy, iter);

	test_info->res = res;
	return NULL;
}

static enum ast_test_result_state test_storage(struct ast_test_info *info,
	enum ast_test_command cmd, struct ast_test *test,
	const char *storage_name, const char *summary)
{
	const struct ast_channelstorage_driver *storage_driver;
	struct test_info ti = {
		.test = test,
		.storage_instance = NULL,
		.res = AST_TEST_PASS,
	};
	pthread_t thread;
	int rc = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = storage_name;
		info->category = "/main/channelstorage/";
		info->summary = summary;
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	storage_driver = ast_channelstorage_get_driver(info->name);
	if (!storage_driver) {
		ast_test_status_update(test, "Storage driver %s not registered\n", info->name);
		return AST_TEST_NOT_RUN;
	}
	ti.storage_instance = ast_channelstorage_open(storage_driver, "channels_test");
	ast_test_validate(test, ti.storage_instance, res);

	rc =  ast_pthread_create(&thread, NULL, test_storage_thread, &ti);
	if (rc) {
		ast_channelstorage_close(ti.storage_instance);
		ast_test_status_update(test, "Failed to create thread: %s\n", strerror(rc));
		return AST_TEST_FAIL;
	}
	pthread_join(thread, NULL);
	ast_channelstorage_close(ti.storage_instance);

	return ti.res;
}

#define DEFINE_STORAGE_TEST(_name) \
AST_TEST_DEFINE(_name) \
{ \
	return test_storage(info, cmd, test, #_name, "Channel Storage test for " #_name); \
}

DEFINE_STORAGE_TEST(ao2_legacy)

DEFINE_STORAGE_TEST(cpp_map_name_id)

#define REGISTER_STORAGE_TEST(_name) \
({ \
	if (ast_channelstorage_get_driver(#_name)) { \
        AST_TEST_REGISTER(_name); \
    } \
})
#endif

static void channelstorage_shutdown(void)
{
#ifdef TEST_FRAMEWORK
	/* Unregistering a test that wasn't previously registered is safe */
	AST_TEST_UNREGISTER(cpp_map_name_id);
	AST_TEST_UNREGISTER(ao2_legacy);
#endif
}

int ast_channelstorage_init(void)
{
#ifdef TEST_FRAMEWORK
	/* Tests run in the reverse order registered */
	REGISTER_STORAGE_TEST(cpp_map_name_id);
	AST_TEST_REGISTER(ao2_legacy);
#endif
	ast_register_cleanup(channelstorage_shutdown);

	return 0;
}

