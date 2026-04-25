/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * Joshua Colp <jcolp@sangoma.com>
 *
 * See https://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/_private.h"
#include "asterisk/module.h"
#include "asterisk/extension_state.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/astobj2.h"
#include "asterisk/lock.h"
#include "asterisk/vector.h"

struct extension_state_legacy_state_cb {
	/*! Watcher ID returned when registered. */
	int id;
	/*! Message router for the traffic to this callback */
	struct stasis_message_router *router;
	/*! Arbitrary data passed for callbacks. */
	void *data;
	/*! Flag if this callback is an extended callback containing detailed device status */
	int extended;
	/*! Callback when state changes. */
	ast_state_cb_type change_cb;
	/*! Callback when destroyed so any resources given by the registerer can be freed. */
	ast_state_cb_destroy_type destroy_cb;
};

/*! \brief Lock to protect the callbacks vector */
AST_MUTEX_DEFINE_STATIC(extension_state_legacy_callbacks_lock);

/*! \brief Legacy callbacks, the index of it in the vector is the id given to the API user for per-extension */
static AST_VECTOR(, struct extension_state_legacy_state_cb *) extension_state_legacy_callbacks;

int ast_extension_state(struct ast_channel *c, const char *context, const char *exten)
{
	struct ast_extension_state_device_snapshot *device_snapshot;
	enum ast_extension_states device_state;

	device_snapshot = ast_extension_state_get_latest_device_snapshot(c, exten, context);
	if (!device_snapshot) {
		return -1;
	}

	device_state = device_snapshot->state;
	ao2_ref(device_snapshot, -1);

	return device_state;
}

int ast_hint_presence_state(struct ast_channel *c, const char *context, const char *exten, char **subtype, char **message)
{
	struct ast_extension_state_presence_snapshot *presence_snapshot;
	enum ast_presence_state presence_state;

	presence_snapshot = ast_extension_state_get_latest_presence_snapshot(c, exten, context);
	if (!presence_snapshot) {
		return -1;
	}

	presence_state = presence_snapshot->presence_state;
	if (presence_snapshot->presence_subtype) {
		*subtype = ast_strdup(presence_snapshot->presence_subtype);
	}
	if (presence_snapshot->presence_message) {
		*message = ast_strdup(presence_snapshot->presence_message);
	}

	ao2_ref(presence_snapshot, -1);

	return presence_state;
}

/*!
 * \internal
 * \brief Destroy function for device state info objects.
 *
 * \param obj The device state info object to destroy.
 */
static void device_state_info_destroy(void *obj)
{
	struct ast_device_state_info *info = obj;

	ao2_cleanup(info->causing_channel);
}

/*!
 * \internal
 * \brief Create a container of device state info objects from an extension device state message.
 *
 * \param device_state_message The extension device state message to create device state info from.
 * \return A container of device state info objects, or NULL on failure.
 */
static struct ao2_container *extension_state_legacy_create_device_state_info(struct ast_extension_state_device_snapshot *device_snapshot)
{
	struct ao2_container *device_state_info = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
	int i;

	if (!device_state_info) {
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&device_snapshot->additional_devices); i++) {
		struct ast_extension_state_device_state_info *source_info = AST_VECTOR_GET(&device_snapshot->additional_devices, i);
		struct ast_device_state_info *obj;

		obj = ao2_alloc_options(sizeof(*obj) + strlen(source_info->device) + 1, device_state_info_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!obj) {
			ao2_ref(device_state_info, -1);
			return NULL;
		}

		obj->device_state = source_info->state;
		strcpy(obj->device_name, source_info->device); /* Safe */
		obj->causing_channel = ast_extension_state_get_device_causing_channel(source_info->device, source_info->state);
		ao2_link(device_state_info, obj);
		ao2_ref(obj, -1);
	}

	return device_state_info;
}

