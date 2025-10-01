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

#include <memory>
#include <string>
#include <map>
#include <cassert>
#include <utility>

#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "channelstorage.h"
#include "channel_private.h"

typedef std::map<std::string, struct ast_channel *> ChannelMap;

struct mni_channelstorage_driver_pvt {
	ChannelMap by_name;
	ChannelMap by_uniqueid;
};

#define getdb(driver) (((struct mni_channelstorage_driver_pvt *)driver->handle)->by_name)
#define map_by_id(driver) (((struct mni_channelstorage_driver_pvt *)driver->handle)->by_uniqueid)

static void rdlock(struct ast_channelstorage_instance *driver)
{
	if (!driver || !driver->lock_handle) {
		return;
	}
	ast_rwlock_rdlock((ast_rwlock_t*)driver->lock_handle);
}

static void wrlock(struct ast_channelstorage_instance *driver)
{
	if (!driver || !driver->lock_handle) {
		return;
	}
	ast_rwlock_wrlock((ast_rwlock_t*)driver->lock_handle);
}

static void unlock(struct ast_channelstorage_instance *driver)
{
	if (!driver || !driver->lock_handle) {
		return;
	}
	ast_rwlock_unlock((ast_rwlock_t*)driver->lock_handle);
}

static int insert_channel(struct ast_channelstorage_instance *driver,
	struct ast_channel *chan, int flags, int lock)
{
	char *l_name = NULL;
	char *l_uniqueid = NULL;
	bool success = false;
	if (!chan) {
		return -1;
	}

	if (lock) {
		wrlock(driver);
	}
	l_name = ast_str_to_lower(ast_strdupa(ast_channel_name(chan)));
	l_uniqueid = ast_str_to_lower(ast_strdupa(ast_channel_uniqueid(chan)));

	auto rtn = getdb(driver).emplace(l_name, ao2_bump(chan));
	if (rtn.second) {
		rtn = map_by_id(driver).emplace(l_uniqueid, ao2_bump(chan));
		if (!rtn.second) {
			ast_log(LOG_ERROR, "Unable to insert channel '%s' '%s'\n",
				ast_channel_name(chan), ast_channel_uniqueid(chan));
			ast_channel_unref(chan);
			getdb(driver).erase(l_name);
			ast_channel_unref(chan);
		}
		success = rtn.second;
	} else {
		ast_log(LOG_ERROR, "Unable to insert channel '%s'\n", ast_channel_name(chan));
		ast_channel_unref(chan);
	}

	if (success) {
		chan->linked_in_container = 1;
	}
	if (lock) {
		unlock(driver);
	}
	return success ? 0 : -1;
}

static int delete_channel(struct ast_channelstorage_instance *driver,
	struct ast_channel *chan, int lock)
{
	char *l_name = NULL;
	char *l_uniqueid = NULL;
	if (!chan) {
		return -1;
	}

	if (!chan->linked_in_container) {
		return 0;
	}

	if (lock) {
		wrlock(driver);
	}

	l_name = ast_str_to_lower(ast_strdupa(ast_channel_name(chan)));
	l_uniqueid = ast_str_to_lower(ast_strdupa(ast_channel_uniqueid(chan)));

	auto deleted = getdb(driver).erase(l_name);
	if (deleted) {
		ast_channel_unref(chan);
	}
	deleted = map_by_id(driver).erase(l_uniqueid);
	if (deleted) {
		ast_channel_unref(chan);
	}
	chan->linked_in_container = 0;

	if (lock) {
		unlock(driver);
	}
	return 0;
}

/*! \brief returns number of active/allocated channels */
static int active_channels(struct ast_channelstorage_instance *driver)
{
	int count = 0;

	if (!driver) {
		return 0;
	}

	rdlock(driver);
	count = getdb(driver).size();
	unlock(driver);

	return count;
}

static struct ast_channel *callback(struct ast_channelstorage_instance *driver,
	ao2_callback_data_fn *cb_fn, void *arg, void *data, int ao2_flags)
{
	struct ast_channel *chan = NULL;
	ChannelMap::const_iterator it;

	if (!cb_fn) {
		return NULL;
	}

	rdlock(driver);
	for (it = getdb(driver).begin(); it != getdb(driver).end(); it++) {
		chan = it->second;
		if (cb_fn(chan, arg, data, ao2_flags) == (CMP_MATCH | CMP_STOP)) {
			ao2_bump(chan);
			unlock(driver);
			return chan;
		}
	}
	unlock(driver);

	return NULL;
}

