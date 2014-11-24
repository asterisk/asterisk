/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Bridging Channel API
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author Richard Mudgett <rmudgett@digium.com>
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <signal.h>
#include <semaphore.h>

#include "asterisk/heap.h"
#include "asterisk/astobj2.h"
#include "asterisk/stringfields.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/timing.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_channel.h"
#include "asterisk/bridge_after.h"
#include "asterisk/bridge_channel_internal.h"
#include "asterisk/bridge_internal.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/musiconhold.h"
#include "asterisk/features_config.h"
#include "asterisk/parking.h"
#include "asterisk/causes.h"
#include "asterisk/test.h"

/*!
 * \brief Used to queue an action frame onto a bridge channel and write an action frame into a bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel work with.
 * \param action Type of bridge action frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
typedef int (*ast_bridge_channel_post_action_data)(struct ast_bridge_channel *bridge_channel, enum bridge_channel_action_type action, const void *data, size_t datalen);

/*!
 * \brief Counter used for assigning synchronous bridge action IDs
 */
static int sync_ids;

/*!
 * \brief Frame payload for synchronous bridge actions.
 *
 * The payload serves as a wrapper around the actual payload of the
 * frame, with the addition of an id used to find the associated
 * bridge_sync object.
 */
struct sync_payload {
	/*! Unique ID for this synchronous action */
	unsigned int id;
	/*! Actual frame data to process */
	unsigned char data[0];
};

/*!
 * \brief Synchronous bridge action object.
 *
 * Synchronous bridge actions require the ability for one thread to wait
 * and for another thread to indicate that the action has completed. This
 * structure facilitates that goal by providing synchronization structures.
 */
struct bridge_sync {
	/*! Unique ID of this synchronization object. Corresponds with ID in synchronous frame payload */
	unsigned int id;
	/*! Semaphore used for synchronization */
	sem_t sem;
	/*! Pointer to next entry in the list */
	AST_LIST_ENTRY(bridge_sync) list;
};

/*!
 * \brief List holding active synchronous action objects.
 */
static AST_RWLIST_HEAD_STATIC(sync_structs, bridge_sync);

/*!
 * \brief initialize a synchronous bridge object.
 *
 * This both initializes the structure and adds it to the list of
 * synchronization structures.
 *
 * \param sync_struct The synchronization object to initialize.
 * \param id ID to assign to the synchronization object.
 */
static void bridge_sync_init(struct bridge_sync *sync_struct, unsigned int id)
{
	memset(sync_struct, 0, sizeof(*sync_struct));
	sync_struct->id = id;
	sem_init(&sync_struct->sem, 0, 0);

	AST_RWLIST_WRLOCK(&sync_structs);
	AST_RWLIST_INSERT_TAIL(&sync_structs, sync_struct, list);
	AST_RWLIST_UNLOCK(&sync_structs);
}

/*!
 * \brief Clean up a syncrhonization bridge object.
 *
 * This frees fields within the synchronization object and removes
 * it from the list of active synchronization objects.
 *
 * Since synchronization objects are stack-allocated, it is vital
 * that this is called before the synchronization object goes
 * out of scope.
 *
 * \param sync_struct Synchronization object to clean up.
 */
static void bridge_sync_cleanup(struct bridge_sync *sync_struct)
{
	struct bridge_sync *iter;

	AST_RWLIST_WRLOCK(&sync_structs);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&sync_structs, iter, list) {
		if (iter->id == sync_struct->id) {
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&sync_structs);

	sem_destroy(&sync_struct->sem);
}

/*!
 * \brief Failsafe for synchronous bridge action waiting.
 *
 * When waiting for a synchronous bridge action to complete,
 * if there is a frame resource leak somewhere, it is possible
 * that we will never get notified that the synchronous action
 * completed.
 *
 * If a significant amount of time passes, then we will abandon
 * waiting for the synchrnous bridge action to complete.
 *
 * This constant represents the number of milliseconds we will
 * wait for the bridge action to complete.
 */
#define PLAYBACK_TIMEOUT (600 * 1000)

/*!
 * \brief Wait for a synchronous bridge action to complete.
 *
 * \param sync_struct Synchronization object corresponding to the bridge action.
 */
static void bridge_sync_wait(struct bridge_sync *sync_struct)
{
	struct timeval timeout_val = ast_tvadd(ast_tvnow(), ast_samp2tv(PLAYBACK_TIMEOUT, 1000));
	struct timespec timeout_spec = {
		.tv_sec = timeout_val.tv_sec,
		.tv_nsec = timeout_val.tv_usec * 1000,
	};

	sem_timedwait(&sync_struct->sem, &timeout_spec);
}

/*!
 * \brief Signal that waiting for a synchronous bridge action is no longer necessary.
 *
 * This may occur for several reasons
 * \li The synchronous bridge action has completed.
 * \li The bridge channel has been removed from the bridge.
 * \li The synchronous bridge action could not be queued.
 *
 * \param sync_struct Synchronization object corresponding to the bridge action.
 */
static void bridge_sync_signal(struct bridge_sync *sync_struct)
{
	sem_post(&sync_struct->sem);
}

void ast_bridge_channel_lock_bridge(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge;

	for (;;) {
		/* Safely get the bridge pointer */
		ast_bridge_channel_lock(bridge_channel);
		bridge = bridge_channel->bridge;
		ao2_ref(bridge, +1);
		ast_bridge_channel_unlock(bridge_channel);

		/* Lock the bridge and see if it is still the bridge we need to lock. */
		ast_bridge_lock(bridge);
		if (bridge == bridge_channel->bridge) {
			ao2_ref(bridge, -1);
			return;
		}
		ast_bridge_unlock(bridge);
		ao2_ref(bridge, -1);
	}
}

int ast_bridge_channel_notify_talking(struct ast_bridge_channel *bridge_channel, int started_talking)
{
	struct ast_frame action = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = started_talking
			? BRIDGE_CHANNEL_ACTION_TALKING_START : BRIDGE_CHANNEL_ACTION_TALKING_STOP,
	};

	return ast_bridge_channel_queue_frame(bridge_channel, &action);
}

/*!
 * \internal
 * \brief Poke the bridge_channel thread
 */
static void bridge_channel_poke(struct ast_bridge_channel *bridge_channel)
{
	if (!pthread_equal(pthread_self(), bridge_channel->thread)) {
		/* Wake up the bridge channel thread. */
		ast_queue_frame(bridge_channel->chan, &ast_null_frame);
	}
}

/*!
 * \internal
 * \brief Set actual cause on channel.
 * \since 12.0.0
 *
 * \param chan Channel to set cause.
 * \param cause Cause to set on channel.
 *   If cause <= 0 then use cause on channel if cause still <= 0 use AST_CAUSE_NORMAL_CLEARING.
 *
 * \return Actual cause set on channel.
 */
static int channel_set_cause(struct ast_channel *chan, int cause)
{
	ast_channel_lock(chan);
	if (cause <= 0) {
		cause = ast_channel_hangupcause(chan);
		if (cause <= 0) {
			cause = AST_CAUSE_NORMAL_CLEARING;
		}
	}
	ast_channel_hangupcause_set(chan, cause);
	ast_channel_unlock(chan);
	return cause;
}

void ast_bridge_channel_leave_bridge_nolock(struct ast_bridge_channel *bridge_channel, enum bridge_channel_state new_state, int cause)
{
	if (bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT) {
		return;
	}

	ast_debug(1, "Setting %p(%s) state from:%u to:%u\n",
		bridge_channel, ast_channel_name(bridge_channel->chan), bridge_channel->state,
		new_state);

	channel_set_cause(bridge_channel->chan, cause);

	/* Change the state on the bridge channel */
	bridge_channel->state = new_state;

	bridge_channel_poke(bridge_channel);
}

void ast_bridge_channel_leave_bridge(struct ast_bridge_channel *bridge_channel, enum bridge_channel_state new_state, int cause)
{
	ast_bridge_channel_lock(bridge_channel);
	ast_bridge_channel_leave_bridge_nolock(bridge_channel, new_state, cause);
	ast_bridge_channel_unlock(bridge_channel);
}

struct ast_bridge_channel *ast_bridge_channel_peer(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_bridge_channel *other = NULL;

	if (bridge_channel->in_bridge && bridge->num_channels == 2) {
		AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
			if (other != bridge_channel) {
				break;
			}
		}
	}

	return other;
}

void ast_bridge_channel_restore_formats(struct ast_bridge_channel *bridge_channel)
{
	ast_assert(bridge_channel->read_format != NULL);
	ast_assert(bridge_channel->write_format != NULL);

	ast_channel_lock(bridge_channel->chan);

	/* Restore original formats of the channel as they came in */
	if (ast_format_cmp(ast_channel_readformat(bridge_channel->chan), bridge_channel->read_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p(%s) to read format %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan),
			ast_format_get_name(bridge_channel->read_format));
		if (ast_set_read_format(bridge_channel->chan, bridge_channel->read_format)) {
			ast_debug(1, "Bridge failed to return %p(%s) to read format %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				ast_format_get_name(bridge_channel->read_format));
		}
	}
	if (ast_format_cmp(ast_channel_writeformat(bridge_channel->chan), bridge_channel->write_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p(%s) to write format %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan),
			ast_format_get_name(bridge_channel->write_format));
		if (ast_set_write_format(bridge_channel->chan, bridge_channel->write_format)) {
			ast_debug(1, "Bridge failed to return %p(%s) to write format %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				ast_format_get_name(bridge_channel->write_format));
		}
	}

	ast_channel_unlock(bridge_channel->chan);
}

struct ast_bridge *ast_bridge_channel_merge_inhibit(struct ast_bridge_channel *bridge_channel, int request)
{
	struct ast_bridge *bridge;

	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge = bridge_channel->bridge;
	ao2_ref(bridge, +1);
	bridge_merge_inhibit_nolock(bridge, request);
	ast_bridge_unlock(bridge);
	return bridge;
}

void ast_bridge_channel_update_linkedids(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct ast_bridge_channel *other;
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_channel *oldest_linkedid_chan = bridge_channel->chan;

	AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
		if (other == swap) {
			continue;
		}
		oldest_linkedid_chan = ast_channel_internal_oldest_linkedid(
			oldest_linkedid_chan, other->chan);
	}

	ast_channel_lock(bridge_channel->chan);
	ast_channel_internal_copy_linkedid(bridge_channel->chan, oldest_linkedid_chan);
	ast_channel_unlock(bridge_channel->chan);
	AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
		if (other == swap) {
			continue;
		}
		ast_channel_lock(other->chan);
		ast_channel_internal_copy_linkedid(other->chan, oldest_linkedid_chan);
		ast_channel_unlock(other->chan);
	}
}

/*!
 * \internal
 * \brief Set dest's empty peeraccount with the src's non-empty accountcode.
 * \since 12.5.0
 *
 * \param dest Channel to update peeraccount.
 * \param src Channel to get accountcode from.
 *
 * \note Both channels are already locked.
 *
 * \return Nothing
 */
