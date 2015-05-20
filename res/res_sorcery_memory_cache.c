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
#include "asterisk/heap.h"

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
	/*! \brief Heap of cached objects. Oldest object is at the top. */
	struct ast_heap *object_heap;
	/*! \brief Scheduler item for expiring oldest object. */
	int expire_id;
#ifdef TEST_FRAMEWORK
	/*! \brief Variable used to indicate we should notify a test when we reach empty */
	unsigned int cache_notify;
	/*! \brief Mutex lock used for signaling when the cache has reached empty */
	ast_mutex_t lock;
	/*! \brief Condition used for signaling when the cache has reached empty */
	ast_cond_t cond;
	/*! \brief Variable that is set when the cache has reached empty */
	unsigned int cache_completed;
#endif
};

/*! \brief Structure for stored a cached object */
struct sorcery_memory_cached_object {
	/*! \brief The cached object */
	void *object;
	/*! \brief The time at which the object was created */
	struct timeval created;
	/*! \brief index required by heap */
	ssize_t __heap_index;
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

/*! \brief Height of heap for cache object heap. Allows 31 initial objects */
#define CACHE_HEAP_INIT_HEIGHT 5

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
	const struct sorcery_memory_cached_object *cached = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_SEARCH_OBJECT | OBJ_SEARCH_KEY | OBJ_SEARCH_PARTIAL_KEY)) {
	default:
	case OBJ_SEARCH_OBJECT:
		name = ast_sorcery_object_get_id(cached->object);
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
	struct sorcery_memory_cached_object *left = obj;
	struct sorcery_memory_cached_object *right = arg;
	const char *right_name = arg;
	int cmp;

	switch (flags & (OBJ_SEARCH_OBJECT | OBJ_SEARCH_KEY | OBJ_SEARCH_PARTIAL_KEY)) {
	default:
	case OBJ_SEARCH_OBJECT:
		right_name = ast_sorcery_object_get_id(right->object);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(left->object), right_name);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(ast_sorcery_object_get_id(left->object), right_name, strlen(right_name));
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
	ast_heap_destroy(cache->object_heap);
}

/*!
 * \internal
 * \brief Destructor function for sorcery memory cached objects
 *
 * \param obj A sorcery memory cached object
 */
static void sorcery_memory_cached_object_destructor(void *obj)
{
	struct sorcery_memory_cached_object *cached = obj;

	ao2_cleanup(cached->object);
}

static int schedule_cache_expiration(struct sorcery_memory_cache *cache);

/*!
 * \internal
 * \brief Remove an object from the cache.
 *
 * This removes the item from both the hashtable and the heap.
 *
 * \pre cache->objects is write-locked
 *
 * \param cache The cache from which the object is being removed.
 * \param id The sorcery object id of the object to remove.
 * \param reschedule Reschedule cache expiration if this was the oldest object.
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int remove_from_cache(struct sorcery_memory_cache *cache, const char *id, int reschedule)
{
	struct sorcery_memory_cached_object *hash_object;
	struct sorcery_memory_cached_object *oldest_object;
	struct sorcery_memory_cached_object *heap_object;

	hash_object = ao2_find(cache->objects, id,
		OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NOLOCK);
	if (!hash_object) {
		return -1;
	}
	oldest_object = ast_heap_peek(cache->object_heap, 1);
	heap_object = ast_heap_remove(cache->object_heap, hash_object);

	ast_assert(heap_object == hash_object);

	ao2_ref(hash_object, -1);

	if (reschedule && (oldest_object == heap_object)) {
		schedule_cache_expiration(cache);
	}

	return 0;
}

/*!
 * \internal
 * \brief Scheduler callback invoked to expire old objects
 *
 * \param data The opaque callback data (in our case, the memory cache)
 */
static int expire_objects_from_cache(const void *data)
{
	struct sorcery_memory_cache *cache = (struct sorcery_memory_cache *)data;
	struct sorcery_memory_cached_object *cached;

	ao2_wrlock(cache->objects);

	cache->expire_id = -1;

	/* This is an optimization for objects which have been cached close to eachother */
	while ((cached = ast_heap_peek(cache->object_heap, 1))) {
		int expiration;

		expiration = ast_tvdiff_ms(ast_tvadd(cached->created, ast_samp2tv(cache->object_lifetime_maximum, 1)), ast_tvnow());

		/* If the current oldest object has not yet expired stop and reschedule for it */
		if (expiration > 0) {
			break;
		}

		remove_from_cache(cache, ast_sorcery_object_get_id(cached->object), 0);
	}

	schedule_cache_expiration(cache);

	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	return 0;
}

