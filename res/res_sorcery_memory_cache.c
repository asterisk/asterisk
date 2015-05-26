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
#include "asterisk/sched.h"
#include "asterisk/test.h"
#include "asterisk/heap.h"

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
	/*! \brief scheduler id of stale update task */
	int stale_update_sched_id;
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

/*! \brief Scheduler for cache management */
static struct ast_sched_context *sched;

#define STALE_UPDATE_THREAD_ID 0x5EED1E55
AST_THREADSTORAGE(stale_update_id_storage);

static int is_stale_update(void)
{
	uint32_t *stale_update_thread_id;

	stale_update_thread_id = ast_threadstorage_get(&stale_update_id_storage,
		sizeof(*stale_update_thread_id));
	if (!stale_update_thread_id) {
		return 0;
	}

	return *stale_update_thread_id == STALE_UPDATE_THREAD_ID;
}

static void start_stale_update(void)
{
	uint32_t *stale_update_thread_id;

	stale_update_thread_id = ast_threadstorage_get(&stale_update_id_storage,
		sizeof(*stale_update_thread_id));
	if (!stale_update_thread_id) {
		ast_log(LOG_ERROR, "Could not set stale update ID for sorcery memory cache thread\n");
		return;
	}

	*stale_update_thread_id = STALE_UPDATE_THREAD_ID;
}

