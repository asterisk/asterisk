/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief Parking Bridge DTMF and Interval features
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "res_parking.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_features.h"
#include "asterisk/features.h"
#include "asterisk/say.h"
#include "asterisk/datastore.h"
#include "asterisk/stasis.h"

struct parked_subscription_datastore {
	struct stasis_subscription *parked_subscription;
};

struct parked_subscription_data {
	char *parkee_uuid;
	char parker_uuid[0];
};

static void parked_subscription_datastore_destroy(void *data)
{
	struct parked_subscription_datastore *subscription_datastore = data;

	stasis_unsubscribe(subscription_datastore->parked_subscription);
	subscription_datastore->parked_subscription = NULL;

	ast_free(subscription_datastore);
}

static const struct ast_datastore_info parked_subscription_info = {
	.type = "park subscription",
	.destroy = parked_subscription_datastore_destroy,
};

static void wipe_subscription_datastore(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	ast_channel_lock(chan);

	datastore = ast_channel_datastore_find(chan, &parked_subscription_info, NULL);
	if (datastore) {
		ast_channel_datastore_remove(chan, datastore);
		ast_datastore_free(datastore);
	}
	ast_channel_unlock(chan);
}

static void parker_parked_call_message_response(struct ast_parked_call_payload *message, struct parked_subscription_data *data,
	struct stasis_subscription *sub)
{
	const char *parkee_to_act_on = data->parkee_uuid;
	char saynum_buf[16];
	struct ast_channel_snapshot *parkee_snapshot = message->parkee;
	RAII_VAR(struct ast_channel *, parker, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);

	if (strcmp(parkee_to_act_on, parkee_snapshot->uniqueid)) {
		return;
	}

	if (message->event_type != PARKED_CALL && message->event_type != PARKED_CALL_FAILED) {
		/* We only care about these two event types */
		return;
	}

	parker = ast_channel_get_by_name(data->parker_uuid);
	if (!parker) {
		return;
	}

	ast_channel_lock(parker);
	bridge_channel = ast_channel_get_bridge_channel(parker);
	ast_channel_unlock(parker);
	if (!bridge_channel) {
		return;
	}

	if (message->event_type == PARKED_CALL) {
		/* queue the saynum on the bridge channel and hangup */
		snprintf(saynum_buf, sizeof(saynum_buf), "%u %u", 1, message->parkingspace);
		ast_bridge_channel_queue_playfile(bridge_channel, say_parking_space, saynum_buf, NULL);
		wipe_subscription_datastore(bridge_channel->chan);
	}

	if (message->event_type == PARKED_CALL_FAILED) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, "pbx-parkingfailed", NULL);
		wipe_subscription_datastore(bridge_channel->chan);
	}
}

static void parker_update_cb(void *data, struct stasis_subscription *sub, struct stasis_topic *topic, struct stasis_message *message)
{
	if (stasis_subscription_final_message(sub, message)) {
		ast_free(data);
		return;
	}

	if (stasis_message_type(message) == ast_parked_call_type()) {
		struct ast_parked_call_payload *parked_call_message = stasis_message_data(message);
		parker_parked_call_message_response(parked_call_message, data, sub);
	}
}

static int create_parked_subscription(struct ast_channel *chan, const char *parkee_uuid)
{
	struct ast_datastore *datastore;
	struct parked_subscription_datastore *parked_datastore;
	struct parked_subscription_data *subscription_data;

	char *parker_uuid = ast_strdupa(ast_channel_uniqueid(chan));
	size_t parker_uuid_size = strlen(parker_uuid) + 1;

	/* If there is already a subscription, get rid of it. */
	wipe_subscription_datastore(chan);

	if (!(datastore = ast_datastore_alloc(&parked_subscription_info, NULL))) {
		return -1;
	}

	if (!(parked_datastore = ast_calloc(1, sizeof(*parked_datastore)))) {
		ast_datastore_free(datastore);
		return -1;
	}

	if (!(subscription_data = ast_calloc(1, sizeof(*subscription_data) + parker_uuid_size +
			strlen(parkee_uuid) + 1))) {
		ast_datastore_free(datastore);
		ast_free(parked_datastore);
		return -1;
	}

	subscription_data->parkee_uuid = subscription_data->parker_uuid + parker_uuid_size;
	strcpy(subscription_data->parkee_uuid, parkee_uuid);
	strcpy(subscription_data->parker_uuid, parker_uuid);

	if (!(parked_datastore->parked_subscription = stasis_subscribe(ast_parking_topic(), parker_update_cb, subscription_data))) {
		return -1;
	}

	datastore->data = parked_datastore;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	return 0;
}

/*!
 * \internal
 * \brief Helper function that creates an outgoing channel and returns it immediately. This function is nearly
 *        identical to the dial_transfer function in bridge_builtin_features.c, however it doesn't swap the
 *        local channel and the channel that instigated the park.
 */
static struct ast_channel *park_local_transfer(struct ast_channel *parker, const char *exten, const char *context)
{
	RAII_VAR(struct ast_channel *, parkee_side_2, NULL, ao2_cleanup);
	char destination[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 1];
	struct ast_channel *parkee;
	int cause;

