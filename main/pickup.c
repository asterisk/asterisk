/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2013, Digium, Inc.
 * Copyright (C) 2012, Russell Bryant
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Routines implementing call pickup
 *
 * \author Matt Jordan <mjordan@digium.com>
 */

/*!
 * \li Call pickup uses the configuration file \ref features.conf
 * \addtogroup configuration_file Configuration Files
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<managerEvent language="en_US" name="Pickup">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a call pickup occurs.</synopsis>
			<syntax>
				<channel_snapshot/>
				<channel_snapshot prefix="Target"/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

#include "asterisk.h"

#include "asterisk/pickup.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/callerid.h"
#include "asterisk/causes.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/features_config.h"

static struct ast_manager_event_blob *call_pickup_to_ami(struct stasis_message *message);

STASIS_MESSAGE_TYPE_DEFN(
	ast_call_pickup_type,
	.to_ami = call_pickup_to_ami);


/*!
 * The presence of this datastore on the channel indicates that
 * someone is attemting to pickup or has picked up the channel.
 * The purpose is to prevent a race between two channels
 * attempting to pickup the same channel.
 */
static const struct ast_datastore_info pickup_active = {
	.type = "pickup-active",
};

int ast_can_pickup(struct ast_channel *chan)
{
	if (!ast_channel_pbx(chan) && !ast_channel_masq(chan) && !ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE)
		&& (ast_channel_state(chan) == AST_STATE_RINGING
			|| ast_channel_state(chan) == AST_STATE_RING
			/*
			 * Check the down state as well because some SIP devices do not
			 * give 180 ringing when they can just give 183 session progress
			 * instead.  Issue 14005.  (Some ISDN switches as well for that
			 * matter.)
			 */
			|| ast_channel_state(chan) == AST_STATE_DOWN)
		&& !ast_channel_datastore_find(chan, &pickup_active, NULL)) {
		return 1;
	}
	return 0;
}

static int find_channel_by_group(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj; /*!< Potential pickup target */
	struct ast_channel *chan = arg;   /*!< Channel wanting to pickup call */

	if (chan == target) {
		return 0;
	}

	ast_channel_lock(target);
	if (ast_can_pickup(target)) {
		/* Lock both channels. */
		while (ast_channel_trylock(chan)) {
			ast_channel_unlock(target);
			sched_yield();
			ast_channel_lock(target);
		}

		/*
		 * Both callgroup and namedcallgroup pickup variants are
		 * matched independently.  Checking for named group match is
		 * done last since it's a more expensive operation.
		 */
		if ((ast_channel_pickupgroup(chan) & ast_channel_callgroup(target))
			|| (ast_namedgroups_intersect(ast_channel_named_pickupgroups(chan),
				ast_channel_named_callgroups(target)))) {
			struct ao2_container *candidates = data;/*!< Candidate channels found. */

			/* This is a candidate to pickup */
			ao2_link(candidates, target);
		}
		ast_channel_unlock(chan);
	}
	ast_channel_unlock(target);

	return 0;
}

struct ast_channel *ast_pickup_find_by_group(struct ast_channel *chan)
{
	struct ao2_container *candidates;/*!< Candidate channels found to pickup. */
	struct ast_channel *target;/*!< Potential pickup target */

	candidates = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
	if (!candidates) {
		return NULL;
	}

	/* Find all candidate targets by group. */
	ast_channel_callback(find_channel_by_group, chan, candidates, 0);

	/* Find the oldest pickup target candidate */
	target = NULL;
	for (;;) {
		struct ast_channel *candidate;/*!< Potential new older target */
		struct ao2_iterator iter;

		iter = ao2_iterator_init(candidates, 0);
		while ((candidate = ao2_iterator_next(&iter))) {
			if (!target) {
				/* First target. */
				target = candidate;
				continue;
			}
			if (ast_tvcmp(ast_channel_creationtime(candidate), ast_channel_creationtime(target)) < 0) {
				/* We have a new target. */
				ast_channel_unref(target);
				target = candidate;
				continue;
			}
			ast_channel_unref(candidate);
		}
		ao2_iterator_destroy(&iter);
		if (!target) {
			/* No candidates found. */
			break;
		}

		/* The found channel must be locked and ref'd. */
		ast_channel_lock(target);

		/* Recheck pickup ability */
		if (ast_can_pickup(target)) {
			/* This is the channel to pickup. */
			break;
		}

		/* Someone else picked it up or the call went away. */
		ast_channel_unlock(target);
		ao2_unlink(candidates, target);
		target = ast_channel_unref(target);
	}
	ao2_ref(candidates, -1);

