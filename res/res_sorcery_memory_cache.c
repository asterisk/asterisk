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

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astobj2.h"
#include "asterisk/sched.h"
#include "asterisk/test.h"
#include "asterisk/heap.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"

/*** DOCUMENTATION
	<manager name="SorceryMemoryCacheExpireObject" language="en_US">
		<synopsis>
			Expire (remove) an object from a sorcery memory cache.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Cache" required="true">
				<para>The name of the cache to expire the object from.</para>
			</parameter>
			<parameter name="Object" required="true">
				<para>The name of the object to expire.</para>
			</parameter>
		</syntax>
		<description>
			<para>Expires (removes) an object from a sorcery memory cache. If full backend caching is enabled
			this action is not available and will fail. In this case the SorceryMemoryCachePopulate or
			SorceryMemoryCacheExpire AMI actions must be used instead.</para>
		</description>
	</manager>
	<manager name="SorceryMemoryCacheExpire" language="en_US">
		<synopsis>
			Expire (remove) ALL objects from a sorcery memory cache.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Cache" required="true">
				<para>The name of the cache to expire all objects from.</para>
			</parameter>
		</syntax>
		<description>
			<para>Expires (removes) ALL objects from a sorcery memory cache.</para>
		</description>
	</manager>
	<manager name="SorceryMemoryCacheStaleObject" language="en_US">
		<synopsis>
			Mark an object in a sorcery memory cache as stale.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Cache" required="true">
				<para>The name of the cache to mark the object as stale in.</para>
			</parameter>
			<parameter name="Object" required="true">
				<para>The name of the object to mark as stale.</para>
			</parameter>
			<parameter name="Reload" required="false">
				<para>If true, then immediately reload the object from the backend cache instead of waiting for the next retrieval</para>
			</parameter>
		</syntax>
		<description>
			<para>Marks an object as stale within a sorcery memory cache.</para>
		</description>
	</manager>
	<manager name="SorceryMemoryCacheStale" language="en_US">
		<synopsis>
			Marks ALL objects in a sorcery memory cache as stale.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Cache" required="true">
				<para>The name of the cache to mark all object as stale in.</para>
			</parameter>
		</syntax>
		<description>
			<para>Marks ALL objects in a sorcery memory cache as stale.</para>
		</description>
	</manager>
	<manager name="SorceryMemoryCachePopulate" language="en_US">
		<synopsis>
			Expire all objects from a memory cache and populate it with all objects from the backend.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Cache" required="true">
				<para>The name of the cache to populate.</para>
			</parameter>
		</syntax>
		<description>
			<para>Expires all objects from a memory cache and populate it with all objects from the backend.</para>
		</description>
	</manager>
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
	/*! \brief Whether all objects are expired when the object type is reloaded, 0 if disabled */
	unsigned int expire_on_reload;
	/*! \brief Whether this is a cache of the entire backend, 0 if disabled */
	unsigned int full_backend_cache;
	/*! \brief Heap of cached objects. Oldest object is at the top. */
	struct ast_heap *object_heap;
	/*! \brief Scheduler item for expiring oldest object. */
	int expire_id;
	/*! \brief scheduler id of stale update task */
	int stale_update_sched_id;
	/*! \brief An unreffed pointer to the sorcery instance, accessible only with lock held */
	const struct ast_sorcery *sorcery;
	/*! \brief The type of object we are caching */
	char *object_type;
	/*! TRUE if trying to stop the oldest object expiration scheduler item. */
	unsigned int del_expire:1;
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
	/*! \brief Cached objectset for field and regex retrieval */
	struct ast_variable *objectset;
};

/*! \brief Structure used for fields comparison */
struct sorcery_memory_cache_fields_cmp_params {
	/*! \brief Pointer to the sorcery structure */
	const struct ast_sorcery *sorcery;
	/*! \brief The sorcery memory cache */
	struct sorcery_memory_cache *cache;
	/*! \brief Pointer to the fields to check */
	const struct ast_variable *fields;
	/*! \brief Regular expression for checking object id */
	regex_t *regex;
	/*! \brief Prefix for matching object id */
	const char *prefix;
	/*! \brief Prefix length in bytes for matching object id */
	const size_t prefix_len;
	/*! \brief Optional container to put object into */
	struct ao2_container *container;
};

static void *sorcery_memory_cache_open(const char *data);
static int sorcery_memory_cache_create(const struct ast_sorcery *sorcery, void *data, void *object);
static void sorcery_memory_cache_load(void *data, const struct ast_sorcery *sorcery, const char *type);
static void sorcery_memory_cache_reload(void *data, const struct ast_sorcery *sorcery, const char *type);
static void *sorcery_memory_cache_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type,
	const char *id);
static void *sorcery_memory_cache_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type,
	const struct ast_variable *fields);
static void sorcery_memory_cache_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type,
	struct ao2_container *objects, const struct ast_variable *fields);
static void sorcery_memory_cache_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type,
	struct ao2_container *objects, const char *regex);
static void sorcery_memory_cache_retrieve_prefix(const struct ast_sorcery *sorcery, void *data, const char *type,
	struct ao2_container *objects, const char *prefix, const size_t prefix_len);
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
	.retrieve_fields = sorcery_memory_cache_retrieve_fields,
	.retrieve_multiple = sorcery_memory_cache_retrieve_multiple,
	.retrieve_regex = sorcery_memory_cache_retrieve_regex,
	.retrieve_prefix = sorcery_memory_cache_retrieve_prefix,
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

#define PASSTHRU_UPDATE_THREAD_ID 0x5EED1E55
AST_THREADSTORAGE(passthru_update_id_storage);

static int is_passthru_update(void)
{
	uint32_t *passthru_update_thread_id;

	passthru_update_thread_id = ast_threadstorage_get(&passthru_update_id_storage,
		sizeof(*passthru_update_thread_id));
	if (!passthru_update_thread_id) {
		return 0;
	}

	return *passthru_update_thread_id == PASSTHRU_UPDATE_THREAD_ID;
}

static void set_passthru_update(uint32_t value)
{
	uint32_t *passthru_update_thread_id;

	passthru_update_thread_id = ast_threadstorage_get(&passthru_update_id_storage,
		sizeof(*passthru_update_thread_id));
	if (!passthru_update_thread_id) {
		ast_log(LOG_ERROR, "Could not set passthru update ID for sorcery memory cache thread\n");
		return;
	}

	*passthru_update_thread_id = value;
}

static void start_passthru_update(void)
{
	set_passthru_update(PASSTHRU_UPDATE_THREAD_ID);
}

