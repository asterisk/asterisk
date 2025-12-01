/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Aurora Innovation AB
 *
 * Daniel Donoghue <daniel.donoghue@aurorainnovation.com>
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

#ifndef _ASTERISK_STASIS_APP_BROADCAST_H
#define _ASTERISK_STASIS_APP_BROADCAST_H

/*! \file
 *
 * \brief Stasis Application Broadcast API
 *
 * \author Daniel Donoghue <daniel.donoghue@aurorainnovation.com>
 *
 * This module provides the infrastructure for broadcasting incoming channels
 * to multiple ARI applications and handling first-claim winner logic.
 */

#include "asterisk/channel.h"
#include "asterisk/optional_api.h"

/*!
 * \brief Start a broadcast for a channel
 *
 * Broadcasts a channel to all ARI applications (or filtered applications)
 * allowing them to claim the channel. Only the first claim will succeed.
 *
 * \param chan The channel to broadcast
 * \param timeout_ms Timeout in milliseconds to wait for a claim
 * \param app_filter Optional regex filter for application names (NULL for all)
 *
 * \retval 0 on success
 * \retval -1 on error
 * \retval AST_OPTIONAL_API_UNAVAILABLE if res_stasis_broadcast is not loaded
 */
AST_OPTIONAL_API(int, stasis_app_broadcast_channel,
	(struct ast_channel *chan, int timeout_ms, const char *app_filter),
	{ return AST_OPTIONAL_API_UNAVAILABLE; });

/*!
 * \brief Attempt to claim a broadcast channel
 *
 * Atomically attempts to claim a channel that is in broadcast state.
 * Only the first claim for a given channel will succeed.
 *
 * \param channel_id The unique ID of the channel
 * \param app_name The name of the application claiming the channel
 *
 * \retval 0 if claim successful
 * \retval -1 if channel not found
 * \retval -2 if already claimed by another application
 * \retval AST_OPTIONAL_API_UNAVAILABLE if res_stasis_broadcast is not loaded
 */
AST_OPTIONAL_API(int, stasis_app_claim_channel,
	(const char *channel_id, const char *app_name),
	{ return AST_OPTIONAL_API_UNAVAILABLE; });

/*!
 * \brief Get the winner app name for a broadcast channel
 *
 * \param channel_id The unique ID of the channel
 *
 * \return A copy of the winner app name (caller must free with ast_free),
 *         or NULL if not claimed or not found
 * \retval NULL if res_stasis_broadcast is not loaded
 */
AST_OPTIONAL_API(char *, stasis_app_broadcast_winner,
	(const char *channel_id),
	{ return NULL; });

/*!
 * \brief Wait for a broadcast channel to be claimed
 *
 * Blocks until the channel is claimed or the timeout expires.
 *
 * \param chan The channel
 * \param timeout_ms Maximum time to wait in milliseconds
 *
 * \retval 0 if claimed within timeout
 * \retval -1 if timeout expired or error
 * \retval AST_OPTIONAL_API_UNAVAILABLE if res_stasis_broadcast is not loaded
 */
AST_OPTIONAL_API(int, stasis_app_broadcast_wait,
	(struct ast_channel *chan, int timeout_ms),
	{ return AST_OPTIONAL_API_UNAVAILABLE; });

/*!
 * \brief Clean up broadcast context for a channel
 *
 * Removes the broadcast context when the channel is done or leaving the
 * broadcast state.
 *
 * \param channel_id The unique ID of the channel
 */
AST_OPTIONAL_API(void, stasis_app_broadcast_cleanup,
	(const char *channel_id),
	{ return; });

#endif /* _ASTERISK_STASIS_APP_BROADCAST_H */