static void channel_fill_empty_peeraccount(struct ast_channel *dest, struct ast_channel *src)
{
	if (ast_strlen_zero(ast_channel_peeraccount(dest))
		&& !ast_strlen_zero(ast_channel_accountcode(src))) {
		ast_debug(1, "Setting channel %s peeraccount with channel %s accountcode '%s'.\n",
			ast_channel_name(dest),
			ast_channel_name(src), ast_channel_accountcode(src));
		ast_channel_peeraccount_set(dest, ast_channel_accountcode(src));
	}
}

/*!
 * \internal
 * \brief Set dest's empty accountcode with the src's non-empty peeraccount.
 * \since 12.5.0
 *
 * \param dest Channel to update accountcode.
 * \param src Channel to get peeraccount from.
 *
 * \note Both channels are already locked.
 *
 * \return Nothing
 */
static void channel_fill_empty_accountcode(struct ast_channel *dest, struct ast_channel *src)
{
	if (ast_strlen_zero(ast_channel_accountcode(dest))
		&& !ast_strlen_zero(ast_channel_peeraccount(src))) {
		ast_debug(1, "Setting channel %s accountcode with channel %s peeraccount '%s'.\n",
			ast_channel_name(dest),
			ast_channel_name(src), ast_channel_peeraccount(src));
		ast_channel_accountcode_set(dest, ast_channel_peeraccount(src));
	}
}

/*!
 * \internal
 * \brief Set empty peeraccount and accountcode in a channel from the other channel.
 * \since 12.5.0
 *
 * \param c0 First bridge channel to update.
 * \param c1 Second bridge channel to update.
 *
 * \note Both channels are already locked.
 *
 * \return Nothing
 */
static void channel_set_empty_accountcodes(struct ast_channel *c0, struct ast_channel *c1)
{
	/* Set empty peeraccount from the other channel's accountcode. */
	channel_fill_empty_peeraccount(c0, c1);
	channel_fill_empty_peeraccount(c1, c0);

	/* Set empty accountcode from the other channel's peeraccount. */
	channel_fill_empty_accountcode(c0, c1);
	channel_fill_empty_accountcode(c1, c0);
}

/*!
 * \internal
 * \brief Update dest's peeraccount with the src's different accountcode.
 * \since 12.5.0
 *
 * \param dest Channel to update peeraccount.
 * \param src Channel to get accountcode from.
 *
 * \note Both channels are already locked.
 *
 * \return Nothing
 */
static void channel_update_peeraccount(struct ast_channel *dest, struct ast_channel *src)
{
	if (strcmp(ast_channel_accountcode(src), ast_channel_peeraccount(dest))) {
		ast_debug(1, "Changing channel %s peeraccount '%s' to match channel %s accountcode '%s'.\n",
			ast_channel_name(dest), ast_channel_peeraccount(dest),
			ast_channel_name(src), ast_channel_accountcode(src));
		ast_channel_peeraccount_set(dest, ast_channel_accountcode(src));
	}
}

/*!
 * \internal
 * \brief Update peeraccounts to match the other channel's accountcode.
 * \since 12.5.0
 *
 * \param c0 First channel to update.
 * \param c1 Second channel to update.
 *
 * \note Both channels are already locked.
 *
 * \return Nothing
 */
static void channel_update_peeraccounts(struct ast_channel *c0, struct ast_channel *c1)
{
	channel_update_peeraccount(c0, c1);
	channel_update_peeraccount(c1, c0);
}

/*!
 * \internal
 * \brief Update channel accountcodes because a channel is joining a bridge.
 * \since 12.5.0
 *
 * \param joining Channel joining the bridge.
 * \param swap Channel being replaced by the joining channel.  May be NULL.
 *
 * \note The bridge must be locked prior to calling this function.
 *
 * \return Nothing
 */
static void bridge_channel_update_accountcodes_joining(struct ast_bridge_channel *joining, struct ast_bridge_channel *swap)
{
	struct ast_bridge *bridge = joining->bridge;
	struct ast_bridge_channel *other;
	unsigned int swap_in_bridge = 0;
	unsigned int will_be_two_party;

	/*
	 * Only update the peeraccount to match if the joining channel
	 * will make it a two party bridge.
	 */
	if (bridge->num_channels <= 2 && swap) {
		AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
			if (other == swap) {
				swap_in_bridge = 1;
				break;
			}
		}
	}
	will_be_two_party = (1 == bridge->num_channels - swap_in_bridge);

	AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
		if (other == swap) {
			continue;
		}
		ast_assert(joining != other);
		ast_channel_lock_both(joining->chan, other->chan);
		channel_set_empty_accountcodes(joining->chan, other->chan);
		if (will_be_two_party) {
			channel_update_peeraccounts(joining->chan, other->chan);
		}
		ast_channel_unlock(joining->chan);
		ast_channel_unlock(other->chan);
	}
}

/*!
 * \internal
 * \brief Update channel peeraccount codes because a channel has left a bridge.
 * \since 12.5.0
 *
 * \param leaving Channel leaving the bridge. (Has already been removed actually)
 *
 * \note The bridge must be locked prior to calling this function.
 *
 * \return Nothing
 */
static void bridge_channel_update_accountcodes_leaving(struct ast_bridge_channel *leaving)
{
	struct ast_bridge *bridge = leaving->bridge;
	struct ast_bridge_channel *first;
	struct ast_bridge_channel *second;

	if (bridge->num_channels != 2 || bridge->dissolved) {
		return;
	}

	first = AST_LIST_FIRST(&bridge->channels);
	second = AST_LIST_LAST(&bridge->channels);
	ast_assert(first && first != second);
	ast_channel_lock_both(first->chan, second->chan);
	channel_set_empty_accountcodes(first->chan, second->chan);
	channel_update_peeraccounts(first->chan, second->chan);
	ast_channel_unlock(second->chan);
	ast_channel_unlock(first->chan);
}

void ast_bridge_channel_update_accountcodes(struct ast_bridge_channel *joining, struct ast_bridge_channel *leaving)
{
	if (joining) {
		bridge_channel_update_accountcodes_joining(joining, leaving);
	} else {
		bridge_channel_update_accountcodes_leaving(leaving);
	}
}

void ast_bridge_channel_kick(struct ast_bridge_channel *bridge_channel, int cause)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	ast_bridge_channel_lock(bridge_channel);
	if (bridge_channel->state == BRIDGE_CHANNEL_STATE_WAIT) {
		channel_set_cause(bridge_channel->chan, cause);
		cause = 0;
	}
	ast_bridge_channel_unlock(bridge_channel);

	/* Run any hangup hooks. */
	iter = ao2_iterator_init(features->other_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		int remove_me;

		if (hook->type != AST_BRIDGE_HOOK_TYPE_HANGUP) {
			continue;
		}
		remove_me = hook->callback(bridge_channel, hook->hook_pvt);
		if (remove_me) {
			ast_debug(1, "Hangup hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->other_hooks, hook);
		}
	}
	ao2_iterator_destroy(&iter);

	/* Default hangup action. */
	ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END, cause);
}

/*!
 * \internal
 * \brief Write an \ref ast_frame onto the bridge channel
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to queue the frame onto.
 * \param frame The frame to write onto the bridge_channel
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int bridge_channel_write_frame(struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	ast_assert(frame->frametype != AST_FRAME_BRIDGE_ACTION_SYNC);

	ast_bridge_channel_lock_bridge(bridge_channel);
/*
 * XXX need to implement a deferred write queue for when there
 * is no peer channel in the bridge (yet or it was kicked).
 *
 * The tech decides if a frame needs to be pushed back for deferral.
 * simple_bridge/native_bridge are likely the only techs that will do this.
 */
	bridge_channel->bridge->technology->write(bridge_channel->bridge, bridge_channel, frame);

	/* Remember any owed events to the bridge. */
	switch (frame->frametype) {
	case AST_FRAME_DTMF_BEGIN:
		bridge_channel->owed.dtmf_tv = ast_tvnow();
		bridge_channel->owed.dtmf_digit = frame->subclass.integer;
		break;
	case AST_FRAME_DTMF_END:
		bridge_channel->owed.dtmf_digit = '\0';
		break;
	case AST_FRAME_CONTROL:
		/*
		 * We explicitly will not remember HOLD/UNHOLD frames because
		 * things like attended transfers will handle them.
		 */
	default:
		break;
	}
	ast_bridge_unlock(bridge_channel->bridge);

	/*
	 * Claim successful write to bridge.  If deferred frame
	 * support is added, claim successfully deferred.
	 */
	return 0;
}

void bridge_channel_settle_owed_events(struct ast_bridge *orig_bridge, struct ast_bridge_channel *bridge_channel)
{
	if (bridge_channel->owed.dtmf_digit) {
		struct ast_frame frame = {
			.frametype = AST_FRAME_DTMF_END,
			.subclass.integer = bridge_channel->owed.dtmf_digit,
			.src = "Bridge channel owed DTMF",
		};

		frame.len = ast_tvdiff_ms(ast_tvnow(), bridge_channel->owed.dtmf_tv);
		if (frame.len < option_dtmfminduration) {
			frame.len = option_dtmfminduration;
		}
		ast_log(LOG_DTMF, "DTMF end '%c' simulated to bridge %s because %s left.  Duration %ld ms.\n",
			bridge_channel->owed.dtmf_digit, orig_bridge->uniqueid,
			ast_channel_name(bridge_channel->chan), frame.len);
		bridge_channel->owed.dtmf_digit = '\0';
		orig_bridge->technology->write(orig_bridge, NULL, &frame);
	}
}

/*!
 * \internal
 * \brief Suspend a channel from a bridge.
 *
 * \param bridge_channel Channel to suspend.
 *
 * \note This function assumes bridge_channel->bridge is locked.
 *
 * \return Nothing
 */
void bridge_channel_internal_suspend_nolock(struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended = 1;
	if (bridge_channel->in_bridge) {
		--bridge_channel->bridge->num_active;
	}

	/* Get technology bridge threads off of the channel. */
	if (bridge_channel->bridge->technology->suspend) {
		bridge_channel->bridge->technology->suspend(bridge_channel->bridge, bridge_channel);
	}
}

/*!
 * \internal
 * \brief Suspend a channel from a bridge.
 *
 * \param bridge_channel Channel to suspend.
 *
 * \return Nothing
 */
static void bridge_channel_suspend(struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge_channel_internal_suspend_nolock(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);
}

/*!
 * \internal
 * \brief Unsuspend a channel from a bridge.
 *
 * \param bridge_channel Channel to unsuspend.
 *
 * \note This function assumes bridge_channel->bridge is locked.
 *
 * \return Nothing
 */