static void end_passthru_update(void)
{
	set_passthru_update(0);
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

	switch (flags & OBJ_SEARCH_MASK) {
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

	switch (flags & OBJ_SEARCH_MASK) {
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

	switch (flags & OBJ_SEARCH_MASK) {
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

	switch (flags & OBJ_SEARCH_MASK) {
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
	if (cache->object_heap) {
		ast_heap_destroy(cache->object_heap);
	}
	ao2_cleanup(cache->objects);
	ast_free(cache->object_type);
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
	ast_variables_destroy(cached->objectset);
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

	hash_object = ao2_find(cache->objects, id, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NOLOCK);
	if (!hash_object) {
		return -1;
	}

	ast_assert(!strcmp(ast_sorcery_object_get_id(hash_object->object), id));

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

	/*
	 * We need to do deadlock avoidance between a non-scheduler thread
	 * blocking when trying to delete the scheduled entry for this
	 * callback because the scheduler thread is running this callback
	 * and this callback waiting for the cache->objects container lock
	 * that the blocked non-scheduler thread already holds.
	 */
	while (ao2_trywrlock(cache->objects)) {
		if (cache->del_expire) {
			cache->expire_id = -1;
			ao2_ref(cache, -1);
			return 0;
		}
		sched_yield();
	}

	cache->expire_id = -1;

	/* This is an optimization for objects which have been cached close to each other */
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
 * \brief Remove all objects from the cache.
 *
 * This removes ALL objects from both the hash table and heap.
 *
 * \pre cache->objects is write-locked
 *
 * \param cache The cache to empty.
 */
static void remove_all_from_cache(struct sorcery_memory_cache *cache)
{
	while (ast_heap_pop(cache->object_heap)) {
	}

	ao2_callback(cache->objects, OBJ_UNLINK | OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE,
		NULL, NULL);

	cache->del_expire = 1;
	AST_SCHED_DEL_UNREF(sched, cache->expire_id, ao2_ref(cache, -1));
	cache->del_expire = 0;
}

/*!
 * \internal
 * \brief AO2 callback function for making an object stale immediately
 *
 * This changes the creation time of an object so it appears as though it is stale immediately.
 *
 * \param obj The cached object
 * \param arg The cache itself
 * \param flags Unused flags
 */
static int object_stale_callback(void *obj, void *arg, int flags)
{
	struct sorcery_memory_cached_object *cached = obj;
	struct sorcery_memory_cache *cache = arg;

	/* Since our granularity is seconds it's possible for something to retrieve us within a window
	 * where we wouldn't be treated as stale. To ensure that doesn't happen we use the configured stale
	 * time plus a second.
	 */
	cached->created = ast_tvsub(cached->created, ast_samp2tv(cache->object_lifetime_stale + 1, 1));

	return CMP_MATCH;
}

/*!
 * \internal
 * \brief Mark an object as stale explicitly.
 *
 * This changes the creation time of an object so it appears as though it is stale immediately.
 *
 * \pre cache->objects is read-locked
 *
 * \param cache The cache the object is in
 * \param id The unique identifier of the object
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int mark_object_as_stale_in_cache(struct sorcery_memory_cache *cache, const char *id)
{
	struct sorcery_memory_cached_object *cached;

	cached = ao2_find(cache->objects, id, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!cached) {
		return -1;
	}

	ast_assert(!strcmp(ast_sorcery_object_get_id(cached->object), id));

	object_stale_callback(cached, cache, 0);
	ao2_ref(cached, -1);

	return 0;
}

/*!
 * \internal
 * \brief Mark all objects as stale within a cache.
 *
 * This changes the creation time of ALL objects so they appear as though they are stale.
 *
 * \pre cache->objects is read-locked
 *
 * \param cache
 */
static void mark_all_as_stale_in_cache(struct sorcery_memory_cache *cache)
{
	ao2_callback(cache->objects, OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE, object_stale_callback, cache);
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

	cache->del_expire = 1;
	AST_SCHED_DEL_UNREF(sched, cache->expire_id, ao2_ref(cache, -1));
	cache->del_expire = 0;

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
	struct sorcery_memory_cached_object *front;

	if (!ao2_link_flags(cache->objects, cached_object, OBJ_NOLOCK)) {
		return -1;
	}

	if (cache->full_backend_cache && (front = ast_heap_peek(cache->object_heap, 1))) {
		/* For a full backend cache all objects share the same lifetime */
		cached_object->created = front->created;
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
 * \brief Allocate a cached object for caching an object
 *
 * \param sorcery The sorcery instance
 * \param cache The sorcery memory cache
 * \param object The object to cache
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static struct sorcery_memory_cached_object *sorcery_memory_cached_object_alloc(const struct ast_sorcery *sorcery,
	const struct sorcery_memory_cache *cache, void *object)
{
	struct sorcery_memory_cached_object *cached;

	cached = ao2_alloc(sizeof(*cached), sorcery_memory_cached_object_destructor);
	if (!cached) {
		return NULL;
	}

	cached->object = ao2_bump(object);
	cached->created = ast_tvnow();
	cached->stale_update_sched_id = -1;

	if (cache->full_backend_cache) {
		/* A cached objectset allows us to easily perform all retrieval operations in a
		 * minimal of time.
		 */
		cached->objectset = ast_sorcery_objectset_create(sorcery, object);
		if (!cached->objectset) {
			ao2_ref(cached, -1);
			return NULL;
		}
	}

	return cached;
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

	cached = sorcery_memory_cached_object_alloc(sorcery, cache, object);
	if (!cached) {
		return -1;
	}

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
			ao2_unlock(cache->objects);
			ao2_ref(cached, -1);
			return -1;
		}
		ast_assert(ao2_container_count(cache->objects) != cache->maximum_objects);
	}
	if (add_to_cache(cache, cached)) {
		ast_log(LOG_ERROR, "Unable to add object '%s' to the cache\n",
			ast_sorcery_object_get_id(object));
		ao2_unlock(cache->objects);
		ao2_ref(cached, -1);
		return -1;
	}
	ao2_unlock(cache->objects);

	ao2_ref(cached, -1);
	return 0;
}

/*!
 * \internal
 * \brief AO2 callback function for adding an object to a memory cache
 *
 * \param obj The cached object
 * \param arg The sorcery instance
 * \param data The cache itself
 * \param flags Unused flags
 */
static int object_add_to_cache_callback(void *obj, void *arg, void *data, int flags)
{
	struct sorcery_memory_cache *cache = data;
	struct sorcery_memory_cached_object *cached;

	cached = sorcery_memory_cached_object_alloc(arg, cache, obj);
	if (!cached) {
		return CMP_STOP;
	}

	add_to_cache(cache, cached);
	ao2_ref(cached, -1);

	return 0;
}

struct stale_cache_update_task_data {
	struct ast_sorcery *sorcery;
	struct sorcery_memory_cache *cache;
	char *type;
};

static void stale_cache_update_task_data_destructor(void *obj)
{
	struct stale_cache_update_task_data *task_data = obj;

	ao2_cleanup(task_data->cache);
	ast_sorcery_unref(task_data->sorcery);
	ast_free(task_data->type);
}

static struct stale_cache_update_task_data *stale_cache_update_task_data_alloc(struct ast_sorcery *sorcery,
		struct sorcery_memory_cache *cache, const char *type)
{
	struct stale_cache_update_task_data *task_data;

	task_data = ao2_alloc_options(sizeof(*task_data), stale_cache_update_task_data_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!task_data) {
		return NULL;
	}

	task_data->sorcery = ao2_bump(sorcery);
	task_data->cache = ao2_bump(cache);
	task_data->type = ast_strdup(type);
	if (!task_data->type) {
		ao2_ref(task_data, -1);
		return NULL;
	}

	return task_data;
}

static int stale_cache_update(const void *data)
{
	struct stale_cache_update_task_data *task_data = (struct stale_cache_update_task_data *) data;
	struct ao2_container *backend_objects;

	start_passthru_update();
	backend_objects = ast_sorcery_retrieve_by_fields(task_data->sorcery, task_data->type,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	end_passthru_update();

	if (!backend_objects) {
		task_data->cache->stale_update_sched_id = -1;
		ao2_ref(task_data, -1);
		return 0;
	}

	if (task_data->cache->maximum_objects && ao2_container_count(backend_objects) >= task_data->cache->maximum_objects) {
		ast_log(LOG_ERROR, "The backend contains %d objects while the sorcery memory cache '%s' is explicitly configured to only allow %d\n",
			ao2_container_count(backend_objects), task_data->cache->name, task_data->cache->maximum_objects);
		task_data->cache->stale_update_sched_id = -1;
		ao2_ref(task_data, -1);
		return 0;
	}

	ao2_wrlock(task_data->cache->objects);
	remove_all_from_cache(task_data->cache);
	ao2_callback_data(backend_objects, OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE, object_add_to_cache_callback,
		task_data->sorcery, task_data->cache);

	/* If the number of cached objects does not match the number of backend objects we encountered a memory allocation
	 * failure and the cache is incomplete, so drop everything and fall back to querying the backend directly
	 * as it may be able to provide what is wanted.
	 */
	if (ao2_container_count(task_data->cache->objects) != ao2_container_count(backend_objects)) {
		ast_log(LOG_WARNING, "The backend contains %d objects while only %d could be added to sorcery memory cache '%s'\n",
			ao2_container_count(backend_objects), ao2_container_count(task_data->cache->objects), task_data->cache->name);
		remove_all_from_cache(task_data->cache);
	}

	ao2_unlock(task_data->cache->objects);
	ao2_ref(backend_objects, -1);

	task_data->cache->stale_update_sched_id = -1;
	ao2_ref(task_data, -1);

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

	task_data = ao2_alloc_options(sizeof(*task_data), stale_update_task_data_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
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

	start_passthru_update();

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
		ao2_ref(object, -1);
	}

	ast_test_suite_event_notify("SORCERY_MEMORY_CACHE_REFRESHED", "Cache: %s\r\nType: %s\r\nName: %s\r\n",
		task_data->cache->name, ast_sorcery_object_get_type(task_data->object),
		ast_sorcery_object_get_id(task_data->object));

	ao2_ref(task_data, -1);
	end_passthru_update();

	return 0;
}

/*!
 * \internal
 * \brief Populate the cache with all objects from the backend
 *
 * \pre cache->objects is write-locked
 *
 * \param sorcery The sorcery instance
 * \param type The type of object
 * \param cache The sorcery memory cache
 */
static void memory_cache_populate(const struct ast_sorcery *sorcery, const char *type, struct sorcery_memory_cache *cache)
{
	struct ao2_container *backend_objects;

	start_passthru_update();
	backend_objects = ast_sorcery_retrieve_by_fields(sorcery, type, AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	end_passthru_update();

	if (!backend_objects) {
		/* This will occur in off-nominal memory allocation failure scenarios */
		return;
	}

	if (cache->maximum_objects && ao2_container_count(backend_objects) >= cache->maximum_objects) {
		ast_log(LOG_ERROR, "The backend contains %d objects while the sorcery memory cache '%s' is explicitly configured to only allow %d\n",
			ao2_container_count(backend_objects), cache->name, cache->maximum_objects);
		return;
	}

	ao2_callback_data(backend_objects, OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE, object_add_to_cache_callback,
		(struct ast_sorcery*)sorcery, cache);

	/* If the number of cached objects does not match the number of backend objects we encountered a memory allocation
	 * failure and the cache is incomplete, so drop everything and fall back to querying the backend directly
	 * as it may be able to provide what is wanted.
	 */
	if (ao2_container_count(cache->objects) != ao2_container_count(backend_objects)) {
		ast_log(LOG_WARNING, "The backend contains %d objects while only %d could be added to sorcery memory cache '%s'\n",
			ao2_container_count(backend_objects), ao2_container_count(cache->objects), cache->name);
		remove_all_from_cache(cache);
	}

	ao2_ref(backend_objects, -1);
}

/*!
 * \internal
 * \brief Determine if a full backend cache update is needed and do it
 *
 * \param sorcery The sorcery instance
 * \param type The type of object
 * \param cache The sorcery memory cache
 */
static void memory_cache_full_update(const struct ast_sorcery *sorcery, const char *type, struct sorcery_memory_cache *cache)
{
	if (!cache->full_backend_cache) {
		return;
	}

	ao2_wrlock(cache->objects);
	if (!ao2_container_count(cache->objects)) {
		memory_cache_populate(sorcery, type, cache);
	}
	ao2_unlock(cache->objects);
}

/*!
 * \internal
 * \brief Queue a full cache update
 *
 * \param sorcery The sorcery instance
 * \param cache The sorcery memory cache
 * \param type The type of object
 */
static void memory_cache_stale_update_full(const struct ast_sorcery *sorcery, struct sorcery_memory_cache *cache,
	const char *type)
{
	ao2_wrlock(cache->objects);
	if (cache->stale_update_sched_id == -1) {
		struct stale_cache_update_task_data *task_data;

		task_data = stale_cache_update_task_data_alloc((struct ast_sorcery *) sorcery,
			cache, type);
		if (task_data) {
			cache->stale_update_sched_id = ast_sched_add(sched, 1,
				stale_cache_update, task_data);
		}
		if (cache->stale_update_sched_id < 0) {
			ao2_cleanup(task_data);
		}
	}
	ao2_unlock(cache->objects);
}

/*!
 * \internal
 * \brief Queue a stale object update
 *
 * \param sorcery The sorcery instance
 * \param cache The sorcery memory cache
 * \param cached The cached object
 */
static void memory_cache_stale_update_object(const struct ast_sorcery *sorcery, struct sorcery_memory_cache *cache,
	struct sorcery_memory_cached_object *cached)
{
	ao2_lock(cached);
	if (cached->stale_update_sched_id == -1) {
		struct stale_update_task_data *task_data;

		task_data = stale_update_task_data_alloc((struct ast_sorcery *) sorcery,
			cache, ast_sorcery_object_get_type(cached->object), cached->object);
		if (task_data) {
			ast_debug(1, "Cached sorcery object type '%s' ID '%s' is stale. Refreshing\n",
				ast_sorcery_object_get_type(cached->object), ast_sorcery_object_get_id(cached->object));
			cached->stale_update_sched_id = ast_sched_add(sched, 1,
				stale_item_update, task_data);
		}
		if (cached->stale_update_sched_id < 0) {
			ao2_cleanup(task_data);
			ast_log(LOG_ERROR, "Unable to update stale cached object type '%s', ID '%s'.\n",
				ast_sorcery_object_get_type(cached->object), ast_sorcery_object_get_id(cached->object));
		}
	}
	ao2_unlock(cached);
}

/*!
 * \internal
 * \brief Check whether an object (or cache) is stale and queue an update
 *
 * \param sorcery The sorcery instance
 * \param cache The sorcery memory cache
 * \param cached The cached object
 */
static void memory_cache_stale_check_object(const struct ast_sorcery *sorcery, struct sorcery_memory_cache *cache,
	struct sorcery_memory_cached_object *cached)
{
	struct timeval elapsed;

	if (!cache->object_lifetime_stale) {
		return;
	}

	/* For a full cache as every object has the same expiration/staleness we can do the same check */
	elapsed = ast_tvsub(ast_tvnow(), cached->created);

	if (elapsed.tv_sec < cache->object_lifetime_stale) {
		return;
	}

	if (cache->full_backend_cache) {
		memory_cache_stale_update_full(sorcery, cache, ast_sorcery_object_get_type(cached->object));
	} else {
		memory_cache_stale_update_object(sorcery, cache, cached);
	}

}

/*!
 * \internal
 * \brief Check whether the entire cache is stale or not and queue an update
 *
 * \param sorcery The sorcery instance
 * \param cache The sorcery memory cache
 *
 * \note Unlike \ref memory_cache_stale_check this does not require  an explicit object
 */
static void memory_cache_stale_check(const struct ast_sorcery *sorcery, struct sorcery_memory_cache *cache)
{
	struct sorcery_memory_cached_object *cached;

	ao2_rdlock(cache->objects);
	cached = ao2_bump(ast_heap_peek(cache->object_heap, 1));
	ao2_unlock(cache->objects);

	if (!cached) {
		return;
	}

	memory_cache_stale_check_object(sorcery, cache, cached);
	ao2_ref(cached, -1);
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

	if (is_passthru_update()) {
		return NULL;
	}

	memory_cache_full_update(sorcery, type, cache);

	cached = ao2_find(cache->objects, id, OBJ_SEARCH_KEY);
	if (!cached) {
		return NULL;
	}

	ast_assert(!strcmp(ast_sorcery_object_get_id(cached->object), id));

	memory_cache_stale_check_object(sorcery, cache, cached);

	object = ao2_bump(cached->object);
	ao2_ref(cached, -1);

	return object;
}

/*!
 * \internal
 * \brief AO2 callback function for comparing a retrieval request and finding applicable objects
 *
 * \param obj The cached object
 * \param arg The comparison parameters
 * \param flags Unused flags
 */
static int sorcery_memory_cache_fields_cmp(void *obj, void *arg, int flags)
{
	struct sorcery_memory_cached_object *cached = obj;
	const struct sorcery_memory_cache_fields_cmp_params *params = arg;
	RAII_VAR(struct ast_variable *, diff, NULL, ast_variables_destroy);

	if (params->regex) {
		/* If a regular expression has been provided see if it matches, otherwise move on */
		if (!regexec(params->regex, ast_sorcery_object_get_id(cached->object), 0, NULL, 0)) {
			ao2_link(params->container, cached->object);
		}
		return 0;
	} else if (params->prefix) {
		if (!strncmp(params->prefix, ast_sorcery_object_get_id(cached->object), params->prefix_len)) {
			ao2_link(params->container, cached->object);
		}
		return 0;
	} else if (params->fields &&
	     (!ast_variable_lists_match(cached->objectset, params->fields, 0))) {
		/* If we can't turn the object into an object set OR if differences exist between the fields
		 * passed in and what are present on the object they are not a match.
		 */
		return 0;
	}

	if (params->container) {
		ao2_link(params->container, cached->object);

		/* As multiple objects are being returned keep going */
		return 0;
	} else {
		/* Immediately stop and return, we only want a single object */
		return CMP_MATCH | CMP_STOP;
	}
}

/*!
 * \internal
 * \brief Callback function to retrieve a single object based on fields
 *
 * \param sorcery The sorcery instance
 * \param data The sorcery memory cache
 * \param type The type of the object to retrieve
 * \param fields Any explicit fields to search for
 */
static void *sorcery_memory_cache_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type,
	const struct ast_variable *fields)
{
	struct sorcery_memory_cache *cache = data;
	struct sorcery_memory_cache_fields_cmp_params params = {
		.sorcery = sorcery,
		.cache = cache,
		.fields = fields,
	};
	struct sorcery_memory_cached_object *cached;
	void *object = NULL;

	if (is_passthru_update() || !cache->full_backend_cache || !fields) {
		return NULL;
	}

	cached = ao2_callback(cache->objects, 0, sorcery_memory_cache_fields_cmp, &params);

	if (cached) {
		memory_cache_stale_check_object(sorcery, cache, cached);
		object = ao2_bump(cached->object);
		ao2_ref(cached, -1);
	}

	return object;
}

/*!
 * \internal
 * \brief Callback function to retrieve multiple objects from a memory cache
 *
 * \param sorcery The sorcery instance
 * \param data The sorcery memory cache
 * \param type The type of the object to retrieve
 * \param objects Container to place the objects into
 * \param fields Any explicit fields to search for
 */
static void sorcery_memory_cache_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type,
	struct ao2_container *objects, const struct ast_variable *fields)
{
	struct sorcery_memory_cache *cache = data;
	struct sorcery_memory_cache_fields_cmp_params params = {
		.sorcery = sorcery,
		.cache = cache,
		.fields = fields,
		.container = objects,
	};

	if (is_passthru_update() || !cache->full_backend_cache) {
		return;
	}

	memory_cache_full_update(sorcery, type, cache);
	ao2_callback(cache->objects, 0, sorcery_memory_cache_fields_cmp, &params);

	if (ao2_container_count(objects)) {
		memory_cache_stale_check(sorcery, cache);
	}
}

/*!
 * \internal
 * \brief Callback function to retrieve multiple objects using a regex on the object id
 *
 * \param sorcery The sorcery instance
 * \param data The sorcery memory cache
 * \param type The type of the object to retrieve
 * \param objects Container to place the objects into
 * \param regex Regular expression to apply to the object id
 */
static void sorcery_memory_cache_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type,
	struct ao2_container *objects, const char *regex)
{
	struct sorcery_memory_cache *cache = data;
	regex_t expression;
	struct sorcery_memory_cache_fields_cmp_params params = {
		.sorcery = sorcery,
		.cache = cache,
		.container = objects,
		.regex = &expression,
	};

	if (is_passthru_update() || !cache->full_backend_cache || regcomp(&expression, regex, REG_EXTENDED | REG_NOSUB)) {
		return;
	}

	memory_cache_full_update(sorcery, type, cache);
	ao2_callback(cache->objects, 0, sorcery_memory_cache_fields_cmp, &params);
	regfree(&expression);

	if (ao2_container_count(objects)) {
		memory_cache_stale_check(sorcery, cache);
	}
}

/*!
 * \internal
 * \brief Callback function to retrieve multiple objects whose id matches a prefix
 *
 * \param sorcery The sorcery instance
 * \param data The sorcery memory cache
 * \param type The type of the object to retrieve
 * \param objects Container to place the objects into
 * \param prefix, prefix_len Prefix to match against the object id
 */
static void sorcery_memory_cache_retrieve_prefix(const struct ast_sorcery *sorcery, void *data, const char *type,
	struct ao2_container *objects, const char *prefix, const size_t prefix_len)
{
	struct sorcery_memory_cache *cache = data;
	struct sorcery_memory_cache_fields_cmp_params params = {
		.sorcery = sorcery,
		.cache = cache,
		.container = objects,
		.prefix = prefix,
		.prefix_len = prefix_len,
	};

	if (is_passthru_update() || !cache->full_backend_cache) {
		return;
	}

	memory_cache_full_update(sorcery, type, cache);
	ao2_callback(cache->objects, 0, sorcery_memory_cache_fields_cmp, &params);

	if (ao2_container_count(objects)) {
		memory_cache_stale_check(sorcery, cache);
	}
}

/*!
 * \internal
 * \brief Callback function to finish configuring the memory cache
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

	cache->sorcery = sorcery;
	cache->object_type = ast_strdup(type);
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
	struct sorcery_memory_cache *cache = data;

	if (!cache->expire_on_reload) {
		return;
	}

	ao2_wrlock(cache->objects);
	remove_all_from_cache(cache);
	ao2_unlock(cache->objects);
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
	cache->stale_update_sched_id = -1;

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
		} else if (!strcasecmp(name, "expire_on_reload")) {
			cache->expire_on_reload = ast_true(value);
		} else if (!strcasecmp(name, "full_backend_cache")) {
			cache->full_backend_cache = ast_true(value);
		} else {
			ast_log(LOG_ERROR, "Unsupported option '%s' used for memory cache\n", name);
			return NULL;
		}
	}

	cache->objects = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0,
		cache->maximum_objects ? cache->maximum_objects : CACHE_CONTAINER_BUCKET_SIZE,
		sorcery_memory_cached_object_hash, NULL, sorcery_memory_cached_object_cmp);
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
		ast_debug(1, "Unable to delete object '%s' from sorcery cache\n", ast_sorcery_object_get_id(object));
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
		remove_all_from_cache(cache);
		ao2_unlock(cache->objects);
	}

	if (cache->full_backend_cache) {
		ao2_wrlock(cache->objects);
		cache->sorcery = NULL;
		ao2_unlock(cache->objects);
	}

	ao2_ref(cache, -1);
}

/*!
 * \internal
 * \brief CLI tab completion for cache names
 */
static char *sorcery_memory_cache_complete_name(const char *word, int state)
{
	struct sorcery_memory_cache *cache;
	struct ao2_iterator it_caches;
	int wordlen = strlen(word);
	int which = 0;
	char *result = NULL;

	it_caches = ao2_iterator_init(caches, 0);
	while ((cache = ao2_iterator_next(&it_caches))) {
		if (!strncasecmp(word, cache->name, wordlen)
			&& ++which > state) {
			result = ast_strdup(cache->name);
		}
		ao2_ref(cache, -1);
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&it_caches);
	return result;
}

/*!
 * \internal
 * \brief CLI command implementation for 'sorcery memory cache show'
 */
static char *sorcery_memory_cache_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sorcery_memory_cache *cache;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sorcery memory cache show";
		e->usage =
		    "Usage: sorcery memory cache show <name>\n"
		    "       Show sorcery memory cache configuration and statistics.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return sorcery_memory_cache_complete_name(a->word, a->n);
		} else {
			return NULL;
		}
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	cache = ao2_find(caches, a->argv[4], OBJ_SEARCH_KEY);
	if (!cache) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' does not exist\n", a->argv[4]);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Sorcery memory cache: %s\n", cache->name);
	ast_cli(a->fd, "Number of objects within cache: %d\n", ao2_container_count(cache->objects));
	if (cache->maximum_objects) {
		ast_cli(a->fd, "Maximum allowed objects: %d\n", cache->maximum_objects);
	} else {
		ast_cli(a->fd, "There is no limit on the maximum number of objects in the cache\n");
	}
	if (cache->object_lifetime_maximum) {
		ast_cli(a->fd, "Number of seconds before object expires: %d\n", cache->object_lifetime_maximum);
	} else {
		ast_cli(a->fd, "Object expiration is not enabled - cached objects will not expire\n");
	}
	if (cache->object_lifetime_stale) {
		ast_cli(a->fd, "Number of seconds before object becomes stale: %d\n", cache->object_lifetime_stale);
	} else {
		ast_cli(a->fd, "Object staleness is not enabled - cached objects will not go stale\n");
	}
	ast_cli(a->fd, "Expire all objects on reload: %s\n", AST_CLI_ONOFF(cache->expire_on_reload));

	ao2_ref(cache, -1);

	return CLI_SUCCESS;
}

/*! \brief Structure used to pass data for printing cached object information */
struct print_object_details {
	/*! \brief The sorcery memory cache */
	struct sorcery_memory_cache *cache;
	/*! \brief The CLI arguments */
	struct ast_cli_args *a;
};

/*!
 * \internal
 * \brief Callback function for displaying object within the cache
 */
static int sorcery_memory_cache_print_object(void *obj, void *arg, int flags)
{
#define FORMAT "%-25.25s %-15u %-15u \n"
	struct sorcery_memory_cached_object *cached = obj;
	struct print_object_details *details = arg;
	int seconds_until_expire = 0, seconds_until_stale = 0;

	if (details->cache->object_lifetime_maximum) {
		seconds_until_expire = ast_tvdiff_ms(ast_tvadd(cached->created, ast_samp2tv(details->cache->object_lifetime_maximum, 1)), ast_tvnow()) / 1000;
	}
	if (details->cache->object_lifetime_stale) {
		seconds_until_stale = ast_tvdiff_ms(ast_tvadd(cached->created, ast_samp2tv(details->cache->object_lifetime_stale, 1)), ast_tvnow()) / 1000;
	}

	ast_cli(details->a->fd, FORMAT, ast_sorcery_object_get_id(cached->object), MAX(seconds_until_stale, 0), MAX(seconds_until_expire, 0));

	return CMP_MATCH;
#undef FORMAT
}

/*!
 * \internal
 * \brief CLI command implementation for 'sorcery memory cache dump'
 */
static char *sorcery_memory_cache_dump(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-25.25s %-15.15s %-15.15s \n"
	struct sorcery_memory_cache *cache;
	struct print_object_details details;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sorcery memory cache dump";
		e->usage =
		    "Usage: sorcery memory cache dump <name>\n"
		    "       Dump a list of the objects within the cache, listed by object identifier.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return sorcery_memory_cache_complete_name(a->word, a->n);
		} else {
			return NULL;
		}
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	cache = ao2_find(caches, a->argv[4], OBJ_SEARCH_KEY);
	if (!cache) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' does not exist\n", a->argv[4]);
		return CLI_FAILURE;
	}

	details.cache = cache;
	details.a = a;

	ast_cli(a->fd, "Dumping sorcery memory cache '%s':\n", cache->name);
	if (!cache->object_lifetime_stale) {
		ast_cli(a->fd, " * Staleness is not enabled - objects will not go stale\n");
	}
	if (!cache->object_lifetime_maximum) {
		ast_cli(a->fd, " * Object lifetime is not enabled - objects will not expire\n");
	}
	ast_cli(a->fd, FORMAT, "Object Name", "Stale In", "Expires In");
	ast_cli(a->fd, FORMAT, "-------------------------", "---------------", "---------------");
	ao2_callback(cache->objects, OBJ_NODATA | OBJ_MULTIPLE, sorcery_memory_cache_print_object, &details);
	ast_cli(a->fd, FORMAT, "-------------------------", "---------------", "---------------");
	ast_cli(a->fd, "Total number of objects cached: %d\n", ao2_container_count(cache->objects));

	ao2_ref(cache, -1);

	return CLI_SUCCESS;
#undef FORMAT
}

