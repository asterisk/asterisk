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
 *
 * \brief Sorcery Memory Cache Object Wizard
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astobj2.h"
#include "asterisk/threadpool.h"
#include "asterisk/config_options.h"
#include "asterisk/sched.h"
#include "asterisk/test.h"

/*** DOCUMENTATION
	<configInfo name="res_sorcery_memory_cache" language="en_US">
		<configFile name="sorcery_memory_cache.conf">
			<configObject name="threadpool">
				<synopsis>Settings that configure the threadpool res_sorcery_memory_cache uses for cache management.</synopsis>
				<configOption name="initial_size" default="1">
					<synopsis>Initial number of threads in the cache management threadpool.</synopsis>
				</configOption>
				<configOption name="idle_timeout_sec" default="20">
					<synopsis>Number of seconds before an idle thread is disposed of.</synopsis>
				</configOption>
				<configOption name="max_size" default="50">
					<synopsis>Maximum number of threads in the threadpool.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

/*! \brief Structure for storing a memory cache */
struct sorcery_memory_cache {
	/*! \brief The name of the memory cache */
	char *name;
	/*! \brief Objects in the cache */
	struct ao2_container *objects;
	/*! \brief The maximum number of objects permitted in the cache, 0 if no limit */
	unsigned int maximum_objects;
	/*! \brief The maximum time (in seconds) an object will stay in the cache, 0 if no limit */
	unsigned int object_lifetime_maximum;
	/*! \brief The amount of time (in seconds) before an object is marked as stale, 0 if disabled */
	unsigned int object_lifetime_stale;
	/*! \brief Whether objects are prefetched from normal storage at load time, 0 if disabled */
	unsigned int prefetch;
	/** \brief Whether all objects are expired when the object type is reloaded, 0 if disabled */
	unsigned int expire_on_reload;
};

static void *sorcery_memory_cache_open(const char *data);
static int sorcery_memory_cache_create(const struct ast_sorcery *sorcery, void *data, void *object);
static void sorcery_memory_cache_load(void *data, const struct ast_sorcery *sorcery, const char *type);
static void sorcery_memory_cache_reload(void *data, const struct ast_sorcery *sorcery, const char *type);
static void *sorcery_memory_cache_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type,
	const char *id);
static int sorcery_memory_cache_delete(const struct ast_sorcery *sorcery, void *data, void *object);
static void sorcery_memory_cache_close(void *data);

static struct ast_sorcery_wizard memory_cache_object_wizard = {
	.name = "memory_cache",
	.open = sorcery_memory_cache_open,
	.create = sorcery_memory_cache_create,
	.update = sorcery_memory_cache_create,
	.delete = sorcery_memory_cache_delete,
	.load = sorcery_memory_cache_load,
	.reload = sorcery_memory_cache_reload,
	.retrieve_id = sorcery_memory_cache_retrieve_id,
	.close = sorcery_memory_cache_close,
};

/*! \brief The bucket size for the container of caches */
#define CACHES_CONTAINER_BUCKET_SIZE 53

/*! \brief The default bucket size for the container of objects in the cache */
#define CACHE_CONTAINER_BUCKET_SIZE 53

/*! \brief Container of created caches */
static struct ao2_container *caches;

/*! \brief Threadpool configuration options */
struct sorcery_memory_cache_threadpool_conf {
	/*! Initial size of the thread pool */
	int initial_size;
	/*! Time, in seconds, before we expire a thread */
	int idle_timeout_sec;
	/*! Maximum number of thread to allow */
	int max_size;
};

struct sorcery_memory_cache_config {
	/*! Thread pool configuration options */
	struct sorcery_memory_cache_threadpool_conf *threadpool_options;
};

static struct aco_type threadpool_option = {
	.type = ACO_GLOBAL,
	.name = "threadpool",
	.item_offset = offsetof(struct sorcery_memory_cache_config, threadpool_options),
	.category = "^threadpool$",
	.category_match = ACO_WHITELIST,
};

