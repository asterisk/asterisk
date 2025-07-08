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
#include <unordered_map>
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
	ChannelMap::const_iterator it;
	ChannelMap::const_iterator it_end;
	enum cpp_map_iterator_type it_type;
	char *channel_name;
	size_t channel_name_len;
	char *context;
	char *exten;

	mni_channel_iterator(ChannelMap::const_iterator it,
		ChannelMap::const_iterator it_end, char *name, size_t name_len)
		: it(it), it_end(it_end), it_type(ITERATOR_BY_NAME), channel_name(name), channel_name_len(name_len),
		context(NULL), exten(NULL)
	{
	}

	mni_channel_iterator(ChannelMap::const_iterator it,
		ChannelMap::const_iterator it_end, char *context, char *exten)
		: it(it), it_end(it_end), it_type(ITERATOR_BY_EXTEN), channel_name(NULL), channel_name_len(0),
		context(context), exten(exten)
	{
	}

	mni_channel_iterator(ChannelMap::const_iterator it, ChannelMap::const_iterator it_end)
		: it(it), it_end(it_end), it_type(ITERATOR_ALL), channel_name(NULL), channel_name_len(0),
		context(NULL), exten(NULL)
	{
	}

	~mni_channel_iterator()
	{
		ast_free(channel_name);
		ast_free(context);
		ast_free(exten);
	}
};

static struct ast_channel_iterator *iterator_destroy(struct ast_channelstorage_instance *driver,
	struct ast_channel_iterator *ai)
{
	struct mni_channel_iterator *i = (struct mni_channel_iterator *)ai;
	delete i;
	return NULL;
}

static struct ast_channel_iterator *iterator_all_new(struct ast_channelstorage_instance *driver)
{
	struct mni_channel_iterator *i = new mni_channel_iterator(
		getdb(driver).begin(), getdb(driver).end());
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
	struct mni_channel_iterator *i = (struct mni_channel_iterator *)ai;
	struct ast_channel *chan = NULL;

	if (i->it == i->it_end) {
		return NULL;
	}

	if (i->it_type == ITERATOR_ALL) {
		chan = ao2_bump(i->it->second);
		++i->it;
		return chan;
	}

	if (i->it_type == ITERATOR_BY_NAME) {
		chan = ao2_bump(i->it->second);
		++i->it;
		return chan;
	}

	/* ITERATOR_BY_EXTEN */
	while (i->it != i->it_end) {
		int ret = channelstorage_exten_cb(i->it->second, i->context, i->exten, 0);
		if (ret & CMP_MATCH) {
			chan = ao2_bump(i->it->second);
			++i->it;
			return chan;
		}
		++i->it;
	}

	return NULL;
}

static struct ast_channel_iterator *iterator_by_name_new(struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len)
{
	char *l_name = NULL;
	char *u_name = NULL;
	struct mni_channel_iterator *i;
	size_t new_name_len = 0;

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	l_name = ast_str_to_lower(ast_strdupa(name));
	if (name_len == 0) {
		name_len = strlen(name);
	}
	l_name[name_len] = '\0';
	new_name_len = strlen(l_name);
	u_name = (char *)ast_alloca(new_name_len + 2);
	sprintf(u_name, "%s%c", l_name, '\xFF');

	i = new mni_channel_iterator(getdb(driver).lower_bound(l_name),
		getdb(driver).upper_bound(u_name));
	if (!i) {
		return NULL;
	}

	if (i->it == getdb(driver).end()) {
		delete i;
		return NULL;
	}

	return (struct ast_channel_iterator *)i;
}

static struct ast_channel_iterator *iterator_by_exten_new(struct ast_channelstorage_instance *driver,
	const char *exten, const char *context)
{
	struct mni_channel_iterator *i =
		new mni_channel_iterator(getdb(driver).begin(),
			getdb(driver).end(),
			ast_str_to_lower(ast_strdup(context)), ast_str_to_lower(ast_strdup(exten)));
	if (!i) {
		return NULL;
	}

	if (i->it == getdb(driver).end()) {
		delete i;
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

	auto rtn = map_by_id(driver).find(search);
	if (rtn != map_by_id(driver).end()) {
		chan = ao2_bump((struct ast_channel *)rtn->second);
	}

	return chan;
}

static struct ast_channel *get_by_name_exact(struct ast_channelstorage_instance *driver,
	const char *name)
{
	char *search = name ? ast_str_to_lower(ast_strdupa(name)) : NULL;
	if (ast_strlen_zero(name)) {
		return NULL;
	}
	auto chan = getdb(driver).find(search);
	if (chan != getdb(driver).end()) {
		return ao2_bump((struct ast_channel *)chan->second);
	}

	return NULL;
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
	auto rtn = getdb(driver).lower_bound(l_name);
	if (rtn != getdb(driver).end()) {
		chan = ao2_bump((struct ast_channel *)rtn->second);
	}
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