enum cpp_map_iterator_type {
	ITERATOR_ALL,
	ITERATOR_BY_NAME,
	ITERATOR_BY_EXTEN,
};

struct mni_channel_iterator {
	enum cpp_map_iterator_type it_type;
	std::string l_name;
	size_t l_name_len;
	char *context;
	char *exten;
	std::string last_channel;
	int counter;

	mni_channel_iterator() :
		it_type(ITERATOR_ALL), l_name(""), l_name_len(0),
		context(NULL), exten(NULL),
		last_channel(""), counter(0)
	{
	}

	mni_channel_iterator(const char *l_name) :
		it_type(ITERATOR_BY_NAME), l_name(l_name), l_name_len(strlen(l_name)),
		context(NULL), exten(NULL),
		last_channel(""), counter(0)
	{
	}

	mni_channel_iterator(const char *context, const char *exten) :
		it_type(ITERATOR_BY_EXTEN), l_name(""), l_name_len(0),
		context(ast_strdup(context)), exten(ast_strdup(exten)),
		last_channel(""), counter(0)
	{
	}

	~mni_channel_iterator()
	{
		ast_free(context);
		ast_free(exten);
		context = NULL;
		exten = NULL;
		l_name.clear();
		last_channel.clear();
		counter = 0;
	}
};

static struct ast_channel_iterator *iterator_destroy(struct ast_channelstorage_instance *driver,
	struct ast_channel_iterator *ai)
{
	struct mni_channel_iterator *i = (struct mni_channel_iterator *)ai;
	if (!driver || !i) {
		return NULL;
	}
	delete i;
	return NULL;
}

/*!
 * \internal
 * \brief Create a new iterator for all channels
 *
 * No I/O is done at this time.  It's simply allocating the iterator
 * structure and initializing it.
 *
 * \return struct mni_channel_iterator *
 */
static struct ast_channel_iterator *iterator_all_new(struct ast_channelstorage_instance *driver)
{
	struct mni_channel_iterator *i = new mni_channel_iterator();
	if (!i) {
		return NULL;
	}

	return (struct ast_channel_iterator *)i;
}

/*!
 * \internal
 * \brief Retrieve the next channel in the iterator.
 *
 * This function retrieves the next channel in the iterator, based on the
 * type of iterator it is. If there are no more channels, it returns NULL.
 *
 * In a single-threaded environment, we'd simply use the std::map
 * begin(), end(), lower_bound() and upper_bound() functions and use
 * standard iterator operations to move through the map.  This doesn't
 * work well in a multi-threaded environment where deletes can happen
 * in another thread because if you delete the object an iterator points
 * to, it becomes invalid and there's no way to test that.  If you try
 * to access or operate on that iterator (like incrementing it), the
 * result will be a SEGV or other undefined behavior.
 *
 * app_chanspy is particularly prone to triggering this issue because
 * it opens an iterator and keeps it open for a long period of time
 * looking for channels to spy on.
 *
 * The solution is to use a C++ iterator to find the next (or first)
 * channel then save that channel's key in our iterator structure to
 * use as the starting point the next time iterator_next() is called.
 * We also put a read lock on the driver to prevent a driver from
 * deleting a channel in the short time we use it.  We NEVER keep
 * C++ iterators across multiple calls to iterator_next().
 *
 * This sounds inefficient but in practice, it works very well
 * because the C++ map is implemented as a red-black tree.  This
 * makes calling lower_bound() very efficient.  Besides, even with
 * this approach, the iterators are still at least an order of
 * magnitude, and sometimes two orders, faster than the ao2_legacy
 * driver. To check the results for yourself, build in development
 * mode and run "test execute category /main/channelstorage/"
 * from the CLI.
 *
 * \return struct ast_channel * or NULL
 */

static struct ast_channel *iterator_next(struct ast_channelstorage_instance *driver,
	struct ast_channel_iterator *ai)
{
	struct mni_channel_iterator *i = (struct mni_channel_iterator *)ai;
	struct ast_channel *chan = NULL;
	ChannelMap::const_iterator it;

	if (!driver || !i) {
		return NULL;
	}

	i->counter++;
	rdlock(driver);

