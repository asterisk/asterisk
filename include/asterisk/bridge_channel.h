/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 * Richard Mudgett <rmudgett@digium.com>
 * Matt Jordan <mjordan@digium.com>
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
 * \page AstBridgeChannel Bridging Channel API
 *
 * An API that act on a channel in a bridge. Note that while the
 * \ref ast_bridge_channel is owned by a channel, it should only be used
 * by members of the bridging system. The only places where this API should
 * be used is:
 *  \arg \ref AstBridging API itself
 *  \arg Bridge mixing technologies
 *  \arg Bridge sub-classes
 *
 * In general, anywhere else it is unsafe to use this API. Care should be
 * taken when using this API to ensure that the locking order remains
 * correct. The locking order must be:
 *  \arg The \ref \c ast_bridge
 *  \arg The \ref \c ast_bridge_channel
 *  \arg The \ref \c ast_channel
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author Richard Mudgett <rmudgett@digium.com>
 * \author Matt Jordan <mjordan@digium.com>
 *
 * See Also:
 * \arg \ref AstBridging
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_BRIDGING_CHANNEL_H
#define _ASTERISK_BRIDGING_CHANNEL_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/bridge_features.h"
#include "asterisk/bridge_technology.h"

/*! \brief State information about a bridged channel */
enum bridge_channel_state {
	/*! Waiting for a signal (Channel in the bridge) */
	BRIDGE_CHANNEL_STATE_WAIT = 0,
	/*! Bridged channel was forced out and should be hung up (Bridge may dissolve.) */
	BRIDGE_CHANNEL_STATE_END,
	/*! Bridged channel was forced out. Don't dissolve the bridge regardless */
	BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE,
};

enum bridge_channel_thread_state {
	/*! Bridge channel thread is idle/waiting. */
	BRIDGE_CHANNEL_THREAD_IDLE,
	/*! Bridge channel thread is writing a normal/simple frame. */
	BRIDGE_CHANNEL_THREAD_SIMPLE,
	/*! Bridge channel thread is processing a frame. */
	BRIDGE_CHANNEL_THREAD_FRAME,
};

struct ast_bridge;
struct ast_bridge_tech_optimizations;

 /*!
 * \brief Structure that contains information regarding a channel in a bridge
 */
