/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 *
 * \brief Call Parking API
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk/stringfields.h"
#include "asterisk/bridge.h"

/*!
 * \brief The default parking application that Asterisk expects.
 */
#define PARK_APPLICATION "Park"

/*!
 * \brief The default parking lot
 */
#define DEFAULT_PARKINGLOT "default"

/*!
 * \brief Defines the type of parked call message being published
 * \since 12
 */
enum ast_parked_call_event_type {
	PARKED_CALL = 0,
	PARKED_CALL_TIMEOUT,
	PARKED_CALL_GIVEUP,
	PARKED_CALL_UNPARKED,
	PARKED_CALL_FAILED,
	PARKED_CALL_SWAP,
};

/*!
 * \brief A parked call message payload
 * \since 12
 */
struct ast_parked_call_payload {
	struct ast_channel_snapshot *parkee;             /*!< Snapshot of the channel that is parked */
	struct ast_channel_snapshot *retriever;          /*!< Snapshot of the channel that retrieved the call (may be NULL) */
	enum ast_parked_call_event_type event_type;      /*!< Reason for issuing the parked call message */
	long unsigned int timeout;                       /*!< Time remaining before the call times out (seconds ) */
	long unsigned int duration;                      /*!< How long the parkee has been parked (seconds) */
	unsigned int parkingspace;                       /*!< Which Parking Space the parkee occupies */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(parkinglot);                /*!< Name of the parking lot used to park the parkee */
		AST_STRING_FIELD(parker_dial_string);          /*!< The device string used for call control on parking timeout */
	);
};

struct ast_exten;

/*!
 * \brief Constructor for parked_call_payload objects
 * \since 12
 *
 * \param event_type What kind of parked call event is happening
 * \param parkee_snapshot channel snapshot of the parkee
 * \param parker_dial_string dialstring used when the call times out
 * \param retriever_snapshot channel snapshot of the retriever (NULL allowed)
 * \param parkinglot name of the parking lot where the parked call is parked
 * \param parkingspace what numerical parking space the parked call is parked in
 * \param timeout how long the parked call can remain at the point this snapshot is created before timing out
 * \param duration how long the parked call has currently been parked
 *
 * \retval NULL if the parked call payload can't be allocated
 * \retval reference to a newly created parked call payload
 */
struct ast_parked_call_payload *ast_parked_call_payload_create(enum ast_parked_call_event_type event_type,
		struct ast_channel_snapshot *parkee_snapshot, const char *parker_dial_string,
		struct ast_channel_snapshot *retriever_snapshot, const char *parkinglot,
		unsigned int parkingspace, unsigned long int timeout, unsigned long int duration);

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \brief accessor for the parking stasis topic
 * \since 12
 *
 * \retval NULL if the parking topic hasn't been created or has been disabled
 * \retval a pointer to the parking topic
 */
struct stasis_topic *ast_parking_topic(void);

/*!
 * \brief accessor for the parked call stasis message type
 * \since 12
 *
 * \retval NULL if the parking topic hasn't been created or has been canceled
 * \retval a pointer to the parked call message type
 */
struct stasis_message_type *ast_parked_call_type(void);

/*! @} */

#define PARKING_MODULE_VERSION 1

/*!
 * \brief A function table providing parking functionality to the \ref AstBridging
 * Bridging API and other consumers
 */
struct ast_parking_bridge_feature_fn_table {

	/*!
	 * \brief The version of this function table. If the ABI for this table
	 * changes, the module version (/ref PARKING_MODULE_VERSION) should be
	 * incremented.
	 */
	unsigned int module_version;

	/*!
	 * \brief The name of the module that provides this parking functionality
	 */
	const char *module_name;

	/*!
	 * \brief Determine if the context/exten is a "parking" extension
	 *
	 * \retval 0 if the extension is not a parking extension
	 * \retval 1 if the extension is a parking extension
	 */
	int (* parking_is_exten_park)(const char *context, const char *exten);

	/*!
	 * \brief Park the bridge and/or callers that this channel is in
	 *
	 * \param parker The bridge_channel parking the bridge
	 * \param exten Optional. The extension the channel or bridge was parked at if the
	 * call succeeds.
	 * \param length Optional. If \c exten is specified, the size of the buffer.
	 *
	 * \note This is safe to be called outside of the \ref AstBridging Bridging API.
	 *
	 * \retval 0 on success
	 * \retval non-zero on error
	 */
	int (* parking_park_call)(struct ast_bridge_channel *parker, char *exten, size_t length);