int ast_extension_state_extended(struct ast_channel *c, const char *context, const char *exten,
	struct ao2_container **device_state_info)
{
	struct ast_extension_state_device_snapshot *device_snapshot;
	enum ast_extension_states device_state;

	device_snapshot = ast_extension_state_get_latest_device_snapshot(c, exten, context);
	if (!device_snapshot) {
		return -1;
	}

	device_state = device_snapshot->state;

	/* The caller wants device state info, so allocate a container and populate it */
	if (device_state_info) {
		*device_state_info = extension_state_legacy_create_device_state_info(device_snapshot);
	}

	ao2_ref(device_snapshot, -1);

	return device_state;
}

/*!
 * \internal
 * \brief Callback for subscription state change, used for reference handling.
 *
 * \param userdata The user data passed to the subscription.
 * \param sub The subscription that received the message.
 * \param msg The message received by the subscription.
 */
static void extension_state_legacy_subscription_change_cb(void *userdata, struct stasis_subscription *sub,
	struct stasis_message *msg)
{
	if (stasis_subscription_final_message(sub, msg)) {
		ao2_cleanup(userdata);
	}
}

/*!
 * \internal
 * \brief Destructor for \ref extension_state_legacy_state_cb.
 *
 * \param obj The extension state legacy state callback object to destroy.
 */
static void extension_state_legacy_state_cb_destroy(void *obj)
{
	struct extension_state_legacy_state_cb *cb = obj;

	if (cb->destroy_cb) {
		cb->destroy_cb(cb->id, cb->data);
	}
}

/*!
 * \internal
 * \brief Callback for extension state updates.
 *
 * \param userdata The user data passed to the subscription.
 * \param sub The subscription that received the message.
 * \param msg The message received by the subscription.
 */
static void extension_state_legacy_update_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct extension_state_legacy_state_cb *cb = userdata;
	struct ast_extension_state_update_message *update_message = stasis_message_data(msg);
	struct ast_state_cb_info info = {
		.exten_state = update_message->new_device_snapshot->state,
		.presence_state = update_message->new_presence_snapshot->presence_state,
		.presence_subtype = S_OR(update_message->new_presence_snapshot->presence_subtype, ""),
		.presence_message = S_OR(update_message->new_presence_snapshot->presence_message, ""),
	};

	/* If the presence has changed, notify the callback */
	if (update_message->new_presence_snapshot != update_message->old_presence_snapshot) {
		info.reason = AST_HINT_UPDATE_PRESENCE;
		cb->change_cb(update_message->context, update_message->extension, &info, cb->data);
	}

	/* If the device state has changed, notify the callback */
	if (update_message->new_device_snapshot != update_message->old_device_snapshot) {
		info.reason = AST_HINT_UPDATE_DEVICE;

		/* If they want extended information we need to provide the channels */
		if (cb->extended) {
			info.device_state_info = extension_state_legacy_create_device_state_info(update_message->new_device_snapshot);
		}

		cb->change_cb(update_message->context, update_message->extension, &info, cb->data);

		ao2_cleanup(info.device_state_info);
	}
}

/*!
 * \internal
 * \brief Callback for extension state removal updates.
 *
 * \param userdata The user data passed to the subscription.
 * \param sub The subscription that received the message.
 * \param msg The message received by the subscription.
 */
static void extension_state_legacy_remove_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct extension_state_legacy_state_cb *cb = userdata;
	struct ast_extension_state_remove_message *remove_message = stasis_message_data(msg);
	struct ast_state_cb_info info = {
		.reason = AST_HINT_UPDATE_DEVICE,
		.exten_state = AST_EXTENSION_REMOVED,
	};

	cb->change_cb(remove_message->context, remove_message->extension, &info, cb->data);
}

/*!
 * \internal
 * \brief Add a legacy extension state callback.
 *
 * \param context The context to monitor.
 * \param exten The extension to monitor.
 * \param change_cb The callback to call when the extension state changes.
 * \param destroy_cb The callback to call when the callback is removed.
 * \param data The data to pass to the callback.
 * \param extended Whether to include extended information in the callback.
 * \return The ID of the callback, or -1 on failure.
 */
