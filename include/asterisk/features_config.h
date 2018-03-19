/*
* Asterisk -- An open source telephony toolkit.
*
* Copyright (C) 2013, Digium, Inc.
*
* Mark Michelson <mmichelson@digium.com>
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

#ifndef _FEATURES_CONFIG_H
#define _FEATURES_CONFIG_H

#include "asterisk/stringfields.h"

struct ast_channel;

/*!
 * \brief General features configuration items
 */
struct ast_features_general_config {
	AST_DECLARE_STRING_FIELDS(
		/*! Sound played when automon or automixmon features are used */
		AST_STRING_FIELD(courtesytone);
		/*! Sound played when automon or automixmon features fail when used */
		AST_STRING_FIELD(recordingfailsound);
	);
	/*! Milliseconds allowed between digit presses when entering feature code */
	unsigned int featuredigittimeout;
};

/*!
 * \brief Get the general configuration options for a channel
 *
 * \note The channel should be locked before calling this function.
 * \note The returned value has its reference count incremented.
 *
 * If no channel is provided, then the global features configuration is returned.
 *
 * \param chan The channel to get configuration options for
 * \retval NULL Failed to get configuration
 * \retval non-NULL The general features configuration
 */
struct ast_features_general_config *ast_get_chan_features_general_config(struct ast_channel *chan);

/*!
 * \brief Feature configuration relating to transfers
 */
struct ast_features_xfer_config {
	AST_DECLARE_STRING_FIELDS (
		/*! Sound to play when transfer succeeds */
		AST_STRING_FIELD(xfersound);
		/*! Sound to play when transfer fails */
		AST_STRING_FIELD(xferfailsound);
		/*! DTMF sequence used to abort an attempted atxfer */
		AST_STRING_FIELD(atxferabort);
		/*! DTMF sequence used to complete an attempted atxfer */
		AST_STRING_FIELD(atxfercomplete);
		/*! DTMF sequence used to turn an attempted atxfer into a three-way call */
		AST_STRING_FIELD(atxferthreeway);
		/*! DTMF sequence used to swap which party the transferer is talking to */
		AST_STRING_FIELD(atxferswap);
		/*! Sound played when an invalid extension is dialed, and the transferer should retry. */
		AST_STRING_FIELD(transferretrysound);
		/*! Sound played when an invalid extension is dialed, and the transferer is being returned to the call. */
		AST_STRING_FIELD(transferinvalidsound);
	);
	/*! Seconds allowed between digit presses when dialing transfer destination */
	unsigned int transferdigittimeout;
	/*! Seconds to wait for the transfer target to answer a transferred call */
	unsigned int atxfernoanswertimeout;
	/*! Seconds to wait before attempting to re-dial the transfer target */
	unsigned int atxferloopdelay;
	/*! Number of times to re-attempt dialing the transfer target */
	unsigned int atxfercallbackretries;
	/*! Determines if the call is dropped on attended transfer failure */
	unsigned int atxferdropcall;
	/*! Number of dial attempts allowed for blind/attended transfers */
	unsigned int transferdialattempts;
};

/*!
 * \brief Get the transfer configuration options for a channel
 *
 * \note The channel should be locked before calling this function.
 * \note The returned value has its reference count incremented.
 *
 * If no channel is provided, then the global transfer configuration is returned.
 *
 * \param chan The channel to get configuration options for
 * \retval NULL Failed to get configuration
 * \retval non-NULL The transfer features configuration
 */
struct ast_features_xfer_config *ast_get_chan_features_xfer_config(struct ast_channel *chan);

/*!
 * \brief Get the transfer configuration option xferfailsound
 *
 * \note The channel should be locked before calling this function.
 * \note The returned value has to be freed.
 *
 * If no channel is provided, then option is pulled from the global
 * transfer configuration.
 *
 * \param chan The channel to get configuration options for
 * \retval NULL Failed to get configuration
 * \retval non-NULL The xferfailsound
 */
char *ast_get_chan_features_xferfailsound(struct ast_channel *chan);

/*!
 * \brief Get the transfer configuration option atxferabort
 *
 * \note The channel should be locked before calling this function.
 * \note The returned value has to be freed.
 *
 * If no channel is provided, then option is pulled from the global
 * transfer configuration.
 *
 * \param chan The channel to get configuration options for
 * \retval NULL Failed to get configuration
 * \retval non-NULL The atxferabort
 */
char *ast_get_chan_features_atxferabort(struct ast_channel *chan);

/*!
 * \brief Configuration relating to call pickup
 */
