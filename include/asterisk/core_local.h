/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief Local proxy channel special access.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_CORE_LOCAL_H
#define _ASTERISK_CORE_LOCAL_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Forward declare some struct names */
struct ast_channel;
struct ast_bridge;
struct ast_bridge_features;
struct stasis_message_type;

/* ------------------------------------------------------------------- */

/*!
 * \brief Add a reference to the local channel's private tech, lock the local channel's
 *        private base, and add references and lock both sides of the local channel.
 *
 * \note None of these locks should be held prior to calling this function.
 * \note To undo this process call ast_local_unlock_all2.
 *
 * \since 13.17.0, 14.6.0
 *
 * \param chan Must be a local channel
 * \param[out] tech_pvt channel's private tech (ref and lock added)
 * \param[out] base_chan One side of the local channel (ref and lock added)
 * \param[out] base_owner Other side of the local channel (ref and lock added)
 */
void ast_local_lock_all(struct ast_channel *chan, void **tech_pvt,
	struct ast_channel **base_chan, struct ast_channel **base_owner);

/*!
 * \brief Remove a reference to the given local channel's private tech, unlock the given
 *        local channel's private base, and remove references and unlock both sides of
 *        given the local channel.
 *
 * \note This function should be used in conjunction with ast_local_lock_all2.
 *
 * \since 13.17.0, 14.6.0
 *
 * \param tech_pvt channel's private tech (ref and lock removed)
 * \param base_chan One side of the local channel (ref and lock removed)
 * \param base_owner Other side of the local channel (ref and lock removed)
 */
void ast_local_unlock_all(void *tech_pvt, struct ast_channel *base_chan,
	struct ast_channel *base_owner);

/*!
 * \brief Get the other local channel in the pair.
 * \since 12.0.0
 *
 * \param ast Local channel to get peer.
 *
 * \note On entry, ast must be locked.
 *
 * \retval peer reffed on success.
 * \retval NULL if no peer or error.
 */
struct ast_channel *ast_local_get_peer(struct ast_channel *ast);

/*!
 * \brief Setup the outgoing local channel to join a bridge on ast_call().
 * \since 12.0.0
 *
 * \param ast Either channel of a local channel pair.
 * \param bridge Bridge to join.
 * \param swap Channel to swap with when joining.
 * \param features Bridge features structure.
 *
 * \note The features parameter must be NULL or obtained by
 * ast_bridge_features_new().  You must not dereference features
 * after calling even if the call fails.
 *
 * \note Intended to be called after ast_request() and before
 * ast_call() on a local channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_local_setup_bridge(struct ast_channel *ast, struct ast_bridge *bridge, struct ast_channel *swap, struct ast_bridge_features *features);

/*!
 * \brief Setup the outgoing local channel to masquerade into a channel on ast_call().
 * \since 12.0.0
 *
 * \param ast Either channel of a local channel pair.
 * \param masq Channel to masquerade into.
 *
 * \note Intended to be called after ast_request() and before
 * ast_call() on a local channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_local_setup_masquerade(struct ast_channel *ast, struct ast_channel *masq);

/* ------------------------------------------------------------------- */

/*!
 * \brief Message type for when two local channel halves are bridged together
 * \since 12.0.0
 *
 * \note Payloads for the \ref ast_local_bridge_type are a \ref ast_multi_channel_blob.
 * Roles for the channels in the \ref ast_multi_channel_blob are "1" and "2", reflecting
 * the two halves. Unlike most other bridges, the 'bridge' between two local channels is
 * not part of the bridge framework; as such, the message simply references the two local
 * channel halves that are now bridged.
 *
 * \retval A \ref stasis message type
 */
struct stasis_message_type *ast_local_bridge_type(void);

/*!
 * \brief Message type for when a local channel optimization begins
 * \since 12.0.0
 *
 * \note Payloads for the \ref ast_local_optimization_begin_type are a
 * \ref ast_multi_channel_blob. Roles for the channels in the \ref ast_multi_channel_blob
 * are "1" and "2", reflecting the two halves.
 *
 * \retval A \ref stasis message type
 */
struct stasis_message_type *ast_local_optimization_begin_type(void);

/*!
 * \brief Message type for when a local channel optimization completes
 * \since 12.0.0
 *
 * \note Payloads for the \ref ast_local_optimization_end_type are a
 * \ref ast_multi_channel_blob. Roles for the channels in the \ref ast_multi_channel_blob
 * are "1" and "2", reflecting the two halves.
 *
 * \retval A \ref stasis message type
 */
struct stasis_message_type *ast_local_optimization_end_type(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_CORE_LOCAL_H */