static struct aco_type *threadpool_options[] = ACO_TYPES(&threadpool_option);

struct aco_file sorcery_memory_cache_conf = {
	.filename = "sorcery_memory_cache.conf",
	.types = ACO_TYPES(&threadpool_option),
};

/*! \brief A global object container that will contain the sorcery_memory_cache_config that gets swapped out on reloads */
static AO2_GLOBAL_OBJ_STATIC(globals);

static void *sorcery_memory_cache_config_alloc(void);

/*! \brief Register information about the configs being processed by this module */
CONFIG_INFO_STANDARD(cfg_info, globals, sorcery_memory_cache_config_alloc,
        .files = ACO_FILES(&sorcery_memory_cache_conf),
);

/*! \brief Thread pool for cache management */
static struct ast_threadpool *pool;

/*! \brief Scheduler for cache management */
static struct ast_sched_context *sched;

/*!
 * \internal
 * \brief Destructor for the sorcery memory cache configuration structure
 */
static void sorcery_memory_cache_config_destructor(void *obj)
{
	struct sorcery_memory_cache_config *cfg = obj;

	ast_free(cfg->threadpool_options);
}

/*!
 * \internal
 * \brief Allocator for the sorcery memory cache configuration structure
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static void *sorcery_memory_cache_config_alloc(void)
{
	struct sorcery_memory_cache_config *cfg;

	cfg = ao2_alloc_options(sizeof(*cfg), sorcery_memory_cache_config_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg) {
		return NULL;
	}

	cfg->threadpool_options = ast_calloc(1, sizeof(*cfg->threadpool_options));
	if (!cfg->threadpool_options) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

/*!
 * \internal
 * \brief Hashing function for the container holding caches
 *
 * \param obj A sorcery memory cache or name of one
 * \param flags Hashing flags
 *
 * \return The hash of the memory cache name
 */
static int sorcery_memory_cache_hash(const void *obj, int flags)
{
	const struct sorcery_memory_cache *cache = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_SEARCH_OBJECT | OBJ_SEARCH_KEY | OBJ_SEARCH_PARTIAL_KEY)) {
	default:
	case OBJ_SEARCH_OBJECT:
		name = cache->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		hash = ast_str_hash(name);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Should never happen in hash callback. */
		ast_assert(0);
		hash = 0;
		break;
	}
	return hash;
}

/*!
 * \internal
 * \brief Comparison function for the container holding caches
 *
 * \param obj A sorcery memory cache
 * \param arg A sorcery memory cache, or name of one
 * \param flags Comparison flags
 *
 * \retval CMP_MATCH if the name is the same
 * \retval 0 if the name does not match
 */
static int sorcery_memory_cache_cmp(void *obj, void *arg, int flags)
{
	const struct sorcery_memory_cache *left = obj;
	const struct sorcery_memory_cache *right = arg;
	const char *right_name = arg;
	int cmp;

	switch (flags & (OBJ_SEARCH_OBJECT | OBJ_SEARCH_KEY | OBJ_SEARCH_PARTIAL_KEY)) {
	default:
	case OBJ_SEARCH_OBJECT:
		right_name = right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left->name, right_name);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left->name, right_name, strlen(right_name));
		break;
	}
	return cmp ? 0 : CMP_MATCH;
}

/*!
 * \internal
 * \brief Hashing function for the container holding cached objects
 *
 * \param obj A cached object or id of one
 * \param flags Hashing flags
 *
 * \return The hash of the cached object id
 */
static int sorcery_memory_cached_object_hash(const void *obj, int flags)
{
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_SEARCH_OBJECT | OBJ_SEARCH_KEY | OBJ_SEARCH_PARTIAL_KEY)) {
	default:
	case OBJ_SEARCH_OBJECT:
		name = ast_sorcery_object_get_id(obj);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		hash = ast_str_hash(name);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Should never happen in hash callback. */
		ast_assert(0);
		hash = 0;
		break;
	}
	return hash;
}