/*!
 * \internal
 * \brief CLI tab completion for cached object names
 */
static char *sorcery_memory_cache_complete_object_name(const char *cache_name, const char *word, int state)
{
	struct sorcery_memory_cache *cache;
	struct sorcery_memory_cached_object *cached;
	struct ao2_iterator it_cached;
	int wordlen = strlen(word);
	int which = 0;
	char *result = NULL;

	cache = ao2_find(caches, cache_name, OBJ_SEARCH_KEY);
	if (!cache) {
		return NULL;
	}

	it_cached = ao2_iterator_init(cache->objects, 0);
	while ((cached = ao2_iterator_next(&it_cached))) {
		if (!strncasecmp(word, ast_sorcery_object_get_id(cached->object), wordlen)
			&& ++which > state) {
			result = ast_strdup(ast_sorcery_object_get_id(cached->object));
		}
		ao2_ref(cached, -1);
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&it_cached);

	ao2_ref(cache, -1);

	return result;
}

/*!
 * \internal
 * \brief CLI command implementation for 'sorcery memory cache expire'
 */
static char *sorcery_memory_cache_expire(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sorcery_memory_cache *cache;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sorcery memory cache expire";
		e->usage =
		    "Usage: sorcery memory cache expire <cache name> [object name]\n"
		    "       Expire a specific object or ALL objects within a sorcery memory cache.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return sorcery_memory_cache_complete_name(a->word, a->n);
		} else if (a->pos == 5) {
			return sorcery_memory_cache_complete_object_name(a->argv[4], a->word, a->n);
		} else {
			return NULL;
		}
	}

	if (a->argc < 5 || a->argc > 6) {
		return CLI_SHOWUSAGE;
	}

	cache = ao2_find(caches, a->argv[4], OBJ_SEARCH_KEY);
	if (!cache) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' does not exist\n", a->argv[4]);
		return CLI_FAILURE;
	}

	ao2_wrlock(cache->objects);
	if (a->argc == 5) {
		remove_all_from_cache(cache);
		ast_cli(a->fd, "All objects have been removed from cache '%s'\n", a->argv[4]);
	} else {
		if (cache->full_backend_cache) {
			ast_cli(a->fd, "Due to full backend caching per-object expiration is not available on cache '%s'\n", a->argv[4]);
		} else if (!remove_from_cache(cache, a->argv[5], 1)) {
			ast_cli(a->fd, "Successfully expired object '%s' from cache '%s'\n", a->argv[5], a->argv[4]);
		} else {
			ast_cli(a->fd, "Object '%s' was not expired from cache '%s' as it was not found\n", a->argv[5],
				a->argv[4]);
		}
	}
	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief CLI command implementation for 'sorcery memory cache stale'
 */
