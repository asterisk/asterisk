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
 * \brief Parking Bridge Class
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"
#include "res_parking.h"
#include "asterisk/astobj2.h"
#include "asterisk/logger.h"
#include "asterisk/say.h"
#include "asterisk/term.h"
#include "asterisk/features.h"
#include "asterisk/bridge_internal.h"

struct ast_bridge_parking
{
	struct ast_bridge base;

	/* private stuff for parking */
	struct parking_lot *lot;
};

/*!
 * \internal
 * \brief ast_bridge parking class destructor
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 *
 * \note XXX Stub... and it might go unused.
 *
 * \return Nothing
 */
static void bridge_parking_destroy(struct ast_bridge_parking *self)
{
	ast_bridge_base_v_table.destroy(&self->base);
}

static void bridge_parking_dissolving(struct ast_bridge_parking *self)
{
	self->lot = NULL;
	ast_bridge_base_v_table.dissolving(&self->base);
}

static void destroy_parked_user(void *obj)
{
	struct parked_user *pu = obj;

	ao2_cleanup(pu->lot);
	ao2_cleanup(pu->retriever);
	ast_free(pu->parker_dial_string);
}

/* Only call this on a parked user that hasn't had its parker_dial_string set already */
static int parked_user_set_parker_dial_string(struct parked_user *pu, const char *parker_channel_name)
{
	char *dial_string = ast_strdupa(parker_channel_name);

	ast_channel_name_to_dial_string(dial_string);
	pu->parker_dial_string = ast_strdup(dial_string);

	if (!pu->parker_dial_string) {
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \since 12
 * \brief Construct a parked_user struct assigned to the specified parking lot
 *
 * \param lot The parking lot we are assigning the user to
 * \param parkee The channel being parked
 * \param parker_channel_name The name of the parker of this channel
 * \param parker_dial_string Takes priority over parker for setting the parker dial string if included
 * \param use_random_space if true, prioritize using a random parking space instead
 *        of ${PARKINGEXTEN} and/or automatic assignment from the parking lot
 * \param time_limit If using a custom timeout, this should be supplied so that the
 *        parked_user struct can provide this information for manager events. If <0,
 *        use the parking lot limit instead.
 *
 * \retval NULL on failure
 * \retval reference to the parked user
 *
 * \note ao2_cleanup this reference when you are done using it or you'll cause leaks.
 */
static struct parked_user *generate_parked_user(struct parking_lot *lot, struct ast_channel *chan, const char *parker_channel_name, const char *parker_dial_string, int use_random_space, int time_limit)
{
	struct parked_user *new_parked_user;
	int preferred_space = -1; /* Initialize to use parking lot defaults */
	int parking_space;
	const char *parkingexten;

	if (lot->mode == PARKINGLOT_DISABLED) {
		ast_log(LOG_NOTICE, "Tried to park in a parking lot that is no longer able to be parked to.\n");
		return NULL;
	}

	new_parked_user = ao2_alloc(sizeof(*new_parked_user), destroy_parked_user);
	if (!new_parked_user) {
		return NULL;
	}

	if (use_random_space) {
		preferred_space = ast_random() % (lot->cfg->parking_stop - lot->cfg->parking_start + 1);
		preferred_space += lot->cfg->parking_start;
	} else {
		ast_channel_lock(chan);
		if ((parkingexten = pbx_builtin_getvar_helper(chan, "PARKINGEXTEN"))) {
			parkingexten = ast_strdupa(parkingexten);
		}
		ast_channel_unlock(chan);

		if (!ast_strlen_zero(parkingexten)) {
			if (sscanf(parkingexten, "%30d", &preferred_space) != 1 || preferred_space <= 0) {
				ast_log(LOG_WARNING, "PARKINGEXTEN='%s' is not a valid parking space.\n", parkingexten);
				ao2_ref(new_parked_user, -1);
				return NULL;
			}
		}
	}

	/* We need to keep the lot locked between parking_lot_get_space and actually placing it in the lot. Or until we decide not to. */
	ao2_lock(lot);

	parking_space = parking_lot_get_space(lot, preferred_space);
	if (parking_space == -1) {
		ast_log(LOG_NOTICE, "Failed to get parking space in lot '%s'. All full.\n", lot->name);
		ao2_ref(new_parked_user, -1);
		ao2_unlock(lot);
		return NULL;
	}

	lot->next_space = ((parking_space + 1) - lot->cfg->parking_start) % (lot->cfg->parking_stop - lot->cfg->parking_start + 1) + lot->cfg->parking_start;
	new_parked_user->chan = chan;
	new_parked_user->parking_space = parking_space;

	/* Have the parked user take a reference to the parking lot. This reference should be immutable and released at destruction */
	new_parked_user->lot = lot;
	ao2_ref(lot, +1);

	new_parked_user->start = ast_tvnow();
	new_parked_user->time_limit = (time_limit >= 0) ? time_limit : lot->cfg->parkingtime;

	if (parker_dial_string) {
		new_parked_user->parker_dial_string = ast_strdup(parker_dial_string);
	} else {
		if (ast_strlen_zero(parker_channel_name) || parked_user_set_parker_dial_string(new_parked_user, parker_channel_name)) {
			ao2_ref(new_parked_user, -1);
			ao2_unlock(lot);
			return NULL;
		}
	}

	if (!new_parked_user->parker_dial_string) {
		ao2_ref(new_parked_user, -1);
		ao2_unlock(lot);
		return NULL;
	}

	/* Insert into the parking lot's parked user list. We can unlock the lot now. */
	ao2_link(lot->parked_users, new_parked_user);
	ao2_unlock(lot);

	return new_parked_user;
}

/* TODO CEL events for parking */

/*!
 * \internal
 * \brief ast_bridge parking push method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon
 * \param bridge_channel Bridge channel to push
 * \param swap Bridge channel to swap places with if not NULL
 *
 * \note On entry, self is already locked
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_parking_push(struct ast_bridge_parking *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct parked_user *pu;
	const char *blind_transfer;
	struct ast_channel_snapshot *parker = NULL;
	const char *parker_channel_name = NULL;
	RAII_VAR(struct park_common_datastore *, park_datastore, NULL, park_common_datastore_free);

	ast_bridge_base_v_table.push(&self->base, bridge_channel, swap);

	ast_assert(self->lot != NULL);

	/* Answer the channel if needed */
	if (ast_channel_state(bridge_channel->chan) != AST_STATE_UP) {
		ast_answer(bridge_channel->chan);
	}

	if (swap) {
		int use_ringing = 0;

		ast_bridge_channel_lock(swap);
		pu = swap->bridge_pvt;
		if (!pu) {
			/* This should be impossible since the only way a channel can enter in the first place
			 * is if it has a parked user associated with it */
			publish_parked_call_failure(bridge_channel->chan);
			ast_bridge_channel_unlock(swap);
			return -1;
		}

		/* Give the swap channel's parked user reference to the incoming channel */
		pu->chan = bridge_channel->chan;
		bridge_channel->bridge_pvt = pu;
		swap->bridge_pvt = NULL;

		if (ast_bridge_channel_has_role(swap, "holding_participant")) {
			const char *idle_mode = ast_bridge_channel_get_role_option(swap, "holding_participant", "idle_mode");

			if (!ast_strlen_zero(idle_mode) && !strcmp(idle_mode, "ringing")) {
				use_ringing = 1;
			}
		}

		ast_bridge_channel_unlock(swap);

		parking_set_duration(bridge_channel->features, pu);

		if (parking_channel_set_roles(bridge_channel->chan, self->lot, use_ringing)) {
			ast_log(LOG_WARNING, "Failed to apply holding bridge roles to %s while joining the parking lot.\n",
				ast_channel_name(bridge_channel->chan));
		}

		publish_parked_call(pu, PARKED_CALL_SWAP);

		return 0;
	}

	if (!(park_datastore = get_park_common_datastore_copy(bridge_channel->chan))) {
		/* There was either a failure to apply the datastore when performing park common setup or else we had alloc failures while cloning. Abort. */
		return -1;
	}
	parker = ast_channel_snapshot_get_latest(park_datastore->parker_uuid);

	/* If the parker and the parkee are the same channel pointer, then the channel entered using
	 * the park application. It's possible that the channel that transferred it is still alive (particularly
	 * when a multichannel bridge is parked), so try to get the real parker if possible. */
	ast_channel_lock(bridge_channel->chan);
	blind_transfer = pbx_builtin_getvar_helper(bridge_channel->chan, "BLINDTRANSFER");
	blind_transfer = ast_strdupa(S_OR(blind_transfer, ""));
	ast_channel_unlock(bridge_channel->chan);
	if (!parker || !strcmp(parker->name, ast_channel_name(bridge_channel->chan))) {
		if (ast_strlen_zero(blind_transfer) && parker) {
			/* If no BLINDTRANSFER exists but the parker does then use their channel name */
			parker_channel_name = parker->name;
		} else {
			/* Even if there is no BLINDTRANSFER dialplan variable then blind_transfer will
			 * be an empty string.
			 */
			parker_channel_name = blind_transfer;
		}
	} else {
		parker_channel_name = parker->name;
	}

	pu = generate_parked_user(self->lot, bridge_channel->chan, parker_channel_name,
		park_datastore->parker_dial_string, park_datastore->randomize, park_datastore->time_limit);
	ao2_cleanup(parker);
	if (!pu) {
		publish_parked_call_failure(bridge_channel->chan);
		return -1;
	}

	/* If a comeback_override was provided, set it for the parked user's comeback string. */
	if (park_datastore->comeback_override) {
		ast_copy_string(pu->comeback, park_datastore->comeback_override, sizeof(pu->comeback));
	}

	/* Generate ParkedCall Stasis Message */
	publish_parked_call(pu, PARKED_CALL);

	/* If not a blind transfer and silence_announce isn't set, play the announcement to the parkee */
	if (ast_strlen_zero(blind_transfer) && !park_datastore->silence_announce) {
		char saynum_buf[16];

		snprintf(saynum_buf, sizeof(saynum_buf), "%d %d", 0, pu->parking_space);
		ast_bridge_channel_queue_playfile(bridge_channel, say_parking_space, saynum_buf, NULL);
	}

	/* Apply parking duration limits */
	parking_set_duration(bridge_channel->features, pu);

	/* Set this to the bridge pvt so that we don't have to refind the parked user associated with this bridge channel again. */
	bridge_channel->bridge_pvt = pu;

	ast_verb(3, "Parking '" COLORIZE_FMT "' in '" COLORIZE_FMT "' at space %d\n",
		COLORIZE(COLOR_BRMAGENTA, 0, ast_channel_name(bridge_channel->chan)),
		COLORIZE(COLOR_BRMAGENTA, 0, self->lot->name),
		pu->parking_space);

	parking_notify_metermaids(pu->parking_space, self->lot->cfg->parking_con, AST_DEVICE_INUSE);

	return 0;
}

/*!
 * \internal
 * \brief ast_bridge parking pull method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to pull.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
static void bridge_parking_pull(struct ast_bridge_parking *self, struct ast_bridge_channel *bridge_channel)
{
	RAII_VAR(struct parked_user *, pu, NULL, ao2_cleanup);

	ast_bridge_base_v_table.pull(&self->base, bridge_channel);

	/* Take over the bridge channel's pu reference. It will be released when we are done. */
	pu = bridge_channel->bridge_pvt;
	bridge_channel->bridge_pvt = NULL;

	/* This should only happen if the exiting channel was swapped out */
	if (!pu) {
		return;
	}

	/* If we got here without the resolution being set, that's because the call was hung up for some reason without
	 * timing out or being picked up. There may be some forcible park removals later, but the resolution should be
	 * handled in those cases */
	ao2_lock(pu);
	if (pu->resolution == PARK_UNSET) {
		pu->resolution = PARK_ABANDON;
	}
	ao2_unlock(pu);

	/* Pull can still happen after the bridge starts dissolving, so make sure we still have a lot before trying to notify metermaids. */
	if (self->lot) {
		parking_notify_metermaids(pu->parking_space, self->lot->cfg->parking_con, AST_DEVICE_NOT_INUSE);
	}

	switch (pu->resolution) {
	case PARK_UNSET:
		/* This should be impossible now since the resolution is forcibly set to abandon if it was unset at this point. Resolution
		   isn't allowed to be changed when it isn't currently PARK_UNSET. */
		break;
	case PARK_ABANDON:
		/* Since the call was abandoned without additional handling, we need to issue the give up event and unpark the user. */
		publish_parked_call(pu, PARKED_CALL_GIVEUP);
		unpark_parked_user(pu);
		break;
	case PARK_FORCED:
		/* PARK_FORCED is currently unused, but it is expected that it would be handled similar to PARK_ANSWERED.
		 * There is currently no event related to forced parked calls either */
		break;
	case PARK_ANSWERED:
		/* If answered or forced, the channel should be pulled from the bridge as part of that process and unlinked from
		 * the parking lot afterwards. We do need to apply bridge features though and play the courtesy tone if set. */
		publish_parked_call(pu, PARKED_CALL_UNPARKED);
		parked_call_retrieve_enable_features(bridge_channel->chan, pu->lot, AST_FEATURE_FLAG_BYCALLEE);

		if (pu->lot->cfg->parkedplay & AST_FEATURE_FLAG_BYCALLEE) {
			ast_bridge_channel_queue_playfile(bridge_channel, NULL, pu->lot->cfg->courtesytone, NULL);
		}
		break;
	case PARK_TIMEOUT:
		/* Timeout is similar to abandon because it simply sets the bridge state to end and doesn't
		 * actually pull the channel. Because of that, unpark should happen in here. */
		publish_parked_call(pu, PARKED_CALL_TIMEOUT);
		parked_call_retrieve_enable_features(bridge_channel->chan, pu->lot, AST_FEATURE_FLAG_BYCALLEE);
		unpark_parked_user(pu);
		break;
	}
}

