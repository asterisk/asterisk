/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Sorcery Unit Tests
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/logger.h"
#include "asterisk/vector.h"
#include "asterisk/cli.h"

/*! \brief The default amount of time (in seconds) that thrash unit tests execute for */
#define TEST_THRASH_TIME 3

/*! \brief The number of threads to use for retrieving for applicable tests */
#define TEST_THRASH_RETRIEVERS 25

/*! \brief The number of threads to use for updating for applicable tests*/
#define TEST_THRASH_UPDATERS 25

/*! \brief Structure for a memory cache thras thread */
struct sorcery_memory_cache_thrash_thread {
	/*! \brief The thread thrashing the cache */
	pthread_t thread;
	/*! \brief Sorcery instance being tested */
	struct ast_sorcery *sorcery;
	/*! \brief The number of unique objects we should restrict ourself to */
	unsigned int unique_objects;
	/*! \brief Set when the thread should stop */
	unsigned int stop;
	/*! \brief Average time spent executing sorcery operation in this thread */
	unsigned int average_execution_time;
};

/*! \brief Structure for memory cache thrashing */
struct sorcery_memory_cache_thrash {
	/*! \brief The sorcery instance being tested */
	struct ast_sorcery *sorcery;
	/*! \brief The number of threads which are updating */
	unsigned int update_threads;
	/*! \brief The average execution time of sorcery update operations */
	unsigned int average_update_execution_time;
	/*! \brief The number of threads which are retrieving */
	unsigned int retrieve_threads;
	/*! \brief The average execution time of sorcery retrieve operations */
	unsigned int average_retrieve_execution_time;
	/*! \brief Threads which are updating or reading from the cache */
	AST_VECTOR(, struct sorcery_memory_cache_thrash_thread *) threads;
};

/*!
 * \brief Sorcery object created based on backend data
 */
struct test_data {
	SORCERY_OBJECT(details);
};

/*!
 * \brief Allocation callback for test_data sorcery object
 */
static void *test_data_alloc(const char *id)
{
	return ast_sorcery_generic_alloc(sizeof(struct test_data), NULL);
}

/*!
 * \brief Callback for retrieving sorcery object by ID
 *
 * \param sorcery The sorcery instance
 * \param data Unused
 * \param type The object type. Will always be "test".
 * \param id The object id. Will always be "test".
 *
 * \retval NULL Backend data successfully allocated
 * \retval non-NULL Backend data could not be successfully allocated
 */
static void *mock_retrieve_id(const struct ast_sorcery *sorcery, void *data,
		const char *type, const char *id)
{
	return ast_sorcery_alloc(sorcery, type, id);
}

/*!
 * \brief Callback for updating a sorcery object
 *
 * \param sorcery The sorcery instance
 * \param data Unused
 * \param object The object to update.
 *
 */
static int mock_update(const struct ast_sorcery *sorcery, void *data,
	void *object)
{
	return 0;
}

/*!
 * \brief A mock sorcery wizard used for the stale test
 */
static struct ast_sorcery_wizard mock_wizard = {
	.name = "mock",
	.retrieve_id = mock_retrieve_id,
	.update = mock_update,
};

/*!
 * \internal
 * \brief Destructor for sorcery memory cache thrasher
 *
 * \param obj The sorcery memory cache thrash structure
 */
static void sorcery_memory_cache_thrash_destroy(void *obj)
{
	struct sorcery_memory_cache_thrash *thrash = obj;
	int idx;

	if (thrash->sorcery) {
		ast_sorcery_unref(thrash->sorcery);
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&thrash->threads); ++idx) {
		struct sorcery_memory_cache_thrash_thread *thread;

		thread = AST_VECTOR_GET(&thrash->threads, idx);
		ast_free(thread);
	}
	AST_VECTOR_FREE(&thrash->threads);

	ast_sorcery_wizard_unregister(&mock_wizard);
}

