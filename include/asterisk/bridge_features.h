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

/*!
 * \file
 * \brief Channel Bridging API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_BRIDGING_FEATURES_H
#define _ASTERISK_BRIDGING_FEATURES_H

#include "asterisk/channel.h"

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
	AST_BRIDGE_FLAG_TRANSFER_PROHIBITED = (1 << 8),
	/*! Bridge transfers require transfer of entire bridge rather than individual channels */
	AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY = (1 << 9),
	/*! Bridge is invisible to AMI/CLI/ARI/etc. */
	AST_BRIDGE_FLAG_INVISIBLE = (1 << 10),
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
	 * BRIDGE_CHANNEL_STATE_END_WITH_DISSOLVE.
	 */
	AST_BRIDGE_BUILTIN_HANGUP,
	/*!
	 * DTMF based Park
	 *
	 * \details The bridge is parked and the channel hears the
	 * parking slot to which it was parked.
	 */
	AST_BRIDGE_BUILTIN_PARKCALL,
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
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0        for interval hooks: setup to fire again at the last interval.
 *                  for other hooks: keep the callback hook.
 * \retval positive for interval hooks: Setup to fire again at the new interval returned.
 *                  for other hooks: n/a
 * \retval -1       for all hooks: remove the callback hook.
 */
typedef int (*ast_bridge_hook_callback)(struct ast_bridge_channel *bridge_channel, void *hook_pvt);

/*!
 * \brief Hook pvt destructor callback
 *
 * \param hook_pvt Private data passed in when the hook was created to destroy
 */
typedef void (*ast_bridge_hook_pvt_destructor)(void *hook_pvt);

/*!
 * \brief Talking indicator callback
 *
 * \details
 * This callback can be registered with the bridge channel in
 * order to receive updates when the bridge_channel has started
 * and stopped talking.
 *
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 * \param talking TRUE if the channel is now talking
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
typedef int (*ast_bridge_talking_indicate_callback)(struct ast_bridge_channel *bridge_channel, void *hook_pvt, int talking);

/*!
 * \brief Move indicator callback
 *
 * \details
 * This callback can be registered with the bridge channel in order
 * to be notified when the bridge channel is being moved from one
 * bridge to another.
 *
 * \param bridge_channel The channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 * \param src The bridge from which the bridge channel is moving
 * \param dst The bridge into which the bridge channel is moving
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
typedef int (*ast_bridge_move_indicate_callback)(struct ast_bridge_channel *bridge_channel,
		void *hook_pvt, struct ast_bridge *src, struct ast_bridge *dst);

enum ast_bridge_hook_remove_flags {
	/*! The hook is removed when the channel is pulled from the bridge. */
	AST_BRIDGE_HOOK_REMOVE_ON_PULL = (1 << 0),
	/*! The hook is removed when the bridge's personality changes. */
	AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE = (1 << 1),
};

enum ast_bridge_hook_type {
	/*! The hook type has not been specified. */
	AST_BRIDGE_HOOK_TYPE_NONE,
	AST_BRIDGE_HOOK_TYPE_DTMF,
	AST_BRIDGE_HOOK_TYPE_TIMER,
	AST_BRIDGE_HOOK_TYPE_HANGUP,
	AST_BRIDGE_HOOK_TYPE_JOIN,
	AST_BRIDGE_HOOK_TYPE_LEAVE,
	AST_BRIDGE_HOOK_TYPE_TALK,
	AST_BRIDGE_HOOK_TYPE_MOVE,
};

/*! \brief Structure that is the essence of a feature hook. */
struct ast_bridge_hook {
	/*! Callback that is called when hook is tripped */
	ast_bridge_hook_callback callback;
	/*! Callback to destroy hook_pvt data right before destruction. */
	ast_bridge_hook_pvt_destructor destructor;
	/*! Unique data that was passed into us */
	void *hook_pvt;
	/*! Flags determining when hooks should be removed from a bridge channel */
	struct ast_flags remove_flags;
	/*! What kind of hook this is. */
	enum ast_bridge_hook_type type;
};

/*!
 * \brief Maximum length of a DTMF feature string
 */