/*!
 * \internal
 * \brief ast_bridge parking notify_masquerade method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel that was masqueraded.
 *
 * \note On entry, self is already locked.
 * \note XXX Stub... and it will probably go unused.
 *
 * \return Nothing
 */
static void bridge_parking_notify_masquerade(struct ast_bridge_parking *self, struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_base_v_table.notify_masquerade(&self->base, bridge_channel);
}

static void bridge_parking_get_merge_priority(struct ast_bridge_parking *self)
{
	ast_bridge_base_v_table.get_merge_priority(&self->base);
}

struct ast_bridge_methods ast_bridge_parking_v_table = {
	.name = "parking",
	.destroy = (ast_bridge_destructor_fn) bridge_parking_destroy,
	.dissolving = (ast_bridge_dissolving_fn) bridge_parking_dissolving,
	.push = (ast_bridge_push_channel_fn) bridge_parking_push,
	.pull = (ast_bridge_pull_channel_fn) bridge_parking_pull,
	.notify_masquerade = (ast_bridge_notify_masquerade_fn) bridge_parking_notify_masquerade,
	.get_merge_priority = (ast_bridge_merge_priority_fn) bridge_parking_get_merge_priority,
};

static struct ast_bridge *ast_bridge_parking_init(struct ast_bridge_parking *self, struct parking_lot *bridge_lot)
{
	if (!self) {
		return NULL;
	}

	/* If no lot is defined for the bridge, then we aren't allowing the bridge to be initialized. */
	if (!bridge_lot) {
		ao2_ref(self, -1);
		return NULL;
	}

	/* It doesn't need to be a reference since the bridge only lives as long as the parking lot lives. */
	self->lot = bridge_lot;

	return &self->base;
}

struct ast_bridge *bridge_parking_new(struct parking_lot *bridge_lot)
{
	void *bridge;

	bridge = bridge_alloc(sizeof(struct ast_bridge_parking), &ast_bridge_parking_v_table);
	bridge = bridge_base_init(bridge, AST_BRIDGE_CAPABILITY_HOLDING,
		AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM
		| AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM,  "Parking", bridge_lot->name, NULL);
	bridge = ast_bridge_parking_init(bridge, bridge_lot);
	bridge = bridge_register(bridge);
	return bridge;
}