static char *sorcery_memory_cache_stale(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sorcery_memory_cache *cache;
	int reload = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sorcery memory cache stale";
		e->usage =
		    "Usage: sorcery memory cache stale <cache name> [object name [reload]]\n"
		    "       Mark a specific object or ALL objects as stale in a sorcery memory cache.\n"
		    "       If \"reload\" is specified, then the object is marked stale and immediately\n"
		    "       retrieved from backend storage to repopulate the cache\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return sorcery_memory_cache_complete_name(a->word, a->n);
		} else if (a->pos == 5) {
			return sorcery_memory_cache_complete_object_name(a->argv[4], a->word, a->n);
		} else if (a->pos == 6) {
			static const char * const completions[] = { "reload", NULL };
			return ast_cli_complete(a->word, completions, a->n);
		} else {
			return NULL;
		}
	}

	if (a->argc < 5 || a->argc > 7) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 7) {
		if (!strcasecmp(a->argv[6], "reload")) {
			reload = 1;
		} else {
			return CLI_SHOWUSAGE;
		}
	}

	cache = ao2_find(caches, a->argv[4], OBJ_SEARCH_KEY);
	if (!cache) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' does not exist\n", a->argv[4]);
		return CLI_FAILURE;
	}

	if (!cache->object_lifetime_stale) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' does not have staleness enabled\n", a->argv[4]);
		ao2_ref(cache, -1);
		return CLI_FAILURE;
	}

	ao2_rdlock(cache->objects);
	if (a->argc == 5) {
		mark_all_as_stale_in_cache(cache);
		ast_cli(a->fd, "Marked all objects in sorcery memory cache '%s' as stale\n", a->argv[4]);
	} else {
		if (!mark_object_as_stale_in_cache(cache, a->argv[5])) {
			ast_cli(a->fd, "Successfully marked object '%s' in memory cache '%s' as stale\n",
				a->argv[5], a->argv[4]);
			if (reload) {
				struct sorcery_memory_cached_object *cached;

				cached = ao2_find(cache->objects, a->argv[5], OBJ_SEARCH_KEY | OBJ_NOLOCK);
				if (cached) {
					memory_cache_stale_update_object(cache->sorcery, cache, cached);
					ao2_ref(cached, -1);
				}
			}
		} else {
			ast_cli(a->fd, "Object '%s' in sorcery memory cache '%s' could not be marked as stale as it was not found\n",
				a->argv[5], a->argv[4]);
		}
	}
	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief CLI command implementation for 'sorcery memory cache populate'
 */
