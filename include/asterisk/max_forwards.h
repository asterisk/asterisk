/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#ifndef MAX_FORWARDS_H
#define MAX_FORWARDS_H

struct ast_channel;

/*!
 * \brief Set the starting max forwards for a particular channel.
 *
 * \pre chan is locked
 *
 * \param starting_count The value to set the max forwards to.
 * \param chan The channel on which to set the max forwards.
 * \retval 0 Success
 * \retval 1 Failure
 */
int ast_max_forwards_set(struct ast_channel *chan, int starting_count);

/*!
 * \brief Get the current max forwards for a particular channel.
 *
 * If the channel has not had max forwards set on it, then the channel
 * will have the default max forwards set on it and that value will
 * be returned.
 *
 * \pre chan is locked
 *
 * \param chan The channel to get the max forwards for.
 * \return The current max forwards count on the channel
 */
int ast_max_forwards_get(struct ast_channel *chan);

/*!
 * \brief Decrement the max forwards count for a particular channel.
 *
 * If the channel has not had max forwards set on it, then the channel
 * will have the default max forwards set on it and that value will
 * not be decremented.
 *
 * \pre chan is locked
 *
 * \chan The channel for which the max forwards value should be decremented
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_max_forwards_decrement(struct ast_channel *chan);

/*!
 * \brief Reset the max forwards on a channel to its starting value.
 *
 * If the channel has not had max forwards set on it, then the channel
 * will have the default max forwards set on it.
 *
 * \pre chan is locked.
 *
 * \param chan The channel on which to reset the max forwards count.
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_max_forwards_reset(struct ast_channel *chan);

#endif /* MAX_FORWARDS_H */
