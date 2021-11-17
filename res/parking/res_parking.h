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

#ifndef ASTERISK_RES_PARKING_H
#define ASTERISK_RES_PARKING_H

#include "asterisk/pbx.h"
#include "asterisk/bridge.h"
#include "asterisk/parking.h"
#include "asterisk/stasis_channels.h"

#define DEFAULT_PARKING_LOT "default"
#define DEFAULT_PARKING_EXTEN "700"
#define BASE_REGISTRAR "res_parking"
#define PARK_DIAL_CONTEXT "park-dial"
#define PARKED_CALL_APPLICATION "ParkedCall"

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
		AST_STRING_FIELD(registrar);          /*!< Which registrar the lot uses if it isn't the default registrar */
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
	struct ast_channel_snapshot *retriever;   /*!< Snapshot of the channel that retrieves a parked call */
	struct timeval start;                     /*!< When the call was parked */
	int parking_space;                        /*!< Which parking space is used */
	char comeback[AST_MAX_CONTEXT];           /*!< Where to go on parking timeout */
	char *parker_dial_string;                 /*!< dialstring to call back with comebacktoorigin. Used timeout extension generation and call control */
	unsigned int time_limit;                  /*!< How long this specific channel may remain in the parking lot before timing out */
	struct parking_lot *lot;                  /*!< Which parking lot the user is parked to */
	enum park_call_resolution resolution;     /*!< How did the parking session end? If the call is in a bridge, lock parked_user before checking/setting */
};

#if defined(TEST_FRAMEWORK)
/*!
 * \since 12.0.0
 * \brief Create an empty parking lot configuration structure
 *        useful for unit tests.
 *
 * \param cat name given to the parking lot
 *
 * \retval NULL failure
 * \retval non-NULL successfully allocated parking lot
 */
struct parking_lot_cfg *parking_lot_cfg_create(const char *cat);
#endif

/*!
 * \since 12.0.0
 * \brief If a parking lot exists in the parking lot list already, update its status to match the provided
 *        configuration and return a reference return a reference to it. Otherwise, create a parking lot
 *        struct based on a parking lot configuration and return a reference to the new one.
 *
 * \param cfg The configuration being used as a reference to build the parking lot from.
 * \param dynamic non-zero if creating a dynamic parking lot with this. Don't replace existing parking lots. Ever.
 *
 * \return A reference to the new parking lot
 * \retval NULL if it was not found and could not be allocated
 *
 * \note The parking lot will need to be unreffed if it ever falls out of scope
 * \note The parking lot will automatically be added to the parking lot container if needed as part of this process
 */
struct parking_lot *parking_lot_build_or_update(struct parking_lot_cfg *cfg, int dynamic);

/*!
 * \since 12.0.0
 * \brief Remove a parking lot from the usable lists if it is no longer involved in any calls and no configuration currently claims it
 *
 * \param lot Which parking lot is being checked for elimination
 *
 * \retval 0 if the parking lot was removed
 * \retval -1 if the parking lot wasn't removed.
 *
 * \note This should generally be called when something is happening that could cause a parking lot to die such as a call being unparked or
 *       a parking lot no longer existing in configurations.
 */
int parking_lot_remove_if_unused(struct parking_lot *lot);

/*!
 * \since 12.0.0
 * \brief Create a new parking bridge
 *
 * \param bridge_lot Parking lot which the new bridge should be based on
 *
 * \retval NULL if the bridge can not be created
 * \return Newly created parking bridge
 */
struct ast_bridge *bridge_parking_new(struct parking_lot *bridge_lot);

/*!
 * \since 12.0.0
 * \brief Get a reference to a parking lot's bridge. If it doesn't exist, create it and get a reference.
 *
 * \param lot Which parking lot we need the bridge from. This parking lot must be locked before calling this function.
 *
 * \return A reference to the ast_bridge associated with the parking lot
 * \retval NULL if it didn't already have a bridge and one couldn't be created
 *
 * \note This bridge will need to be unreffed if it ever falls out of scope.
 */
struct ast_bridge *parking_lot_get_bridge(struct parking_lot *lot);