	/* Used for side_2 hack */
	char *parkee_name;
	char *semi_pos;

	/* Fill the variable with the extension and context we want to call */
	snprintf(destination, sizeof(destination), "%s@%s", exten, context);

	/* Now we request that chan_local prepare to call the destination */
	parkee = ast_request("Local", ast_channel_nativeformats(parker), parker, destination,
		&cause);
	if (!parkee) {
		return NULL;
	}

	/* Before we actually dial out let's inherit appropriate information. */
	ast_channel_lock_both(parker, parkee);
	ast_connected_line_copy_from_caller(ast_channel_connected(parkee), ast_channel_caller(parker));
	ast_channel_inherit_variables(parker, parkee);
	ast_channel_datastore_inherit(parker, parkee);
	ast_channel_unlock(parkee);
	ast_channel_unlock(parker);

	/* BUGBUG Use Richard's unreal channel stuff here instead of this hack */
	parkee_name = ast_strdupa(ast_channel_name(parkee));

	semi_pos = strrchr(parkee_name, ';');
	if (!semi_pos) {
		/* There should always be a semicolon present in the string if is used since it's a local channel. */
		ast_assert(0);
		return NULL;
	}

	parkee_name[(semi_pos - parkee_name) + 1] = '2';
	parkee_side_2 = ast_channel_get_by_name(parkee_name);

	/* We need to have the parker subscribe to the new local channel before hand. */
	create_parked_subscription(parker, ast_channel_uniqueid(parkee_side_2));

	pbx_builtin_setvar_helper(parkee_side_2, "BLINDTRANSFER", ast_channel_name(parker));

	/* Since the above worked fine now we actually call it and return the channel */
	if (ast_call(parkee, destination, 0)) {
		ast_hangup(parkee);
		return NULL;
	}

	return parkee;
}

