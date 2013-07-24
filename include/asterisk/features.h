/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Call Parking and Pickup API
 * Includes code and algorithms from the Zapata library.
 */

#ifndef _AST_FEATURES_H
#define _AST_FEATURES_H

#include "asterisk/pbx.h"
#include "asterisk/linkedlists.h"
#include "asterisk/bridging.h"

#define FEATURE_MAX_LEN		11
#define FEATURE_APP_LEN		64
#define FEATURE_APP_ARGS_LEN	256
#define FEATURE_SNAME_LEN	32
#define FEATURE_EXTEN_LEN	32
#define FEATURE_MOH_LEN		80  /* same as MAX_MUSICCLASS from channel.h */

#define DEFAULT_PARKINGLOT "default"	/*!< Default parking lot */

#define AST_FEATURE_RETURN_HANGUP           -1
#define AST_FEATURE_RETURN_SUCCESSBREAK     0
#define AST_FEATURE_RETURN_PBX_KEEPALIVE    AST_PBX_KEEPALIVE
#define AST_FEATURE_RETURN_NO_HANGUP_PEER   AST_PBX_NO_HANGUP_PEER
#define AST_FEATURE_RETURN_PASSDIGITS       21
#define AST_FEATURE_RETURN_STOREDIGITS      22
#define AST_FEATURE_RETURN_SUCCESS          23
#define AST_FEATURE_RETURN_KEEPTRYING       24
#define AST_FEATURE_RETURN_PARKFAILED       25

#define FEATURE_SENSE_CHAN	(1 << 0)
#define FEATURE_SENSE_PEER	(1 << 1)

typedef int (*ast_feature_operation)(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, const char *code, int sense, void *data);

/*! \brief main call feature structure */

enum {
	AST_FEATURE_FLAG_NEEDSDTMF = (1 << 0),
	AST_FEATURE_FLAG_ONPEER =    (1 << 1),
	AST_FEATURE_FLAG_ONSELF =    (1 << 2),
	AST_FEATURE_FLAG_BYCALLEE =  (1 << 3),
	AST_FEATURE_FLAG_BYCALLER =  (1 << 4),
	AST_FEATURE_FLAG_BYBOTH	 =   (3 << 3),
};

/*!
 * \brief Park a call and read back parked location
 *
 * \param park_me Channel to be parked.
 * \param parker Channel parking the call.
 * \param timeout is a timeout in milliseconds
 * \param park_exten Parking lot access extension (Not used)
 * \param extout is a parameter to an int that will hold the parked location, or NULL if you want.
 *
 * \details
 * Park the park_me channel, and read back the parked location
 * to the parker channel.  If the call is not picked up within a
 * specified period of time, then the call will return to the
 * last step that it was in (in terms of exten, priority and
 * context).
 *
 * \note Use ast_park_call_exten() instead.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_park_call(struct ast_channel *park_me, struct ast_channel *parker, int timeout, const char *park_exten, int *extout);

/*!
 * \brief Park a call and read back parked location
 * \since 1.8.9
 *
 * \param park_me Channel to be parked.
 * \param parker Channel parking the call.
 * \param park_exten Parking lot access extension
 * \param park_context Parking lot context
 * \param timeout is a timeout in milliseconds
 * \param extout is a parameter to an int that will hold the parked location, or NULL if you want.
 *
 * \details
 * Park the park_me channel, and read back the parked location
 * to the parker channel.  If the call is not picked up within a
 * specified period of time, then the call will return to the
 * last step that it was in (in terms of exten, priority and
 * context).
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_park_call_exten(struct ast_channel *park_me, struct ast_channel *parker, const char *park_exten, const char *park_context, int timeout, int *extout);

/*!
 * \brief Park a call via a masqueraded channel
 *
 * \param park_me Channel to be parked.
 * \param parker Channel parking the call.
 * \param timeout is a timeout in milliseconds
 * \param extout is a parameter to an int that will hold the parked location, or NULL if you want.
 *
 * \details
 * Masquerade the park_me channel into a new, empty channel which is then parked.
 *
 * \note Use ast_masq_park_call_exten() instead.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_masq_park_call(struct ast_channel *park_me, struct ast_channel *parker, int timeout, int *extout);

/*!
 * \brief Park a call via a masqueraded channel
 * \since 1.8.9
 *
 * \param park_me Channel to be parked.
 * \param parker Channel parking the call.
 * \param park_exten Parking lot access extension
 * \param park_context Parking lot context
 * \param timeout is a timeout in milliseconds
 * \param extout is a parameter to an int that will hold the parked location, or NULL if you want.
 *
 * \details
 * Masquerade the park_me channel into a new, empty channel which is then parked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_masq_park_call_exten(struct ast_channel *park_me, struct ast_channel *parker, const char *park_exten, const char *park_context, int timeout, int *extout);

/*!
 * \brief Determine if parking extension exists in a given context
 * \retval 0 if extension does not exist
 * \retval 1 if extension does exist
*/
int ast_parking_ext_valid(const char *exten_str, struct ast_channel *chan, const char *context);