/*!
 * \since 12.0.0
 * \brief Get an available parking space within a parking lot.
 *
 * \param lot Which parking lot we are getting a space from
 * \param target_override If there is a specific slot we want, provide it here and we'll start from that position
 *
 * \retval -1 if No slot can be found
 * \return integer value of parking space selected
 *
 * \note lot should be locked before this is called and unlocked only after a parked_user with the space
 *       returned has been added to the parking lot.
 */
int parking_lot_get_space(struct parking_lot *lot, int target_override);

/*!
 * \brief Determine if there is a parked user in a parking space and return it if there is.
 *
 * \param lot Parking lot being pulled from
 * \param target If < 0   search for the first occupied space in the parking lot
 *               If >= 0  Only pull from the indicated target
 *
 * \retval NULL if no parked user could be pulled from the requested parking lot at the requested parking space
 * \return reference to the requested parked user
 *
 */
struct parked_user *parking_lot_inspect_parked_user(struct parking_lot *lot, int target);

/*!
 * \since 12.0.0
 * \brief Determine if there is a parked user in a parking space and pull it from the parking lot if there is.
 *
 * \param lot Parking lot being pulled from
 * \param target If < 0   search for the first occupied space in the parking lot
 *               If >= 0  Only pull from the indicated target
 *
 * \retval NULL if no parked user could be pulled from the requested parking lot at the requested parking space
 * \return reference to the requested parked user
 *
 * \note The parked user will be removed from parking lot as part of this process
 * \note Remove this reference with ao2_cleanup once it falls out of scope.
 */
struct parked_user *parking_lot_retrieve_parked_user(struct parking_lot *lot, int target);

/*!
 * \since 12.0.0
 * \brief Apply features based on the parking lot feature options
 *
 * \param chan Which channel's feature set is being modified
 * \param lot parking lot which establishes the features used
 * \param recipient_mode AST_FEATURE_FLAG_BYCALLER if the user is the retriever
 *                       AST_FEATURE_FLAG_BYCALLEE if the user is the parkee
 */
void parked_call_retrieve_enable_features(struct ast_channel *chan, struct parking_lot *lot, int recipient_mode);

/*!
 * \since 12.0.0
 * \brief Set necessary bridge roles on a channel that is about to enter a parking lot
 *
 * \param chan Entering channel
 * \param lot The parking lot the channel will be entering
 * \param force_ringing Use ringing instead of music on hold
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int parking_channel_set_roles(struct ast_channel *chan, struct parking_lot *lot, int force_ringing);

/*!
 * \since 12.0.0
 * \brief custom callback function for ast_bridge_channel_queue_playfile which plays a parking space
 *        and optionally hangs up the call afterwards based on the payload in playfile.
 */
void say_parking_space(struct ast_bridge_channel *bridge_channel, const char *payload);

/*!
 * \since 12.0.0
 * \brief Setup timeout interval feature on an ast_bridge_features for parking
 *
 * \param features The ast_bridge_features we are establishing the interval hook on
 * \param user The parked_user receiving the timeout duration limits
 */
void parking_set_duration(struct ast_bridge_features *features, struct parked_user *user);

/*!
 * \since 12.0.0
 * \brief Get a pointer to the parking lot container for purposes such as iteration
 *
 * \return pointer to the parking lot container.
 */
struct ao2_container *get_parking_lot_container(void);

/*!
 * \since 12.0.0
 * \brief Find a parking lot based on its name
 *
 * \param lot_name Name of the parking lot sought
 *
 * \return The parking lot if found
 * \retval NULL if no parking lot with the name specified exists
 *
 * \note ao2_cleanup this reference when you are done using it or you'll cause leaks.
 */
struct parking_lot *parking_lot_find_by_name(const char *lot_name);

/*!
 * \since 12.0.0
 * \brief Create a dynamic parking lot
 *
 * \param name Dynamic parking lot name to create
 * \param chan Channel parkee to get dynamic parking lot parameters from
 *
 * \return dynamically created parking lot on success
 * \retval NULL on error
 *
 * \note This should be called only after verifying that the named parking lot doesn't already exist in a non-dynamic way.
 */
