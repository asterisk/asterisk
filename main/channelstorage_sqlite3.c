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

#include <sqlite3.h>
#include "asterisk.h"
#include "asterisk/channel.h"
#include "asterisk/threadstorage.h"

#include "channelstorage.h"
#include "channel_private.h"

struct ast_channelstorage_driver_pvt {
	sqlite3 *handle;
};

#define getdb(driver) (driver->handle->handle)

static const char * sql_create_table =
	"create table channels "
	"( name TEXT, "
	"uniqueid TEXT, "
	"ptr INTEGER PRIMARY KEY);"
	"create unique index ix_name on channels (name);"
	"create unique index ix_uniqueid on channels (uniqueid);"
	"PRAGMA optimize;"
	;

//	" WITHOUT ROWID;"
// "create unique index ix_ixptr on channels (ptr);"

static void stmt_finalize(void *stmt)
{
	sqlite3_finalize(stmt);
}

static sqlite3_stmt *stmt_get_or_prepare(struct ast_channelstorage_instance* driver,
	struct ast_threadstorage *ts, const void *sql)
{
	sqlite3_stmt *stmt;
	int rc = 0;

	stmt = ast_threadstorage_get_ptr(ts);
	if (stmt) {
		return stmt;
	}
#ifdef SQLITE_PREPARE_PERSISTENT
	rc = sqlite3_prepare_v3(getdb(driver), (const char *)sql, -1,
		SQLITE_PREPARE_PERSISTENT, &stmt, NULL);
#else
	rc = sqlite3_prepare_v2(getdb(driver), (const char *)sql, -1,
		&stmt, NULL);
#endif
	if (rc != SQLITE_OK) {
		ast_log(LOG_ERROR, "Unable to prepare statement '%s': %s\n", (const char *)sql, sqlite3_errstr(rc));
		return NULL;
	}
	ast_threadstorage_set_ptr(ts, stmt);
	return stmt;
}

#define DEFINE_SQL_STATEMENT(__stmt, __sql) \
const char * __stmt ## _sql = __sql; \
AST_THREADSTORAGE_CUSTOM_SCOPE(__stmt ## _ts, NULL, \
	stmt_finalize, static);