/*!
 * \internal
 * \brief Comparison function for the container holding cached objects
 *
 * \param obj A cached object
 * \param arg A cached object, or id of one
 * \param flags Comparison flags
 *
 * \retval CMP_MATCH if the id is the same
 * \retval 0 if the id does not match
 */
static int sorcery_memory_cached_object_cmp(void *obj, void *arg, int flags)
{
	void *left = obj;
	void *right = arg;
	const char *right_name = arg;
	int cmp;

	switch (flags & (OBJ_SEARCH_OBJECT | OBJ_SEARCH_KEY | OBJ_SEARCH_PARTIAL_KEY)) {
	default:
	case OBJ_SEARCH_OBJECT:
		right_name = ast_sorcery_object_get_id(right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(left), right_name);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(ast_sorcery_object_get_id(left), right_name, strlen(right_name));
		break;
	}
	return cmp ? 0 : CMP_MATCH;
}

/*!
 * \internal
 * \brief Destructor function for a sorcery memory cache
 *
 * \param obj A sorcery memory cache
 */
static void sorcery_memory_cache_destructor(void *obj)
{
	struct sorcery_memory_cache *cache = obj;

	ast_free(cache->name);
	ao2_cleanup(cache->objects);
}

/*!
 * \internal
 * \brief Callback function to cache an object in a memory cache
 *
 * \param sorcery The sorcery instance
 * \param data The sorcery memory cache
 * \param object The object to cache
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int sorcery_memory_cache_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct sorcery_memory_cache *cache = data;

	/* As there is no guarantee that this won't be called by multiple threads wanting to cache
	 * the same object we remove any old ones, which turns this into a create/update function
	 * in reality. As well since there's no guarantee that the object in the cache is the same
	 * one here we remove any old objects using the object identifier.
	 */

	ao2_wrlock(cache->objects);
	ao2_find(cache->objects, ast_sorcery_object_get_id(object), OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK | OBJ_NOLOCK);
	ao2_link_flags(cache->objects, object, OBJ_NOLOCK);
	ao2_unlock(cache->objects);

	return 0;
}

/*!
 * \internal
 * \brief Callback function to retrieve an object from a memory cache
 *
 * \param sorcery The sorcery instance
 * \param data The sorcery memory cache
 * \param type The type of the object to retrieve
 * \param id The id of the object to retrieve
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static void *sorcery_memory_cache_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id)
{
	struct sorcery_memory_cache *cache = data;

	return ao2_find(cache->objects, id, OBJ_SEARCH_KEY);
}

/*!
 * \internal
 * \brief Callback function to finish configuring the memory cache and to prefetch objects
 *
 * \param data The sorcery memory cache
 * \param sorcery The sorcery instance
 * \param type The type of object being loaded
 */
static void sorcery_memory_cache_load(void *data, const struct ast_sorcery *sorcery, const char *type)
{
	struct sorcery_memory_cache *cache = data;

	/* If no name was explicitly specified generate one given the sorcery instance and object type */
	if (ast_strlen_zero(cache->name)) {
		ast_asprintf(&cache->name, "%s/%s", ast_sorcery_get_module(sorcery), type);
	}

	ao2_link(caches, cache);
	ast_debug(1, "Memory cache '%s' associated with sorcery instance '%p' of module '%s' with object type '%s'\n",
		cache->name, sorcery, ast_sorcery_get_module(sorcery), type);
}

/*!
 * \internal
 * \brief Callback function to expire objects from the memory cache on reload (if configured)
 *
 * \param data The sorcery memory cache
 * \param sorcery The sorcery instance
 * \param type The type of object being reloaded
 */
static void sorcery_memory_cache_reload(void *data, const struct ast_sorcery *sorcery, const char *type)
{
}

/*!
 * \internal
 * \brief Function used to take an unsigned integer based configuration option and parse it
 *
 * \param value The string value of the configuration option
 * \param result The unsigned integer to place the result in
 *
 * \retval 0 failure
 * \retval 1 success
 */