void bridge_channel_internal_unsuspend_nolock(struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended = 0;
	if (bridge_channel->in_bridge) {
		++bridge_channel->bridge->num_active;
	}

	/* Wake technology bridge threads to take care of channel again. */
	if (bridge_channel->bridge->technology->unsuspend) {
		bridge_channel->bridge->technology->unsuspend(bridge_channel->bridge, bridge_channel);
	}

	/* Wake suspended channel. */
	ast_bridge_channel_lock(bridge_channel);
	ast_cond_signal(&bridge_channel->cond);
	ast_bridge_channel_unlock(bridge_channel);
}

/*!
 * \internal
 * \brief Unsuspend a channel from a bridge.
 *
 * \param bridge_channel Channel to unsuspend.
 *
 * \return Nothing
 */
static void bridge_channel_unsuspend(struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge_channel_internal_unsuspend_nolock(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);
}

/*!
 * \internal
 * \brief Queue an action frame onto the bridge channel with data.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to queue the frame onto.
 * \param action Type of bridge action frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int bridge_channel_queue_action_data(struct ast_bridge_channel *bridge_channel,
	enum bridge_channel_action_type action, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = action,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return ast_bridge_channel_queue_frame(bridge_channel, &frame);
}

/*!
 * \internal
 * \brief Queue an action frame onto the bridge channel with data synchronously.
 * \since 12.2.0
 *
 * The function will not return until the queued frame is freed.
 *
 * \param bridge_channel Which channel to queue the frame onto.
 * \param action Type of bridge action frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int bridge_channel_queue_action_data_sync(struct ast_bridge_channel *bridge_channel,
	enum bridge_channel_action_type action, const void *data, size_t datalen)
{
	struct sync_payload *sync_payload;
	int sync_payload_len = sizeof(*sync_payload) + datalen;
	struct bridge_sync sync_struct;
	struct ast_frame frame = {
		.frametype = AST_FRAME_BRIDGE_ACTION_SYNC,
		.subclass.integer = action,
	};

	/* Make sure we don't end up trying to wait on ourself to deliver the frame */
	ast_assert(!pthread_equal(pthread_self(), bridge_channel->thread));

	sync_payload = ast_alloca(sync_payload_len);
	sync_payload->id = ast_atomic_fetchadd_int(&sync_ids, +1);
	memcpy(sync_payload->data, data, datalen);

	frame.datalen = sync_payload_len;
	frame.data.ptr = sync_payload;

	bridge_sync_init(&sync_struct, sync_payload->id);
	if (ast_bridge_channel_queue_frame(bridge_channel, &frame)) {
		bridge_sync_cleanup(&sync_struct);
		return -1;
	}

	bridge_sync_wait(&sync_struct);
	bridge_sync_cleanup(&sync_struct);
	return 0;
}
/*!
 * \internal
 * \brief Write an action frame onto the bridge channel with data.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to queue the frame onto.
 * \param action Type of bridge action frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int bridge_channel_write_action_data(struct ast_bridge_channel *bridge_channel,
	enum bridge_channel_action_type action, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = action,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return bridge_channel_write_frame(bridge_channel, &frame);
}

static void bridge_frame_free(struct ast_frame *frame)
{
	if (frame->frametype == AST_FRAME_BRIDGE_ACTION_SYNC) {
		struct sync_payload *sync_payload = frame->data.ptr;
		struct bridge_sync *sync;

		AST_RWLIST_RDLOCK(&sync_structs);
		AST_RWLIST_TRAVERSE(&sync_structs, sync, list) {
			if (sync->id == sync_payload->id) {
				break;
			}
		}
		if (sync) {
			bridge_sync_signal(sync);
		}
		AST_RWLIST_UNLOCK(&sync_structs);
	}

	ast_frfree(frame);
}

int ast_bridge_channel_queue_frame(struct ast_bridge_channel *bridge_channel, struct ast_frame *fr)
{
	struct ast_frame *dup;
	char nudge = 0;

	if (bridge_channel->suspended
		/* Also defer DTMF frames. */
		&& fr->frametype != AST_FRAME_DTMF_BEGIN
		&& fr->frametype != AST_FRAME_DTMF_END
		&& !ast_is_deferrable_frame(fr)) {
		/* Drop non-deferable frames when suspended. */
		return 0;
	}
	if (fr->frametype == AST_FRAME_NULL) {
		/* "Accept" the frame and discard it. */
		return 0;
	}

	dup = ast_frdup(fr);
	if (!dup) {
		return -1;
	}

	ast_bridge_channel_lock(bridge_channel);
	if (bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT) {
		/* Drop frames on channels leaving the bridge. */
		ast_bridge_channel_unlock(bridge_channel);
		bridge_frame_free(dup);
		return 0;
	}

	AST_LIST_INSERT_TAIL(&bridge_channel->wr_queue, dup, frame_list);
	if (write(bridge_channel->alert_pipe[1], &nudge, sizeof(nudge)) != sizeof(nudge)) {
		ast_log(LOG_ERROR, "We couldn't write alert pipe for %p(%s)... something is VERY wrong\n",
			bridge_channel, ast_channel_name(bridge_channel->chan));
	}
	ast_bridge_channel_unlock(bridge_channel);
	return 0;
}

int ast_bridge_queue_everyone_else(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_channel *cur;
	int not_written = -1;

	if (frame->frametype == AST_FRAME_NULL) {
		/* "Accept" the frame and discard it. */
		return 0;
	}

	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		if (cur == bridge_channel) {
			continue;
		}
		if (!ast_bridge_channel_queue_frame(cur, frame)) {
			not_written = 0;
		}
	}
	return not_written;
}

int ast_bridge_channel_queue_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = control,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return ast_bridge_channel_queue_frame(bridge_channel, &frame);
}

int ast_bridge_channel_write_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = control,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	return bridge_channel_write_frame(bridge_channel, &frame);
}

int ast_bridge_channel_write_hold(struct ast_bridge_channel *bridge_channel, const char *moh_class)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	size_t datalen;

	if (!ast_strlen_zero(moh_class)) {
		datalen = strlen(moh_class) + 1;

		blob = ast_json_pack("{s: s}",
			"musicclass", moh_class);
	} else {
		moh_class = NULL;
		datalen = 0;
	}

	ast_channel_publish_cached_blob(bridge_channel->chan, ast_channel_hold_type(), blob);

	return ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_HOLD,
		moh_class, datalen);
}

int ast_bridge_channel_write_unhold(struct ast_bridge_channel *bridge_channel)
{
	ast_channel_publish_cached_blob(bridge_channel->chan, ast_channel_unhold_type(), NULL);

	return ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_UNHOLD, NULL, 0);
}

/*!
 * \internal
 * \brief Helper function to kick off a PBX app on a bridge_channel
 */
static int run_app_helper(struct ast_channel *chan, const char *app_name, const char *app_args)
{
	int res = 0;

	if (!strcasecmp("Gosub", app_name)) {
		ast_app_exec_sub(NULL, chan, app_args, 0);
	} else if (!strcasecmp("Macro", app_name)) {
		ast_app_exec_macro(NULL, chan, app_args);
	} else {
		struct ast_app *app;

		app = pbx_findapp(app_name);
		if (!app) {
			ast_log(LOG_WARNING, "Could not find application (%s)\n", app_name);
		} else {
			struct ast_str *substituted_args = ast_str_create(16);

			if (substituted_args) {
				ast_str_substitute_variables(&substituted_args, 0, chan, app_args);
				res = pbx_exec(chan, app, ast_str_buffer(substituted_args));
				ast_free(substituted_args);
			} else {
				ast_log(LOG_WARNING, "Could not substitute application argument variables for %s\n", app_name);
				res = pbx_exec(chan, app, app_args);
			}
		}
	}
	return res;
}

void ast_bridge_channel_run_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	if (moh_class) {
		ast_bridge_channel_write_hold(bridge_channel, moh_class);
	}
	if (run_app_helper(bridge_channel->chan, app_name, S_OR(app_args, ""))) {
		/* Break the bridge if the app returns non-zero. */
		ast_bridge_channel_kick(bridge_channel, AST_CAUSE_NORMAL_CLEARING);
	}
	if (moh_class) {
		ast_bridge_channel_write_unhold(bridge_channel);
	}
}

struct bridge_run_app {
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

/*!
 * \internal
 * \brief Handle the run application bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to run the application on.
 * \param data Action frame data to run the application.
 *
 * \return Nothing
 */
static void bridge_channel_run_app(struct ast_bridge_channel *bridge_channel, struct bridge_run_app *data)
{
	ast_bridge_channel_run_app(bridge_channel, data->app_name,
		data->app_args_offset ? &data->app_name[data->app_args_offset] : NULL,
		data->moh_offset ? &data->app_name[data->moh_offset] : NULL);
}

/*!
 * \internal
 * \brief Marshal an application to be executed on a bridge_channel
 */
static int payload_helper_app(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	struct bridge_run_app *app_data;
	size_t len_name = strlen(app_name) + 1;
	size_t len_args = ast_strlen_zero(app_args) ? 0 : strlen(app_args) + 1;
	size_t len_moh = !moh_class ? 0 : strlen(moh_class) + 1;
	size_t len_data = sizeof(*app_data) + len_name + len_args + len_moh;

	/* Fill in application run frame data. */
	app_data = alloca(len_data);
	app_data->app_args_offset = len_args ? len_name : 0;
	app_data->moh_offset = len_moh ? len_name + len_args : 0;
	strcpy(app_data->app_name, app_name);/* Safe */
	if (len_args) {
		strcpy(&app_data->app_name[app_data->app_args_offset], app_args);/* Safe */
	}
	if (moh_class) {
		strcpy(&app_data->app_name[app_data->moh_offset], moh_class);/* Safe */
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_RUN_APP, app_data, len_data);
}

int ast_bridge_channel_write_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	return payload_helper_app(bridge_channel_write_action_data,
		bridge_channel, app_name, app_args, moh_class);
}

int ast_bridge_channel_queue_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	return payload_helper_app(bridge_channel_queue_action_data,
		bridge_channel, app_name, app_args, moh_class);
}

void ast_bridge_channel_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	if (moh_class) {
		ast_bridge_channel_write_hold(bridge_channel, moh_class);
	}
	if (custom_play) {
		custom_play(bridge_channel, playfile);
	} else {
		ast_stream_and_wait(bridge_channel->chan, playfile, AST_DIGIT_NONE);
	}
	if (moh_class) {
		ast_bridge_channel_write_unhold(bridge_channel);
	}

	/*
	 * It may be necessary to resume music on hold after we finish
	 * playing the announcment.
	 */
	if (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_MOH)) {
		const char *latest_musicclass;

		ast_channel_lock(bridge_channel->chan);
		latest_musicclass = ast_strdupa(ast_channel_latest_musicclass(bridge_channel->chan));
		ast_channel_unlock(bridge_channel->chan);
		ast_moh_start(bridge_channel->chan, latest_musicclass, NULL);
	}
}