	/*!
	 * \brief Perform a blind transfer to a parking extension.
	 *
	 * \param parker The \ref bridge_channel object that is initiating the parking
	 * \param context The context to blind transfer to
	 * \param exten The extension to blind transfer to
	 * \param parked_channel_cb Execute the following function on the the channel that gets parked
	 * \param parked_channel_data Data for the parked_channel_cb
	 *
	 * \note If the bridge \ref parker is in has more than one other occupant, the entire
	 * bridge will be parked using a Local channel
	 *
	 * \note This is safe to be called outside of the \ref AstBridging Bridging API.
	 *
	 * \retval 0 on success
	 * \retval non-zero on error
	 */
	int (* parking_blind_transfer_park)(struct ast_bridge_channel *parker, const char *context,
		const char *exten, transfer_channel_cb parked_channel_cb, struct transfer_channel_data *parked_channel_data);

	/*!
	 * \brief Perform a direct park on a channel in a bridge.
	 *
	 * \param parkee The channel in the bridge to be parked.
	 * \param parkee_uuid The UUID of the channel being packed.
	 * \param parker_uuid The UUID of the channel performing the park.
	 * \param app_data Data to pass to the Park application
	 *
	 * \note This must be called within the context of the \ref AstBridging Bridging API.
	 * External entities should not call this method directly, but should instead use
	 * the direct call parking method or the blind transfer method.
	 *
	 * \retval 0 on success
	 * \retval non-zero on error
	 */
	int (* parking_park_bridge_channel)(struct ast_bridge_channel *parkee, const char *parkee_uuid, const char *parker_uuid, const char *app_data);

	/*! \brief The module registering this parking provider */
	struct ast_module_lib *lib;
};

/*!
 * \brief Determine if the context/exten is a "parking" extension
 *
 * \retval 0 if the extension is not a parking extension
 * \retval 1 if the extension is a parking extension
 */
int ast_parking_is_exten_park(const char *context, const char *exten);

/*!
 * \brief Park the bridge and/or callers that this channel is in
 *
 * \param parker The bridge_channel parking the bridge
 * \param exten Optional. The extension the channel or bridge was parked at if the
 * call succeeds.
 * \param length Optional. If \c exten is specified, the size of the buffer.
 *
 * \note This is safe to be called outside of the \ref AstBridging Bridging API.
 *
 * \retval 0 on success
 * \retval non-zero on error
 */
int ast_parking_park_call(struct ast_bridge_channel *parker, char *exten, size_t length);

/*!
 * \brief Perform a blind transfer to a parking extension.
 *
 * \param parker The \ref bridge_channel object that is initiating the parking
 * \param context The context to blind transfer to
 * \param exten The extension to blind transfer to
 * \param exten The extension to blind transfer to
 * \param parked_channel_cb Execute the following function on the the channel that gets parked
 * \param parked_channel_data Data for the parked_channel_cb
 *
 * \note If the bridge \ref parker is in has more than one other occupant, the entire
 * bridge will be parked using a Local channel
 *
 * \note This is safe to be called outside of the \ref AstBridging Bridging API.
 *
 * \retval 0 on success
 * \retval non-zero on error
 */
int ast_parking_blind_transfer_park(struct ast_bridge_channel *parker, const char *context,
	const char *exten, transfer_channel_cb parked_channel_cb, struct transfer_channel_data *parked_channel_data);

/*!
 * \brief Perform a direct park on a channel in a bridge.
 *
 * \param parkee The channel in the bridge to be parked.
 * \param parkee_uuid The UUID of the channel being packed.
 * \param parker_uuid The UUID of the channel performing the park.
 * \param app_data Data to pass to the Park application
 *
 * \note This must be called within the context of the \ref AstBridging Bridging API.
 * External entities should not call this method directly, but should instead use
 * the direct call parking method or the blind transfer method.
 *
 * \retval 0 on success
 * \retval non-zero on error
 */
int ast_parking_park_bridge_channel(struct ast_bridge_channel *parkee, const char *parkee_uuid, const char *parker_uuid, const char *app_data);

/*!
 * \brief Register a parking provider
 *
 * \param fn_table The \ref ast_parking_bridge_feature_fn_table to register
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int ast_parking_register_bridge_features(struct ast_parking_bridge_feature_fn_table *fn_table);

/*!
 * \brief Unregister the current parking provider
 *
 * \param The module name of the provider to unregister
 *
 * \retval 0 if the parking provider \c module_name was unregsistered
 * \retval -1 on error
 */
int ast_parking_unregister_bridge_features(const char *module_name);

/*!
 * \brief Check whether a parking provider is registered
 *
 * \retval 0 if there is no parking provider regsistered
 * \retval 1 if there is a parking provider regsistered
 */
int ast_parking_provider_registered(void);