static int configuration_parse_unsigned_integer(const char *value, unsigned int *result)
{
	if (ast_strlen_zero(value) || !strncmp(value, "-", 1)) {
		return 0;
	}

	return sscanf(value, "%30u", result);
}

/*!
 * \internal
 * \brief Callback function to create a new sorcery memory cache using provided configuration
 *
 * \param data A stringified configuration for the memory cache
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static void *sorcery_memory_cache_open(const char *data)
{
	char *options = ast_strdup(data), *option;
	RAII_VAR(struct sorcery_memory_cache *, cache, NULL, ao2_cleanup);

	cache = ao2_alloc_options(sizeof(*cache), sorcery_memory_cache_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cache) {
		return NULL;
	}

	/* If no configuration options have been provided this memory cache will operate in a default
	 * configuration.
	 */
	while (!ast_strlen_zero(options) && (option = strsep(&options, ","))) {
		char *name = strsep(&option, "="), *value = option;

		if (!strcasecmp(name, "name")) {
			if (ast_strlen_zero(value)) {
				ast_log(LOG_ERROR, "A name must be specified for the memory cache\n");
				return NULL;
			}
			ast_free(cache->name);
			cache->name = ast_strdup(value);
		} else if (!strcasecmp(name, "maximum_objects")) {
			if (configuration_parse_unsigned_integer(value, &cache->maximum_objects) != 1) {
				ast_log(LOG_ERROR, "Unsupported maximum objects value of '%s' used for memory cache\n",
					value);
				return NULL;
			}
		} else if (!strcasecmp(name, "object_lifetime_maximum")) {
			if (configuration_parse_unsigned_integer(value, &cache->object_lifetime_maximum) != 1) {
				ast_log(LOG_ERROR, "Unsupported object maximum lifetime value of '%s' used for memory cache\n",
					value);
				return NULL;
			}
		} else if (!strcasecmp(name, "object_lifetime_stale")) {
			if (configuration_parse_unsigned_integer(value, &cache->object_lifetime_stale) != 1) {
				ast_log(LOG_ERROR, "Unsupported object stale lifetime value of '%s' used for memory cache\n",
					value);
				return NULL;
			}
		} else if (!strcasecmp(name, "prefetch")) {
			cache->prefetch = ast_true(value);
		} else if (!strcasecmp(name, "expire_on_reload")) {
			cache->expire_on_reload = ast_true(value);
		} else {
			ast_log(LOG_ERROR, "Unsupported option '%s' used for memory cache\n", name);
			return NULL;
		}
	}

	cache->objects = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK,
		cache->maximum_objects ? cache->maximum_objects : CACHE_CONTAINER_BUCKET_SIZE,
		sorcery_memory_cached_object_hash, sorcery_memory_cached_object_cmp);
	if (!cache->objects) {
		ast_log(LOG_ERROR, "Could not create a container to hold cached objects for memory cache\n");
		return NULL;
	}

	/* The memory cache is not linked to the caches container until the load callback is invoked.
	 * Linking occurs there so an intelligent cache name can be constructed using the module of
	 * the sorcery instance and the specific object type if no cache name was specified as part
	 * of the configuration.
	 */

	/* This is done as RAII_VAR will drop the reference */
	return ao2_bump(cache);
}

/*!
 * \internal
 * \brief Callback function to delete an object from a memory cache
 *
 * \param sorcery The sorcery instance
 * \param data The sorcery memory cache
 * \param object The object to cache
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int sorcery_memory_cache_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct sorcery_memory_cache *cache = data;

	/* There is no guarantee that the object we have cached is the one we will be provided
	 * with in this callback function. As a result of this we remove the cached object based on
	 * the identifier and not the object itself.
	 */
	ao2_find(cache->objects, ast_sorcery_object_get_id(object), OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK);

	return 0;
}

/*!
 * \internal
 * \brief Callback function to terminate a memory cache
 *
 * \param data The sorcery memory cache
 */
