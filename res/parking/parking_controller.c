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
 * \brief Parking Entry, Exit, and other assorted controls.
 *
 * \author Jonathan Rose <jrose@digium.com>
 */
#include "asterisk.h"

#include "asterisk/logger.h"
#include "res_parking.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/test.h"
#include "asterisk/features.h"
#include "asterisk/bridge_basic.h"

struct ast_bridge *parking_lot_get_bridge(struct parking_lot *lot)
{
	struct ast_bridge *lot_bridge;

	if (lot->parking_bridge) {
		ao2_ref(lot->parking_bridge, +1);
		return lot->parking_bridge;
	}

	lot_bridge = bridge_parking_new(lot);
	if (!lot_bridge) {
		return NULL;
	}

	/* The parking lot needs a reference to the bridge as well. */
	lot->parking_bridge = lot_bridge;
	ao2_ref(lot->parking_bridge, +1);

	return lot_bridge;
}

int parking_channel_set_roles(struct ast_channel *chan, struct parking_lot *lot, int force_ringing)
{
	if (ast_channel_add_bridge_role(chan, "holding_participant")) {
		return -1;
	}

	if (force_ringing) {
		if (ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "ringing")) {
			return -1;
		}
	} else {
		if (ast_channel_set_bridge_role_option(chan, "holding_participant", "idle_mode", "musiconhold")) {
			return -1;
		}
		if (!ast_strlen_zero(lot->cfg->mohclass)) {
			if (ast_channel_set_bridge_role_option(chan, "holding_participant", "moh_class", lot->cfg->mohclass)) {
				return -1;
			}
		}
	}

	return 0;
}

struct parking_limits_pvt {
	struct parked_user *user;
};

int unpark_parked_user(struct parked_user *pu)
{
	if (pu->lot) {
		ao2_unlink(pu->lot->parked_users, pu);
		parking_lot_remove_if_unused(pu->lot);
		return 0;
	}

	return -1;
}

int parking_lot_get_space(struct parking_lot *lot, int target_override)
{
	int original_target;
	int current_target;
	struct ao2_iterator i;
	struct parked_user *user;
	int wrap;

	if (lot->cfg->parkfindnext) {
		/* Use next_space if the lot already has next_space set; otherwise use lot start. */
		original_target = lot->next_space ? lot->next_space : lot->cfg->parking_start;
	} else {
		original_target = lot->cfg->parking_start;
	}

	if (target_override >= lot->cfg->parking_start && target_override <= lot->cfg->parking_stop) {
		original_target = target_override;
	} else if (target_override > -1) {
		ast_log(LOG_WARNING, "Preferred parking spot %d is out of bounds (%d-%d)\n", target_override, lot->cfg->parking_start, lot->cfg->parking_stop);
	}

	current_target = original_target;

	wrap = lot->cfg->parking_start;

	i = ao2_iterator_init(lot->parked_users, 0);
	while ((user = ao2_iterator_next(&i))) {
		/* Increment the wrap on each pass until we find an empty space */
		if (wrap == user->parking_space) {
			wrap += 1;
		}

		if (user->parking_space < current_target) {
			/* It's lower than the anticipated target, so we haven't reached the target yet. */
			ao2_ref(user, -1);
			continue;
		}

		if (user->parking_space > current_target) {
			/* The current target is usable because all items below have been read and the next target is higher than the one we want. */
			ao2_ref(user, -1);
			break;
		}

		/* We found one already parked here. */
		current_target += 1;
		ao2_ref(user, -1);
	}
	ao2_iterator_destroy(&i);

	if (current_target <= lot->cfg->parking_stop) {
		return current_target;
	}

	if (wrap <= lot->cfg->parking_stop) {
		return wrap;
	}

	return -1;
}

static int retrieve_parked_user_targeted(void *obj, void *arg, int flags)
{
	int *target = arg;
	struct parked_user *user = obj;
	if (user->parking_space == *target) {
		return CMP_MATCH;
	}

	return 0;
}

struct parked_user *parking_lot_inspect_parked_user(struct parking_lot *lot, int target)
{
	struct parked_user *user;

	if (target < 0) {
		user = ao2_callback(lot->parked_users, 0, NULL, NULL);
	} else {
		user = ao2_callback(lot->parked_users, 0, retrieve_parked_user_targeted, &target);
	}

