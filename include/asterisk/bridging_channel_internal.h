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

/*!
 * \file
 * \brief Private Bridging Channel API
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 * A private API to manipulate channels in a bridge. These can be called on a channel in
 * a bridge by the bridging API, but should not be called by external consumers of the
 * Bridging API.
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_PRIVATE_BRIDGING_CHANNEL_H
#define _ASTERISK_PRIVATE_BRIDGING_CHANNEL_H

struct blind_transfer_data {
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_CONTEXT];
};

/*!
 * \brief Adjust the bridge_channel's bridge merge inhibit request count.
 * \since 12.0.0
 *
 * \param bridge_channel What to operate on.
 * \param request Inhibit request increment.
 *     (Positive to add requests.  Negative to remove requests.)
 *
 * \note This API call is meant for internal bridging operations.
 *
 * \retval bridge adjusted merge inhibit with reference count.
 */
struct ast_bridge *bridge_channel_merge_inhibit(struct ast_bridge_channel *bridge_channel, int request);

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
void bridge_channel_pull(struct ast_bridge_channel *bridge_channel);

/*!
 * \internal
 * \brief Push the bridge channel into its specified bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to push.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel did not get pushed.
 */
int bridge_channel_push(struct ast_bridge_channel *bridge_channel);

void bridge_channel_join(struct ast_bridge_channel *bridge_channel);

void bridge_channel_suspend_nolock(struct ast_bridge_channel *bridge_channel);

void bridge_channel_unsuspend_nolock(struct ast_bridge_channel *bridge_channel);

/*!
 * \internal
 * \brief Update the linkedids for all channels in a bridge
 * \since 12.0.0
 *
 * \param bridge_channel The channel joining the bridge
 * \param swap The channel being swapped out of the bridge. May be NULL.
 *
 * \note The bridge must be locked prior to calling this function. This should be called
 * during a \ref bridge_channel_push operation, typically by a sub-class of a bridge
 */
void bridge_channel_update_linkedids(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap);

/*!
 * \internal
 * \brief Update the accountcodes for a channel entering a bridge
 * \since 12.0.0
 *
 * This function updates the accountcode and peeraccount on channels in two-party
 * bridges. In multi-party bridges, peeraccount is not set - it doesn't make much sense -
 * however accountcode propagation will still occur if the channel joining has an
 * accountcode.
 *
 * \param bridge_channel The channel joining the bridge
 * \param swap The channel being swapped out of the bridge. May be NULL.
 *
 * \note The bridge must be locked prior to calling this function. This should be called
 * during a \ref bridge_channel_push operation, typically by a sub-class of a bridge
 */
void bridge_channel_update_accountcodes(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap);


/*!
 * \brief Set bridge channel state to leave bridge (if not leaving already) with no lock.
 *
 * \param bridge_channel Channel to change the state on
 * \param new_state The new state to place the channel into
 *
 * \note This API call is only meant to be used within the
 * bridging module and hook callbacks to request the channel
 * exit the bridge.
 *
 * \note This function assumes the bridge_channel is locked.
 */
void ast_bridge_change_state_nolock(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state);

/*!
 * \brief Set bridge channel state to leave bridge (if not leaving already).
 *
 * \param bridge_channel Channel to change the state on
 * \param new_state The new state to place the channel into
 *
 * Example usage:
 *
 * \code
 * ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
 * \endcode
 *
 * This places the channel pointed to by bridge_channel into the
 * state AST_BRIDGE_CHANNEL_STATE_HANGUP if it was
 * AST_BRIDGE_CHANNEL_STATE_WAIT before.
 *
 * \note This API call is only meant to be used within the
 * bridging module and hook callbacks to request the channel
 * exit the bridge.
 */
void ast_bridge_change_state(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state);

#endif /* _ASTERISK_PRIVATE_BRIDGING_H */