struct ast_bridge_channel {
/* XXX ASTERISK-21271 cond is only here because of external party suspend/unsuspend support. */
	/*! Condition, used if we want to wake up a thread waiting on the bridged channel */
	ast_cond_t cond;
	/*! Current bridged channel state */
	enum bridge_channel_state state;
	/*! Asterisk channel participating in the bridge */
	struct ast_channel *chan;
	/*! Asterisk channel we are swapping with (if swapping) */
	struct ast_channel *swap;
	/*!
	 * \brief Bridge this channel is participating in
	 *
	 * \note The bridge pointer cannot change while the bridge or
	 * bridge_channel is locked.
	 */
	struct ast_bridge *bridge;
	/*!
	 * \brief Bridge class private channel data.
	 *
	 * \note This information is added when the channel is pushed
	 * into the bridge and removed when it is pulled from the
	 * bridge.
	 */
	void *bridge_pvt;
	/*!
	 * \brief Private information unique to the bridge technology.
	 *
	 * \note This information is added when the channel joins the
	 * bridge's technology and removed when it leaves the bridge's
	 * technology.
	 */
	void *tech_pvt;
	/*! Thread handling the bridged channel (Needed by ast_bridge_depart) */
	pthread_t thread;
	/* v-- These flags change while the bridge is locked or before the channel is in the bridge. */
	/*! TRUE if the channel is in a bridge. */
	unsigned int in_bridge:1;
	/*! TRUE if the channel just joined the bridge. */
	unsigned int just_joined:1;
	/*! TRUE if the channel is suspended from the bridge. */
	unsigned int suspended:1;
	/*! TRUE if the COLP update on initial join is inhibited. */
	unsigned int inhibit_colp:1;
	/*! TRUE if the channel must wait for an ast_bridge_depart to reclaim the channel. */
	unsigned int depart_wait:1;
	/* ^-- These flags change while the bridge is locked or before the channel is in the bridge. */
	/*! Features structure for features that are specific to this channel */
	struct ast_bridge_features *features;
	/*! Technology optimization parameters used by bridging technologies capable of
	 *  optimizing based upon talk detection. */
	struct ast_bridge_tech_optimizations tech_args;
	/*! Copy of read format used by chan before join */
	struct ast_format *read_format;
	/*! Copy of write format used by chan before join */
	struct ast_format *write_format;
	/*! Call ID associated with bridge channel */
	ast_callid callid;
	/*! A clone of the roles living on chan when the bridge channel joins the bridge. This may require some opacification */
	struct bridge_roles_datastore *bridge_roles;
	/*! Linked list information */
	AST_LIST_ENTRY(ast_bridge_channel) entry;
	/*! Queue of outgoing frames to the channel. */
	AST_LIST_HEAD_NOLOCK(, ast_frame) wr_queue;
	/*! Queue of deferred frames, queued onto channel when other party joins. */
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_queue;
	/*! Pipe to alert thread when frames are put into the wr_queue. */
	int alert_pipe[2];
	/*!
	 * \brief The bridge channel thread activity.
	 *
	 * \details Used by local channel optimization to determine if
	 * the thread is in an acceptable state to optimize.
	 *
	 * \note Needs to be atomically settable.
	 */
	enum bridge_channel_thread_state activity;
	/*! Owed events to the bridge. */
	struct {
		/*! Time started sending the current digit. (Invalid if owed.dtmf_digit is zero.) */
		struct timeval dtmf_tv;
		/*! Digit currently sending into the bridge. (zero if not sending) */
		char dtmf_digit;
		/*! Non-zero if a T.38 session terminate is owed to the bridge. */
		char t38_terminate;
	} owed;
	/*! DTMF hook sequence state */
	struct {
		/*! Time at which the DTMF hooks should stop waiting for more digits to come. */
		struct timeval interdigit_timeout;
		/*! Collected DTMF digits for DTMF hooks. */
		char collected[MAXIMUM_DTMF_FEATURE_STRING];
	} dtmf_hook_state;
	union {
		uint32_t raw;
		struct {
			/*! TRUE if binaural is suspended. */
			unsigned int binaural_suspended:1;
			/*! TRUE if a change of binaural positions has to be performed. */
			unsigned int binaural_pos_change:1;
			/*! Padding */
			unsigned int padding:30;
		};
	};
	struct {
		/*! An index mapping of where a channel's media needs to be routed */
		struct ast_vector_int to_bridge;
		/*! An index mapping of where a bridge's media needs to be routed */
		struct ast_vector_int to_channel;
	} stream_map;
};

/*!
 * \brief Try locking the bridge_channel.
 *
 * \param bridge_channel What to try locking
 *
 * \retval 0 on success.
 * \retval non-zero on error.
 */
#define ast_bridge_channel_trylock(bridge_channel)	_ast_bridge_channel_trylock(bridge_channel, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge_channel)
static inline int _ast_bridge_channel_trylock(struct ast_bridge_channel *bridge_channel, const char *file, const char *function, int line, const char *var)
{
	return __ao2_trylock(bridge_channel, AO2_LOCK_REQ_MUTEX, file, function, line, var);
}

/*!
 * \brief Lock the bridge_channel.
 *
 * \param bridge_channel What to lock
 *
 * \return Nothing
 */
#define ast_bridge_channel_lock(bridge_channel)	_ast_bridge_channel_lock(bridge_channel, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge_channel)
static inline void _ast_bridge_channel_lock(struct ast_bridge_channel *bridge_channel, const char *file, const char *function, int line, const char *var)
{
	__ao2_lock(bridge_channel, AO2_LOCK_REQ_MUTEX, file, function, line, var);
}

