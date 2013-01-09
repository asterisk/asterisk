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
	/*! Upon hangup the bridge should be discontinued */
	AST_BRIDGE_FLAG_DISSOLVE = (1 << 0),
	/*! Move between bridging technologies as needed. */
	AST_BRIDGE_FLAG_SMART = (1 << 1),
};

/*! \brief Built in features */
enum ast_bridge_builtin_feature {
	/*! DTMF Based Blind Transfer */
	AST_BRIDGE_BUILTIN_BLINDTRANSFER = 0,
	/*! DTMF Based Attended Transfer */
	AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER,
	/*! DTMF Based Hangup Feature */
	AST_BRIDGE_BUILTIN_HANGUP,
	/*! End terminator for list of built in features. Must remain last. */
	AST_BRIDGE_BUILTIN_END,
};

struct ast_bridge;
struct ast_bridge_channel;

/*!
 * \brief Features hook callback type
 *
 * \param bridge The bridge that the channel is part of
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0 success
 * \retval -1 failure
 */
typedef int (*ast_bridge_features_hook_callback)(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt);

/*!
 * \brief Maximum length of a DTMF feature string
 */
#define MAXIMUM_DTMF_FEATURE_STRING 8

/*!
 * \brief Structure that is the essence of a features hook
 */
struct ast_bridge_features_hook {
	/*! DTMF String that is examined during a feature hook lookup */
	char dtmf[MAXIMUM_DTMF_FEATURE_STRING];
	/*! Callback that is called when DTMF string is matched */
	ast_bridge_features_hook_callback callback;
	/*! Unique data that was passed into us */
	void *hook_pvt;
	/*! Linked list information */
	AST_LIST_ENTRY(ast_bridge_features_hook) entry;
};

/*!
 * \brief Structure that contains features information
 */
struct ast_bridge_features {
	/*! Attached DTMF based feature hooks */
	AST_LIST_HEAD_NOLOCK(, ast_bridge_features_hook) hooks;
	/*! Feature flags that are enabled */
	struct ast_flags feature_flags;
	/*! Bit to indicate that this structure is useful and should be considered when looking for features */
	unsigned int usable:1;
	/*! Bit to indicate whether the channel/bridge is muted or not */
	unsigned int mute:1;
};

/*!
 * \brief Structure that contains configuration information for the blind transfer built in feature
 */
struct ast_bridge_features_blind_transfer {
	/*! Context to use for transfers */
	char context[AST_MAX_CONTEXT];
};

/*!
 * \brief Structure that contains configuration information for the attended transfer built in feature
 */
struct ast_bridge_features_attended_transfer {
	/*! DTMF string used to abort the transfer */
	char abort[MAXIMUM_DTMF_FEATURE_STRING];
	/*! DTMF string used to turn the transfer into a three way conference */
	char threeway[MAXIMUM_DTMF_FEATURE_STRING];
	/*! DTMF string used to complete the transfer */
	char complete[MAXIMUM_DTMF_FEATURE_STRING];
	/*! Context to use for transfers */
	char context[AST_MAX_CONTEXT];
};

/*! \brief Register a handler for a built in feature
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
int ast_bridge_features_register(enum ast_bridge_builtin_feature feature, ast_bridge_features_hook_callback callback, const char *dtmf);

/*! \brief Unregister a handler for a built in feature
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

/*! \brief Attach a custom hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param dtmf DTMF string to be activated upon
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_features_hook(&features, "#", pound_callback, NULL);
 * \endcode
 *
 * This makes the bridging core call pound_callback if a channel that has this
 * feature structure inputs the DTMF string '#'. A pointer to useful data may be
 * provided to the hook_pvt parameter.
 *
 * \note It is important that the callback set the bridge channel state back to
 *       AST_BRIDGE_CHANNEL_STATE_WAIT or the bridge thread will not service the channel.
 */
int ast_bridge_features_hook(struct ast_bridge_features *features, const char *dtmf, ast_bridge_features_hook_callback callback, void *hook_pvt);

/*! \brief Enable a built in feature on a bridge features structure
 *
 * \param features Bridge features structure
 * \param feature Feature to enable
 * \param dtmf Optionally the DTMF stream to trigger the feature, if not specified it will be the default
 * \param config Configuration structure unique to the built in type
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_features_enable(&features, AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER, NULL);
 * \endcode
 *
 * This enables the attended transfer DTMF option using the default DTMF string. An alternate
 * string may be provided using the dtmf parameter. Internally this is simply setting up a hook
 * to a built in feature callback function.
 */
int ast_bridge_features_enable(struct ast_bridge_features *features, enum ast_bridge_builtin_feature feature, const char *dtmf, void *config);

/*! \brief Set a flag on a bridge features structure
 *
 * \param features Bridge features structure
 * \param flag Flag to enable
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge_features features;
 * ast_bridge_features_init(&features);
 * ast_bridge_features_set_flag(&features, AST_BRIDGE_FLAG_DISSOLVE);
 * \endcode
 *
 * This sets the AST_BRIDGE_FLAG_DISSOLVE feature to be enabled on the features structure
 * 'features'.
 */
int ast_bridge_features_set_flag(struct ast_bridge_features *features, enum ast_bridge_feature_flags flag);

/*! \brief Initialize bridge features structure
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

/*! \brief Clean up the contents of a bridge features structure
 *
 * \param features Bridge features structure
 *
 * \retval 0 on success
 * \retval -1 on failure
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
int ast_bridge_features_cleanup(struct ast_bridge_features *features);

/*! \brief Play a DTMF stream into a bridge, optionally not to a given channel
 *
 * \param bridge Bridge to play stream into
 * \param dtmf DTMF to play
 * \param chan Channel to optionally not play to
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_dtmf_stream(bridge, "0123456789", NULL);
 * \endcode
 *
 * This sends the DTMF digits '0123456789' to all channels in the bridge pointed to
 * by the bridge pointer. Optionally a channel may be excluded by passing it's channel pointer
 * using the chan parameter.
 */
int ast_bridge_dtmf_stream(struct ast_bridge *bridge, const char *dtmf, struct ast_channel *chan);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BRIDGING_FEATURES_H */
