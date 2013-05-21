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

/* ------------------------------------------------------------------- */

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

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_CORE_LOCAL_H */
