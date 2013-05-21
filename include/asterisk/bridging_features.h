/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Channel Bridging API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_BRIDGING_FEATURES_H
#define _ASTERISK_BRIDGING_FEATURES_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief Flags used for bridge features */
enum ast_bridge_feature_flags {
	/*! Upon channel hangup all bridge participants should be kicked out. */
	AST_BRIDGE_FLAG_DISSOLVE_HANGUP = (1 << 0),
	/*! The last channel to leave the bridge dissolves it. */
	AST_BRIDGE_FLAG_DISSOLVE_EMPTY = (1 << 1),
	/*! Move between bridging technologies as needed. */
	AST_BRIDGE_FLAG_SMART = (1 << 2),
	/*! Bridge channels cannot be merged from this bridge. */
	AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM = (1 << 3),
	/*! Bridge channels cannot be merged to this bridge. */
	AST_BRIDGE_FLAG_MERGE_INHIBIT_TO = (1 << 4),
	/*! Bridge channels cannot be local channel swap optimized from this bridge. */
	AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM = (1 << 5),
	/*! Bridge channels cannot be local channel swap optimized to this bridge. */
	AST_BRIDGE_FLAG_SWAP_INHIBIT_TO = (1 << 6),
	/*! Bridge channels can be moved to another bridge only by masquerade (ConfBridge) */
	AST_BRIDGE_FLAG_MASQUERADE_ONLY = (1 << 7),
	/*! Bridge does not allow transfers of channels out */
	AST_BRIDGE_FLAG_TRANSFER_PROHIBITED = (1 << 6),
	/*! Bridge transfers require transfer of entire bridge rather than individual channels */
	AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY = (1 << 7),
};

/*! \brief Flags used for per bridge channel features */
enum ast_bridge_channel_feature_flags {
	/*! Upon channel hangup all bridge participants should be kicked out. */
	AST_BRIDGE_CHANNEL_FLAG_DISSOLVE_HANGUP = (1 << 0),
	/*! This channel leaves the bridge if all participants have this flag set. */
	AST_BRIDGE_CHANNEL_FLAG_LONELY = (1 << 1),
	/*! This channel cannot be moved to another bridge. */
	AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE = (1 << 2),
};

/*! \brief Built in DTMF features */
enum ast_bridge_builtin_feature {
	/*! DTMF based Blind Transfer */
	AST_BRIDGE_BUILTIN_BLINDTRANSFER,
	/*! DTMF based Attended Transfer */
	AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER,
	/*!
	 * DTMF based depart bridge feature
	 *
	 * \note Imparted channels are optionally hangup depending upon
	 * how it was imparted.
	 *
	 * \note Joined channels exit the bridge with
	 * AST_BRIDGE_CHANNEL_STATE_END.
	 */
	AST_BRIDGE_BUILTIN_HANGUP,
	/*!
	 * DTMF based Park
	 *
	 * \details The bridge is parked and the channel hears the
	 * parking slot to which it was parked.
	 */
	AST_BRIDGE_BUILTIN_PARKCALL,
/* BUGBUG does Monitor and/or MixMonitor require a two party bridge?  MixMonitor is used by ConfBridge so maybe it doesn't. */
	/*!
	 * DTMF one-touch-record toggle using Monitor app.
	 *
	 * \note Only valid on two party bridges.
	 */
	AST_BRIDGE_BUILTIN_AUTOMON,
	/*!
	 * DTMF one-touch-record toggle using MixMonitor app.
	 *
	 * \note Only valid on two party bridges.
	 */
	AST_BRIDGE_BUILTIN_AUTOMIXMON,

	/*! End terminator for list of built in features. Must remain last. */
	AST_BRIDGE_BUILTIN_END
};

enum ast_bridge_builtin_interval {
	/*! Apply Call Duration Limits */
	AST_BRIDGE_BUILTIN_INTERVAL_LIMITS,

	/*! End terminator for list of built in interval features. Must remain last. */
	AST_BRIDGE_BUILTIN_INTERVAL_END
};

