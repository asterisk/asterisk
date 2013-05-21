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
 * \brief Call Parking Resource Internal API
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk/pbx.h"
#include "asterisk/bridging.h"
#include "asterisk/parking.h"
#include "asterisk/stasis_channels.h"

#define DEFAULT_PARKING_LOT "default"
#define DEFAULT_PARKING_EXTEN "700"
#define PARK_DIAL_CONTEXT "park-dial"

enum park_call_resolution {
	PARK_UNSET = 0,		/*! Nothing set a resolution. This should never be observed in practice. */
	PARK_ABANDON,		/*! The channel for the parked call hung up */
	PARK_TIMEOUT,		/*! The parked call stayed parked until the parking lot timeout was reached and was removed */
	PARK_FORCED,		/*! The parked call was forcibly terminated by an unusual means in Asterisk */
	PARK_ANSWERED,		/*! The parked call was retrieved successfully */
};

enum parked_call_feature_options {
	OPT_PARKEDPLAY = 0,
	OPT_PARKEDTRANSFERS,
	OPT_PARKEDREPARKING,
	OPT_PARKEDHANGUP,
	OPT_PARKEDRECORDING,
};

enum parking_lot_modes {
	PARKINGLOT_NORMAL = 0,          /*! The parking lot is configured normally and can accept new calls. Disable on reload if the config isn't replaced.
	                                 *  valid transitions: PARKINGLOT_DISABLED */
	PARKINGLOT_DYNAMIC,             /*! The parking lot is a dynamically created parking lot. It can be parked to at any time. Disabled on last parked call leaving.
	                                 *  valid transitions: PARKINGLOT_DISABLED */
	PARKINGLOT_DISABLED,            /*! The parking lot is no longer linked to a parking lot in configuration. It can no longer be parked to.
	                                 *  and it can not be parked to. This mode has no transitions. */
};

struct parking_lot_cfg {
	int parking_start;                        /*!< First space in the parking lot */
	int parking_stop;                         /*!< Last space in the parking lot */

	unsigned int parkingtime;                 /*!< Analogous to parkingtime config option */
	unsigned int comebackdialtime;            /*!< Analogous to comebackdialtime config option */
	unsigned int parkfindnext;                /*!< Analogous to parkfindnext config option */
	unsigned int parkext_exclusive;           /*!< Analogous to parkext_exclusive config option */
	unsigned int parkaddhints;                /*!< Analogous to parkaddhints config option */
	unsigned int comebacktoorigin;            /*!< Analogous to comebacktoorigin config option */
	int parkedplay;                           /*!< Analogous to parkedplay config option */
	int parkedcalltransfers;                  /*!< Analogous to parkedcalltransfers config option */
	int parkedcallreparking;                  /*!< Analogous to parkedcallreparking config option */
	int parkedcallhangup;                     /*!< Analogous to parkedcallhangup config option */
	int parkedcallrecording;                  /*!< Analogous to parkedcallrecording config option */

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);               /*!< Name of the parking lot configuration object */
		AST_STRING_FIELD(mohclass);           /*!< Analogous to mohclass config option */
		AST_STRING_FIELD(parkext);            /*!< Analogous to parkext config option */
		AST_STRING_FIELD(parking_con);        /*!< Analogous to context config option */
		AST_STRING_FIELD(comebackcontext);    /*!< Analogous to comebackcontext config option */
		AST_STRING_FIELD(courtesytone);       /*!< Analogous to courtesytone config option */
	);
};

struct parking_lot {
	int next_space;                           /*!< When using parkfindnext, which space we should start searching from next time we park */
	struct ast_bridge *parking_bridge;        /*!< Bridged where parked calls will rest until they are answered or otherwise leave */
	struct ao2_container *parked_users;       /*!< List of parked users rigidly ordered by their parking space */
	struct parking_lot_cfg *cfg;              /*!< Reference to configuration object for the parking lot */
	enum parking_lot_modes mode;              /*!< Whether a parking lot is operational, being reconfigured, primed for deletion, or dynamically created. */
	int disable_mark;                         /*!< On reload, disable this parking lot if it doesn't receive a new configuration. */

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);               /*!< Name of the parking lot object */
	);
};

