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

/*! \file
 *
 * \brief Stasis Message API.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/astobj2.h"
#include "asterisk/hashtab.h"
#include "asterisk/stasis_internal.h"
#include "asterisk/stasis.h"
#include "asterisk/utils.h"
#include "asterisk/vector.h"

#ifdef LOW_MEMORY
#define NUM_CACHE_BUCKETS 17
#else
#define NUM_CACHE_BUCKETS 563
#endif

/*! \internal */
struct stasis_cache {
	struct ao2_container *entries;
	snapshot_get_id id_fn;
	cache_aggregate_calc_fn aggregate_calc_fn;
	cache_aggregate_publish_fn aggregate_publish_fn;
};

/*! \internal */
struct stasis_caching_topic {
	struct stasis_cache *cache;
	struct stasis_topic *topic;
	struct stasis_topic *original_topic;
	struct stasis_subscription *sub;
};

static void stasis_caching_topic_dtor(void *obj)
{
	struct stasis_caching_topic *caching_topic = obj;

	/* Caching topics contain subscriptions, and must be manually
	 * unsubscribed. */
	ast_assert(!stasis_subscription_is_subscribed(caching_topic->sub));
	/* If there are any messages in flight to this subscription; that would
	 * be bad. */
	ast_assert(stasis_subscription_is_done(caching_topic->sub));

	ao2_cleanup(caching_topic->sub);
	caching_topic->sub = NULL;
	ao2_cleanup(caching_topic->cache);
	caching_topic->cache = NULL;
	ao2_cleanup(caching_topic->topic);
	caching_topic->topic = NULL;
	ao2_cleanup(caching_topic->original_topic);
	caching_topic->original_topic = NULL;
}

struct stasis_topic *stasis_caching_get_topic(struct stasis_caching_topic *caching_topic)
{
	return caching_topic->topic;
}

struct stasis_caching_topic *stasis_caching_unsubscribe(struct stasis_caching_topic *caching_topic)
{
	if (!caching_topic) {
		return NULL;
	}

	/*
	 * The subscription may hold the last reference to this caching
	 * topic, but we want to make sure the unsubscribe finishes
	 * before kicking of the caching topic's dtor.
	 */
	ao2_ref(caching_topic, +1);

	if (stasis_subscription_is_subscribed(caching_topic->sub)) {
		/*
		 * Increment the reference to hold on to it past the
		 * unsubscribe. Will be cleaned up in dtor.
		 */
		ao2_ref(caching_topic->sub, +1);
		stasis_unsubscribe(caching_topic->sub);
	} else {
		ast_log(LOG_ERROR, "stasis_caching_topic unsubscribed multiple times\n");
	}
	ao2_cleanup(caching_topic);
	return NULL;
}

struct stasis_caching_topic *stasis_caching_unsubscribe_and_join(struct stasis_caching_topic *caching_topic)
{
	if (!caching_topic) {
		return NULL;
	}

	/* Hold a ref past the unsubscribe */
	ao2_ref(caching_topic, +1);
	stasis_caching_unsubscribe(caching_topic);
	stasis_subscription_join(caching_topic->sub);
	ao2_cleanup(caching_topic);
	return NULL;
}

/*!
 * \brief The key for an entry in the cache
 * \note The items in this struct must be immutable for the item in the cache
 */
struct cache_entry_key {
	/*! The message type of the item stored in the cache */
	struct stasis_message_type *type;
	/*! The unique ID of the item stored in the cache */
	const char *id;
	/*! The hash, computed from \c type and \c id */
	unsigned int hash;
};

struct stasis_cache_entry {
	struct cache_entry_key key;
	/*! Aggregate snapshot of the stasis cache. */
	struct stasis_message *aggregate;
	/*! Local entity snapshot of the stasis event. */
	struct stasis_message *local;
	/*! Remote entity snapshots of the stasis event. */
	AST_VECTOR(, struct stasis_message *) remote;
};

static void cache_entry_dtor(void *obj)
{
	struct stasis_cache_entry *entry = obj;
	size_t idx;

	ao2_cleanup(entry->key.type);
	entry->key.type = NULL;
	ast_free((char *) entry->key.id);
	entry->key.id = NULL;

	ao2_cleanup(entry->aggregate);
	entry->aggregate = NULL;
	ao2_cleanup(entry->local);
	entry->local = NULL;

	for (idx = 0; idx < AST_VECTOR_SIZE(&entry->remote); ++idx) {
		struct stasis_message *remote;

		remote = AST_VECTOR_GET(&entry->remote, idx);
		ao2_cleanup(remote);
	}
	AST_VECTOR_FREE(&entry->remote);
}

