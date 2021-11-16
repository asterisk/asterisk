/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#ifndef _ASTERISK_STASIS_APP_PLAYBACK_H
#define _ASTERISK_STASIS_APP_PLAYBACK_H

/*! \file
 *
 * \brief Stasis Application Playback API. See \ref res_stasis "Stasis
 * Application API" for detailed documentation.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

#include "asterisk/stasis_app.h"

/*! Opaque struct for handling the playback of a single file */
struct stasis_app_playback;

/*! State of a playback operation */
enum stasis_app_playback_state {
	/*! The playback has not started yet */
	STASIS_PLAYBACK_STATE_QUEUED,
	/*! The media is currently playing */
	STASIS_PLAYBACK_STATE_PLAYING,
	/*! The media is currently playing */
	STASIS_PLAYBACK_STATE_PAUSED,
	/*! The media is transitioning to the next in the list */
	STASIS_PLAYBACK_STATE_CONTINUING,
	/*! The media has stopped playing */
	STASIS_PLAYBACK_STATE_COMPLETE,
	/*! The media has stopped because of an error playing the file */
	STASIS_PLAYBACK_STATE_FAILED,
	/*! The playback was canceled. */
	STASIS_PLAYBACK_STATE_CANCELED,
	/*! The playback was stopped. */
	STASIS_PLAYBACK_STATE_STOPPED,
	/*! Enum end sentinel. */
	STASIS_PLAYBACK_STATE_MAX,
};

/*! Valid operation for controlling a playback. */
enum stasis_app_playback_media_operation {
	/*! Stop the playback operation. */
	STASIS_PLAYBACK_STOP,
	/*! Restart the media from the beginning. */
	STASIS_PLAYBACK_RESTART,
	/*! Pause playback. */
	STASIS_PLAYBACK_PAUSE,
	/*! Resume paused playback. */
	STASIS_PLAYBACK_UNPAUSE,
	/*! Rewind playback. */
	STASIS_PLAYBACK_REVERSE,
	/*! Fast forward playback. */
	STASIS_PLAYBACK_FORWARD,
	/*! Enum end sentinel. */
	STASIS_PLAYBACK_MEDIA_OP_MAX,
};

enum stasis_app_playback_target_type {
	/*! The target is a channel */
	STASIS_PLAYBACK_TARGET_CHANNEL = 0,
	/*! The target is a bridge */
	STASIS_PLAYBACK_TARGET_BRIDGE,
};

/*!
 * \brief Play a file to the control's channel.
 *
 * Note that the file isn't the full path to the file. Asterisk's internal
 * playback mechanism will automagically select the best format based on the
 * available codecs for the channel.
 *
 * \param control Control for \c res_stasis.
 * \param media Array of const char * media files to play.
 * \param media_count The number of media files in \c media.
 * \param language Selects the file based on language.
 * \param target_id ID of the target bridge or channel.
 * \param target_type What the target type is
 * \param skipms Number of milliseconds to skip for forward/reverse operations.
 * \param offsetms Number of milliseconds to skip before playing.
 * \param id ID to assign the new playback or NULL for default.
 * \return Playback control object.
 * \retval NULL on error.
 */
struct stasis_app_playback *stasis_app_control_play_uri(
	struct stasis_app_control *control, const char **media,
	size_t media_count, const char *language, const char *target_id,
	enum stasis_app_playback_target_type target_type,
	int skipms, long offsetms, const char *id);

/*!
 * \brief Gets the current state of a playback operation.
 *
 * \param playback Playback control object.
 * \return The state of the \a playback object.
 */
enum stasis_app_playback_state stasis_app_playback_get_state(
	struct stasis_app_playback *playback);

/*!
 * \brief Gets the unique id of a playback object.
 *
 * \param playback Playback control object.
 * \return \a playback's id.
 * \retval NULL if \a playback ic \c NULL
 */
const char *stasis_app_playback_get_id(
	struct stasis_app_playback *playback);

/*!
 * \brief Finds the playback object with the given id.
 *
 * \param id Id of the playback object to find.
 * \return Associated \ref stasis_app_playback object.
 * \retval NULL if \a id not found.
 */
struct stasis_app_playback *stasis_app_playback_find_by_id(const char *id);

/*!
 * \brief Convert a playback to its JSON representation
 *
 * \param playback The playback object to convert to JSON
 *
 * \retval NULL on error
 * \return A JSON object on success
 */
struct ast_json *stasis_app_playback_to_json(
	const struct stasis_app_playback *playback);

enum stasis_playback_oper_results {
	STASIS_PLAYBACK_OPER_OK,
	STASIS_PLAYBACK_OPER_FAILED,
	STASIS_PLAYBACK_OPER_NOT_PLAYING,
};
/*!
 * \brief Controls the media for a given playback operation.
 *
 * \param playback Playback control object.
 * \param operation Media control operation.
 * \retval STASIS_PLAYBACK_OPER_OK on success.
 * \return \ref stasis_playback_oper_results indicating failure.
 */
enum stasis_playback_oper_results stasis_app_playback_operation(
	struct stasis_app_playback *playback,
	enum stasis_app_playback_media_operation operation);

/*!
 * \brief Message type for playback updates. The data is an
 * \ref ast_channel_blob.
 */
struct stasis_message_type *stasis_app_playback_snapshot_type(void);

#endif /* _ASTERISK_STASIS_APP_PLAYBACK_H */
