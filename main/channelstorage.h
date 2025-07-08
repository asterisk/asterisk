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

#ifndef CHANNELSTORAGE_H_
#define CHANNELSTORAGE_H_

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk.h"
#include "asterisk/channel.h"
#include "asterisk/channel_internal.h"

#define AST_CHANNELSTORAGE_DEFAULT_TYPE "ao2_legacy"

struct ast_channelstorage_driver {
	const char *driver_name;
	struct ast_channelstorage_instance* (*open_instance)(const char *instance_name);
};

int ast_channelstorage_register_driver(
	const struct ast_channelstorage_driver *driver_name);

const struct ast_channelstorage_driver *ast_channelstorage_get_driver(
	const char *driver_name);

struct ast_channelstorage_driver_pvt;

struct ast_channelstorage_instance {
	struct ast_channelstorage_driver_pvt *handle;
	void *lock_handle;
	void (*close_instance)(struct ast_channelstorage_instance *driver);
	int (*insert)(struct ast_channelstorage_instance *driver, struct ast_channel *chan, int flags, int lock);
	int (*remove)(struct ast_channelstorage_instance *driver, struct ast_channel *chan, int lock);
	void (*rdlock)(struct ast_channelstorage_instance *driver);
	void (*wrlock)(struct ast_channelstorage_instance *driver);
	void (*unlock)(struct ast_channelstorage_instance *driver);
	int (*active_channels)(struct ast_channelstorage_instance *driver);
	struct ast_channel *(*callback)(struct ast_channelstorage_instance *driver, ao2_callback_data_fn *cb_fn,
		void *arg, void *data, int ao2_flags);
	struct ast_channel *(*get_by_name_prefix)(struct ast_channelstorage_instance *driver, const char *name, size_t len);
	struct ast_channel *(*get_by_name_prefix_or_uniqueid)(struct ast_channelstorage_instance *driver, const char *name, size_t len);
	struct ast_channel *(*get_by_exten)(struct ast_channelstorage_instance *driver, const char *exten, const char *context);
	struct ast_channel *(*get_by_uniqueid)(struct ast_channelstorage_instance *driver, const char *uniqueid);
	struct ast_channel_iterator *(*iterator_all_new)(struct ast_channelstorage_instance *driver);
	struct ast_channel_iterator *(*iterator_by_exten_new)
		(struct ast_channelstorage_instance *driver, const char *exten, const char *context);
	struct ast_channel_iterator *(*iterator_by_name_new)
		(struct ast_channelstorage_instance *driver, const char *driver_name, size_t name_len);
	struct ast_channel *(*iterator_next)(struct ast_channelstorage_instance *driver, struct ast_channel_iterator *i);
	struct ast_channel_iterator *(*iterator_destroy)(
		struct ast_channelstorage_instance *driver, struct ast_channel_iterator *i);
	char name[0];
};

#define CHANNELSTORAGE_API(_instance, _func, ...) \
	(_instance)->_func((_instance), ##__VA_ARGS__)

int ast_channelstorage_init(void);

struct ast_channelstorage_instance *ast_channelstorage_open(
	const struct ast_channelstorage_driver *storage_driver, const char *instance_name);

void ast_channelstorage_close(struct ast_channelstorage_instance *storage_instance);

int channelstorage_exten_cb(void *obj, void *arg, void *data, int flags);
struct ast_channel *channelstorage_by_exten(struct ast_channelstorage_instance *driver,
	const char *exten, const char *context);
int channelstorage_name_cb(void *obj, void *arg, void *data, int flags);
struct ast_channel *channelstorage_by_name_or_uniqueid(struct ast_channelstorage_instance *driver,
	const char *name);
struct ast_channel *channelstorage_by_name_prefix_or_uniqueid(struct ast_channelstorage_instance *driver,
	const char *name, size_t name_len);
int channelstorage_uniqueid_cb(void *obj, void *arg, void *data, int flags);
struct ast_channel *channelstorage_by_uniqueid(struct ast_channelstorage_instance *driver,
	const char *uniqueid);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* CHANNELSTORAGE_H_ */
