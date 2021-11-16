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

#ifndef _ASTERISK_STASIS_APP_RECORDING_H
#define _ASTERISK_STASIS_APP_RECORDING_H

/*! \file
 *
 * \brief Stasis Application Recording API. See \ref res_stasis "Stasis
 * Application API" for detailed documentation.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

#include "asterisk/app.h"
#include "asterisk/stasis_app.h"

/*! @{ */

/*! \brief Structure representing a recording stored on disk */
struct stasis_app_stored_recording;

/*!
 * \brief Returns the filename for this recording, for use with streamfile.
 *
 * The returned string will be valid until the \a recording object is freed.
 *
 * \param recording Recording to query.
 * \return Absolute path to the recording file, without the extension.
 * \retval NULL on error.
 */
const char *stasis_app_stored_recording_get_file(
	struct stasis_app_stored_recording *recording);

/*!
 * \brief Returns the full filename, with extension, for this recording.
 * \since 14.0.0
 *
 * \param recording Recording to query.
 *
 * \return Absolute path to the recording file, with the extension.
 * \retval NULL on error
 */
const char *stasis_app_stored_recording_get_filename(
	struct stasis_app_stored_recording *recording);

/*!
 * \brief Returns the extension for this recording.
 * \since 14.0.0
 *
 * \param recording Recording to query.
 *
 * \return The extension associated with this recording.
 * \retval NULL on error
 */
const char *stasis_app_stored_recording_get_extension(
	struct stasis_app_stored_recording *recording);

/*!
 * \brief Convert stored recording info to JSON.
 *
 * \param recording Recording to convert.
 * \return JSON representation.
 * \retval NULL on error.
 */
struct ast_json *stasis_app_stored_recording_to_json(
	struct stasis_app_stored_recording *recording);

/*!
 * \brief Find all stored recordings on disk.
 *
 * \return Container of \ref stasis_app_stored_recording objects.
 * \retval NULL on error.
 */
struct ao2_container *stasis_app_stored_recording_find_all(void);

/*!
 * \brief Creates a stored recording object, with the given name.
 *
 * \param name Name of the recording.
 * \return New recording object.
 * \retval NULL if recording is not found. \c errno is set to indicate why
 *	- \c ENOMEM - out of memeory
 *	- \c EACCES - file permissions (or recording is outside the config dir)
 *	- Any of the error codes for stat(), opendir(), readdir()
 */
struct stasis_app_stored_recording *stasis_app_stored_recording_find_by_name(
	const char *name);

/*!
 * \brief Copy a recording.
 *
 * \param src_recording The recording to copy
 * \param dst The destination of the recording to make
 * \param dst_recording If successful, the stored recording created as a result of the copy
 *
 * \retval 0 on success
 * \retval Non-zero on error
 */
int stasis_app_stored_recording_copy(struct stasis_app_stored_recording *src_recording, const char *dst,
	struct stasis_app_stored_recording **dst_recording);

/*!
 * \brief Delete a recording from disk.
 *
 * \param recording Recording to delete.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int stasis_app_stored_recording_delete(
	struct stasis_app_stored_recording *recording);

/*! @} */

/*! @{ */

/*! Opaque struct for handling the recording of media to a file. */
struct stasis_app_recording;

/*! State of a recording operation */
enum stasis_app_recording_state {
	/*! The recording has not started yet */
	STASIS_APP_RECORDING_STATE_QUEUED,
	/*! The media is currently recording */
	STASIS_APP_RECORDING_STATE_RECORDING,
	/*! The media is currently paused */
	STASIS_APP_RECORDING_STATE_PAUSED,
	/*! The media has stopped recording */
	STASIS_APP_RECORDING_STATE_COMPLETE,
	/*! The media has stopped recording, with error */
	STASIS_APP_RECORDING_STATE_FAILED,
	/*! The media has stopped recording, discard the recording file */
	STASIS_APP_RECORDING_STATE_CANCELED,
	/*! Sentinel */
	STASIS_APP_RECORDING_STATE_MAX,
};

/*! Valid operation for controlling a recording. */
enum stasis_app_recording_media_operation {
	/*! Stop the recording, deleting the media file(s) */
	STASIS_APP_RECORDING_CANCEL,
	/*! Stop the recording. */
	STASIS_APP_RECORDING_STOP,
	/*! Pause the recording */
	STASIS_APP_RECORDING_PAUSE,
	/*! Unpause the recording */
	STASIS_APP_RECORDING_UNPAUSE,
	/*! Mute the recording (record silence) */
	STASIS_APP_RECORDING_MUTE,
	/*! Unmute the recording */
	STASIS_APP_RECORDING_UNMUTE,
	/*! Sentinel */
	STASIS_APP_RECORDING_OPER_MAX,
};

#define STASIS_APP_RECORDING_TERMINATE_INVALID 0
#define STASIS_APP_RECORDING_TERMINATE_NONE -1
#define STASIS_APP_RECORDING_TERMINATE_ANY -2