static void cache_entry_compute_hash(struct cache_entry_key *key)
{
	key->hash = ast_hashtab_hash_string(stasis_message_type_name(key->type));
	key->hash += ast_hashtab_hash_string(key->id);
}

static struct stasis_cache_entry *cache_entry_create(struct stasis_message_type *type, const char *id, struct stasis_message *snapshot)
{
	struct stasis_cache_entry *entry;
	int is_remote;

	ast_assert(id != NULL);
	ast_assert(snapshot != NULL);

	if (!type) {
		return NULL;
	}

	entry = ao2_alloc_options(sizeof(*entry), cache_entry_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!entry) {
		return NULL;
	}

	entry->key.id = ast_strdup(id);
	if (!entry->key.id) {
		ao2_cleanup(entry);
		return NULL;
	}
	entry->key.type = ao2_bump(type);
	cache_entry_compute_hash(&entry->key);

	is_remote = ast_eid_cmp(&ast_eid_default, stasis_message_eid(snapshot)) ? 1 : 0;
	if (AST_VECTOR_INIT(&entry->remote, is_remote)) {
		ao2_cleanup(entry);
		return NULL;
	}

	if (is_remote) {
		if (AST_VECTOR_APPEND(&entry->remote, snapshot)) {
			ao2_cleanup(entry);
			return NULL;
		}
	} else {
		entry->local = snapshot;
	}
	ao2_bump(snapshot);

	return entry;
}

static int cache_entry_hash(const void *obj, int flags)
{
	const struct stasis_cache_entry *object;
	const struct cache_entry_key *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = &object->key;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}

	return (int)key->hash;
}

static int cache_entry_cmp(void *obj, void *arg, int flags)
{
	const struct stasis_cache_entry *object_left = obj;
	const struct stasis_cache_entry *object_right = arg;
	const struct cache_entry_key *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = &object_right->key;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = object_left->key.type != right_key->type
			|| strcmp(object_left->key.id, right_key->id);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Not supported by container */
		ast_assert(0);
		cmp = -1;
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	/*
	 * At this point the traversal callback is identical to a sorted
	 * container.
	 */
	return CMP_MATCH;
}

static void cache_dtor(void *obj)
{
	struct stasis_cache *cache = obj;

	ao2_cleanup(cache->entries);
	cache->entries = NULL;
}

struct stasis_cache *stasis_cache_create_full(snapshot_get_id id_fn,
	cache_aggregate_calc_fn aggregate_calc_fn,
	cache_aggregate_publish_fn aggregate_publish_fn)
{
	struct stasis_cache *cache;

	cache = ao2_alloc_options(sizeof(*cache), cache_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cache) {
		return NULL;
	}

	cache->entries = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0,
		NUM_CACHE_BUCKETS, cache_entry_hash, NULL, cache_entry_cmp);
	if (!cache->entries) {
		ao2_cleanup(cache);
		return NULL;
	}

	cache->id_fn = id_fn;
	cache->aggregate_calc_fn = aggregate_calc_fn;
	cache->aggregate_publish_fn = aggregate_publish_fn;

	return cache;
}

struct stasis_cache *stasis_cache_create(snapshot_get_id id_fn)
{
	return stasis_cache_create_full(id_fn, NULL, NULL);
}

struct stasis_message *stasis_cache_entry_get_aggregate(struct stasis_cache_entry *entry)
{
	return entry->aggregate;
}

struct stasis_message *stasis_cache_entry_get_local(struct stasis_cache_entry *entry)
{
	return entry->local;
}

struct stasis_message *stasis_cache_entry_get_remote(struct stasis_cache_entry *entry, int idx)
{
	if (idx < AST_VECTOR_SIZE(&entry->remote)) {
		return AST_VECTOR_GET(&entry->remote, idx);
	}
	return NULL;
}

/*!
 * \internal
 * \brief Find the cache entry in the cache entries container.
 *
 * \param entries Container of cached entries.
 * \param type Type of message to retrieve the cache entry.
 * \param id Identity of the snapshot to retrieve the cache entry.
 *
 * \note The entries container is already locked.
 *
 * \retval Cache-entry on success.
 * \retval NULL Not in cache.
 */
static struct stasis_cache_entry *cache_find(struct ao2_container *entries, struct stasis_message_type *type, const char *id)
{
	struct cache_entry_key search_key;
	struct stasis_cache_entry *entry;

