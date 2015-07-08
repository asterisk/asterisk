/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
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

#ifndef _ASTERISK_PRIVATE_BRIDGING_CHANNEL_H
#define _ASTERISK_PRIVATE_BRIDGING_CHANNEL_H

/*!
 * \file
 * \brief Private Bridging Channel API
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 * A private API to manipulate channels in a bridge. These can be called on a channel in
 * a bridge by \ref bridge.c. These functions should not be called elsewhere, including
 * by other members of the Bridging API.
 *
 * See Also:
 * \arg \ref AstCREDITS
 * \arg \ref Ast
 */

/*!
 * \internal
 * \brief Actions that can be taken on a channel in a bridge
 */
enum bridge_channel_action_type {
	/*! Bridged channel is to send a DTMF stream out */
	BRIDGE_CHANNEL_ACTION_DTMF_STREAM,
	/*! Bridged channel is to indicate talking start */
	BRIDGE_CHANNEL_ACTION_TALKING_START,
	/*! Bridged channel is to indicate talking stop */
	BRIDGE_CHANNEL_ACTION_TALKING_STOP,
	/*! Bridge channel is to play the indicated sound file. */
	BRIDGE_CHANNEL_ACTION_PLAY_FILE,
	/*! Bridge channel is to run the indicated application. */
	BRIDGE_CHANNEL_ACTION_RUN_APP,
	/*! Bridge channel is to run the custom callback routine. */
	BRIDGE_CHANNEL_ACTION_CALLBACK,
	/*! Bridge channel is to get parked. */
	BRIDGE_CHANNEL_ACTION_PARK,
	/*! Bridge channel is to execute a blind transfer. */
	BRIDGE_CHANNEL_ACTION_BLIND_TRANSFER,
	/*! Bridge channel is to execute an attended transfer */
	BRIDGE_CHANNEL_ACTION_ATTENDED_TRANSFER,

	/*
	 * Bridge actions put after this comment must never be put onto
	 * the bridge_channel wr_queue because they have other resources
	 * that must be freed.
	 */

	/*! Bridge reconfiguration deferred technology destruction. */
	BRIDGE_CHANNEL_ACTION_DEFERRED_TECH_DESTROY = 1000,
	/*! Bridge deferred dissolving. */
	BRIDGE_CHANNEL_ACTION_DEFERRED_DISSOLVING,
};

/*!
 * \internal
 * \brief Allocate a new ao2 ref counted bridge_channel
 * \since 12.0.0
 *
 * \param bridge The bridge to make the bridge_channel for
 *
 * \retval NULL on error
 * \retval ao2 ref counted object on success
 */
struct ast_bridge_channel *bridge_channel_internal_alloc(struct ast_bridge *bridge);

/*!
 * \internal
 * \brief Clear owed events by the channel to the original bridge.
 * \since 12.0.0
 *
 * \param orig_bridge Original bridge the channel was in before leaving.
 * \param bridge_channel Channel that owes events to the original bridge.
 *
 * \note On entry, the orig_bridge is already locked.
 *
 * \return Nothing
 */
void bridge_channel_settle_owed_events(struct ast_bridge *orig_bridge, struct ast_bridge_channel *bridge_channel);

/*!
 * \internal
 * \brief Push the bridge channel into its specified bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to push.
 *
 * \note A ref is not held by bridge_channel->swap when calling because the
 * push with swap happens immediately.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel did not get pushed.
 *
 * \note On failure the caller must call
 * ast_bridge_features_remove(bridge_channel->features, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
 */
int bridge_channel_internal_push(struct ast_bridge_channel *bridge_channel);

/*!
 * \internal
 * \brief Pull the bridge channel out of its current bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to pull.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \return Nothing
 */
void bridge_channel_internal_pull(struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Internal bridge channel wait condition and associated result.
 */
struct bridge_channel_internal_cond {
	/*! Lock for the data structure */
	ast_mutex_t lock;
	/*! Wait condition */
	ast_cond_t cond;
	/*! Wait until done */
	int done;
	/*! The bridge channel */
	struct ast_bridge_channel *bridge_channel;
};

/*!
 * \internal
 * \brief Wait for the expected signal.
 *
 * \param cond the wait object
 */
void bridge_channel_internal_wait(struct bridge_channel_internal_cond *cond);

/*!
 * \internal
 * \brief Signal the condition wait.
 *
 * \param cond the wait object
 */
void bridge_channel_internal_signal(struct bridge_channel_internal_cond *cond);

/*!
 * \internal
 * \brief Join the bridge_channel to the bridge (blocking)
 *
 * \param bridge_channel The Channel in the bridge
 * \param cond data used for signaling
 *
 * \note The bridge_channel->swap holds a channel reference for the swap
 * channel going into the bridging system.  The ref ensures that the swap
 * pointer is valid for the bridge subclass push callbacks.  The pointer
 * will be NULL on return if the ref was consumed.
 *
 * \details
 * This API call puts the bridge_channel into the bridge and handles the
 * bridge_channel's processing of events while it is in the bridge.  It
 * will return when the channel has been instructed to leave the bridge.
 *
 * \retval 0 bridge channel successfully joined the bridge
 * \retval -1 bridge channel failed to join the bridge
 */
int bridge_channel_internal_join(struct ast_bridge_channel *bridge_channel,
				 struct bridge_channel_internal_cond *cond);

/*!
 * \internal
 * \brief Temporarily suspend a channel from a bridge, handing control over to some
 * other system
 *
 * \param bridge_channel The channel in the bridge
 * \note This function assumes that \ref bridge_channel is already locked
 */
void bridge_channel_internal_suspend_nolock(struct ast_bridge_channel *bridge_channel);

/*!
 * \internal
 * \brief Unsuspend a channel that was previously suspended
 *
 * \param bridge_channel The channel in the bridge
 * \note This function assumes that \ref bridge_channel is already locked
 */
void bridge_channel_internal_unsuspend_nolock(struct ast_bridge_channel *bridge_channel);

/*!
 * \internal
 * \brief Queue a blind transfer action on a transferee bridge channel
 *
 * This is only relevant for when a blind transfer is performed on a two-party
 * bridge. The transferee's bridge channel will have a blind transfer bridge
 * action queued onto it, resulting in the party being redirected to a new
 * destination
 *
 * \param transferee The channel to have the action queued on
 * \param exten The destination extension for the transferee
 * \param context The destination context for the transferee
 * \param hook Frame hook to attach to transferee
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int bridge_channel_internal_queue_blind_transfer(struct ast_channel *transferee,
		const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data);

/*!
 * \internal
 * \brief Queue an attended transfer action on a transferee bridge channel
 *
 * This is only relevant for when an attended transfer is performed on a two-party
 * bridge. The transferee's bridge channel will have an attended transfer bridge
 * action queued onto it.
 *
 * \param transferee The channel to have the action queued on
 * \param unbridged_chan The unbridged channel who is the target of the attended
 * transfer
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int bridge_channel_internal_queue_attended_transfer(struct ast_channel *transferee,
		struct ast_channel *unbridged_chan);

/*!
 * \internal
 * \brief Return whether or not the bridge_channel would allow optimization
 *
 * \retval 0 if optimization is not allowed
 * \retval non-zero if optimization is allowed
 */
int bridge_channel_internal_allows_optimization(struct ast_bridge_channel *bridge_channel);

#endif /* _ASTERISK_PRIVATE_BRIDGING_H */