struct ast_features_pickup_config {
	AST_DECLARE_STRING_FIELDS (
		/*! Digit sequence to press to pick up a ringing call */
		AST_STRING_FIELD(pickupexten);
		/*! Sound to play to picker when pickup succeeds */
		AST_STRING_FIELD(pickupsound);
		/*! Sound to play to picker when pickup fails */
		AST_STRING_FIELD(pickupfailsound);
	);
};

/*!
 * \brief Get the pickup configuration options for a channel
 *
 * \note The channel should be locked before calling this function.
 * \note The returned value has its reference count incremented.
 *
 * If no channel is provided, then the global pickup configuration is returned.
 *
 * \param chan The channel to get configuration options for
 * \retval NULL Failed to get configuration
 * \retval non-NULL The pickup features configuration
 */
struct ast_features_pickup_config *ast_get_chan_features_pickup_config(struct ast_channel *chan);

/*!
 * \brief Configuration for the builtin features
 */
struct ast_featuremap_config {
	AST_DECLARE_STRING_FIELDS (
		/*! Blind transfer DTMF code */
		AST_STRING_FIELD(blindxfer);
		/*! Disconnect DTMF code */
		AST_STRING_FIELD(disconnect);
		/*! Automon DTMF code */
		AST_STRING_FIELD(automon);
		/*! Attended Transfer DTMF code */
		AST_STRING_FIELD(atxfer);
		/*! One-touch parking DTMF code */
		AST_STRING_FIELD(parkcall);
		/*! Automixmon DTMF code */
		AST_STRING_FIELD(automixmon);
	);
};

/*!
 * \brief Get the featuremap configuration options for a channel
 *
 * \note The channel should be locked before calling this function.
 * \note The returned value has its reference count incremented.
 *
 * If no channel is provided, then the global featuremap configuration is returned.
 *
 * \param chan The channel to get configuration options for
 * \retval NULL Failed to get configuration
 * \retval non-NULL The pickup features configuration
 */
struct ast_featuremap_config *ast_get_chan_featuremap_config(struct ast_channel *chan);

/*!
 * \brief Get the DTMF code for a builtin feature
 *
 * \note The channel should be locked before calling this function
 *
 * If no channel is provided, then the global setting for the option is returned.
 *
 * \param chan The channel to get the option from
 * \param feature The short name of the feature (as it appears in features.conf)
 * \param[out] buf The buffer to write the DTMF value into
 * \param size The size of the buffer in bytes
 * \retval 0 Success
 * \retval non-zero Unrecognized builtin feature name
 */
int ast_get_builtin_feature(struct ast_channel *chan, const char *feature, char *buf, size_t len);

/*!
 * \brief Get the DTMF code for a call feature
 *
 * \note The channel should be locked before calling this function
 *
 * If no channel is provided, then the global setting for the option is returned.
 *
 * This function is like \ref ast_get_builtin_feature except that it will
 * also check the applicationmap in addition to the builtin features.
 *
 * \param chan The channel to get the option from
 * \param feature The short name of the feature
 * \param[out] buf The buffer to write the DTMF value into
 * \param size The size of the buffer in bytes
 * \retval 0 Success
 * \retval non-zero Unrecognized feature name
 */
int ast_get_feature(struct ast_channel *chan, const char *feature, char *buf, size_t len);

#define AST_FEATURE_MAX_LEN 11

/*!
 * \brief An applicationmap configuration item
 */
struct ast_applicationmap_item {
	AST_DECLARE_STRING_FIELDS (
		/* Name of the item */
		AST_STRING_FIELD(name);
		/* Name Dialplan application that is invoked by the feature */
		AST_STRING_FIELD(app);
		/* Data to pass to the application */
		AST_STRING_FIELD(app_data);
		/* Music-on-hold class to play to party on which feature is not activated */
		AST_STRING_FIELD(moh_class);
	);
	/* DTMF key sequence used to activate the feature */
	char dtmf[AST_FEATURE_MAX_LEN];
	/* If true, activate on party that input the sequence, otherwise activate on the other party */
	unsigned int activate_on_self;
};

/*!
 * \brief Get the applicationmap for a given channel.
 *
 * \note The channel should be locked before calling this function.
 *
 * This uses the value of the DYNAMIC_FEATURES channel variable to build a
 * custom applicationmap for this channel. The returned container has
 * applicationmap_items inside.
 *
 * \param chan The channel for which applicationmap is being retrieved.
 * \retval NULL An error occurred or the channel has no dynamic features.
 * \retval non-NULL A container of applicationmap_items pertaining to the channel.
 */
struct ao2_container *ast_get_chan_applicationmap(struct ast_channel *chan);

#endif /* _FEATURES_CONFIG_H */