static void end_stale_update(void)
{
	uint32_t *stale_update_thread_id;

	stale_update_thread_id = ast_threadstorage_get(&stale_update_id_storage,
		sizeof(*stale_update_thread_id));
	if (!stale_update_thread_id) {
		ast_log(LOG_ERROR, "Could not set stale update ID for sorcery memory cache thread\n");
		return;
	}

	*stale_update_thread_id = 0;
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
	if (cache->object_heap) {
		ast_heap_destroy(cache->object_heap);
	}
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

	cached = ao2_alloc(sizeof(*cached), sorcery_memory_cached_object_destructor);
	if (!cached) {
		return -1;
	}
	cached->object = ao2_bump(object);
	cached->created = ast_tvnow();
	cached->stale_update_sched_id = -1;

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

struct stale_update_task_data {
	struct ast_sorcery *sorcery;
	struct sorcery_memory_cache *cache;
	void *object;
};

static void stale_update_task_data_destructor(void *obj)
{
	struct stale_update_task_data *task_data = obj;

	ao2_cleanup(task_data->cache);
	ao2_cleanup(task_data->object);
	ast_sorcery_unref(task_data->sorcery);
}

static struct stale_update_task_data *stale_update_task_data_alloc(struct ast_sorcery *sorcery,
		struct sorcery_memory_cache *cache, const char *type, void *object)
{
	struct stale_update_task_data *task_data;

	task_data = ao2_alloc_options(sizeof(*task_data) + strlen(type) + 1,
		stale_update_task_data_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!task_data) {
		return NULL;
	}

	task_data->sorcery = ao2_bump(sorcery);
	task_data->cache = ao2_bump(cache);
	task_data->object = ao2_bump(object);

	return task_data;
}

static int stale_item_update(const void *data)
{
	struct stale_update_task_data *task_data = (struct stale_update_task_data *) data;
	void *object;

	start_stale_update();

	object = ast_sorcery_retrieve_by_id(task_data->sorcery,
		ast_sorcery_object_get_type(task_data->object),
		ast_sorcery_object_get_id(task_data->object));
	if (!object) {
		ast_debug(1, "Backend no longer has object type '%s' ID '%s'. Removing from cache\n",
			ast_sorcery_object_get_type(task_data->object),
			ast_sorcery_object_get_id(task_data->object));
		sorcery_memory_cache_delete(task_data->sorcery, task_data->cache,
			task_data->object);
	} else {
		ast_debug(1, "Refreshing stale cache object type '%s' ID '%s'\n",
			ast_sorcery_object_get_type(task_data->object),
			ast_sorcery_object_get_id(task_data->object));
		sorcery_memory_cache_create(task_data->sorcery, task_data->cache,
			object);
	}

	ao2_ref(task_data, -1);
	end_stale_update();

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

	if (is_stale_update()) {
		return NULL;
	}

	cached = ao2_find(cache->objects, id, OBJ_SEARCH_KEY);
	if (!cached) {
		return NULL;
	}

	if (cache->object_lifetime_stale) {
		struct timeval elapsed;

		elapsed = ast_tvsub(ast_tvnow(), cached->created);
		if (elapsed.tv_sec > cache->object_lifetime_stale) {
			ao2_lock(cached);
			if (cached->stale_update_sched_id == -1) {
				struct stale_update_task_data *task_data;

				task_data = stale_update_task_data_alloc((struct ast_sorcery *)sorcery, cache,
					type, cached->object);
				if (task_data) {
					ast_debug(1, "Cached sorcery object type '%s' ID '%s' is stale. Refreshing\n",
						type, id);
					cached->stale_update_sched_id = ast_sched_add(sched, 1, stale_item_update, task_data);
				} else {
					ast_log(LOG_ERROR, "Unable to update stale cached object type '%s', ID '%s'.\n",
						type, id);
				}
			}
			ao2_unlock(cached);
		}
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

/*!
 * \brief Backend data that the mock sorcery wizard uses to create objects
 */
static struct backend_data {
	/*! An arbitrary data field */
	int salt;
	/*! Another arbitrary data field */
	int pepper;
	/*! Indicates whether the backend has data */
	int exists;
} *real_backend_data;

/*!
 * \brief Sorcery object created based on backend data
 */
struct test_data {
	SORCERY_OBJECT(details);
	/*! Mirrors the backend data's salt field */
	int salt;
	/*! Mirrors the backend data's pepper field */
	int pepper;
};

/*!
 * \brief Allocation callback for test_data sorcery object
 */
static void *test_data_alloc(const char *id) {
	return ast_sorcery_generic_alloc(sizeof(struct test_data), NULL);
}

/*!
 * \brief Callback for retrieving sorcery object by ID
 *
 * The mock wizard uses the \ref real_backend_data in order to construct
 * objects. If the backend data is "nonexisent" then no object is returned.
 * Otherwise, an object is created that has the backend data's salt and
 * pepper values copied.
 *
 * \param sorcery The sorcery instance
 * \param data Unused
 * \param type The object type. Will always be "test".
 * \param id The object id. Will always be "test".
 *
 * \retval NULL Backend data does not exist
 * \retval non-NULL An object representing the backend data
 */
static void *mock_retrieve_id(const struct ast_sorcery *sorcery, void *data,
		const char *type, const char *id)
{
	struct test_data *b_data;

	if (!real_backend_data->exists) {
		return NULL;
	}

	b_data = ast_sorcery_alloc(sorcery, type, id);
	if (!b_data) {
		return NULL;
	}

	b_data->salt = real_backend_data->salt;
	b_data->pepper = real_backend_data->pepper;
	return b_data;
}

/*!
 * \brief A mock sorcery wizard used for the stale test
 */
static struct ast_sorcery_wizard mock_wizard = {
	.name = "mock",
	.retrieve_id = mock_retrieve_id,
};

/*!
 * \brief Wait for the cache to be updated after a stale object is retrieved.
 *
 * Since the cache does not know what type of objects it is dealing with, and
 * since we do not have the internals of the cache, the only way to make this
 * determination is to continuously retrieve an object from the cache until
 * we retrieve a different object than we had previously retrieved.
 *
 * \param sorcery The sorcery instance
 * \param previous_object The object we had previously retrieved from the cache
 * \param[out] new_object The new object we retrieve from the cache
 *
 * \retval 0 Successfully retrieved a new object from the cache
 * \retval non-zero Failed to retrieve a new object from the cache
 */
static int wait_for_cache_update(const struct ast_sorcery *sorcery,
		void *previous_object, struct test_data **new_object)
{
	struct timeval start = ast_tvnow();

	while (ast_remaining_ms(start, 5000) > 0) {
		void *object;

		object = ast_sorcery_retrieve_by_id(sorcery, "test", "test");
		if (object != previous_object) {
			*new_object = object;
			return 0;
		}
		ao2_cleanup(object);
	}

	return -1;
}

AST_TEST_DEFINE(stale)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct test_data *backend_object;
	struct backend_data iterations[] = {
		{ .salt = 1,      .pepper = 2,       .exists = 1 },
		{ .salt = 568729, .pepper = -234123, .exists = 1 },
		{ .salt = 0,      .pepper = 0,       .exists = 0 },
	};
	struct backend_data initial = {
		.salt = 0,
		.pepper = 0,
		.exists = 1,
	};
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stale";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Ensure that stale objects are replaced with updated objects";
		info->description = "This test performs the following:\n"
			"\t* Create a sorcery instance with two wizards"
			"\t\t* The first is a memory cache that marks items stale after 3 seconds\n"
			"\t\t* The second is a mock of a back-end\n"
			"\t* Pre-populates the cache by retrieving some initial data from the backend.\n"
			"\t* Performs iterations of the following:\n"
			"\t\t* Update backend data with new values\n"
			"\t\t* Retrieve item from the cache\n"
			"\t\t* Ensure the retrieved item does not have the new backend values\n"
			"\t\t* Wait for cached object to become stale\n"
			"\t\t* Retrieve the stale cached object\n"
			"\t\t* Ensure that the stale object retrieved is the same as the fresh one from earlier\n"
			"\t\t* Wait for the cache to update with new data\n"
			"\t\t* Ensure that new data in the cache matches backend data\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_sorcery_wizard_register(&mock_wizard);

	sorcery = ast_sorcery_open();
	if (!sorcery) {
		ast_test_status_update(test, "Failed to create sorcery instance\n");
		goto cleanup;
	}

	ast_sorcery_apply_wizard_mapping(sorcery, "test", "memory_cache",
			"object_lifetime_stale=3", 1);
	ast_sorcery_apply_wizard_mapping(sorcery, "test", "mock", NULL, 0);
	ast_sorcery_internal_object_register(sorcery, "test", test_data_alloc, NULL, NULL);

	/* Prepopulate the cache */
	real_backend_data = &initial;

	backend_object = ast_sorcery_retrieve_by_id(sorcery, "test", "test");
	if (!backend_object) {
		ast_test_status_update(test, "Unable to retrieve backend data and populate the cache\n");
		goto cleanup;
	}
	ao2_ref(backend_object, -1);

	for (i = 0; i < ARRAY_LEN(iterations); ++i) {
		RAII_VAR(struct test_data *, cache_fresh, NULL, ao2_cleanup);
		RAII_VAR(struct test_data *, cache_stale, NULL, ao2_cleanup);
		RAII_VAR(struct test_data *, cache_new, NULL, ao2_cleanup);

		real_backend_data = &iterations[i];

		ast_test_status_update(test, "Begininning iteration %d\n", i);

		cache_fresh = ast_sorcery_retrieve_by_id(sorcery, "test", "test");
		if (!cache_fresh) {
			ast_test_status_update(test, "Unable to retrieve fresh cached object\n");
			goto cleanup;
		}

		if (cache_fresh->salt == iterations[i].salt || cache_fresh->pepper == iterations[i].pepper) {
			ast_test_status_update(test, "Fresh cached object has unexpected values. Did we hit the backend?\n");
			goto cleanup;
		}

		sleep(5);

		cache_stale = ast_sorcery_retrieve_by_id(sorcery, "test", "test");
		if (!cache_stale) {
			ast_test_status_update(test, "Unable to retrieve stale cached object\n");
			goto cleanup;
		}

		if (cache_stale != cache_fresh) {
			ast_test_status_update(test, "Stale cache hit retrieved different object than fresh cache hit\n");
			goto cleanup;
		}

		if (wait_for_cache_update(sorcery, cache_stale, &cache_new)) {
			ast_test_status_update(test, "Cache was not updated\n");
			goto cleanup;
		}

		if (iterations[i].exists) {
			if (!cache_new) {
				ast_test_status_update(test, "Failed to retrieve item from cache when there should be one present\n");
				goto cleanup;
			} else if (cache_new->salt != iterations[i].salt ||
					cache_new->pepper != iterations[i].pepper) {
				ast_test_status_update(test, "New cached item has unexpected values\n");
				goto cleanup;
			}
		} else if (cache_new) {
			ast_test_status_update(test, "Retrieved a cached item when there should not have been one present\n");
			goto cleanup;
		}
	}

	res = AST_TEST_PASS;

cleanup:
	if (sorcery) {
		ast_sorcery_unref(sorcery);
	}
	ast_sorcery_wizard_unregister(&mock_wizard);
	return res;
}

#endif

static int unload_module(void)
{
	if (sched) {
		ast_sched_context_destroy(sched);
		sched = NULL;
	}

	ao2_cleanup(caches);

	ast_sorcery_wizard_unregister(&memory_cache_object_wizard);

	AST_TEST_UNREGISTER(open_with_valid_options);
	AST_TEST_UNREGISTER(open_with_invalid_options);
	AST_TEST_UNREGISTER(create_and_retrieve);
	AST_TEST_UNREGISTER(update);
	AST_TEST_UNREGISTER(delete);
	AST_TEST_UNREGISTER(maximum_objects);
	AST_TEST_UNREGISTER(expiration);
	AST_TEST_UNREGISTER(stale);

	return 0;
}

static int load_module(void)
{
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
	AST_TEST_REGISTER(stale);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery Memory Cache Object Wizard",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