	search_key.type = type;
	search_key.id = id;
	cache_entry_compute_hash(&search_key);
	entry = ao2_find(entries, &search_key, OBJ_SEARCH_KEY | OBJ_NOLOCK);

	/* Ensure that what we looked for is what we found. */
	ast_assert(!entry
		|| (!strcmp(stasis_message_type_name(entry->key.type),
			stasis_message_type_name(type)) && !strcmp(entry->key.id, id)));
	return entry;
}

/*!
 * \internal
 * \brief Remove the stasis snapshot in the cache entry determined by eid.
 *
 * \param entries Container of cached entries.
 * \param cached_entry The entry to remove the snapshot from.
 * \param eid Which snapshot in the cached entry.
 *
 * \note The entries container is already locked.
 *
 * \return Previous stasis entry snapshot.
 */
static struct stasis_message *cache_remove(struct ao2_container *entries, struct stasis_cache_entry *cached_entry, const struct ast_eid *eid)
{
	struct stasis_message *old_snapshot;
	int is_remote;

	is_remote = ast_eid_cmp(eid, &ast_eid_default);
	if (!is_remote) {
		old_snapshot = cached_entry->local;
		cached_entry->local = NULL;
	} else {
		int idx;

		old_snapshot = NULL;
		for (idx = 0; idx < AST_VECTOR_SIZE(&cached_entry->remote); ++idx) {
			struct stasis_message *cur;

			cur = AST_VECTOR_GET(&cached_entry->remote, idx);
			if (!ast_eid_cmp(eid, stasis_message_eid(cur))) {
				old_snapshot = AST_VECTOR_REMOVE_UNORDERED(&cached_entry->remote, idx);
				break;
			}
		}
	}

	if (!cached_entry->local && !AST_VECTOR_SIZE(&cached_entry->remote)) {
		ao2_unlink_flags(entries, cached_entry, OBJ_NOLOCK);
	}

	return old_snapshot;
}

/*!
 * \internal
 * \brief Update the stasis snapshot in the cache entry determined by eid.
 *
 * \param cached_entry The entry to remove the snapshot from.
 * \param eid Which snapshot in the cached entry.
 * \param new_snapshot Snapshot to replace the old snapshot.
 *
 * \return Previous stasis entry snapshot.
 */
static struct stasis_message *cache_udpate(struct stasis_cache_entry *cached_entry, const struct ast_eid *eid, struct stasis_message *new_snapshot)
{
	struct stasis_message *old_snapshot;
	int is_remote;
	int idx;

	is_remote = ast_eid_cmp(eid, &ast_eid_default);
	if (!is_remote) {
		old_snapshot = cached_entry->local;
		cached_entry->local = ao2_bump(new_snapshot);
		return old_snapshot;
	}

	old_snapshot = NULL;
	for (idx = 0; idx < AST_VECTOR_SIZE(&cached_entry->remote); ++idx) {
		struct stasis_message *cur;

		cur = AST_VECTOR_GET(&cached_entry->remote, idx);
		if (!ast_eid_cmp(eid, stasis_message_eid(cur))) {
			old_snapshot = AST_VECTOR_REMOVE_UNORDERED(&cached_entry->remote, idx);
			break;
		}
	}
	if (!AST_VECTOR_APPEND(&cached_entry->remote, new_snapshot)) {
		ao2_bump(new_snapshot);
	}

	return old_snapshot;
}

struct cache_put_snapshots {
	/*! Old cache eid snapshot. */
	struct stasis_message *old;
	/*! Old cache aggregate snapshot. */
	struct stasis_message *aggregate_old;
	/*! New cache aggregate snapshot. */
	struct stasis_message *aggregate_new;
};

static struct cache_put_snapshots cache_put(struct stasis_cache *cache,
	struct stasis_message_type *type, const char *id, const struct ast_eid *eid,
	struct stasis_message *new_snapshot)
{
	struct stasis_cache_entry *cached_entry;
	struct cache_put_snapshots snapshots;

	ast_assert(cache->entries != NULL);
	ast_assert(eid != NULL);/* Aggregate snapshots not allowed to be put directly. */
	ast_assert(new_snapshot == NULL ||
		type == stasis_message_type(new_snapshot));

	memset(&snapshots, 0, sizeof(snapshots));

	ao2_wrlock(cache->entries);

	cached_entry = cache_find(cache->entries, type, id);