static void sorcery_memory_cache_close(void *data)
{
	struct sorcery_memory_cache *cache = data;

	/* This can occur if a cache is created but never loaded */
	if (!ast_strlen_zero(cache->name)) {
		ao2_unlink(caches, cache);
	}

	ao2_ref(cache, -1);
}

#ifdef TEST_FRAMEWORK

/*! \brief Dummy sorcery object */
struct test_sorcery_object {
	SORCERY_OBJECT(details);
};

/*!
 * \internal
 * \brief Allocator for test object
 *
 * \param id The identifier for the object
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static void *test_sorcery_object_alloc(const char *id)
{
	return ast_sorcery_generic_alloc(sizeof(struct test_sorcery_object), NULL);
}

/*!
 * \internal
 * \brief Allocator for test sorcery instance
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static struct ast_sorcery *alloc_and_initialize_sorcery(void)
{
	struct ast_sorcery *sorcery;

	if (!(sorcery = ast_sorcery_open())) {
		return NULL;
	}

	if ((ast_sorcery_apply_default(sorcery, "test", "memory", NULL) != AST_SORCERY_APPLY_SUCCESS) ||
		ast_sorcery_internal_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_sorcery_unref(sorcery);
		return NULL;
	}

	return sorcery;
}

AST_TEST_DEFINE(open_with_valid_options)
{
	int res = AST_TEST_PASS;
	struct sorcery_memory_cache *cache;

	switch (cmd) {
	case TEST_INIT:
		info->name = "open_with_valid_options";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Attempt to create sorcery memory caches using valid options";
		info->description = "This test performs the following:\n"
			"\t* Creates a memory cache with default configuration\n"
			"\t* Creates a memory cache with a maximum object count of 10 and verifies it\n"
			"\t* Creates a memory cache with a maximum object lifetime of 60 and verifies it\n"
			"\t* Creates a memory cache with a stale object lifetime of 90 and verifies it\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = sorcery_memory_cache_open("");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache using default configuration\n");
		res = AST_TEST_FAIL;
	} else {
		sorcery_memory_cache_close(cache);
	}

	cache = sorcery_memory_cache_open("maximum_objects=10");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache with a maximum object count of 10\n");
		res = AST_TEST_FAIL;
	} else {
		if (cache->maximum_objects != 10) {
			ast_test_status_update(test, "Created a sorcery memory cache with a maximum object count of 10 but it has '%u'\n",
				cache->maximum_objects);
		}
		sorcery_memory_cache_close(cache);
	}

	cache = sorcery_memory_cache_open("object_lifetime_maximum=60");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache with a maximum object lifetime of 60\n");
		res = AST_TEST_FAIL;
	} else {
		if (cache->object_lifetime_maximum != 60) {
			ast_test_status_update(test, "Created a sorcery memory cache with a maximum object lifetime of 60 but it has '%u'\n",
				cache->object_lifetime_maximum);
		}
		sorcery_memory_cache_close(cache);
	}

	cache = sorcery_memory_cache_open("object_lifetime_stale=90");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache with a stale object lifetime of 90\n");
		res = AST_TEST_FAIL;
	} else {
		if (cache->object_lifetime_stale != 90) {
			ast_test_status_update(test, "Created a sorcery memory cache with a stale object lifetime of 90 but it has '%u'\n",
				cache->object_lifetime_stale);
		}
		sorcery_memory_cache_close(cache);
	}


	return res;
}

AST_TEST_DEFINE(open_with_invalid_options)
{
	int res = AST_TEST_PASS;
	struct sorcery_memory_cache *cache;

	switch (cmd) {
	case TEST_INIT:
		info->name = "open_with_invalid_options";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Attempt to create sorcery memory caches using invalid options";
		info->description = "This test attempts to perform the following:\n"
			"\t* Create a memory cache with an empty name\n"
			"\t* Create a memory cache with a maximum object count of -1\n"
			"\t* Create a memory cache with a maximum object count of toast\n"
			"\t* Create a memory cache with a maximum object lifetime of -1\n"
			"\t* Create a memory cache with a maximum object lifetime of toast\n"
			"\t* Create a memory cache with a stale object lifetime of -1\n"
			"\t* Create a memory cache with a stale object lifetime of toast\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = sorcery_memory_cache_open("name=");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with an empty name\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	cache = sorcery_memory_cache_open("maximum_objects=-1");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with a maximum object count of -1\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	cache = sorcery_memory_cache_open("maximum_objects=toast");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with a maximum object count of toast\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	cache = sorcery_memory_cache_open("object_lifetime_maximum=-1");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with an object lifetime maximum of -1\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	cache = sorcery_memory_cache_open("object_lifetime_maximum=toast");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with an object lifetime maximum of toast\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	cache = sorcery_memory_cache_open("object_lifetime_stale=-1");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with a stale object lifetime of -1\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	cache = sorcery_memory_cache_open("object_lifetime_stale=toast");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with a stale object lifetime of toast\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	cache = sorcery_memory_cache_open("tacos");
	if (cache) {
		ast_test_status_update(test, "Created a sorcery memory cache with an invalid configuration option 'tacos'\n");
		sorcery_memory_cache_close(cache);
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(create_and_retrieve)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct sorcery_memory_cache *cache = NULL;
	RAII_VAR(void *, object, NULL, ao2_cleanup);
	RAII_VAR(void *, cached_object, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "create";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Attempt to create an object in the cache";
		info->description = "This test performs the following:\n"
			"\t* Creates a memory cache with default options\n"
			"\t* Creates a sorcery instance with a test object\n"
			"\t* Creates a test object with an id of test\n"
			"\t* Pushes the test object into the memory cache\n"
			"\t* Confirms that the test object is in the cache\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = sorcery_memory_cache_open("");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache using default options\n");
		goto cleanup;
	}

	if (ao2_container_count(cache->objects)) {
		ast_test_status_update(test, "Memory cache contains cached objects before we added one\n");
		goto cleanup;
	}

	sorcery = alloc_and_initialize_sorcery();
	if (!sorcery) {
		ast_test_status_update(test, "Failed to create a test sorcery instance\n");
		goto cleanup;
	}

	object = ast_sorcery_alloc(sorcery, "test", "test");
	if (!object) {
		ast_test_status_update(test, "Failed to allocate a test object\n");
		goto cleanup;
	}

	sorcery_memory_cache_create(sorcery, cache, object);

	if (!ao2_container_count(cache->objects)) {
		ast_test_status_update(test, "Added test object to memory cache but cache remains empty\n");
		goto cleanup;
	}

	cached_object = sorcery_memory_cache_retrieve_id(sorcery, cache, "test", "test");
	if (!cached_object) {
		ast_test_status_update(test, "Object placed into memory cache could not be retrieved\n");
		goto cleanup;
	}

	if (cached_object != object) {
		ast_test_status_update(test, "Object retrieved from memory cached is not the one we cached\n");
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	if (cache) {
		sorcery_memory_cache_close(cache);
	}
	if (sorcery) {
		ast_sorcery_unref(sorcery);
	}

	return res;
}

AST_TEST_DEFINE(update)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct sorcery_memory_cache *cache = NULL;
	RAII_VAR(void *, original_object, NULL, ao2_cleanup);
	RAII_VAR(void *, updated_object, NULL, ao2_cleanup);
	RAII_VAR(void *, cached_object, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "create";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Attempt to create and then update an object in the cache";
		info->description = "This test performs the following:\n"
			"\t* Creates a memory cache with default options\n"
			"\t* Creates a sorcery instance with a test object\n"
			"\t* Creates a test object with an id of test\n"
			"\t* Pushes the test object into the memory cache\n"
			"\t* Confirms that the test object is in the cache\n"
			"\t* Creates a new test object with the same id of test\n"
			"\t* Pushes the new test object into the memory cache\n"
			"\t* Confirms that the new test object has replaced the old one\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = sorcery_memory_cache_open("");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache using default options\n");
		goto cleanup;
	}

	if (ao2_container_count(cache->objects)) {
		ast_test_status_update(test, "Memory cache contains cached objects before we added one\n");
		goto cleanup;
	}

	sorcery = alloc_and_initialize_sorcery();
	if (!sorcery) {
		ast_test_status_update(test, "Failed to create a test sorcery instance\n");
		goto cleanup;
	}

	original_object = ast_sorcery_alloc(sorcery, "test", "test");
	if (!original_object) {
		ast_test_status_update(test, "Failed to allocate a test object\n");
		goto cleanup;
	}

	sorcery_memory_cache_create(sorcery, cache, original_object);

	updated_object = ast_sorcery_alloc(sorcery, "test", "test");
	if (!updated_object) {
		ast_test_status_update(test, "Failed to allocate an updated test object\n");
		goto cleanup;
	}

	sorcery_memory_cache_create(sorcery, cache, updated_object);

	if (ao2_container_count(cache->objects) != 1) {
		ast_test_status_update(test, "Added updated test object to memory cache but cache now contains %d objects instead of 1\n",
			ao2_container_count(cache->objects));
		goto cleanup;
	}

	cached_object = sorcery_memory_cache_retrieve_id(sorcery, cache, "test", "test");
	if (!cached_object) {
		ast_test_status_update(test, "Updated object placed into memory cache could not be retrieved\n");
		goto cleanup;
	}

	if (cached_object == original_object) {
		ast_test_status_update(test, "Updated object placed into memory cache but old one is being retrieved\n");
		goto cleanup;
	} else if (cached_object != updated_object) {
		ast_test_status_update(test, "Updated object placed into memory cache but different one is being retrieved\n");
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	if (cache) {
		sorcery_memory_cache_close(cache);
	}
	if (sorcery) {
		ast_sorcery_unref(sorcery);
	}

	return res;
}

AST_TEST_DEFINE(delete)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct sorcery_memory_cache *cache = NULL;
	RAII_VAR(void *, object, NULL, ao2_cleanup);
	RAII_VAR(void *, cached_object, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "delete";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Attempt to create and then delete an object in the cache";
		info->description = "This test performs the following:\n"
			"\t* Creates a memory cache with default options\n"
			"\t* Creates a sorcery instance with a test object\n"
			"\t* Creates a test object with an id of test\n"
			"\t* Pushes the test object into the memory cache\n"
			"\t* Confirms that the test object is in the cache\n"
			"\t* Deletes the test object from the cache\n"
			"\t* Confirms that the test object is no longer in the cache\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = sorcery_memory_cache_open("");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache using default options\n");
		goto cleanup;
	}

	if (ao2_container_count(cache->objects)) {
		ast_test_status_update(test, "Memory cache contains cached objects before we added one\n");
		goto cleanup;
	}

	sorcery = alloc_and_initialize_sorcery();
	if (!sorcery) {
		ast_test_status_update(test, "Failed to create a test sorcery instance\n");
		goto cleanup;
	}

	object = ast_sorcery_alloc(sorcery, "test", "test");
	if (!object) {
		ast_test_status_update(test, "Failed to allocate a test object\n");
		goto cleanup;
	}

	sorcery_memory_cache_create(sorcery, cache, object);

	if (!ao2_container_count(cache->objects)) {
		ast_test_status_update(test, "Added test object to memory cache but cache contains no objects\n");
		goto cleanup;
	}

	cached_object = sorcery_memory_cache_retrieve_id(sorcery, cache, "test", "test");
	if (!cached_object) {
		ast_test_status_update(test, "Test object placed into memory cache could not be retrieved\n");
		goto cleanup;
	}

	ao2_ref(cached_object, -1);
	cached_object = NULL;

	sorcery_memory_cache_delete(sorcery, cache, object);

	cached_object = sorcery_memory_cache_retrieve_id(sorcery, cache, "test", "test");
	if (cached_object) {
		ast_test_status_update(test, "Test object deleted from memory cache can still be retrieved\n");
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	if (cache) {
		sorcery_memory_cache_close(cache);
	}
	if (sorcery) {
		ast_sorcery_unref(sorcery);
	}

	return res;
}

#endif

static int unload_module(void)
{
	if (sched) {
		ast_sched_context_destroy(sched);
		sched = NULL;
	}

	ast_threadpool_shutdown(pool);
	pool = NULL;

	ao2_cleanup(caches);

	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);

	ast_sorcery_wizard_unregister(&memory_cache_object_wizard);

	AST_TEST_UNREGISTER(open_with_valid_options);
	AST_TEST_UNREGISTER(open_with_invalid_options);
	AST_TEST_UNREGISTER(create_and_retrieve);
	AST_TEST_UNREGISTER(update);
	AST_TEST_UNREGISTER(delete);

	return 0;
}

static int load_module(void)
{
	RAII_VAR(struct sorcery_memory_cache_config *, cfg, NULL, ao2_cleanup);
	struct ast_threadpool_options threadpool_opts = { 0, };

	if (aco_info_init(&cfg_info)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	aco_option_register(&cfg_info, "initial_size", ACO_EXACT,
		threadpool_options, "5", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct sorcery_memory_cache_threadpool_conf, initial_size), 0,
		INT_MAX);
	aco_option_register(&cfg_info, "idle_timeout_sec", ACO_EXACT,
		threadpool_options, "20", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct sorcery_memory_cache_threadpool_conf, idle_timeout_sec), 0,
		INT_MAX);
	aco_option_register(&cfg_info, "max_size", ACO_EXACT,
		threadpool_options, "50", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct sorcery_memory_cache_threadpool_conf, max_size), 0,
		INT_MAX);

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		struct sorcery_memory_cache_config *default_cfg = sorcery_memory_cache_config_alloc();

		if (!default_cfg) {
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}

		if (aco_set_defaults(&threadpool_option, "threadpool", default_cfg->threadpool_options)) {
			ast_log(LOG_ERROR, "Failed to initialize defaults on res_sorcery_memory_cache configuration object\n");
			ao2_ref(default_cfg, -1);
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}

		ast_log(LOG_NOTICE, "Could not load res_sorcery_memory_cache configuration; using defaults\n");
		ao2_global_obj_replace_unref(globals, default_cfg);
		cfg = default_cfg;
	} else {
		cfg = ao2_global_obj_ref(globals);
		if (!cfg) {
			ast_log(LOG_ERROR, "Failed to obtain res_sorcery_memory_cache configuration object\n");
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	threadpool_opts.version = AST_THREADPOOL_OPTIONS_VERSION;
	threadpool_opts.initial_size = cfg->threadpool_options->initial_size;
	threadpool_opts.auto_increment = 1;
	threadpool_opts.max_size = cfg->threadpool_options->max_size;
	threadpool_opts.idle_timeout = cfg->threadpool_options->idle_timeout_sec;
	pool = ast_threadpool_create("res_sorcery_memory_cache", NULL, &threadpool_opts);
	if (!pool) {
		ast_log(LOG_ERROR, "Failed to create 'res_sorcery_memory_cache' threadpool\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	sched = ast_sched_context_create();
	if (!sched) {
		ast_log(LOG_ERROR, "Failed to create scheduler for cache management\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Failed to create scheduler thread for cache management\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	caches = ao2_container_alloc(CACHES_CONTAINER_BUCKET_SIZE, sorcery_memory_cache_hash,
		sorcery_memory_cache_cmp);
	if (!caches) {
		ast_log(LOG_ERROR, "Failed to create container for configured caches\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sorcery_wizard_register(&memory_cache_object_wizard)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(open_with_valid_options);
	AST_TEST_REGISTER(open_with_invalid_options);
	AST_TEST_REGISTER(create_and_retrieve);
	AST_TEST_REGISTER(update);
	AST_TEST_REGISTER(delete);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery Memory Cache Object Wizard",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