	return target;
}

/*!
 * \brief Pickup a call
 *
 * Walk list of channels, checking it is not itself, channel is pbx one,
 * check that the callgroup for both channels are the same and the channel is ringing.
 * Answer calling channel, flag channel as answered on queue, masq channels together.
 */
int ast_pickup_call(struct ast_channel *chan)
{
	struct ast_channel *target;/*!< Potential pickup target */
	int res = -1;
	RAII_VAR(struct ast_features_pickup_config *, pickup_cfg, NULL, ao2_cleanup);
	const char *pickup_sound;
	const char *fail_sound;

	ast_debug(1, "Pickup attempt by %s\n", ast_channel_name(chan));
	ast_channel_lock(chan);
	pickup_cfg = ast_get_chan_features_pickup_config(chan);
	if (!pickup_cfg) {
		ast_log(LOG_ERROR, "Unable to retrieve pickup configuration. Unable to play pickup sounds\n");
	}
	pickup_sound = ast_strdupa(pickup_cfg ? pickup_cfg->pickupsound : "");
	fail_sound = ast_strdupa(pickup_cfg ? pickup_cfg->pickupfailsound : "");
	ast_channel_unlock(chan);

	/* The found channel is already locked. */
	target = ast_pickup_find_by_group(chan);
	if (target) {
		ast_log(LOG_NOTICE, "Pickup %s attempt by %s\n", ast_channel_name(target), ast_channel_name(chan));

		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		if (!res) {
			if (!ast_strlen_zero(pickup_sound)) {
				pbx_builtin_setvar_helper(target, "BRIDGE_PLAY_SOUND", pickup_sound);
			}
		} else {
			ast_log(LOG_WARNING, "Pickup %s failed by %s\n", ast_channel_name(target), ast_channel_name(chan));
		}
		target = ast_channel_unref(target);
	}

	if (res < 0) {
		ast_debug(1, "No call pickup possible... for %s\n", ast_channel_name(chan));
		if (!ast_strlen_zero(fail_sound)) {
			ast_answer(chan);
			ast_stream_and_wait(chan, fail_sound, "");
		}
	}

	return res;
}

static struct ast_manager_event_blob *call_pickup_to_ami(struct stasis_message *message)
{
	struct ast_multi_channel_blob *contents = stasis_message_data(message);
	struct ast_channel_snapshot *chan;
	struct ast_channel_snapshot *target;
	struct ast_manager_event_blob *res;

	RAII_VAR(struct ast_str *, channel_str, NULL, ast_free);
	RAII_VAR(struct ast_str *, target_str, NULL, ast_free);

	chan = ast_multi_channel_blob_get_channel(contents, "channel");
	target = ast_multi_channel_blob_get_channel(contents, "target");

	ast_assert(chan != NULL && target != NULL);

	if (!(channel_str = ast_manager_build_channel_state_string(chan))) {
		return NULL;
	}

	if (!(target_str = ast_manager_build_channel_state_string_prefix(target, "Target"))) {
		return NULL;
	}

	res = ast_manager_event_blob_create(EVENT_FLAG_CALL, "Pickup",
		"%s"
		"%s",
		ast_str_buffer(channel_str),
		ast_str_buffer(target_str));

	return res;
}

