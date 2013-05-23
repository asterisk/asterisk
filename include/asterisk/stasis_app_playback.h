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
	/*! The media has stopped playing */
	STASIS_PLAYBACK_STATE_COMPLETE,
};

enum stasis_app_playback_media_control {
	STASIS_PLAYBACK_STOP,
	STASIS_PLAYBACK_PAUSE,
	STASIS_PLAYBACK_PLAY,
	STASIS_PLAYBACK_REWIND,
	STASIS_PLAYBACK_FAST_FORWARD,
	STASIS_PLAYBACK_SPEED_UP,
	STASIS_PLAYBACK_SLOW_DOWN,
};

/*!
 * \brief Play a file to the control's channel.
 *
 * Note that the file isn't the full path to the file. Asterisk's internal
 * playback mechanism will automagically select the best format based on the
 * available codecs for the channel.
 *
 * \param control Control for \c res_stasis.
 * \param file Base filename for the file to play.
 * \return Playback control object.
 * \return \c NULL on error.
 */
struct stasis_app_playback *stasis_app_control_play_uri(
	struct stasis_app_control *control, const char *file,
	const char *language);

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
 * \return \c NULL if \a playback ic \c NULL
 */
const char *stasis_app_playback_get_id(
	struct stasis_app_playback *playback);

/*!
 * \brief Finds the playback object with the given id.
 *
 * \param id Id of the playback object to find.
 * \return Associated \ref stasis_app_playback object.
 * \return \c NULL if \a id not found.
 */
struct ast_json *stasis_app_playback_find_by_id(const char *id);

/*!
 * \brief Controls the media for a given playback operation.
 *
 * \param playback Playback control object.
 * \param control Media control operation.
 * \return 0 on success
 * \return non-zero on error.
 */
int stasis_app_playback_control(struct stasis_app_playback *playback,
	enum stasis_app_playback_media_control control);

/*!
 * \brief Message type for playback updates. The data is an
 * \ref ast_channel_blob.
 */
struct stasis_message_type *stasis_app_playback_snapshot_type(void);

#endif /* _ASTERISK_STASIS_APP_PLAYBACK_H */
