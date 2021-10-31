/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief FrameHook Architecture
 */

/*!

\page AstFrameHookAPI Asterisk FrameHook API

\section FrameHookFunctionality How FrameHooks Work
	FrameHooks work by intercepting all frames being written and read off
	a channel and allowing those frames to be viewed and manipulated within a
	call back function.  Frame interception occurs before any processing is
	done on the frame, which means this hook can be used to transparently
	manipulate a frame before it is read from the channel or written
	to the tech_pvt.  This API can be thought of as a layer between the
	channel API and the Asterisk core when going in the READ direction, and
	as a layer between the Channel API and the tech_pvt when going in the
	WRITE direction.

\section FrameHookAPIUsage How to Use an FrameHook
	Attaching and detaching an FrameHook to a channel is very simple.  There are only
	two functions involved, ast_framehook_attach() which will return an id representing
	the new FrameHook on the channel, and ast_framehook_detach() which signals the
	FrameHook for detachment and destruction. Below is detailed information each of these
	functions and their usage.

\code
	struct ast_framehook_interface interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = hook_event_cb,
		.destroy_cb = hook_destroy_cb,
		.data = data, // where the data ptr points to any custom data used later by the hook cb.
	};
	int id = ast_framehook_attach(channel, &interface);
\endcode

	The ast_framehook_attach() function creates and attaches a new FrameHook onto
	a channel. Once attached to the channel, the FrameHook will call the event_callback
	function each time a frame is written or read on the channel.  A custom data
	pointer can be provided to this function to store on the FrameHook as well.  This
	pointer can be used to keep up with any statefull information associated with the FrameHook
	and is provided during the event_callback function.  The destroy_callback function is optional.
	This function exists so any custom data stored on the FrameHook can be destroyed before
	the Framehook if destroyed.

\code
	ast_framehook_detach(channel, id);
\endcode

	The ast_framehook_detach() function signals the FrameHook represented by an id to
	be detached and destroyed on a channel.  Since it is possible this function may be called
	during the FrameHook's event callback, it is impossible to synchronously detach the
	FrameHook from the channel during this function call.  It is guaranteed that the next
	event proceeding the ast_framehook_detach() will be of type AST_FRAMEHOOK_EVENT_DETACH,
	and that after that event occurs no other event will ever be issued for that FrameHook.
	Once the FrameHook is destroyed, the destroy callback function will be called if it was
	provided. Note that if this function is never called, the FrameHook will be detached
	on channel destruction.

\section FrameHookAPICodeExample FrameHook Example Code
	The example code below attaches an FrameHook on a channel, and then detachs it when
	the first ast_frame is read or written to the event callback function.  The Framehook's id
	is stored on the FrameHook's data pointer so it can be detached within the callback.

\code
	static void destroy_cb(void *data) {
		ast_free(data);
	}

	static struct ast_frame *event_cb(struct ast_channel *chan,
			struct ast_frame *frame,
			enum ast_framehook_event event,
			void *data) {

		int *id = data;

		if (!frame) {
			return frame;
		}

		if (event == AST_FRAMEHOOK_EVENT_WRITE) {
			ast_log(LOG_NOTICE, "YAY we received a frame in the write direction, Type: %d\n", frame->frametype)
			ast_framehook_detach(chan, id); // the channel is guaranteed to be locked during this function call.
		} else if (event == AST_FRAMEHOOK_EVENT_READ) {
			ast_log(LOG_NOTICE, "YAY we received a frame in the read direction: Type: %d\n", frame->frametype);
			ast_framehook_detach(chan, id); // the channel is guaranteed to be locked during this function call.
		}

		return frame;
	{

	int some_function()
	{
		struct ast_framehook_interface interface = {
			.version = AST_FRAMEHOOK_INTERFACE_VERSION,
			.event_cb = hook_event_cb,
			.destroy_cb = hook_destroy_cb,
		};
		int *id = ast_calloc(1, sizeof(int));

		if (!id) {
			return -1;
		}

		interface.data = id; // This data will be returned to us in the callbacks.

		ast_channel_lock(chan);
		*id = ast_framehook_attach(chan, &interface);
		ast_channel_unlock(chan);

		if (*id < 0) {
			// framehook attach failed, free data
			ast_free(id);
			return -1;
		}
		return 0;
	}
\endcode
*/

#ifndef _AST_FRAMEHOOK_H_
#define _AST_FRAMEHOOK_H_
#include "asterisk/linkedlists.h"
#include "asterisk/frame.h"

struct ast_framehook;
struct ast_framehook_list;

/*!
 * \brief These are the types of events that the framehook's event callback can receive
 * \since 1.8
 */
