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

#define PARK_APPLICATION "Park"

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
};

/*!
 * \brief A parked call message payload
 * \since 12
 */
struct ast_parked_call_payload {
	struct ast_channel_snapshot *parkee;             /*!< Snapshot of the channel that is parked */
	struct ast_channel_snapshot *parker;             /*!< Snapshot of the channel that parked the call */
	struct ast_channel_snapshot *retriever;          /*!< Snapshot of the channel that retrieved the call */
	enum ast_parked_call_event_type event_type;      /*!< Reason for issuing the parked call message */
	long unsigned int timeout;                       /*!< Time remaining before the call times out (seconds ) */
	long unsigned int duration;                      /*!< How long the parkee has been parked (seconds) */
	unsigned int parkingspace;                       /*!< Which Parking Space the parkee occupies */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(parkinglot);                /*!< Name of the parking lot used to park the parkee */
	);
};

/*!
 * \brief Constructor for parked_call_payload objects
 * \since 12
 *
 * \param event_type What kind of parked call event is happening
 * \param parkee_snapshot channel snapshot of the parkee
 * \param parker_snapshot channel snapshot of the parker
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
		struct ast_channel_snapshot *parkee_snapshot, struct ast_channel_snapshot *parker_snapshot,
		struct ast_channel_snapshot *retriever_snapshot, const char *parkinglot,
		unsigned int parkingspace, unsigned long int timeout, unsigned long int duration);

/*!
 * \brief initialize parking stasis types
 * \since 12
 */
void ast_parking_stasis_init(void);

/*!
 * \brief disable parking stasis types
 * \since 12
 */
void ast_parking_stasis_disable(void);

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

/*!
 * \brief invoke an installable park callback to asynchronously park a bridge_channel in a bridge
 * \since 12
 *
 * \param bridge_channel the bridge channel that initiated parking
 * \parkee_uuid channel id of the channel being parked
 * \parker_uuid channel id of the channel that initiated parking
 * \param app_data string of application data that might be applied to parking
 */
void ast_bridge_channel_park(struct ast_bridge_channel *bridge_channel,
	const char *parkee_uuid,
	const char *parker_uuid,
	const char *app_data);

typedef int (*ast_park_blind_xfer_fn)(struct ast_bridge *bridge, struct ast_bridge_channel *parker,
	struct ast_exten *park_exten);

/*!
 * \brief install a callback for handling blind transfers to a parking extension
 * \since 12
 *
 * \param parking_func Function to use for transfers to 'Park' applications
 */
void ast_install_park_blind_xfer_func(ast_park_blind_xfer_fn park_blind_xfer_func);

/*!
 * \brief uninstall a callback for handling blind transfers to a parking extension
 * \since 12
 */
void ast_uninstall_park_blind_xfer_func(void);

/*!
 * \brief use the installed park blind xfer func
 * \since 12
 *
 * \param bridge Bridge being transferred from
 * \param bridge_channel Bridge channel initiating the transfer
 * \param app_data arguments to the park application
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_park_blind_xfer(struct ast_bridge *bridge, struct ast_bridge_channel *parker,
		struct ast_exten *park_exten);

typedef void (*ast_bridge_channel_park_fn)(struct ast_bridge_channel *parkee, const char *parkee_uuid,
	const char *parker_uuid, const char *app_data);

/*!
 * \brief Install a function for ast_bridge_channel_park
 * \since 12
 *
 * \param bridge_channel_park_func function callback to use for ast_bridge_channel_park
 */
void ast_install_bridge_channel_park_func(ast_bridge_channel_park_fn bridge_channel_park_func);

/*!
 * \brief Uninstall the ast_bridge_channel_park function callback
 * \since 12
 */
void ast_uninstall_bridge_channel_park_func(void);


/*!
 * \brief Determines whether a certain extension is a park application extension or not.
 * \since 12
 *
 * \param exten_str string representation of the extension sought
 * \param chan channel the extension is sought for
 * \param context context the extension is sought from
 *
 * \retval pointer to the extension if the extension is a park extension
 * \retval NULL if the extension was not a park extension
 */
struct ast_exten *ast_get_parking_exten(const char *exten_str, struct ast_channel *chan, const char *context);