struct ast_bridge;
struct ast_bridge_channel;

/*!
 * \brief Hook callback type
 *
 * \param bridge The bridge that the channel is part of
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * For interval hooks:
 * \retval 0 Setup to fire again at the last interval.
 * \retval positive Setup to fire again at the new interval returned.
 * \retval -1 Remove the callback hook.
 *
 * For other hooks:
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
typedef int (*ast_bridge_hook_callback)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt);

/*!
 * \brief Hook pvt destructor callback
 *
 * \param hook_pvt Private data passed in when the hook was created to destroy
 */
typedef void (*ast_bridge_hook_pvt_destructor)(void *hook_pvt);

/*!
 * \brief Talking indicator callback
 *
 * \details This callback can be registered with the bridge in order
 * to receive updates on when a bridge_channel has started and stopped
 * talking
 *
 * \param bridge_channel Channel executing the feature
 * \param talking TRUE if the channel is now talking
 *
 * \retval 0 success
 * \retval -1 failure
 */
typedef void (*ast_bridge_talking_indicate_callback)(struct ast_bridge_channel *bridge_channel, void *pvt_data, int talking);


typedef void (*ast_bridge_talking_indicate_destructor)(void *pvt_data);

/*!
 * \brief Maximum length of a DTMF feature string
 */
#define MAXIMUM_DTMF_FEATURE_STRING (11 + 1)

/*! Extra parameters for a DTMF feature hook. */
struct ast_bridge_hook_dtmf {
	/*! DTMF String that is examined during a feature hook lookup */
	char code[MAXIMUM_DTMF_FEATURE_STRING];
};

/*! Extra parameters for an interval timer hook. */
struct ast_bridge_hook_timer {
	/*! Time at which the hook should actually trip */
	struct timeval trip_time;
	/*! Heap index for interval hook */
	ssize_t heap_index;
	/*! Interval that the hook should execute at in milliseconds */
	unsigned int interval;
	/*! Sequence number for the hook to ensure expiration ordering */
	unsigned int seqno;
};

/* BUGBUG Need to be able to selectively remove DTMF, hangup, and interval hooks. */
/*! \brief Structure that is the essence of a feature hook. */
struct ast_bridge_hook {
	/*! Linked list information */
	AST_LIST_ENTRY(ast_bridge_hook) entry;
	/*! Callback that is called when hook is tripped */
	ast_bridge_hook_callback callback;
	/*! Callback to destroy hook_pvt data right before destruction. */
	ast_bridge_hook_pvt_destructor destructor;
	/*! Unique data that was passed into us */
	void *hook_pvt;
	/*! TRUE if the hook is removed when the channel is pulled from the bridge. */
	unsigned int remove_on_pull:1;
	/*! Extra hook parameters. */
	union {
		/*! Extra parameters for a DTMF feature hook. */
		struct ast_bridge_hook_dtmf dtmf;
		/*! Extra parameters for an interval timer hook. */
		struct ast_bridge_hook_timer timer;
	} parms;
};

#define BRIDGE_FEATURES_INTERVAL_RATE 10

/*!
 * \brief Structure that contains features information
 */
struct ast_bridge_features {
	/*! Attached DTMF feature hooks */
	struct ao2_container *dtmf_hooks;
	/*! Attached hangup interception hooks container */
	struct ao2_container *hangup_hooks;
	/*! Attached bridge channel join interception hooks container */
	struct ao2_container *join_hooks;
	/*! Attached bridge channel leave interception hooks container */
	struct ao2_container *leave_hooks;
	/*! Attached interval hooks */
	struct ast_heap *interval_hooks;
	/*! Used to determine when interval based features should be checked */
	struct ast_timer *interval_timer;
	/*! Limits feature data */
	struct ast_bridge_features_limits *limits;
	/*! Callback to indicate when a bridge channel has started and stopped talking */
	ast_bridge_talking_indicate_callback talker_cb;
	/*! Callback to destroy any pvt data stored for the talker. */
	ast_bridge_talking_indicate_destructor talker_destructor_cb;
	/*! Talker callback pvt data */
	void *talker_pvt_data;
	/*! Feature flags that are enabled */
	struct ast_flags feature_flags;
	/*! Used to assign the sequence number to the next interval hook added. */
	unsigned int interval_sequence;
	/*! TRUE if feature_flags is setup */
	unsigned int usable:1;
	/*! TRUE if the channel/bridge is muted. */
	unsigned int mute:1;
	/*! TRUE if DTMF should be passed into the bridge tech.  */
	unsigned int dtmf_passthrough:1;
};