static char *sorcery_memory_cache_populate(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sorcery_memory_cache *cache;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sorcery memory cache populate";
		e->usage =
		    "Usage: sorcery memory cache populate <cache name>\n"
		    "       Expire all objects in the cache and populate it with ALL objects from backend.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return sorcery_memory_cache_complete_name(a->word, a->n);
		} else {
			return NULL;
		}
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	cache = ao2_find(caches, a->argv[4], OBJ_SEARCH_KEY);
	if (!cache) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' does not exist\n", a->argv[4]);
		return CLI_FAILURE;
	}

	if (!cache->full_backend_cache) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' does not have full backend caching enabled\n", a->argv[4]);
		ao2_ref(cache, -1);
		return CLI_FAILURE;
	}

	ao2_wrlock(cache->objects);
	if (!cache->sorcery) {
		ast_cli(a->fd, "Specified sorcery memory cache '%s' is no longer active\n", a->argv[4]);
		ao2_unlock(cache->objects);
		ao2_ref(cache, -1);
		return CLI_FAILURE;
	}

	remove_all_from_cache(cache);
	memory_cache_populate(cache->sorcery, cache->object_type, cache);

	ast_cli(a->fd, "Specified sorcery memory cache '%s' has been populated with '%d' objects from the backend\n",
		a->argv[4], ao2_container_count(cache->objects));

	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_memory_cache[] = {
	AST_CLI_DEFINE(sorcery_memory_cache_show, "Show sorcery memory cache information"),
	AST_CLI_DEFINE(sorcery_memory_cache_dump, "Dump all objects within a sorcery memory cache"),
	AST_CLI_DEFINE(sorcery_memory_cache_expire, "Expire a specific object or ALL objects within a sorcery memory cache"),
	AST_CLI_DEFINE(sorcery_memory_cache_stale, "Mark a specific object or ALL objects as stale within a sorcery memory cache"),
	AST_CLI_DEFINE(sorcery_memory_cache_populate, "Clear and populate the sorcery memory cache with objects from the backend"),
};

