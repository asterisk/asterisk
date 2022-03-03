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

#define AST_TRANSFERER_ROLE_NAME "transferer"
/* ------------------------------------------------------------------- */

/*!
 * \brief Sets the features a channel will use upon being bridged.
 * \since 12.0.0
 *
 * \param chan Which channel to set features for
 * \param features Which feature codes to set for the channel
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_bridge_features_ds_set_string(struct ast_channel *chan, const char *features);

/*!
 * \brief writes a channel's DTMF features to a buffer string
 * \since 12.0.0
 *
 * \param chan channel whose feature flags should be checked
 * \param buffer pointer string buffer where the output should be stored
 * \param buf_size size of the provided buffer (ideally enough for all features, 6+)
 *
 * \retval 0 on successful write
 * \retval -1 on failure
 */
int ast_bridge_features_ds_get_string(struct ast_channel *chan, char *buffer, size_t buf_size);

/*!
 * \brief Get DTMF feature flags from the channel.
 * \since 12.0.0
 *
 * \param chan Channel to get DTMF features datastore.
 *
 * \note The channel should be locked before calling this function.
 * \note The channel must remain locked until the flags returned have been consumed.
 *
 * \return flags on success.
 * \retval NULL if the datastore does not exist.
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
 * \brief Append basic bridge DTMF feature flags on the channel.
 * \since 12.0.0
 *
 * \param chan Channel to append DTMF features datastore.
 * \param flags Builtin DTMF feature flags. (ast_bridge_config flags)
 *
 * \note The channel must be locked before calling this function.
 * \note This function differs from ast_bridge_features_ds_set only in that it won't
 *       remove features already set on the channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_features_ds_append(struct ast_channel *chan, struct ast_flags *flags);

/*! \brief Bridge basic class virtual method table. */
extern struct ast_bridge_methods ast_bridge_basic_v_table;

/*!
 * \brief Create a new basic class bridge
 *
 * \return a pointer to a new bridge on success
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

/*!
 * \brief Set feature flags on a basic bridge
 *
 * Using this function instead of setting flags directly will
 * ensure that after operations such as an attended transfer,
 * the bridge will maintain the flags that were set on it.
 *
 * \param bridge
 * \param flags These are added to the flags already set.
 */
void ast_bridge_basic_set_flags(struct ast_bridge *bridge, unsigned int flags);

/*! Initialize the basic bridge class for use by the system. */
void ast_bridging_init_basic(void);

/* ------------------------------------------------------------------- */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_BRIDGING_BASIC_H */