	if (!user) {
		return NULL;
	}

	return user;
}

struct parked_user *parking_lot_retrieve_parked_user(struct parking_lot *lot, int target)
{
	RAII_VAR(struct parked_user *, user, NULL, ao2_cleanup);

	if (target < 0) {
		user = ao2_callback(lot->parked_users, 0, NULL, NULL);
	} else {
		user = ao2_callback(lot->parked_users, 0, retrieve_parked_user_targeted, &target);
	}

	if (!user) {
		return NULL;
	}

	ao2_lock(user);
	if (user->resolution != PARK_UNSET) {
		/* Abandon. Something else has resolved the parked user before we got to it. */
		ao2_unlock(user);
		return NULL;
	}

	ao2_unlink(lot->parked_users, user);
	user->resolution = PARK_ANSWERED;
	ao2_unlock(user);

	parking_lot_remove_if_unused(user->lot);

	/* Bump the ref count by 1 since the RAII_VAR will eat the reference otherwise */
	ao2_ref(user, +1);
	return user;
}

void parked_call_retrieve_enable_features(struct ast_channel *chan, struct parking_lot *lot, int recipient_mode)
{
	/* Enabling features here should be additive to features that are already on the channel. */
	struct ast_flags feature_flags = { 0 };
	struct ast_flags *existing_features;

	ast_channel_lock(chan);
	existing_features = ast_bridge_features_ds_get(chan);
	if (existing_features) {
		feature_flags = *existing_features;
	}

	if (lot->cfg->parkedcalltransfers & recipient_mode) {
		ast_set_flag(&feature_flags, AST_FEATURE_REDIRECT);
	}

	if (lot->cfg->parkedcallreparking & recipient_mode) {
		ast_set_flag(&feature_flags, AST_FEATURE_PARKCALL);
	}

	if (lot->cfg->parkedcallhangup & recipient_mode) {
		ast_set_flag(&feature_flags, AST_FEATURE_DISCONNECT);
	}

	if (lot->cfg->parkedcallrecording & recipient_mode) {
		ast_set_flag(&feature_flags, AST_FEATURE_AUTOMIXMON);
	}

	ast_bridge_features_ds_set(chan, &feature_flags);
	ast_channel_unlock(chan);

	return;
}

void flatten_dial_string(char *dialstring)
{
	int i;

	for (i = 0; dialstring[i]; i++) {
		if (dialstring[i] == '/') {
			/* The underscore is the flattest character of all. */
			dialstring[i] = '_';
		}
	}
}

int comeback_goto(struct parked_user *pu, struct parking_lot *lot)
{
	struct ast_channel *chan = pu->chan;
	char *peername_flat = ast_strdupa(pu->parker_dial_string);

	/* Flatten the peername so that it can be used for performing the timeout PBX operations */
	flatten_dial_string(peername_flat);

	if (lot->cfg->comebacktoorigin) {
		if (ast_exists_extension(chan, PARK_DIAL_CONTEXT, peername_flat, 1, NULL)) {
			ast_async_goto(chan, PARK_DIAL_CONTEXT, peername_flat, 1);
			return 0;
		} else {
			ast_log(LOG_ERROR, "Can not start %s at %s,%s,1 because extension does not exist. Terminating call.\n",
				ast_channel_name(chan), PARK_DIAL_CONTEXT, peername_flat);
			return -1;
		}
	}

	if (ast_exists_extension(chan, lot->cfg->comebackcontext, peername_flat, 1, NULL)) {
		ast_async_goto(chan, lot->cfg->comebackcontext, peername_flat, 1);
		return 0;
	}

	if (ast_exists_extension(chan, lot->cfg->comebackcontext, "s", 1, NULL)) {
		ast_verb(2, "Could not start %s at %s,%s,1. Using 's@%s' instead.\n", ast_channel_name(chan),
			lot->cfg->comebackcontext, peername_flat, lot->cfg->comebackcontext);
		ast_async_goto(chan, lot->cfg->comebackcontext, "s", 1);
		return 0;
	}

	ast_verb(2, "Can not start %s at %s,%s,1 and exten 's@%s' does not exist. Using 's@default'\n",
		ast_channel_name(chan),
		lot->cfg->comebackcontext, peername_flat, lot->cfg->comebackcontext);
	ast_async_goto(chan, "default", "s", 1);

	return 0;
}
