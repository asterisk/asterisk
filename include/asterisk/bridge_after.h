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
 * \brief After Bridge Execution API
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_BRIDGING_AFTER_H
#define _ASTERISK_BRIDGING_AFTER_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Reason the after bridge callback will not be called. */
enum ast_bridge_after_cb_reason {
	/*! The datastore is being destroyed.  Likely due to hangup. (Enum value must be zero.) */
	AST_BRIDGE_AFTER_CB_REASON_DESTROY,
	/*! Something else replaced the callback with another. */
	AST_BRIDGE_AFTER_CB_REASON_REPLACED,
	/*! The callback was removed because of a masquerade. (fixup) */
	AST_BRIDGE_AFTER_CB_REASON_MASQUERADE,
	/*! The channel was departed from the bridge. */
	AST_BRIDGE_AFTER_CB_REASON_DEPART,
	/*! Was explicitly removed by external code. */
	AST_BRIDGE_AFTER_CB_REASON_REMOVED,
	/*! The channel failed to enter the bridge. */
	AST_BRIDGE_AFTER_CB_REASON_IMPART_FAILED,
};

/*!
 * \brief Set channel to goto specific location after the bridge.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param context Context to goto after bridge.
 * \param exten Exten to goto after bridge.
 * \param priority Priority to goto after bridge.
 *
 * \note chan is locked by this function.
 *
 * Add a channel datastore to setup the goto location when the
 * channel leaves the bridge and run a PBX from there.
 */
void ast_bridge_set_after_goto(struct ast_channel *chan, const char *context, const char *exten, int priority);

/*!
 * \brief Set channel to run the h exten after the bridge.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param context Context to goto after bridge.
 *
 * \note chan is locked by this function.
 *
 * Add a channel datastore to setup the goto location when the
 * channel leaves the bridge and run a PBX from there.
 */
void ast_bridge_set_after_h(struct ast_channel *chan, const char *context);

/*!
 * \brief Set channel to go on in the dialplan after the bridge.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param context Current context of the caller channel.
 * \param exten Current exten of the caller channel.
 * \param priority Current priority of the caller channel
 * \param parseable_goto User specified goto string from dialplan.
 *
 * \note chan is locked by this function.
 *
 * Add a channel datastore to setup the goto location when the
 * channel leaves the bridge and run a PBX from there.
 *
 * If parseable_goto then use the given context/exten/priority
 *   as the relative position for the parseable_goto.
 * Else goto the given context/exten/priority+1.
 */
void ast_bridge_set_after_go_on(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *parseable_goto);

/*!
 * \brief Setup any after bridge goto location to begin execution.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 *
 * \note chan is locked by this function.
 *
 * Pull off any after bridge goto location datastore and setup for
 * dialplan execution there.
 *
 * \retval 0 on success.  The goto location is set for a PBX to run it.
 * \retval non-zero on error or no goto location.
 *
 * \note If the after bridge goto is set to run an h exten it is
 * run here immediately.
 */
int ast_bridge_setup_after_goto(struct ast_channel *chan);

/*!
 * \brief Run any after bridge callback.
 * \since 12.0.0
 *
 * \param chan Channel to run after bridge callback.
 */
void ast_bridge_run_after_callback(struct ast_channel *chan);

/*!
 * \brief Run discarding any after bridge callbacks.
 * \since 12.0.0
 *
 * \param chan Channel to run after bridge callback.
 * \param reason
 */
void ast_bridge_discard_after_callback(struct ast_channel *chan, enum ast_bridge_after_cb_reason reason);

/*!
 * \brief Run a PBX on any after bridge goto location.
 * \since 12.0.0
 *
 * \param chan Channel to execute after bridge goto location.
 *
 * \note chan is locked by this function.
 *
 * Pull off any after bridge goto location datastore and run a PBX at that
 * location.
 *
 * \note On return, the chan pointer is no longer valid because
 * the channel has hung up.
 */
void ast_bridge_run_after_goto(struct ast_channel *chan);

/*!
 * \brief Discard channel after bridge goto location.
 * \since 12.0.0
 *
 * \param chan Channel to discard after bridge goto location.
 *
 * \note chan is locked by this function.
 */
void ast_bridge_discard_after_goto(struct ast_channel *chan);

/*!
 * \brief Read after bridge goto if it exists
 * \since 12.0.0
 *
 * \param chan Channel to read the after bridge goto parseable goto string from
 * \param buffer Buffer to write the after bridge goto data to
 * \param buf_size size of the buffer being written to
 */
void ast_bridge_read_after_goto(struct ast_channel *chan, char *buffer, size_t buf_size);

/*!
 * \brief After bridge callback failed.
 * \since 12.0.0
 *
 * \param reason Reason callback is failing.
 * \param data Extra data what setup the callback wanted to pass.
 *
 * \note Called when the channel leaves the bridging system or
 * is destroyed.
 */
typedef void (*ast_bridge_after_cb_failed)(enum ast_bridge_after_cb_reason reason, void *data);

/*!
 * \brief After bridge callback function.
 * \since 12.0.0
 *
 * \param chan Channel just leaving bridging system.
 * \param data Extra data what setup the callback wanted to pass.
 */
typedef void (*ast_bridge_after_cb)(struct ast_channel *chan, void *data);

/*!
 * \brief Setup an after bridge callback for when the channel leaves the bridging system.
 * \since 12.0.0
 *
 * \param chan Channel to setup an after bridge callback on.
 * \param callback Function to call when the channel leaves the bridging system.
 * \param failed Function to call when it will not be calling the callback.
 * \param data Extra data to pass with the callback.
 *
 * \note chan is locked by this function.
 *
 * \note failed is called when the channel leaves the bridging
 * system or is destroyed.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_set_after_callback(struct ast_channel *chan, ast_bridge_after_cb callback, ast_bridge_after_cb_failed failed, void *data);

/*!
 * \brief Get a string representation of an after bridge callback reason
 * \since 12.0.0
 *
 * \param reason The reason to interpret to a string
 * \retval NULL Unrecognized reason
 * \retval non-NULL String representation of reason
 */
const char *ast_bridge_after_cb_reason_string(enum ast_bridge_after_cb_reason reason);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_BRIDGING_H */