/*!
 * \internal
 * \brief AMI command implementation for 'SorceryMemoryCacheExpireObject'
 */
static int sorcery_memory_cache_ami_expire_object(struct mansession *s, const struct message *m)
{
	const char *cache_name = astman_get_header(m, "Cache");
	const char *object_name = astman_get_header(m, "Object");
	struct sorcery_memory_cache *cache;
	int res;

	if (ast_strlen_zero(cache_name)) {
		astman_send_error(s, m, "SorceryMemoryCacheExpireObject requires that a cache name be provided.\n");
		return 0;
	} else if (ast_strlen_zero(object_name)) {
		astman_send_error(s, m, "SorceryMemoryCacheExpireObject requires that an object name be provided\n");
		return 0;
	}

	cache = ao2_find(caches, cache_name, OBJ_SEARCH_KEY);
	if (!cache) {
		astman_send_error(s, m, "The provided cache does not exist\n");
		return 0;
	}

	ao2_wrlock(cache->objects);
	if (cache->full_backend_cache) {
		res = 1;
	} else {
		res = remove_from_cache(cache, object_name, 1);
	}
	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	if (res == 1) {
		astman_send_error(s, m, "Due to full backend caching per-object expiration is not available, consider using SorceryMemoryCachePopulate or SorceryMemoryCacheExpire instead\n");
	} else if (!res) {
		astman_send_ack(s, m, "The provided object was expired from the cache\n");
	} else {
		astman_send_error(s, m, "The provided object could not be expired from the cache\n");
	}

	return 0;
}

/*!
 * \internal
 * \brief AMI command implementation for 'SorceryMemoryCacheExpire'
 */
static int sorcery_memory_cache_ami_expire(struct mansession *s, const struct message *m)
{
	const char *cache_name = astman_get_header(m, "Cache");
	struct sorcery_memory_cache *cache;

	if (ast_strlen_zero(cache_name)) {
		astman_send_error(s, m, "SorceryMemoryCacheExpire requires that a cache name be provided.\n");
		return 0;
	}

	cache = ao2_find(caches, cache_name, OBJ_SEARCH_KEY);
	if (!cache) {
		astman_send_error(s, m, "The provided cache does not exist\n");
		return 0;
	}

	ao2_wrlock(cache->objects);
	remove_all_from_cache(cache);
	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	astman_send_ack(s, m, "All objects were expired from the cache\n");

	return 0;
}

/*!
 * \internal
 * \brief AMI command implementation for 'SorceryMemoryCacheStaleObject'
 */
