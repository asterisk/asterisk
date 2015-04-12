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

ASTERISK_REGISTER_FILE()

#include "res_parking.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_internal.h"
#include "asterisk/bridge_channel.h"
#include "asterisk/bridge_features.h"
#include "asterisk/features.h"
#include "asterisk/say.h"
#include "asterisk/datastore.h"
#include "asterisk/stasis.h"
#include "asterisk/module.h"
#include "asterisk/core_local.h"
#include "asterisk/causes.h"

struct parked_subscription_datastore {
	struct stasis_subscription *parked_subscription;
};

struct parked_subscription_data {
	struct transfer_channel_data *transfer_data;
	char *parkee_uuid;
	int hangup_after:1;
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
	RAII_VAR(struct ast_channel *, parker, NULL, ast_channel_cleanup);
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

	/* This subscription callback will block for the duration of the announcement if
	 * parked_subscription_data is tracking a transfer_channel_data struct. */
	if (message->event_type == PARKED_CALL) {
		/* queue the saynum on the bridge channel and hangup */
		snprintf(saynum_buf, sizeof(saynum_buf), "%d %u", data->hangup_after, message->parkingspace);
		if (!data->transfer_data) {
			ast_bridge_channel_queue_playfile(bridge_channel, say_parking_space, saynum_buf, NULL);
		} else {
			ast_bridge_channel_queue_playfile_sync(bridge_channel, say_parking_space, saynum_buf, NULL);
			data->transfer_data->completed = 1;
		}
		wipe_subscription_datastore(parker);
	} else if (message->event_type == PARKED_CALL_FAILED) {
		if (!data->transfer_data) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, "pbx-parkingfailed", NULL);
		} else {
			ast_bridge_channel_queue_playfile_sync(bridge_channel, NULL, "pbx-parkingfailed", NULL);
			data->transfer_data->completed = 1;
		}
		wipe_subscription_datastore(parker);
	}
}

static void parker_update_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	if (stasis_subscription_final_message(sub, message)) {
		struct parked_subscription_data *ps_data = data;
		ao2_cleanup(ps_data->transfer_data);
		ps_data->transfer_data = NULL;
		ast_free(data);
		return;
	}

	if (stasis_message_type(message) == ast_parked_call_type()) {
		struct ast_parked_call_payload *parked_call_message = stasis_message_data(message);
		parker_parked_call_message_response(parked_call_message, data, sub);
	}
}

static int create_parked_subscription_full(struct ast_channel *chan, const char *parkee_uuid, int hangup_after,
	struct transfer_channel_data *parked_channel_data)
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

	if (parked_channel_data) {
		subscription_data->transfer_data = parked_channel_data;
		ao2_ref(parked_channel_data, +1);
	}

	subscription_data->hangup_after = hangup_after;
	subscription_data->parkee_uuid = subscription_data->parker_uuid + parker_uuid_size;
	strcpy(subscription_data->parkee_uuid, parkee_uuid);
	strcpy(subscription_data->parker_uuid, parker_uuid);

	if (!(parked_datastore->parked_subscription = stasis_subscribe_pool(ast_parking_topic(), parker_update_cb, subscription_data))) {
		return -1;
	}

	datastore->data = parked_datastore;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	return 0;
}

int create_parked_subscription(struct ast_channel *chan, const char *parkee_uuid, int hangup_after)
{
	return create_parked_subscription_full(chan, parkee_uuid, hangup_after, NULL);
}

/*!
 * \internal
 * \brief Helper function that creates an outgoing channel and returns it immediately. This function is nearly
 *        identical to the dial_transfer function in bridge_basic.c, however it doesn't swap the
 *        local channel and the channel that instigated the park.
 */
static struct ast_channel *park_local_transfer(struct ast_channel *parker, const char *context, const char *exten, struct transfer_channel_data *parked_channel_data)
{
	char destination[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 1];
	struct ast_channel *parkee;
	struct ast_channel *parkee_side_2;
	int cause;

	/* Fill the variable with the extension and context we want to call */
	snprintf(destination, sizeof(destination), "%s@%s", exten, context);

	/* Now we request that chan_local prepare to call the destination */
	parkee = ast_request("Local", ast_channel_nativeformats(parker), NULL, parker, destination,
		&cause);
	if (!parkee) {
		return NULL;
	}