/*!
 * \internal
 * \brief Schedule a callback for cached object expiration.
 *
 * \pre cache->objects is write-locked
 *
 * \param cache The cache that is having its callback scheduled.
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int schedule_cache_expiration(struct sorcery_memory_cache *cache)
{
	struct sorcery_memory_cached_object *cached;
	int expiration = 0;

	if (!cache->object_lifetime_maximum) {
		return 0;
	}

	if (cache->expire_id != -1) {
		/* If we can't unschedule this expiration then it is currently attempting to run,
		 * so let it run - it just means that it'll be the one scheduling instead of us.
		 */
		if (ast_sched_del(sched, cache->expire_id)) {
			return 0;
		}

		/* Since it successfully cancelled we need to drop the ref to the cache it had */
		ao2_ref(cache, -1);
		cache->expire_id = -1;
	}

	cached = ast_heap_peek(cache->object_heap, 1);
	if (!cached) {
#ifdef TEST_FRAMEWORK
		ast_mutex_lock(&cache->lock);
		cache->cache_completed = 1;
		ast_cond_signal(&cache->cond);
		ast_mutex_unlock(&cache->lock);
#endif
		return 0;
	}

	expiration = MAX(ast_tvdiff_ms(ast_tvadd(cached->created, ast_samp2tv(cache->object_lifetime_maximum, 1)), ast_tvnow()),
		1);

	cache->expire_id = ast_sched_add(sched, expiration, expire_objects_from_cache, ao2_bump(cache));
	if (cache->expire_id < 0) {
		ao2_ref(cache, -1);
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Remove the oldest item from the cache.
 *
 * \pre cache->objects is write-locked
 *
 * \param cache The cache from which to remove the oldest object
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int remove_oldest_from_cache(struct sorcery_memory_cache *cache)
{
	struct sorcery_memory_cached_object *heap_old_object;
	struct sorcery_memory_cached_object *hash_old_object;

	heap_old_object = ast_heap_pop(cache->object_heap);
	if (!heap_old_object) {
		return -1;
	}
	hash_old_object = ao2_find(cache->objects, heap_old_object,
		OBJ_SEARCH_OBJECT | OBJ_UNLINK | OBJ_NOLOCK);

	ast_assert(heap_old_object == hash_old_object);

	ao2_ref(hash_old_object, -1);

	schedule_cache_expiration(cache);

	return 0;
}

/*!
 * \internal
 * \brief Add a new object to the cache.
 *
 * \pre cache->objects is write-locked
 *
 * \param cache The cache in which to add the new object
 * \param cached_object The object to add to the cache
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int add_to_cache(struct sorcery_memory_cache *cache,
		struct sorcery_memory_cached_object *cached_object)
{
	if (!ao2_link_flags(cache->objects, cached_object, OBJ_NOLOCK)) {
		return -1;
	}

	if (ast_heap_push(cache->object_heap, cached_object)) {
		ao2_find(cache->objects, cached_object,
			OBJ_SEARCH_OBJECT | OBJ_UNLINK | OBJ_NODATA | OBJ_NOLOCK);
		return -1;
	}

	if (cache->expire_id == -1) {
		schedule_cache_expiration(cache);
	}

	return 0;
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
	struct sorcery_memory_cached_object *cached;

	cached = ao2_alloc_options(sizeof(*cached), sorcery_memory_cached_object_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cached) {
		return -1;
	}
	cached->object = ao2_bump(object);
	cached->created = ast_tvnow();

	/* As there is no guarantee that this won't be called by multiple threads wanting to cache
	 * the same object we remove any old ones, which turns this into a create/update function
	 * in reality. As well since there's no guarantee that the object in the cache is the same
	 * one here we remove any old objects using the object identifier.
	 */

	ao2_wrlock(cache->objects);
	remove_from_cache(cache, ast_sorcery_object_get_id(object), 1);
	if (cache->maximum_objects && ao2_container_count(cache->objects) >= cache->maximum_objects) {
		if (remove_oldest_from_cache(cache)) {
			ast_log(LOG_ERROR, "Unable to make room in cache for sorcery object '%s'.\n",
				ast_sorcery_object_get_id(object));
			ao2_ref(cached, -1);
			ao2_unlock(cache->objects);
			return -1;
		}
	}
	if (add_to_cache(cache, cached)) {
		ast_log(LOG_ERROR, "Unable to add object '%s' to the cache\n",
			ast_sorcery_object_get_id(object));
		ao2_ref(cached, -1);
		ao2_unlock(cache->objects);
		return -1;
	}
	ao2_unlock(cache->objects);

	ao2_ref(cached, -1);
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
	struct sorcery_memory_cached_object *cached;
	void *object;

	cached = ao2_find(cache->objects, id, OBJ_SEARCH_KEY);
	if (!cached) {
		return NULL;
	}

	object = ao2_bump(cached->object);
	ao2_ref(cached, -1);

	return object;
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

static int age_cmp(void *a, void *b)
{
	return ast_tvcmp(((struct sorcery_memory_cached_object *) b)->created,
			((struct sorcery_memory_cached_object *) a)->created);
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

	cache->expire_id = -1;

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

	cache->object_heap = ast_heap_create(CACHE_HEAP_INIT_HEIGHT, age_cmp,
		offsetof(struct sorcery_memory_cached_object, __heap_index));
	if (!cache->object_heap) {
		ast_log(LOG_ERROR, "Could not create heap to hold cached objects\n");
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
	int res;

	ao2_wrlock(cache->objects);
	res = remove_from_cache(cache, ast_sorcery_object_get_id(object), 1);
	ao2_unlock(cache->objects);

	if (res) {
		ast_log(LOG_ERROR, "Unable to delete object '%s' from sorcery cache\n", ast_sorcery_object_get_id(object));
	}

	return res;
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

	if (cache->object_lifetime_maximum) {
		/* If object lifetime support is enabled we need to explicitly drop all cached objects here
		 * and stop the scheduled task. Failure to do so could potentially keep the cache around for
		 * a prolonged period of time.
		 */
		ao2_wrlock(cache->objects);
		ao2_callback(cache->objects, OBJ_UNLINK | OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE,
			NULL, NULL);
		AST_SCHED_DEL_UNREF(sched, cache->expire_id, ao2_ref(cache, -1));
		ao2_unlock(cache->objects);
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

static int check_cache_content(struct ast_test *test, struct ast_sorcery *sorcery, struct sorcery_memory_cache *cache,
		const char **in_cache, size_t num_in_cache, const char **not_in_cache, size_t num_not_in_cache)
{
	int i;
	int res = 0;
	RAII_VAR(void *, cached_object, NULL, ao2_cleanup);

	for (i = 0; i < num_in_cache; ++i) {
		cached_object = sorcery_memory_cache_retrieve_id(sorcery, cache, "test", in_cache[i]);
		if (!cached_object) {
			ast_test_status_update(test, "Failed to retrieve '%s' object from the cache\n",
					in_cache[i]);
			res = -1;
		}
		ao2_ref(cached_object, -1);
	}

	for (i = 0; i < num_not_in_cache; ++i) {
		cached_object = sorcery_memory_cache_retrieve_id(sorcery, cache, "test", not_in_cache[i]);
		if (cached_object) {
			ast_test_status_update(test, "Retrieved '%s' object from the cache unexpectedly\n",
					not_in_cache[i]);
			ao2_ref(cached_object, -1);
			res = -1;
		}
	}

	return res;
}

AST_TEST_DEFINE(maximum_objects)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct sorcery_memory_cache *cache = NULL;
	RAII_VAR(void *, alice, NULL, ao2_cleanup);
	RAII_VAR(void *, bob, NULL, ao2_cleanup);
	RAII_VAR(void *, charlie, NULL, ao2_cleanup);
	RAII_VAR(void *, cached_object, NULL, ao2_cleanup);
	const char *in_cache[2];
	const char *not_in_cache[2];

	switch (cmd) {
	case TEST_INIT:
		info->name = "maximum_objects";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Ensure that the 'maximum_objects' option works as expected";
		info->description = "This test performs the following:\n"
			"\t* Creates a memory cache with maximum_objects=2\n"
			"\t* Creates a sorcery instance\n"
			"\t* Creates a three test objects: alice, bob, charlie, and david\n"
			"\t* Pushes alice and bob into the memory cache\n"
			"\t* Confirms that alice and bob are in the memory cache\n"
			"\t* Pushes charlie into the memory cache\n"
			"\t* Confirms that bob and charlie are in the memory cache\n"
			"\t* Deletes charlie from the memory cache\n"
			"\t* Confirms that only bob is in the memory cache\n"
			"\t* Pushes alice into the memory cache\n"
			"\t* Confirms that bob and alice are in the memory cache\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = sorcery_memory_cache_open("maximum_objects=2");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache with maximum_objects=2\n");
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

	alice = ast_sorcery_alloc(sorcery, "test", "alice");
	bob = ast_sorcery_alloc(sorcery, "test", "bob");
	charlie = ast_sorcery_alloc(sorcery, "test", "charlie");

	if (!alice || !bob || !charlie) {
		ast_test_status_update(test, "Failed to allocate sorcery object(s)\n");
		goto cleanup;
	}

	sorcery_memory_cache_create(sorcery, cache, alice);
	in_cache[0] = "alice";
	in_cache[1] = NULL;
	not_in_cache[0] = "bob";
	not_in_cache[1] = "charlie";
	if (check_cache_content(test, sorcery, cache, in_cache, 1, not_in_cache, 2)) {
		goto cleanup;
	}

	/* Delays are added to ensure that we are not adding cache entries within the
	 * same microsecond
	 */
	usleep(1000);

	sorcery_memory_cache_create(sorcery, cache, bob);
	in_cache[0] = "alice";
	in_cache[1] = "bob";
	not_in_cache[0] = "charlie";
	not_in_cache[1] = NULL;
	if (check_cache_content(test, sorcery, cache, in_cache, 2, not_in_cache, 1)) {
		goto cleanup;
	}

	usleep(1000);

	sorcery_memory_cache_create(sorcery, cache, charlie);
	in_cache[0] = "bob";
	in_cache[1] = "charlie";
	not_in_cache[0] = "alice";
	not_in_cache[1] = NULL;
	if (check_cache_content(test, sorcery, cache, in_cache, 2, not_in_cache, 1)) {
		goto cleanup;
	}
	usleep(1000);

	sorcery_memory_cache_delete(sorcery, cache, charlie);
	in_cache[0] = "bob";
	in_cache[1] = NULL;
	not_in_cache[0] = "alice";
	not_in_cache[1] = "charlie";
	if (check_cache_content(test, sorcery, cache, in_cache, 1, not_in_cache, 2)) {
		goto cleanup;
	}
	usleep(1000);

	sorcery_memory_cache_create(sorcery, cache, alice);
	in_cache[0] = "bob";
	in_cache[1] = "alice";
	not_in_cache[0] = "charlie";
	not_in_cache[1] = NULL;
	if (check_cache_content(test, sorcery, cache, in_cache, 2, not_in_cache, 1)) {
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

AST_TEST_DEFINE(expiration)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct sorcery_memory_cache *cache = NULL;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "expiration";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Add objects to a cache configured with maximum lifetime, confirm they are removed";
		info->description = "This test performs the following:\n"
			"\t* Creates a memory cache with a maximum object lifetime of 5 seconds\n"
			"\t* Pushes 10 objects into the memory cache\n"
			"\t* Waits (up to) 10 seconds for expiration to occur\n"
			"\t* Confirms that the objects have been removed from the cache\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cache = sorcery_memory_cache_open("object_lifetime_maximum=5");
	if (!cache) {
		ast_test_status_update(test, "Failed to create a sorcery memory cache using default options\n");
		goto cleanup;
	}

	sorcery = alloc_and_initialize_sorcery();
	if (!sorcery) {
		ast_test_status_update(test, "Failed to create a test sorcery instance\n");
		goto cleanup;
	}

	cache->cache_notify = 1;
	ast_mutex_init(&cache->lock);
	ast_cond_init(&cache->cond, NULL);

	for (i = 0; i < 5; ++i) {
		char uuid[AST_UUID_STR_LEN];
		void *object;

		object = ast_sorcery_alloc(sorcery, "test", ast_uuid_generate_str(uuid, sizeof(uuid)));
		if (!object) {
			ast_test_status_update(test, "Failed to allocate test object for expiration\n");
			goto cleanup;
		}

		sorcery_memory_cache_create(sorcery, cache, object);

		ao2_ref(object, -1);
	}

	ast_mutex_lock(&cache->lock);
	while (!cache->cache_completed) {
		struct timeval start = ast_tvnow();
		struct timespec end = {
			.tv_sec = start.tv_sec + 10,
			.tv_nsec = start.tv_usec * 1000,
		};

		if (ast_cond_timedwait(&cache->cond, &cache->lock, &end) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&cache->lock);

	if (ao2_container_count(cache->objects)) {
		ast_test_status_update(test, "Objects placed into the memory cache did not expire and get removed\n");
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	if (cache) {
		if (cache->cache_notify) {
			ast_cond_destroy(&cache->cond);
			ast_mutex_destroy(&cache->lock);
		}
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
	AST_TEST_UNREGISTER(maximum_objects);
	AST_TEST_UNREGISTER(expiration);

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
		threadpool_options, "10", OPT_INT_T, PARSE_IN_RANGE,
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
	AST_TEST_REGISTER(maximum_objects);
	AST_TEST_REGISTER(expiration);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery Memory Cache Object Wizard",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
