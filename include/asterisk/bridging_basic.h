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
 * \brief Basic bridge subclass API.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_BRIDGING_BASIC_H
#define _ASTERISK_BRIDGING_BASIC_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* ------------------------------------------------------------------- */

/*!
 * \brief Get DTMF feature flags from the channel.
 * \since 12.0.0
 *
 * \param chan Channel to get DTMF features datastore.
 *
 * \note The channel should be locked before calling this function.
 *
 * \retval flags on success.
 * \retval NULL on error.
 */
struct ast_flags *ast_bridge_features_ds_get(struct ast_channel *chan);

/*!
 * \brief Set basic bridge DTMF feature flags datastore on the channel.
 * \since 12.0.0
 *
 * \param chan Channel to set DTMF features datastore.
 * \param flags Builtin DTMF feature flags. (ast_bridge_config flags)
 *
 * \note The channel must be locked before calling this function.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_features_ds_set(struct ast_channel *chan, struct ast_flags *flags);

/*!
 * \brief Setup DTMF feature hooks using the channel features datastore property.
 * \since 12.0.0
 *
 * \param bridge_channel What to setup DTMF features on.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_setup_features(struct ast_bridge_channel *bridge_channel);

/*! \brief Bridge basic class virtual method table. */
extern struct ast_bridge_methods ast_bridge_basic_v_table;

/*!
 * \brief Create a new basic class bridge
 *
 * \retval a pointer to a new bridge on success
 * \retval NULL on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge *bridge;
 * bridge = ast_bridge_basic_new();
 * \endcode
 *
 * This creates a basic two party bridge with any configured
 * DTMF features enabled that will be destroyed once one of the
 * channels hangs up.
 */
struct ast_bridge *ast_bridge_basic_new(void);

/*! Initialize the basic bridge class for use by the system. */
void ast_bridging_init_basic(void);

/* ------------------------------------------------------------------- */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_BRIDGING_BASIC_H */