struct bridge_playfile {
	/*! Call this function to play the playfile. (NULL if normal sound file to play) */
	ast_bridge_custom_play_fn custom_play;
	/*! Offset into playfile[] where the MOH class name starts.  (zero if no MOH)*/
	int moh_offset;
	/*! Filename to play. */
	char playfile[0];
};

/*!
 * \internal
 * \brief Handle the playfile bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to play a file on.
 * \param payload Action frame payload to play a file.
 *
 * \return Nothing
 */
static void bridge_channel_playfile(struct ast_bridge_channel *bridge_channel, struct bridge_playfile *payload)
{
	ast_bridge_channel_playfile(bridge_channel, payload->custom_play, payload->playfile,
		payload->moh_offset ? &payload->playfile[payload->moh_offset] : NULL);
}

/*!
 * \internal
 * \brief Marshal a file to be played on a bridge_channel
 */
static int payload_helper_playfile(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	struct bridge_playfile *payload;
	size_t len_name = strlen(playfile) + 1;
	size_t len_moh = !moh_class ? 0 : strlen(moh_class) + 1;
	size_t len_payload = sizeof(*payload) + len_name + len_moh;

	/* Fill in play file frame data. */
	payload = ast_alloca(len_payload);
	payload->custom_play = custom_play;
	payload->moh_offset = len_moh ? len_name : 0;
	strcpy(payload->playfile, playfile);/* Safe */
	if (moh_class) {
		strcpy(&payload->playfile[payload->moh_offset], moh_class);/* Safe */
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_PLAY_FILE, payload, len_payload);
}

int ast_bridge_channel_write_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	return payload_helper_playfile(bridge_channel_write_action_data,
		bridge_channel, custom_play, playfile, moh_class);
}

int ast_bridge_channel_queue_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	return payload_helper_playfile(bridge_channel_queue_action_data,
		bridge_channel, custom_play, playfile, moh_class);
}

int ast_bridge_channel_queue_playfile_sync(struct ast_bridge_channel *bridge_channel,
		ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	return payload_helper_playfile(bridge_channel_queue_action_data_sync,
		bridge_channel, custom_play, playfile, moh_class);
}

struct bridge_custom_callback {
	/*! Call this function on the bridge channel thread. */
	ast_bridge_custom_callback_fn callback;
	/*! Size of the payload if it exists.  A number otherwise. */
	size_t payload_size;
	/*! Option flags determining how callback is called. */
	unsigned int flags;
	/*! Nonzero if the payload exists. */
	char payload_exists;
	/*! Payload to give to callback. */
	char payload[0];
};

/*!
 * \internal
 * \brief Handle the do custom callback bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to call the callback on.
 * \param data Action frame data to call the callback.
 *
 * \return Nothing
 */
static void bridge_channel_do_callback(struct ast_bridge_channel *bridge_channel, struct bridge_custom_callback *data)
{
	if (ast_test_flag(data, AST_BRIDGE_CHANNEL_CB_OPTION_MEDIA)) {
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
	}
	data->callback(bridge_channel, data->payload_exists ? data->payload : NULL, data->payload_size);
	if (ast_test_flag(data, AST_BRIDGE_CHANNEL_CB_OPTION_MEDIA)) {
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
	}
}

/*!
 * \internal
 * \brief Marshal a custom callback function to be called on a bridge_channel
 */
static int payload_helper_cb(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel,
	enum ast_bridge_channel_custom_callback_option flags,
	ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	struct bridge_custom_callback *cb_data;
	size_t len_data = sizeof(*cb_data) + (payload ? payload_size : 0);

	/* Sanity check. */
	if (!callback) {
		ast_assert(0);
		return -1;
	}

	/* Fill in custom callback frame data. */
	cb_data = alloca(len_data);
	cb_data->callback = callback;
	cb_data->payload_size = payload_size;
	cb_data->flags = flags;
	cb_data->payload_exists = payload && payload_size;
	if (cb_data->payload_exists) {
		memcpy(cb_data->payload, payload, payload_size);/* Safe */
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_CALLBACK, cb_data, len_data);
}

int ast_bridge_channel_write_callback(struct ast_bridge_channel *bridge_channel,
	enum ast_bridge_channel_custom_callback_option flags,
	ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	return payload_helper_cb(bridge_channel_write_action_data,
		bridge_channel, flags, callback, payload, payload_size);
}

int ast_bridge_channel_queue_callback(struct ast_bridge_channel *bridge_channel,
	enum ast_bridge_channel_custom_callback_option flags,
	ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	return payload_helper_cb(bridge_channel_queue_action_data,
		bridge_channel, flags, callback, payload, payload_size);
}

struct bridge_park {
	int parker_uuid_offset;
	int app_data_offset;
	/* buffer used for holding those strings */
	char parkee_uuid[0];
};

/*!
 * \internal
 * \brief Park a bridge_cahnnel
 */
static void bridge_channel_park(struct ast_bridge_channel *bridge_channel, struct bridge_park *payload)
{
	if (!ast_parking_provider_registered()) {
		ast_log(AST_LOG_WARNING, "Unable to park %s: No parking provider loaded!\n",
			ast_channel_name(bridge_channel->chan));
		return;
	}

	if (ast_parking_park_bridge_channel(bridge_channel, payload->parkee_uuid,
		&payload->parkee_uuid[payload->parker_uuid_offset],
		payload->app_data_offset ? &payload->parkee_uuid[payload->app_data_offset] : NULL)) {
		ast_log(AST_LOG_WARNING, "Error occurred while parking %s\n",
			ast_channel_name(bridge_channel->chan));
	}
}

/*!
 * \internal
 * \brief Marshal a park action onto a bridge_channel
 */
static int payload_helper_park(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel,
	const char *parkee_uuid,
	const char *parker_uuid,
	const char *app_data)
{
	struct bridge_park *payload;
	size_t len_parkee_uuid = strlen(parkee_uuid) + 1;
	size_t len_parker_uuid = strlen(parker_uuid) + 1;
	size_t len_app_data = !app_data ? 0 : strlen(app_data) + 1;
	size_t len_payload = sizeof(*payload) + len_parker_uuid + len_parkee_uuid + len_app_data;

	payload = alloca(len_payload);
	payload->app_data_offset = len_app_data ? len_parkee_uuid + len_parker_uuid : 0;
	payload->parker_uuid_offset = len_parkee_uuid;
	strcpy(payload->parkee_uuid, parkee_uuid);
	strcpy(&payload->parkee_uuid[payload->parker_uuid_offset], parker_uuid);
	if (app_data) {
		strcpy(&payload->parkee_uuid[payload->app_data_offset], app_data);
	}

	return post_it(bridge_channel, BRIDGE_CHANNEL_ACTION_PARK, payload, len_payload);
}

int ast_bridge_channel_write_park(struct ast_bridge_channel *bridge_channel, const char *parkee_uuid, const char *parker_uuid, const char *app_data)
{
	return payload_helper_park(bridge_channel_write_action_data,
		bridge_channel, parkee_uuid, parker_uuid, app_data);
}

/*!
 * \internal
 * \brief Handle bridge channel interval expiration.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to run expired intervals on.
 *
 * \return Nothing
 */