	/* Update the eid snapshot. */
	if (!new_snapshot) {
		/* Remove snapshot from cache */
		if (cached_entry) {
			snapshots.old = cache_remove(cache->entries, cached_entry, eid);
		}
	} else if (cached_entry) {
		/* Update snapshot in cache */
		snapshots.old = cache_udpate(cached_entry, eid, new_snapshot);
	} else {
		/* Insert into the cache */
		cached_entry = cache_entry_create(type, id, new_snapshot);
		if (cached_entry) {
			ao2_link_flags(cache->entries, cached_entry, OBJ_NOLOCK);
		}
	}

	/* Update the aggregate snapshot. */
	if (cache->aggregate_calc_fn && cached_entry) {
		snapshots.aggregate_new = cache->aggregate_calc_fn(cached_entry, new_snapshot);
		snapshots.aggregate_old = cached_entry->aggregate;
		cached_entry->aggregate = ao2_bump(snapshots.aggregate_new);
	}

	ao2_unlock(cache->entries);

	ao2_cleanup(cached_entry);
	return snapshots;
}

/*!
 * \internal
 * \brief Dump all entity snapshots in the cache entry into the given container.
 *
 * \param snapshots Container to put all snapshots in the cache entry.
 * \param entry Cache entry to use.
 *
 * \retval 0 on success.
 * \retval non-zero on error.
 */
static int cache_entry_dump(struct ao2_container *snapshots, const struct stasis_cache_entry *entry)
{
	int idx;
	int err = 0;

	ast_assert(snapshots != NULL);
	ast_assert(entry != NULL);

	/* The aggregate snapshot is not a snapshot from an entity. */

	if (entry->local) {
		err |= !ao2_link(snapshots, entry->local);
	}

	for (idx = 0; !err && idx < AST_VECTOR_SIZE(&entry->remote); ++idx) {
		struct stasis_message *snapshot;

		snapshot = AST_VECTOR_GET(&entry->remote, idx);
		err |= !ao2_link(snapshots, snapshot);
	}

	return err;
}

struct ao2_container *stasis_cache_get_all(struct stasis_cache *cache, struct stasis_message_type *type, const char *id)
{
	struct stasis_cache_entry *cached_entry;
	struct ao2_container *found;

	ast_assert(cache != NULL);
	ast_assert(cache->entries != NULL);
	ast_assert(id != NULL);

	if (!type) {
		return NULL;
	}

	found = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
	if (!found) {
		return NULL;
	}

	ao2_rdlock(cache->entries);

	cached_entry = cache_find(cache->entries, type, id);
	if (cached_entry && cache_entry_dump(found, cached_entry)) {
		ao2_cleanup(found);
		found = NULL;
	}

	ao2_unlock(cache->entries);

	ao2_cleanup(cached_entry);
	return found;
}

/*!
 * \internal
 * \brief Retrieve an item from the cache entry for a specific eid.
 *
 * \param entry Cache entry to use.
 * \param eid Specific entity id to retrieve.  NULL for aggregate.
 *
 * \note The returned snapshot has not had its reference bumped.
 *
 * \retval Snapshot from the cache.
 * \retval \c NULL if snapshot is not found.
 */
static struct stasis_message *cache_entry_by_eid(const struct stasis_cache_entry *entry, const struct ast_eid *eid)
{
	int is_remote;
	int idx;

	if (!eid) {
		/* Get aggregate. */
		return entry->aggregate;
	}

	/* Get snapshot with specific eid. */
	is_remote = ast_eid_cmp(eid, &ast_eid_default);
	if (!is_remote) {
		return entry->local;
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&entry->remote); ++idx) {
		struct stasis_message *cur;

		cur = AST_VECTOR_GET(&entry->remote, idx);
		if (!ast_eid_cmp(eid, stasis_message_eid(cur))) {
			return cur;
		}
	}

	return NULL;
}

struct stasis_message *stasis_cache_get_by_eid(struct stasis_cache *cache, struct stasis_message_type *type, const char *id, const struct ast_eid *eid)
{
	struct stasis_cache_entry *cached_entry;
	struct stasis_message *snapshot = NULL;

	ast_assert(cache != NULL);
	ast_assert(cache->entries != NULL);
	ast_assert(id != NULL);

	if (!type) {
		return NULL;
	}

	ao2_rdlock(cache->entries);

	cached_entry = cache_find(cache->entries, type, id);
	if (cached_entry) {
		snapshot = cache_entry_by_eid(cached_entry, eid);
		ao2_bump(snapshot);
	}

	ao2_unlock(cache->entries);

	ao2_cleanup(cached_entry);
	return snapshot;
}