struct parking_lot *parking_create_dynamic_lot(const char *name, struct ast_channel *chan);

#if defined(TEST_FRAMEWORK)
/*!
 * \since 12.0.0
 * \brief Create a dynamic parking lot without respect to whether they are enabled by configuration
 *
 * \param name Dynamic parking lot name to create
 * \param chan Channel parkee to get the dynamic parking lot parameters from
 *
 * \return dynamically created parking lot on success
 * \retval NULL on error
 *
 * \note This should be called only after verifying that the named parking lot doesn't already exist in a non-dynamic way.
 */
struct parking_lot *parking_create_dynamic_lot_forced(const char *name, struct ast_channel *chan);
#endif

/*!
 * \since 12.0.0
 * \brief Find parking lot name from channel
 *
 * \param chan The channel we want the parking lot name for
 *
 * \return name of the parking lot to use for the channel.
 *
 * \note Always returns a parking lot name.
 *
 * \note Channel needs to be locked while the returned string is in use.
 */
const char *find_channel_parking_lot_name(struct ast_channel *chan);

/*!
 * \since 12.0.0
 * \brief Flattens a dial string so that it can be written to/found from PBX extensions
 *
 * \param dialstring unflattened dial string. This will be flattened in place.
 */
void flatten_dial_string(char *dialstring);

/*!
 * \since 12.0.0
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
 * \since 12.0.0
 * \brief Add extensions for a parking lot configuration
 *
 * \param lot_cfg parking lot configuration to generate extensions for
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int parking_lot_cfg_create_extensions(struct parking_lot_cfg *lot_cfg);

/*!
 * \since 12.0.0
 * \brief Remove extensions belonging to a parking lot configuration
 *
 * \param lot_cfg parking lot configuratin to remove extensions from
 *
 * \note This will not remove extensions registered non-exclusively even
 *       if those extensions were registered by lot_cfg. Those are only
 *       purged on a res_parking module reload.
 */
void parking_lot_cfg_remove_extensions(struct parking_lot_cfg *lot_cfg);

/*!
 * \since 12.0.0
 * \brief Pull a parked user out of its parking lot. Use this when you don't want to use the parked user afterwards.
 * \param user The parked user being pulled.
 *
 * \retval 0 on success
 * \retval -1 if the user didn't have its parking lot set
 */
int unpark_parked_user(struct parked_user *user);

/*!
 * \since 12.0.0
 * \brief Publish a stasis parked call message for the channel indicating failure to park.
 *
 * \param parkee channel belonging to the failed parkee
 */
void publish_parked_call_failure(struct ast_channel *parkee);

/*!
 * \since 12.0.0
 * \brief Publish a stasis parked call message for a given parked user
 *
 * \param pu pointer to a parked_user that we are generating the message for
 * \param event_type What parked call event type is provoking this message
 */
void publish_parked_call(struct parked_user *pu, enum ast_parked_call_event_type event_type);

/*!
 * \since 12.3.0
 * \brief Create a parking announcement subscription
 *
 * \param chan Channel that will receive the announcement
 * \param parkee_uuid Unique ID of the channel being parked
 * \param hangup_after if non-zero, have the channel hangup after hearing the announcement
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int create_parked_subscription(struct ast_channel *chan, const char *parkee_uuid, int hangup_after);

/*!
 * \since 12.0.0
 * \brief Setup a parked call on a parking bridge without needing to parse appdata
 *
 */
struct ast_bridge *park_common_setup(struct ast_channel *parkee, struct ast_channel *parker,
		const char *lot_name, const char *comeback_override,
		int use_ringing, int randomize, int time_limit, int silence_announcements);

/*!
 * \since 12.0.0
 * \brief Function to prepare a channel for parking by determining which parking bridge should
 *        be used, setting up a park common datastore so that the parking bridge will have access
 *        to necessary parking information when joining, and applying various bridge roles to the
 *        channel.
 *
 * \param parkee The channel being prepared for parking
 * \param parker The channel initiating the park; may be the parkee as well. May be NULL.
 * \param app_data arguments supplied to the Park application. May be NULL.
 * \param silence_announcements optional pointer to an integer where we want to store the silence option flag
 *        this value should be initialized to 0 prior to calling park_common_setup.
 *
 * \return reference to a parking bridge if successful
 * \retval NULL on failure
 *
 * \note ao2_cleanup this reference when you are done using it or you'll cause leaks.
 */