static int extension_state_legacy_add_destroy(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data, int extended)
{
	struct extension_state_legacy_state_cb *state_cb;
	int id;

	state_cb = ao2_alloc_options(sizeof(*state_cb), extension_state_legacy_state_cb_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!state_cb) {
		return -1;
	}

	state_cb->change_cb = change_cb;
	state_cb->destroy_cb = destroy_cb;
	state_cb->data = data;
	state_cb->extended = extended;

	ast_mutex_lock(&extension_state_legacy_callbacks_lock);

	/*
	 * Callbacks for both per-extension and all are stored in a single vector which may have gaps in it.
	 * When adding a new callback, we look for the first gap in the vector and insert the callback there.
	 * If there are no gaps, we append it to the end of the vector.
	 * For per-extension the ID of a callback is its index in the vector + 1, since 0 is reserved for "all" callbacks.
	 */
	for (id = 0; id < AST_VECTOR_SIZE(&extension_state_legacy_callbacks); id++) {
		if (AST_VECTOR_GET(&extension_state_legacy_callbacks, id)) {
			continue;
		}

		state_cb->id = id + 1;

		/* This can't fail since the vector would have already resized */
		AST_VECTOR_REPLACE(&extension_state_legacy_callbacks, id, state_cb);

		break;
	}

	if (!state_cb->id) {
		/* The vector will resize when we append, which can fail, so handle it */
		if (AST_VECTOR_APPEND(&extension_state_legacy_callbacks, state_cb)) {
			ast_mutex_unlock(&extension_state_legacy_callbacks_lock);
			ao2_ref(state_cb, -1);
			return -1;
		}
		state_cb->id = AST_VECTOR_SIZE(&extension_state_legacy_callbacks);
	}

	/* At this point it is guaranteed that the callback has been inserted so we can setup
	 * the message router to accept messages from the appropriate topic and translate into
	 * the legacy callback.
	 */
	if (!context && !exten) {
		/*
		 * The all topic will receive all extension state updates which can end up being quite
		 * a lot, so we use a dedicated thread for each legacy callback to ensure that the
		 * pool of stasis threads does not become overloaded.
		 */
		state_cb->router = stasis_message_router_create(ast_extension_state_topic_all());
	} else {
		struct stasis_topic *topic = ast_extension_state_topic(exten, context);

		/*
		 * Per-extension on the other hand will have comparatively few extension state updates
		 * so we use the pool for it instead. Additionally the creation of the message router will
		 * fail if topic is NULL, so we don't do an explicit check and just let it try.
		 */
		state_cb->router = stasis_message_router_create_pool(topic);
		ao2_cleanup(topic);
	}

	/* If there is no message router allocated this callback is useless, so bail */
	if (!state_cb->router) {
		AST_VECTOR_REPLACE(&extension_state_legacy_callbacks, state_cb->id - 1, NULL);
		ast_mutex_unlock(&extension_state_legacy_callbacks_lock);
		ao2_ref(state_cb, -1);
		return -1;
	}

	/*
	 * Each of the message router callbacks translates the extension state messages into
	 * the legacy callback format and then calls the legacy callback with the appropriate data.
	 */
	stasis_message_router_add(state_cb->router, stasis_subscription_change_type(),
			extension_state_legacy_subscription_change_cb, ao2_bump(state_cb));
	stasis_message_router_add(state_cb->router, ast_extension_state_update_message_type(),
			extension_state_legacy_update_cb, state_cb);
	stasis_message_router_add(state_cb->router, ast_extension_state_remove_message_type(),
			extension_state_legacy_remove_cb, state_cb);

	ast_mutex_unlock(&extension_state_legacy_callbacks_lock);

	/*
	 * We don't hold a reference directly but the vector does and since we haven't given the ID back
	 * there's no way for the caller to remove it, thus it has to be valid even now.
	 */
	return (!context && !exten) ? 0 : state_cb->id;
}