enum ast_framehook_event {
	AST_FRAMEHOOK_EVENT_READ, /*!< frame is intercepted in the read direction on the channel. */
	AST_FRAMEHOOK_EVENT_WRITE, /*!< frame is intercepted on the write direction on the channel. */
	AST_FRAMEHOOK_EVENT_ATTACHED, /*!< framehook is attached and running on the channel, the first message sent to event_cb. */
	AST_FRAMEHOOK_EVENT_DETACHED /*!< framehook is detached from the channel, last message sent to event_cb. */
};

/*!
 * \brief This callback is called every time an event occurs on the framehook.
 * \since 1.8
 *
 * \details Two events are guaranteed to occur once the ast_framehook_attach()
 * function is called. These events are AST_FRAMEHOOK_EVENT_ATTACHED, which occurs
 * immediately after the framehook is attached to a channel, and
 * AST_FRAMEHOOK_EVENT_DETACHED, which occurs right after the framehook is
 * detached.
 *
 * It is completely valid for the frame variable to be set to NULL. Always do a NULL
 * check on the frame before attempted to access it. When the frame variable is present,
 * it is safe to view and manipulate that frame in any way possible.  It is even safe
 * to return a completely different frame, but when that occurs this function is in
 * charge of freeing the previous frame.
 *
 * The ast_channel will always be locked during this callback. Never attempt to unlock the
 * channel for any reason.
 *
 * \param channel, The ast_channel this framehook is attached to
 * \param frame, The ast_frame being intercepted for viewing and manipulation
 * \param event, The type of event which is occurring
 * \param data, The data pointer provided at framehook initialization.
 *
 * \retval the resulting frame.
 */
typedef struct ast_frame *(*ast_framehook_event_callback)(
	struct ast_channel *chan,
	struct ast_frame *frame,
	enum ast_framehook_event event,
	void *data);

/*!
 * \brief This callback is called immediately before the framehook is destroyed.
 * \since 1.8
 * \note  This function should be used to clean up any pointers pointing to the
 * framehook structure as the framehook will be freed immediately afterwards.
 *
 * \param data, The data pointer provided at framehook initialization. This
 * is a good place to clean up any state data allocated for the framehook stored in this
 * pointer.
 */
typedef void (*ast_framehook_destroy_callback)(void *data);

/*!
 * \brief This callback is called to determine if the framehook is currently consuming
 * frames of a given type
 * \since 12
 *
 * \param data, The data pointer provided at framehook initialization.
 * \param type, The type of frame.
 *
 * \return 0 if frame type is being ignored
 * \return 1 if frame type is not being ignored
 */
typedef int (*ast_framehook_consume_callback)(void *data, enum ast_frame_type type);

/*!
 * \brief This callback is called when a masquerade occurs on a channel with a framehook
 * \since 12
 *
 * \param data, The data pointer provided at framehook initialization.
 * \param framehook_id, The framehook ID where the framehook lives now
 * \param old_chan, The channel that was masqueraded.
 * \param new_chan, The channel that the masqueraded channel became.
 */
typedef void (*ast_framehook_chan_fixup_callback)(void *data, int framehook_id,
	struct ast_channel *old_chan, struct ast_channel *new_chan);

#define AST_FRAMEHOOK_INTERFACE_VERSION 4
/*! This interface is required for attaching a framehook to a channel. */
struct ast_framehook_interface {
	/*! framehook interface version number */
	uint16_t version;
	/*! event_cb represents the function that will be called everytime an event occurs on the framehook. */
	ast_framehook_event_callback event_cb;
	/*! destroy_cb is optional.  This function is called immediately before the framehook
	 * is destroyed to allow for stored_data cleanup. */
	ast_framehook_destroy_callback destroy_cb;
	/*! consume_cb is optional. This function is called to query whether the framehook is consuming
	* frames of a specific type at this time. If this callback is not implemented it is assumed that the
	* framehook will consume frames of all types. */
	ast_framehook_consume_callback consume_cb;
	/*! chan_fixup_cb is optional. This function is called when the channel that a framehook is running
	 * on is masqueraded and should be used to move any essential framehook data onto the channel the
	 * old channel was masqueraded to. */
	ast_framehook_chan_fixup_callback chan_fixup_cb;
	/*! chan_breakdown_cb is optional. This function is called when another channel is masqueraded into
	 * the channel that a framehook is running on and should be used to evaluate whether the framehook
	 * should remain on the channel. */
	ast_framehook_chan_fixup_callback chan_breakdown_cb;
	/*! disable_inheritance is optional. If set to non-zero, when a channel using this framehook is
	 * masqueraded, detach and destroy the framehook instead of moving it to the new channel. */
	int disable_inheritance;
	 /*! This pointer can represent any custom data to be stored on the !framehook. This
	 * data pointer will be provided during each event callback which allows the framehook
	 * to store any stateful data associated with the application using the hook. */
	void *data;
};

