/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \author Russell Bryant <russell@digium.com>
 * \brief Generic event system
 */

#ifndef AST_EVENT_DEFS_H
#define AST_EVENT_DEFS_H

/*! \brief Event types
 * \note These values can *never* change. */
enum ast_event_type {
	/*! Reserved to provide the ability to subscribe to all events.  A specific
	 *  event should never have a payload of 0. */
	AST_EVENT_ALL                 = 0x00,
	/*! This event type is reserved for use by third-party modules to create
	 *  custom events without having to modify this file. 
	 *  \note There are no "custom" IE types, because IEs only have to be
	 *  unique to the event itself, not necessarily across all events. */
	AST_EVENT_CUSTOM              = 0x01,
	/*! Voicemail message waiting indication */
	AST_EVENT_MWI                 = 0x02,
	/*! Someone has subscribed to events */
	AST_EVENT_SUB                 = 0x03,
	/*! Someone has unsubscribed from events */
	AST_EVENT_UNSUB               = 0x04,
	/*! The aggregate state of a device across all servers configured to be
	 *  a part of a device state cluster has changed. */
	AST_EVENT_DEVICE_STATE        = 0x05,
	/*! The state of a device has changed on _one_ server.  This should not be used
	 *  directly, in general.  Use AST_EVENT_DEVICE_STATE instead. */
	AST_EVENT_DEVICE_STATE_CHANGE = 0x06,
	/*! Channel Event Logging events */
	AST_EVENT_CEL = 0x07,
	/*! Number of event types.  This should be the last event type + 1 */
	AST_EVENT_TOTAL        = 0x08,
};

/*! \brief Event Information Element types */
enum ast_event_ie_type {
	/*! Used to terminate the arguments to event functions */
	AST_EVENT_IE_END       = -1,

