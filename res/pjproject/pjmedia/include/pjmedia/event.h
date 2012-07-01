/* $Id$ */
/* 
 * Copyright (C) 2011-2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#ifndef __PJMEDIA_EVENT_H__
#define __PJMEDIA_EVENT_H__

/**
 * @file pjmedia/event.h
 * @brief Event framework
 */
#include <pjmedia/format.h>
#include <pjmedia/signatures.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJMEDIA_EVENT Event Framework
 * @brief PJMEDIA event framework
 * @{
 */

/**
 * This enumeration describes list of media events.
 */
typedef enum pjmedia_event_type
{
    /**
     * No event.
     */
    PJMEDIA_EVENT_NONE,

    /**
     * Media format has changed event.
     */
    PJMEDIA_EVENT_FMT_CHANGED	= PJMEDIA_FOURCC('F', 'M', 'C', 'H'),

    /**
     * Video window is being closed.
     */
    PJMEDIA_EVENT_WND_CLOSING	= PJMEDIA_FOURCC('W', 'N', 'C', 'L'),

    /**
     * Video window has been closed event.
     */
    PJMEDIA_EVENT_WND_CLOSED	= PJMEDIA_FOURCC('W', 'N', 'C', 'O'),

    /**
     * Video window has been resized event.
     */
    PJMEDIA_EVENT_WND_RESIZED	= PJMEDIA_FOURCC('W', 'N', 'R', 'Z'),

    /**
     * Mouse button has been pressed event.
     */
    PJMEDIA_EVENT_MOUSE_BTN_DOWN = PJMEDIA_FOURCC('M', 'S', 'D', 'N'),

    /**
     * Video keyframe has just been decoded event.
     */
    PJMEDIA_EVENT_KEYFRAME_FOUND = PJMEDIA_FOURCC('I', 'F', 'R', 'F'),

    /**
     * Video decoding error due to missing keyframe event.
     */
    PJMEDIA_EVENT_KEYFRAME_MISSING = PJMEDIA_FOURCC('I', 'F', 'R', 'M'),

    /**
     * Video orientation has been changed event.
     */
    PJMEDIA_EVENT_ORIENT_CHANGED = PJMEDIA_FOURCC('O', 'R', 'N', 'T')

} pjmedia_event_type;

/**
 * Additional data/parameters for media format changed event
 * (PJMEDIA_EVENT_FMT_CHANGED).
 */
typedef struct pjmedia_event_fmt_changed_data
{
    /** The media flow direction */
    pjmedia_dir		dir;

    /** The new media format. */
    pjmedia_format	new_fmt;
} pjmedia_event_fmt_changed_data;

/**
 * Additional data/parameters are not needed.
 */
typedef struct pjmedia_event_dummy_data
{
    /** Dummy data */
    int			dummy;
} pjmedia_event_dummy_data;

/**
 * Additional data/parameters for window resized event
 * (PJMEDIA_EVENT_WND_RESIZED).
 */
typedef struct pjmedia_event_wnd_resized_data
{
    /**
     * The new window size.
     */
    pjmedia_rect_size	new_size;
} pjmedia_event_wnd_resized_data;

/**
 * Additional data/parameters for window closing event.
 */
typedef struct pjmedia_event_wnd_closing_data
{
    /** Consumer may set this field to PJ_TRUE to cancel the closing */
    pj_bool_t		cancel;
} pjmedia_event_wnd_closing_data;

/** Additional parameters for window changed event. */
typedef pjmedia_event_dummy_data pjmedia_event_wnd_closed_data;

/** Additional parameters for mouse button down event */
typedef pjmedia_event_dummy_data pjmedia_event_mouse_btn_down_data;

/** Additional parameters for keyframe found event */
typedef pjmedia_event_dummy_data pjmedia_event_keyframe_found_data;

/** Additional parameters for keyframe missing event */
typedef pjmedia_event_dummy_data pjmedia_event_keyframe_missing_data;

/**
 * Maximum size of additional parameters section in pjmedia_event structure
 */
#define PJMEDIA_EVENT_DATA_MAX_SIZE	sizeof(pjmedia_event_fmt_changed_data)

/** Type of storage to hold user data in pjmedia_event structure */
typedef char pjmedia_event_user_data[PJMEDIA_EVENT_DATA_MAX_SIZE];

/**
 * This structure describes a media event. It consists mainly of the event
 * type and additional data/parameters for the event. Applications can
 * use #pjmedia_event_init() to initialize this event structure with
 * basic information about the event.
 */
typedef struct pjmedia_event
{
    /**
     * The event type.
     */
    pjmedia_event_type			 type;

    /**
     * The media timestamp when the event occurs.
     */
    pj_timestamp		 	 timestamp;

    /**
     * Pointer information about the source of this event. This field
     * is provided mainly for comparison purpose so that event subscribers
     * can check which source the event originated from. Usage of this
     * pointer for other purpose may require special care such as mutex
     * locking or checking whether the object is already destroyed.
     */
    const void	                        *src;

    /**
     * Pointer information about the publisher of this event. This field
     * is provided mainly for comparison purpose so that event subscribers
     * can check which object published the event. Usage of this
     * pointer for other purpose may require special care such as mutex
     * locking or checking whether the object is already destroyed.
     */
    const void	                        *epub;

    /**
     * Additional data/parameters about the event. The type of data
     * will be specific to the event type being reported.
     */
    union {
	/** Media format changed event data. */
	pjmedia_event_fmt_changed_data		fmt_changed;

	/** Window resized event data */
	pjmedia_event_wnd_resized_data		wnd_resized;

	/** Window closing event data. */
	pjmedia_event_wnd_closing_data		wnd_closing;

	/** Window closed event data */
	pjmedia_event_wnd_closed_data		wnd_closed;

	/** Mouse button down event data */
	pjmedia_event_mouse_btn_down_data	mouse_btn_down;

	/** Keyframe found event data */
	pjmedia_event_keyframe_found_data	keyframe_found;

	/** Keyframe missing event data */
	pjmedia_event_keyframe_missing_data	keyframe_missing;

	/** Storage for user event data */
	pjmedia_event_user_data			user;

	/** Pointer to storage to user event data, if it's outside
	 * this struct
	 */
	void					*ptr;
    } data;
} pjmedia_event;

/**
 * The callback to receive media events.
 *
 * @param event		The media event.
 * @param user_data	The user data associated with the callback.
 *
 * @return		If the callback returns non-PJ_SUCCESS, this return
 * 			code may be propagated back to the caller.
 */
typedef pj_status_t pjmedia_event_cb(pjmedia_event *event,
                                     void *user_data);

/**
 * This enumeration describes flags for event publication via
 * #pjmedia_event_publish().
 */
typedef enum pjmedia_event_publish_flag
{
    /**
     * Publisher will only post the event to the event manager. It is the
     * event manager that will later notify all the publisher's subscribers.
     */
    PJMEDIA_EVENT_PUBLISH_POST_EVENT = 1

} pjmedia_event_publish_flag;

/**
 * Event manager flag.
 */
typedef enum pjmedia_event_mgr_flag
{
    /**
     * Tell the event manager not to create any event worker thread.
     */
    PJMEDIA_EVENT_MGR_NO_THREAD = 1

} pjmedia_event_mgr_flag;

/**
 * Opaque data type for event manager. Typically, the event manager
 * is a singleton instance, although application may instantiate more than one
 * instances of this if required.
 */
typedef struct pjmedia_event_mgr pjmedia_event_mgr;

/**
 * Create a new event manager instance. This will also set the pointer
 * to the singleton instance if the value is still NULL.
 *
 * @param pool		Pool to allocate memory from.
 * @param options       Options. Bitmask flags from #pjmedia_event_mgr_flag
 * @param mgr		Pointer to hold the created instance of the
 * 			event manager.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_event_mgr_create(pj_pool_t *pool,
                                              unsigned options,
				              pjmedia_event_mgr **mgr);

/**
 * Get the singleton instance of the event manager.
 *
 * @return		The instance.
 */
PJ_DECL(pjmedia_event_mgr*) pjmedia_event_mgr_instance(void);

/**
 * Manually assign a specific event manager instance as the singleton
 * instance. Normally this is not needed if only one instance is ever
 * going to be created, as the library automatically assign the singleton
 * instance.
 *
 * @param mgr		The instance to be used as the singleton instance.
 * 			Application may specify NULL to clear the singleton
 * 			singleton instance.
 */
PJ_DECL(void) pjmedia_event_mgr_set_instance(pjmedia_event_mgr *mgr);

/**
 * Destroy an event manager. If the manager happens to be the singleton
 * instance, the singleton instance will be set to NULL.
 *
 * @param mgr		The eventmanager. Specify NULL to use
 * 			the singleton instance.
 */
PJ_DECL(void) pjmedia_event_mgr_destroy(pjmedia_event_mgr *mgr);

/**
 * Initialize event structure with basic data about the event.
 *
 * @param event		The event to be initialized.
 * @param type		The event type to be set for this event.
 * @param ts		Event timestamp. May be set to NULL to set the event
 * 			timestamp to zero.
 * @param src		Event source.
 */
PJ_DECL(void) pjmedia_event_init(pjmedia_event *event,
                                 pjmedia_event_type type,
                                 const pj_timestamp *ts,
                                 const void *src);

/**
 * Subscribe a callback function to events published by the specified
 * publisher. Note that the subscriber may receive not only events emitted by
 * the specific publisher specified in the argument, but also from other
 * publishers contained by the publisher, if the publisher is republishing
 * events from other publishers.
 *
 * @param mgr		The event manager.
 * @param cb            The callback function to receive the event.
 * @param user_data     The user data to be associated with the callback
 *                      function.
 * @param epub		The event publisher.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_event_subscribe(pjmedia_event_mgr *mgr,
                                             pjmedia_event_cb *cb,
                                             void *user_data,
                                             void *epub);

/**
 * Unsubscribe the callback associated with the user data from a publisher.
 * If the user data is not specified, this function will do the
 * unsubscription for all user data. If the publisher, epub, is not
 * specified, this function will do the unsubscription from all publishers.
 *
 * @param mgr		The event manager.
 * @param cb            The callback function.
 * @param user_data     The user data associated with the callback
 *                      function, can be NULL.
 * @param epub		The event publisher, can be NULL.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t)
pjmedia_event_unsubscribe(pjmedia_event_mgr *mgr,
                          pjmedia_event_cb *cb,
                          void *user_data,
                          void *epub);

/**
 * Publish the specified event to all subscribers of the specified event
 * publisher. By default, the function will call all the subcribers'
 * callbacks immediately. If the publisher uses the flag
 * PJMEDIA_EVENT_PUBLISH_POST_EVENT, publisher will only post the event
 * to the event manager and return immediately. It is the event manager
 * that will later notify all the publisher's subscribers.
 *
 * @param mgr		The event manager.
 * @param epub		The event publisher.
 * @param event		The event to be published.
 * @param flag          Publication flag.
 *
 * @return		PJ_SUCCESS only if all subscription callbacks returned
 * 			PJ_SUCCESS.
 */
PJ_DECL(pj_status_t) pjmedia_event_publish(pjmedia_event_mgr *mgr,
                                           void *epub,
                                           pjmedia_event *event,
                                           pjmedia_event_publish_flag flag);


/**
 * @}  PJMEDIA_EVENT
 */


PJ_END_DECL

#endif	/* __PJMEDIA_EVENT_H__ */
