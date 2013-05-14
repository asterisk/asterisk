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
 * \brief Stasis application support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "app.h"

#include "asterisk/stasis_app.h"
#include "asterisk/stasis_channels.h"

/*!
 * \brief Number of buckets for the channels container for app instances.  Remember
 * to keep it a prime number!
 */
#define APP_CHANNELS_BUCKETS 7

struct app {
	/*! Callback function for this application. */
	stasis_app_cb handler;
	/*! Opaque data to hand to callback function. */
	void *data;
	/*! List of channel identifiers this app instance is interested in */
	struct ao2_container *channels;
	/*! Name of the Stasis application */
	char name[];
};

static void app_dtor(void *obj)
{
	struct app *app = obj;

	ao2_cleanup(app->data);
	app->data = NULL;
	ao2_cleanup(app->channels);
	app->channels = NULL;
}

struct app *app_create(const char *name, stasis_app_cb handler, void *data)
{
	RAII_VAR(struct app *, app, NULL, ao2_cleanup);
	size_t size;

	ast_assert(name != NULL);
	ast_assert(handler != NULL);

	size = sizeof(*app) + strlen(name) + 1;
	app = ao2_alloc_options(size, app_dtor, AO2_ALLOC_OPT_LOCK_MUTEX);

	if (!app) {
		return NULL;
	}

	strncpy(app->name, name, size - sizeof(*app));
	app->handler = handler;
	ao2_ref(data, +1);
	app->data = data;

	app->channels = ast_str_container_alloc(APP_CHANNELS_BUCKETS);
	if (!app->channels) {
		return NULL;
	}

	ao2_ref(app, +1);
	return app;
}

int app_add_channel(struct app *app, const struct ast_channel *chan)
{
	const char *uniqueid;
	ast_assert(chan != NULL);
	ast_assert(app != NULL);

	uniqueid = ast_channel_uniqueid(chan);
	if (!ast_str_container_add(app->channels, uniqueid)) {
		return -1;
	}
	return 0;
}

void app_remove_channel(struct app* app, const struct ast_channel *chan)
{
	ast_assert(chan != NULL);
	ast_assert(app != NULL);

	ao2_find(app->channels, ast_channel_uniqueid(chan), OBJ_KEY | OBJ_NODATA | OBJ_UNLINK);
}

/*!
 * \brief Send a message to the given application.
 * \param app App to send the message to.
 * \param message Message to send.
 */
void app_send(struct app *app, struct ast_json *message)
{
	app->handler(app->data, app->name, message);
}

void app_update(struct app *app, stasis_app_cb handler, void *data)
{
	SCOPED_AO2LOCK(lock, app);

	app->handler = handler;
	ao2_cleanup(app->data);
	ao2_ref(data, +1);
	app->data = data;
}

const char *app_name(const struct app *app)
{
	return app->name;
}

int app_is_watching_channel(struct app *app, const char *uniqueid)
{
	RAII_VAR(char *, found, NULL, ao2_cleanup);
	found = ao2_find(app->channels, uniqueid, OBJ_KEY);
	return found != NULL;
}