	/*! 
	 * \brief Number of new messages
	 * Used by: AST_EVENT_MWI 
	 * Payload type: UINT
	 */
	AST_EVENT_IE_NEWMSGS   = 0x01,
	/*! 
	 * \brief Number of
	 * Used by: AST_EVENT_MWI 
	 * Payload type: UINT
	 */
	AST_EVENT_IE_OLDMSGS   = 0x02,
	/*! 
	 * \brief Mailbox name \verbatim (mailbox[@context]) \endverbatim
	 * Used by: AST_EVENT_MWI 
	 * Payload type: STR
	 */
	AST_EVENT_IE_MAILBOX   = 0x03,
	/*! 
	 * \brief Unique ID
	 * Used by: AST_EVENT_SUB, AST_EVENT_UNSUB
	 * Payload type: UINT
	 */
	AST_EVENT_IE_UNIQUEID  = 0x04,
	/*! 
	 * \brief Event type 
	 * Used by: AST_EVENT_SUB, AST_EVENT_UNSUB
	 * Payload type: UINT
	 */
	AST_EVENT_IE_EVENTTYPE = 0x05,
	/*!
	 * \brief Hint that someone cares that an IE exists
	 * Used by: AST_EVENT_SUB
	 * Payload type: UINT (ast_event_ie_type)
	 */
	AST_EVENT_IE_EXISTS    = 0x6,
	/*!
	 * \brief Device Name
	 * Used by AST_EVENT_DEVICE_STATE_CHANGE
	 * Payload type: STR
	 */
	AST_EVENT_IE_DEVICE    = 0x07,
	/*!
	 * \brief Generic State IE
	 * Used by AST_EVENT_DEVICE_STATE_CHANGE
	 * Payload type: UINT
	 * The actual state values depend on the event which
	 * this IE is a part of.
	 */
	 AST_EVENT_IE_STATE    = 0x08,
	 /*!
	  * \brief Context IE
	  * Used by AST_EVENT_MWI
	  * Payload type: str
	  */
	 AST_EVENT_IE_CONTEXT  = 0x09,
	/*! 
	 * \brief Channel Event Type
	 * Used by: AST_EVENT_CEL
	 * Payload type: UINT
	 */
	AST_EVENT_IE_CEL_EVENT_TYPE = 0x0a,
	/*! 
	 * \brief Channel Event Time (seconds)
	 * Used by: AST_EVENT_CEL
	 * Payload type: UINT
	 */
	AST_EVENT_IE_CEL_EVENT_TIME = 0x0b,
	/*! 
	 * \brief Channel Event Time (micro-seconds)
	 * Used by: AST_EVENT_CEL
	 * Payload type: UINT
	 */
	AST_EVENT_IE_CEL_EVENT_TIME_USEC = 0x0c,
	/*! 
	 * \brief Channel Event User Event Name
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_USEREVENT_NAME = 0x0d,
	/*! 
	 * \brief Channel Event CID name
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_CIDNAME = 0x0e,
	/*! 
	 * \brief Channel Event CID num
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_CIDNUM = 0x0f,
	/*! 
	 * \brief Channel Event extension name
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_EXTEN = 0x10,
	/*! 
	 * \brief Channel Event context name
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_CONTEXT = 0x11,
	/*! 
	 * \brief Channel Event channel name
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_CHANNAME = 0x12,
	/*! 
	 * \brief Channel Event app name
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_APPNAME = 0x13,
	/*! 
	 * \brief Channel Event app args/data
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_APPDATA = 0x14,
	/*! 
	 * \brief Channel Event AMA flags
	 * Used by: AST_EVENT_CEL
	 * Payload type: UINT
	 */
	AST_EVENT_IE_CEL_AMAFLAGS = 0x15,
	/*! 
	 * \brief Channel Event AccountCode
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_ACCTCODE = 0x16,
	/*! 
	 * \brief Channel Event UniqueID
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_UNIQUEID = 0x17,
	/*! 
	 * \brief Channel Event Userfield
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_USERFIELD = 0x18,
	/*! 
	 * \brief Channel Event CID ANI field
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_CIDANI = 0x19,
	/*! 
	 * \brief Channel Event CID RDNIS field
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_CIDRDNIS = 0x1a,
	/*! 
	 * \brief Channel Event CID dnid
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_CIDDNID = 0x1b,
	/*! 
	 * \brief Channel Event Peer -- for Things involving multiple channels, like BRIDGE
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_PEER = 0x1c,
	/*! 
	 * \brief Channel Event LinkedID
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_LINKEDID = 0x1d,
	/*! 
	 * \brief Channel Event peeraccount
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_PEERACCT = 0x1e,
	/*! 
	 * \brief Channel Event extra data
	 * Used by: AST_EVENT_CEL
	 * Payload type: STR
	 */
	AST_EVENT_IE_CEL_EXTRA = 0x1f,
	/*!
	 * \brief Description
	 * Used by: AST_EVENT_SUB, AST_EVENT_UNSUB
	 * Payload type: STR
	 */
	AST_EVENT_IE_DESCRIPTION = 0x20,
	/*!
	 * \brief Entity ID
	 * Used by All events
	 * Payload type: RAW
	 * This IE indicates which server the event originated from
	 */
	AST_EVENT_IE_EID      = 0x21,
};

#define AST_EVENT_IE_MAX AST_EVENT_IE_EID

/*!
 * \brief Payload types for event information elements
 */
enum ast_event_ie_pltype {
	AST_EVENT_IE_PLTYPE_UNKNOWN = -1,
	/*! Just check if it exists, not the value */
	AST_EVENT_IE_PLTYPE_EXISTS,
	/*! Unsigned Integer (Can be used for signed, too ...) */
	AST_EVENT_IE_PLTYPE_UINT,
	/*! String */
	AST_EVENT_IE_PLTYPE_STR,
	/*! Raw data, compared with memcmp */
	AST_EVENT_IE_PLTYPE_RAW,
	/*! Bit flags (unsigned integer, compared using boolean logic) */
	AST_EVENT_IE_PLTYPE_BITFLAGS,
};

/*!
 * \brief Results for checking for subscribers
 *
 * \ref ast_event_check_subscriber()
 */
enum ast_event_subscriber_res {
	/*! No subscribers exist */
	AST_EVENT_SUB_NONE,
	/*! At least one subscriber exists */
	AST_EVENT_SUB_EXISTS,
};

struct ast_event;
struct ast_event_ie;
struct ast_event_sub;
struct ast_event_iterator;

/*!
 * \brief supposed to be an opaque type
 *
 * This is only here so that it can be declared on the stack.
 */
struct ast_event_iterator {
	uint16_t event_len;
	const struct ast_event *event;
	struct ast_event_ie *ie;
};

#endif /* AST_EVENT_DEFS_H */