struct parked_user {
	struct ast_channel *chan;                 /*!< Parked channel */
	struct ast_channel_snapshot *parker;      /*!< Snapshot of the channel that parked the call at the time of parking */
	struct ast_channel_snapshot *retriever;   /*!< Snapshot of the channel that retrieves a parked call */
	struct timeval start;                     /*!< When the call was parked */
	int parking_space;                        /*!< Which parking space is used */
	char comeback[AST_MAX_CONTEXT];           /*!< Where to go on parking timeout */
	unsigned int time_limit;                  /*!< How long this specific channel may remain in the parking lot before timing out */
	struct parking_lot *lot;      /*!< Which parking lot the user is parked to */
	enum park_call_resolution resolution;     /*!< How did the parking session end? If the call is in a bridge, lock parked_user before checking/setting */
};

/*!
 * \since 12
 * \brief If a parking lot exists in the parking lot list already, update its status to match the provided
 *        configuration and return a reference return a reference to it. Otherwise, create a parking lot
 *        struct based on a parking lot configuration and return a reference to the new one.
 *
 * \param cfg The configuration being used as a reference to build the parking lot from.
 *
 * \retval A reference to the new parking lot
 * \retval NULL if it was not found and could not be be allocated
 *
 * \note The parking lot will need to be unreffed if it ever falls out of scope
 * \note The parking lot will automatically be added to the parking lot container if needed as part of this process
 */
struct parking_lot *parking_lot_build_or_update(struct parking_lot_cfg *cfg);

/*!
 * \since 12
 * \brief Remove a parking lot from the usable lists if it is no longer involved in any calls and no configuration currently claims it
 *
 * \param lot Which parking lot is being checked for elimination
 *
 * \note This should generally be called when something is happening that could cause a parking lot to die such as a call being unparked or
 *       a parking lot no longer existing in configurations.
 */
void parking_lot_remove_if_unused(struct parking_lot *lot);

/*!
 * \since 12
 * \brief Create a new parking bridge
 *
 * \param bridge_lot Parking lot which the new bridge should be based on
 *
 * \retval NULL if the bridge can not be created
 * \retval Newly created parking bridge
 */
struct ast_bridge *bridge_parking_new(struct parking_lot *bridge_lot);

/*!
 * \since 12
 * \brief Get a reference to a parking lot's bridge. If it doesn't exist, create it and get a reference.
 *
 * \param lot Which parking lot we need the bridge from. This parking lot must be locked before calling this function.
 *
 * \retval A reference to the ast_bridge associated with the parking lot
 * \retval NULL if it didn't already have a bridge and one couldn't be created
 *
 * \note This bridge will need to be unreffed if it ever falls out of scope.
 */
struct ast_bridge *parking_lot_get_bridge(struct parking_lot *lot);

/*!
 * \since 12
 * \brief Get an available parking space within a parking lot.
 *
 * \param lot Which parking lot we are getting a space from
 * \param target_override If there is a specific slot we want, provide it here and we'll start from that position
 *
 * \retval -1 if No slot can be found
 * \retval integer value of parking space selected
 *
 * \note lot should be locked before this is called and unlocked only after a parked_user with the space
 *       returned has been added to the parking lot.
 */
int parking_lot_get_space(struct parking_lot *lot, int target_override);

/*!
 * \since 12
 * \brief Determine if there is a parked user in a parking space and pull it from the parking lot if there is.
 *
 * \param lot Parking lot being pulled from
 * \param target If < 0   search for the first occupied space in the parking lot
 *               If >= 0  Only pull from the indicated target
 *
 * \retval NULL if no parked user could be pulled from the requested parking lot at the requested parking space
 * \retval reference to the requested parked user
 *
 * \note The parked user will be removed from parking lot as part of this process
 * \note Remove this reference with ao2_cleanup once it falls out of scope.
 */
struct parked_user *parking_lot_retrieve_parked_user(struct parking_lot *lot, int target);