/*!
 * \brief Structure that contains configuration information for the blind transfer built in feature
 */
struct ast_bridge_features_blind_transfer {
/* BUGBUG the context should be figured out based upon TRANSFER_CONTEXT channel variable of A/B or current context of A/B. More appropriate for when channel moved to other bridges. */
	/*! Context to use for transfers */
	char context[AST_MAX_CONTEXT];
};

/*!
 * \brief Structure that contains configuration information for the attended transfer built in feature
 */
struct ast_bridge_features_attended_transfer {
/* BUGBUG the context should be figured out based upon TRANSFER_CONTEXT channel variable of A/B or current context of A/B. More appropriate for when channel moved to other bridges. */
	/*! Context to use for transfers */
	char context[AST_MAX_CONTEXT];
	/*! DTMF string used to abort the transfer */
	char abort[MAXIMUM_DTMF_FEATURE_STRING];
	/*! DTMF string used to turn the transfer into a three way conference */
	char threeway[MAXIMUM_DTMF_FEATURE_STRING];
	/*! DTMF string used to complete the transfer */
	char complete[MAXIMUM_DTMF_FEATURE_STRING];
};

/*!
 * \brief Structure that contains configuration information for the limits feature
 */
struct ast_bridge_features_limits {
	/*! Maximum duration that the channel is allowed to be in the bridge (specified in milliseconds) */
	unsigned int duration;
	/*! Duration into the call when warnings should begin (specified in milliseconds or 0 to disable) */
	unsigned int warning;
	/*! Interval between the warnings (specified in milliseconds or 0 to disable) */
	unsigned int frequency;

	AST_DECLARE_STRING_FIELDS(
		/*! Sound file to play when the maximum duration is reached (if empty, then nothing will be played) */
		AST_STRING_FIELD(duration_sound);
		/*! Sound file to play when the warning time is reached (if empty, then the remaining time will be played) */
		AST_STRING_FIELD(warning_sound);
		/*! Sound file to play when the call is first entered (if empty, then the remaining time will be played) */
		AST_STRING_FIELD(connect_sound);
	);
	/*! Time when the bridge will be terminated by the limits feature */
	struct timeval quitting_time;
};

/*!
 * \brief Register a handler for a built in feature
 *
 * \param feature The feature that the handler will be responsible for
 * \param callback The callback function that will handle it
 * \param dtmf Default DTMF string used to activate the feature
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_features_register(AST_BRIDGE_BUILTIN_ATTENDED_TRANSFER, bridge_builtin_attended_transfer, "*1");
 * \endcode
 *
 * This registers the function bridge_builtin_attended_transfer as the function responsible for the built in
 * attended transfer feature.
 */
int ast_bridge_features_register(enum ast_bridge_builtin_feature feature, ast_bridge_hook_callback callback, const char *dtmf);

/*!
 * \brief Unregister a handler for a built in feature
 *
 * \param feature The feature to unregister
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_features_unregister(AST_BRIDGE_BUILTIN_ATTENDED_TRANSFER);
 * \endcode
 *
 * This unregisters the function that is handling the built in attended transfer feature.
 */
int ast_bridge_features_unregister(enum ast_bridge_builtin_feature feature);