struct stasis_message *stasis_cache_get(struct stasis_cache *cache, struct stasis_message_type *type, const char *id)
{
	return stasis_cache_get_by_eid(cache, type, id, &ast_eid_default);
}

struct cache_dump_data {
	struct ao2_container *container;
	struct stasis_message_type *type;
	const struct ast_eid *eid;
};

static int cache_dump_by_eid_cb(void *obj, void *arg, int flags)
{
	struct cache_dump_data *cache_dump = arg;
	struct stasis_cache_entry *entry = obj;

	if (!cache_dump->type || entry->key.type == cache_dump->type) {
		struct stasis_message *snapshot;

		snapshot = cache_entry_by_eid(entry, cache_dump->eid);
		if (snapshot) {
			if (!ao2_link(cache_dump->container, snapshot)) {
				ao2_cleanup(cache_dump->container);
				cache_dump->container = NULL;
				return CMP_STOP;
			}
		}
	}

	return 0;
}

struct ao2_container *stasis_cache_dump_by_eid(struct stasis_cache *cache, struct stasis_message_type *type, const struct ast_eid *eid)
{
	struct cache_dump_data cache_dump;

	ast_assert(cache != NULL);
	ast_assert(cache->entries != NULL);

	cache_dump.eid = eid;
	cache_dump.type = type;
	cache_dump.container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
	if (!cache_dump.container) {
		return NULL;
	}

	ao2_callback(cache->entries, OBJ_MULTIPLE | OBJ_NODATA, cache_dump_by_eid_cb, &cache_dump);
	return cache_dump.container;
}

struct ao2_container *stasis_cache_dump(struct stasis_cache *cache, struct stasis_message_type *type)
{
	return stasis_cache_dump_by_eid(cache, type, &ast_eid_default);
}

static int cache_dump_all_cb(void *obj, void *arg, int flags)
{
	struct cache_dump_data *cache_dump = arg;
	struct stasis_cache_entry *entry = obj;

	if (!cache_dump->type || entry->key.type == cache_dump->type) {
		if (cache_entry_dump(cache_dump->container, entry)) {
			ao2_cleanup(cache_dump->container);
			cache_dump->container = NULL;
			return CMP_STOP;
		}
	}

	return 0;
}

struct ao2_container *stasis_cache_dump_all(struct stasis_cache *cache, struct stasis_message_type *type)
{
	struct cache_dump_data cache_dump;

	ast_assert(cache != NULL);
	ast_assert(cache->entries != NULL);

	cache_dump.eid = NULL;
	cache_dump.type = type;
	cache_dump.container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
	if (!cache_dump.container) {
		return NULL;
	}

	ao2_callback(cache->entries, OBJ_MULTIPLE | OBJ_NODATA, cache_dump_all_cb, &cache_dump);
	return cache_dump.container;
}

STASIS_MESSAGE_TYPE_DEFN(stasis_cache_clear_type);
STASIS_MESSAGE_TYPE_DEFN(stasis_cache_update_type);

struct stasis_message *stasis_cache_clear_create(struct stasis_message *id_message)
{
	return stasis_message_create(stasis_cache_clear_type(), id_message);
}

static void stasis_cache_update_dtor(void *obj)
{
	struct stasis_cache_update *update = obj;

	ao2_cleanup(update->old_snapshot);
	update->old_snapshot = NULL;
	ao2_cleanup(update->new_snapshot);
	update->new_snapshot = NULL;
	ao2_cleanup(update->type);
	update->type = NULL;
}

static struct stasis_message *update_create(struct stasis_message *old_snapshot, struct stasis_message *new_snapshot)
{
	struct stasis_cache_update *update;
	struct stasis_message *msg;

	ast_assert(old_snapshot != NULL || new_snapshot != NULL);

	if (!stasis_cache_update_type()) {
		return NULL;
	}

	update = ao2_alloc_options(sizeof(*update), stasis_cache_update_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!update) {
		return NULL;
	}

	if (old_snapshot) {
		ao2_ref(old_snapshot, +1);
		update->old_snapshot = old_snapshot;
		if (!new_snapshot) {
			ao2_ref(stasis_message_type(old_snapshot), +1);
			update->type = stasis_message_type(old_snapshot);
		}
	}
	if (new_snapshot) {
		ao2_ref(new_snapshot, +1);
		update->new_snapshot = new_snapshot;
		ao2_ref(stasis_message_type(new_snapshot), +1);
		update->type = stasis_message_type(new_snapshot);
	}