/*!
 * \brief Unlock the bridge_channel.
 *
 * \param bridge_channel What to unlock
 *
 * \return Nothing
 */
#define ast_bridge_channel_unlock(bridge_channel)	_ast_bridge_channel_unlock(bridge_channel, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge_channel)
static inline void _ast_bridge_channel_unlock(struct ast_bridge_channel *bridge_channel, const char *file, const char *function, int line, const char *var)
{
	__ao2_unlock(bridge_channel, file, function, line, var);
}

/*!
 * \brief Lock the bridge associated with the bridge channel.
 * \since 12.0.0
 *
 * \param bridge_channel Channel that wants to lock the bridge.
 *
 * \details
 * This is an upstream lock operation.  The defined locking
 * order is bridge then bridge_channel.
 *
 * \note On entry, neither the bridge nor bridge_channel is locked.
 *
 * \note The bridge_channel->bridge pointer changes because of a
 * bridge-merge/channel-move operation between bridges.
 *
 * \return Nothing
 */
void ast_bridge_channel_lock_bridge(struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Lets the bridging indicate when a bridge channel has stopped or started talking.
 *
 * \note All DSP functionality on the bridge has been pushed down to the lowest possible
 * layer, which in this case is the specific bridging technology being used. Since it
 * is necessary for the knowledge of which channels are talking to make its way up to the
 * application, this function has been created to allow the bridging technology to communicate
 * that information with the bridging core.
 *
 * \param bridge_channel The bridge channel that has either started or stopped talking.
 * \param started_talking set to 1 when this indicates the channel has started talking set to 0
 * when this indicates the channel has stopped talking.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_notify_talking(struct ast_bridge_channel *bridge_channel, int started_talking);

/*!
 * \brief Set bridge channel state to leave bridge (if not leaving already).
 *
 * \param bridge_channel Channel to change the state on
 * \param new_state The new state to place the channel into
 * \param cause Cause of channel leaving bridge.
 *   If cause <= 0 then use cause on channel if cause still <= 0 use AST_CAUSE_NORMAL_CLEARING.
 *
 * Example usage:
 *
 * \code
 * ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END, AST_CAUSE_NORMAL_CLEARING);
 * \endcode
 *
 * This places the channel pointed to by bridge_channel into the
 * state BRIDGE_CHANNEL_STATE_END if it was
 * BRIDGE_CHANNEL_STATE_WAIT before.
 */
void ast_bridge_channel_leave_bridge(struct ast_bridge_channel *bridge_channel, enum bridge_channel_state new_state, int cause);

/*!
 * \brief Set bridge channel state to leave bridge (if not leaving already).
 *
 * \param bridge_channel Channel to change the state on
 * \param new_state The new state to place the channel into
 * \param cause Cause of channel leaving bridge.
 *   If cause <= 0 then use cause on channel if cause still <= 0 use AST_CAUSE_NORMAL_CLEARING.
 *
 * Example usage:
 *
 * \code
 * ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END, AST_CAUSE_NORMAL_CLEARING);
 * \endcode
 *
 * This places the channel pointed to by bridge_channel into the
 * state BRIDGE_CHANNEL_STATE_END if it was
 * BRIDGE_CHANNEL_STATE_WAIT before.
 */
void ast_bridge_channel_leave_bridge_nolock(struct ast_bridge_channel *bridge_channel, enum bridge_channel_state new_state, int cause);

/*!
 * \brief Get the peer bridge channel of a two party bridge.
 * \since 12.0.0
 *
 * \param bridge_channel What to get the peer of.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \note This is an internal bridge function.
 *
 * \retval peer on success.
 * \retval NULL no peer channel.
 */
struct ast_bridge_channel *ast_bridge_channel_peer(struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Restore the formats of a bridge channel's channel to how they were before bridge_channel_internal_join
 * \since 12.0.0
 *
 * \param bridge_channel Channel to restore
 */
void ast_bridge_channel_restore_formats(struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Adjust the bridge_channel's bridge merge inhibit request count.
 * \since 12.0.0
 *
 * \param bridge_channel What to operate on.
 * \param request Inhibit request increment.
 *     (Positive to add requests.  Negative to remove requests.)
 *
 * \note This API call is meant for internal bridging operations.
 *
 * \retval bridge adjusted merge inhibit with reference count.
 */
struct ast_bridge *ast_bridge_channel_merge_inhibit(struct ast_bridge_channel *bridge_channel, int request);

/*!
 * \internal
 * \brief Update the linkedids for all channels in a bridge
 * \since 12.0.0
 *
 * \param bridge_channel The channel joining the bridge
 * \param swap The channel being swapped out of the bridge. May be NULL.
 *
 * \note The bridge must be locked prior to calling this function.
 * \note This should be called during a \ref bridge_channel_internal_push
 * operation, typically by a sub-class of a bridge.
 */
void ast_bridge_channel_update_linkedids(struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap);

/*!
 * \internal
 * \brief Update the accountcodes for channels joining/leaving a bridge
 * \since 12.0.0
 *
 * This function updates the accountcode and peeraccount on channels in two-party
 * bridges. In multi-party bridges, peeraccount is not set - it doesn't make much sense -
 * however accountcode propagation will still occur if the channel joining has an
 * accountcode.
 *
 * \param joining The channel joining the bridge.  May be NULL.
 * \param leaving The channel leaving or being swapped out of the bridge. May be NULL.
 *
 * \note The joining and leaving parameters cannot both be NULL.
 *
 * \note The bridge must be locked prior to calling this function.
 * \note This should be called during a \ref bridge_channel_internal_push
 * or \ref bridge_channel_internal_pull operation, typically by a
 * sub-class of a bridge.
 */
void ast_bridge_channel_update_accountcodes(struct ast_bridge_channel *joining, struct ast_bridge_channel *leaving);

/*!
 * \brief Write a frame to the specified bridge_channel.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to queue the frame.
 * \param fr Frame to write.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_queue_frame(struct ast_bridge_channel *bridge_channel, struct ast_frame *fr);

/*!
 * \brief Queue a control frame onto the bridge channel with data.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to queue the frame onto.
 * \param control Type of control frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_queue_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen);

/*!
 * \brief Write a control frame into the bridge with data.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is putting the frame into the bridge.
 * \param control Type of control frame.
 * \param data Frame payload data to pass.
 * \param datalen Frame payload data length to pass.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_write_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen);

/*!
 * \brief Write a hold frame into the bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is putting the hold into the bridge.
 * \param moh_class The suggested music class for the other end to use.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_write_hold(struct ast_bridge_channel *bridge_channel, const char *moh_class);

/*!
 * \brief Write an unhold frame into the bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is putting the hold into the bridge.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_write_unhold(struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Run an application on the bridge channel.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to run the application on.
 * \param app_name Dialplan application name.
 * \param app_args Arguments for the application. (NULL tolerant)
 * \param moh_class MOH class to request bridge peers to hear while application is running.
 *     NULL if no MOH.
 *     Empty if default MOH class.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \return Nothing
 */
void ast_bridge_channel_run_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class);

/*!
 * \brief Write a bridge action run application frame into the bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is putting the frame into the bridge
 * \param app_name Dialplan application name.
 * \param app_args Arguments for the application. (NULL or empty for no arguments)
 * \param moh_class MOH class to request bridge peers to hear while application is running.
 *     NULL if no MOH.
 *     Empty if default MOH class.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_write_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class);

/*!
 * \brief Queue a bridge action run application frame onto the bridge channel.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to put the frame onto
 * \param app_name Dialplan application name.
 * \param app_args Arguments for the application. (NULL or empty for no arguments)
 * \param moh_class MOH class to request bridge peers to hear while application is running.
 *     NULL if no MOH.
 *     Empty if default MOH class.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_queue_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class);

/*!
 * \brief Custom interpretation of the playfile name.
 *
 * \param bridge_channel Which channel to play the file on
 * \param playfile Sound filename to play.
 *
 * \return Nothing
 */
typedef void (*ast_bridge_custom_play_fn)(struct ast_bridge_channel *bridge_channel, const char *playfile);

/*!
 * \brief Play a file on the bridge channel.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to play the file on
 * \param custom_play Call this function to play the playfile. (NULL if normal sound file to play)
 * \param playfile Sound filename to play.
 * \param moh_class MOH class to request bridge peers to hear while file is played.
 *     NULL if no MOH.
 *     Empty if default MOH class.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \return Nothing
 */
void ast_bridge_channel_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class);

/*!
 * \brief Write a bridge action play file frame into the bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is putting the frame into the bridge
 * \param custom_play Call this function to play the playfile. (NULL if normal sound file to play)
 * \param playfile Sound filename to play.
 * \param moh_class MOH class to request bridge peers to hear while file is played.
 *     NULL if no MOH.
 *     Empty if default MOH class.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_write_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class);

/*!
 * \brief Queue a bridge action play file frame onto the bridge channel.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to put the frame onto.
 * \param custom_play Call this function to play the playfile. (NULL if normal sound file to play)
 * \param playfile Sound filename to play.
 * \param moh_class MOH class to request bridge peers to hear while file is played.
 *     NULL if no MOH.
 *     Empty if default MOH class.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_queue_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class);

/*!
 * \brief Synchronously queue a bridge action play file frame onto the bridge channel.
 * \since 12.2.0
 *
 * \param bridge_channel Which channel to put the frame onto.
 * \param custom_play Call this function to play the playfile. (NULL if normal sound file to play)
 * \param playfile Sound filename to play.
 * \param moh_class MOH class to request bridge peers to hear while file is played.
 *     NULL if no MOH.
 *     Empty if default MOH class.
 *
 * This function will block until the queued frame has been destroyed. This will happen
 * either if an error occurs or if the queued playback finishes.
 *
 * \note No locks may be held when calling this function.
 *
 * \retval 0 The playback was successfully queued.
 * \retval -1 The playback could not be queued.
 */
int ast_bridge_channel_queue_playfile_sync(struct ast_bridge_channel *bridge_channel,
		ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class);

/*!
 * \brief Custom callback run on a bridge channel.
 *
 * \param bridge_channel Which channel to operate on.
 * \param payload Data to pass to the callback. (NULL if none).
 * \param payload_size Size of the payload if payload is non-NULL.  A number otherwise.
 *
 * \note The payload MUST NOT have any resources that need to be freed.
 *
 * \return Nothing
 */
typedef void (*ast_bridge_custom_callback_fn)(struct ast_bridge_channel *bridge_channel, const void *payload, size_t payload_size);

enum ast_bridge_channel_custom_callback_option {
	/*! The callback temporarily affects media. (Like a custom playfile.) */
	AST_BRIDGE_CHANNEL_CB_OPTION_MEDIA = (1 << 0),
};

/*!
 * \brief Write a bridge action custom callback frame into the bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is putting the frame into the bridge
 * \param flags Custom callback option flags.
 * \param callback Custom callback run on a bridge channel.
 * \param payload Data to pass to the callback. (NULL if none).
 * \param payload_size Size of the payload if payload is non-NULL.  A number otherwise.
 *
 * \note The payload MUST NOT have any resources that need to be freed.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_write_callback(struct ast_bridge_channel *bridge_channel,
	enum ast_bridge_channel_custom_callback_option flags,
	ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size);

/*!
 * \brief Queue a bridge action custom callback frame onto the bridge channel.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to put the frame onto.
 * \param flags Custom callback option flags.
 * \param callback Custom callback run on a bridge channel.
 * \param payload Data to pass to the callback. (NULL if none).
 * \param payload_size Size of the payload if payload is non-NULL.  A number otherwise.
 *
 * \note The payload MUST NOT have any resources that need to be freed.
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_queue_callback(struct ast_bridge_channel *bridge_channel,
	enum ast_bridge_channel_custom_callback_option flags,
	ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size);

/*!
 * \brief Have a bridge channel park a channel in the bridge
 * \since 12.0.0
 *
 * \param bridge_channel Bridge channel performing the parking
 * \param parkee_uuid Unique id of the channel we want to park
 * \param parker_uuid Unique id of the channel parking the call
 * \param app_data string indicating data used for park application (NULL allowed)
 *
 * \note This is intended to be called by bridge hooks.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_bridge_channel_write_park(struct ast_bridge_channel *bridge_channel, const char *parkee_uuid,
	const char *parker_uuid, const char *app_data);

/*!
 * \brief Kick the channel out of the bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is being kicked or hungup.
 * \param cause Cause of channel being kicked.
 *   If cause <= 0 then use cause on channel if cause still <= 0 use AST_CAUSE_NORMAL_CLEARING.
 *
 * \note This is intended to be called by bridge hooks and the
 * bridge channel thread.
 *
 * \return Nothing
 */
void ast_bridge_channel_kick(struct ast_bridge_channel *bridge_channel, int cause);

/*!
 * \brief Add a DTMF digit to the collected digits.
 * \since 13.3.0
 *
 * \param bridge_channel Channel that received a DTMF digit.
 * \param digit DTMF digit to add to collected digits
 *
 * \note Neither the bridge nor the bridge_channel locks should be held
 * when entering this function.
 *
 * \note This is can only be called from within DTMF bridge hooks.
 */
void ast_bridge_channel_feature_digit_add(struct ast_bridge_channel *bridge_channel, int digit);

/*!
 * \brief Add a DTMF digit to the collected digits to match against DTMF features.
 * \since 12.8.0
 *
 * \param bridge_channel Channel that received a DTMF digit.
 * \param digit DTMF digit to add to collected digits or 0 for timeout event.
 * \param clear_digits clear the digits array prior to calling hooks
 *
 * \note Neither the bridge nor the bridge_channel locks should be held
 * when entering this function.
 *
 * \note This is intended to be called by bridge hooks and the
 * bridge channel thread.
 *
 * \note This is intended to be called by non-DTMF bridge hooks and the bridge
 * channel thread.  Calling from a DTMF bridge hook can potentially cause
 * unbounded recursion.
 *
 * \return Nothing
 */
void ast_bridge_channel_feature_digit(struct ast_bridge_channel *bridge_channel, int digit);

/*!
 * \brief Maps a channel's stream topology to and from the bridge
 * \since 15.0.0
 *
 * \details
 * When a channel joins a bridge or its associated stream topology is
 * updated, each stream in the topology needs to be mapped according
 * to its media type to the bridge.  Calling this method creates a
 * mapping of each stream on the channel indexed to the bridge's
 * supported media types and vice versa (i.e. bridge's media types
 * indexed to channel streams).
 *
 * The first channel to join the bridge creates the initial order for
 * the bridge's media types (e.g. a one to one mapping is made).
 * Subsequently added channels are mapped to that order adding more
 * media types if/when the newly added channel has more streams and/or
 * media types specified by the bridge.
 *
 * \param bridge_channel Channel to map
 *
 * \note The bridge_channel's bridge must be locked prior to calling this function.
 *
 * \return Nothing
 */
void ast_bridge_channel_stream_map(struct ast_bridge_channel *bridge_channel);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_BRIDGING_CHANNEL_H */