	/* Before we actually dial out let's inherit appropriate information. */
	ast_channel_lock_both(parker, parkee);
	ast_channel_req_accountcodes(parkee, parker, AST_CHANNEL_REQUESTOR_REPLACEMENT);
	ast_connected_line_copy_from_caller(ast_channel_connected(parkee), ast_channel_caller(parker));
	ast_channel_inherit_variables(parker, parkee);
	ast_channel_datastore_inherit(parker, parkee);
	ast_channel_unlock(parker);

	parkee_side_2 = ast_local_get_peer(parkee);
	ast_assert(parkee_side_2 != NULL);
	ast_channel_unlock(parkee);

	/* We need to have the parker subscribe to the new local channel before hand. */
	if (create_parked_subscription_full(parker, ast_channel_uniqueid(parkee_side_2), 1, parked_channel_data)) {
		ast_channel_unref(parkee_side_2);
		ast_hangup(parkee);
		return NULL;
	}

	ast_bridge_set_transfer_variables(parkee_side_2, ast_channel_name(parker), 0);

	ast_channel_unref(parkee_side_2);

	/* Since the above worked fine now we actually call it and return the channel */
	if (ast_call(parkee, destination, 0)) {
		ast_hangup(parkee);
		return NULL;
	}

	return parkee;
}

/*!
 * \internal
 * \brief Determine if an extension is a parking extension
 */
static int parking_is_exten_park(const char *context, const char *exten)
{
	struct ast_exten *exten_obj;
	struct pbx_find_info info = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	const char *app_at_exten;

	ast_debug(4, "Checking if %s@%s is a parking exten\n", exten, context);
	exten_obj = pbx_find_extension(NULL, NULL, &info, context, exten, 1, NULL, NULL, E_MATCH);
	if (!exten_obj) {
		return 0;
	}

	app_at_exten = ast_get_extension_app(exten_obj);
	if (!app_at_exten || strcasecmp(PARK_APPLICATION, app_at_exten)) {
		return 0;
	}

	return 1;
}

/*!
 * \internal
 * \since 12.0.0
 * \brief Perform a blind transfer to a parking lot
 *
 * In general, most parking features should work to call this function. This will safely
 * park either a channel in the bridge with \ref bridge_channel or will park the entire
 * bridge if more than one channel is in the bridge. It will create the correct data to
 * pass to the \ref AstBridging Bridging API to safely park the channel.
 *
 * \param bridge_channel The bridge_channel representing the channel performing the park
 * \param context The context to blind transfer to
 * \param exten The extension to blind transfer to
 * \param parked_channel_cb Optional callback executed prior to sending the parked channel into the bridge
 * \param parked_channel_data Data for the parked_channel_cb
 *
 * \retval 0 on success
 * \retval non-zero on error
 */
static int parking_blind_transfer_park(struct ast_bridge_channel *bridge_channel,
		const char *context, const char *exten, transfer_channel_cb parked_channel_cb,
		struct transfer_channel_data *parked_channel_data)
{
	RAII_VAR(struct ast_bridge_channel *, other, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, other_chan, NULL, ast_channel_cleanup);

	struct ast_exten *e;
	struct pbx_find_info find_info = { .stacklen = 0 };
	int peer_count;

	if (ast_strlen_zero(context) || ast_strlen_zero(exten)) {
		return -1;
	}

	if (!bridge_channel->in_bridge) {
		return -1;
	}

	if (!parking_is_exten_park(context, exten)) {
		return -1;
	}

	ast_bridge_channel_lock_bridge(bridge_channel);
	peer_count = bridge_channel->bridge->num_channels;
	if (peer_count == 2) {
		other = ast_bridge_channel_peer(bridge_channel);
		ao2_ref(other, +1);
		other_chan = other->chan;
		ast_channel_ref(other_chan);
	}
	ast_bridge_unlock(bridge_channel->bridge);

	if (peer_count < 2) {
		/* There is nothing to do if there is no one to park. */
		return -1;
	}