/*!
 * \internal
 * \brief Set up thrashing against a memory cache on a sorcery instance
 *
 * \param cache_configuration The sorcery memory cache configuration to use
 * \param update_threads The number of threads which should be constantly updating sorcery
 * \param retrieve_threads The number of threads which should be constantly retrieving from sorcery
 * \param unique_objects The number of unique objects that can exist
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static struct sorcery_memory_cache_thrash *sorcery_memory_cache_thrash_create(const char *cache_configuration,
	unsigned int update_threads, unsigned int retrieve_threads, unsigned int unique_objects)
{
	struct sorcery_memory_cache_thrash *thrash;
	struct sorcery_memory_cache_thrash_thread *thread;
	unsigned int total_threads = update_threads + retrieve_threads;

	thrash = ao2_alloc_options(sizeof(*thrash), sorcery_memory_cache_thrash_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!thrash) {
		return NULL;
	}

	thrash->update_threads = update_threads;
	thrash->retrieve_threads = retrieve_threads;

	ast_sorcery_wizard_register(&mock_wizard);

	thrash->sorcery = ast_sorcery_open();
	if (!thrash->sorcery) {
		ao2_ref(thrash, -1);
		return NULL;
	}

	ast_sorcery_apply_wizard_mapping(thrash->sorcery, "test", "memory_cache",
			!strcmp(cache_configuration, "default") ? "" : cache_configuration, 1);
	ast_sorcery_apply_wizard_mapping(thrash->sorcery, "test", "mock", NULL, 0);
	ast_sorcery_internal_object_register(thrash->sorcery, "test", test_data_alloc, NULL, NULL);

	if (AST_VECTOR_INIT(&thrash->threads, update_threads + retrieve_threads)) {
		ao2_ref(thrash, -1);
		return NULL;
	}

	while (AST_VECTOR_SIZE(&thrash->threads) != total_threads) {
		thread = ast_calloc(1, sizeof(*thread));

		if (!thread) {
			ao2_ref(thrash, -1);
			return NULL;
		}

		thread->thread = AST_PTHREADT_NULL;
		thread->unique_objects = unique_objects;

		/* This purposely holds no ref as the main thrash structure does */
		thread->sorcery = thrash->sorcery;

		if (AST_VECTOR_APPEND(&thrash->threads, thread)) {
			ast_free(thread);
			ao2_ref(thrash, -1);
			return NULL;
		}
	}

	return thrash;
}

/*!
 * \internal
 * \brief Thrashing cache update thread
 *
 * \param data The sorcery memory cache thrash thread
 */
static void *sorcery_memory_cache_thrash_update(void *data)
{
	struct sorcery_memory_cache_thrash_thread *thread = data;
	struct timeval start;
	unsigned int object_id;
	char object_id_str[AST_UUID_STR_LEN];
	void *object;

	while (!thread->stop) {
		object_id = ast_random() % thread->unique_objects;
		snprintf(object_id_str, sizeof(object_id_str), "%u", object_id);

		object = ast_sorcery_alloc(thread->sorcery, "test", object_id_str);
		ast_assert(object != NULL);

		start = ast_tvnow();
		ast_sorcery_update(thread->sorcery, object);
		thread->average_execution_time = (thread->average_execution_time + ast_tvdiff_ms(ast_tvnow(), start)) / 2;
		ao2_ref(object, -1);
	}

	return NULL;
}

/*!
 * \internal
 * \brief Thrashing cache retrieve thread
 *
 * \param data The sorcery memory cache thrash thread
 */
static void *sorcery_memory_cache_thrash_retrieve(void *data)
{
	struct sorcery_memory_cache_thrash_thread *thread = data;
	struct timeval start;
	unsigned int object_id;
	char object_id_str[AST_UUID_STR_LEN];
	void *object;

	while (!thread->stop) {
		object_id = ast_random() % thread->unique_objects;
		snprintf(object_id_str, sizeof(object_id_str), "%u", object_id);

		start = ast_tvnow();
		object = ast_sorcery_retrieve_by_id(thread->sorcery, "test", object_id_str);
		thread->average_execution_time = (thread->average_execution_time + ast_tvdiff_ms(ast_tvnow(), start)) / 2;
		ast_assert(object != NULL);

		ao2_ref(object, -1);
	}

	return NULL;
}

