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

#if !defined(NDEBUG)
#define BOOST_MULTI_INDEX_ENABLE_INVARIANT_CHECKING
#define BOOST_MULTI_INDEX_ENABLE_SAFE_MODE
#endif

#include <boost/unordered/unordered_map.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/functional/hash.hpp>

#include <memory>
#include <string>
#include <map>
#include <cassert>
#include <algorithm>
#include <iterator>

#include "asterisk.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "channelstorage.h"
#include "channel_private.h"

typedef boost::unordered_map<std::string, struct ast_channel *> ChannelMap;

struct bn_channelstorage_driver_pvt {
	ChannelMap by_name;
};

#define getdb(driver) (((struct bn_channelstorage_driver_pvt *)driver->handle)->by_name)

static void rdlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_rdlock((ast_rwlock_t*)driver->lock_handle);
}

static void wrlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_wrlock((ast_rwlock_t*)driver->lock_handle);
}

static void unlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_unlock((ast_rwlock_t*)driver->lock_handle);
}

static int insert_channel(struct ast_channelstorage_instance *driver,
	struct ast_channel *chan, int flags, int lock)
{
	char *l_name = chan ? ast_str_to_lower(ast_strdupa(ast_channel_name(chan))) : NULL;
	if (!chan) {
		return -1;
	}

	if (lock) {
		wrlock(driver);
	}

	const auto rtn = getdb(driver).emplace(l_name, chan);

	if (!rtn.second) {
		ast_log(LOG_ERROR, "Unable to insert channel duplicate channel '%s'\n", ast_channel_name(chan));
	} else {
		chan->linked_in_container = 1;
	}

	if (lock) {
		unlock(driver);
	}
	return rtn.second ? 0 : -1;
}

static int delete_channel(struct ast_channelstorage_instance *driver,
	struct ast_channel *chan, int lock)
{
	int ret = 0;
	char *l_name = chan ? ast_str_to_lower(ast_strdupa(ast_channel_name(chan))) : NULL;
	if (!chan) {
		return -1;
	}

	if (lock) {
		wrlock(driver);
	}

	auto count = getdb(driver).erase(l_name);
	if (count) {
		chan->linked_in_container = 0;
		ret = 0;
	} else {
		ast_log(LOG_ERROR, "Unable to find channel '%s'!\n", ast_channel_name(chan));
		ret = -1;
	}
	if (lock) {
		unlock(driver);
	}
	return ret;
}

/*! \brief returns number of active/allocated channels */
static int active_channels(struct ast_channelstorage_instance *driver)
{
	return driver ? getdb(driver).size() : 0;
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
	for (auto it = getdb(driver).begin(); it != getdb(driver).end(); ++it) {
		chan = (struct ast_channel *)(it->second);
		if (cb_fn(chan, arg, data, ao2_flags) == (CMP_MATCH | CMP_STOP)) {
			ao2_bump(chan);
			break;
		}
	}
	unlock(driver);

	return chan;
}

enum cpp_map_iterator_type {
	ITERATOR_ALL,
	ITERATOR_BY_NAME,
	ITERATOR_BY_EXTEN,
};

struct bn_channel_iterator {
	ChannelMap::const_iterator it;
	enum cpp_map_iterator_type it_type;
	char *channel_name = NULL;
	size_t channel_name_len = 0;
	char *context = NULL;
	char *exten = NULL;

	bn_channel_iterator(ChannelMap::const_iterator it, char *name, size_t name_len)
		: it(it), it_type(ITERATOR_BY_NAME), channel_name(name), channel_name_len(name_len),
		context(NULL), exten(NULL)
	{
	}

	bn_channel_iterator(ChannelMap::const_iterator it, char *context, char *exten)
		: it(it), it_type(ITERATOR_BY_EXTEN), channel_name(NULL), channel_name_len(0),
		context(context), exten(exten)
	{
	}

	bn_channel_iterator(ChannelMap::const_iterator it)
		: it(it), it_type(ITERATOR_ALL), channel_name(NULL), channel_name_len(0),
		context(NULL), exten(NULL)
	{
	}

	~bn_channel_iterator()
	{
		ast_free(channel_name);
		ast_free(context);
		ast_free(exten);
	}

};

static struct ast_channel_iterator *iterator_destroy(struct ast_channelstorage_instance *driver,
	struct ast_channel_iterator *i)
{
	delete (struct bn_channel_iterator *)i;
	return NULL;
}

static struct ast_channel_iterator *iterator_all_new(struct ast_channelstorage_instance *driver)
{
	struct bn_channel_iterator *i =
		new bn_channel_iterator(getdb(driver).begin());
	if (!i) {
		return NULL;
	}