/*! \brief Bridge a call, optionally allowing redirection */
int ast_bridge_call(struct ast_channel *chan, struct ast_channel *peer,struct ast_bridge_config *config);

/*!
 * \brief Add an arbitrary channel to a bridge
 * \since 12.0.0
 *
 * The channel that is being added to the bridge can be in any state: unbridged,
 * bridged, answered, unanswered, etc. The channel will be added asynchronously,
 * meaning that when this function returns once the channel has been added to
 * the bridge, not once the channel has been removed from the bridge.
 *
 * In addition, a tone can optionally be played to the channel once the
 * channel is placed into the bridge.
 *
 * \note When this function returns, there is no guarantee that the channel that
 * was passed in is valid any longer. Do not attempt to operate on the channel
 * after this function returns.
 *
 * \param bridge Bridge to which the channel should be added
 * \param chan The channel to add to the bridge
 * \param features Features for this channel in the bridge
 * \param play_tone Indicates if a tone should be played to the channel
 * \param xfersound Sound that should be used to indicate transfer with play_tone
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_bridge_add_channel(struct ast_bridge *bridge, struct ast_channel *chan,
		struct ast_bridge_features *features, int play_tone, const char *xfersound);

/*!
 * \brief Test if a channel can be picked up.
 *
 * \param chan Channel to test if can be picked up.
 *
 * \note This function assumes that chan is locked.
 *
 * \return TRUE if channel can be picked up.
 */
int ast_can_pickup(struct ast_channel *chan);

/*!
 * \brief Find a pickup channel target by group.
 *
 * \param chan channel that initiated pickup.
 *
 * \retval target on success.  The returned channel is locked and reffed.
 * \retval NULL on error.
 */
struct ast_channel *ast_pickup_find_by_group(struct ast_channel *chan);

/*! \brief Pickup a call */
int ast_pickup_call(struct ast_channel *chan);

/*!
 * \brief Pickup a call target.
 *
 * \param chan channel that initiated pickup.
 * \param target channel to be picked up.
 *
 * \note This function assumes that target is locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_do_pickup(struct ast_channel *chan, struct ast_channel *target);

/*!
 * \brief accessor for call pickup message type
 * \since 12.0.0
 *
 * \retval pointer to the stasis message type
 * \retval NULL if not initialized
 */
struct stasis_message_type *ast_call_pickup_type(void);

/*! \brief Reload call features from features.conf */
int ast_features_reload(void);

/*!
 * \brief parse L option and read associated channel variables to set warning, warning frequency, and timelimit
 * \note caller must be aware of freeing memory for warning_sound, end_sound, and start_sound
*/
int ast_bridge_timelimit(struct ast_channel *chan, struct ast_bridge_config *config, char *parse, struct timeval *calldurationlimit);

#endif /* _AST_FEATURES_H */
