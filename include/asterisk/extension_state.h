/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * Joshua C. Colp <jcolp@sangoma.com>
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

/*! \file
 * \ref ExtensionState
 *
 * \page ExtensionState API providing extension state management.

Before we talk about extension state let's talk about the fundamentals that
drive it. Extension state is driven based on three things: hints, device state,
and presence state. Hints are the stateless configuration that map a dialplan location,
context and extension, to zero or more devices and/or zero or more presence state
providers. Device state provides information about the associated device(s).
Presence state provides information about the associated presence state provider(s).

The extension state API itself acts as an aggregator of the device state and presence
state information using the hint configuration to determine both the individual
identifier as well as the aggregation sources. The API provides the ability to query
the current state of an extension, as well as subscribe to be notified when the state
of an extension changes.

When hint configuration changes this is reconciled and the extension state is updated
accordingly. If a hint has been added the corresponding extension state is created. If
a hint has been removed the corresponding extension state is removed. For cases where
the hint has been added or updated the sources of information are updated on the
extension state and its state is recalculated. This may involve subscribing to new
device state topics or unsubscribing from old ones.

Extension state uses synchronous per-device subscriptions to receive device state
updates. Synchronous is used as the overhead of asynchronous delivery is not worth
the added overhead and CPU for the small amount of work done when device states change.
On receipt of a device state update the extension device state is recalculated and if
the state has changed the extension device state is updated and any subscribers are
notified.

Presence state uses a single global subscription to receive presence state updates as
no per-presence state provider topic is available and also due to the extremely small
number of presence state updates that occur on a system. Just like device state
updates the extension presence state is recalculated and if it has changed it is
updated and any subscribers are notified.

To minimize querying of other APIs in Asterisk extension state keeps an internal
cache of device states and presence state on each extension state. This cache is
updated when device state or presence state changes and is used to determine the
aggregated state of an extension. The aggregated state is also cached on the
extension state for quick access by API users who do not subscribe to receive
updates.

*/

#ifndef _ASTERISK_EXTENSION_STATE_H
#define _ASTERISK_EXTENSION_STATE_H

#include "asterisk/pbx.h"

/*! \brief Individual device states that contributed to snapshot */
struct ast_extension_state_device_state_info {
	/*! \brief The state of the device */
	enum ast_device_state state;
	/*! \brief The name of the device */
	char device[0];
};

/*! \brief Device snapshot for an extension state*/
struct ast_extension_state_device_snapshot {
	/*! \brief The state of the extension */
	enum ast_extension_states state;
	/*! \brief The device that caused this update */
	struct ast_extension_state_device_state_info *causing_device;
	/*! \brief Vector of additional device states that contributed to update */
	AST_VECTOR(, struct ast_extension_state_device_state_info *) additional_devices;
};

/*! \brief Presence snapshot for an extension state */
struct ast_extension_state_presence_snapshot {
	/*! \brief The presence state of the extension */
	enum ast_presence_state presence_state;
	/*! \brief The subtype of the presence state */
	char *presence_subtype;
	/*! \brief An optional message for the presence */
	char *presence_message;
};

/*! \brief Stasis message for extension state update message */
struct ast_extension_state_update_message {
	/*! \brief The old device snapshot */
	struct ast_extension_state_device_snapshot *old_device_snapshot;
	/*! \brief The new device snapshot - will be pointer equivalent to old if unchanged */
	struct ast_extension_state_device_snapshot *new_device_snapshot;
	/*! \brief The old presence snapshot */
	struct ast_extension_state_presence_snapshot *old_presence_snapshot;
	/*! \brief The new presence snapshot - will be pointed equivalent to old if unchanged */
	struct ast_extension_state_presence_snapshot *new_presence_snapshot;
	/*! \brief The dialplan context */
	char *context;
	/*! \brief The dialplan extension */
	char extension[0];
};

/*! \brief Stasis message for extension state removal message */
struct ast_extension_state_remove_message {
	/*! \brief The dialplan context */
	char *context;
	/*! \brief The dialplan extension */
	char extension[0];
};

/*!
 * \brief Get the Stasis topic to receive all extension state messages
 * \since 23.5.0
 * \since 22.11.0
 * \since 20.21.0
 *
 * \return The topic for extension state messages
 * \retval NULL if it has not been allocated
 */
struct stasis_topic *ast_extension_state_topic_all(void);

/*!
 * \brief Get the Stasis topic to receive extension state messages for a specific extension
 * \since 23.5.0
 * \since 22.11.0
 * \since 20.21.0
 *
 * \param exten The extension to receive extension state messages for
 * \param context The context of the extension
 * \return The topic for extension state messages
 * \retval NULL if it has not been allocated
 */
struct stasis_topic *ast_extension_state_topic(const char *exten, const char *context);

/*!
 * \brief Get the latest device state message for an extension
 * \since 23.5.0
 * \since 22.11.0
 * \since 20.21.0
 *
 * \param chan The optional channel to get the underlying hint from, if it needs to be created
 * \param exten The extension to get the device state message for
 * \param context The context of the extension
 * \return The latest device snapshot for the extension
 * \retval NULL if the extension does not have a configured hint
 */
struct ast_extension_state_device_snapshot *ast_extension_state_get_latest_device_snapshot(struct ast_channel *chan,
	const char *exten, const char *context);

/*!
 * \brief Get the latest presence state message for an extension
 * \since 23.5.0
 * \since 22.11.0
 * \since 20.21.0
 *
 * \param chan The optional channel to get the underlying hint from, if it needs to be created
 * \param exten The extension to get the presence state message for
 * \param context The context of the extension
 * \return The latest presence snapshot for the extension
 * \retval NULL if the extension does not have a configured hint
 */
struct ast_extension_state_presence_snapshot *ast_extension_state_get_latest_presence_snapshot(struct ast_channel *chan,
	const char *exten, const char *context);

/*!
 * \brief Get the channel that is causing the device to be in the given state, if any
 * \since 23.5.0
 * \since 22.11.0
 * \since 20.21.0
 *
 * \param device The device itself
 * \param device_state The state of the device
 * \return The channel that is causing the device to be in the given state
 * \retval NULL if there is no channel causing the device to be in the given state
 */
struct ast_channel *ast_extension_state_get_device_causing_channel(const char *device, enum ast_device_state device_state);

/*!
 * \brief Get extension state update message type
 * \since 23.5.0
 * \since 22.11.0
 * \since 20.21.0
 *
 * \retval Stasis message type for extension state update messages
 */
struct stasis_message_type *ast_extension_state_update_message_type(void);

/*!
 * \brief Get extension state remove message type
 * \since 23.5.0
 * \since 22.11.0
 * \since 20.21.0
 *
 * \retval Stasis message type for extension state remove messages
 */
struct stasis_message_type *ast_extension_state_remove_message_type(void);

#endif /* ASTERISK_EXTENSION_STATE_H */
