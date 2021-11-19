/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2013, Digium, Inc.
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
 * \brief Call Pickup API
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#ifndef _AST_PICKUP_H
#define _AST_PICKUP_H

/*!
 * \brief Test if a channel can be picked up.
 *
 * \param chan Channel to test if can be picked up.
 *
 * \note This function assumes that chan is locked.
 *
 * \retval TRUE if channel can be picked up.
 */
int ast_can_pickup(struct ast_channel *chan);

/*!
 * \brief Find a pickup channel target by group.
 *
 * \param chan channel that initiated pickup.
 *
 * \return target on success.  The returned channel is locked and reffed.
 * \retval NULL on error.
 */
struct ast_channel *ast_pickup_find_by_group(struct ast_channel *chan);

/*!
 * \brief Pickup a call
 *
 * \param chan The channel that initiated the pickup
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_pickup_call(struct ast_channel *chan);

/*!
 * \brief Pickup a call target.
 *
 * \param chan channel that initiated pickup.
 * \param target channel to be picked up.
 *
 * \note This function assumes that target is locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_do_pickup(struct ast_channel *chan, struct ast_channel *target);

/*!
 * \brief accessor for call pickup message type
 * \since 12.0.0
 *
 * \return pointer to the stasis message type
 * \retval NULL if not initialized
 */
struct stasis_message_type *ast_call_pickup_type(void);

/*!
 * \brief Initialize pickup
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int ast_pickup_init(void);

#endif /* _AST_PICKUP_H */