#define MAXIMUM_DTMF_FEATURE_STRING (11 + 1)

/*! Extra parameters for a DTMF feature hook. */
struct ast_bridge_hook_dtmf_parms {
	/*! DTMF String that is examined during a feature hook lookup */
	char code[MAXIMUM_DTMF_FEATURE_STRING];
};

/*! DTMF specific hook. */
struct ast_bridge_hook_dtmf {
	/*! Generic feature hook information. */
	struct ast_bridge_hook generic;
	/*! Extra parameters for a DTMF feature hook. */
	struct ast_bridge_hook_dtmf_parms dtmf;
};

enum ast_bridge_hook_timer_option {
	/*! The timer temporarily affects media. (Like a custom playfile.) */
	AST_BRIDGE_HOOK_TIMER_OPTION_MEDIA = (1 << 0),
};

/*! Extra parameters for an interval timer hook. */
struct ast_bridge_hook_timer_parms {
	/*! Time at which the hook should actually trip */
	struct timeval trip_time;
	/*! Heap index for interval hook */
	ssize_t heap_index;
	/*! Interval that the hook should execute at in milliseconds */
	unsigned int interval;
	/*! Sequence number for the hook to ensure expiration ordering */
	unsigned int seqno;
	/*! Option flags determining how callback is called. */
	unsigned int flags;
};

/*! Timer specific hook. */
struct ast_bridge_hook_timer {
	/*! Generic feature hook information. */
	struct ast_bridge_hook generic;
	/*! Extra parameters for an interval timer hook. */
	struct ast_bridge_hook_timer_parms timer;
};

/*!
 * \brief Structure that contains features information
 */
struct ast_bridge_features {
	/*! Attached DTMF feature hooks */
	struct ao2_container *dtmf_hooks;
	/*! Attached miscellaneous other hooks. */
	struct ao2_container *other_hooks;
	/*! Attached interval hooks */
	struct ast_heap *interval_hooks;
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
	/*! TRUE to avoid generating COLP frames when joining the bridge */
	unsigned int inhibit_colp:1;
	/*! TRUE if text messaging is permitted. */
	unsigned int text_messaging:1;
};

/*!
 * \brief Structure that contains configuration information for the blind transfer built in feature
 */
struct ast_bridge_features_blind_transfer {
	/*! Context to use for transfers (If not empty.) */
	char context[AST_MAX_CONTEXT];
};

/*!
 * \brief Structure that contains configuration information for the attended transfer built in feature
 */
struct ast_bridge_features_attended_transfer {
	/*! Context to use for transfers (If not empty.) */
	char context[AST_MAX_CONTEXT];
	/*! DTMF string used to abort the transfer (If not empty.) */
	char abort[MAXIMUM_DTMF_FEATURE_STRING];
	/*! DTMF string used to turn the transfer into a three way conference (If not empty.) */
	char threeway[MAXIMUM_DTMF_FEATURE_STRING];
	/*! DTMF string used to complete the transfer (If not empty.) */
	char complete[MAXIMUM_DTMF_FEATURE_STRING];
	/*! DTMF string used to swap bridged targets (If not empty.) */
	char swap[MAXIMUM_DTMF_FEATURE_STRING];
};

enum ast_bridge_features_monitor {
	/*! Toggle start/stop of Monitor/MixMonitor. */
	AUTO_MONITOR_TOGGLE,
	/*! Start Monitor/MixMonitor if not already started. */
	AUTO_MONITOR_START,
	/*! Stop Monitor/MixMonitor if not already stopped. */
	AUTO_MONITOR_STOP,
};

struct ast_bridge_features_automonitor {
	/*! Start/Stop behavior. */
	enum ast_bridge_features_monitor start_stop;
};

struct ast_bridge_features_automixmonitor {
	/*! Start/Stop behavior. */
	enum ast_bridge_features_monitor start_stop;
};

/*!
 * \brief Structure that contains configuration information for the limits feature
 */