int ast_extension_state_add_destroy(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data)
{
	return extension_state_legacy_add_destroy(context, exten, change_cb, destroy_cb, data, 0);
}

int ast_extension_state_add(const char *context, const char *exten,
	ast_state_cb_type change_cb, void *data)
{
	return extension_state_legacy_add_destroy(context, exten, change_cb, NULL, data, 0);
}

int ast_extension_state_add_destroy_extended(const char *context, const char *exten,
	ast_state_cb_type change_cb, ast_state_cb_destroy_type destroy_cb, void *data)
{
	return extension_state_legacy_add_destroy(context, exten, change_cb, destroy_cb, data, 1);
}

int ast_extension_state_add_extended(const char *context, const char *exten,
	ast_state_cb_type change_cb, void *data)
{
	return extension_state_legacy_add_destroy(context, exten, change_cb, NULL, data, 1);
}

int ast_extension_state_del(int id, ast_state_cb_type change_cb)
{
	struct extension_state_legacy_state_cb *cb;

	if (!id) {	/* id == 0 is a callback without extension */
		if (!change_cb) {
			return -1;
		}

		/*
		 * Global callbacks all have the ID of 0 so we need to find the actual index
		 * for them in the vector for removal based on callback.
		 */
		ast_mutex_lock(&extension_state_legacy_callbacks_lock);
		for (id = 0; id < AST_VECTOR_SIZE(&extension_state_legacy_callbacks); id++) {
			cb = AST_VECTOR_GET(&extension_state_legacy_callbacks, id);
			if (cb && cb->change_cb == change_cb) {
				AST_VECTOR_REPLACE(&extension_state_legacy_callbacks, id, NULL);
				ast_mutex_unlock(&extension_state_legacy_callbacks_lock);
				stasis_message_router_unsubscribe(cb->router);
				ao2_ref(cb, -1);
				return 0;
			}
		}
		ast_mutex_unlock(&extension_state_legacy_callbacks_lock);

		return -1;
	}

	ast_mutex_lock(&extension_state_legacy_callbacks_lock);
	if (id > 0 && id <= AST_VECTOR_SIZE(&extension_state_legacy_callbacks)) {
		cb = AST_VECTOR_GET(&extension_state_legacy_callbacks, id - 1);
		if (cb) {
			AST_VECTOR_REPLACE(&extension_state_legacy_callbacks, id - 1, NULL);
			ast_mutex_unlock(&extension_state_legacy_callbacks_lock);
			stasis_message_router_unsubscribe(cb->router);
			ao2_ref(cb, -1);
			return 0;
		}
	}
	ast_mutex_unlock(&extension_state_legacy_callbacks_lock);

	return -1;
}

/*!
 * \internal
 * \brief Clean up the legacy extension state system, called at shutdown.
 *
 * This function unregisters all legacy extension state callbacks and cleans up
 * the associated resources.
 */
static void extension_state_legacy_cleanup(void)
{
	int i;

	ast_mutex_lock(&extension_state_legacy_callbacks_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&extension_state_legacy_callbacks); i++) {
		struct extension_state_legacy_state_cb *cb = AST_VECTOR_GET(&extension_state_legacy_callbacks, i);

		if (!cb) {
			continue;
		}

		AST_VECTOR_REPLACE(&extension_state_legacy_callbacks, i, NULL);
		stasis_message_router_unsubscribe_and_join(cb->router);
		ao2_ref(cb, -1);
	}
	ast_mutex_unlock(&extension_state_legacy_callbacks_lock);
	AST_VECTOR_FREE(&extension_state_legacy_callbacks);
}

int ast_extension_state_legacy_init(void)
{
	/* Since we're not pre-allocating for any callbacks this can't fail */
	AST_VECTOR_INIT(&extension_state_legacy_callbacks, 0);
	ast_register_cleanup(extension_state_legacy_cleanup);

	return 0;
}