static int park_feature_helper(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_exten *park_exten)
{
	RAII_VAR(struct ast_channel *, other, NULL, ao2_cleanup);
	RAII_VAR(struct parking_lot *, lot, NULL, ao2_cleanup);
	RAII_VAR(struct parked_user *, pu, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, parking_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, bridge_peers, NULL, ao2_cleanup);
	struct ao2_iterator iter;

	bridge_peers = ast_bridge_peers(bridge);

	if (ao2_container_count(bridge_peers) < 2) {
		/* There is nothing to do if there is no one to park. */
		return 0;
	}

	if (ao2_container_count(bridge_peers) > 2) {
		/* With a multiparty bridge, we need to do a regular blind transfer. We link the existing bridge to the parking lot with a
		 * local channel rather than transferring others. */
		struct ast_channel *transfer_chan = NULL;

		if (!park_exten) {
			/* This simply doesn't work. The user attempted to one-touch park the parking lot and we can't originate a local channel
			 * without knowing an extension to transfer it to.
			 * XXX However, when parking lots are changed to be able to register extensions then this will be doable. */
			ast_log(LOG_ERROR, "Can not one-touch park a multiparty bridge.\n");
			return 0;
		}

		transfer_chan = park_local_transfer(bridge_channel->chan,
			ast_get_extension_name(park_exten), ast_get_context_name(ast_get_extension_context(park_exten)));

		if (!transfer_chan) {
			return 0;
		}

		if (ast_bridge_impart(bridge_channel->bridge, transfer_chan, NULL, NULL, 1)) {
			ast_hangup(transfer_chan);
		}

		return 0;
	}

	/* Since neither of the above cases were used, we are doing a simple park with a two party bridge. */

	for (iter = ao2_iterator_init(bridge_peers, 0); (other = ao2_iterator_next(&iter)); ao2_ref(other, -1)) {
		/* We need the channel that isn't the bridge_channel's channel. */
		if (strcmp(ast_channel_uniqueid(other), ast_channel_uniqueid(bridge_channel->chan))) {
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	if (!other) {
		ast_assert(0);
		return -1;
	}

	/* Subscribe to park messages with the other channel entering */
	if (create_parked_subscription(bridge_channel->chan, ast_channel_uniqueid(other))) {
		return -1;
	}

	/* Write the park frame with the intended recipient and other data out to the bridge. */
	ast_bridge_channel_write_park(bridge_channel, ast_channel_uniqueid(other), ast_channel_uniqueid(bridge_channel->chan), ast_get_extension_app_data(park_exten));

	return 0;
}

static void park_bridge_channel(struct ast_bridge_channel *bridge_channel, const char *uuid_parkee, const char *uuid_parker, const char *app_data)
{
	RAII_VAR(struct ast_bridge *, parking_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, original_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, parker, NULL, ao2_cleanup);

	if (strcmp(ast_channel_uniqueid(bridge_channel->chan), uuid_parkee)) {
		/* We aren't the parkee, so ignore this action. */
		return;
	}

	parker = ast_channel_get_by_name(uuid_parker);

	if (!parker) {
		ast_log(LOG_NOTICE, "Channel with uuid %s left before we could start parking the call. Parking canceled.\n", uuid_parker);
		publish_parked_call_failure(bridge_channel->chan);
		return;
	}

	if (!(parking_bridge = park_common_setup(bridge_channel->chan, parker, app_data, NULL))) {
		publish_parked_call_failure(bridge_channel->chan);
		return;
	}

	pbx_builtin_setvar_helper(bridge_channel->chan, "BLINDTRANSFER", ast_channel_name(parker));

	/* bridge_channel must be locked so we can get a reference to the bridge it is currently on */
	ao2_lock(bridge_channel);

	original_bridge = bridge_channel->bridge;
	if (!original_bridge) {
		ao2_unlock(bridge_channel);
		publish_parked_call_failure(bridge_channel->chan);
		return;
	}

	ao2_ref(original_bridge, +1); /* Cleaned by RAII_VAR */

	ao2_unlock(bridge_channel);

	if (ast_bridge_move(parking_bridge, original_bridge, bridge_channel->chan, NULL, 1)) {
		ast_log(LOG_ERROR, "Failed to move %s into the parking bridge.\n",
			ast_channel_name(bridge_channel->chan));
	}
}

static int feature_park(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	park_feature_helper(bridge, bridge_channel, NULL);
	return 0;
}

static void parking_duration_cb_destroyer(void *hook_pvt)
{
	struct parked_user *user = hook_pvt;
	ao2_ref(user, -1);
}

/*! \internal
 * \brief Interval hook. Pulls a parked call from the parking bridge after the timeout is passed and sets the resolution to timeout.
 *
 * \param bridge Which bridge the channel was parked in
 * \param bridge_channel bridge channel this interval hook is being executed on
 * \param hook_pvt A pointer to the parked_user struct associated with the channel is stuffed in here
 */
static int parking_duration_callback(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct parked_user *user = hook_pvt;
	struct ast_channel *chan = user->chan;
	char *peername;
	char parking_space[AST_MAX_EXTENSION];

	/* We are still in the bridge, so it's possible for other stuff to mess with the parked call before we leave the bridge
	   to deal with this, lock the parked user, check and set resolution. */
	ao2_lock(user);
	if (user->resolution != PARK_UNSET) {
		/* Abandon timeout since something else has resolved the parked user before we got to it. */
		ao2_unlock(user);
		return -1;
	}

	user->resolution = PARK_TIMEOUT;
	ao2_unlock(user);

	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);

	/* Set parking timeout channel variables */
	snprintf(parking_space, sizeof(parking_space), "%d", user->parking_space);
	pbx_builtin_setvar_helper(chan, "PARKING_SPACE", parking_space);
	pbx_builtin_setvar_helper(chan, "PARKINGSLOT", parking_space); /* Deprecated version of PARKING_SPACE */
	pbx_builtin_setvar_helper(chan, "PARKEDLOT", user->lot->name);

	peername = ast_strdupa(user->parker->name);
	flatten_peername(peername);

	pbx_builtin_setvar_helper(chan, "PARKER", peername);

	/* TODO Dialplan generation for park-dial extensions */

	/* async_goto the proper PBX destination - this should happen when we come out of the bridge */
	if (!ast_strlen_zero(user->comeback)) {
		ast_async_parseable_goto(chan, user->comeback);
	} else {
		comeback_goto(user, user->lot);
	}

	return -1;
}

void say_parking_space(struct ast_bridge_channel *bridge_channel, const char *payload)
{
	int numeric_value;
	int hangup_after;

	if (sscanf(payload, "%u %u", &hangup_after, &numeric_value) != 2) {
		/* If say_parking_space is called with a non-numeric string, we have a problem. */
		ast_assert(0);
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
		return;
	}

	ast_say_digits(bridge_channel->chan, numeric_value, "", ast_channel_language(bridge_channel->chan));

	if (hangup_after) {
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
	}
}

void parking_set_duration(struct ast_bridge_features *features, struct parked_user *user)
{
	unsigned int time_limit;

	time_limit = user->time_limit * 1000;

	if (!time_limit) {
		/* There is no duration limit that we need to apply. */
		return;
	}

	/* If the time limit has already been passed, set a really low time limit so we can kick them out immediately. */
	time_limit = ast_remaining_ms(user->start, time_limit);
	if (time_limit <= 0) {
		time_limit = 1;
	}

	/* The interval hook is going to need a reference to the parked_user */
	ao2_ref(user, +1);

	if (ast_bridge_interval_hook(features, time_limit,
		parking_duration_callback, user, parking_duration_cb_destroyer, 1)) {
		ast_log(LOG_ERROR, "Failed to apply duration limits to the parking call.\n");
	}
}

void unload_parking_bridge_features(void)
{
	ast_bridge_features_unregister(AST_BRIDGE_BUILTIN_PARKCALL);
	ast_uninstall_park_blind_xfer_func();
	ast_uninstall_bridge_channel_park_func();
}

int load_parking_bridge_features(void)
{
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_PARKCALL, feature_park, NULL);
	ast_install_park_blind_xfer_func(park_feature_helper);
	ast_install_bridge_channel_park_func(park_bridge_channel);
	return 0;
}