static void bridge_channel_handle_interval(struct ast_bridge_channel *bridge_channel)
{
	struct ast_heap *interval_hooks;
	struct ast_bridge_hook_timer *hook;
	struct timeval start;
	int chan_suspended = 0;

	interval_hooks = bridge_channel->features->interval_hooks;
	ast_heap_wrlock(interval_hooks);
	start = ast_tvnow();
	while ((hook = ast_heap_peek(interval_hooks, 1))) {
		int interval;
		unsigned int execution_time;

		if (ast_tvdiff_ms(hook->timer.trip_time, start) > 0) {
			ast_debug(1, "Hook %p on %p(%s) wants to happen in the future, stopping our traversal\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}
		ao2_ref(hook, +1);
		ast_heap_unlock(interval_hooks);

		if (!chan_suspended
			&& ast_test_flag(&hook->timer, AST_BRIDGE_HOOK_TIMER_OPTION_MEDIA)) {
			chan_suspended = 1;
			bridge_channel_suspend(bridge_channel);
			ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		}

		ast_debug(1, "Executing hook %p on %p(%s)\n",
			hook, bridge_channel, ast_channel_name(bridge_channel->chan));
		interval = hook->generic.callback(bridge_channel, hook->generic.hook_pvt);

		ast_heap_wrlock(interval_hooks);
		if (ast_heap_peek(interval_hooks, hook->timer.heap_index) != hook
			|| !ast_heap_remove(interval_hooks, hook)) {
			/* Interval hook is already removed from the bridge_channel. */
			ao2_ref(hook, -1);
			continue;
		}
		ao2_ref(hook, -1);

		if (interval < 0) {
			ast_debug(1, "Removed interval hook %p from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_ref(hook, -1);
			continue;
		}
		if (interval) {
			/* Set new interval for the hook. */
			hook->timer.interval = interval;
		}

		ast_debug(1, "Updating interval hook %p with interval %u on %p(%s)\n",
			hook, hook->timer.interval, bridge_channel,
			ast_channel_name(bridge_channel->chan));

		/* resetting start */
		start = ast_tvnow();

		/*
		 * Resetup the interval hook for the next interval.  We may need
		 * to skip over any missed intervals because the hook was
		 * delayed or took too long.
		 */
		execution_time = ast_tvdiff_ms(start, hook->timer.trip_time);
		while (hook->timer.interval < execution_time) {
			execution_time -= hook->timer.interval;
		}
		hook->timer.trip_time = ast_tvadd(start, ast_samp2tv(hook->timer.interval - execution_time, 1000));
		hook->timer.seqno = ast_atomic_fetchadd_int((int *) &bridge_channel->features->interval_sequence, +1);

		if (ast_heap_push(interval_hooks, hook)) {
			/* Could not push the hook back onto the heap. */
			ao2_ref(hook, -1);
		}
	}
	ast_heap_unlock(interval_hooks);

	if (chan_suspended) {
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
	}
}

/*!
 * \internal
 * \brief Write a DTMF stream out to a channel
 */
static int bridge_channel_write_dtmf_stream(struct ast_bridge_channel *bridge_channel, const char *dtmf)
{
	return bridge_channel_write_action_data(bridge_channel,
		BRIDGE_CHANNEL_ACTION_DTMF_STREAM, dtmf, strlen(dtmf) + 1);
}

/*!
 * \internal
 * \brief Indicate to the testsuite a feature was successfully detected.
 *
 * Currently, this function only will relay built-in features to the testsuite,
 * but it could be modified to detect applicationmap items should the need arise.
 *
 * \param chan The channel that activated the feature
 * \param dtmf The DTMF sequence entered to activate the feature
 */
static void testsuite_notify_feature_success(struct ast_channel *chan, const char *dtmf)
{
#ifdef TEST_FRAMEWORK
	char *feature = "unknown";
	struct ast_featuremap_config *featuremap = ast_get_chan_featuremap_config(chan);
	struct ast_features_xfer_config *xfer = ast_get_chan_features_xfer_config(chan);

	if (featuremap) {
		if (!strcmp(dtmf, featuremap->blindxfer)) {
			feature = "blindxfer";
		} else if (!strcmp(dtmf, featuremap->atxfer)) {
			feature = "atxfer";
		} else if (!strcmp(dtmf, featuremap->disconnect)) {
			feature = "disconnect";
		} else if (!strcmp(dtmf, featuremap->automon)) {
			feature = "automon";
		} else if (!strcmp(dtmf, featuremap->automixmon)) {
			feature = "automixmon";
		} else if (!strcmp(dtmf, featuremap->parkcall)) {
			feature = "parkcall";
		}
	}
	if (xfer) {
		if (!strcmp(dtmf, xfer->atxferthreeway)) {
			feature = "atxferthreeway";
		}
	}

	ao2_cleanup(featuremap);
	ao2_cleanup(xfer);

	ast_test_suite_event_notify("FEATURE_DETECTION",
			"Result: success\r\n"
			"Feature: %s", feature);
#endif /* TEST_FRAMEWORK */
}

void ast_bridge_channel_feature_digit(struct ast_bridge_channel *bridge_channel, int digit)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook_dtmf *hook = NULL;
	size_t dtmf_len;

	struct sanity_check_of_dtmf_size {
		char check[1 / (ARRAY_LEN(bridge_channel->dtmf_hook_state.collected) == ARRAY_LEN(hook->dtmf.code))];
	};

	dtmf_len = strlen(bridge_channel->dtmf_hook_state.collected);
	if (!dtmf_len && !digit) {
		/* Nothing to do */
		return;
	}

	if (digit) {
		/* There should always be room for the new digit. */
		ast_assert(dtmf_len < ARRAY_LEN(bridge_channel->dtmf_hook_state.collected) - 1);

		/* Add the new digit to the DTMF string so we can do our matching */
		bridge_channel->dtmf_hook_state.collected[dtmf_len++] = digit;
		bridge_channel->dtmf_hook_state.collected[dtmf_len] = '\0';

		ast_debug(1, "DTMF feature string on %p(%s) is now '%s'\n",
			bridge_channel, ast_channel_name(bridge_channel->chan),
			bridge_channel->dtmf_hook_state.collected);

		/* See if a DTMF feature hook matches or can match */
		hook = ao2_find(features->dtmf_hooks, bridge_channel->dtmf_hook_state.collected,
			OBJ_SEARCH_PARTIAL_KEY);
		if (!hook) {
			ast_debug(1, "No DTMF feature hooks on %p(%s) match '%s'\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				bridge_channel->dtmf_hook_state.collected);
		} else if (dtmf_len != strlen(hook->dtmf.code)) {
			unsigned int digit_timeout;
			struct ast_features_general_config *gen_cfg;

			/* Need more digits to match */
			ao2_ref(hook, -1);

			/* Determine interdigit timeout */
			ast_channel_lock(bridge_channel->chan);
			gen_cfg = ast_get_chan_features_general_config(bridge_channel->chan);
			ast_channel_unlock(bridge_channel->chan);
			if (!gen_cfg) {
				ast_log(LOG_ERROR, "Unable to retrieve features configuration.\n");
				digit_timeout = 3000; /* Pick a reasonable failsafe timeout in ms */
			} else {
				digit_timeout = gen_cfg->featuredigittimeout;
				ao2_ref(gen_cfg, -1);
			}

			bridge_channel->dtmf_hook_state.interdigit_timeout =
				ast_tvadd(ast_tvnow(), ast_samp2tv(digit_timeout, 1000));
			return;
		} else {
			int remove_me;
			int already_suspended;

			ast_debug(1, "DTMF feature hook %p matched DTMF string '%s' on %p(%s)\n",
				hook, bridge_channel->dtmf_hook_state.collected, bridge_channel,
				ast_channel_name(bridge_channel->chan));

			/*
			 * Clear the collected digits before executing the hook
			 * in case the hook starts another sequence.
			 */
			bridge_channel->dtmf_hook_state.collected[0] = '\0';

			ast_bridge_channel_lock_bridge(bridge_channel);
			already_suspended = bridge_channel->suspended;
			if (!already_suspended) {
				bridge_channel_internal_suspend_nolock(bridge_channel);
			}
			ast_bridge_unlock(bridge_channel->bridge);
			ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);

			/* Execute the matched hook on this channel. */
			remove_me = hook->generic.callback(bridge_channel, hook->generic.hook_pvt);
			if (remove_me) {
				ast_debug(1, "DTMF hook %p is being removed from %p(%s)\n",
					hook, bridge_channel, ast_channel_name(bridge_channel->chan));
				ao2_unlink(features->dtmf_hooks, hook);
			}
			testsuite_notify_feature_success(bridge_channel->chan, hook->dtmf.code);
			ao2_ref(hook, -1);

			ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
			if (!already_suspended) {
				bridge_channel_unsuspend(bridge_channel);
			}

			/*
			 * If we are handing the channel off to an external hook for
			 * ownership, we are not guaranteed what kind of state it will
			 * come back in.  If the channel hungup, we need to detect that
			 * here if the hook did not already change the state.
			 */
			if (bridge_channel->chan && ast_check_hangup_locked(bridge_channel->chan)) {
				ast_bridge_channel_kick(bridge_channel, 0);
			}
			return;
		}
	} else {
		ast_debug(1, "DTMF feature string collection on %p(%s) timed out\n",
			bridge_channel, ast_channel_name(bridge_channel->chan));
	}

	/* Timeout or DTMF digit didn't allow a match with any hooks. */
	if (features->dtmf_passthrough) {
		/* Stream the collected DTMF to the other channels. */
		bridge_channel_write_dtmf_stream(bridge_channel,
			bridge_channel->dtmf_hook_state.collected);
	}
	bridge_channel->dtmf_hook_state.collected[0] = '\0';

	ast_test_suite_event_notify("FEATURE_DETECTION", "Result: fail");
}

/*!
 * \internal
 * \brief Handle bridge channel DTMF feature timeout expiration.
 * \since 12.8.0
 *
 * \param bridge_channel Channel to check expired interdigit timer on.
 *
 * \return Nothing
 */
static void bridge_channel_handle_feature_timeout(struct ast_bridge_channel *bridge_channel)
{
	if (!bridge_channel->dtmf_hook_state.collected[0]
		|| 0 < ast_tvdiff_ms(bridge_channel->dtmf_hook_state.interdigit_timeout,
			ast_tvnow())) {
		/* Not within a sequence or not timed out. */
		return;
	}

	ast_bridge_channel_feature_digit(bridge_channel, 0);
}

/*!
 * \internal
 * \brief Indicate that a bridge_channel is talking
 */
static void bridge_channel_talking(struct ast_bridge_channel *bridge_channel, int talking)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run any talk detection hooks. */
	iter = ao2_iterator_init(features->other_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		int remove_me;
		ast_bridge_talking_indicate_callback talk_cb;

		if (hook->type != AST_BRIDGE_HOOK_TYPE_TALK) {
			continue;
		}
		talk_cb = (ast_bridge_talking_indicate_callback) hook->callback;
		remove_me = talk_cb(bridge_channel, hook->hook_pvt, talking);
		if (remove_me) {
			ast_debug(1, "Talk detection hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->other_hooks, hook);
		}
	}
	ao2_iterator_destroy(&iter);
}

/*! \brief Internal function that plays back DTMF on a bridge channel */
static void bridge_channel_dtmf_stream(struct ast_bridge_channel *bridge_channel, const char *dtmf)
{
	ast_debug(1, "Playing DTMF stream '%s' out to %p(%s)\n",
		dtmf, bridge_channel, ast_channel_name(bridge_channel->chan));
	ast_dtmf_stream(bridge_channel->chan, NULL, dtmf, 0, 0);
}

/*! \brief Data specifying where a blind transfer is going to */
struct blind_transfer_data {
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_CONTEXT];
};

/*!
 * \internal
 * \brief Execute after bridge actions on a channel when it leaves a bridge
 */
static void after_bridge_move_channel(struct ast_channel *chan_bridged, void *data)
{
	RAII_VAR(struct ast_channel *, chan_target, data, ao2_cleanup);
	struct ast_party_connected_line connected_target;
	unsigned char connected_line_data[1024];
	int payload_size;

	ast_party_connected_line_init(&connected_target);

	ast_channel_lock(chan_target);
	ast_party_connected_line_copy(&connected_target, ast_channel_connected(chan_target));
	ast_channel_unlock(chan_target);
	ast_party_id_reset(&connected_target.priv);

	if (ast_channel_move(chan_target, chan_bridged)) {
		ast_softhangup(chan_target, AST_SOFTHANGUP_DEV);
		ast_party_connected_line_free(&connected_target);
		return;
	}

	if ((payload_size = ast_connected_line_build_data(connected_line_data,
		sizeof(connected_line_data), &connected_target, NULL)) != -1) {
		struct ast_control_read_action_payload *frame_payload;
		int frame_size;

		frame_size = payload_size + sizeof(*frame_payload);
		frame_payload = ast_alloca(frame_size);
		frame_payload->action = AST_FRAME_READ_ACTION_CONNECTED_LINE_MACRO;
		frame_payload->payload_size = payload_size;
		memcpy(frame_payload->payload, connected_line_data, payload_size);
		ast_queue_control_data(chan_target, AST_CONTROL_READ_ACTION, frame_payload, frame_size);
	}

	ast_party_connected_line_free(&connected_target);
}

/*!
 * \internal
 * \brief Execute logic to cleanup when after bridge fails
 */
static void after_bridge_move_channel_fail(enum ast_bridge_after_cb_reason reason, void *data)
{
	RAII_VAR(struct ast_channel *, chan_target, data, ao2_cleanup);

	ast_log(LOG_WARNING, "Unable to complete transfer: %s\n",
		ast_bridge_after_cb_reason_string(reason));
	ast_softhangup(chan_target, AST_SOFTHANGUP_DEV);
}

/*!
 * \internal
 * \brief Perform a blind transfer on a channel in a bridge
 */
static void bridge_channel_blind_transfer(struct ast_bridge_channel *bridge_channel,
		struct blind_transfer_data *blind_data)
{
	ast_async_goto(bridge_channel->chan, blind_data->context, blind_data->exten, 1);
	ast_bridge_channel_kick(bridge_channel, AST_CAUSE_NORMAL_CLEARING);
}

/*!
 * \internal
 * \brief Perform an attended transfer on a channel in a bridge
 */
static void bridge_channel_attended_transfer(struct ast_bridge_channel *bridge_channel,
		const char *target_chan_name)
{
	RAII_VAR(struct ast_channel *, chan_target, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan_bridged, NULL, ao2_cleanup);

	chan_target = ast_channel_get_by_name(target_chan_name);
	if (!chan_target) {
		/* Dang, it disappeared somehow */
		ast_bridge_channel_kick(bridge_channel, AST_CAUSE_NORMAL_CLEARING);
		return;
	}

	ast_bridge_channel_lock(bridge_channel);
	chan_bridged = bridge_channel->chan;
	ast_assert(chan_bridged != NULL);
	ao2_ref(chan_bridged, +1);
	ast_bridge_channel_unlock(bridge_channel);

	if (ast_bridge_set_after_callback(chan_bridged, after_bridge_move_channel,
		after_bridge_move_channel_fail, ast_channel_ref(chan_target))) {
		ast_softhangup(chan_target, AST_SOFTHANGUP_DEV);

		/* Release the ref we tried to pass to ast_bridge_set_after_callback(). */
		ast_channel_unref(chan_target);
	}
	ast_bridge_channel_kick(bridge_channel, AST_CAUSE_NORMAL_CLEARING);
}

/*!
 * \internal
 * \brief Handle bridge channel bridge action frame.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to execute the action on.
 * \param action What to do.
 * \param data data from the action.
 *
 * \return Nothing
 */
static void bridge_channel_handle_action(struct ast_bridge_channel *bridge_channel,
	enum bridge_channel_action_type action, void *data)
{
	switch (action) {
	case BRIDGE_CHANNEL_ACTION_DTMF_STREAM:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_dtmf_stream(bridge_channel, data);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_TALKING_START:
	case BRIDGE_CHANNEL_ACTION_TALKING_STOP:
		bridge_channel_talking(bridge_channel,
			action == BRIDGE_CHANNEL_ACTION_TALKING_START);
		break;
	case BRIDGE_CHANNEL_ACTION_PLAY_FILE:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_playfile(bridge_channel, data);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_RUN_APP:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_run_app(bridge_channel, data);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_CALLBACK:
		bridge_channel_do_callback(bridge_channel, data);
		break;
	case BRIDGE_CHANNEL_ACTION_PARK:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_park(bridge_channel, data);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case BRIDGE_CHANNEL_ACTION_BLIND_TRANSFER:
		bridge_channel_blind_transfer(bridge_channel, data);
		break;
	case BRIDGE_CHANNEL_ACTION_ATTENDED_TRANSFER:
		bridge_channel_attended_transfer(bridge_channel, data);
		break;
	default:
		break;
	}
}

/*!
 * \internal
 * \brief Check if a bridge should dissolve and do it.
 * \since 12.0.0
 *
 * \param bridge_channel Channel causing the check.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_channel_dissolve_check(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;

	if (bridge->dissolved) {
		return;
	}

	if (!bridge->num_channels
		&& ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE_EMPTY)) {
		/* Last channel leaving the bridge turns off the lights. */
		bridge_dissolve(bridge, ast_channel_hangupcause(bridge_channel->chan));
		return;
	}

	switch (bridge_channel->state) {
	case BRIDGE_CHANNEL_STATE_END:
		/* Do we need to dissolve the bridge because this channel hung up? */
		if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE_HANGUP)
			|| (bridge_channel->features->usable
				&& ast_test_flag(&bridge_channel->features->feature_flags,
					AST_BRIDGE_CHANNEL_FLAG_DISSOLVE_HANGUP))) {
			bridge_dissolve(bridge, ast_channel_hangupcause(bridge_channel->chan));
			return;
		}
		break;
	default:
		break;
	}

	if (bridge->num_lonely && bridge->num_lonely == bridge->num_channels) {
		/*
		 * This will start a chain reaction where each channel leaving
		 * enters this function and causes the next to leave as long as
		 * there aren't non-lonely channels in the bridge.
		 */
		ast_bridge_channel_leave_bridge(AST_LIST_FIRST(&bridge->channels),
			BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE,
			ast_channel_hangupcause(bridge_channel->chan));
	}
}