/*!
 * \brief Attach interval hooks to a bridge features structure
 *
 * \param features Bridge features structure
 * \param limits Configured limits applicable to the channel
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
typedef int (*ast_bridge_builtin_set_limits_fn)(struct ast_bridge_features *features, struct ast_bridge_features_limits *limits, int remove_on_pull);

/*!
 * \brief Register a handler for a built in interval feature
 *
 * \param interval The interval feature that the handler will be responsible for
 * \param callback the Callback function that will handle it
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_interval_register(AST_BRIDGE_BUILTIN_INTERVAL_LIMITS, bridge_builtin_set_limits);
 * \endcode
 *
 * This registers the function bridge_builtin_set_limits as the function responsible for the built in
 * duration limit feature.
 */
int ast_bridge_interval_register(enum ast_bridge_builtin_interval interval, ast_bridge_builtin_set_limits_fn callback);

/*!
 * \brief Unregisters a handler for a built in interval feature
 *
 * \param interval the interval feature to unregister
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_interval_unregister(AST_BRIDGE_BULTIN_INTERVAL_LIMITS)
 * /endcode
 *
 * This unregisters the function that is handling the built in duration limit feature.
 */
int ast_bridge_interval_unregister(enum ast_bridge_builtin_interval interval);

/*!
 * \brief Attach a bridge channel join hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_join_hook(&features, join_callback, NULL, NULL, 0);
 * \endcode
 *
 * This makes the bridging core call join_callback when a
 * channel successfully joins the bridging system.  A pointer to
 * useful data may be provided to the hook_pvt parameter.
 */
int ast_bridge_join_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull);

/*!
 * \brief Attach a bridge channel leave hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_leave_hook(&features, leave_callback, NULL, NULL, 0);
 * \endcode
 *
 * This makes the bridging core call leave_callback when a
 * channel successfully leaves the bridging system.  A pointer
 * to useful data may be provided to the hook_pvt parameter.
 */
int ast_bridge_leave_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull);

/*!
 * \brief Attach a hangup hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_hangup_hook(&features, hangup_callback, NULL, NULL, 0);
 * \endcode
 *
 * This makes the bridging core call hangup_callback if a
 * channel that has this hook hangs up.  A pointer to useful
 * data may be provided to the hook_pvt parameter.
 */
int ast_bridge_hangup_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull);

/*!
 * \brief Attach a DTMF hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param dtmf DTMF string to be activated upon
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_dtmf_hook(&features, "#", pound_callback, NULL, NULL, 0);
 * \endcode
 *
 * This makes the bridging core call pound_callback if a channel that has this
 * feature structure inputs the DTMF string '#'. A pointer to useful data may be
 * provided to the hook_pvt parameter.
 */
int ast_bridge_dtmf_hook(struct ast_bridge_features *features,
	const char *dtmf,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull);

/*!
 * \brief attach an interval hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param interval The interval that the hook should execute at in milliseconds
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_interval_hook(&features, 1000, playback_callback, NULL, NULL, 0);
 * \endcode
 *
 * This makes the bridging core call playback_callback every second. A pointer to useful
 * data may be provided to the hook_pvt parameter.
 */
int ast_bridge_interval_hook(struct ast_bridge_features *features,
	unsigned int interval,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull);

/*!
 * \brief Set a callback on the features structure to receive talking notifications on.
 *
 * \param features Bridge features structure
 * \param talker_cb Callback function to execute when talking events occur in the bridge core.
 * \param pvt_data Optional unique data that will be passed with the talking events.
 * \param talker_destructor Optional destructor callback for pvt data.
 *
 * \return Nothing
 */
void ast_bridge_features_set_talk_detector(struct ast_bridge_features *features,
	ast_bridge_talking_indicate_callback talker_cb,
	ast_bridge_talking_indicate_destructor talker_destructor,
	void *pvt_data);

/*!
 * \brief Enable a built in feature on a bridge features structure
 *
 * \param features Bridge features structure
 * \param feature Feature to enable
 * \param dtmf Optionally the DTMF stream to trigger the feature, if not specified it will be the default
 * \param config Configuration structure unique to the built in type
 * \param destructor Optional destructor callback for config data
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_features_enable(&features, AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER, NULL, NULL, 0);
 * \endcode
 *
 * This enables the attended transfer DTMF option using the default DTMF string. An alternate
 * string may be provided using the dtmf parameter. Internally this is simply setting up a hook
 * to a built in feature callback function.
 */