static int sorcery_memory_cache_ami_stale_object(struct mansession *s, const struct message *m)
{
	const char *cache_name = astman_get_header(m, "Cache");
	const char *object_name = astman_get_header(m, "Object");
	const char *reload = astman_get_header(m, "Reload");
	struct sorcery_memory_cache *cache;
	int res;

	if (ast_strlen_zero(cache_name)) {
		astman_send_error(s, m, "SorceryMemoryCacheStaleObject requires that a cache name be provided.\n");
		return 0;
	} else if (ast_strlen_zero(object_name)) {
		astman_send_error(s, m, "SorceryMemoryCacheStaleObject requires that an object name be provided\n");
		return 0;
	}

	cache = ao2_find(caches, cache_name, OBJ_SEARCH_KEY);
	if (!cache) {
		astman_send_error(s, m, "The provided cache does not exist\n");
		return 0;
	}

	ao2_rdlock(cache->objects);

	res = mark_object_as_stale_in_cache(cache, object_name);

	if (ast_true(reload)) {
		struct sorcery_memory_cached_object *cached;

		cached = ao2_find(cache->objects, object_name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
		if (cached) {
			memory_cache_stale_update_object(cache->sorcery, cache, cached);
			ao2_ref(cached, -1);
		}
	}

	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	if (!res) {
		astman_send_ack(s, m, "The provided object was marked as stale in the cache\n");
	} else {
		astman_send_error(s, m, "The provided object could not be marked as stale in the cache\n");
	}

	return 0;
}

/*!
 * \internal
 * \brief AMI command implementation for 'SorceryMemoryCacheStale'
 */
static int sorcery_memory_cache_ami_stale(struct mansession *s, const struct message *m)
{
	const char *cache_name = astman_get_header(m, "Cache");
	struct sorcery_memory_cache *cache;

	if (ast_strlen_zero(cache_name)) {
		astman_send_error(s, m, "SorceryMemoryCacheStale requires that a cache name be provided.\n");
		return 0;
	}

	cache = ao2_find(caches, cache_name, OBJ_SEARCH_KEY);
	if (!cache) {
		astman_send_error(s, m, "The provided cache does not exist\n");
		return 0;
	}

	ao2_rdlock(cache->objects);
	mark_all_as_stale_in_cache(cache);
	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	astman_send_ack(s, m, "All objects were marked as stale in the cache\n");

	return 0;
}

/*!
 * \internal
 * \brief AMI command implementation for 'SorceryMemoryCachePopulate'
 */
static int sorcery_memory_cache_ami_populate(struct mansession *s, const struct message *m)
{
	const char *cache_name = astman_get_header(m, "Cache");
	struct sorcery_memory_cache *cache;

	if (ast_strlen_zero(cache_name)) {
		astman_send_error(s, m, "SorceryMemoryCachePopulate requires that a cache name be provided.\n");
		return 0;
	}

	cache = ao2_find(caches, cache_name, OBJ_SEARCH_KEY);
	if (!cache) {
		astman_send_error(s, m, "The provided cache does not exist\n");
		return 0;
	}

	if (!cache->full_backend_cache) {
		astman_send_error(s, m, "The provided cache does not have full backend caching enabled\n");
		ao2_ref(cache, -1);
		return 0;
	}

	ao2_wrlock(cache->objects);
	if (!cache->sorcery) {
		astman_send_error(s, m, "The provided cache is no longer active\n");
		ao2_unlock(cache->objects);
		ao2_ref(cache, -1);
		return 0;
	}

	remove_all_from_cache(cache);
	memory_cache_populate(cache->sorcery, cache->object_type, cache);

	ao2_unlock(cache->objects);

	ao2_ref(cache, -1);

	astman_send_ack(s, m, "Cache has been expired and populated\n");

	return 0;
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
			"\t* Creates a memory cache with a stale object lifetime of 90 and verifies it";
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
			"\t* Create a memory cache with a stale object lifetime of toast";
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
			"\t* Confirms that the test object is in the cache";
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
			"\t* Confirms that the new test object has replaced the old one";
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
			"\t* Confirms that the test object is no longer in the cache";
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
			"\t* Confirms that bob and alice are in the memory cache";
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
			"\t* Confirms that the objects have been removed from the cache";
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
 * \brief Callback for retrieving multiple sorcery objects
 *
 * The mock wizard uses the \ref real_backend_data in order to construct
 * objects. If the backend data is "nonexisent" then no object is returned.
 * Otherwise, the number of objects matching the exists value will be returned.
 *
 * \param sorcery The sorcery instance
 * \param data Unused
 * \param type The object type. Will always be "test".
 * \param objects Container to place objects into.
 * \param fields Fields to search for.
 */
static void mock_retrieve_multiple(const struct ast_sorcery *sorcery, void *data,
		const char *type, struct ao2_container *objects, const struct ast_variable *fields)
{
	int i;

	if (fields) {
		return;
	}

	for (i = 0; i < real_backend_data->exists; ++i) {
		char uuid[AST_UUID_STR_LEN];
		struct test_data *b_data;

		b_data = ast_sorcery_alloc(sorcery, type, ast_uuid_generate_str(uuid, sizeof(uuid)));
		if (!b_data) {
			continue;
		}

		b_data->salt = real_backend_data->salt;
		b_data->pepper = real_backend_data->pepper;

		ao2_link(objects, b_data);
		ao2_ref(b_data, -1);
	}
}

/*!
 * \brief A mock sorcery wizard used for the stale test
 */
static struct ast_sorcery_wizard mock_wizard = {
	.name = "mock",
	.retrieve_id = mock_retrieve_id,
	.retrieve_multiple = mock_retrieve_multiple,
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
			"\t\t* Ensure that new data in the cache matches backend data";
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

AST_TEST_DEFINE(full_backend_cache_expiration)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct backend_data initial = {
		.salt = 0,
		.pepper = 0,
		.exists = 4,
	};
	struct ao2_container *objects;
	ast_mutex_t lock;
	ast_cond_t cond;
	struct timeval start;
	struct timespec end;

	switch (cmd) {
	case TEST_INIT:
		info->name = "full_backend_cache_expiration";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Ensure that the full backend cache actually caches the backend";
		info->description = "This test performs the following:\n"
			"\t* Create a sorcery instance with two wizards"
			"\t\t* The first is a memory cache that expires objects after 3 seconds and does full backend caching\n"
			"\t\t* The second is a mock of a back-end\n"
			"\t* Populates the cache by requesting all objects which returns 4.\n"
			"\t* Updates the backend to contain a different number of objects, 8.\n"
			"\t* Requests all objects and confirms the number returned is only 4.\n"
			"\t* Wait for cached objects to expire.\n"
			"\t* Requests all objects and confirms the number returned is 8.";
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
			"object_lifetime_maximum=3,full_backend_cache=yes", 1);
	ast_sorcery_apply_wizard_mapping(sorcery, "test", "mock", NULL, 0);
	ast_sorcery_internal_object_register(sorcery, "test", test_data_alloc, NULL, NULL);
	ast_sorcery_object_field_register_nodoc(sorcery, "test", "salt", "0", OPT_UINT_T, 0, FLDSET(struct test_data, salt));
	ast_sorcery_object_field_register_nodoc(sorcery, "test", "pepper", "0", OPT_UINT_T, 0, FLDSET(struct test_data, pepper));

	/* Prepopulate the cache */
	real_backend_data = &initial;

	/* Get all current objects in the backend */
	objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!objects) {
		ast_test_status_update(test, "Unable to retrieve all objects in backend and populate cache\n");
		goto cleanup;
	}
	ao2_ref(objects, -1);

	/* Update the backend to have a different number of objects */
	initial.exists = 8;

	/* Get all current objects in the backend */
	objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!objects) {
		ast_test_status_update(test, "Unable to retrieve all objects in backend and populate cache\n");
		goto cleanup;
	}

	if (ao2_container_count(objects) == initial.exists) {
		ast_test_status_update(test, "Number of objects returned is of the current backend and not the cache\n");
		ao2_ref(objects, -1);
		goto cleanup;
	}

	ao2_ref(objects, -1);

	ast_mutex_init(&lock);
	ast_cond_init(&cond, NULL);

	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 5;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&lock);
	while (ast_cond_timedwait(&cond, &lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&lock);

	ast_mutex_destroy(&lock);
	ast_cond_destroy(&cond);

	/* Get all current objects in the backend */
	objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!objects) {
		ast_test_status_update(test, "Unable to retrieve all objects in backend and populate cache\n");
		goto cleanup;
	}

	if (ao2_container_count(objects) != initial.exists) {
		ast_test_status_update(test, "Number of objects returned is NOT of the current backend when it should be\n");
		ao2_ref(objects, -1);
		goto cleanup;
	}

	ao2_ref(objects, -1);

	res = AST_TEST_PASS;