	if (i->counter == 1) {
		/*
		 * When this is the first call to iterator_next(),
		 * lower_bound(i->l_name) will return the first
		 * channel in the map if i->l_name is empty
		 * (ITERATOR_ALL and ITERATOR_BY_EXTEN) or the
		 * first channel whose name starts with i->l_name
		 * (ITERATOR_BY_NAME).  This is exactly what we want.
		 */
		it = getdb(driver).lower_bound(i->l_name);
	} else {
		/*
		 * When this is not the first call to iterator_next(),
		 * we want to return the next channel after the last
		 * channel returned.  We can do this by using the
		 * last_channel key stored in the iterator to get
		 * an iterator to directly to it, then advancing it.
		 * It's possible that last_channel was actually the
		 * last channel in the map and was deleted between the
		 * last call to iterator_next() and now so we need to
		 * check that it's still around before we try to advance it.
		 */
		it = getdb(driver).lower_bound(i->last_channel);
		if (it == getdb(driver).end()) {
			unlock(driver);
			return NULL;
		}
		std::advance(it, 1);
	}

	/*
	 * Whether this is the first call to iterator_next() or
	 * a subsequent call, if we reached the end of the map,
	 * return NULL.
	 */
	if (it == getdb(driver).end()) {
		unlock(driver);
		return NULL;
	}

	if (i->it_type == ITERATOR_ALL) {
		/*
		 * The simplest case. Save the channel key to last_channel
		 * and bump and return the channel.
		 */
		i->last_channel = it->first;
		chan = ao2_bump(it->second);

	} else if (i->it_type == ITERATOR_BY_NAME) {
		/*
		 * If this was a search by name, we need to check that
		 * the channel key still matches the name being searched for.
		 * If it does, save the channel key to last_channel and bump
		 * and return the channel.
		 * If it doesn't match, we're done because the map is sorted
		 * by channel name so any further channels in the map won't
		 * match either.
		 */
		if (it->first.substr(0, i->l_name_len) == i->l_name) {
			i->last_channel = it->first;
			chan = ao2_bump(it->second);
		}

	} else if (i->it_type == ITERATOR_BY_EXTEN) {
		/*
		 * Searching by context and extension is a bit more complex.
		 * Every time iterator_next() is called, we need to search for
		 * matching context and extension from the last_channel forward
		 * to the end of the map.  It's f'ugly and we have to hold
		 * the read lock while we traverse but it works, it's safe,
		 * and it's STILL better than the ao2_legacy driver albeit not
		 * by much.
		 */
		while (it != getdb(driver).end()) {
			int ret = channelstorage_exten_cb(it->second, i->context, i->exten, 0);
			if (ret & CMP_MATCH) {
				i->last_channel = it->first;
				chan = ao2_bump(it->second);
				break;
			}
			std::advance(it, 1);
		}
	} else {
		ast_log(LOG_ERROR, "Unknown iterator type %d\n", i->it_type);
	}
	unlock(driver);

	return chan;
}

/*!
 * \internal
 * \brief Create a new iterator for retrieving all channels matching
 * a specific name prefix. A full channel name can be supplied but calling
 * get_by_name_exact() is more efficient for that.
 *
 * No I/O is done at this time.  It's simply allocating the iterator
 * structure and initializing it.
 *
 * \return struct mni_channel_iterator *
 */
static struct ast_channel_iterator *iterator_by_name_new(
	struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len)
{
	char *l_name = NULL;
	struct mni_channel_iterator *i;

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	l_name = ast_str_to_lower(ast_strdupa(name));
	if (name_len == 0) {
		name_len = strlen(name);
	}
	l_name[name_len] = '\0';

	i = new mni_channel_iterator(l_name);
	if (!i) {
		return NULL;
	}

	return (struct ast_channel_iterator *)i;
}

/*!
 * \internal
 * \brief Create a new iterator for retrieving all channels
 * matching a specific context and optionally exten.
 *
 * No I/O is done at this time.  It's simply allocating the iterator
 * structure and initializing it.
 *
 * \return struct mni_channel_iterator *
 */
static struct ast_channel_iterator *iterator_by_exten_new(struct ast_channelstorage_instance *driver,
	const char *exten, const char *context)
{
	struct mni_channel_iterator *i = new mni_channel_iterator(
		ast_str_to_lower(ast_strdupa(context)),
		ast_str_to_lower(ast_strdupa(exten)));

	if (!i) {
		return NULL;
	}

	return (struct ast_channel_iterator *)i;
}