int ast_bridge_features_enable(struct ast_bridge_features *features,
	enum ast_bridge_builtin_feature feature,
	const char *dtmf,
	void *config,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull);

/*!
 * \brief Constructor function for ast_bridge_features_limits
 *
 * \param limits pointer to a ast_bridge_features_limits struct that has been allocted, but not initialized
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_bridge_features_limits_construct(struct ast_bridge_features_limits *limits);

/*!
 * \brief Destructor function for ast_bridge_features_limits
 *
 * \param limits pointer to an ast_bridge_features_limits struct that needs to be destroyed
 *
 * This function does not free memory allocated to the ast_bridge_features_limits struct, it only frees elements within the struct.
 * You must still call ast_free on the the struct if you allocated it with malloc.
 */
void ast_bridge_features_limits_destroy(struct ast_bridge_features_limits *limits);

/*!
 * \brief Limit the amount of time a channel may stay in the bridge and optionally play warning messages as time runs out
 *
 * \param features Bridge features structure
 * \param limits Configured limits applicable to the channel
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * struct ast_bridge_features_limits limits;
 * ast_bridge_features_init(&features);
 * ast_bridge_features_limits_construct(&limits);
 * ast_bridge_features_set_limits(&features, &limits, 0);
 * ast_bridge_features_limits_destroy(&limits);
 * \endcode
 *
 * This sets the maximum time the channel can be in the bridge to 10 seconds and does not play any warnings.
 *
 * \note This API call can only be used on a features structure that will be used in association with a bridge channel.
 * \note The ast_bridge_features_limits structure must remain accessible for the lifetime of the features structure.
 */
int ast_bridge_features_set_limits(struct ast_bridge_features *features, struct ast_bridge_features_limits *limits, int remove_on_pull);

/*!
 * \brief Set a flag on a bridge channel features structure
 *
 * \param features Bridge channel features structure
 * \param flag Flag to enable
 *
 * \return Nothing
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_features_set_flag(&features, AST_BRIDGE_CHANNEL_FLAG_DISSOLVE_HANGUP);
 * \endcode
 *
 * This sets the AST_BRIDGE_CHANNEL_FLAG_DISSOLVE_HANGUP feature
 * to be enabled on the features structure 'features'.
 */
void ast_bridge_features_set_flag(struct ast_bridge_features *features, unsigned int flag);

/*!
 * \brief Initialize bridge features structure
 *
 * \param features Bridge featues structure
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * \endcode
 *
 * This initializes the feature structure 'features' to have nothing enabled.
 *
 * \note This MUST be called before enabling features or flags. Failure to do so
 *       may result in a crash.
 */
int ast_bridge_features_init(struct ast_bridge_features *features);

/*!
 * \brief Clean up the contents of a bridge features structure
 *
 * \param features Bridge features structure
 *
 * \return Nothing
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_features_cleanup(&features);
 * \endcode
 *
 * This cleans up the feature structure 'features'.
 *
 * \note This MUST be called after the features structure is done being used
 *       or a memory leak may occur.
 */
void ast_bridge_features_cleanup(struct ast_bridge_features *features);

/*!
 * \brief Allocate a new bridge features struct.
 * \since 12.0.0
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features *features;
 * features = ast_bridge_features_new();
 * ast_bridge_features_destroy(features);
 * \endcode
 *
 * \retval features New allocated features struct.
 * \retval NULL on error.
 */
struct ast_bridge_features *ast_bridge_features_new(void);

/*!
 * \brief Destroy an allocated bridge features struct.
 * \since 12.0.0
 *
 * \param features Bridge features structure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features *features;
 * features = ast_bridge_features_new();
 * ast_bridge_features_destroy(features);
 * \endcode
 *
 * \return Nothing
 */
void ast_bridge_features_destroy(struct ast_bridge_features *features);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BRIDGING_FEATURES_H */