	if (i->it == getdb(driver).end()) {
		delete i;
		return NULL;
	}

	return (struct ast_channel_iterator *)i;
}

static struct ast_channel *iterator_next(struct ast_channelstorage_instance *driver,
	struct ast_channel_iterator *ai)
{
	struct bn_channel_iterator *i = (struct bn_channel_iterator *)ai;
	struct ast_channel *chan = NULL;

	if (i->it == getdb(driver).end()) {
		return NULL;
	}

	if (i->it_type == ITERATOR_ALL) {
		chan = ao2_bump((struct ast_channel *)(i->it->second));
		++i->it;
		return chan;
	}

	while (i->it != getdb(driver).end()) {
		if (i->it_type == ITERATOR_BY_NAME) {
			chan = (struct ast_channel *)(i->it->second);
			if (strncmp(i->it->first.c_str(), i->channel_name, i->channel_name_len) == 0) {
				++i->it;
				return ao2_bump(chan);
			}
		} else if (i->it_type == ITERATOR_BY_EXTEN) {
			chan = (struct ast_channel *)(i->it->second);
			int ret = channelstorage_exten_cb(chan, i->context, i->exten, 0);
			if (ret & CMP_MATCH) {
				++i->it;
				return ao2_bump(chan);
			}
		}
		++i->it;
	}
	return NULL;
}

static struct ast_channel_iterator *iterator_by_name_new(struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len)
{
	struct bn_channel_iterator *i =
		new bn_channel_iterator(getdb(driver).begin(),
			ast_str_to_lower(ast_strdup(name)), name_len);
	if (!i) {
		return NULL;
	}

	if (i->it == getdb(driver).end()) {
		ast_free(i->channel_name);
		delete i;
		return NULL;
	}

	return (struct ast_channel_iterator *)i;
}

static struct ast_channel_iterator *iterator_by_exten_new(struct ast_channelstorage_instance *driver,
	const char *exten, const char *context)
{
	struct bn_channel_iterator *i =
		new bn_channel_iterator(getdb(driver).begin(),
			ast_strdup(context), ast_strdup(exten));
	if (!i) {
		return NULL;
	}

	if (i->it == getdb(driver).end()) {
		ast_free(i->context);
		ast_free(i->exten);
		delete i;
		return NULL;
	}

	return (struct ast_channel_iterator *)i;
}

static struct ast_channel *get_by_name_only(struct ast_channelstorage_instance *driver,
	const char *name)
{
	char *l_name = name ? ast_str_to_lower(ast_strdupa(name)) : NULL;
	if (ast_strlen_zero(name)) {
		return NULL;
	}
	auto chanx = getdb(driver).find(l_name);
	if (chanx == getdb(driver).end()) {
		return NULL;
	}
	return ao2_bump((struct ast_channel *)chanx->second);
}

static struct ast_channel *get_by_name_prefix(struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len)
{
	struct ast_channel *chan = NULL;
	char *l_name = NULL;

	if (ast_strlen_zero(name)) {
		/* We didn't have a name to search for so quit. */
		return NULL;
	}

	if (name_len == 0) {
		chan = get_by_name_only(driver, name);
		if (chan) {
			return chan;
		}
		chan = channelstorage_by_uniqueid(driver, name);
		if (chan) {
			return chan;
		}
		return NULL;
	}

	rdlock(driver);
	l_name = ast_str_to_lower(ast_strdupa(name));
	for (auto it = getdb(driver).begin(); it != getdb(driver).end(); ++it) {
		if (strncmp(it->first.c_str(), l_name, name_len) == 0) {
			chan = (struct ast_channel*) (it->second);
			break;
		}
	}
	unlock(driver);

	if (chan) {
		return ao2_bump(chan);
	}
	return NULL;
}

static void close_instance(struct ast_channelstorage_instance *driver)
{
	ast_debug(1, "Closing channel storage driver %s\n", driver ? driver->name : "NULL");
	if (!driver) {
		return;
	}

	if (driver->handle) {
		delete ((struct bn_channelstorage_driver_pvt *)driver->handle);
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
	.get_by_name_prefix= get_by_name_prefix,
	.get_by_name_prefix_or_uniqueid = channelstorage_by_name_prefix_or_uniqueid,
	.get_by_exten = channelstorage_by_exten,
	.get_by_uniqueid = channelstorage_by_uniqueid,
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

	driver->handle = (struct ast_channelstorage_driver_pvt *)new bn_channelstorage_driver_pvt();

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
	.driver_name = "boost_map_name",
	.open = get_instance,
};

static void __attribute__((constructor)) __startup(void)
{
	ast_channelstorage_register_driver(&driver_type);
}