void bridge_channel_internal_pull(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;

	if (!bridge_channel->in_bridge) {
		return;
	}
	bridge_channel->in_bridge = 0;

	ast_debug(1, "Bridge %s: pulling %p(%s)\n",
		bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));

	ast_verb(3, "Channel %s left '%s' %s-bridge <%s>\n",
		ast_channel_name(bridge_channel->chan),
		bridge->technology->name,
		bridge->v_table->name,
		bridge->uniqueid);

	if (!bridge_channel->just_joined) {
		/* Tell the bridge technology we are leaving so they tear us down */
		ast_debug(1, "Bridge %s: %p(%s) is leaving %s technology\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			bridge->technology->name);
		if (bridge->technology->leave) {
			bridge->technology->leave(bridge, bridge_channel);
		}
	}

	/* Remove channel from the bridge */
	if (!bridge_channel->suspended) {
		--bridge->num_active;
	}
	if (ast_test_flag(&bridge_channel->features->feature_flags, AST_BRIDGE_CHANNEL_FLAG_LONELY)) {
		--bridge->num_lonely;
	}
	--bridge->num_channels;
	AST_LIST_REMOVE(&bridge->channels, bridge_channel, entry);

	bridge_channel_dissolve_check(bridge_channel);
	bridge->v_table->pull(bridge, bridge_channel);

	ast_bridge_channel_clear_roles(bridge_channel);

	/* If we are not going to be hung up after leaving a bridge, and we were an
	 * outgoing channel, clear the outgoing flag.
	 */
	if (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_OUTGOING)
	    && (ast_channel_is_leaving_bridge(bridge_channel->chan)
	        || bridge_channel->state == BRIDGE_CHANNEL_STATE_WAIT)) {
		ast_debug(2, "Channel %s will survive this bridge; clearing outgoing (dialed) flag\n", ast_channel_name(bridge_channel->chan));
		ast_clear_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_OUTGOING);
	}

	bridge->reconfigured = 1;
	ast_bridge_publish_leave(bridge, bridge_channel->chan);
}

int bridge_channel_internal_push(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_bridge_channel *swap;

	ast_assert(!bridge_channel->in_bridge);

	swap = bridge_find_channel(bridge, bridge_channel->swap);
	bridge_channel->swap = NULL;

	if (swap) {
		ast_debug(1, "Bridge %s: pushing %p(%s) by swapping with %p(%s)\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			swap, ast_channel_name(swap->chan));
	} else {
		ast_debug(1, "Bridge %s: pushing %p(%s)\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));
	}

	/* Add channel to the bridge */
	if (bridge->dissolved
		|| bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT
		|| (swap && swap->state != BRIDGE_CHANNEL_STATE_WAIT)
		|| bridge->v_table->push(bridge, bridge_channel, swap)
		|| ast_bridge_channel_establish_roles(bridge_channel)) {
		ast_debug(1, "Bridge %s: pushing %p(%s) into bridge failed\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));
		return -1;
	}
	bridge_channel->in_bridge = 1;
	bridge_channel->just_joined = 1;
	AST_LIST_INSERT_TAIL(&bridge->channels, bridge_channel, entry);
	++bridge->num_channels;
	if (ast_test_flag(&bridge_channel->features->feature_flags, AST_BRIDGE_CHANNEL_FLAG_LONELY)) {
		++bridge->num_lonely;
	}
	if (!bridge_channel->suspended) {
		++bridge->num_active;
	}

	ast_verb(3, "Channel %s %s%s%s '%s' %s-bridge <%s>\n",
		ast_channel_name(bridge_channel->chan),
		swap ? "swapped with " : "joined",
		swap ? ast_channel_name(swap->chan) : "",
		swap ? " into" : "",
		bridge->technology->name,
		bridge->v_table->name,
		bridge->uniqueid);

	ast_bridge_publish_enter(bridge, bridge_channel->chan, swap ? swap->chan : NULL);
	if (swap) {
		ast_bridge_channel_leave_bridge(swap, BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, 0);
		bridge_channel_internal_pull(swap);
	}

	/* Clear any BLINDTRANSFER and ATTENDEDTRANSFER since the transfer has completed. */
	pbx_builtin_setvar_helper(bridge_channel->chan, "BLINDTRANSFER", NULL);
	pbx_builtin_setvar_helper(bridge_channel->chan, "ATTENDEDTRANSFER", NULL);

	/* Wake up the bridge channel thread to reevaluate any interval timers. */
	ast_queue_frame(bridge_channel->chan, &ast_null_frame);

	bridge->reconfigured = 1;
	return 0;
}

/*!
 * \internal
 * \brief Handle bridge channel control frame action.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to execute the control frame action on.
 * \param fr Control frame to handle.
 *
 * \return Nothing
 */
static void bridge_channel_handle_control(struct ast_bridge_channel *bridge_channel, struct ast_frame *fr)
{
	struct ast_channel *chan;
	struct ast_option_header *aoh;
	int is_caller;

	chan = bridge_channel->chan;
	switch (fr->subclass.integer) {
	case AST_CONTROL_REDIRECTING:
		is_caller = !ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);
		if (ast_channel_redirecting_sub(NULL, chan, fr, 1) &&
			ast_channel_redirecting_macro(NULL, chan, fr, is_caller, 1)) {
			ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		}
		break;
	case AST_CONTROL_CONNECTED_LINE:
		is_caller = !ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);
		if (ast_channel_connected_line_sub(NULL, chan, fr, 1) &&
			ast_channel_connected_line_macro(NULL, chan, fr, is_caller, 1)) {
			ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		}
		break;
	case AST_CONTROL_OPTION:
		/*
		 * Forward option Requests, but only ones we know are safe These
		 * are ONLY sent by chan_iax2 and I'm not convinced that they
		 * are useful. I haven't deleted them entirely because I just am
		 * not sure of the ramifications of removing them.
		 */
		aoh = fr->data.ptr;
		if (aoh && aoh->flag == AST_OPTION_FLAG_REQUEST) {
			switch (ntohs(aoh->option)) {
			case AST_OPTION_TONE_VERIFY:
			case AST_OPTION_TDD:
			case AST_OPTION_RELAXDTMF:
			case AST_OPTION_AUDIO_MODE:
			case AST_OPTION_DIGIT_DETECT:
			case AST_OPTION_FAX_DETECT:
				ast_channel_setoption(chan, ntohs(aoh->option), aoh->data,
					fr->datalen - sizeof(*aoh), 0);
				break;
			default:
				break;
			}
		}
		break;
	case AST_CONTROL_ANSWER:
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_answer(chan);
		} else {
			ast_indicate(chan, -1);
		}
		break;
	case AST_CONTROL_MASQUERADE_NOTIFY:
		/* Should never happen. */
		ast_assert(0);
		break;
	default:
		ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		break;
	}
}

/*!
 * \internal
 * \param bridge_channel Channel to read wr_queue alert pipe.
 *
 * \return Nothing
 */