/*!
 * \internal
 * \brief Stop thrashing against a sorcery memory cache
 *
 * \param thrash The sorcery memory cache thrash structure
 */
static void sorcery_memory_cache_thrash_stop(struct sorcery_memory_cache_thrash *thrash)
{
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(&thrash->threads); ++idx) {
		struct sorcery_memory_cache_thrash_thread *thread;

		thread = AST_VECTOR_GET(&thrash->threads, idx);
		if (thread->thread == AST_PTHREADT_NULL) {
			continue;
		}

		thread->stop = 1;
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&thrash->threads); ++idx) {
		struct sorcery_memory_cache_thrash_thread *thread;

		thread = AST_VECTOR_GET(&thrash->threads, idx);
		if (thread->thread == AST_PTHREADT_NULL) {
			continue;
		}

		pthread_join(thread->thread, NULL);

		if (idx < thrash->update_threads) {
			thrash->average_update_execution_time += thread->average_execution_time;
		} else {
			thrash->average_retrieve_execution_time += thread->average_execution_time;
		}
	}

	if (thrash->update_threads) {
		thrash->average_update_execution_time /= thrash->update_threads;
	}
	if (thrash->retrieve_threads) {
		thrash->average_retrieve_execution_time /= thrash->retrieve_threads;
	}
}

/*!
 * \internal
 * \brief Start thrashing against a sorcery memory cache
 *
 * \param thrash The sorcery memory cache thrash structure
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int sorcery_memory_cache_thrash_start(struct sorcery_memory_cache_thrash *thrash)
{
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(&thrash->threads); ++idx) {
		struct sorcery_memory_cache_thrash_thread *thread;

		thread = AST_VECTOR_GET(&thrash->threads, idx);

		if (ast_pthread_create(&thread->thread, NULL, idx < thrash->update_threads ?
			sorcery_memory_cache_thrash_update : sorcery_memory_cache_thrash_retrieve, thread)) {
			sorcery_memory_cache_thrash_stop(thrash);
			return -1;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief CLI command implementation for 'sorcery memory cache thrash'
 */