#define GET_SQL_STATEMENT(__driver, __stmt, ...) \
	sqlite3_stmt *__stmt ## _stmt = \
		stmt_get_or_prepare(__driver, &__stmt ## _ts, __stmt ## _sql); \
	if (!__stmt ## _stmt) {\
        return __VA_ARGS__; \
    }

#define GET_SQL_STATEMENT2(__driver, __stmt, ...) \
({ \
	sqlite3_stmt *_s = \
		stmt_get_or_prepare(__driver, &__stmt ## _ts, __stmt ## _sql); \
	if (!_s) {\
		return __VA_ARGS__; \
	} \
	_s; \
})

#define BIND_TEXT_STATEMENT(__stmt, __ix, __value, __rtn) \
({ \
	int __rc = sqlite3_bind_text(__stmt ## _stmt, __ix, \
	__value, strlen(__value), SQLITE_STATIC); \
	if (__rc != SQLITE_OK) { \
		ast_log(LOG_ERROR, "Unable bind " #__stmt ": %s\n", \
		sqlite3_errstr(__rc)); \
	return __rtn; \
	} \
})

#define BIND_INT64_STATEMENT(__stmt, __ix, __value, __rtn) \
({ \
	int __rc = sqlite3_bind_int64(__stmt ## _stmt, __ix, __value); \
	if (__rc != SQLITE_OK) { \
		ast_log(LOG_ERROR, "Unable bind " #__stmt ": %s\n", \
		sqlite3_errstr(__rc)); \
	return __rtn; \
	} \
})

#define BIND_INT_STATEMENT(__stmt, __ix, __value, __rtn) \
({ \
	int __rc = sqlite3_bind_int(__stmt ## _stmt, __ix, __value); \
	if (__rc != SQLITE_OK) { \
		ast_log(LOG_ERROR, "Unable bind " #__stmt ": %s\n", \
		sqlite3_errstr(__rc)); \
	return __rtn; \
	} \
})

static void rdlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_rdlock(driver->lock_handle);
}

static void wrlock(struct ast_channelstorage_instance *driver)
{
	ast_rwlock_wrlock(driver->lock_handle);
}

static void unlock(struct ast_channelstorage_instance* driver)
{
	ast_rwlock_unlock(driver->lock_handle);
}

DEFINE_SQL_STATEMENT(insert_channel, "insert into channels values(?, ?, ?)");
static int insert_channel(struct ast_channelstorage_instance* driver,
	struct ast_channel *chan, int flags, int lock)
{
	int rc = 0;
	char *_name = chan ? ast_str_to_lower(ast_strdupa(ast_channel_name(chan))) : NULL;
	char *_uniqueid = chan ? ast_str_to_lower(ast_strdupa(ast_channel_uniqueid(chan))) : NULL;
	sqlite3_stmt *insert_channel_stmt;

	if (!chan) {
		return -1;
	}
	if (lock) {
		wrlock(driver);
	}

	insert_channel_stmt = GET_SQL_STATEMENT2(driver, insert_channel, -1);

	BIND_TEXT_STATEMENT(insert_channel, 1, _name, -1);
	BIND_TEXT_STATEMENT(insert_channel, 2, _uniqueid, -1);
	BIND_INT64_STATEMENT(insert_channel, 3, (long int)chan, -1);

	rc = sqlite3_step(insert_channel_stmt);
	if (lock) {
		unlock(driver);
	}
	sqlite3_clear_bindings(insert_channel_stmt);
	sqlite3_reset(insert_channel_stmt);
	if (rc != SQLITE_DONE) {
		ast_log(LOG_ERROR, "Unable to insert channel '%s' into db: %s\n",
			ast_channel_name(chan), sqlite3_errstr(rc));
		return -1;
	} else {
		chan->linked_in_container = 1;
		ao2_bump(chan);
	}

	return 0;
}

DEFINE_SQL_STATEMENT(delete_channel, "delete from channels where ptr == ?");
static int delete_channel(struct ast_channelstorage_instance* driver,
	struct ast_channel *chan, int lock)
{
	int rc = 0;
	sqlite3_stmt *delete_channel_stmt;

	if (!chan) {
		return -1;
	}

	if (!chan->linked_in_container) {
		return 0;
	}

	if (lock) {
		wrlock(driver);
	}
	delete_channel_stmt = GET_SQL_STATEMENT2(driver, delete_channel, -1);

	BIND_INT64_STATEMENT(delete_channel, 1, (int64_t)chan, -1);
	rc = sqlite3_step(delete_channel_stmt);
	/*
	 * We're using a "returning" clause in the delete statement so...
	 * If we don't get a row back, it means the channel was not found.
	 * If we do get a row back, it means the channel was found and deleted
	 * so we need to unref it.  If we were querying by name, it's possible
	 * that the channel in the database is different from the one we're
	 * so we'd need to check the returned pointer against the one we
	 * were given.  Since we're querying by pointer, there's no need.
	 */
	if (rc == SQLITE_DONE) {
		chan->linked_in_container = 0;
		ast_channel_unref(chan);
		rc = 0;
	} else {
		ast_log(LOG_ERROR, "Unable to delete channel '%s': %s\n",
			ast_channel_name(chan), sqlite3_errstr(rc));
		rc = -1;
	}

	if (lock) {
		unlock(driver);
	}

	sqlite3_clear_bindings(delete_channel_stmt);
	sqlite3_reset(delete_channel_stmt);

	return rc;
}

DEFINE_SQL_STATEMENT(count_channels, "select CAST(count(*) as INT) from channels");
static int active_channels(struct ast_channelstorage_instance* driver)
{
	int rc = 0;
	int count = 0;
	GET_SQL_STATEMENT(driver, count_channels, -1);

	rc = sqlite3_step(count_channels_stmt);
	if (rc == SQLITE_ROW) {
		count = sqlite3_column_int(count_channels_stmt, 0);
	}
	sqlite3_reset(count_channels_stmt);
	if (rc != SQLITE_ROW) {
		ast_log(LOG_ERROR, "Unable to count channels: %s\n",
			sqlite3_errstr(rc));
		return -1;
	}

	return count;
}

DEFINE_SQL_STATEMENT(all_chans, "select ptr from channels");
static struct ast_channel *callback(struct ast_channelstorage_instance* driver,
	ao2_callback_data_fn *cb_fn, void *arg, void *data, int ao2_flags)
{
	int rc = 0;
	struct ast_channel *chan = NULL;
	GET_SQL_STATEMENT(driver, all_chans, NULL);

	if (!cb_fn) {
		return NULL;
	}

	rdlock(driver);
	while ((rc = sqlite3_step(all_chans_stmt) == SQLITE_ROW)) {
		chan = (struct ast_channel *)sqlite3_column_int64(all_chans_stmt, 0);
		if (cb_fn(chan, arg, data, ao2_flags) == (CMP_MATCH | CMP_STOP)) {
			ao2_bump(chan);
			break;
		}
	}
	unlock(driver);
	sqlite3_reset(all_chans_stmt);

	return chan;
}

static struct ast_channel *get_by_uniqueid(struct ast_channelstorage_instance* driver,
	const char *uniqueid);

DEFINE_SQL_STATEMENT(chan_by_name, "select ptr from channels where name == ?");
static struct ast_channel *get_by_name_exact(struct ast_channelstorage_instance* driver,
	const char *name)
{
	int rc = 0;
	struct ast_channel *chan = NULL;
	char *_name = name ? ast_str_to_lower(ast_strdupa(name)) : NULL;
	GET_SQL_STATEMENT(driver, chan_by_name, NULL);

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	BIND_TEXT_STATEMENT(chan_by_name, 1, _name, NULL);

	rdlock(driver);
	rc = sqlite3_step(chan_by_name_stmt);
	if (rc == SQLITE_ROW) {
		chan = ao2_bump(
			(struct ast_channel *)sqlite3_column_int64(chan_by_name_stmt, 0));
	}
	unlock(driver);
	sqlite3_clear_bindings(chan_by_name_stmt);
	sqlite3_reset(chan_by_name_stmt);

	if (!chan) {
		chan = get_by_uniqueid(driver, name);
	}
	return chan;
}

DEFINE_SQL_STATEMENT(chan_by_name_prefix, "select ptr from channels "
	"where name between ? and ? limit 1");
static struct ast_channel *get_by_name_prefix(struct ast_channelstorage_instance* driver,
	const char *name, size_t len)
{
	int rc = 0;
	struct ast_channel *chan = NULL;
	char *_name = NULL;
	char *_name_end = NULL;
	GET_SQL_STATEMENT(driver, chan_by_name_prefix, NULL);

	if (ast_strlen_zero(name)) {
		return NULL;
	}

	if (len == 0) {
		return get_by_name_exact(driver, name);
	}

	_name = ast_strdupa(name);
	_name[len] = '\0';
	ast_str_to_lower(_name);
	ast_asprintf(&_name_end, "%s\xFF", _name);

	BIND_TEXT_STATEMENT(chan_by_name_prefix, 1, _name, NULL);
	BIND_TEXT_STATEMENT(chan_by_name_prefix, 2, _name_end, NULL);
	rdlock(driver);
	rc = sqlite3_step(chan_by_name_prefix_stmt);
	if (rc == SQLITE_ROW) {
		chan = ao2_bump(
			(struct ast_channel *)sqlite3_column_int64(chan_by_name_prefix_stmt, 0));
	}
	unlock(driver);
	ast_free(_name_end);
	sqlite3_clear_bindings(chan_by_name_prefix_stmt);
	sqlite3_reset(chan_by_name_prefix_stmt);

	return chan;
}

DEFINE_SQL_STATEMENT(chan_by_uniqueid, "select ptr from channels where uniqueid == ?");
static struct ast_channel *get_by_uniqueid(struct ast_channelstorage_instance* driver,
	const char *uniqueid)
{
	int rc = 0;
	struct ast_channel *chan = NULL;
	char *_uniqueid = uniqueid ? ast_str_to_lower(ast_strdupa(uniqueid)) : NULL;
	GET_SQL_STATEMENT(driver, chan_by_uniqueid, NULL);

	if (ast_strlen_zero(uniqueid)) {
		return NULL;
	}

	BIND_TEXT_STATEMENT(chan_by_uniqueid, 1, _uniqueid, NULL);
	rdlock(driver);
	rc = sqlite3_step(chan_by_uniqueid_stmt);
	if (rc == SQLITE_ROW) {
		chan = ao2_bump(
			(struct ast_channel *)sqlite3_column_int64(chan_by_uniqueid_stmt, 0));
	}
	unlock(driver);
	sqlite3_clear_bindings(chan_by_uniqueid_stmt);
	sqlite3_reset(chan_by_uniqueid_stmt);

	return chan;
}

enum sqlite3_iterator_type {
	ITERATOR_ALL,
	ITERATOR_BY_NAME,
	ITERATOR_BY_EXTEN,
};

struct ast_channel_iterator {
	sqlite3_stmt *stmt;
	enum sqlite3_iterator_type iterator_type;
	char *name;
	char *name_end;
	char *context;
	char *exten;
};

static struct ast_channel_iterator *iterator_all_new(struct ast_channelstorage_instance* driver)
{
	struct ast_channel_iterator *i;
	GET_SQL_STATEMENT(driver, all_chans, NULL);

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}
	i->iterator_type = ITERATOR_ALL;
	i->stmt = all_chans_stmt;

	return i;
}

static struct ast_channel_iterator *iterator_by_exten_new(
	struct ast_channelstorage_instance* driver,
	const char *exten, const char *context)
{
	struct ast_channel_iterator *i;
	GET_SQL_STATEMENT(driver, all_chans, NULL);

	if (ast_strlen_zero(context) || ast_strlen_zero(exten)) {
		return NULL;
	}

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->iterator_type = ITERATOR_BY_EXTEN;
	i->context = ast_strdup(context);
	i->exten = ast_strdup(exten);
	i->stmt = all_chans_stmt;

	return i;
}

DEFINE_SQL_STATEMENT(chans_by_name_prefix, "select ptr from channels "
	"where name between ? and ?");
static struct ast_channel_iterator *iterator_by_name_new(
	struct ast_channelstorage_instance* driver, const char *name, size_t len)
{
	struct ast_channel_iterator *i;

	GET_SQL_STATEMENT(driver, chans_by_name_prefix, NULL);

	if (ast_strlen_zero(name) || len > strlen(name)) {
		return NULL;
	}

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->iterator_type = ITERATOR_BY_NAME;

	if (len == 0) {
		len = strlen(name);
	}

	i->name = ast_strdup(name);
	i->name[len] = '\0';
	ast_str_to_lower(i->name);
	ast_asprintf(&i->name_end, "%s\xFF", i->name);

	BIND_TEXT_STATEMENT(chans_by_name_prefix, 1, i->name, NULL);
	BIND_TEXT_STATEMENT(chans_by_name_prefix, 2, i->name_end, NULL);

	i->stmt = chans_by_name_prefix_stmt;

	return i;
}

static struct ast_channel *iterator_next(struct ast_channelstorage_instance* driver,
	struct ast_channel_iterator *i)
{
	struct ast_channel *chan = NULL;
	int rc = 0;

	if (!i || !i->stmt) {
		return NULL;
	}

	rdlock(driver);
	if (i->iterator_type == ITERATOR_BY_EXTEN) {
		while ((rc = sqlite3_step(i->stmt)) == SQLITE_ROW) {
			chan = (struct ast_channel *)sqlite3_column_int64(i->stmt, 0);
			rc = channelstorage_exten_cb((void*) chan, i->context, i->exten, OBJ_MULTIPLE);
			if (rc & CMP_MATCH) {
				ao2_ref(chan, +1);
				break;
			}
			chan = NULL;
		}
	} else {
		rc = sqlite3_step(i->stmt);
		if (rc == SQLITE_ROW) {
			chan = ao2_bump((struct ast_channel *)sqlite3_column_int64(i->stmt, 0));
		}
	}
	unlock(driver);
	return chan;
}

static struct ast_channel_iterator *iterator_destroy(
	struct ast_channelstorage_instance* driver, struct ast_channel_iterator *i)
{
	sqlite3_clear_bindings(i->stmt);
	sqlite3_reset(i->stmt);
	i->stmt = NULL;
	ast_free(i->name);
	ast_free(i->name_end);
	ast_free(i->context);
	ast_free(i->exten);
	ast_free(i);

	return NULL;
}

static void close_instance(struct ast_channelstorage_instance* driver)
{
	ast_debug(1, "Closing channel storage driver %s\n", driver ? driver->name : "NULL");
	if (!driver) {
		return;
	}
	if (driver->handle) {
		if (getdb(driver)) {
			sqlite3_close_v2(getdb(driver));
			getdb(driver) = NULL;
		}
		ast_free(driver->handle);
		driver->handle = NULL;
	}
	ast_free(driver->lock_handle);
	driver->lock_handle = NULL;
	ast_free(driver);
}

struct ast_channelstorage_instance channelstorage_instance = {
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
	struct ast_channelstorage_instance* driver = ast_calloc(1,
		sizeof(*driver) + strlen(_name) + 1);
	char *errmsg = NULL;
	int rc = 0;
	unsigned int open_opts = SQLITE_OPEN_MEMORY | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
		| SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_PRIVATECACHE;

#ifdef SQLITE_OPEN_EXRESCODE
	open_opts |= SQLITE_OPEN_EXRESCODE;
#endif

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

	rc = sqlite3_open_v2(_name, (sqlite3 **)&getdb(driver), open_opts, NULL);

	if (rc != SQLITE_OK) {
		ast_log(LOG_ERROR, "Unable to open channel storage database %s: %s\n",
			_name, sqlite3_errstr(rc));
		close_instance(driver);
		return NULL;
	}

	rc = sqlite3_exec(getdb(driver), sql_create_table, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		ast_log(LOG_ERROR, "Unable to create channel storage %s channels table or index: %s\n",
			_name, errmsg);
		close_instance(driver);
		return NULL;
	}
	driver->lock_handle = ast_calloc(1, sizeof(ast_rwlock_t));
	if (!driver->lock_handle) {
		ast_log(LOG_ERROR, "Failed to create database lock for channel storage driver %s\n",
			_name);
		close_instance(driver);
		return NULL;
	}
	ast_rwlock_init(driver->lock_handle);

	ast_debug(1, "Opened channel storage driver %s. driver: %p  database: %p\n",
		_name, driver, driver->handle);

	return driver;
}

static struct ast_channelstorage_driver driver_type = {
	.driver_name = "sqlite3",
	.open = get_instance,
};

static void __attribute__((constructor)) __startup(void)
{
	ast_channelstorage_register_driver(&driver_type);
}


