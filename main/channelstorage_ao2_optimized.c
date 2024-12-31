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

#include "asterisk/channel.h"
#include "asterisk/astobj2.h"
#include "channelstorage.h"
#include "channel_private.h"

struct ast_channelstorage_driver_pvt {
	struct ao2_container *handle;
};

#define getdb(driver) (driver->handle->handle)

static void rdlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_rdlock(driver->lock_handle);
}

static void wrlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_wrlock(driver->lock_handle);
}

static void unlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_unlock(driver->lock_handle);
}

static int insert_channel(struct ast_channelstorage_instance *driver,
	struct ast_channel *chan, int flags, int lock)
{
	int ret = 0;
	if (lock) {
		wrlock(driver);
	}

	ret = ao2_link_flags(getdb(driver), chan, OBJ_NOLOCK | flags);
	if (ret == 1) {
		chan->linked_in_container = 1;
	}

	if (lock) {
		unlock(driver);
	}
	return ret ? 0 : -1;
}

static int delete_channel(struct ast_channelstorage_instance *driver,
	struct ast_channel *chan, int lock)
{
	if (lock) {
		wrlock(driver);
	}
	ao2_unlink(getdb(driver), chan);
	chan->linked_in_container = 0;

	if (lock) {
		unlock(driver);
	}
	return 0;
}

/*! \brief returns number of active/allocated channels */
static int active_channels(struct ast_channelstorage_instance *driver)
{
	return getdb(driver) ? ao2_container_count(getdb(driver)) : 0;
}

static struct ast_channel *callback(struct ast_channelstorage_instance *driver,
	ao2_callback_data_fn *cb_fn, void *arg, void *data, int ao2_flags)
{
	struct ast_channel *chan;
	rdlock(driver);
	chan = ao2_callback_data(getdb(driver), ao2_flags, cb_fn, arg, data);
	unlock(driver);
	return chan;
}

struct ast_channel_iterator {
	/* storage for non-dynamically allocated iterator */
	struct ao2_iterator simple_iterator;
	/* pointer to the actual iterator (simple_iterator or a dynamically
	 * allocated iterator)
	 */
	struct ao2_iterator *active_iterator;
};

static struct ast_channel_iterator *iterator_destroy(struct ast_channelstorage_instance *driver,
	struct ast_channel_iterator *i)
{
	ao2_iterator_destroy(i->active_iterator);
	ast_free(i);

	return NULL;
}

static struct ast_channel_iterator *iterator_by_exten_new(struct ast_channelstorage_instance *driver,
	const char *exten, const char *context)
{
	struct ast_channel_iterator *i;
	char *l_exten = (char *) exten;
	char *l_context = (char *) context;

	if (ast_strlen_zero(exten) || ast_strlen_zero(context)) {
		ast_log(LOG_ERROR, "BUG! Must have a context and extension to match!\n");
		return NULL;
	}

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->active_iterator = (void *) callback(driver, channelstorage_exten_cb,
		l_context, l_exten, OBJ_MULTIPLE);
	if (!i->active_iterator) {
		ast_free(i);
		return NULL;
	}

	return i;
}

static struct ast_channel_iterator *iterator_by_name_new(struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len)
{
	struct ast_channel_iterator *i;
	char *l_name = (char *) name;

	if (ast_strlen_zero(name)) {
		ast_log(LOG_ERROR, "BUG! Must supply a channel name or partial name to match!\n");
		return NULL;
	}

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->active_iterator = (void *) callback(driver, channelstorage_name_cb,
		l_name, &name_len,
		OBJ_MULTIPLE | (name_len == 0 /* match the whole word, so optimize */ ? OBJ_KEY : 0));
	if (!i->active_iterator) {
		ast_free(i);
		return NULL;
	}

	return i;
}

static struct ast_channel_iterator *iterator_all_new(struct ast_channelstorage_instance *driver)
{
	struct ast_channel_iterator *i;

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->simple_iterator = ao2_iterator_init(getdb(driver), 0);
	i->active_iterator = &i->simple_iterator;

	return i;
}

static struct ast_channel *iterator_next(struct ast_channelstorage_instance *driver,
	struct ast_channel_iterator *i)
{
	return ao2_iterator_next(i->active_iterator);
}

static struct ast_channel *get_by_name_exact(struct ast_channelstorage_instance *driver,
	const char *name)
{
	struct ast_channel *chan = NULL;
	rdlock(driver);
	chan = ao2_find(getdb(driver), name, OBJ_SEARCH_KEY);
	unlock(driver);
	return chan;
}

static struct ast_channel *get_by_name_prefix(struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len)
{
	struct ast_channel *chan = NULL;
	char *l_name = NULL;

	if (name_len == 0) {
		chan = get_by_name_exact(driver, name);
		return chan;
	}