	msg = stasis_message_create(stasis_cache_update_type(), update);

	ao2_cleanup(update);
	return msg;
}

static void caching_topic_exec(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_caching_topic *caching_topic_needs_unref;
	struct stasis_caching_topic *caching_topic = data;
	struct stasis_message *msg;
	struct stasis_message *msg_put;
	struct stasis_message_type *msg_type;
	const struct ast_eid *msg_eid;
	const char *msg_id;

	ast_assert(caching_topic != NULL);
	ast_assert(caching_topic->topic != NULL);
	ast_assert(caching_topic->cache != NULL);
	ast_assert(caching_topic->cache->id_fn != NULL);

	if (stasis_subscription_final_message(sub, message)) {
		caching_topic_needs_unref = caching_topic;
	} else {
		caching_topic_needs_unref = NULL;
	}

	msg_type = stasis_message_type(message);
	if (stasis_cache_clear_type() == msg_type) {
		/* Cache clear event. */
		msg_put = NULL;
		msg = stasis_message_data(message);
		msg_type = stasis_message_type(msg);
	} else {
		/* Normal cache update event. */
		msg_put = message;
		msg = message;
	}
	ast_assert(msg_type != NULL);

	msg_eid = stasis_message_eid(msg);/* msg_eid is NULL for aggregate message. */
	msg_id = caching_topic->cache->id_fn(msg);
	if (msg_id && msg_eid) {
		struct stasis_message *update;
		struct cache_put_snapshots snapshots;

		/* Update the cache */
		snapshots = cache_put(caching_topic->cache, msg_type, msg_id, msg_eid, msg_put);
		if (snapshots.old || msg_put) {
			update = update_create(snapshots.old, msg_put);
			if (update) {
				stasis_publish(caching_topic->topic, update);
			}
			ao2_cleanup(update);
		} else {
			ast_log(LOG_ERROR,
				"Attempting to remove an item from the %s cache that isn't there: %s %s\n",
				stasis_topic_name(caching_topic->topic),
				stasis_message_type_name(msg_type), msg_id);
		}

		if (snapshots.aggregate_old != snapshots.aggregate_new) {
			if (snapshots.aggregate_new && caching_topic->cache->aggregate_publish_fn) {
				caching_topic->cache->aggregate_publish_fn(caching_topic->original_topic,
					snapshots.aggregate_new);
			}
			update = update_create(snapshots.aggregate_old, snapshots.aggregate_new);
			if (update) {
				stasis_publish(caching_topic->topic, update);
			}
			ao2_cleanup(update);
		}

		ao2_cleanup(snapshots.old);
		ao2_cleanup(snapshots.aggregate_old);
		ao2_cleanup(snapshots.aggregate_new);
	}

	ao2_cleanup(caching_topic_needs_unref);
}

struct stasis_caching_topic *stasis_caching_topic_create(struct stasis_topic *original_topic, struct stasis_cache *cache)
{
	RAII_VAR(struct stasis_caching_topic *, caching_topic, NULL, ao2_cleanup);
	struct stasis_subscription *sub;
	RAII_VAR(char *, new_name, NULL, ast_free);
	int ret;

	ret = ast_asprintf(&new_name, "%s-cached", stasis_topic_name(original_topic));
	if (ret < 0) {
		return NULL;
	}

	caching_topic = ao2_alloc_options(sizeof(*caching_topic),
		stasis_caching_topic_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (caching_topic == NULL) {
		return NULL;
	}

	caching_topic->topic = stasis_topic_create(new_name);
	if (caching_topic->topic == NULL) {
		return NULL;
	}

	ao2_ref(cache, +1);
	caching_topic->cache = cache;

	sub = internal_stasis_subscribe(original_topic, caching_topic_exec, caching_topic, 0, 0);
	if (sub == NULL) {
		return NULL;
	}

	ao2_ref(original_topic, +1);
	caching_topic->original_topic = original_topic;

	/* This is for the reference contained in the subscription above */
	ao2_ref(caching_topic, +1);
	caching_topic->sub = sub;

	/* The subscription holds the reference, so no additional ref bump. */
	return caching_topic;
}

static void stasis_cache_cleanup(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_cache_clear_type);
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_cache_update_type);
}

int stasis_cache_init(void)
{
	ast_register_cleanup(stasis_cache_cleanup);

	if (STASIS_MESSAGE_TYPE_INIT(stasis_cache_clear_type) != 0) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(stasis_cache_update_type) != 0) {
		return -1;
	}

	return 0;
}