struct ast_bridge_features_limits {
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
	/*! Maximum duration that the channel is allowed to be in the bridge (specified in milliseconds) */
	unsigned int duration;
	/*! Duration into the call when warnings should begin (specified in milliseconds or 0 to disable) */
	unsigned int warning;
	/*! Interval between the warnings (specified in milliseconds or 0 to disable) */
	unsigned int frequency;
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
 * \brief Invoke a built in feature hook now.
 *
 * \param feature The feature to invoke
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \note This API call is only meant to be used by bridge
 * subclasses and hook callbacks to request a builtin feature
 * hook to be executed.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_features_do(AST_BRIDGE_BUILTIN_ATTENDED_TRANSFER, bridge_channel, hook_pvt);
 * \endcode
 */
int ast_bridge_features_do(enum ast_bridge_builtin_feature feature, struct ast_bridge_channel *bridge_channel, void *hook_pvt);

/*!
 * \brief Attach interval hooks to a bridge features structure
 *
 * \param features Bridge features structure
 * \param limits Configured limits applicable to the channel
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
typedef int (*ast_bridge_builtin_set_limits_fn)(struct ast_bridge_features *features,
		struct ast_bridge_features_limits *limits, enum ast_bridge_hook_remove_flags remove_flags);

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
 * \endcode
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
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
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
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Attach a bridge channel leave hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
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
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Attach a hangup hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
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
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Attach a DTMF hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param dtmf DTMF string to be activated upon
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
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
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Attach an interval hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param flags Interval timer callback option flags.
 * \param interval The interval that the hook should execute at in milliseconds
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
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
	enum ast_bridge_hook_timer_option flags,
	unsigned int interval,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Attach a bridge channel talk detection hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_talk_hook(&features, talk_callback, NULL, NULL, 0);
 * \endcode
 *
 * This makes the bridging technology call talk_callback when a
 * channel is recognized as starting and stopping talking.  A
 * pointer to useful data may be provided to the hook_pvt
 * parameter.
 *
 * \note This hook is currently only supported by softmix.
 */
int ast_bridge_talk_detector_hook(struct ast_bridge_features *features,
	ast_bridge_talking_indicate_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Attach a bridge channel move detection hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_move_hook(&features, move_callback, NULL, NULL, 0);
 * \endcode
 *
 * This makes the bridging core call \p callback when a
 * channel is moved from one bridge to another.  A
 * pointer to useful data may be provided to the hook_pvt
 * parameter.
 */
int ast_bridge_move_hook(struct ast_bridge_features *features,
	ast_bridge_move_indicate_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Enable a built in feature on a bridge features structure
 *
 * \param features Bridge features structure
 * \param feature Feature to enable
 * \param dtmf Optionally the DTMF stream to trigger the feature, if not specified it will be the default
 * \param config Configuration structure unique to the built in type
 * \param destructor Optional destructor callback for config data
 * \param remove_flags Dictates what situations the hook should be removed.
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
	enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Constructor function for ast_bridge_features_limits
 *
 * \param limits pointer to a ast_bridge_features_limits struct that has been allocated, but not initialized
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
 * You must still call ast_free on the struct if you allocated it with malloc.
 */
void ast_bridge_features_limits_destroy(struct ast_bridge_features_limits *limits);

/*!
 * \brief Limit the amount of time a channel may stay in the bridge and optionally play warning messages as time runs out
 *
 * \param features Bridge features structure
 * \param limits Configured limits applicable to the channel
 * \param remove_flags Dictates what situations the hook should be removed.
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
int ast_bridge_features_set_limits(struct ast_bridge_features *features, struct ast_bridge_features_limits *limits, enum ast_bridge_hook_remove_flags remove_flags);

/*!
 * \brief Set a flag on a bridge channel features structure
 *
 * \param features Bridge channel features structure
 * \param flag Flag to enable
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
 * \brief Merge one ast_bridge_features into another
 *
 * \param into The ast_bridge_features that will be merged into
 * \param from The ast_bridge_features that will be merged from
 */
void ast_bridge_features_merge(struct ast_bridge_features *into, const struct ast_bridge_features *from);

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
 * \return features New allocated features struct.
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
 */
void ast_bridge_features_destroy(struct ast_bridge_features *features);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BRIDGING_FEATURES_H */