static int send_call_pickup_stasis_message(struct ast_channel *picking_up, struct ast_channel_snapshot *chan, struct ast_channel_snapshot *target)
{
	RAII_VAR(struct ast_multi_channel_blob *, pickup_payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	if (!ast_call_pickup_type()) {
		return -1;
	}

	if (!(pickup_payload = ast_multi_channel_blob_create(ast_json_null()))) {
		return -1;
	}

	ast_multi_channel_blob_add_channel(pickup_payload, "channel", chan);
	ast_multi_channel_blob_add_channel(pickup_payload, "target", target);

	if (!(msg = stasis_message_create(ast_call_pickup_type(), pickup_payload))) {
		return -1;
	}

	stasis_publish(ast_channel_topic(picking_up), msg);
	return 0;
}

int ast_do_pickup(struct ast_channel *chan, struct ast_channel *target)
{
	struct ast_party_connected_line connected_caller;
	struct ast_datastore *ds_pickup;
	const char *chan_name;/*!< A masquerade changes channel names. */
	const char *target_name;/*!< A masquerade changes channel names. */
	int res = -1;

	RAII_VAR(struct ast_channel_snapshot *, chan_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, target_snapshot, NULL, ao2_cleanup);

	target_name = ast_strdupa(ast_channel_name(target));
	ast_debug(1, "Call pickup on '%s' by '%s'\n", target_name, ast_channel_name(chan));

	/* Mark the target to block any call pickup race. */
	ds_pickup = ast_datastore_alloc(&pickup_active, NULL);
	if (!ds_pickup) {
		ast_log(LOG_WARNING,
			"Unable to create channel datastore on '%s' for call pickup\n", target_name);
		return -1;
	}
	ast_channel_datastore_add(target, ds_pickup);

	ast_party_connected_line_init(&connected_caller);
	ast_party_connected_line_copy(&connected_caller, ast_channel_connected(target));
	ast_channel_unlock(target);/* The pickup race is avoided so we do not need the lock anymore. */
	/* Reset any earlier private connected id representation */
	ast_party_id_reset(&connected_caller.priv);

	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
	if (ast_channel_connected_line_sub(NULL, chan, &connected_caller, 0) &&
		ast_channel_connected_line_macro(NULL, chan, &connected_caller, 0, 0)) {
		ast_channel_update_connected_line(chan, &connected_caller, NULL);
	}
	ast_party_connected_line_free(&connected_caller);

	ast_channel_lock(chan);
	chan_name = ast_strdupa(ast_channel_name(chan));
	ast_connected_line_copy_from_caller(&connected_caller, ast_channel_caller(chan));
	ast_channel_unlock(chan);
	connected_caller.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;

	if (ast_answer(chan)) {
		ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan_name);
		goto pickup_failed;
	}

	if (ast_queue_control(chan, AST_CONTROL_ANSWER)) {
		ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan_name);
		goto pickup_failed;
	}

	ast_channel_queue_connected_line_update(chan, &connected_caller, NULL);

	/* setting the HANGUPCAUSE so the ringing channel knows this call was not a missed call */
	ast_channel_hangupcause_set(chan, AST_CAUSE_ANSWERED_ELSEWHERE);

	ast_channel_lock(chan);
	chan_snapshot = ast_channel_snapshot_create(chan);
	ast_channel_unlock(chan);
	if (!chan_snapshot) {
		goto pickup_failed;
	}

	target_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(target));
	if (!target_snapshot) {
		goto pickup_failed;
	}

	if (ast_channel_move(target, chan)) {
		ast_log(LOG_WARNING, "Unable to complete call pickup of '%s' with '%s'\n",
			chan_name, target_name);
		goto pickup_failed;
	}

	/* target points to the channel that did the pickup at this point, so use that channel's topic instead of chan */
	send_call_pickup_stasis_message(target, chan_snapshot, target_snapshot);

	res = 0;

pickup_failed:
	ast_channel_lock(target);
	if (!ast_channel_datastore_remove(target, ds_pickup)) {
		ast_datastore_free(ds_pickup);
	}
	ast_party_connected_line_free(&connected_caller);

	return res;
}

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void pickup_shutdown(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(ast_call_pickup_type);
}

int ast_pickup_init(void)
{
	STASIS_MESSAGE_TYPE_INIT(ast_call_pickup_type);
	ast_register_cleanup(pickup_shutdown);

	return 0;
}