static void bridge_channel_read_wr_queue_alert(struct ast_bridge_channel *bridge_channel)
{
	char nudge;

	if (read(bridge_channel->alert_pipe[0], &nudge, sizeof(nudge)) < 0) {
		if (errno != EINTR && errno != EAGAIN) {
			ast_log(LOG_WARNING, "read() failed for alert pipe on %p(%s): %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				strerror(errno));
		}
	}
}

/*!
 * \internal
 * \brief Handle bridge channel write frame to channel.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to write outgoing frame.
 *
 * \return Nothing
 */
static void bridge_channel_handle_write(struct ast_bridge_channel *bridge_channel)
{
	struct ast_frame *fr;
	struct sync_payload *sync_payload;

	ast_bridge_channel_lock(bridge_channel);

	/* It's not good to have unbalanced frames and alert_pipe alerts. */
	ast_assert(!AST_LIST_EMPTY(&bridge_channel->wr_queue));
	if (AST_LIST_EMPTY(&bridge_channel->wr_queue)) {
		/* No frame, flush the alert pipe of excess alerts. */
		ast_log(LOG_WARNING, "Weird.  No frame from bridge for %s to process?\n",
			ast_channel_name(bridge_channel->chan));
		bridge_channel_read_wr_queue_alert(bridge_channel);
		ast_bridge_channel_unlock(bridge_channel);
		return;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(&bridge_channel->wr_queue, fr, frame_list) {
		if (bridge_channel->dtmf_hook_state.collected[0]) {
			switch (fr->frametype) {
			case AST_FRAME_BRIDGE_ACTION:
			case AST_FRAME_BRIDGE_ACTION_SYNC:
				/* Defer processing these frames while DTMF is collected. */
				continue;
			default:
				break;
			}
		}
		bridge_channel_read_wr_queue_alert(bridge_channel);
		AST_LIST_REMOVE_CURRENT(frame_list);
		break;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ast_bridge_channel_unlock(bridge_channel);
	if (!fr) {
		/*
		 * Wait some to reduce CPU usage from a tight loop
		 * without any wait because we only have deferred
		 * frames in the wr_queue.
		 */
		usleep(1);
		return;
	}

	switch (fr->frametype) {
	case AST_FRAME_BRIDGE_ACTION:
		bridge_channel_handle_action(bridge_channel, fr->subclass.integer, fr->data.ptr);
		break;
	case AST_FRAME_BRIDGE_ACTION_SYNC:
		sync_payload = fr->data.ptr;
		bridge_channel_handle_action(bridge_channel, fr->subclass.integer, sync_payload->data);
		break;
	case AST_FRAME_CONTROL:
		bridge_channel_handle_control(bridge_channel, fr);
		break;
	case AST_FRAME_NULL:
		break;
	default:
		/* Write the frame to the channel. */
		bridge_channel->activity = BRIDGE_CHANNEL_THREAD_SIMPLE;
		ast_write(bridge_channel->chan, fr);
		break;
	}
	bridge_frame_free(fr);
}

/*! \brief Internal function to handle DTMF from a channel */
static struct ast_frame *bridge_handle_dtmf(struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook_dtmf *hook = NULL;
	char dtmf[2];

	/*
	 * See if we are already matching a DTMF feature hook sequence or
	 * if this DTMF matches the beginning of any DTMF feature hooks.
	 */
	dtmf[0] = frame->subclass.integer;
	dtmf[1] = '\0';
	if (bridge_channel->dtmf_hook_state.collected[0]
		|| (hook = ao2_find(features->dtmf_hooks, dtmf, OBJ_SEARCH_PARTIAL_KEY))) {
		enum ast_frame_type frametype = frame->frametype;

		bridge_frame_free(frame);
		frame = NULL;

		ao2_cleanup(hook);

		switch (frametype) {
		case AST_FRAME_DTMF_BEGIN:
			/* Just eat the frame. */
			break;
		case AST_FRAME_DTMF_END:
			ast_bridge_channel_feature_digit(bridge_channel, dtmf[0]);
			break;
		default:
			/* Unexpected frame type. */
			ast_assert(0);
			break;
		}
#ifdef TEST_FRAMEWORK
	} else if (frame->frametype == AST_FRAME_DTMF_END) {
		/* Only transmit this event on DTMF end or else every DTMF
		 * press will result in the event being broadcast twice
		 */
		ast_test_suite_event_notify("FEATURE_DETECTION", "Result: fail");
#endif
	}

	return frame;
}


/*!
 * \internal
 * \brief Feed notification that a frame is waiting on a channel into the bridging core
 *
 * \param bridge_channel Bridge channel the notification was received on
 */
static void bridge_handle_trip(struct ast_bridge_channel *bridge_channel)
{
	struct ast_frame *frame;

	if (bridge_channel->features->mute) {
		frame = ast_read_noaudio(bridge_channel->chan);
	} else {
		frame = ast_read(bridge_channel->chan);
	}

	if (!frame) {
		ast_bridge_channel_kick(bridge_channel, 0);
		return;
	}
	switch (frame->frametype) {
	case AST_FRAME_CONTROL:
		switch (frame->subclass.integer) {
		case AST_CONTROL_HANGUP:
			ast_bridge_channel_kick(bridge_channel, 0);
			bridge_frame_free(frame);
			return;
		default:
			break;
		}
		break;
	case AST_FRAME_DTMF_BEGIN:
	case AST_FRAME_DTMF_END:
		frame = bridge_handle_dtmf(bridge_channel, frame);
		if (!frame) {
			return;
		}
		if (!bridge_channel->features->dtmf_passthrough) {
			bridge_frame_free(frame);
			return;
		}
		break;
	default:
		break;
	}

	/* Simply write the frame out to the bridge technology. */
	bridge_channel_write_frame(bridge_channel, frame);
	bridge_frame_free(frame);
}

/*!
 * \internal
 * \brief Determine how long till the next timer interval.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to determine how long can wait.
 *
 * \retval ms Number of milliseconds to wait.
 * \retval -1 to wait forever.
 */
static int bridge_channel_next_interval(struct ast_bridge_channel *bridge_channel)
{
	struct ast_heap *interval_hooks = bridge_channel->features->interval_hooks;
	struct ast_bridge_hook_timer *hook;
	int ms;

	ast_heap_wrlock(interval_hooks);
	hook = ast_heap_peek(interval_hooks, 1);
	if (hook) {
		ms = ast_tvdiff_ms(hook->timer.trip_time, ast_tvnow());
		if (ms < 0) {
			/* Expire immediately.  An interval hook is ready to run. */
			ms = 0;
		}
	} else {
		/* No hook so wait forever. */
		ms = -1;
	}
	ast_heap_unlock(interval_hooks);

	return ms;
}

/*!
 * \internal
 * \brief Determine how long till the DTMF interdigit timeout.
 * \since 12.8.0
 *
 * \param bridge_channel Channel to determine how long can wait.
 *
 * \retval ms Number of milliseconds to wait.
 * \retval -1 to wait forever.
 */
static int bridge_channel_feature_timeout(struct ast_bridge_channel *bridge_channel)
{
	int ms;

	if (bridge_channel->dtmf_hook_state.collected[0]) {
		ms = ast_tvdiff_ms(bridge_channel->dtmf_hook_state.interdigit_timeout,
			ast_tvnow());
		if (ms < 0) {
			/* Expire immediately. */
			ms = 0;
		}
	} else {
		/* Timer is not active so wait forever. */
		ms = -1;
	}

	return ms;
}

/*!
 * \internal
 * \brief Determine how long till a timeout.
 * \since 12.8.0
 *
 * \param bridge_channel Channel to determine how long can wait.
 *
 * \retval ms Number of milliseconds to wait.
 * \retval -1 to wait forever.
 */
static int bridge_channel_next_timeout(struct ast_bridge_channel *bridge_channel)
{
	int ms_interval;
	int ms;

	ms_interval = bridge_channel_next_interval(bridge_channel);
	ms = bridge_channel_feature_timeout(bridge_channel);
	if (ms < 0 || (0 <= ms_interval && ms_interval < ms)) {
		/* Interval hook timeout is next. */
		ms = ms_interval;
	}

	return ms;
}

/*!
 * \internal
 * \brief Wait for something to happen on the bridge channel and handle it.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to wait.
 *
 * \note Each channel does writing/reading in their own thread.
 *
 * \return Nothing
 */
static void bridge_channel_wait(struct ast_bridge_channel *bridge_channel)
{
	int ms;
	int outfd;
	struct ast_channel *chan;

	/* Wait for data to either come from the channel or us to be signaled */
	ast_bridge_channel_lock(bridge_channel);
	if (bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT) {
	} else if (bridge_channel->suspended) {
/* XXX ASTERISK-21271 the external party use of suspended will go away as will these references because this is the bridge channel thread */
		ast_debug(1, "Bridge %s: %p(%s) is going into a signal wait\n",
			bridge_channel->bridge->uniqueid, bridge_channel,
			ast_channel_name(bridge_channel->chan));
		ast_cond_wait(&bridge_channel->cond, ao2_object_get_lockaddr(bridge_channel));
	} else {
		ast_bridge_channel_unlock(bridge_channel);
		outfd = -1;
		ms = bridge_channel_next_timeout(bridge_channel);
		chan = ast_waitfor_nandfds(&bridge_channel->chan, 1,
			&bridge_channel->alert_pipe[0], 1, NULL, &outfd, &ms);
		if (ast_channel_unbridged(bridge_channel->chan)) {
			ast_channel_set_unbridged(bridge_channel->chan, 0);
			ast_bridge_channel_lock_bridge(bridge_channel);
			bridge_channel->bridge->reconfigured = 1;
			bridge_reconfigured(bridge_channel->bridge, 0);
			ast_bridge_unlock(bridge_channel->bridge);
		}
		ast_bridge_channel_lock(bridge_channel);
		bridge_channel->activity = BRIDGE_CHANNEL_THREAD_FRAME;
		ast_bridge_channel_unlock(bridge_channel);
		if (!bridge_channel->suspended
			&& bridge_channel->state == BRIDGE_CHANNEL_STATE_WAIT) {
			if (chan) {
				bridge_handle_trip(bridge_channel);
			} else if (ms == 0) {
				/* An interdigit timeout or interval expired. */
				bridge_channel_handle_feature_timeout(bridge_channel);
				bridge_channel_handle_interval(bridge_channel);
			} else if (-1 < outfd) {
				/*
				 * Must do this after checking timeouts or may have
				 * an infinite loop due to deferring write queue
				 * actions while trying to match DTMF feature hooks.
				 */
				bridge_channel_handle_write(bridge_channel);
			}
		}
		bridge_channel->activity = BRIDGE_CHANNEL_THREAD_IDLE;
		return;
	}
	ast_bridge_channel_unlock(bridge_channel);
}

/*!
 * \internal
 * \brief Handle bridge channel join/leave event.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is involved.
 * \param type Specified join/leave event.
 *
 * \return Nothing
 */
static void bridge_channel_event_join_leave(struct ast_bridge_channel *bridge_channel, enum ast_bridge_hook_type type)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run the specified hooks. */
	iter = ao2_iterator_init(features->other_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		if (hook->type == type) {
			break;
		}
	}
	if (hook) {
		/* Found the first specified hook to run. */
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		do {
			if (hook->type == type) {
				hook->callback(bridge_channel, hook->hook_pvt);
				ao2_unlink(features->other_hooks, hook);
			}
			ao2_ref(hook, -1);
		} while ((hook = ao2_iterator_next(&iter)));
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
	}
	ao2_iterator_destroy(&iter);
}

/*! \brief Join a channel to a bridge and handle anything the bridge may want us to do */
int bridge_channel_internal_join(struct ast_bridge_channel *bridge_channel)
{
	int res = 0;
	struct ast_bridge_features *channel_features;

	ast_debug(1, "Bridge %s: %p(%s) is joining\n",
		bridge_channel->bridge->uniqueid,
		bridge_channel, ast_channel_name(bridge_channel->chan));

	/*
	 * Directly locking the bridge is safe here because nobody else
	 * knows about this bridge_channel yet.
	 */
	ast_bridge_lock(bridge_channel->bridge);

	ast_channel_lock(bridge_channel->chan);

	bridge_channel->read_format = ao2_bump(ast_channel_readformat(bridge_channel->chan));
	bridge_channel->write_format = ao2_bump(ast_channel_writeformat(bridge_channel->chan));

	/* Make sure we're still good to be put into a bridge */
	if (ast_channel_internal_bridge(bridge_channel->chan)
		|| ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_ZOMBIE)) {
		ast_channel_unlock(bridge_channel->chan);
		ast_bridge_unlock(bridge_channel->bridge);
		ast_debug(1, "Bridge %s: %p(%s) failed to join Bridge\n",
			bridge_channel->bridge->uniqueid,
			bridge_channel,
			ast_channel_name(bridge_channel->chan));
		return -1;
	}
	ast_channel_internal_bridge_set(bridge_channel->chan, bridge_channel->bridge);

	/* Attach features requested by the channel */
	channel_features = ast_channel_feature_hooks_get(bridge_channel->chan);
	if (channel_features) {
		ast_bridge_features_merge(bridge_channel->features, channel_features);
	}
	ast_channel_unlock(bridge_channel->chan);

	/* Add the jitterbuffer if the channel requires it */
	ast_jb_enable_for_channel(bridge_channel->chan);

	if (!bridge_channel->bridge->callid) {
		bridge_channel->bridge->callid = ast_read_threadstorage_callid();
	}

	if (bridge_channel_internal_push(bridge_channel)) {
		int cause = bridge_channel->bridge->cause;

		ast_bridge_unlock(bridge_channel->bridge);
		ast_bridge_channel_kick(bridge_channel, cause);
		ast_bridge_channel_lock_bridge(bridge_channel);
		ast_bridge_features_remove(bridge_channel->features,
			AST_BRIDGE_HOOK_REMOVE_ON_PULL);
		bridge_channel_dissolve_check(bridge_channel);
		res = -1;
	}
	bridge_reconfigured(bridge_channel->bridge, !bridge_channel->inhibit_colp);

	if (bridge_channel->state == BRIDGE_CHANNEL_STATE_WAIT) {
		/*
		 * Indicate a source change since this channel is entering the
		 * bridge system only if the bridge technology is not MULTIMIX
		 * capable.  The MULTIMIX technology has already done it.
		 */
		if (!(bridge_channel->bridge->technology->capabilities
			& AST_BRIDGE_CAPABILITY_MULTIMIX)) {
			ast_indicate(bridge_channel->chan, AST_CONTROL_SRCCHANGE);
		}

		ast_bridge_unlock(bridge_channel->bridge);
		bridge_channel_event_join_leave(bridge_channel, AST_BRIDGE_HOOK_TYPE_JOIN);

		while (bridge_channel->state == BRIDGE_CHANNEL_STATE_WAIT) {
			/* Wait for something to do. */
			bridge_channel_wait(bridge_channel);
		}

		/* Force a timeout on any accumulated DTMF hook digits. */
		ast_bridge_channel_feature_digit(bridge_channel, 0);

		bridge_channel_event_join_leave(bridge_channel, AST_BRIDGE_HOOK_TYPE_LEAVE);
		ast_bridge_channel_lock_bridge(bridge_channel);
	}

	bridge_channel_internal_pull(bridge_channel);
	bridge_channel_settle_owed_events(bridge_channel->bridge, bridge_channel);
	bridge_reconfigured(bridge_channel->bridge, 1);

	ast_bridge_unlock(bridge_channel->bridge);

	/* Complete any active hold before exiting the bridge. */
	if (ast_channel_hold_state(bridge_channel->chan) == AST_CONTROL_HOLD) {
		ast_debug(1, "Channel %s simulating UNHOLD for bridge end.\n",
			ast_channel_name(bridge_channel->chan));
		ast_indicate(bridge_channel->chan, AST_CONTROL_UNHOLD);
	}

	/* Complete any partial DTMF digit before exiting the bridge. */
	if (ast_channel_sending_dtmf_digit(bridge_channel->chan)) {
		ast_channel_end_dtmf(bridge_channel->chan,
			ast_channel_sending_dtmf_digit(bridge_channel->chan),
			ast_channel_sending_dtmf_tv(bridge_channel->chan), "bridge end");
	}

	/* Indicate a source change since this channel is leaving the bridge system. */
	ast_indicate(bridge_channel->chan, AST_CONTROL_SRCCHANGE);

	/*
	 * Wait for any dual redirect to complete.
	 *
	 * Must be done while "still in the bridge" for ast_async_goto()
	 * to work right.
	 */
	while (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT)) {
		sched_yield();
	}
	ast_channel_lock(bridge_channel->chan);
	ast_channel_internal_bridge_set(bridge_channel->chan, NULL);
	ast_channel_unlock(bridge_channel->chan);

	ast_bridge_channel_restore_formats(bridge_channel);

	return res;
}