cleanup:
	if (sorcery) {
		ast_sorcery_unref(sorcery);
	}
	ast_sorcery_wizard_unregister(&mock_wizard);
	return res;
}

AST_TEST_DEFINE(full_backend_cache_stale)
{
	int res = AST_TEST_FAIL;
	struct ast_sorcery *sorcery = NULL;
	struct backend_data initial = {
		.salt = 0,
		.pepper = 0,
		.exists = 4,
	};
	struct ao2_container *objects;
	ast_mutex_t lock;
	ast_cond_t cond;
	struct timeval start;
	struct timespec end;

	switch (cmd) {
	case TEST_INIT:
		info->name = "full_backend_cache_stale";
		info->category = "/res/res_sorcery_memory_cache/";
		info->summary = "Ensure that the full backend cache works with staleness";
		info->description = "This test performs the following:\n"
			"\t* Create a sorcery instance with two wizards"
			"\t\t* The first is a memory cache that stales objects after 1 second and does full backend caching\n"
			"\t\t* The second is a mock of a back-end\n"
			"\t* Populates the cache by requesting all objects which returns 4.\n"
			"\t* Wait for objects to go stale.\n"
			"\t* Updates the backend to contain a different number of objects, 8.\""
			"\t* Requests all objects and confirms the number returned is only 4.\n"
			"\t* Wait for objects to be refreshed from backend.\n"
			"\t* Requests all objects and confirms the number returned is 8.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_sorcery_wizard_register(&mock_wizard);

	ast_mutex_init(&lock);
	ast_cond_init(&cond, NULL);

	sorcery = ast_sorcery_open();
	if (!sorcery) {
		ast_test_status_update(test, "Failed to create sorcery instance\n");
		goto cleanup;
	}

	ast_sorcery_apply_wizard_mapping(sorcery, "test", "memory_cache",
			"object_lifetime_stale=1,full_backend_cache=yes", 1);
	ast_sorcery_apply_wizard_mapping(sorcery, "test", "mock", NULL, 0);
	ast_sorcery_internal_object_register(sorcery, "test", test_data_alloc, NULL, NULL);
	ast_sorcery_object_field_register_nodoc(sorcery, "test", "salt", "0", OPT_UINT_T, 0, FLDSET(struct test_data, salt));
	ast_sorcery_object_field_register_nodoc(sorcery, "test", "pepper", "0", OPT_UINT_T, 0, FLDSET(struct test_data, pepper));

	/* Prepopulate the cache */
	real_backend_data = &initial;

	/* Get all current objects in the backend */
	objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!objects) {
		ast_test_status_update(test, "Unable to retrieve all objects in backend and populate cache\n");
		goto cleanup;
	}
	ao2_ref(objects, -1);

	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 5;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&lock);
	while (ast_cond_timedwait(&cond, &lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&lock);

	initial.exists = 8;

	/* Get all current objects in the backend */
	objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!objects) {
		ast_test_status_update(test, "Unable to retrieve all objects in backend and populate cache\n");
		goto cleanup;
	}

	if (ao2_container_count(objects) == initial.exists) {
		ast_test_status_update(test, "Number of objects returned is of the backend and not the cache\n");
		ao2_ref(objects, -1);
		goto cleanup;
	}

	ao2_ref(objects, -1);

	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 5;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&lock);
	while (ast_cond_timedwait(&cond, &lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&lock);

	/* Get all current objects in the backend */
	objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!objects) {
		ast_test_status_update(test, "Unable to retrieve all objects in backend and populate cache\n");
		goto cleanup;
	}

	if (ao2_container_count(objects) != initial.exists) {
		ast_test_status_update(test, "Number of objects returned is not of backend\n");
		ao2_ref(objects, -1);
		goto cleanup;
	}

	ao2_ref(objects, -1);

	start = ast_tvnow();
	end.tv_sec = start.tv_sec + 5;
	end.tv_nsec = start.tv_usec * 1000;

	ast_mutex_lock(&lock);
	while (ast_cond_timedwait(&cond, &lock, &end) != ETIMEDOUT) {
	}
	ast_mutex_unlock(&lock);

	res = AST_TEST_PASS;

cleanup:
	if (sorcery) {
		ast_sorcery_unref(sorcery);
	}
	ast_sorcery_wizard_unregister(&mock_wizard);
	ast_mutex_destroy(&lock);
	ast_cond_destroy(&cond);
	return res;
}

#endif

static int unload_module(void)
{
	AST_TEST_UNREGISTER(open_with_valid_options);
	AST_TEST_UNREGISTER(open_with_invalid_options);
	AST_TEST_UNREGISTER(create_and_retrieve);
	AST_TEST_UNREGISTER(update);
	AST_TEST_UNREGISTER(delete);
	AST_TEST_UNREGISTER(maximum_objects);
	AST_TEST_UNREGISTER(expiration);
	AST_TEST_UNREGISTER(stale);
	AST_TEST_UNREGISTER(full_backend_cache_expiration);
	AST_TEST_UNREGISTER(full_backend_cache_stale);

	ast_manager_unregister("SorceryMemoryCacheExpireObject");
	ast_manager_unregister("SorceryMemoryCacheExpire");
	ast_manager_unregister("SorceryMemoryCacheStaleObject");
	ast_manager_unregister("SorceryMemoryCacheStale");
	ast_manager_unregister("SorceryMemoryCachePopulate");

	ast_cli_unregister_multiple(cli_memory_cache, ARRAY_LEN(cli_memory_cache));

	ast_sorcery_wizard_unregister(&memory_cache_object_wizard);

	/*
	 * XXX There is the potential to leak memory if there are pending
	 * next-cache-expiration and stale-cache-update tasks in the scheduler.
	 */
	if (sched) {
		ast_sched_context_destroy(sched);
		sched = NULL;
	}

	ao2_cleanup(caches);
	caches = NULL;

	return 0;
}

static int load_module(void)
{
	int res;

	caches = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		CACHES_CONTAINER_BUCKET_SIZE, sorcery_memory_cache_hash, NULL,
		sorcery_memory_cache_cmp);
	if (!caches) {
		ast_log(LOG_ERROR, "Failed to create container for configured caches\n");
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

	if (ast_sorcery_wizard_register(&memory_cache_object_wizard)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res = ast_cli_register_multiple(cli_memory_cache, ARRAY_LEN(cli_memory_cache));
	res |= ast_manager_register_xml("SorceryMemoryCacheExpireObject", EVENT_FLAG_SYSTEM, sorcery_memory_cache_ami_expire_object);
	res |= ast_manager_register_xml("SorceryMemoryCacheExpire", EVENT_FLAG_SYSTEM, sorcery_memory_cache_ami_expire);
	res |= ast_manager_register_xml("SorceryMemoryCacheStaleObject", EVENT_FLAG_SYSTEM, sorcery_memory_cache_ami_stale_object);
	res |= ast_manager_register_xml("SorceryMemoryCacheStale", EVENT_FLAG_SYSTEM, sorcery_memory_cache_ami_stale);
	res |= ast_manager_register_xml("SorceryMemoryCachePopulate", EVENT_FLAG_SYSTEM, sorcery_memory_cache_ami_populate);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/* This causes the stale unit test to execute last, so if a sorcery instance persists
	 * longer than expected subsequent unit tests don't fail when setting it up.
	 */
	AST_TEST_REGISTER(stale);
	AST_TEST_REGISTER(open_with_valid_options);
	AST_TEST_REGISTER(open_with_invalid_options);
	AST_TEST_REGISTER(create_and_retrieve);
	AST_TEST_REGISTER(update);
	AST_TEST_REGISTER(delete);
	AST_TEST_REGISTER(maximum_objects);
	AST_TEST_REGISTER(expiration);
	AST_TEST_REGISTER(full_backend_cache_expiration);
	AST_TEST_REGISTER(full_backend_cache_stale);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery Memory Cache Object Wizard",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