/*!
 * \since 12
 * \brief Apply features based on the parking lot feature options
 *
 * \param chan Which channel's feature set is being modified
 * \param lot parking lot which establishes the features used
 * \param recipient_mode AST_FEATURE_FLAG_BYCALLER if the user is the retriever
 *                       AST_FEATURE_FLAG_BYCALLEE if the user is the parkee
 */
void parked_call_retrieve_enable_features(struct ast_channel *chan, struct parking_lot *lot, int recipient_mode);

/*!
 * \since 12
 * \brief Set necessary bridge roles on a channel that is about to enter a parking lot
 *
 * \param chan Entering channel
 * \param lot The parking lot the channel will be entering
 * \param force_ringing Use ringing instead of music on hold
 */
void parking_channel_set_roles(struct ast_channel *chan, struct parking_lot *lot, int force_ringing);

/*!
 * \since 12
 * \brief custom callback function for ast_bridge_channel_queue_playfile which plays a parking space
 *        and optionally hangs up the call afterwards based on the payload in playfile.
 */
void say_parking_space(struct ast_bridge_channel *bridge_channel, const char *payload);

/*!
 * \since 12
 * \brief Setup timeout interval feature on an ast_bridge_features for parking
 *
 * \param features The ast_bridge_features we are establishing the interval hook on
 * \param user The parked_user receiving the timeout duration limits
 */
void parking_set_duration(struct ast_bridge_features *features, struct parked_user *user);

/*!
 * \since 12
 * \brief Get a pointer to the parking lot container for purposes such as iteration
 *
 * \retval pointer to the parking lot container.
 */
struct ao2_container *get_parking_lot_container(void);

/*!
 * \since 12
 * \brief Find a parking lot based on its name
 *
 * \param lot_name Name of the parking lot sought
 *
 * \retval The parking lot if found
 * \retval NULL if no parking lot with the name specified exists
 *
 * \note ao2_cleanup this reference when you are done using it or you'll cause leaks.
 */
struct parking_lot *parking_lot_find_by_name(const char *lot_name);

/*!
 * \since 12
 * \brief Find parking lot name from channel
 *
 * \param chan The channel we want the parking lot name for
 *
 * \retval name of the channel's assigned parking lot if it is defined by the channel in some way
 * \retval name of the default parking lot if it is not
 *
 * \note Channel needs to be locked while the returned string is in use.
 */