	l_name = (char *)name;
	chan = callback(driver, channelstorage_name_cb, l_name, &name_len, 0);

	return chan;
}

/*!
 * \internal
 * \brief Print channel object key (name).
 * \since 12.0.0
 *
 * \param v_obj A pointer to the object we want the key printed.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 */
static void prnt_channel_key(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct ast_channel *chan = v_obj;

	if (!chan) {
		return;
	}
	prnt(where, "%s", ast_channel_name(chan));
}

static void close_instance(struct ast_channelstorage_instance *driver)
{
	ast_debug(1, "Closing ao2_container channel storage driver %s\n", driver ? driver->name : "NULL");
	if (!driver) {
		return;
	}

	if (driver->handle) {
		if (getdb(driver)) {
			ao2_container_unregister(driver->name);
			ao2_ref(getdb(driver), -1);
			getdb(driver) = NULL;
		}
		ast_free(driver->handle);
		driver->handle = NULL;
	}
	ast_free(driver->lock_handle);
	driver->lock_handle = NULL;
	ast_free(driver);
}

static struct ast_channelstorage_instance channelstorage_instance = {
	.handle = NULL,
	.lock_handle = NULL,
	.close = close_instance,
	.insert = insert_channel,
	.remove = delete_channel,
	.rdlock = rdlock,
	.wrlock = wrlock,
	.unlock = unlock,
	.active_channels = active_channels,
	.callback = callback,
	.get_by_name_prefix = get_by_name_prefix,
	.get_by_name_prefix_or_uniqueid = channelstorage_by_name_prefix_or_uniqueid,
	.get_by_exten = channelstorage_by_exten,
	.get_by_uniqueid = channelstorage_by_uniqueid,
	.iterator_all_new = iterator_all_new,
	.iterator_by_name_new = iterator_by_name_new,
	.iterator_by_exten_new = iterator_by_exten_new,
	.iterator_next = iterator_next,
	.iterator_destroy = iterator_destroy,
};

static int channel_cmp_cb(void *obj_left, void *obj_right, int flags)
{
	struct ast_channel *tps_left = obj_left;
	struct ast_channel *tps_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	default:
	case OBJ_SEARCH_OBJECT:
		right_key = ast_channel_name(tps_right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(ast_channel_name(tps_left), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncasecmp(ast_channel_name(tps_left), right_key, strlen(right_key));
		break;
	}
	return cmp == 0 ? CMP_MATCH : 0;
}

static int hash_cb(const void *obj, const int flags)
{
	const char *name = (flags & OBJ_KEY) ? obj : ast_channel_name((struct ast_channel *) obj);

	/* If the name isn't set, return 0 so that the ao2_find() search will
	 * start in the first bucket. */
	if (ast_strlen_zero(name)) {
		return 0;
	}

	return ast_str_case_hash(name);
}

static struct ast_channelstorage_instance* get_instance(const char *name)
{
	const char *_name = name ? name : "default";
	struct ast_channelstorage_instance* driver = ast_calloc(1,
		sizeof(*driver) + strlen(_name) + 1);

	ast_debug(1, "Opening channel storage driver %s\n", _name);

	if (!driver) {
		ast_log(LOG_ERROR, "Failed to allocate memory for channel storage driver %s\n",
			_name);
		return NULL;
	}
	memcpy(driver, &channelstorage_instance, sizeof(*driver));
	strcpy(driver->name, _name); /* Safe */
	driver->handle = ast_calloc(1, sizeof(*driver->handle));
	if (!driver->handle) {
		close_instance(driver);
		ast_log(LOG_ERROR, "Failed to allocate memory for channel storage driver %s\n",
			_name);
		return NULL;
	}

	getdb(driver) = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		AST_NUM_CHANNEL_BUCKETS, hash_cb, NULL, channel_cmp_cb);

	if (!driver->handle) {
		ast_log(LOG_ERROR, "Failed to create channel storage driver %s\n",
			_name);
		close_instance(driver);
		return NULL;
	}
	driver->lock_handle = ast_calloc(1, sizeof(ast_rwlock_t));
	if (!driver->lock_handle) {
		ast_log(LOG_ERROR, "Failed to create container lock for channel storage driver %s\n",
			_name);
		close_instance(driver);
		return NULL;
	}
	ast_rwlock_init(driver->lock_handle);

	ao2_container_register(name, getdb(driver), prnt_channel_key);
	ast_debug(1, "Opened channel storage driver %s. driver: %p  container: %p\n",
		_name, driver, driver->handle);

	return driver;
}

static struct ast_channelstorage_driver driver_type = {
	.driver_name = "ao2_optimized",
	.open = get_instance,
};

static void __attribute__((constructor)) __startup(void)
{
	ast_channelstorage_register_driver(&driver_type);
}