static struct ast_channel *get_by_uniqueid(struct ast_channelstorage_instance *driver,
	const char *uniqueid)
{
	struct ast_channel *chan = NULL;
	char *search = uniqueid ? ast_str_to_lower(ast_strdupa(uniqueid)) : NULL;

	if (ast_strlen_zero(uniqueid)) {
		return NULL;
	}

	rdlock(driver);
	auto rtn = map_by_id(driver).find(search);
	if (rtn != map_by_id(driver).end()) {
		chan = ao2_bump((struct ast_channel *)rtn->second);
	}
	unlock(driver);

	return chan;
}

static struct ast_channel *get_by_name_exact(struct ast_channelstorage_instance *driver,
	const char *name)
{
	struct ast_channel *chan = NULL;
	char *search = name ? ast_str_to_lower(ast_strdupa(name)) : NULL;

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	rdlock(driver);
	auto rtn = getdb(driver).find(search);
	if (rtn != getdb(driver).end()) {
		chan = ao2_bump((struct ast_channel *)rtn->second);
	}
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

	l_name = ast_str_to_lower(ast_strdupa(name));

	rdlock(driver);
	auto rtn = getdb(driver).lower_bound(l_name);
	if (rtn != getdb(driver).end()) {
		chan = ao2_bump((struct ast_channel *)rtn->second);
	}
	unlock(driver);

	return chan;
}


static void close_instance(struct ast_channelstorage_instance *driver)
{
	ast_debug(1, "Closing channel storage driver %s\n", driver ? driver->name : "NULL");
	if (!driver) {
		return;
	}

	if (driver->handle) {
		delete (struct mni_channelstorage_driver_pvt *)driver->handle;
		driver->handle = NULL;
	}
	ast_free(driver->lock_handle);
	driver->lock_handle = NULL;
	ast_free(driver);
}

static struct ast_channelstorage_instance channelstorage_instance = {
	.handle = NULL,
	.lock_handle = NULL,
	.close_instance = close_instance,
	.insert = insert_channel,
	.remove = delete_channel,
	.rdlock = rdlock,
	.wrlock = wrlock,
	.unlock = unlock,
	.active_channels = active_channels,
	.callback = callback,
	.get_by_name_prefix= get_by_name_prefix,
	.get_by_name_prefix_or_uniqueid = channelstorage_by_name_prefix_or_uniqueid,
	.get_by_exten = channelstorage_by_exten,
	.get_by_uniqueid = get_by_uniqueid,
	.iterator_all_new = iterator_all_new,
	.iterator_by_exten_new = iterator_by_exten_new,
	.iterator_by_name_new = iterator_by_name_new,
	.iterator_next = iterator_next,
	.iterator_destroy = iterator_destroy,
};

static struct ast_channelstorage_instance* get_instance(const char *name)
{
	const char *_name = name ? name : "default";
	struct ast_channelstorage_instance* driver =
		(struct ast_channelstorage_instance*)ast_calloc(1,
		sizeof(*driver) + strlen(_name) + 1);

	ast_debug(1, "Opening channel storage driver %s\n", _name);

	if (!driver) {
		ast_log(LOG_ERROR, "Failed to allocate memory for channel storage driver %s\n",
			_name);
		return NULL;
	}
	memcpy(driver, &channelstorage_instance, sizeof(*driver));
	strcpy(driver->name, _name); /* Safe */

	driver->handle = (struct ast_channelstorage_driver_pvt *)new mni_channelstorage_driver_pvt();

	if (!driver->handle) {
		ast_log(LOG_ERROR, "Failed to create channel storage driver %s\n",
			_name);
		ast_free(driver);
		return NULL;
	}
	driver->lock_handle = ast_calloc(1, sizeof(ast_rwlock_t));
	if (!driver->lock_handle) {
		ast_log(LOG_ERROR, "Failed to create container lock for channel storage driver %s\n",
			_name);
		close_instance(driver);
		return NULL;
	}
	ast_rwlock_init((ast_rwlock_t *)driver->lock_handle);

	return driver;
}

static struct ast_channelstorage_driver driver_type = {
	.driver_name = "cpp_map_name_id",
	.open_instance = get_instance,
};

static void __attribute__((constructor)) __startup(void)
{
	ast_channelstorage_register_driver(&driver_type);
}