	/* With a multiparty bridge, we need to do a regular blind transfer. We link the
	 * existing bridge to the parking lot with a Local channel rather than
	 * transferring others. */
	if (peer_count > 2) {
		struct ast_channel *transfer_chan = NULL;

		transfer_chan = park_local_transfer(bridge_channel->chan, context, exten, parked_channel_data);
		if (!transfer_chan) {
			return -1;
		}
		ast_channel_ref(transfer_chan);

		if (parked_channel_cb) {
			parked_channel_cb(transfer_chan, parked_channel_data, AST_BRIDGE_TRANSFER_MULTI_PARTY);
		}

		if (ast_bridge_impart(bridge_channel->bridge, transfer_chan, NULL, NULL,
			AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
			ast_hangup(transfer_chan);
			ast_channel_unref(transfer_chan);
			return -1;
		}

		ast_channel_unref(transfer_chan);

		return 0;
	}

	/* Subscribe to park messages with the other channel entering */
	if (create_parked_subscription_full(bridge_channel->chan, ast_channel_uniqueid(other->chan), 1, parked_channel_data)) {
		return -1;
	}

	if (parked_channel_cb) {
		parked_channel_cb(other_chan, parked_channel_data, AST_BRIDGE_TRANSFER_SINGLE_PARTY);
	}

	e = pbx_find_extension(NULL, NULL, &find_info, context, exten, 1, NULL, NULL, E_MATCH);

	/* Write the park frame with the intended recipient and other data out to the bridge. */
	ast_bridge_channel_write_park(bridge_channel,
		ast_channel_uniqueid(other_chan),
		ast_channel_uniqueid(bridge_channel->chan),
		e ? ast_get_extension_app_data(e) : NULL);

	return 0;
}

/*!
 * \internal
 * \since 12.0.0
 * \brief Perform a direct park on a channel in a bridge
 *
 * \note This will be called from within the \ref AstBridging Bridging API
 *
 * \param bridge_channel The bridge_channel representing the channel to be parked
 * \param uuid_parkee The UUID of the channel being parked
 * \param uuid_parker The UUID of the channel performing the park
 * \param app_data Application parseable data to pass to the parking application
 */
static int parking_park_bridge_channel(struct ast_bridge_channel *bridge_channel, const char *uuid_parkee, const char *uuid_parker, const char *app_data)
{
	RAII_VAR(struct ast_bridge *, parking_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, original_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, parker, NULL, ao2_cleanup);

	if (strcmp(ast_channel_uniqueid(bridge_channel->chan), uuid_parkee)) {
		/* We aren't the parkee, so ignore this action. */
		return -1;
	}

	parker = ast_channel_get_by_name(uuid_parker);

	if (!parker) {
		ast_log(LOG_NOTICE, "Channel with uuid %s left before we could start parking the call. Parking canceled.\n", uuid_parker);
		publish_parked_call_failure(bridge_channel->chan);
		return -1;
	}

	if (!(parking_bridge = park_application_setup(bridge_channel->chan, parker, app_data, NULL))) {
		publish_parked_call_failure(bridge_channel->chan);
		return -1;
	}

	ast_bridge_set_transfer_variables(bridge_channel->chan, ast_channel_name(parker), 0);

	/* bridge_channel must be locked so we can get a reference to the bridge it is currently on */
	ao2_lock(bridge_channel);

	original_bridge = bridge_channel->bridge;
	if (!original_bridge) {
		ao2_unlock(bridge_channel);
		publish_parked_call_failure(bridge_channel->chan);
		return -1;
	}

	ao2_ref(original_bridge, +1); /* Cleaned by RAII_VAR */

	ao2_unlock(bridge_channel);

	if (ast_bridge_move(parking_bridge, original_bridge, bridge_channel->chan, NULL, 1)) {
		ast_log(LOG_ERROR, "Failed to move %s into the parking bridge.\n",
			ast_channel_name(bridge_channel->chan));
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \since 12.0.0
 * \brief Park a call
 *
 * \param parker The bridge_channel parking the call
 * \param exten Optional. The extension where the call was parked.
 * \param length Optional. If \c exten is specified, the length of the buffer.
 *
 * \note This will determine the context and extension to park the channel based on
 * the configuration of the \ref ast_channel associated with \ref parker. It will then
 * park either the channel or the entire bridge.
 *
 * \retval 0 on success
 * \retval -1 on error
 */
static int parking_park_call(struct ast_bridge_channel *parker, char *exten, size_t length)
{
	RAII_VAR(struct parking_lot *, lot, NULL, ao2_cleanup);
	const char *lot_name = NULL;

	ast_channel_lock(parker->chan);
	lot_name = find_channel_parking_lot_name(parker->chan);
	if (!ast_strlen_zero(lot_name)) {
		lot_name = ast_strdupa(lot_name);
	}
	ast_channel_unlock(parker->chan);

	if (ast_strlen_zero(lot_name)) {
		return -1;
	}

	lot = parking_lot_find_by_name(lot_name);
	if (!lot) {
		ast_log(AST_LOG_WARNING, "Cannot Park %s: lot %s unknown\n",
			ast_channel_name(parker->chan), lot_name);
		return -1;
	}

	if (exten) {
		ast_copy_string(exten, lot->cfg->parkext, length);
	}
	return parking_blind_transfer_park(parker, lot->cfg->parking_con, lot->cfg->parkext, NULL, NULL);
}

static int feature_park_call(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	SCOPED_MODULE_USE(parking_get_module_info()->self);

	return parking_park_call(bridge_channel, NULL, 0);
}

/*!
 * \internal
 * \brief Setup the caller features for when that channel is dialed.
 * \since 12.0.0
 *
 * \param chan Parked channel leaving the parking lot.
 * \param cfg Parking lot configuration.
 *
 * \return Nothing
 */
static void parking_timeout_set_caller_features(struct ast_channel *chan, struct parking_lot_cfg *cfg)
{
	char features[5];
	char *pos;

	/*
	 * We are setting the callee Dial flag values because in the
	 * timeout case, the caller is who is being called back.
	 */
	pos = features;
	if (cfg->parkedcalltransfers & AST_FEATURE_FLAG_BYCALLER) {
		*pos++ = 't';
	}
	if (cfg->parkedcallreparking & AST_FEATURE_FLAG_BYCALLER) {
		*pos++ = 'k';
	}
	if (cfg->parkedcallhangup & AST_FEATURE_FLAG_BYCALLER) {
		*pos++ = 'h';
	}
	if (cfg->parkedcallrecording & AST_FEATURE_FLAG_BYCALLER) {
		*pos++ = 'x';
	}
	*pos = '\0';

	pbx_builtin_setvar_helper(chan, "BRIDGE_FEATURES", features);
}

/*! \internal
 * \brief Interval hook. Pulls a parked call from the parking bridge after the timeout is passed and sets the resolution to timeout.
 *
 * \param bridge_channel bridge channel this interval hook is being executed on
 * \param hook_pvt A pointer to the parked_user struct associated with the channel is stuffed in here
 */
static int parking_duration_callback(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct parked_user *user = hook_pvt;
	struct ast_channel *chan = user->chan;
	struct ast_context *park_dial_context;
	const char *dial_string;
	char *dial_string_flat;
	char parking_space[AST_MAX_EXTENSION];

	char returnexten[AST_MAX_EXTENSION];
	char *duplicate_returnexten;
	struct ast_exten *existing_exten;
	struct pbx_find_info pbx_finder = { .stacklen = 0 }; /* The rest is reset in pbx_find_extension */


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

	ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE,
		AST_CAUSE_NORMAL_CLEARING);

	dial_string = user->parker_dial_string;
	dial_string_flat = ast_strdupa(dial_string);
	flatten_dial_string(dial_string_flat);

	/* Set parking timeout channel variables */
	snprintf(parking_space, sizeof(parking_space), "%d", user->parking_space);
	ast_channel_lock(chan);
	ast_channel_stage_snapshot(chan);
	pbx_builtin_setvar_helper(chan, "PARKING_SPACE", parking_space);
	pbx_builtin_setvar_helper(chan, "PARKINGSLOT", parking_space); /* Deprecated version of PARKING_SPACE */
	pbx_builtin_setvar_helper(chan, "PARKEDLOT", user->lot->name);
	pbx_builtin_setvar_helper(chan, "PARKER", dial_string);
	pbx_builtin_setvar_helper(chan, "PARKER_FLAT", dial_string_flat);
	parking_timeout_set_caller_features(chan, user->lot->cfg);
	ast_channel_stage_snapshot_done(chan);
	ast_channel_unlock(chan);

	/* Dialplan generation for park-dial extensions */

	if (ast_wrlock_contexts()) {
		ast_log(LOG_ERROR, "Failed to lock the contexts list. Can't add the park-dial extension.\n");
		return -1;
	}

	if (!(park_dial_context = ast_context_find_or_create(NULL, NULL, PARK_DIAL_CONTEXT, BASE_REGISTRAR))) {
		ast_log(LOG_ERROR, "Parking dial context '%s' does not exist and unable to create\n", PARK_DIAL_CONTEXT);
		if (ast_unlock_contexts()) {
			ast_assert(0);
		}
		goto abandon_extension_creation;
	}

	if (ast_wrlock_context(park_dial_context)) {
		ast_log(LOG_ERROR, "failed to obtain write lock on context '%s'\n", PARK_DIAL_CONTEXT);
		if (ast_unlock_contexts()) {
			ast_assert(0);
		}
		goto abandon_extension_creation;
	}

	if (ast_unlock_contexts()) {
		ast_assert(0);
	}

	snprintf(returnexten, sizeof(returnexten), "%s,%u", dial_string,
		user->lot->cfg->comebackdialtime);

	duplicate_returnexten = ast_strdup(returnexten);
	if (!duplicate_returnexten) {
		ast_log(LOG_ERROR, "Failed to create parking redial parker extension %s@%s - Dial(%s)\n",
			dial_string_flat, PARK_DIAL_CONTEXT, returnexten);
	}

	/* If an extension already exists here because we registered it for another parked call timing out, then we may overwrite it. */
	if ((existing_exten = pbx_find_extension(NULL, NULL, &pbx_finder, PARK_DIAL_CONTEXT, dial_string_flat, 1, NULL, NULL, E_MATCH)) &&
	    (strcmp(ast_get_extension_registrar(existing_exten), BASE_REGISTRAR))) {
		ast_debug(3, "An extension for '%s@%s' was already registered by another registrar '%s'\n",
			dial_string_flat, PARK_DIAL_CONTEXT, ast_get_extension_registrar(existing_exten));
	} else if (ast_add_extension2_nolock(park_dial_context, 1, dial_string_flat, 1, NULL, NULL,
			"Dial", duplicate_returnexten, ast_free_ptr, BASE_REGISTRAR)) {
			ast_free(duplicate_returnexten);
		ast_log(LOG_ERROR, "Failed to create parking redial parker extension %s@%s - Dial(%s)\n",
			dial_string_flat, PARK_DIAL_CONTEXT, returnexten);
	}

	if (ast_unlock_context(park_dial_context)) {
		ast_assert(0);
	}

abandon_extension_creation:

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
	unsigned int numeric_value;
	unsigned int hangup_after;

	if (sscanf(payload, "%u %u", &hangup_after, &numeric_value) != 2) {
		/* If say_parking_space is called with a non-numeric string, we have a problem. */
		ast_assert(0);
		ast_bridge_channel_leave_bridge(bridge_channel,
			BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);
		return;
	}

	ast_say_digits(bridge_channel->chan, numeric_value, "",
		ast_channel_language(bridge_channel->chan));

	if (hangup_after) {
		ast_bridge_channel_leave_bridge(bridge_channel,
			BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);
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

	if (ast_bridge_interval_hook(features, 0, time_limit,
		parking_duration_callback, user, __ao2_cleanup, AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		ast_log(LOG_ERROR, "Failed to apply duration limit to the parked call.\n");
		ao2_ref(user, -1);
	}
}

struct ast_parking_bridge_feature_fn_table parking_provider = {
	.module_version = PARKING_MODULE_VERSION,
	.module_name = __FILE__,
	.parking_is_exten_park = parking_is_exten_park,
	.parking_blind_transfer_park = parking_blind_transfer_park,
	.parking_park_bridge_channel = parking_park_bridge_channel,
	.parking_park_call = parking_park_call,
};

void unload_parking_bridge_features(void)
{
	ast_bridge_features_unregister(AST_BRIDGE_BUILTIN_PARKCALL);
	ast_parking_unregister_bridge_features(parking_provider.module_name);
}

int load_parking_bridge_features(void)
{
	parking_provider.module_info = parking_get_module_info();

	if (ast_parking_register_bridge_features(&parking_provider)) {
		return -1;
	}

	if (ast_bridge_features_register(AST_BRIDGE_BUILTIN_PARKCALL, feature_park_call, NULL)) {
		return -1;
	}

	return 0;
}