static char *sorcery_memory_cache_cli_thrash(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sorcery_memory_cache_thrash *thrash;
	unsigned int thrash_time, unique_objects, retrieve_threads, update_threads;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sorcery memory cache thrash";
		e->usage =
		    "Usage: sorcery memory cache thrash <cache configuration> <amount of time to thrash the cache> <number of unique objects> <number of retrieve threads> <number of update threads>\n"
		    "       Create a sorcery instance with a memory cache using the provided configuration and thrash it.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 9) {
		return CLI_SHOWUSAGE;
	}

	if (sscanf(a->argv[5], "%30u", &thrash_time) != 1) {
		ast_cli(a->fd, "An invalid value of '%s' has been provided for the thrashing time\n", a->argv[5]);
		return CLI_FAILURE;
	} else if (sscanf(a->argv[6], "%30u", &unique_objects) != 1) {
		ast_cli(a->fd, "An invalid value of '%s' has been provided for number of unique objects\n", a->argv[6]);
		return CLI_FAILURE;
	} else if (sscanf(a->argv[7], "%30u", &retrieve_threads) != 1) {
		ast_cli(a->fd, "An invalid value of '%s' has been provided for the number of retrieve threads\n", a->argv[7]);
		return CLI_FAILURE;
	} else if (sscanf(a->argv[8], "%30u", &update_threads) != 1) {
		ast_cli(a->fd, "An invalid value of '%s' has been provided for the number of update threads\n", a->argv[8]);
		return CLI_FAILURE;
	}

	thrash = sorcery_memory_cache_thrash_create(a->argv[4], update_threads, retrieve_threads, unique_objects);
	if (!thrash) {
		ast_cli(a->fd, "Could not create a sorcery memory cache thrash test using the provided arguments\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Starting cache thrash test.\n");
	ast_cli(a->fd, "Memory cache configuration: %s\n", a->argv[4]);
	ast_cli(a->fd, "Amount of time to perform test: %u seconds\n", thrash_time);
	ast_cli(a->fd, "Number of unique objects: %u\n", unique_objects);
	ast_cli(a->fd, "Number of retrieve threads: %u\n", retrieve_threads);
	ast_cli(a->fd, "Number of update threads: %u\n", update_threads);

	sorcery_memory_cache_thrash_start(thrash);
	while ((thrash_time = sleep(thrash_time)));
	sorcery_memory_cache_thrash_stop(thrash);

	ast_cli(a->fd, "Stopped cache thrash test\n");

	ast_cli(a->fd, "Average retrieve execution time (in milliseconds): %u\n", thrash->average_retrieve_execution_time);
	ast_cli(a->fd, "Average update execution time (in milliseconds): %u\n", thrash->average_update_execution_time);

	ao2_ref(thrash, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_memory_cache_thrash[] = {
	AST_CLI_DEFINE(sorcery_memory_cache_cli_thrash, "Thrash a sorcery memory cache"),
};

/*!
 * \internal
 * \brief Perform a thrash test against a cache
 *
 * \param test The unit test being run
 * \param cache_configuration The underlying cache configuration
 * \param thrash_time How long (in seconds) to thrash the cache for
 * \param unique_objects The number of unique objects
 * \param retrieve_threads The number of threads constantly doing a retrieve
 * \param update_threads The number of threads constantly doing an update
 *
 * \retval AST_TEST_PASS success
 * \retval AST_TEST_FAIL failure
 */
static enum ast_test_result_state nominal_thrash(struct ast_test *test, const char *cache_configuration,
	unsigned int thrash_time, unsigned int unique_objects, unsigned int retrieve_threads,
	unsigned int update_threads)
{
	struct sorcery_memory_cache_thrash *thrash;

	thrash = sorcery_memory_cache_thrash_create(cache_configuration, update_threads, retrieve_threads, unique_objects);
	if (!thrash) {
		return AST_TEST_FAIL;
	}

	sorcery_memory_cache_thrash_start(thrash);
	while ((thrash_time = sleep(thrash_time)));
	sorcery_memory_cache_thrash_stop(thrash);

	ao2_ref(thrash, -1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(low_unique_object_count_immediately_stale)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "low_unique_object_count_immediately_stale";
		info->category = "/res/res_sorcery_memory_cache/thrash/";
		info->summary = "Thrash a cache with low number of unique objects that are immediately stale";
		info->description = "This test creates a cache with objects that are stale\n"
			"after 1 second. It also creates 25 threads which are constantly attempting\n"
			"to retrieve the objects. This test confirms that the background refreshes\n"
			"being done as a result of going stale do not conflict or cause problems with\n"
			"the large number of retrieve threads.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_thrash(test, "object_lifetime_stale=1", TEST_THRASH_TIME, 10, TEST_THRASH_RETRIEVERS, 0);
}

AST_TEST_DEFINE(low_unique_object_count_immediately_expire)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "low_unique_object_count_immediately_expire";
		info->category = "/res/res_sorcery_memory_cache/thrash/";
		info->summary = "Thrash a cache with low number of unique objects that are immediately expired";
		info->description = "This test creates a cache with objects that are expired\n"
			"after 1 second. It also creates 25 threads which are constantly attempting\n"
			"to retrieve the objects. This test confirms that the expiration process does\n"
			"not cause a problem as the retrieve threads execute.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_thrash(test, "object_lifetime_maximum=1", TEST_THRASH_TIME, 10, TEST_THRASH_RETRIEVERS, 0);
}

AST_TEST_DEFINE(low_unique_object_count_high_concurrent_updates)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "low_unique_object_count_high_concurrent_updates";
		info->category = "/res/res_sorcery_memory_cache/thrash/";
		info->summary = "Thrash a cache with low number of unique objects that are updated frequently";
		info->description = "This test creates a cache with objects that are being constantly\n"
			"updated and retrieved at the same time. This will create contention between all\n"
			"of the threads as the write lock is held for the updates. This test confirms that\n"
			"no problems occur in this situation.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_thrash(test, "default", TEST_THRASH_TIME, 10, TEST_THRASH_RETRIEVERS, TEST_THRASH_UPDATERS);
}

AST_TEST_DEFINE(unique_objects_exceeding_maximum)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "unique_objects_exceeding_maximum";
		info->category = "/res/res_sorcery_memory_cache/thrash/";
		info->summary = "Thrash a cache with a fixed maximum object count";
		info->description = "This test creates a cache with a maximum number of objects\n"
			"allowed in it. The maximum number of unique objects, however, far exceeds the\n"
			"the maximum number allowed in the cache. This test confirms that the cache does\n"
			"not exceed the maximum and that the removal of older objects does not cause\n"
			"a problem.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_thrash(test, "maximum_objects=10", TEST_THRASH_TIME, 100, TEST_THRASH_RETRIEVERS, 0);
}