struct ast_bridge *park_application_setup(struct ast_channel *parkee, struct ast_channel *parker,
	const char *app_data, int *silence_announcements);

struct park_common_datastore {
	char *parker_uuid;           /*!< Unique ID of the channel parking the call. */
	char *parker_dial_string;    /*!< Dial string that we would attempt to call when timing out when comebacktoorigin=yes */
	char *comeback_override;     /*!< Optional goto string for where to send the call after we are done */
	int randomize;               /*!< Pick a parking space to enter on at random */
	int time_limit;              /*!< time limit override. -1 values don't override, 0 for unlimited time, >0 for custom time limit in seconds */
	int silence_announce;        /*!< Used when a call parks itself to keep it from hearing the parked call announcement */
};

/*!
 * \since 12.0.0
 * \brief Get a copy of the park_common_datastore from a channel that is being parked
 *
 * \param parkee The channel entering parking with the datastore we are checking
 *
 * \return Pointer to a copy of the park common datastore for parkee if it could be cloned. This needs to be free'd with park_common_datastore free.
 * \retval NULL if the park_common_datastore could not be copied off of the channel.
 */
struct park_common_datastore *get_park_common_datastore_copy(struct ast_channel *parkee);

/*!
 * \since 12.0.0
 * \brief Free a park common datastore struct
 *
 * \param datastore The park_common_datastore being free'd. (NULL tolerant)
 */
void park_common_datastore_free(struct park_common_datastore *datastore);

/*!
 * \since 12.0.0
 * \brief Notify metermaids that we've changed an extension
 *
 * \param exten Extension of the call parked/unparked
 * \param context Context of the call parked/unparked
 * \param state new device state
 */
void parking_notify_metermaids(int exten, const char *context, enum ast_device_state state);

/*!
 * \since 12.0.0
 * \brief Check global configuration to see if dynamic parking is enabled
 *
 * \retval 1 if dynamic parking is enabled
 * \retval 0 if dynamic parking is disabled
 */
int parking_dynamic_lots_enabled(void);

/*!
 * \since 12.0.0
 * \brief Register parking applications
 *
 * \retval 0 if successful
 * \retval -1 on failure
 */
int load_parking_applications(void);

/*!
 * \since 12.0.0
 * \brief Unregister parking applications
 */
void unload_parking_applications(void);

/*!
 * \since 12.0.0
 * \brief Register CLI commands
 *
 * \retval 0 if successful
 * \retval -1 on failure
 */
int load_parking_ui(void);

/*!
 * \since 12.0.0
 * \brief Unregister CLI commands
 */
void unload_parking_ui(void);

/*!
 * \since 12.0.0
 * \brief Register manager actions and setup subscriptions for stasis events
 */
int load_parking_manager(void);

/*!
 * \since 12.0.0
 * \brief Unregister manager actions and remove subscriptions for stasis events
 */
void unload_parking_manager(void);

/*!
 * \since 12.0.0
 * \brief Register bridge features for parking
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int load_parking_bridge_features(void);

/*!
 * \since 12.0.0
 * \brief Unregister features registered by load_parking_bridge_features
 */
void unload_parking_bridge_features(void);

/*!
 * \since 12.0.0
 * \brief Register Parking devstate handler
 */
int load_parking_devstate(void);

/*!
 * \since 12.0.0
 * \brief Unregister Parking devstate handler
 */
void unload_parking_devstate(void);

/*!
 * \since 12.0.0
 * \brief Register parking unit tests
 *
 * \retval 0 on success
 * \retval nonzero on failure
 */
int load_parking_tests(void);

/*!
 * \since 12.0.0
 * \brief Unregister parking unit tests
 */
void unload_parking_tests(void);

#endif /* ASTERISK_RES_PARKING_H */