struct stasis_app_recording_options {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);	/*!< name Name of the recording. */
		AST_STRING_FIELD(format);	/*!< Format to be recorded (wav, gsm, etc.) */
		AST_STRING_FIELD(target); /*!< URI of what is being recorded */
		);
	/*! Number of seconds of silence before ending the recording. */
	int max_silence_seconds;
	/*! Maximum recording duration. 0 for no maximum. */
	int max_duration_seconds;
	/*! Which DTMF to use to terminate the recording
	 *  \c STASIS_APP_RECORDING_TERMINATE_NONE to terminate only on hangup
	 *  \c STASIS_APP_RECORDING_TERMINATE_ANY to terminate on any DTMF
	 */
	char terminate_on;
	/*! How to handle recording when a file already exists */
	enum ast_record_if_exists if_exists;
	/*! If true, a beep is played at the start of recording */
	int beep:1;
};

/*!
 * \brief Allocate a recording options object.
 *
 * Clean up with ao2_cleanup().
 *
 * \param name Name of the recording.
 * \param format Format to record in.
 * \return Newly allocated options object.
 * \retval NULL on error.
 */
struct stasis_app_recording_options *stasis_app_recording_options_create(
	const char *name, const char *format);

/*!
 * \brief Parse a string into the recording termination enum.
 *
 * \param str String to parse.
 * \return DTMF value to terminate on.
 * \retval STASIS_APP_RECORDING_TERMINATE_NONE to not terminate on DTMF.
 * \retval STASIS_APP_RECORDING_TERMINATE_ANY to terminate on any DTMF.
 * \retval STASIS_APP_RECORDING_TERMINATE_INVALID if input was invalid.
 */
char stasis_app_recording_termination_parse(const char *str);

/*!
 * \brief Parse a string into the if_exists enum.
 *
 * \param str String to parse.
 * \return How to handle an existing file.
 * \return -1 on error.
 */
enum ast_record_if_exists stasis_app_recording_if_exists_parse(
	const char *str);

/*!
 * \brief Record media from a channel.
 *
 * A reference to the \a options object may be kept, so it MUST NOT be modified
 * after calling this function.
 *
 * On error, \c errno is set to indicate the failure reason.
 *  - \c EINVAL: Invalid input.
 *  - \c EEXIST: A recording with that name is in session.
 *  - \c ENOMEM: Out of memory.
 *
 * \param control Control for \c res_stasis.
 * \param options Recording options.
 * \return Recording control object.
 * \retval NULL on error.
 */
struct stasis_app_recording *stasis_app_control_record(
	struct stasis_app_control *control,
	struct stasis_app_recording_options *options);

/*!
 * \brief Gets the current state of a recording operation.
 *
 * \param recording Recording control object.
 * \return The state of the \a recording object.
 */
enum stasis_app_recording_state stasis_app_recording_get_state(
	struct stasis_app_recording *recording);

/*!
 * \brief Gets the unique name of a recording object.
 *
 * \param recording Recording control object.
 * \return \a recording's name.
 * \retval NULL if \a recording ic \c NULL
 */
const char *stasis_app_recording_get_name(
	struct stasis_app_recording *recording);

/*!
 * \brief Finds the recording object with the given name.
 *
 * \param name Name of the recording object to find.
 * \return Associated \ref stasis_app_recording object.
 * \retval NULL if \a name not found.
 */
struct stasis_app_recording *stasis_app_recording_find_by_name(const char *name);

/*!
 * \brief Construct a JSON model of a recording.
 *
 * \param recording Recording to conver.
 * \return JSON model.
 * \retval NULL on error.
 */
struct ast_json *stasis_app_recording_to_json(
	const struct stasis_app_recording *recording);

/*!
 * \brief Possible results from a recording operation.
 */
enum stasis_app_recording_oper_results {
	/*! Operation completed successfully. */
	STASIS_APP_RECORDING_OPER_OK,
	/*! Operation failed. */
	STASIS_APP_RECORDING_OPER_FAILED,
	/*! Operation failed b/c recording is not in session. */
	STASIS_APP_RECORDING_OPER_NOT_RECORDING,
};

/*!
 * \brief Controls the media for a given recording operation.
 *
 * \param recording Recording control object.
 * \param operation Media control operation.
 * \retval STASIS_APP_RECORDING_OPER_OK on success.
 * \return \ref stasis_app_recording_oper_results indicating failure.
 */
enum stasis_app_recording_oper_results stasis_app_recording_operation(
	struct stasis_app_recording *recording,
	enum stasis_app_recording_media_operation operation);

/*!
 * \brief Message type for recording updates. The data is an
 * \ref ast_channel_blob.
 */
struct stasis_message_type *stasis_app_recording_snapshot_type(void);

/*! @} */

#endif /* _ASTERISK_STASIS_APP_RECORDING_H */