int bridge_channel_internal_queue_blind_transfer(struct ast_channel *transferee,
		const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data)
{
	RAII_VAR(struct ast_bridge_channel *, transferee_bridge_channel, NULL, ao2_cleanup);
	struct blind_transfer_data blind_data;

	ast_channel_lock(transferee);
	transferee_bridge_channel = ast_channel_get_bridge_channel(transferee);
	ast_channel_unlock(transferee);

	if (!transferee_bridge_channel) {
		return -1;
	}

	if (new_channel_cb) {
		new_channel_cb(transferee, user_data, AST_BRIDGE_TRANSFER_SINGLE_PARTY);
	}

	ast_copy_string(blind_data.exten, exten, sizeof(blind_data.exten));
	ast_copy_string(blind_data.context, context, sizeof(blind_data.context));

	return bridge_channel_queue_action_data(transferee_bridge_channel,
		BRIDGE_CHANNEL_ACTION_BLIND_TRANSFER, &blind_data, sizeof(blind_data));
}

int bridge_channel_internal_queue_attended_transfer(struct ast_channel *transferee,
		struct ast_channel *unbridged_chan)
{
	RAII_VAR(struct ast_bridge_channel *, transferee_bridge_channel, NULL, ao2_cleanup);
	char unbridged_chan_name[AST_CHANNEL_NAME];

	ast_channel_lock(transferee);
	transferee_bridge_channel = ast_channel_get_bridge_channel(transferee);
	ast_channel_unlock(transferee);

	if (!transferee_bridge_channel) {
		return -1;
	}

	ast_copy_string(unbridged_chan_name, ast_channel_name(unbridged_chan),
		sizeof(unbridged_chan_name));

	return bridge_channel_queue_action_data(transferee_bridge_channel,
		BRIDGE_CHANNEL_ACTION_ATTENDED_TRANSFER, unbridged_chan_name,
		sizeof(unbridged_chan_name));
}

int bridge_channel_internal_allows_optimization(struct ast_bridge_channel *bridge_channel)
{
	return bridge_channel->in_bridge
		&& AST_LIST_EMPTY(&bridge_channel->wr_queue);
}

/*!
 * \internal
 * \brief Close a pipe.
 * \since 12.0.0
 *
 * \param my_pipe What to close.
 *
 * \return Nothing
 */
static void pipe_close(int *my_pipe)
{
	if (my_pipe[0] > -1) {
		close(my_pipe[0]);
		my_pipe[0] = -1;
	}
	if (my_pipe[1] > -1) {
		close(my_pipe[1]);
		my_pipe[1] = -1;
	}
}

/*!
 * \internal
 * \brief Initialize a pipe as non-blocking.
 * \since 12.0.0
 *
 * \param my_pipe What to initialize.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int pipe_init_nonblock(int *my_pipe)
{
	int flags;

	my_pipe[0] = -1;
	my_pipe[1] = -1;
	if (pipe(my_pipe)) {
		ast_log(LOG_WARNING, "Can't create pipe! Try increasing max file descriptors with ulimit -n\n");
		return -1;
	}
	flags = fcntl(my_pipe[0], F_GETFL);
	if (fcntl(my_pipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
		ast_log(LOG_WARNING, "Unable to set read pipe nonblocking! (%d: %s)\n",
			errno, strerror(errno));
		return -1;
	}
	flags = fcntl(my_pipe[1], F_GETFL);
	if (fcntl(my_pipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
		ast_log(LOG_WARNING, "Unable to set write pipe nonblocking! (%d: %s)\n",
			errno, strerror(errno));
		return -1;
	}
	return 0;
}

/* Destroy elements of the bridge channel structure and the bridge channel structure itself */
static void bridge_channel_destroy(void *obj)
{
	struct ast_bridge_channel *bridge_channel = obj;
	struct ast_frame *fr;

	if (bridge_channel->callid) {
		bridge_channel->callid = ast_callid_unref(bridge_channel->callid);
	}

	if (bridge_channel->bridge) {
		ao2_ref(bridge_channel->bridge, -1);
		bridge_channel->bridge = NULL;
	}

	/* Flush any unhandled wr_queue frames. */
	while ((fr = AST_LIST_REMOVE_HEAD(&bridge_channel->wr_queue, frame_list))) {
		bridge_frame_free(fr);
	}
	pipe_close(bridge_channel->alert_pipe);

	ast_cond_destroy(&bridge_channel->cond);

	ao2_cleanup(bridge_channel->write_format);
	ao2_cleanup(bridge_channel->read_format);
}

struct ast_bridge_channel *bridge_channel_internal_alloc(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel;

	bridge_channel = ao2_alloc(sizeof(struct ast_bridge_channel), bridge_channel_destroy);
	if (!bridge_channel) {
		return NULL;
	}
	ast_cond_init(&bridge_channel->cond, NULL);
	if (pipe_init_nonblock(bridge_channel->alert_pipe)) {
		ao2_ref(bridge_channel, -1);
		return NULL;
	}
	if (bridge) {
		bridge_channel->bridge = bridge;
		ao2_ref(bridge_channel->bridge, +1);
	}

	return bridge_channel;
}