/*!
 * \brief Attach an framehook onto a channel for frame interception.
 * \since 1.8
 *
 * \param chan ast_channel The channel to attach the hook on to.
 * \param i framehook interface, The framehook's callback functions and stored data.
 *
 * \pre The Channel must be locked during this function call.
 *
 * \note The data pointer is never touched by the framehook API except to
 * provide it during the event and destruction callbacks.  It is entirely up to the
 * application using this API to manage the memory associated with the data pointer.
 *
 * \retval On success, non-negative id representing this hook on the channel
 * \retval On failure, -1
 */
int ast_framehook_attach(struct ast_channel *chan, struct ast_framehook_interface *i);

/*!
 * \brief Detach an framehook from a channel.
 * \since 1.8
 *
 * \pre The Channel must be locked during this function call.
 * If this function is never called after attaching an framehook,
 * the framehook will be detached and destroyed during channel
 * destruction.
 *
 * \param chan The channel the framehook is attached to
 * \param framehook_id The framehook's id
 *
 * \retval 0 success
 * \retval -1 framehook did not exist on the channel. This means the
 * framehook either never existed on the channel, or was already detached.
 */
int ast_framehook_detach(struct ast_channel *chan, int framehook_id);

/*!
 * \brief This is used by the channel API to detach and destroy all
 * framehooks on a channel during channel destruction.
 * \since 1.8
 *
 * \pre The Channel must be locked during this function call.
 *
 * \param chan channel containing the framehook list to destroy.
 * \retval 0 success
 * \retval -1 failure
 */
int ast_framehook_list_destroy(struct ast_channel *chan);

/*!
 * \brief This is used by the channel API during a masquerade operation
 * to move all mobile framehooks from the original channel to the clone channel.
 * \since 12.5.0
 *
 * \pre Both channels must be locked prior to this function call.
 *
 * \param old_chan The channel being cloned from
 * \param new_chan The channel being cloned to
 */
void ast_framehook_list_fixup(struct ast_channel *old_chan, struct ast_channel *new_chan);

/*!
 * \brief This is used by the channel API push a frame read event to a channel's framehook list.
 * \since 1.8
 *
 * \details After this function completes, the resulting frame that is returned could be anything,
 * even NULL.  There is nothing to keep up with after this function. If the frame is modified, the
 * framehook callback is in charge of any memory management associated with that modification.
 *
 * \pre The Channel must be locked during this function call.
 *
 * \param framehooks list to push event to.
 * \param frame being pushed to the framehook list.
 *
 * \return The resulting frame after being viewed and modified by the framehook callbacks.
 */
struct ast_frame *ast_framehook_list_read_event(struct ast_framehook_list *framehooks, struct ast_frame *frame);

/*!
 * \brief This is used by the channel API push a frame write event to a channel's framehook list.
 * \since 1.8
 *
 * \details After this function completes, the resulting frame that is returned could be anything,
 * even NULL.  There is nothing to keep up with after this function. If the frame is modified, the
 * framehook callback is in charge of any memory management associated with that modification.
 *
 * \pre The Channel must be locked during this function call.
 *
 * \param framehooks list to push event to.
 * \param frame being pushed to the framehook list.
 *
 * \return The resulting frame after being viewed and modified by the framehook callbacks.
 */
struct ast_frame *ast_framehook_list_write_event(struct ast_framehook_list *framehooks, struct ast_frame *frame);

/*!
 * \brief Determine if an framehook list is empty or not
 * \since 1.8
 * \pre The Channel must be locked during this function call.
 *
 * \param framehooks the framehook list
 * \retval 0, not empty
 * \retval 1, is empty
 */
int ast_framehook_list_is_empty(struct ast_framehook_list *framehooks);

/*!
 * \brief Determine if a framehook list is free of active framehooks or not
 * \since 12.0.0
 * \pre The channel must be locked during this function call.
 *
 * \param framehooks the framehook list
 * \retval 0, not empty
 * \retval 1, is empty (aside from dying framehooks)
 *
 * \note This function is very similar to ast_framehook_list_is_empty, but it checks individual
 *       framehooks to see if they have been marked for destruction and doesn't count them if they are.
 */
int ast_framehook_list_contains_no_active(struct ast_framehook_list *framehooks);

/*!
 * \brief Determine if a framehook list is free of active framehooks consuming a specific type of frame
 * \since 12.3.0
 * \pre The channel must be locked during this function call.
 *
 * \param framehooks the framehook list
 * \retval 0, not empty
 * \retval 1, is empty (aside from dying framehooks)
 *
 * \note This function is very similar to ast_framehook_list_is_empty, but it checks individual
 *       framehooks to see if they have been marked for destruction and doesn't count them if they are.
 */
int ast_framehook_list_contains_no_active_of_type(struct ast_framehook_list *framehooks,
	enum ast_frame_type type);

#endif /* _AST_FRAMEHOOK_H */