AST_TEST_DEFINE(unique_objects_exceeding_maximum_with_expire_and_stale)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "unique_objects_exceeding_maximum_with_expire_and_stale";
		info->category = "/res/res_sorcery_memory_cache/thrash/";
		info->summary = "Thrash a cache with a fixed maximum object count with objects that expire and go stale";
		info->description = "This test creates a cache with a maximum number of objects\n"
			"allowed in it with objects that also go stale after a period of time and expire.\n"
			"A number of threads are created that constantly retrieve from the cache, causing\n"
			"both stale refresh and expiration to occur. This test confirms that the combination\n"
			"of these do not present a problem.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_thrash(test, "maximum_objects=10,object_lifetime_maximum=2,object_lifetime_stale=1",
		TEST_THRASH_TIME * 2, 100, TEST_THRASH_RETRIEVERS, 0);
}

AST_TEST_DEFINE(conflicting_expire_and_stale)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "conflicting_expire_and_stale";
		info->category = "/res/res_sorcery_memory_cache/thrash/";
		info->summary = "Thrash a cache with a large number of objects that expire and go stale";
		info->description = "This test creates a cache with a large number of objects that expire\n"
			"and go stale. As there is such a large number this ensures that both operations occur.\n"
			"This test confirms that stale refreshing and expiration do not conflict.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_thrash(test, "object_lifetime_maximum=2,object_lifetime_stale=1", TEST_THRASH_TIME * 2, 5000,
		TEST_THRASH_RETRIEVERS, 0);
}

AST_TEST_DEFINE(high_object_count_without_expiration)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "high_object_count_without_expiration";
		info->category = "/res/res_sorcery_memory_cache/thrash/";
		info->summary = "Thrash a cache with a large number of objects";
		info->description = "This test creates a cache with a large number of objects that persist.\n"
			"A large number of threads are created which constantly retrieve from the cache.\n"
			"This test confirms that the large number of retrieves do not cause a problem.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_thrash(test, "default", TEST_THRASH_TIME, 5000, TEST_THRASH_RETRIEVERS, 0);
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_memory_cache_thrash, ARRAY_LEN(cli_memory_cache_thrash));
	AST_TEST_UNREGISTER(low_unique_object_count_immediately_stale);
	AST_TEST_UNREGISTER(low_unique_object_count_immediately_expire);
	AST_TEST_UNREGISTER(low_unique_object_count_high_concurrent_updates);
	AST_TEST_UNREGISTER(unique_objects_exceeding_maximum);
	AST_TEST_UNREGISTER(unique_objects_exceeding_maximum_with_expire_and_stale);
	AST_TEST_UNREGISTER(conflicting_expire_and_stale);
	AST_TEST_UNREGISTER(high_object_count_without_expiration);

	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_memory_cache_thrash, ARRAY_LEN(cli_memory_cache_thrash));
	AST_TEST_REGISTER(low_unique_object_count_immediately_stale);
	AST_TEST_REGISTER(low_unique_object_count_immediately_expire);
	AST_TEST_REGISTER(low_unique_object_count_high_concurrent_updates);
	AST_TEST_REGISTER(unique_objects_exceeding_maximum);
	AST_TEST_REGISTER(unique_objects_exceeding_maximum_with_expire_and_stale);
	AST_TEST_REGISTER(conflicting_expire_and_stale);
	AST_TEST_REGISTER(high_object_count_without_expiration);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Sorcery Cache Thrasing test module");