const char *find_channel_parking_lot_name(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Flattens a peer name so that it can be written to/found from PBX extensions
 *
 * \param peername unflattened peer name. This will be flattened in place, so expect it to change.
 */
void flatten_peername(char *peername);

/*!
 * \since 12
 * \brief Set a channel's position in the PBX after timeout using the parking lot settings
 *
 * \param pu Parked user who is entering/reentering the PBX
 * \param lot Parking lot the user was removed from.
 *
 * \retval 0 Position set successfully
 * \retval -1 Failed to set the position
 */
int comeback_goto(struct parked_user *pu, struct parking_lot *lot);

/*!
 * \since 12
 * \brief Pull a parked user out of its parking lot. Use this when you don't want to use the parked user afterwards.
 * \param user The parked user being pulled.
 *
 * \retval 0 on success
 * \retval -1 if the user didn't have its parking lot set
 */
int unpark_parked_user(struct parked_user *user);

/*!
 * \since 12
 * \brief Publish a stasis parked call message for the channel indicating failure to park.
 *
 * \param parkee channel belonging to the failed parkee
 */
void publish_parked_call_failure(struct ast_channel *parkee);

/*!
 * \since 12
 * \brief Publish a stasis parked call message for a given parked user
 *
 * \param pu pointer to a parked_user that we are generating the message for
 * \param event_type What parked call event type is provoking this message
 */
void publish_parked_call(struct parked_user *pu, enum ast_parked_call_event_type event_type);

/*!
 * \since 12
 * \brief Function to prepare a channel for parking by determining which parking bridge should
 *        be used, setting up a park common datastore so that the parking bridge will have access
 *        to necessary parking information when joining, and applying various bridge roles to the
 *        channel.
 *
 * \param parkee The channel being preparred for parking
 * \param parker The channel initiating the park; may be the parkee as well
 * \param app_data arguments supplied to the Park application. May be NULL.
 * \param silence_announcements optional pointer to an integer where we want to store the silence option flag
 *        this value should be initialized to 0 prior to calling park_common_setup.
 *
 * \retval reference to a parking bridge if successful
 * \retval NULL on failure
 *
 * \note ao2_cleanup this reference when you are done using it or you'll cause leaks.
 */
struct ast_bridge *park_common_setup(struct ast_channel *parkee, struct ast_channel *parker,
	const char *app_data, int *silence_announcements);

struct park_common_datastore {
	char *parker_uuid;           /*!< Unique ID of the channel parking the call. */
	char *comeback_override;     /*!< Optional goto string for where to send the call after we are done */
	int randomize;               /*!< Pick a parking space to enter on at random */
	int time_limit;              /*!< time limit override. -1 values don't override, 0 for unlimited time, >0 for custom time limit in seconds */
	int silence_announce;        /*!< Used when a call parks itself to keep it from hearing the parked call announcement */
};

/*!
 * \since 12
 * \brief Function that pulls data from the park common datastore on a channel in order to apply it to
 *        the parked user struct upon bridging.
 *
 * \param parkee The channel entering parking with the datastore we are checking
 * \param parker_uuid pointer to a string pointer for placing the name of the channel that parked parkee
 * \param comeback_override pointer to a string pointer for placing the comeback_override option
 * \param randomize integer pointer to an integer for placing the randomize option
 * \param time_limit integer pointer to an integer for placing the time limit option
 * \param silence_announce pointer to an integer for placing the silence_announcements option
 */
void get_park_common_datastore_data(struct ast_channel *parkee,
		char **parker_uuid, char **comeback_override,
		int *randomize, int *time_limit, int *silence_announce);

/*!
 * \since 12
 * \brief Execution function for the parking application
 *
 * \param chan ast_channel entering the application
 * \param data arguments to the application
 *
 * \retval 0 the application executed in such a way that the channel should proceed in the dial plan
 * \retval -1 the channel should no longer proceed through the dial plan
 *
 * \note this function should only be used to register the parking application and not generally to park calls.
 */
int park_app_exec(struct ast_channel *chan, const char *data);

/*!
 * \since 12
 * \brief Execution function for the parked call application
 *
 * \param chan ast_channel entering the application
 * \param data arguments to the application
 *
 * \retval 0 the application executed in such a way that the channel should proceed in the dial plan
 * \retval -1 the channel should no longer proceed through the dial plan
 */
int parked_call_app_exec(struct ast_channel *chan, const char *data);

/*!
 * \since 12
 * \brief Execution function for the park and retrieve application
 *
 * \param chan ast_channel entering the application
 * \param data arguments to the application
 *
 * \retval 0 the application executed in such a way that the channel should proceed in the dial plan
 * \retval -1 the channel should no longer proceed through the dial plan
 *
 * \note this function should only be used to register the park and announce application and not generally to park and announce.
 */
int park_and_announce_app_exec(struct ast_channel *chan, const char *data);

/*!
 * \since 12
 * \brief Register CLI commands
 *
 * \retval 0 if successful
 * \retval -1 on failure
 */
int load_parking_ui(void);

/*!
 * \since 12
 * \brief Unregister CLI commands
 */
void unload_parking_ui(void);

/*!
 * \since 12
 * \brief Register manager actions and setup subscriptions for stasis events
 */
int load_parking_manager(void);

/*!
 * \since 12
 * \brief Unregister manager actions and remove subscriptions for stasis events
 */
void unload_parking_manager(void);

/*!
 * \since 12
 * \brief Register bridge features for parking
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int load_parking_bridge_features(void);

/*!
 * \since 12
 * \brief Unregister features registered by load_parking_bridge_features
 */
void unload_parking_bridge_features(void);
