/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief General Asterisk PBX channel definitions.
 * \par See also:
 *  \arg \ref Def_Channel
 *  \arg \ref channel_drivers
 */

/*! \page Def_Channel Asterisk Channels
	\par What is a Channel?
	A phone call through Asterisk consists of an incoming
	connection and an outbound connection. Each call comes
	in through a channel driver that supports one technology,
	like SIP, DAHDI, IAX2 etc.
	\par
	Each channel driver, technology, has it's own private
	channel or dialog structure, that is technology-dependent.
	Each private structure is "owned" by a generic Asterisk
	channel structure, defined in channel.h and handled by
	channel.c .
	\par Call scenario
	This happens when an incoming call arrives to Asterisk
	-# Call arrives on a channel driver interface
	-# Channel driver creates a PBX channel and starts a
	   pbx thread on the channel
	-# The dial plan is executed
	-# At this point at least two things can happen:
		-# The call is answered by Asterisk and
		   Asterisk plays a media stream or reads media
		-# The dial plan forces Asterisk to create an outbound
		   call somewhere with the dial (see \ref app_dial.c)
		   application
	.

	\par Bridging channels
	If Asterisk dials out this happens:
	-# Dial creates an outbound PBX channel and asks one of the
	   channel drivers to create a call
	-# When the call is answered, Asterisk bridges the media streams
	   so the caller on the first channel can speak with the callee
	   on the second, outbound channel
	-# In some cases where we have the same technology on both
	   channels and compatible codecs, a native bridge is used.
	   In a native bridge, the channel driver handles forwarding
	   of incoming audio to the outbound stream internally, without
	   sending audio frames through the PBX.
	-# In SIP, theres an "external native bridge" where Asterisk
	   redirects the endpoint, so audio flows directly between the
	   caller's phone and the callee's phone. Signalling stays in
	   Asterisk in order to be able to provide a proper CDR record
	   for the call.


	\par Masquerading channels
	In some cases, a channel can masquerade itself into another
	channel. This happens frequently in call transfers, where
	a new channel takes over a channel that is already involved
	in a call. The new channel sneaks in and takes over the bridge
	and the old channel, now a zombie, is hung up.

	\par Reference
	\arg channel.c - generic functions
	\arg channel.h - declarations of functions, flags and structures
	\arg translate.h - Transcoding support functions
	\arg \ref channel_drivers - Implemented channel drivers
	\arg \ref Def_Frame Asterisk Multimedia Frames
	\arg \ref Def_Bridge

*/
/*! \page Def_Bridge Asterisk Channel Bridges

	In Asterisk, there's several media bridges.

	The Core bridge handles two channels (a "phone call") and bridge
	them together.

	The conference bridge (meetme) handles several channels simultaneously
	with the support of an external timer (DAHDI timer). This is used
	not only by the Conference application (meetme) but also by the
	page application and the SLA system introduced in 1.4.
	The conference bridge does not handle video.

	When two channels of the same type connect, the channel driver
	or the media subsystem used by the channel driver (i.e. RTP)
	can create a native bridge without sending media through the
	core.

	Native bridging can be disabled by a number of reasons,
	like DTMF being needed by the core or codecs being incompatible
	so a transcoding module is needed.

References:
	\li \see ast_channel_early_bridge()
	\li \see ast_channel_bridge()
	\li \see app_meetme.c
	\li \ref AstRTPbridge
	\li \see ast_rtp_bridge()
	\li \ref Def_Channel
*/

/*! \page AstFileDesc File descriptors
	Asterisk File descriptors are connected to each channel (see \ref Def_Channel)
	in the \ref ast_channel structure.
*/

#ifndef _ASTERISK_CHANNEL_H
#define _ASTERISK_CHANNEL_H

#include "asterisk/abstract_jb.h"
#include "asterisk/astobj2.h"
#include "asterisk/poll-compat.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_MAX_EXTENSION       80  /*!< Max length of an extension */
#define AST_MAX_CONTEXT         80  /*!< Max length of a context */

/*!
 * Max length of a channel uniqueid reported to the outside world.
 *
 * \details
 * 149 = 127 (max systemname) + "-" + 10 (epoch timestamp)
 *     + "." + 10 (monotonically incrementing integer).
 *
 * \note If this value is ever changed, MAX_CHANNEL_ID should
 * be updated in rtp_engine.h.
 */
#define AST_MAX_PUBLIC_UNIQUEID 149

/*!
 * Maximum size of an internal Asterisk channel unique ID.
 *
 * \details
 * Add two for the Local;2 channel to append a ';2' if needed
 * plus nul terminator.
 *
 * \note If this value is ever changed, MAX_CHANNEL_ID should
 * be updated in rtp_engine.h.
 */
#define AST_MAX_UNIQUEID        (AST_MAX_PUBLIC_UNIQUEID + 2 + 1)

#define AST_MAX_ACCOUNT_CODE    20  /*!< Max length of an account code */
#define AST_CHANNEL_NAME        80  /*!< Max length of an ast_channel name */
#define MAX_LANGUAGE            40  /*!< Max length of the language setting */
#define MAX_MUSICCLASS          80  /*!< Max length of the music class setting */
#define AST_MAX_USER_FIELD      256 /*!< Max length of the channel user field */

#include "asterisk/frame.h"
#include "asterisk/chanvars.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/cdr.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/stringfields.h"
#include "asterisk/datastore.h"
#include "asterisk/data.h"
#include "asterisk/channelstate.h"
#include "asterisk/ccss.h"
#include "asterisk/framehook.h"
#include "asterisk/stasis.h"
#include "asterisk/json.h"
#include "asterisk/endpoints.h"

#define DATASTORE_INHERIT_FOREVER	INT_MAX

#define AST_MAX_FDS		11
/*
 * We have AST_MAX_FDS file descriptors in a channel.
 * Some of them have a fixed use:
 */
#define AST_ALERT_FD	(AST_MAX_FDS-1)		/*!< used for alertpipe */
#define AST_TIMING_FD	(AST_MAX_FDS-2)		/*!< used for timingfd */
#define AST_AGENT_FD	(AST_MAX_FDS-3)		/*!< used by agents for pass through */
#define AST_GENERATOR_FD	(AST_MAX_FDS-4)	/*!< used by generator */
#define AST_JITTERBUFFER_FD	(AST_MAX_FDS-5)	/*!< used by generator */

enum ast_bridge_result {
	AST_BRIDGE_COMPLETE = 0,
	AST_BRIDGE_FAILED = -1,
	AST_BRIDGE_FAILED_NOWARN = -2,
	AST_BRIDGE_RETRY = -3,
};

typedef unsigned long long ast_group_t;

/*! \todo Add an explanation of an Asterisk generator
*/
struct ast_generator {
	void *(*alloc)(struct ast_channel *chan, void *params);
	/*! Channel is locked during this function callback. */
	void (*release)(struct ast_channel *chan, void *data);
	/*! This function gets called with the channel unlocked, but is called in
	 *  the context of the channel thread so we know the channel is not going
	 *  to disappear.  This callback is responsible for locking the channel as
	 *  necessary. */
	int (*generate)(struct ast_channel *chan, void *data, int len, int samples);
	/*! This gets called when DTMF_END frames are read from the channel */
	void (*digit)(struct ast_channel *chan, char digit);
	/*! This gets called when the write format on a channel is changed while
	 * generating. The channel is locked during this callback. */
	void (*write_format_change)(struct ast_channel *chan, void *data);
};

/*! Party name character set enumeration values (values from Q.SIG) */
enum AST_PARTY_CHAR_SET {
	AST_PARTY_CHAR_SET_UNKNOWN = 0,
	AST_PARTY_CHAR_SET_ISO8859_1 = 1,
	AST_PARTY_CHAR_SET_WITHDRAWN = 2,/* ITU withdrew this enum value. */
	AST_PARTY_CHAR_SET_ISO8859_2 = 3,
	AST_PARTY_CHAR_SET_ISO8859_3 = 4,
	AST_PARTY_CHAR_SET_ISO8859_4 = 5,
	AST_PARTY_CHAR_SET_ISO8859_5 = 6,
	AST_PARTY_CHAR_SET_ISO8859_7 = 7,
	AST_PARTY_CHAR_SET_ISO10646_BMPSTRING = 8,
	AST_PARTY_CHAR_SET_ISO10646_UTF_8STRING = 9,
};

/*!
 * \since 1.8
 * \brief Information needed to specify a name in a call.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_name {
	/*! \brief Subscriber name (Malloced) */
	char *str;
	/*!
	 * \brief Character set the name is using.
	 * \see enum AST_PARTY_CHAR_SET
	 * \note
	 * Set to AST_PARTY_CHAR_SET_ISO8859_1 if unsure what to use.
	 * \todo Start using the party name character set value.  Not currently used.
	 */
	int char_set;
	/*!
	 * \brief Q.931 encoded presentation-indicator encoded field
	 * \note Must tolerate the Q.931 screening-indicator field values being present.
	 */
	int presentation;
	/*! \brief TRUE if the name information is valid/present */
	unsigned char valid;
};

/*!
 * \since 1.8
 * \brief Information needed to specify a number in a call.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_number {
	/*! \brief Subscriber phone number (Malloced) */
	char *str;
	/*! \brief Q.931 Type-Of-Number and Numbering-Plan encoded fields */
	int plan;
	/*! \brief Q.931 presentation-indicator and screening-indicator encoded fields */
	int presentation;
	/*! \brief TRUE if the number information is valid/present */
	unsigned char valid;
};

/*!
 * \since 1.8
 * \brief Information needed to specify a subaddress in a call.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_subaddress {
	/*!
	 * \brief Malloced subaddress string.
	 * \note If the subaddress type is user specified then the subaddress is
	 * a string of ASCII hex because the actual subaddress is likely BCD encoded.
	 */
	char *str;
	/*!
	 * \brief Q.931 subaddress type.
	 * \details
	 * nsap(0),
	 * user_specified(2)
	 */
	int type;
	/*!
	 * \brief TRUE if odd number of address signals
	 * \note The odd/even indicator is used when the type of subaddress is
	 * user_specified and the coding is BCD.
	 */
	unsigned char odd_even_indicator;
	/*! \brief TRUE if the subaddress information is valid/present */
	unsigned char valid;
};

/*!
 * \since 1.8
 * \brief Information needed to identify an endpoint in a call.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_id {
	/*! \brief Subscriber name */
	struct ast_party_name name;
	/*! \brief Subscriber phone number */
	struct ast_party_number number;
	/*! \brief Subscriber subaddress. */
	struct ast_party_subaddress subaddress;

	/*!
	 * \brief User-set "tag"
	 * \details
	 * A user-settable field used to help associate some extrinsic information
	 * about the channel or user of the channel to the party ID.  This information
	 * is normally not transmitted over the wire and so is only useful within an
	 * Asterisk environment.
	 */
	char *tag;
};

/*!
 * \since 1.8
 * \brief Indicate what information in ast_party_id should be set.
 */
struct ast_set_party_id {
	/*! TRUE if the ast_party_name information should be set. */
	unsigned char name;
	/*! TRUE if the ast_party_number information should be set. */
	unsigned char number;
	/*! TRUE if the ast_party_subaddress information should be set. */
	unsigned char subaddress;
};

/*!
 * \since 1.8
 * \brief Dialed/Called Party information.
 * \note Dialed Number Identifier (DNID)
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_dialed {
	/*!
	 * \brief Dialed/Called number
	 * \note Done this way in case we ever really need to use ast_party_number.
	 * We currently do not need all of the ast_party_number fields.
	 */
	struct {
		/*! \brief Subscriber phone number (Malloced) */
		char *str;
		/*! \brief Q.931 Type-Of-Number and Numbering-Plan encoded fields */
		int plan;
	} number;
	/*! \brief Dialed/Called subaddress */
	struct ast_party_subaddress subaddress;
	/*!
	 * \brief Transit Network Select
	 * \note Currently this value is just passed around the system.
	 * You can read it and set it but it is never used for anything.
	 */
	int transit_network_select;
};

/*!
 * \since 1.8
 * \brief Caller Party information.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 *
 * \note SIP and IAX2 has UTF8 encoded Unicode Caller ID names.
 * In some cases, we also have an alternative (RPID) E.164 number that can
 * be used as Caller ID on numeric E.164 phone networks (DAHDI or SIP/IAX2 to
 * PSTN gateway).
 *
 * \todo Implement settings for transliteration between UTF8 Caller ID names in
 *       to ASCII Caller ID's (DAHDI). Östen Åsklund might be transliterated into
 *       Osten Asklund or Oesten Aasklund depending upon language and person...
 *       We need automatic routines for incoming calls and static settings for
 *       our own accounts.
 */
struct ast_party_caller {
	/*! \brief Caller party ID */
	struct ast_party_id id;

	/*!
	 * \brief Automatic Number Identification (ANI)
	 * \note The name subcomponent is only likely to be used by SIP.
	 * \note The subaddress subcomponent is not likely to be used.
	 */
	struct ast_party_id ani;

	/*! \brief Private caller party ID */
	struct ast_party_id priv;

	/*! \brief Automatic Number Identification 2 (Info Digits) */
	int ani2;
};

/*!
 * \since 1.8
 * \brief Indicate what information in ast_party_caller should be set.
 */
struct ast_set_party_caller {
	/*! What caller id information to set. */
	struct ast_set_party_id id;
	/*! What ANI id information to set. */
	struct ast_set_party_id ani;
	/*! What private caller id information to set. */
	struct ast_set_party_id priv;
};

/*!
 * \since 1.8
 * \brief Connected Line/Party information.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_connected_line {
	/*! \brief Connected party ID */
	struct ast_party_id id;

	/*!
	 * \brief Automatic Number Identification (ANI)
	 * \note Not really part of connected line data but needed to
	 * save the corresponding caller id value.
	 */
	struct ast_party_id ani;

	/*! \brief Private connected party ID */
	struct ast_party_id priv;

	/*!
	 * \brief Automatic Number Identification 2 (Info Digits)
	 * \note Not really part of connected line data but needed to
	 * save the corresponding caller id value.
	 */
	int ani2;

	/*!
	 * \brief Information about the source of an update.
	 * \note enum AST_CONNECTED_LINE_UPDATE_SOURCE values
	 * for Normal-Answer and Call-transfer.
	 */
	int source;
};

/*!
 * \since 1.8
 * \brief Indicate what information in ast_party_connected_line should be set.
 */
struct ast_set_party_connected_line {
	/*! What connected line id information to set. */
	struct ast_set_party_id id;
	/*! What ANI id information to set. */
	struct ast_set_party_id ani;
	/*! What private connected line id information to set. */
	struct ast_set_party_id priv;
};

/*!
 * \brief Redirecting reason information
 */
struct ast_party_redirecting_reason {
	/*! \brief a string value for the redirecting reason
	 *
	 * Useful for cases where an endpoint has specified a redirecting reason
	 * that does not correspond to an enum AST_REDIRECTING_REASON
	 */
	char *str;

	/*! \brief enum AST_REDIRECTING_REASON value for redirection */
	int code;
};

/*!
 * \since 1.8
 * \brief Redirecting Line information.
 * RDNIS (Redirecting Directory Number Information Service)
 * Where a call diversion or transfer was invoked.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_redirecting {
	/*! \brief Who originally redirected the call (Sent to the party the call is redirected toward) */
	struct ast_party_id orig;

	/*! \brief Who is redirecting the call (Sent to the party the call is redirected toward) */
	struct ast_party_id from;

	/*! \brief Call is redirecting to a new party (Sent to the caller) */
	struct ast_party_id to;

	/*! \brief Who originally redirected the call (Sent to the party the call is redirected toward) - private representation */
	struct ast_party_id priv_orig;

	/*! \brief Who is redirecting the call (Sent to the party the call is redirected toward) - private representation */
	struct ast_party_id priv_from;

	/*! \brief Call is redirecting to a new party (Sent to the caller)  - private representation */
	struct ast_party_id priv_to;

	/*! \brief Reason for the redirection */
	struct ast_party_redirecting_reason reason;

	/*! \brief Reason for the redirection by the original party */
	struct ast_party_redirecting_reason orig_reason;

	/*! \brief Number of times the call was redirected */
	int count;
};

/*!
 * \since 1.8
 * \brief Indicate what information in ast_party_redirecting should be set.
 */
struct ast_set_party_redirecting {
	/*! What redirecting-orig id information to set. */
	struct ast_set_party_id orig;
	/*! What redirecting-from id information to set. */
	struct ast_set_party_id from;
	/*! What redirecting-to id information to set. */
	struct ast_set_party_id to;
	/*! What private redirecting-orig id information to set. */
	struct ast_set_party_id priv_orig;
	/*! What private redirecting-from id information to set. */
	struct ast_set_party_id priv_from;
	/*! What private redirecting-to id information to set. */
	struct ast_set_party_id priv_to;
};

/*!
 * \brief Typedef for a custom read function
 * \note data should be treated as const char *.
 */
typedef int (*ast_acf_read_fn_t)(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len);

/*!
 * \brief Typedef for a custom read2 function
 * \note data should be treated as const char *.
 */
typedef int (*ast_acf_read2_fn_t)(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **str, ssize_t len);

/*!
 * \brief Typedef for a custom write function
 * \note data should be treated as const char *.
 */
typedef int (*ast_acf_write_fn_t)(struct ast_channel *chan, const char *function, char *data, const char *value);

/*! \brief Structure to handle passing func_channel_write info to channels via setoption */
typedef struct {
	/*! \brief ast_chan_write_info_t version. Must be incremented if structure is changed */
	#define AST_CHAN_WRITE_INFO_T_VERSION 1
	uint32_t version;
	ast_acf_write_fn_t write_fn;
	struct ast_channel *chan;
	const char *function;
	char *data;
	const char *value;
} ast_chan_write_info_t;

/*!
 * \brief Structure to pass both assignedid values to channel drivers
 * \note The second value is used only by core_unreal (LOCAL)
 */
struct ast_assigned_ids {
	const char *uniqueid;
	const char *uniqueid2;
};

/*!
 * \brief
 * Structure to describe a channel "technology", ie a channel driver
 * See for examples:
 * \arg chan_iax2.c - The Inter-Asterisk exchange protocol
 * \arg chan_sip.c - The SIP channel driver
 * \arg chan_dahdi.c - PSTN connectivity (TDM, PRI, T1/E1, FXO, FXS)
 *
 * \details
 * If you develop your own channel driver, this is where you
 * tell the PBX at registration of your driver what properties
 * this driver supports and where different callbacks are
 * implemented.
 */
struct ast_channel_tech {
	const char * const type;
	const char * const description;

	struct ast_format_cap *capabilities;  /*!< format capabilities this channel can handle */

	int properties;         /*!< Technology Properties */

	/*!
	 * \brief Requester - to set up call data structures (pvt's)
	 *
	 * \param type type of channel to request
	 * \param cap Format capabilities for requested channel
	 * \param assignedid Unique ID string to assign to channel
	 * \param requestor channel asking for data
	 * \param addr destination of the call
	 * \param cause Cause of failure
	 *
	 * \details
	 * Request a channel of a given type, with addr as optional information used
	 * by the low level module
	 *
	 * \retval NULL failure
	 * \retval non-NULL channel on success
	 */
	struct ast_channel *(* const requester)(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *addr, int *cause);

	int (* const devicestate)(const char *device_number);	/*!< Devicestate call back */

	/*!
	 * \brief Start sending a literal DTMF digit
	 *
	 * \note The channel is not locked when this function gets called.
	 */
	int (* const send_digit_begin)(struct ast_channel *chan, char digit);

	/*!
	 * \brief Stop sending a literal DTMF digit
	 *
	 * \note The channel is not locked when this function gets called.
	 */
	int (* const send_digit_end)(struct ast_channel *chan, char digit, unsigned int duration);

	/*!
	 * \brief Make a call
	 * \note The channel is locked when called.
	 * \param chan which channel to make the call on
	 * \param addr destination of the call
	 * \param timeout time to wait on for connect (Doesn't seem to be used.)
	 * \retval 0 on success
	 * \retval -1 on failure
	 */
	int (* const call)(struct ast_channel *chan, const char *addr, int timeout);

	/*! \brief Hangup (and possibly destroy) the channel */
	int (* const hangup)(struct ast_channel *chan);

	/*! \brief Answer the channel */
	int (* const answer)(struct ast_channel *chan);

	/*! \brief Read a frame, in standard format (see frame.h) */
	struct ast_frame * (* const read)(struct ast_channel *chan);

	/*! \brief Write a frame, in standard format (see frame.h) */
	int (* const write)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Display or transmit text */
	int (* const send_text)(struct ast_channel *chan, const char *text);

	/*! \brief Display or send an image */
	int (* const send_image)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Send HTML data */
	int (* const send_html)(struct ast_channel *chan, int subclass, const char *data, int len);

	/*! \brief Handle an exception, reading a frame */
	struct ast_frame * (* const exception)(struct ast_channel *chan);

	/*! \brief Bridge two channels of the same type together (early) */
	enum ast_bridge_result (* const early_bridge)(struct ast_channel *c0, struct ast_channel *c1);

	/*! \brief Indicate a particular condition (e.g. AST_CONTROL_BUSY or AST_CONTROL_RINGING or AST_CONTROL_CONGESTION */
	int (* const indicate)(struct ast_channel *c, int condition, const void *data, size_t datalen);

	/*! \brief Fix up a channel:  If a channel is consumed, this is called.  Basically update any ->owner links */
	int (* const fixup)(struct ast_channel *oldchan, struct ast_channel *newchan);

	/*! \brief Set a given option. Called with chan locked */
	int (* const setoption)(struct ast_channel *chan, int option, void *data, int datalen);

	/*! \brief Query a given option. Called with chan locked */
	int (* const queryoption)(struct ast_channel *chan, int option, void *data, int *datalen);

	/*! \brief Blind transfer other side (see app_transfer.c and ast_transfer() */
	int (* const transfer)(struct ast_channel *chan, const char *newdest);

	/*! \brief Write a frame, in standard format */
	int (* const write_video)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Write a text frame, in standard format */
	int (* const write_text)(struct ast_channel *chan, struct ast_frame *frame);

	/*!
	 * \brief Provide additional read items for CHANNEL() dialplan function
	 * \note data should be treated as a const char *.
	 */
	int (* func_channel_read)(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len);

	/*!
	 * \brief Provide additional write items for CHANNEL() dialplan function
	 * \note data should be treated as a const char *.
	 */
	int (* func_channel_write)(struct ast_channel *chan, const char *function, char *data, const char *value);

	/*! \brief Get the unique identifier for the PVT, i.e. SIP call-ID for SIP */
	const char * (* get_pvt_uniqueid)(struct ast_channel *chan);

	/*! \brief Call a function with cc parameters as a function parameter
	 *
	 * \details
	 * This is a highly specialized callback that is not likely to be needed in many
	 * channel drivers. When dealing with a busy channel, for instance, most channel
	 * drivers will successfully return a channel to the requester. Once called, the channel
	 * can then queue a busy frame when it receives an appropriate message from the far end.
	 * In such a case, the channel driver has the opportunity to also queue a CC frame.
	 * The parameters for the CC channel can be retrieved from the channel structure.
	 *
	 * For other channel drivers, notably those that deal with "dumb" phones, the channel
	 * driver will not return a channel when one is requested. In such a scenario, there is never
	 * an opportunity for the channel driver to queue a CC frame since the channel is never
	 * called. Furthermore, it is not possible to retrieve the CC configuration parameters
	 * for the desired channel because no channel is ever allocated or returned to the
	 * requester. In such a case, call completion may still be a viable option. What we do is
	 * pass the same string that the requester used originally to request the channel to the
	 * channel driver. The channel driver can then find any potential channels/devices that
	 * match the input and return call the designated callback with the device's call completion
	 * parameters as a parameter.
	 */
	int (* cc_callback)(struct ast_channel *inbound, const char *dest, ast_cc_callback_fn callback);

	/*!
	 * \brief Execute a Gosub call on the channel in a technology specific way before a call is placed.
	 * \since 11.0
	 *
	 * \param chan Channel to execute Gosub in a tech specific way.
	 * \param sub_args Gosub application parameter string.
	 *
	 * \note The chan is locked before calling.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*pre_call)(struct ast_channel *chan, const char *sub_args);
};

/*! Kill the channel channel driver technology descriptor. */
extern const struct ast_channel_tech ast_kill_tech;

struct ast_epoll_data;

/*!
 * The high bit of the frame count is used as a debug marker, so
 * increments of the counters must be done with care.
 * Please use c->fin = FRAMECOUNT_INC(c->fin) and the same for c->fout.
 */
#define	DEBUGCHAN_FLAG  0x80000000

/* XXX not ideal to evaluate x twice... */
#define	FRAMECOUNT_INC(x)	( ((x) & DEBUGCHAN_FLAG) | (((x)+1) & ~DEBUGCHAN_FLAG) )

/*!
 * The current value of the debug flags is stored in the two
 * variables global_fin and global_fout (declared in main/channel.c)
 */
extern unsigned long global_fin, global_fout;

enum ast_channel_adsicpe {
	AST_ADSI_UNKNOWN,
	AST_ADSI_AVAILABLE,
	AST_ADSI_UNAVAILABLE,
	AST_ADSI_OFFHOOKONLY,
};

/*!
 * \brief Possible T38 states on channels
 */
enum ast_t38_state {
	T38_STATE_UNAVAILABLE,	/*!< T38 is unavailable on this channel or disabled by configuration */
	T38_STATE_UNKNOWN,	/*!< The channel supports T38 but the current status is unknown */
	T38_STATE_NEGOTIATING,	/*!< T38 is being negotiated */
	T38_STATE_REJECTED,	/*!< Remote side has rejected our offer */
	T38_STATE_NEGOTIATED,	/*!< T38 established */
};

/*! Hangup handler instance node. */
struct ast_hangup_handler {
	/*! Next hangup handler node. */
	AST_LIST_ENTRY(ast_hangup_handler) node;
	/*! Hangup handler arg string passed to the Gosub application */
	char args[0];
};

AST_LIST_HEAD_NOLOCK(ast_hangup_handler_list, ast_hangup_handler);
AST_LIST_HEAD_NOLOCK(ast_datastore_list, ast_datastore);
AST_LIST_HEAD_NOLOCK(ast_autochan_list, ast_autochan);
AST_LIST_HEAD_NOLOCK(ast_readq_list, ast_frame);

typedef int(*ast_timing_func_t)(const void *data);
/*!
 * \page AstChannel ast_channel locking and reference tracking
 *
 * \par Creating Channels
 * A channel is allocated using the ast_channel_alloc() function.  When created, it is
 * automatically inserted into the main channels hash table that keeps track of all
 * active channels in the system.  The hash key is based on the channel name.  Because
 * of this, if you want to change the name, you _must_ use ast_change_name(), not change
 * the name field directly.  When ast_channel_alloc() returns a channel pointer, you now
 * hold both a reference to that channel and a lock on the channel. Once the channel has
 * been set up the lock can be released. In most cases the reference is given to ast_pbx_run().
 *
 * \par Channel Locking
 * There is a lock associated with every ast_channel.  It is allocated internally via astobj2.
 * To lock or unlock a channel, you must use the ast_channel_lock() wrappers.
 *
 * Previously, before ast_channel was converted to astobj2, the channel lock was used in some
 * additional ways that are no longer necessary.  Before, the only way to ensure that a channel
 * did not disappear out from under you if you were working with a channel outside of the channel
 * thread that owns it, was to hold the channel lock.  Now, that is no longer necessary.
 * You simply must hold a reference to the channel to ensure it does not go away.
 *
 * The channel must be locked if you need to ensure that data that you reading from the channel
 * does not change while you access it.  Further, you must hold the channel lock if you are
 * making a non-atomic change to channel data.
 *
 * \par Channel References
 * There are multiple ways to get a reference to a channel.  The first is that you hold a reference
 * to a channel after creating it.  The other ways involve using the channel search or the channel
 * traversal APIs.  These functions are the ast_channel_get_*() functions or ast_channel_iterator_*()
 * functions.  Once a reference is retrieved by one of these methods, you know that the channel will
 * not go away.  So, the channel should only get locked as needed for data access or modification.
 * But, make sure that the reference gets released when you are done with it!
 *
 * There are different things you can do when you are done with a reference to a channel.  The first
 * is to simply release the reference using ast_channel_unref().  The other option is to call
 * ast_channel_release().  This function is generally used where ast_channel_free() was used in
 * the past.  The release function releases a reference as well as ensures that the channel is no
 * longer in the global channels container.  That way, the channel will get destroyed as soon as any
 * other pending references get released.
 *
 * \par Exceptions to the rules
 * Even though ast_channel is reference counted, there are some places where pointers to an ast_channel
 * get stored, but the reference count does not reflect it.  The reason is mostly historical.
 * The only places where this happens should be places where because of how the code works, we
 * _know_ that the pointer to the channel will get removed before the channel goes away.  The main
 * example of this is in channel drivers.  Channel drivers generally store a pointer to their owner
 * ast_channel in their technology specific pvt struct.  In this case, the channel drivers _know_
 * that this pointer to the channel will be removed in time, because the channel's hangup callback
 * gets called before the channel goes away.
 */

struct ast_channel;

/*! \brief ast_channel_tech Properties */
enum {
	/*!
	 * \brief Channels have this property if they can accept input with jitter;
	 * i.e. most VoIP channels
	 */
	AST_CHAN_TP_WANTSJITTER = (1 << 0),
	/*!
	 * \brief Channels have this property if they can create jitter;
	 * i.e. most VoIP channels
	 */
	AST_CHAN_TP_CREATESJITTER = (1 << 1),
	/*!
	 * \brief Channels with this particular technology are an implementation detail of
	 * Asterisk and should generally not be exposed or manipulated by the outside
	 * world
	 */
	AST_CHAN_TP_INTERNAL = (1 << 2),
};

/*! \brief ast_channel flags */
enum {
	/*! Queue incoming DTMF, to be released when this flag is turned off */
	AST_FLAG_DEFER_DTMF =    (1 << 1),
	/*! write should be interrupt generator */
	AST_FLAG_WRITE_INT =     (1 << 2),
	/*! a thread is blocking on this channel */
	AST_FLAG_BLOCKING =      (1 << 3),
	/*! This is a zombie channel */
	AST_FLAG_ZOMBIE =        (1 << 4),
	/*! There is an exception pending */
	AST_FLAG_EXCEPTION =     (1 << 5),
	/*! Listening to moh XXX anthm promises me this will disappear XXX */
	AST_FLAG_MOH =           (1 << 6),
	/*! This channel is spying on another channel */
	AST_FLAG_SPYING =        (1 << 7),
	/*! the channel is in an auto-incrementing dialplan processor,
	 *  so when ->priority is set, it will get incremented before
	 *  finding the next priority to run */
	AST_FLAG_IN_AUTOLOOP =   (1 << 9),
	/*! This is an outgoing call */
	AST_FLAG_OUTGOING =      (1 << 10),
	/*! A DTMF_BEGIN frame has been read from this channel, but not yet an END */
	AST_FLAG_IN_DTMF =       (1 << 12),
	/*! A DTMF_END was received when not IN_DTMF, so the length of the digit is
	 *  currently being emulated */
	AST_FLAG_EMULATE_DTMF =  (1 << 13),
	/*! This is set to tell the channel not to generate DTMF begin frames, and
	 *  to instead only generate END frames. */
	AST_FLAG_END_DTMF_ONLY = (1 << 14),
	/* OBSOLETED in favor of AST_CAUSE_ANSWERED_ELSEWHERE
	 * Flag to show channels that this call is hangup due to the fact that the call
	 * was indeed answered, but in another channel */
	/* AST_FLAG_ANSWERED_ELSEWHERE = (1 << 15), */
	/*! This flag indicates that on a masquerade, an active stream should not
	 *  be carried over */
	AST_FLAG_MASQ_NOSTREAM = (1 << 16),
	/*! This flag indicates that the hangup exten was run when the bridge terminated,
	 *  a message aimed at preventing a subsequent hangup exten being run at the pbx_run
	 *  level */
	AST_FLAG_BRIDGE_HANGUP_RUN = (1 << 17),
	/*! Disable certain workarounds.  This reintroduces certain bugs, but allows
	 *  some non-traditional dialplans (like AGI) to continue to function.
	 */
	AST_FLAG_DISABLE_WORKAROUNDS = (1 << 20),
	/*!
	 * Disable device state event caching.  This allows channel
	 * drivers to selectively prevent device state events from being
	 * cached by certain channels such as anonymous calls which have
	 * no persistent represenatation that can be tracked.
	 */
	AST_FLAG_DISABLE_DEVSTATE_CACHE = (1 << 21),
	/*!
	 * This flag indicates that a dual channel redirect is in
	 * progress.  The bridge needs to wait until the flag is cleared
	 * to continue.
	 */
	AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT = (1 << 22),
	/*!
	 * This flag indicates that the channel was originated.
	 */
	AST_FLAG_ORIGINATED = (1 << 23),
	/*!
	 * The channel is well and truly dead. Once this is set and published, no further
	 * actions should be taken upon the channel, and no further publications should
	 * occur.
	 */
	AST_FLAG_DEAD = (1 << 24),
	/*!
	 * Channel snapshot should not be published, it is being staged for an explicit
	 * publish.
	 */
	AST_FLAG_SNAPSHOT_STAGE = (1 << 25),
	/*!
	 * The data on chan->timingdata is an astobj2 object.
	 */
	AST_FLAG_TIMINGDATA_IS_AO2_OBJ = (1 << 26),
	/*!
	 * The channel is executing a subroutine or macro
	 */
	AST_FLAG_SUBROUTINE_EXEC = (1 << 27),
};

/*! \brief ast_bridge_config flags */
enum {
	AST_FEATURE_PLAY_WARNING = (1 << 0),
	AST_FEATURE_REDIRECT =     (1 << 1),
	AST_FEATURE_DISCONNECT =   (1 << 2),
	AST_FEATURE_ATXFER =       (1 << 3),
	AST_FEATURE_AUTOMON =      (1 << 4),
	AST_FEATURE_PARKCALL =     (1 << 5),
	AST_FEATURE_AUTOMIXMON =   (1 << 6),
};

#define AST_FEATURE_DTMF_MASK (AST_FEATURE_REDIRECT | AST_FEATURE_DISCONNECT |\
	AST_FEATURE_ATXFER | AST_FEATURE_AUTOMON | AST_FEATURE_PARKCALL | AST_FEATURE_AUTOMIXMON)

/*! \brief bridge configuration */
struct ast_bridge_config {
	struct ast_flags features_caller;
	struct ast_flags features_callee;
	struct timeval start_time;
	struct timeval nexteventts;
	struct timeval feature_start_time;
	long feature_timer;
	long timelimit;
	long play_warning;
	long warning_freq;
	const char *warning_sound;
	const char *end_sound;
	const char *start_sound;
	unsigned int flags;
	void (* end_bridge_callback)(void *);   /*!< A callback that is called after a bridge attempt */
	void *end_bridge_callback_data;         /*!< Data passed to the callback */
	/*! If the end_bridge_callback_data refers to a channel which no longer is going to
	 * exist when the end_bridge_callback is called, then it needs to be fixed up properly
	 */
	void (*end_bridge_callback_data_fixup)(struct ast_bridge_config *bconfig, struct ast_channel *originator, struct ast_channel *terminator);
};

struct chanmon;

struct outgoing_helper {
	const char *context;
	const char *exten;
	int priority;
	int connect_on_early_media;	/* If set, treat session progress as answer */
	const char *cid_num;
	const char *cid_name;
	const char *account;
	struct ast_variable *vars;
	struct ast_channel *parent_channel;
};

enum {
	/*!
	 * Soft hangup requested by device or other internal reason.
	 * Actual hangup needed.
	 */
	AST_SOFTHANGUP_DEV =       (1 << 0),
	/*!
	 * Used to break the normal frame flow so an async goto can be
	 * done instead of actually hanging up.
	 */
	AST_SOFTHANGUP_ASYNCGOTO = (1 << 1),
	/*!
	 * Soft hangup requested by system shutdown.  Actual hangup
	 * needed.
	 */
	AST_SOFTHANGUP_SHUTDOWN =  (1 << 2),
	/*!
	 * Used to break the normal frame flow after a timeout so an
	 * implicit async goto can be done to the 'T' exten if it exists
	 * instead of actually hanging up.  If the exten does not exist
	 * then actually hangup.
	 */
	AST_SOFTHANGUP_TIMEOUT =   (1 << 3),
	/*!
	 * Soft hangup requested by application/channel-driver being
	 * unloaded.  Actual hangup needed.
	 */
	AST_SOFTHANGUP_APPUNLOAD = (1 << 4),
	/*!
	 * Soft hangup requested by non-associated party.  Actual hangup
	 * needed.
	 */
	AST_SOFTHANGUP_EXPLICIT =  (1 << 5),
	/*!
	 * Used to indicate that the channel is currently executing hangup
	 * logic in the dialplan. The channel has been hungup when this is
	 * set.
	 */
	AST_SOFTHANGUP_HANGUP_EXEC = (1 << 7),
	/*!
	 * \brief All softhangup flags.
	 *
	 * This can be used as an argument to ast_channel_clear_softhangup()
	 * to clear all softhangup flags from a channel.
	 */
	AST_SOFTHANGUP_ALL =       (0xFFFFFFFF)
};


/*! \brief Channel reload reasons for manager events at load or reload of configuration */
enum channelreloadreason {
	CHANNEL_MODULE_LOAD,
	CHANNEL_MODULE_RELOAD,
	CHANNEL_CLI_RELOAD,
	CHANNEL_MANAGER_RELOAD,
	CHANNEL_ACL_RELOAD,
};

/*!
 * \brief Channel AMA Flags
 */
enum ama_flags {
	AST_AMA_NONE = 0,
	AST_AMA_OMIT,
	AST_AMA_BILLING,
	AST_AMA_DOCUMENTATION,
};

/*!
 * \note None of the datastore API calls lock the ast_channel they are using.
 *       So, the channel should be locked before calling the functions that
 *       take a channel argument.
 */

/*!
 * \brief Create a channel data store object
 * \deprecated You should use the ast_datastore_alloc() generic function instead.
 * \version 1.6.1 deprecated
 */
struct ast_datastore * attribute_malloc ast_channel_datastore_alloc(const struct ast_datastore_info *info, const char *uid)
	__attribute__((deprecated));

/*!
 * \brief Free a channel data store object
 * \deprecated You should use the ast_datastore_free() generic function instead.
 * \version 1.6.1 deprecated
 */
int ast_channel_datastore_free(struct ast_datastore *datastore)
	__attribute__((deprecated));

/*! \brief Inherit datastores from a parent to a child. */
int ast_channel_datastore_inherit(struct ast_channel *from, struct ast_channel *to);

/*!
 * \brief Add a datastore to a channel
 *
 * \note The channel should be locked before calling this function.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_channel_datastore_add(struct ast_channel *chan, struct ast_datastore *datastore);

/*!
 * \brief Remove a datastore from a channel
 *
 * \note The channel should be locked before calling this function.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_channel_datastore_remove(struct ast_channel *chan, struct ast_datastore *datastore);

/*!
 * \brief Find a datastore on a channel
 *
 * \note The channel should be locked before calling this function.
 *
 * \note The datastore returned from this function must not be used if the
 *       reference to the channel is released.
 *
 * \retval pointer to the datastore if found
 * \retval NULL if not found
 */
struct ast_datastore *ast_channel_datastore_find(struct ast_channel *chan, const struct ast_datastore_info *info, const char *uid);

/*!
 * \brief Create a channel structure
 * \since 1.8
 *
 * \retval NULL failure
 * \retval non-NULL successfully allocated channel
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 * \note By default, new channels are set to the "s" extension
 *       and "default" context.
 * \note Since 12.0.0 this function returns with the newly created channel locked.
 */
struct ast_channel * attribute_malloc __attribute__((format(printf, 15, 16)))
	__ast_channel_alloc(int needqueue, int state, const char *cid_num,
		const char *cid_name, const char *acctcode,
		const char *exten, const char *context, const struct ast_assigned_ids *assignedids,
		const struct ast_channel *requestor, enum ama_flags amaflag,
		struct ast_endpoint *endpoint,
		const char *file, int line, const char *function,
		const char *name_fmt, ...);

/*!
 * \brief Create a channel structure
 *
 * \retval NULL failure
 * \retval non-NULL successfully allocated channel
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 * \note By default, new channels are set to the "s" extension
 *       and "default" context.
 * \note Since 12.0.0 this function returns with the newly created channel locked.
 */
#define ast_channel_alloc(needqueue, state, cid_num, cid_name, acctcode, exten, context, assignedids, requestor, amaflag, ...) \
	__ast_channel_alloc(needqueue, state, cid_num, cid_name, acctcode, exten, context, assignedids, requestor, amaflag, NULL, \
		__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#define ast_channel_alloc_with_endpoint(needqueue, state, cid_num, cid_name, acctcode, exten, context, assignedids, requestor, amaflag, endpoint, ...) \
	__ast_channel_alloc((needqueue), (state), (cid_num), (cid_name), (acctcode), (exten), (context), (assignedids), (requestor), (amaflag), (endpoint), \
		__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)


#if defined(REF_DEBUG) || defined(__AST_DEBUG_MALLOC)
/*!
 * \brief Create a fake channel structure
 *
 * \retval NULL failure
 * \retval non-NULL successfully allocated channel
 *
 * \note This function should ONLY be used to create a fake channel
 *       that can then be populated with data for use in variable
 *       substitution when a real channel does not exist.
 *
 * \note The created dummy channel should be destroyed by
 * ast_channel_unref().  Using ast_channel_release() needlessly
 * grabs the channel container lock and can cause a deadlock as
 * a result.  Also grabbing the channel container lock reduces
 * system performance.
 */
#define ast_dummy_channel_alloc()	__ast_dummy_channel_alloc(__FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_channel *__ast_dummy_channel_alloc(const char *file, int line, const char *function);
#else
/*!
 * \brief Create a fake channel structure
 *
 * \retval NULL failure
 * \retval non-NULL successfully allocated channel
 *
 * \note This function should ONLY be used to create a fake channel
 *       that can then be populated with data for use in variable
 *       substitution when a real channel does not exist.
 *
 * \note The created dummy channel should be destroyed by
 * ast_channel_unref().  Using ast_channel_release() needlessly
 * grabs the channel container lock and can cause a deadlock as
 * a result.  Also grabbing the channel container lock reduces
 * system performance.
 */
struct ast_channel *ast_dummy_channel_alloc(void);
#endif

/*!
 * \brief Queue one or more frames to a channel's frame queue
 *
 * \param chan the channel to queue the frame(s) on
 * \param f the frame(s) to queue.  Note that the frame(s) will be duplicated
 *        by this function.  It is the responsibility of the caller to handle
 *        freeing the memory associated with the frame(s) being passed if
 *        necessary.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_queue_frame(struct ast_channel *chan, struct ast_frame *f);

/*!
 * \brief Queue one or more frames to the head of a channel's frame queue
 *
 * \param chan the channel to queue the frame(s) on
 * \param f the frame(s) to queue.  Note that the frame(s) will be duplicated
 *        by this function.  It is the responsibility of the caller to handle
 *        freeing the memory associated with the frame(s) being passed if
 *        necessary.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_queue_frame_head(struct ast_channel *chan, struct ast_frame *f);

/*!
 * \brief Queue a hangup frame
 *
 * \note The channel does not need to be locked before calling this function.
 */
int ast_queue_hangup(struct ast_channel *chan);

/*!
 * \brief Queue a hangup frame with hangupcause set
 *
 * \note The channel does not need to be locked before calling this function.
 * \param[in] chan channel to queue frame onto
 * \param[in] cause the hangup cause
 * \return 0 on success, -1 on error
 * \since 1.6.1
 */
int ast_queue_hangup_with_cause(struct ast_channel *chan, int cause);

/*!
 * \brief Queue a hold frame
 *
 * \param chan channel to queue frame onto
 * \param musicclass The suggested musicclass for the other end to use
 *
 * \note The channel does not need to be locked before calling this function.
 *
 * \retval zero on success
 * \retval non-zero on failure
 */
int ast_queue_hold(struct ast_channel *chan, const char *musicclass);

/*!
 * \brief Queue an unhold frame
 *
 * \param chan channel to queue frame onto
 *
 * \note The channel does not need to be locked before calling this function.
 *
 * \retval zero on success
 * \retval non-zero on failure
 */
int ast_queue_unhold(struct ast_channel *chan);

/*!
 * \brief Queue a control frame without payload
 *
 * \param chan channel to queue frame onto
 * \param control type of control frame
 *
 * \note The channel does not need to be locked before calling this function.
 *
 * \retval zero on success
 * \retval non-zero on failure
 */
int ast_queue_control(struct ast_channel *chan, enum ast_control_frame_type control);

/*!
 * \brief Queue a control frame with payload
 *
 * \param chan channel to queue frame onto
 * \param control type of control frame
 * \param data pointer to payload data to be included in frame
 * \param datalen number of bytes of payload data
 *
 * \retval 0 success
 * \retval non-zero failure
 *
 * \details
 * The supplied payload data is copied into the frame, so the caller's copy
 * is not modified nor freed, and the resulting frame will retain a copy of
 * the data even if the caller frees their local copy.
 *
 * \note This method should be treated as a 'network transport'; in other
 * words, your frames may be transferred across an IAX2 channel to another
 * system, which may be a different endianness than yours. Because of this,
 * you should ensure that either your frames will never be expected to work
 * across systems, or that you always put your payload data into 'network byte
 * order' before calling this function.
 *
 * \note The channel does not need to be locked before calling this function.
 */
int ast_queue_control_data(struct ast_channel *chan, enum ast_control_frame_type control,
			   const void *data, size_t datalen);

/*!
 * \brief Change channel name
 *
 * \pre Absolutely all channels _MUST_ be unlocked before calling this function.
 *
 * \param chan the channel to change the name of
 * \param newname the name to change to
 *
 * \return nothing
 *
 * \note this function must _NEVER_ be used when any channels are locked
 * regardless if it is the channel who's name is being changed or not because
 * it invalidates our channel container locking order... lock container first,
 * then the individual channels, never the other way around.
 */
void ast_change_name(struct ast_channel *chan, const char *newname);

/*!
 * \brief Unlink and release reference to a channel
 *
 * This function will unlink the channel from the global channels container
 * if it is still there and also release the current reference to the channel.
 *
 * \return NULL, convenient for clearing invalid pointers
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \since 1.8
 */
struct ast_channel *ast_channel_release(struct ast_channel *chan);

/*!
 * \brief Requests a channel
 *
 * \param type type of channel to request
 * \param request_cap Format capabilities for requested channel
 * \param assignedids Unique ID to create channel with
 * \param requestor channel asking for data
 * \param addr destination of the call
 * \param cause Cause of failure
 *
 * \details
 * Request a channel of a given type, with addr as optional information used
 * by the low level module
 *
 * \retval NULL failure
 * \retval non-NULL channel on success
 */
struct ast_channel *ast_request(const char *type, struct ast_format_cap *request_cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *addr, int *cause);

enum ast_channel_requestor_relationship {
	/*! The requestor is the future bridge peer of the channel. */
	AST_CHANNEL_REQUESTOR_BRIDGE_PEER,
	/*! The requestor is to be replaced by the channel. */
	AST_CHANNEL_REQUESTOR_REPLACEMENT,
};

/*!
 * \brief Setup new channel accountcodes from the requestor channel after ast_request().
 * \since 13.0.0
 *
 * \param chan New channel to get accountcodes setup.
 * \param requestor Requesting channel to get accountcodes from.
 * \param relationship What the new channel was created for.
 *
 * \pre The chan and requestor channels are already locked.
 *
 * \note Pre-existing accountcodes on chan will be overwritten.
 *
 * \return Nothing
 */
void ast_channel_req_accountcodes(struct ast_channel *chan, const struct ast_channel *requestor, enum ast_channel_requestor_relationship relationship);

/*!
 * \brief Setup new channel accountcodes from the requestor channel after ast_request().
 * \since 13.0.0
 *
 * \param chan New channel to get accountcodes setup.
 * \param requestor Requesting channel to get accountcodes from.
 * \param relationship What the new channel was created for.
 *
 * \pre The chan and requestor channels are already locked.
 *
 * \note Pre-existing accountcodes on chan will not be overwritten.
 *
 * \return Nothing
 */
void ast_channel_req_accountcodes_precious(struct ast_channel *chan, const struct ast_channel *requestor, enum ast_channel_requestor_relationship relationship);

/*!
 * \brief Request a channel of a given type, with data as optional information used
 *        by the low level module and attempt to place a call on it
 *
 * \param type type of channel to request
 * \param cap format capabilities for requested channel
 * \param assignedids Unique Id to assign to channel
 * \param requestor channel asking for data
 * \param addr destination of the call
 * \param timeout maximum amount of time to wait for an answer
 * \param reason why unsuccessful (if unsuccessful)
 * \param cid_num Caller-ID Number
 * \param cid_name Caller-ID Name (ascii)
 *
 * \return Returns an ast_channel on success or no answer, NULL on failure.  Check the value of chan->_state
 * to know if the call was answered or not.
 */
struct ast_channel *ast_request_and_dial(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *addr,
	int timeout, int *reason, const char *cid_num, const char *cid_name);

/*!
 * \brief Request a channel of a given type, with data as optional information used
 * by the low level module and attempt to place a call on it
 * \param type type of channel to request
 * \param cap format capabilities for requested channel
 * \param assignedids Unique Id to assign to channel
 * \param requestor channel requesting data
 * \param addr destination of the call
 * \param timeout maximum amount of time to wait for an answer
 * \param reason why unsuccessful (if unsuccessful)
 * \param cid_num Caller-ID Number
 * \param cid_name Caller-ID Name (ascii)
 * \param oh Outgoing helper
 * \return Returns an ast_channel on success or no answer, NULL on failure.  Check the value of chan->_state
 * to know if the call was answered or not.
 */
struct ast_channel *__ast_request_and_dial(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *addr,
	int timeout, int *reason, const char *cid_num, const char *cid_name, struct outgoing_helper *oh);

/*!
 * \brief Forwards a call to a new channel specified by the original channel's call_forward str.  If possible, the new forwarded channel is created and returned while the original one is terminated.
 * \param caller in channel that requested orig
 * \param orig channel being replaced by the call forward channel
 * \param timeout maximum amount of time to wait for setup of new forward channel
 * \param cap format capabilities for requested channel
 * \param oh outgoing helper used with original channel
 * \param outstate reason why unsuccessful (if uncuccessful)
 * \return Returns the forwarded call's ast_channel on success or NULL on failure
 */
struct ast_channel *ast_call_forward(struct ast_channel *caller, struct ast_channel *orig, int *timeout, struct ast_format_cap *cap, struct outgoing_helper *oh, int *outstate);

/*!
 * \brief Register a channel technology (a new channel driver)
 * Called by a channel module to register the kind of channels it supports.
 * \param tech Structure defining channel technology or "type"
 * \return Returns 0 on success, -1 on failure.
 */
int ast_channel_register(const struct ast_channel_tech *tech);

/*!
 * \brief Unregister a channel technology
 * \param tech Structure defining channel technology or "type" that was previously registered
 * \return No return value.
 */
void ast_channel_unregister(const struct ast_channel_tech *tech);

/*!
 * \brief Get a channel technology structure by name
 * \param name name of technology to find
 * \return a pointer to the structure, or NULL if no matching technology found
 */
const struct ast_channel_tech *ast_get_channel_tech(const char *name);

/*!
 * \brief Hang up a channel
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 * \note This function performs a hard hangup on a channel.  Unlike the soft-hangup, this function
 * performs all stream stopping, etc, on the channel that needs to end.
 * chan is no longer valid after this call.
 * \param chan channel to hang up (NULL tolerant)
 * \return Nothing
 */
void ast_hangup(struct ast_channel *chan);

/*!
 * \brief Soft hangup all active channels.
 * \since 13.3.0
 *
 * \return Nothing
 */
void ast_softhangup_all(void);

/*!
 * \brief Softly hangup up a channel
 *
 * \param chan channel to be soft-hung-up
 * \param reason an AST_SOFTHANGUP_* reason code
 *
 * \details
 * Call the protocol layer, but don't destroy the channel structure
 * (use this if you are trying to
 * safely hangup a channel managed by another thread.
 *
 * \note The channel passed to this function does not need to be locked.
 *
 * \return Returns 0 regardless
 */
int ast_softhangup(struct ast_channel *chan, int reason);

/*!
 * \brief Softly hangup up a channel (no channel lock)
 * \param chan channel to be soft-hung-up
 * \param reason an AST_SOFTHANGUP_* reason code
 */
int ast_softhangup_nolock(struct ast_channel *chan, int reason);

/*!
 * \brief Clear a set of softhangup flags from a channel
 *
 * Never clear a softhangup flag from a channel directly.  Instead,
 * use this function.  This ensures that all aspects of the softhangup
 * process are aborted.
 *
 * \param chan the channel to clear the flag on
 * \param flag the flag or flags to clear
 *
 * \return Nothing.
 */
void ast_channel_clear_softhangup(struct ast_channel *chan, int flag);

/*!
 * \brief Set the source of the hangup in this channel and it's bridge
 *
 * \param chan channel to set the field on
 * \param source a string describing the source of the hangup for this channel
 * \param force
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \since 1.8
 *
 * Hangupsource is generally the channel name that caused the bridge to be
 * hung up, but it can also be other things such as "dialplan/agi"
 * This can then be logged in the CDR or CEL
 */
void ast_set_hangupsource(struct ast_channel *chan, const char *source, int force);

/*! \brief Check to see if a channel is needing hang up
 * \param chan channel on which to check for hang up
 * This function determines if the channel is being requested to be hung up.
 * \return Returns 0 if not, or 1 if hang up is requested (including time-out).
 */
int ast_check_hangup(struct ast_channel *chan);

int ast_check_hangup_locked(struct ast_channel *chan);

/*! \brief This function will check if the bridge needs to be re-evaluated due to
 *         external changes.
 *
 *  \param chan Channel on which to check the unbridge_eval flag
 *
 *  \return Returns 0 if the flag is down or 1 if the flag is up.
 */
int ast_channel_unbridged(struct ast_channel *chan);

/*! \brief ast_channel_unbridged variant. Use this if the channel
 *         is already locked prior to calling.
 *
 *  \param chan Channel on which to check the unbridge flag
 *
 *  \return Returns 0 if the flag is down or 1 if the flag is up.
 */
int ast_channel_unbridged_nolock(struct ast_channel *chan);

/*! \brief Sets the unbridged flag and queues a NULL frame on the channel to trigger
 *         a check by bridge_channel_wait
 *
 *  \param chan Which channel is having its unbridged value set
 *  \param value What the unbridge value is being set to
 */
void ast_channel_set_unbridged(struct ast_channel *chan, int value);

/*! \brief Variant of ast_channel_set_unbridged. Use this if the channel
 *         is already locked prior to calling.
 *
 *  \param chan Which channel is having its unbridged value set
 *  \param value What the unbridge value is being set to
 */
void ast_channel_set_unbridged_nolock(struct ast_channel *chan, int value);

/*!
 * \brief Lock the given channel, then request softhangup on the channel with the given causecode
 * \param chan channel on which to hang up
 * \param causecode cause code to use (Zero if don't use cause code)
 * \return Nothing
 */
void ast_channel_softhangup_withcause_locked(struct ast_channel *chan, int causecode);

/*!
 * \brief Compare a offset with the settings of when to hang a channel up
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds from current time
 * \return 1, 0, or -1
 * \details
 * This function compares a offset from current time with the absolute time
 * out on a channel (when to hang up). If the absolute time out on a channel
 * is earlier than current time plus the offset, it returns 1, if the two
 * time values are equal, it return 0, otherwise, it return -1.
 * \sa ast_channel_cmpwhentohangup_tv()
 * \version 1.6.1 deprecated function (only had seconds precision)
 */
int ast_channel_cmpwhentohangup(struct ast_channel *chan, time_t offset) __attribute__((deprecated));

/*!
 * \brief Compare a offset with the settings of when to hang a channel up
 * \param chan channel on which to check for hangup
 * \param offset offset in seconds and microseconds from current time
 * \return 1, 0, or -1
 * This function compares a offset from current time with the absolute time
 * out on a channel (when to hang up). If the absolute time out on a channel
 * is earlier than current time plus the offset, it returns 1, if the two
 * time values are equal, it return 0, otherwise, it return -1.
 * \since 1.6.1
 */
int ast_channel_cmpwhentohangup_tv(struct ast_channel *chan, struct timeval offset);

/*!
 * \brief Set when to hang a channel up
 *
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds relative to the current time of when to hang up
 *
 * \details
 * This function sets the absolute time out on a channel (when to hang up).
 *
 * \pre chan is locked
 *
 * \return Nothing
 * \sa ast_channel_setwhentohangup_tv()
 * \version 1.6.1 deprecated function (only had seconds precision)
 */
void ast_channel_setwhentohangup(struct ast_channel *chan, time_t offset) __attribute__((deprecated));

/*!
 * \brief Set when to hang a channel up
 *
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds and useconds relative to the current time of when to hang up
 *
 * This function sets the absolute time out on a channel (when to hang up).
 *
 * \pre chan is locked
 *
 * \return Nothing
 * \since 1.6.1
 */
void ast_channel_setwhentohangup_tv(struct ast_channel *chan, struct timeval offset);

/*!
 * \brief Answer a channel
 *
 * \param chan channel to answer
 *
 * \details
 * This function answers a channel and handles all necessary call
 * setup functions.
 *
 * \note The channel passed does not need to be locked, but is locked
 * by the function when needed.
 *
 * \note This function will wait up to 500 milliseconds for media to
 * arrive on the channel before returning to the caller, so that the
 * caller can properly assume the channel is 'ready' for media flow.
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int ast_answer(struct ast_channel *chan);

/*!
 * \brief Answer a channel, if it's not already answered.
 *
 * \param chan channel to answer
 *
 * \details See ast_answer()
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int ast_auto_answer(struct ast_channel *chan);

/*!
 * \brief Answer a channel
 *
 * \param chan channel to answer
 *
 * This function answers a channel and handles all necessary call
 * setup functions.
 *
 * \note The channel passed does not need to be locked, but is locked
 * by the function when needed.
 *
 * \note Unlike ast_answer(), this function will not wait for media
 * flow to begin. The caller should be careful before sending media
 * to the channel before incoming media arrives, as the outgoing
 * media may be lost.
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int ast_raw_answer(struct ast_channel *chan);

/*!
 * \brief Answer a channel, with a selectable delay before returning
 *
 * \param chan channel to answer
 * \param delay maximum amount of time to wait for incoming media
 *
 * This function answers a channel and handles all necessary call
 * setup functions.
 *
 * \note The channel passed does not need to be locked, but is locked
 * by the function when needed.
 *
 * \note This function will wait up to 'delay' milliseconds for media to
 * arrive on the channel before returning to the caller, so that the
 * caller can properly assume the channel is 'ready' for media flow. If
 * 'delay' is less than 500, the function will wait up to 500 milliseconds.
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int __ast_answer(struct ast_channel *chan, unsigned int delay);

/*!
 * \brief Execute a Gosub call on the channel before a call is placed.
 * \since 11.0
 *
 * \details
 * This is called between ast_request() and ast_call() to
 * execute a predial routine on the newly created channel.
 *
 * \param chan Channel to execute Gosub.
 * \param sub_args Gosub application parameter string.
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_pre_call(struct ast_channel *chan, const char *sub_args);

/*!
 * \brief Make a call
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 * \param chan which channel to make the call on
 * \param addr destination of the call
 * \param timeout time to wait on for connect (Doesn't seem to be used.)
 * \details
 * Place a call, take no longer than timeout ms.
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_call(struct ast_channel *chan, const char *addr, int timeout);

/*!
 * \brief Indicates condition of channel
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 * \note Indicate a condition such as AST_CONTROL_BUSY, AST_CONTROL_RINGING, or AST_CONTROL_CONGESTION on a channel
 * \param chan channel to change the indication
 * \param condition which condition to indicate on the channel
 * \return Returns 0 on success, -1 on failure
 */
int ast_indicate(struct ast_channel *chan, int condition);

/*!
 * \brief Indicates condition of channel, with payload
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 * \note Indicate a condition such as AST_CONTROL_HOLD with payload being music on hold class
 * \param chan channel to change the indication
 * \param condition which condition to indicate on the channel
 * \param data pointer to payload data
 * \param datalen size of payload data
 * \return Returns 0 on success, -1 on failure
 */
int ast_indicate_data(struct ast_channel *chan, int condition, const void *data, size_t datalen);

/* Misc stuff ------------------------------------------------ */

/*!
 * \brief Wait for input on a channel
 * \param chan channel to wait on
 * \param ms length of time to wait on the channel
 * \details
 * Wait for input on a channel for a given # of milliseconds (<0 for indefinite).
 * \retval < 0 on failure
 * \retval 0 if nothing ever arrived
 * \retval the # of ms remaining otherwise
 */
int ast_waitfor(struct ast_channel *chan, int ms);

/*!
 * \brief Should we keep this frame for later?
 *
 * There are functions such as ast_safe_sleep which will
 * service a channel to ensure that it does not have a
 * large backlog of queued frames. When this happens,
 * we want to hold on to specific frame types and just drop
 * others. This function will tell if the frame we just
 * read should be held onto.
 *
 * \param frame The frame we just read
 * \retval 1 frame should be kept
 * \retval 0 frame should be dropped
 */
int ast_is_deferrable_frame(const struct ast_frame *frame);

/*!
 * \brief Wait for a specified amount of time, looking for hangups
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep. This should never be less than zero.
 * \details
 * Waits for a specified amount of time, servicing the channel as required.
 * \return returns -1 on hangup, otherwise 0.
 */
int ast_safe_sleep(struct ast_channel *chan, int ms);

/*!
 * \brief Wait for a specified amount of time, looking for hangups and a condition argument
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep.
 * \param cond a function pointer for testing continue condition
 * \param data argument to be passed to the condition test function
 * \return returns -1 on hangup, otherwise 0.
 * \details
 * Waits for a specified amount of time, servicing the channel as required. If cond
 * returns 0, this function returns.
 */
int ast_safe_sleep_conditional(struct ast_channel *chan, int ms, int (*cond)(void*), void *data );

/*!
 * \brief Waits for activity on a group of channels
 * \param chan an array of pointers to channels
 * \param n number of channels that are to be waited upon
 * \param fds an array of fds to wait upon
 * \param nfds the number of fds to wait upon
 * \param exception exception flag
 * \param outfd fd that had activity on it
 * \param ms how long the wait was
 * \details
 * Big momma function here.  Wait for activity on any of the n channels, or any of the nfds
 * file descriptors.
 * \return Returns the channel with activity, or NULL on error or if an FD
 * came first.  If the FD came first, it will be returned in outfd, otherwise, outfd
 * will be -1
 */
struct ast_channel *ast_waitfor_nandfds(struct ast_channel **chan, int n,
	int *fds, int nfds, int *exception, int *outfd, int *ms);

/*!
 * \brief Waits for input on a group of channels
 * Wait for input on an array of channels for a given # of milliseconds.
 * \return Return channel with activity, or NULL if none has activity.
 * \param chan an array of pointers to channels
 * \param n number of channels that are to be waited upon
 * \param ms time "ms" is modified in-place, if applicable
 */
struct ast_channel *ast_waitfor_n(struct ast_channel **chan, int n, int *ms);

/*!
 * \brief Waits for input on an fd
 * \note This version works on fd's only.  Be careful with it.
 */
int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception);


/*!
 * \brief Reads a frame
 * \param chan channel to read a frame from
 * \return Returns a frame, or NULL on error.  If it returns NULL, you
 * best just stop reading frames and assume the channel has been
 * disconnected.
 */
struct ast_frame *ast_read(struct ast_channel *chan);

/*!
 * \brief Reads a frame, returning AST_FRAME_NULL frame if audio.
 * \param chan channel to read a frame from
 * \return  Returns a frame, or NULL on error.  If it returns NULL, you
 * best just stop reading frames and assume the channel has been
 * disconnected.
 * \note Audio is replaced with AST_FRAME_NULL to avoid
 * transcode when the resulting audio is not necessary.
 */
struct ast_frame *ast_read_noaudio(struct ast_channel *chan);

/*!
 * \brief Write a frame to a channel
 * This function writes the given frame to the indicated channel.
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * \return It returns 0 on success, -1 on failure.
 */
int ast_write(struct ast_channel *chan, struct ast_frame *frame);

/*!
 * \brief Write video frame to a channel
 * This function writes the given frame to the indicated channel.
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * \return It returns 1 on success, 0 if not implemented, and -1 on failure.
 */
int ast_write_video(struct ast_channel *chan, struct ast_frame *frame);

/*!
 * \brief Write text frame to a channel
 * This function writes the given frame to the indicated channel.
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * \return It returns 1 on success, 0 if not implemented, and -1 on failure.
 */
int ast_write_text(struct ast_channel *chan, struct ast_frame *frame);

/*! \brief Send empty audio to prime a channel driver */
int ast_prod(struct ast_channel *chan);

/*!
 * \brief Set specific read path on channel.
 * \since 13.4.0
 *
 * \param chan Channel to setup read path.
 * \param raw_format Format to expect from the channel driver.
 * \param core_format What the core wants to read.
 *
 * \pre chan is locked
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_set_read_format_path(struct ast_channel *chan, struct ast_format *raw_format, struct ast_format *core_format);

/*!
 * \brief Sets read format on channel chan from capabilities
 * Set read format for channel to whichever component of "format" is best.
 * \param chan channel to change
 * \param formats new formats to pick from for reading
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_read_format_from_cap(struct ast_channel *chan, struct ast_format_cap *formats);

/*!
 * \brief Sets read format on channel chan
 * \param chan channel to change
 * \param format format to set for reading
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_read_format(struct ast_channel *chan, struct ast_format *format);

/*!
 * \brief Sets write format on channel chan
 * Set write format for channel to whichever component of "format" is best.
 * \param chan channel to change
 * \param formats new formats to pick from for writing
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_write_format_from_cap(struct ast_channel *chan, struct ast_format_cap *formats);

/*!
 * \brief Sets write format on channel chan
 * \param chan channel to change
 * \param format format to set for writing
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_write_format(struct ast_channel *chan, struct ast_format *format);

/*!
 * \brief Sends text to a channel
 *
 * \param chan channel to act upon
 * \param text string of text to send on the channel
 *
 * \details
 * Write text to a display on a channel
 *
 * \note The channel does not need to be locked before calling this function.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_sendtext(struct ast_channel *chan, const char *text);

/*!
 * \brief Receives a text character from a channel
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * \details
 * Read a char of text from a channel
 * \return 0 on success, -1 on failure
 */
int ast_recvchar(struct ast_channel *chan, int timeout);

/*!
 * \brief Send a DTMF digit to a channel.
 * \param chan channel to act upon
 * \param digit the DTMF digit to send, encoded in ASCII
 * \param duration the duration of the digit ending in ms
 * \return 0 on success, -1 on failure
 */
int ast_senddigit(struct ast_channel *chan, char digit, unsigned int duration);

/*!
 * \brief Send a DTMF digit to a channel.
 * \param chan channel to act upon
 * \param digit the DTMF digit to send, encoded in ASCII
 * \return 0 on success, -1 on failure
 */
int ast_senddigit_begin(struct ast_channel *chan, char digit);

/*!
 * \brief Send a DTMF digit to a channel.
 * \param chan channel to act upon
 * \param digit the DTMF digit to send, encoded in ASCII
 * \param duration the duration of the digit ending in ms
 * \return Returns 0 on success, -1 on failure
 */
int ast_senddigit_end(struct ast_channel *chan, char digit, unsigned int duration);

/*!
 * \brief Receives a text string from a channel
 * Read a string of text from a channel
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * \return the received text, or NULL to signify failure.
 */
char *ast_recvtext(struct ast_channel *chan, int timeout);

/*!
 * \brief Waits for a digit
 * \param c channel to wait for a digit on
 * \param ms how many milliseconds to wait (<0 for indefinite).
 * \return Returns <0 on error, 0 on no entry, and the digit on success.
 */
int ast_waitfordigit(struct ast_channel *c, int ms);

/*!
 * \brief Wait for a digit
 * Same as ast_waitfordigit() with audio fd for outputting read audio and ctrlfd to monitor for reading.
 * \param c channel to wait for a digit on
 * \param ms how many milliseconds to wait (<0 for indefinite).
 * \param audiofd audio file descriptor to write to if audio frames are received
 * \param ctrlfd control file descriptor to monitor for reading
 * \return Returns 1 if ctrlfd becomes available
 */
int ast_waitfordigit_full(struct ast_channel *c, int ms, int audiofd, int ctrlfd);

/*!
 * \brief Reads multiple digits
 * \param c channel to read from
 * \param s string to read in to.  Must be at least the size of your length
 * \param len how many digits to read (maximum)
 * \param timeout how long to timeout between digits
 * \param rtimeout timeout to wait on the first digit
 * \param enders digits to end the string
 * \details
 * Read in a digit string "s", max length "len", maximum timeout between
 * digits "timeout" (-1 for none), terminated by anything in "enders".  Give them rtimeout
 * for the first digit.
 * \return Returns 0 on normal return, or 1 on a timeout.  In the case of
 * a timeout, any digits that were read before the timeout will still be available in s.
 * RETURNS 2 in full version when ctrlfd is available, NOT 1
 */
int ast_readstring(struct ast_channel *c, char *s, int len, int timeout, int rtimeout, char *enders);
int ast_readstring_full(struct ast_channel *c, char *s, int len, int timeout, int rtimeout, char *enders, int audiofd, int ctrlfd);

/*! \brief Report DTMF on channel 0 */
#define AST_BRIDGE_DTMF_CHANNEL_0		(1 << 0)
/*! \brief Report DTMF on channel 1 */
#define AST_BRIDGE_DTMF_CHANNEL_1		(1 << 1)


/*!
 * \brief Make the frame formats of two channels compatible.
 *
 * \param chan First channel to make compatible.  Should be the calling party.
 * \param peer Other channel to make compatible.  Should be the called party.
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \details
 * Set two channels to compatible frame formats in both
 * directions.  The path from peer to chan is made compatible
 * first to allow for in-band audio in case the other direction
 * cannot be made compatible.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_channel_make_compatible(struct ast_channel *chan, struct ast_channel *peer);

/*!
 * \brief Bridge two channels together (early)
 * \param c0 first channel to bridge
 * \param c1 second channel to bridge
 * \details
 * Bridge two channels (c0 and c1) together early. This implies either side may not be answered yet.
 * \return Returns 0 on success and -1 if it could not be done
 */
int ast_channel_early_bridge(struct ast_channel *c0, struct ast_channel *c1);

/*!
 * \brief Gives the string form of a given cause code.
 *
 * \param state cause to get the description of
 * \return the text form of the binary cause code given
 */
const char *ast_cause2str(int state) attribute_pure;

/*!
 * \brief Convert the string form of a cause code to a number
 *
 * \param name string form of the cause
 * \return the cause code
 */
int ast_str2cause(const char *name) attribute_pure;

/*!
 * \brief Gives the string form of a given channel state
 *
 * \param ast_channel_state state to get the name of
 * \return the text form of the binary state given
 */
const char *ast_state2str(enum ast_channel_state);

/*!
 * \brief Gives the string form of a given transfer capability
 *
 * \param transfercapability transfer capability to get the name of
 * \return the text form of the binary transfer capability
 */
char *ast_transfercapability2str(int transfercapability) attribute_const;

/*
 * Options: Some low-level drivers may implement "options" allowing fine tuning of the
 * low level channel.  See frame.h for options.  Note that many channel drivers may support
 * none or a subset of those features, and you should not count on this if you want your
 * asterisk application to be portable.  They're mainly useful for tweaking performance
 */

/*!
 * \brief Sets an option on a channel
 *
 * \param channel channel to set options on
 * \param option option to change
 * \param data data specific to option
 * \param datalen length of the data
 * \param block blocking or not
 * \details
 * Set an option on a channel (see frame.h), optionally blocking awaiting the reply
 * \return 0 on success and -1 on failure
 */
int ast_channel_setoption(struct ast_channel *channel, int option, void *data, int datalen, int block);

/*!
 * \brief Checks the value of an option
 *
 * Query the value of an option
 * Works similarly to setoption except only reads the options.
 */
int ast_channel_queryoption(struct ast_channel *channel, int option, void *data, int *datalen, int block);

/*!
 * \brief Checks for HTML support on a channel
 * \return 0 if channel does not support HTML or non-zero if it does
 */
int ast_channel_supports_html(struct ast_channel *channel);

/*!
 * \brief Sends HTML on given channel
 * Send HTML or URL on link.
 * \return 0 on success or -1 on failure
 */
int ast_channel_sendhtml(struct ast_channel *channel, int subclass, const char *data, int datalen);

/*!
 * \brief Sends a URL on a given link
 * Send URL on link.
 * \return 0 on success or -1 on failure
 */
int ast_channel_sendurl(struct ast_channel *channel, const char *url);

/*!
 * \brief Defers DTMF so that you only read things like hangups and audio.
 * \return non-zero if channel was already DTMF-deferred or
 * 0 if channel is just now being DTMF-deferred
 */
int ast_channel_defer_dtmf(struct ast_channel *chan);

/*! Undo defer.  ast_read will return any DTMF characters that were queued */
void ast_channel_undefer_dtmf(struct ast_channel *chan);

/*! \return number of channels available for lookup */
int ast_active_channels(void);

/*! \return the number of channels not yet destroyed */
int ast_undestroyed_channels(void);

/*! Activate a given generator */
int ast_activate_generator(struct ast_channel *chan, struct ast_generator *gen, void *params);

/*! Deactivate an active generator */
void ast_deactivate_generator(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Obtain how long the channel since the channel was created
 *
 * \param chan The channel object
 *
 * \retval 0 if the time value cannot be computed (or you called this really fast)
 * \retval The number of seconds the channel has been up
 */
int ast_channel_get_duration(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Obtain how long it has been since the channel was answered
 *
 * \param chan The channel object
 *
 * \retval 0 if the channel isn't answered (or you called this really fast)
 * \retval The number of seconds the channel has been up
 */
int ast_channel_get_up_time(struct ast_channel *chan);

/*!
 * \brief Set caller ID number, name and ANI and generate AMI event.
 *
 * \note Use ast_channel_set_caller() and ast_channel_set_caller_event() instead.
 * \note The channel does not need to be locked before calling this function.
 */
void ast_set_callerid(struct ast_channel *chan, const char *cid_num, const char *cid_name, const char *cid_ani);

/*!
 * \brief Set the caller id information in the Asterisk channel
 * \since 1.8
 *
 * \param chan Asterisk channel to set caller id information
 * \param caller Caller id information
 * \param update What caller information to update.  NULL if all.
 *
 * \return Nothing
 *
 * \note The channel does not need to be locked before calling this function.
 */
void ast_channel_set_caller(struct ast_channel *chan, const struct ast_party_caller *caller, const struct ast_set_party_caller *update);

/*!
 * \brief Set the caller id information in the Asterisk channel and generate an AMI event
 * if the caller id name or number changed.
 * \since 1.8
 *
 * \param chan Asterisk channel to set caller id information
 * \param caller Caller id information
 * \param update What caller information to update.  NULL if all.
 *
 * \return Nothing
 *
 * \note The channel does not need to be locked before calling this function.
 */
void ast_channel_set_caller_event(struct ast_channel *chan, const struct ast_party_caller *caller, const struct ast_set_party_caller *update);

/*! Set the file descriptor on the channel */
void ast_channel_set_fd(struct ast_channel *chan, int which, int fd);

/*! Add a channel to an optimized waitfor */
void ast_poll_channel_add(struct ast_channel *chan0, struct ast_channel *chan1);

/*! Delete a channel from an optimized waitfor */
void ast_poll_channel_del(struct ast_channel *chan0, struct ast_channel *chan1);

/*! Start a tone going */
int ast_tonepair_start(struct ast_channel *chan, int freq1, int freq2, int duration, int vol);
/*! Stop a tone from playing */
void ast_tonepair_stop(struct ast_channel *chan);
/*! Play a tone pair for a given amount of time */
int ast_tonepair(struct ast_channel *chan, int freq1, int freq2, int duration, int vol);

/*!
 * \brief Automatically service a channel for us...
 *
 * \retval 0 success
 * \retval -1 failure, or the channel is already being autoserviced
 */
int ast_autoservice_start(struct ast_channel *chan);

/*!
 * \brief Stop servicing a channel for us...
 *
 * \note if chan is locked prior to calling ast_autoservice_stop, it
 * is likely that there will be a deadlock between the thread that calls
 * ast_autoservice_stop and the autoservice thread. It is important
 * that chan is not locked prior to this call
 *
 * \param chan
 * \retval 0 success
 * \retval -1 error, or the channel has been hungup
 */
int ast_autoservice_stop(struct ast_channel *chan);

/*!
 * \brief Put chan into autoservice while hanging up peer.
 * \since 11.0
 *
 * \param chan Chan to put into autoservice.
 * \param peer Chan to run hangup handlers and hangup.
 *
 * \return Nothing
 */
void ast_autoservice_chan_hangup_peer(struct ast_channel *chan, struct ast_channel *peer);

/*!
 * \brief Ignore certain frame types
 * \note Normally, we cache DTMF, IMAGE, HTML, TEXT, and CONTROL frames
 * while a channel is in autoservice and queue them up when taken out of
 * autoservice.  When this is not desireable, this API may be used to
 * cause the channel to ignore those frametypes after the channel is put
 * into autoservice, but before autoservice is stopped.
 * \retval 0 success
 * \retval -1 channel is not in autoservice
 */
int ast_autoservice_ignore(struct ast_channel *chan, enum ast_frame_type ftype);

/*!
 * \brief Enable or disable timer ticks for a channel
 *
 * \param c channel
 * \param rate number of timer ticks per second
 * \param func callback function
 * \param data
 *
 * \details
 * If timers are supported, force a scheduled expiration on the
 * timer fd, at which point we call the callback function / data
 *
 * \note Call this function with a rate of 0 to turn off the timer ticks
 *
 * \version 1.6.1 changed samples parameter to rate, accomodates new timing methods
 */
int ast_settimeout(struct ast_channel *c, unsigned int rate, int (*func)(const void *data), void *data);
int ast_settimeout_full(struct ast_channel *c, unsigned int rate, int (*func)(const void *data), void *data, unsigned int is_ao2_obj);

/*!
 * \brief Transfer a channel (if supported).
 * \retval -1 on error
 * \retval 0 if not supported
 * \retval 1 if supported and requested
 * \param chan current channel
 * \param dest destination extension for transfer
 */
int ast_transfer(struct ast_channel *chan, char *dest);

/*!
 * \brief Inherits channel variable from parent to child channel
 * \param parent Parent channel
 * \param child Child channel
 *
 * \details
 * Scans all channel variables in the parent channel, looking for those
 * that should be copied into the child channel.
 * Variables whose names begin with a single '_' are copied into the
 * child channel with the prefix removed.
 * Variables whose names begin with '__' are copied into the child
 * channel with their names unchanged.
 */
void ast_channel_inherit_variables(const struct ast_channel *parent, struct ast_channel *child);

/*!
 * \brief adds a list of channel variables to a channel
 * \param chan the channel
 * \param vars a linked list of variables
 *
 * \pre chan is locked
 *
 * \details
 * Variable names can be for a regular channel variable or a dialplan function
 * that has the ability to be written to.
 */
void ast_set_variables(struct ast_channel *chan, struct ast_variable *vars);

/*!
 * \brief An opaque 'object' structure use by silence generators on channels.
 */
struct ast_silence_generator;

/*!
 * \brief Starts a silence generator on the given channel.
 * \param chan The channel to generate silence on
 * \return An ast_silence_generator pointer, or NULL if an error occurs
 *
 * \details
 * This function will cause SLINEAR silence to be generated on the supplied
 * channel until it is disabled; if the channel cannot be put into SLINEAR
 * mode then the function will fail.
 *
 * \note
 * The pointer returned by this function must be preserved and passed to
 * ast_channel_stop_silence_generator when you wish to stop the silence
 * generation.
 */
struct ast_silence_generator *ast_channel_start_silence_generator(struct ast_channel *chan);

/*!
 * \brief Stops a previously-started silence generator on the given channel.
 * \param chan The channel to operate on
 * \param state The ast_silence_generator pointer return by a previous call to
 * ast_channel_start_silence_generator.
 * \return nothing
 *
 * \details
 * This function will stop the operating silence generator and return the channel
 * to its previous write format.
 */
void ast_channel_stop_silence_generator(struct ast_channel *chan, struct ast_silence_generator *state);

/*!
 * \brief Determine which channel has an older linkedid
 * \param a First channel
 * \param b Second channel
 * \return Returns an ast_channel structure that has oldest linkedid
 */
struct ast_channel *ast_channel_internal_oldest_linkedid(struct ast_channel *a, struct ast_channel *b);

/*!
 * \brief Copy the full linkedid channel id structure from one channel to another
 * \param dest Destination to copy linkedid to
 * \param source Source channel to copy linkedid from
 * \return void
 */
void ast_channel_internal_copy_linkedid(struct ast_channel *dest, struct ast_channel *source);

/*!
 * \brief Swap uniqueid and linkedid beteween two channels
 * \param a First channel
 * \param b Second channel
 * \return void
 *
 * \note
 * This is used in masquerade to exchange identities
 */
void ast_channel_internal_swap_uniqueid_and_linkedid(struct ast_channel *a, struct ast_channel *b);

/*!
 * \brief Swap topics beteween two channels
 * \param a First channel
 * \param b Second channel
 * \return void
 *
 * \note
 * This is used in masquerade to exchange topics for message routing
 */
void ast_channel_internal_swap_topics(struct ast_channel *a, struct ast_channel *b);

/*!
 * \brief Set uniqueid and linkedid string value only (not time)
 * \param chan The channel to set the uniqueid to
 * \param uniqueid The uniqueid to set
 * \param linkedid The linkedid to set
 * \return void
 *
 * \note
 * This is used only by ast_cel_fabricate_channel_from_event()
 * to create a temporary fake channel - time values are invalid
 */
void ast_channel_internal_set_fake_ids(struct ast_channel *chan, const char *uniqueid, const char *linkedid);

/* Misc. functions below */

/*!
 * \brief if fd is a valid descriptor, set *pfd with the descriptor
 * \return Return 1 (not -1!) if added, 0 otherwise (so we can add the
 * return value to the index into the array)
 */
static inline int ast_add_fd(struct pollfd *pfd, int fd)
{
	pfd->fd = fd;
	pfd->events = POLLIN | POLLPRI;
	return fd >= 0;
}

/*! \brief Helper function for migrating select to poll */
static inline int ast_fdisset(struct pollfd *pfds, int fd, int maximum, int *start)
{
	int x;
	int dummy = 0;

	if (fd < 0)
		return 0;
	if (!start)
		start = &dummy;
	for (x = *start; x < maximum; x++)
		if (pfds[x].fd == fd) {
			if (x == *start)
				(*start)++;
			return pfds[x].revents;
		}
	return 0;
}

/*! \brief Retrieves the current T38 state of a channel */
static inline enum ast_t38_state ast_channel_get_t38_state(struct ast_channel *chan)
{
	enum ast_t38_state state = T38_STATE_UNAVAILABLE;
	int datalen = sizeof(state);

	ast_channel_queryoption(chan, AST_OPTION_T38_STATE, &state, &datalen, 0);

	return state;
}

#define CHECK_BLOCKING(c) do { 	 \
	if (ast_test_flag(ast_channel_flags(c), AST_FLAG_BLOCKING)) {\
		ast_debug(1, "Thread %p is blocking '%s', already blocked by thread %p in procedure %s\n", \
			(void *) pthread_self(), ast_channel_name(c), (void *) ast_channel_blocker(c), ast_channel_blockproc(c)); \
	} else { \
		ast_channel_blocker_set((c), pthread_self()); \
		ast_channel_blockproc_set((c), __PRETTY_FUNCTION__); \
		ast_set_flag(ast_channel_flags(c), AST_FLAG_BLOCKING); \
	} } while (0)

ast_group_t ast_get_group(const char *s);

/*! \brief Print call and pickup groups into buffer */
char *ast_print_group(char *buf, int buflen, ast_group_t group);

/*! \brief Opaque struct holding a namedgroups set, i.e. a set of group names */
struct ast_namedgroups;

/*! \brief Create an ast_namedgroups set with group names from comma separated string */
struct ast_namedgroups *ast_get_namedgroups(const char *s);
struct ast_namedgroups *ast_unref_namedgroups(struct ast_namedgroups *groups);
struct ast_namedgroups *ast_ref_namedgroups(struct ast_namedgroups *groups);
/*! \brief Return TRUE if group a and b contain at least one common groupname */
int ast_namedgroups_intersect(struct ast_namedgroups *a, struct ast_namedgroups *b);

/*! \brief Print named call groups and named pickup groups */
char *ast_print_namedgroups(struct ast_str **buf, struct ast_namedgroups *groups);

/*!
 * \brief Convert enum channelreloadreason to text string for manager event
 * \param reason The reason for reload (manager, cli, start etc)
 */
const char *channelreloadreason2txt(enum channelreloadreason reason);

/*! \brief return an ast_variable list of channeltypes */
struct ast_variable *ast_channeltype_list(void);

/*!
 * \brief return an english explanation of the code returned thru __ast_request_and_dial's 'outstate' argument
 * \param reason  The integer argument, usually taken from AST_CONTROL_ macros
 * \return char pointer explaining the code
 */
const char *ast_channel_reason2str(int reason);

/*! \brief channel group info */
struct ast_group_info {
	struct ast_channel *chan;
	char *category;
	char *group;
	AST_LIST_ENTRY(ast_group_info) group_list;
};

#define ast_channel_lock(chan) ao2_lock(chan)
#define ast_channel_unlock(chan) ao2_unlock(chan)
#define ast_channel_trylock(chan) ao2_trylock(chan)

/*!
 * \brief Lock two channels.
 */
#define ast_channel_lock_both(chan1, chan2) do { \
		ast_channel_lock(chan1); \
		while (ast_channel_trylock(chan2)) { \
			ast_channel_unlock(chan1); \
			sched_yield(); \
			ast_channel_lock(chan1); \
		} \
	} while (0)

/*!
 * \brief Increase channel reference count
 *
 * \param c the channel
 *
 * \retval c always
 *
 * \since 1.8
 */
#define ast_channel_ref(c) ({ ao2_ref(c, +1); (c); })

/*!
 * \brief Decrease channel reference count
 *
 * \param c the channel
 *
 * \retval NULL always
 *
 * \since 1.8
 */
#define ast_channel_unref(c) ({ ao2_ref(c, -1); (struct ast_channel *) (NULL); })

/*!
 * \brief Cleanup a channel reference
 *
 * \param c the channel (NULL tolerant)
 *
 * \retval NULL always
 *
 * \since 12.0.0
 */
#define ast_channel_cleanup(c) ({ ao2_cleanup(c); (struct ast_channel *) (NULL); })

/*! Channel Iterating @{ */

/*!
 * \brief A channel iterator
 *
 * This is an opaque type.
 */
struct ast_channel_iterator;

/*!
 * \brief Destroy a channel iterator
 *
 * \param i the itereator to destroy
 *
 * \details
 * This function is used to destroy a channel iterator that was retrieved by
 * using one of the channel_iterator_xxx_new() functions.
 *
 * \return NULL, for convenience to clear out the pointer to the iterator that
 * was just destroyed.
 *
 * \since 1.8
 */
struct ast_channel_iterator *ast_channel_iterator_destroy(struct ast_channel_iterator *i);

/*!
 * \brief Create a new channel iterator based on extension
 *
 * \param exten The extension that channels must be in
 * \param context The context that channels must be in
 *
 * \details
 * After creating an iterator using this function, the ast_channel_iterator_next()
 * function can be used to iterate through all channels that are currently
 * in the specified context and extension.
 *
 * \note You must call ast_channel_iterator_destroy() when done.
 *
 * \retval NULL on failure
 * \retval a new channel iterator based on the specified parameters
 *
 * \since 1.8
 */
struct ast_channel_iterator *ast_channel_iterator_by_exten_new(const char *exten, const char *context);

/*!
 * \brief Create a new channel iterator based on name
 *
 * \param name channel name or channel uniqueid to match
 * \param name_len number of characters in the channel name to match on.  This
 *      would be used to match based on name prefix.  If matching on the full
 *      channel name is desired, then this parameter should be 0.
 *
 * \details
 * After creating an iterator using this function, the ast_channel_iterator_next()
 * function can be used to iterate through all channels that exist that have
 * the specified name or name prefix.
 *
 * \note You must call ast_channel_iterator_destroy() when done.
 *
 * \retval NULL on failure
 * \retval a new channel iterator based on the specified parameters
 *
 * \since 1.8
 */
struct ast_channel_iterator *ast_channel_iterator_by_name_new(const char *name,	size_t name_len);

/*!
 * \brief Create a new channel iterator
 *
 * \details
 * After creating an iterator using this function, the ast_channel_iterator_next()
 * function can be used to iterate through all channels that exist.
 *
 * \note You must call ast_channel_iterator_destroy() when done.
 *
 * \retval NULL on failure
 * \retval a new channel iterator
 *
 * \since 1.8
 */
struct ast_channel_iterator *ast_channel_iterator_all_new(void);

/*!
 * \brief Get the next channel for a channel iterator
 *
 * \param i the channel iterator that was created using one of the
 *  channel_iterator_xxx_new() functions.
 *
 * \details
 * This function should be used to iterate through all channels that match a
 * specified set of parameters that were provided when the iterator was created.
 *
 * \retval the next channel that matches the parameters used when the iterator
 *         was created.
 * \retval NULL, if no more channels match the iterator parameters.
 *
 * \since 1.8
 */
struct ast_channel *ast_channel_iterator_next(struct ast_channel_iterator *i);

/*! @} End channel iterator definitions. */

/*!
 * \brief Call a function with every active channel
 *
 * \details
 * This function executes a callback one time for each active channel on the
 * system.  The channel is provided as an argument to the function.
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 * \since 1.8
 */
struct ast_channel *ast_channel_callback(ao2_callback_data_fn *cb_fn, void *arg,
		void *data, int ao2_flags);

/*! @{ Channel search functions */

/*!
 * \brief Find a channel by name
 *
 * \param name the name or uniqueid of the channel to search for
 *
 * \details
 * Find a channel that has the same name as the provided argument.
 *
 * \retval a channel with the name specified by the argument
 * \retval NULL if no channel was found
 *
 * \since 1.8
 */
struct ast_channel *ast_channel_get_by_name(const char *name);

/*!
 * \brief Find a channel by a name prefix
 *
 * \param name The channel name or uniqueid prefix to search for
 * \param name_len Only search for up to this many characters from the name
 *
 * \details
 * Find a channel that has the same name prefix as specified by the arguments.
 *
 * \retval a channel with the name prefix specified by the arguments
 * \retval NULL if no channel was found
 *
 * \since 1.8
 */
struct ast_channel *ast_channel_get_by_name_prefix(const char *name, size_t name_len);

/*!
 * \brief Find a channel by extension and context
 *
 * \param exten the extension to search for
 * \param context the context to search for
 *
 * \details
 * Return a channel that is currently at the specified extension and context.
 *
 * \retval a channel that is at the specified extension and context
 * \retval NULL if no channel was found
 *
 * \since 1.8
 */
struct ast_channel *ast_channel_get_by_exten(const char *exten, const char *context);

/*! @} End channel search functions. */

/*!
 * \brief Initialize the given name structure.
 * \since 1.8
 *
 * \param init Name structure to initialize.
 *
 * \return Nothing
 */
void ast_party_name_init(struct ast_party_name *init);

/*!
 * \brief Copy the source party name information to the destination party name.
 * \since 1.8
 *
 * \param dest Destination party name
 * \param src Source party name
 *
 * \return Nothing
 */
void ast_party_name_copy(struct ast_party_name *dest, const struct ast_party_name *src);

/*!
 * \brief Initialize the given party name structure using the given guide
 * for a set update operation.
 * \since 1.8
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Party name structure to initialize.
 * \param guide Source party name to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_name_set_init(struct ast_party_name *init, const struct ast_party_name *guide);

/*!
 * \brief Set the source party name information into the destination party name.
 * \since 1.8
 *
 * \param dest The name one wishes to update
 * \param src The new name values to update the dest
 *
 * \return Nothing
 */
void ast_party_name_set(struct ast_party_name *dest, const struct ast_party_name *src);

/*!
 * \brief Destroy the party name contents
 * \since 1.8
 *
 * \param doomed The party name to destroy.
 *
 * \return Nothing
 */
void ast_party_name_free(struct ast_party_name *doomed);

/*!
 * \brief Initialize the given number structure.
 * \since 1.8
 *
 * \param init Number structure to initialize.
 *
 * \return Nothing
 */
void ast_party_number_init(struct ast_party_number *init);

/*!
 * \brief Copy the source party number information to the destination party number.
 * \since 1.8
 *
 * \param dest Destination party number
 * \param src Source party number
 *
 * \return Nothing
 */
void ast_party_number_copy(struct ast_party_number *dest, const struct ast_party_number *src);

/*!
 * \brief Initialize the given party number structure using the given guide
 * for a set update operation.
 * \since 1.8
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Party number structure to initialize.
 * \param guide Source party number to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_number_set_init(struct ast_party_number *init, const struct ast_party_number *guide);

/*!
 * \brief Set the source party number information into the destination party number.
 * \since 1.8
 *
 * \param dest The number one wishes to update
 * \param src The new number values to update the dest
 *
 * \return Nothing
 */
void ast_party_number_set(struct ast_party_number *dest, const struct ast_party_number *src);

/*!
 * \brief Destroy the party number contents
 * \since 1.8
 *
 * \param doomed The party number to destroy.
 *
 * \return Nothing
 */
void ast_party_number_free(struct ast_party_number *doomed);

/*!
 * \since 1.8
 * \brief Initialize the given subaddress structure.
 *
 * \param init Subaddress structure to initialize.
 *
 * \return Nothing
 */
void ast_party_subaddress_init(struct ast_party_subaddress *init);

/*!
 * \since 1.8
 * \brief Copy the source party subaddress information to the destination party subaddress.
 *
 * \param dest Destination party subaddress
 * \param src Source party subaddress
 *
 * \return Nothing
 */
void ast_party_subaddress_copy(struct ast_party_subaddress *dest, const struct ast_party_subaddress *src);

/*!
 * \since 1.8
 * \brief Initialize the given party subaddress structure using the given guide
 * for a set update operation.
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Party subaddress structure to initialize.
 * \param guide Source party subaddress to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_subaddress_set_init(struct ast_party_subaddress *init, const struct ast_party_subaddress *guide);

/*!
 * \since 1.8
 * \brief Set the source party subaddress information into the destination party subaddress.
 *
 * \param dest The subaddress one wishes to update
 * \param src The new subaddress values to update the dest
 *
 * \return Nothing
 */
void ast_party_subaddress_set(struct ast_party_subaddress *dest, const struct ast_party_subaddress *src);

/*!
 * \since 1.8
 * \brief Destroy the party subaddress contents
 *
 * \param doomed The party subaddress to destroy.
 *
 * \return Nothing
 */
void ast_party_subaddress_free(struct ast_party_subaddress *doomed);

/*!
 * \brief Set the update marker to update all information of a corresponding party id.
 * \since 11.0
 *
 * \param update_id The update marker for a corresponding party id.
 *
 * \return Nothing
 */
void ast_set_party_id_all(struct ast_set_party_id *update_id);

/*!
 * \brief Initialize the given party id structure.
 * \since 1.8
 *
 * \param init Party id structure to initialize.
 *
 * \return Nothing
 */
void ast_party_id_init(struct ast_party_id *init);

/*!
 * \brief Copy the source party id information to the destination party id.
 * \since 1.8
 *
 * \param dest Destination party id
 * \param src Source party id
 *
 * \return Nothing
 */
void ast_party_id_copy(struct ast_party_id *dest, const struct ast_party_id *src);

/*!
 * \brief Initialize the given party id structure using the given guide
 * for a set update operation.
 * \since 1.8
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Party id structure to initialize.
 * \param guide Source party id to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_id_set_init(struct ast_party_id *init, const struct ast_party_id *guide);

/*!
 * \brief Set the source party id information into the destination party id.
 * \since 1.8
 *
 * \param dest The id one wishes to update
 * \param src The new id values to update the dest
 * \param update What id information to update.  NULL if all.
 *
 * \return Nothing
 */
void ast_party_id_set(struct ast_party_id *dest, const struct ast_party_id *src, const struct ast_set_party_id *update);

/*!
 * \brief Destroy the party id contents
 * \since 1.8
 *
 * \param doomed The party id to destroy.
 *
 * \return Nothing
 */
void ast_party_id_free(struct ast_party_id *doomed);

/*!
 * \brief Determine the overall presentation value for the given party.
 * \since 1.8
 *
 * \param id Party to determine the overall presentation value.
 *
 * \return Overall presentation value for the given party.
 */
int ast_party_id_presentation(const struct ast_party_id *id);

/*!
 * \brief Invalidate all components of the given party id.
 * \since 11.0
 *
 * \param id The party id to invalidate.
 *
 * \return Nothing
 */
void ast_party_id_invalidate(struct ast_party_id *id);

/*!
 * \brief Destroy and initialize the given party id structure.
 * \since 11.0
 *
 * \param id The party id to reset.
 *
 * \return Nothing
 */
void ast_party_id_reset(struct ast_party_id *id);

/*!
 * \brief Merge a given party id into another given party id.
 * \since 11.0
 *
 * \details
 * This function will generate an effective party id.
 *
 * Each party id component of the party id 'base' is overwritten
 * by components of the party id 'overlay' if the overlay
 * component is marked as valid.  However the component 'tag' of
 * the base party id remains untouched.
 *
 * \param base The party id which is merged.
 * \param overlay The party id which is used to merge into.
 *
 * \return The merged party id as a struct, not as a pointer.
 * \note The merged party id returned is a shallow copy and must not be freed.
 */
struct ast_party_id ast_party_id_merge(struct ast_party_id *base, struct ast_party_id *overlay);

/*!
 * \brief Copy a merge of a given party id into another given party id to a given destination party id.
 * \since 11.0
 *
 * \details
 * Each party id component of the party id 'base' is overwritten by components
 * of the party id 'overlay' if the 'overlay' component is marked as valid.
 * However the component 'tag' of the 'base' party id remains untouched.
 * The result is copied into the given party id 'dest'.
 *
 * \note The resulting merged party id is a real copy and has to be freed.
 *
 * \param dest The resulting merged party id.
 * \param base The party id which is merged.
 * \param overlay The party id which is used to merge into.
 *
 * \return Nothing
 */
void ast_party_id_merge_copy(struct ast_party_id *dest, struct ast_party_id *base, struct ast_party_id *overlay);

/*!
 * \brief Initialize the given dialed structure.
 * \since 1.8
 *
 * \param init Dialed structure to initialize.
 *
 * \return Nothing
 */
void ast_party_dialed_init(struct ast_party_dialed *init);

/*!
 * \brief Copy the source dialed party information to the destination dialed party.
 * \since 1.8
 *
 * \param dest Destination dialed party
 * \param src Source dialed party
 *
 * \return Nothing
 */
void ast_party_dialed_copy(struct ast_party_dialed *dest, const struct ast_party_dialed *src);

/*!
 * \brief Initialize the given dialed structure using the given
 * guide for a set update operation.
 * \since 1.8
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Caller structure to initialize.
 * \param guide Source dialed to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_dialed_set_init(struct ast_party_dialed *init, const struct ast_party_dialed *guide);

/*!
 * \brief Set the dialed information based on another dialed source
 * \since 1.8
 *
 * This is similar to ast_party_dialed_copy, except that NULL values for
 * strings in the src parameter indicate not to update the corresponding dest values.
 *
 * \param dest The dialed one wishes to update
 * \param src The new dialed values to update the dest
 *
 * \return Nada
 */
void ast_party_dialed_set(struct ast_party_dialed *dest, const struct ast_party_dialed *src);

/*!
 * \brief Destroy the dialed party contents
 * \since 1.8
 *
 * \param doomed The dialed party to destroy.
 *
 * \return Nothing
 */
void ast_party_dialed_free(struct ast_party_dialed *doomed);

/*!
 * \since 1.8
 * \brief Initialize the given caller structure.
 *
 * \param init Caller structure to initialize.
 *
 * \return Nothing
 */
void ast_party_caller_init(struct ast_party_caller *init);

/*!
 * \since 1.8
 * \brief Copy the source caller information to the destination caller.
 *
 * \param dest Destination caller
 * \param src Source caller
 *
 * \return Nothing
 */
void ast_party_caller_copy(struct ast_party_caller *dest, const struct ast_party_caller *src);

/*!
 * \brief Initialize the given caller structure using the given
 * guide for a set update operation.
 * \since 1.8
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Caller structure to initialize.
 * \param guide Source caller to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_caller_set_init(struct ast_party_caller *init, const struct ast_party_caller *guide);

/*!
 * \brief Set the caller information based on another caller source
 * \since 1.8
 *
 * This is similar to ast_party_caller_copy, except that NULL values for
 * strings in the src parameter indicate not to update the corresponding dest values.
 *
 * \param dest The caller one wishes to update
 * \param src The new caller values to update the dest
 * \param update What caller information to update.  NULL if all.
 *
 * \return Nada
 */
void ast_party_caller_set(struct ast_party_caller *dest, const struct ast_party_caller *src, const struct ast_set_party_caller *update);

/*!
 * \brief Destroy the caller party contents
 * \since 1.8
 *
 * \param doomed The caller party to destroy.
 *
 * \return Nothing
 */
void ast_party_caller_free(struct ast_party_caller *doomed);

/*!
 * \since 1.8
 * \brief Initialize the given connected line structure.
 *
 * \param init Connected line structure to initialize.
 *
 * \return Nothing
 */
void ast_party_connected_line_init(struct ast_party_connected_line *init);

/*!
 * \since 1.8
 * \brief Copy the source connected line information to the destination connected line.
 *
 * \param dest Destination connected line
 * \param src Source connected line
 *
 * \return Nothing
 */
void ast_party_connected_line_copy(struct ast_party_connected_line *dest, const struct ast_party_connected_line *src);

/*!
 * \since 1.8
 * \brief Initialize the given connected line structure using the given
 * guide for a set update operation.
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Connected line structure to initialize.
 * \param guide Source connected line to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_connected_line_set_init(struct ast_party_connected_line *init, const struct ast_party_connected_line *guide);

/*!
 * \since 1.8
 * \brief Set the connected line information based on another connected line source
 *
 * This is similar to ast_party_connected_line_copy, except that NULL values for
 * strings in the src parameter indicate not to update the corresponding dest values.
 *
 * \param dest The connected line one wishes to update
 * \param src The new connected line values to update the dest
 * \param update What connected line information to update.  NULL if all.
 *
 * \return Nothing
 */
void ast_party_connected_line_set(struct ast_party_connected_line *dest, const struct ast_party_connected_line *src, const struct ast_set_party_connected_line *update);

/*!
 * \since 1.8
 * \brief Collect the caller party information into a connected line structure.
 *
 * \param connected Collected caller information for the connected line
 * \param caller Caller information.
 *
 * \return Nothing
 *
 * \warning This is a shallow copy.
 * \warning DO NOT call ast_party_connected_line_free() on the filled in
 * connected line structure!
 */
void ast_party_connected_line_collect_caller(struct ast_party_connected_line *connected, struct ast_party_caller *caller);

/*!
 * \since 1.8
 * \brief Destroy the connected line information contents
 *
 * \param doomed The connected line information to destroy.
 *
 * \return Nothing
 */
void ast_party_connected_line_free(struct ast_party_connected_line *doomed);

/*!
 * \brief Initialize the given redirecting reason structure
 *
 * \param init Redirecting reason structure to initialize
 *
 * \return Nothing
 */
void ast_party_redirecting_reason_init(struct ast_party_redirecting_reason *init);

/*!
 * \brief Copy the source redirecting reason information to the destination redirecting reason.
 *
 * \param dest Destination redirecting reason
 * \param src Source redirecting reason
 *
 * \return Nothing
 */
void ast_party_redirecting_reason_copy(struct ast_party_redirecting_reason *dest,
		const struct ast_party_redirecting_reason *src);

/*!
 * \brief Initialize the given redirecting reason structure using the given guide
 * for a set update operation.
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Redirecting reason structure to initialize.
 * \param guide Source redirecting reason to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_redirecting_reason_set_init(struct ast_party_redirecting_reason *init,
		const struct ast_party_redirecting_reason *guide);

/*!
 * \brief Set the redirecting reason information based on another redirecting reason source
 *
 * This is similar to ast_party_redirecting_reason_copy, except that NULL values for
 * strings in the src parameter indicate not to update the corresponding dest values.
 *
 * \param dest The redirecting reason one wishes to update
 * \param src The new redirecting reason values to update the dest
 *
 * \return Nothing
 */
void ast_party_redirecting_reason_set(struct ast_party_redirecting_reason *dest,
		const struct ast_party_redirecting_reason *src);

/*!
 * \brief Destroy the redirecting reason contents
 *
 * \param doomed The redirecting reason to destroy.
 *
 * \return Nothing
 */
void ast_party_redirecting_reason_free(struct ast_party_redirecting_reason *doomed);

/*!
 * \brief Initialize the given redirecting structure.
 * \since 1.8
 *
 * \param init Redirecting structure to initialize.
 *
 * \return Nothing
 */
void ast_party_redirecting_init(struct ast_party_redirecting *init);

/*!
 * \since 1.8
 * \brief Copy the source redirecting information to the destination redirecting.
 *
 * \param dest Destination redirecting
 * \param src Source redirecting
 *
 * \return Nothing
 */
void ast_party_redirecting_copy(struct ast_party_redirecting *dest, const struct ast_party_redirecting *src);

/*!
 * \since 1.8
 * \brief Initialize the given redirecting id structure using the given guide
 * for a set update operation.
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Redirecting id structure to initialize.
 * \param guide Source redirecting id to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_redirecting_set_init(struct ast_party_redirecting *init, const struct ast_party_redirecting *guide);

/*!
 * \brief Set the redirecting information based on another redirecting source
 * \since 1.8
 *
 * This is similar to ast_party_redirecting_copy, except that NULL values for
 * strings in the src parameter indicate not to update the corresponding dest values.
 *
 * \param dest The redirecting one wishes to update
 * \param src The new redirecting values to update the dest
 * \param update What redirecting information to update.  NULL if all.
 *
 * \return Nothing
 */
void ast_party_redirecting_set(struct ast_party_redirecting *dest, const struct ast_party_redirecting *src, const struct ast_set_party_redirecting *update);

/*!
 * \since 1.8
 * \brief Destroy the redirecting information contents
 *
 * \param doomed The redirecting information to destroy.
 *
 * \return Nothing
 */
void ast_party_redirecting_free(struct ast_party_redirecting *doomed);

/*!
 * \since 1.8
 * \brief Copy the caller information to the connected line information.
 *
 * \param dest Destination connected line information
 * \param src Source caller information
 *
 * \return Nothing
 *
 * \note Assumes locks are already acquired
 */
void ast_connected_line_copy_from_caller(struct ast_party_connected_line *dest, const struct ast_party_caller *src);

/*!
 * \since 1.8
 * \brief Copy the connected line information to the caller information.
 *
 * \param dest Destination caller information
 * \param src Source connected line information
 *
 * \return Nothing
 *
 * \note Assumes locks are already acquired
 */
void ast_connected_line_copy_to_caller(struct ast_party_caller *dest, const struct ast_party_connected_line *src);

/*!
 * \since 1.8
 * \brief Set the connected line information in the Asterisk channel
 *
 * \param chan Asterisk channel to set connected line information
 * \param connected Connected line information
 * \param update What connected line information to update.  NULL if all.
 *
 * \return Nothing
 *
 * \note The channel does not need to be locked before calling this function.
 */
void ast_channel_set_connected_line(struct ast_channel *chan, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update);

/*!
 * \since 1.8
 * \brief Build the connected line information data frame.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param connected Connected line information
 * \param update What connected line information to build.  NULL if all.
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
int ast_connected_line_build_data(unsigned char *data, size_t datalen, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update);

/*!
 * \since 1.8
 * \brief Parse connected line indication frame data
 *
 * \param data Buffer with the frame data to parse
 * \param datalen Size of the buffer
 * \param connected Extracted connected line information
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note The filled in connected line structure needs to be initialized by
 * ast_party_connected_line_set_init() before calling.  If defaults are not
 * required use ast_party_connected_line_init().
 * \note The filled in connected line structure needs to be destroyed by
 * ast_party_connected_line_free() when it is no longer needed.
 */
int ast_connected_line_parse_data(const unsigned char *data, size_t datalen, struct ast_party_connected_line *connected);

/*!
 * \since 1.8
 * \brief Indicate that the connected line information has changed
 *
 * \param chan Asterisk channel to indicate connected line information
 * \param connected Connected line information
 * \param update What connected line information to update.  NULL if all.
 *
 * \return Nothing
 */
void ast_channel_update_connected_line(struct ast_channel *chan, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update);

/*!
 * \since 1.8
 * \brief Queue a connected line update frame on a channel
 *
 * \param chan Asterisk channel to indicate connected line information
 * \param connected Connected line information
 * \param update What connected line information to update.  NULL if all.
 *
 * \return Nothing
 */
void ast_channel_queue_connected_line_update(struct ast_channel *chan, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update);

/*!
 * \since 1.8
 * \brief Set the redirecting id information in the Asterisk channel
 *
 * \param chan Asterisk channel to set redirecting id information
 * \param redirecting Redirecting id information
 * \param update What redirecting information to update.  NULL if all.
 *
 * \return Nothing
 *
 * \note The channel does not need to be locked before calling this function.
 */
void ast_channel_set_redirecting(struct ast_channel *chan, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update);

/*!
 * \since 1.8
 * \brief Build the redirecting id data frame.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param redirecting Redirecting id information
 * \param update What redirecting information to build.  NULL if all.
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
int ast_redirecting_build_data(unsigned char *data, size_t datalen, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update);

/*!
 * \since 1.8
 * \brief Parse redirecting indication frame data
 *
 * \param data Buffer with the frame data to parse
 * \param datalen Size of the buffer
 * \param redirecting Extracted redirecting id information
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note The filled in id structure needs to be initialized by
 * ast_party_redirecting_set_init() before calling.
 * \note The filled in id structure needs to be destroyed by
 * ast_party_redirecting_free() when it is no longer needed.
 */
int ast_redirecting_parse_data(const unsigned char *data, size_t datalen, struct ast_party_redirecting *redirecting);

/*!
 * \since 1.8
 * \brief Indicate that the redirecting id has changed
 *
 * \param chan Asterisk channel to indicate redirecting id information
 * \param redirecting Redirecting id information
 * \param update What redirecting information to update.  NULL if all.
 *
 * \return Nothing
 */
void ast_channel_update_redirecting(struct ast_channel *chan, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update);

/*!
 * \since 1.8
 * \brief Queue a redirecting update frame on a channel
 *
 * \param chan Asterisk channel to indicate redirecting id information
 * \param redirecting Redirecting id information
 * \param update What redirecting information to update.  NULL if all.
 *
 * \return Nothing
 */
void ast_channel_queue_redirecting_update(struct ast_channel *chan, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update);

/*!
 * \since 1.8
 * \brief Run a connected line interception macro and update a channel's connected line
 * information
 * \deprecated You should use the ast_channel_connected_line_sub() function instead.
 *
 * Whenever we want to update a channel's connected line information, we may need to run
 * a macro so that an administrator can manipulate the information before sending it
 * out. This function both runs the macro and sends the update to the channel.
 *
 * \param autoservice_chan Channel to place into autoservice while the macro is running.
 * It is perfectly safe for this to be NULL
 * \param macro_chan The channel to run the macro on. Also the channel from which we
 * determine which macro we need to run.
 * \param connected_info Either an ast_party_connected_line or ast_frame pointer of type
 * AST_CONTROL_CONNECTED_LINE
 * \param is_caller If true, then run CONNECTED_LINE_CALLER_SEND_MACRO with arguments from
 * CONNECTED_LINE_CALLER_SEND_MACRO_ARGS, otherwise run CONNECTED_LINE_CALLEE_SEND_MACRO
 * with arguments from CONNECTED_LINE_CALLEE_SEND_MACRO_ARGS
 * \param frame If true, then connected_info is an ast_frame pointer, otherwise it is an
 * ast_party_connected_line pointer.
 * \retval 0 Success
 * \retval -1 Either the macro does not exist, or there was an error while attempting to
 * run the macro
 *
 * \todo Have multiple return codes based on the MACRO_RESULT
 * \todo Make constants so that caller and frame can be more expressive than just '1' and
 * '0'
 */
int ast_channel_connected_line_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const void *connected_info, int is_caller, int frame);

/*!
 * \since 11
 * \brief Run a connected line interception subroutine and update a channel's connected line
 * information
 *
 * Whenever we want to update a channel's connected line information, we may need to run
 * a subroutine so that an administrator can manipulate the information before sending it
 * out. This function both runs the subroutine specified by CONNECTED_LINE_SEND_SUB and
 * sends the update to the channel.
 *
 * \param autoservice_chan Channel to place into autoservice while the sub is running.
 * It is perfectly safe for this to be NULL
 * \param sub_chan The channel to run the subroutine on. Also the channel from which we
 * determine which subroutine we need to run.
 * \param connected_info Either an ast_party_connected_line or ast_frame pointer of type
 * AST_CONTROL_CONNECTED_LINE
 * \param frame If true, then connected_info is an ast_frame pointer, otherwise it is an
 * ast_party_connected_line pointer.
 * \retval 0 Success
 * \retval -1 Either the subroutine does not exist, or there was an error while attempting to
 * run the subroutine
 */
int ast_channel_connected_line_sub(struct ast_channel *autoservice_chan, struct ast_channel *sub_chan, const void *connected_info, int frame);

/*!
 * \brief Insert into an astdata tree, the channel structure.
 * \param[in] tree The ast data tree.
 * \param[in] chan The channel structure to add to tree.
 * \param[in] add_bridged Add the bridged channel to the structure.
 * \retval <0 on error.
 * \retval 0 on success.
 */
int ast_channel_data_add_structure(struct ast_data *tree, struct ast_channel *chan, int add_bridged);

/*!
 * \brief Compare to channel structures using the data api.
 * \param[in] tree The search tree generated by the data api.
 * \param[in] chan The channel to compare.
 * \param[in] structure_name The name of the node of the channel structure.
 * \retval 0 The structure matches.
 * \retval 1 The structure doesn't matches.
 */
int ast_channel_data_cmp_structure(const struct ast_data_search *tree, struct ast_channel *chan,
	const char *structure_name);

/*!
 * \since 1.8
 * \brief Run a redirecting interception macro and update a channel's redirecting information
 * \deprecated You should use the ast_channel_redirecting_sub() function instead.
 *
 * \details
 * Whenever we want to update a channel's redirecting information, we may need to run
 * a macro so that an administrator can manipulate the information before sending it
 * out. This function both runs the macro and sends the update to the channel.
 *
 * \param autoservice_chan Channel to place into autoservice while the macro is running.
 * It is perfectly safe for this to be NULL
 * \param macro_chan The channel to run the macro on. Also the channel from which we
 * determine which macro we need to run.
 * \param redirecting_info Either an ast_party_redirecting or ast_frame pointer of type
 * AST_CONTROL_REDIRECTING
 * \param is_caller If true, then run REDIRECTING_CALLER_SEND_MACRO with arguments from
 * REDIRECTING_CALLER_SEND_MACRO_ARGS, otherwise run REDIRECTING_CALLEE_SEND_MACRO with
 * arguments from REDIRECTING_CALLEE_SEND_MACRO_ARGS
 * \param is_frame If true, then redirecting_info is an ast_frame pointer, otherwise it is an
 * ast_party_redirecting pointer.
 *
 * \retval 0 Success
 * \retval -1 Either the macro does not exist, or there was an error while attempting to
 * run the macro
 *
 * \todo Have multiple return codes based on the MACRO_RESULT
 * \todo Make constants so that caller and frame can be more expressive than just '1' and
 * '0'
 */
int ast_channel_redirecting_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const void *redirecting_info, int is_caller, int is_frame);

/*!
 * \since 11
 * \brief Run a redirecting interception subroutine and update a channel's redirecting information
 *
 * \details
 * Whenever we want to update a channel's redirecting information, we may need to run
 * a subroutine so that an administrator can manipulate the information before sending it
 * out. This function both runs the subroutine specified by REDIRECTING_SEND_SUB and
 * sends the update to the channel.
 *
 * \param autoservice_chan Channel to place into autoservice while the subroutine is running.
 * It is perfectly safe for this to be NULL
 * \param sub_chan The channel to run the subroutine on. Also the channel from which we
 * determine which subroutine we need to run.
 * \param redirecting_info Either an ast_party_redirecting or ast_frame pointer of type
 * AST_CONTROL_REDIRECTING
 * \param is_frame If true, then redirecting_info is an ast_frame pointer, otherwise it is an
 * ast_party_redirecting pointer.
 *
 * \retval 0 Success
 * \retval -1 Either the subroutine does not exist, or there was an error while attempting to
 * run the subroutine
 */
int ast_channel_redirecting_sub(struct ast_channel *autoservice_chan, struct ast_channel *sub_chan, const void *redirecting_info, int is_frame);

#include "asterisk/ccss.h"

/*!
 * \since 1.8
 * \brief Set up datastore with CCSS parameters for a channel
 *
 * \note
 * If base_params is NULL, the channel will get the default
 * values for all CCSS parameters.
 *
 * \details
 * This function makes use of datastore operations on the channel, so
 * it is important to lock the channel before calling this function.
 *
 * \param chan The channel to create the datastore on
 * \param base_params CCSS parameters we wish to copy into the channel
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_channel_cc_params_init(struct ast_channel *chan,
		const struct ast_cc_config_params *base_params);

/*!
 * \since 1.8
 * \brief Get the CCSS parameters from a channel
 *
 * \details
 * This function makes use of datastore operations on the channel, so
 * it is important to lock the channel before calling this function.
 *
 * \param chan Channel to retrieve parameters from
 * \retval NULL Failure
 * \retval non-NULL The parameters desired
 */
struct ast_cc_config_params *ast_channel_get_cc_config_params(struct ast_channel *chan);


/*!
 * \since 1.8
 * \brief Get a device name given its channel structure
 *
 * \details
 * A common practice in Asterisk is to determine the device being talked
 * to by dissecting the channel name. For certain channel types, this is not
 * accurate. For instance, an ISDN channel is named based on what B channel is
 * used, not the device being communicated with.
 *
 * This function interfaces with a channel tech's queryoption callback to
 * retrieve the name of the device being communicated with. If the channel does not
 * implement this specific option, then the traditional method of using the channel
 * name is used instead.
 *
 * \param chan The channel to retrieve the information from
 * \param[out] device_name The buffer to place the device's name into
 * \param name_buffer_length The allocated space for the device_name
 * \return 0 always
 */
int ast_channel_get_device_name(struct ast_channel *chan, char *device_name, size_t name_buffer_length);

/*!
 * \since 1.8
 * \brief Find the appropriate CC agent type to use given a channel
 *
 * \details
 * During call completion, we will need to create a call completion agent structure. To
 * figure out the type of agent to construct, we need to ask the channel driver for the
 * appropriate type.
 *
 * Prior to adding this function, the call completion core attempted to figure this
 * out for itself by stripping the technology off the channel's name. However, in the
 * case of chan_dahdi, there are multiple agent types registered, and so simply searching
 * for an agent type called "DAHDI" is not possible. In a case where multiple agent types
 * are defined, the channel driver must have a queryoption callback defined in its
 * channel_tech, and the queryoption callback must handle AST_OPTION_CC_AGENT_TYPE
 *
 * If a channel driver does not have a queryoption callback or if the queryoption callback
 * does not handle AST_OPTION_CC_AGENT_TYPE, then the old behavior of using the technology
 * portion of the channel name is used instead. This is perfectly suitable for channel drivers
 * whose channel technologies are a one-to-one match with the agent types defined within.
 *
 * Note that this function is only called when the agent policy on a given channel is set
 * to "native." Generic agents' type can be determined automatically by the core.
 *
 * \param chan The channel for which we wish to retrieve the agent type
 * \param[out] agent_type The type of agent the channel driver wants us to use
 * \param size The size of the buffer to write to
 */
int ast_channel_get_cc_agent_type(struct ast_channel *chan, char *agent_type, size_t size);
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

/*!
 * \brief Remove a channel from the global channels container
 *
 * \param chan channel to remove
 *
 * In a case where it is desired that a channel not be available in any lookups
 * in the global channels conatiner, use this function.
 */
void ast_channel_unlink(struct ast_channel *chan);

/*!
 * \brief Sets the HANGUPCAUSE hash and optionally the SIP_CAUSE hash
 * on the given channel
 *
 * \param chan channel on which to set the cause information
 * \param cause_code ast_control_pvt_cause_code structure containing cause information
 * \param datalen total length of the structure since it may vary
 */
void ast_channel_hangupcause_hash_set(struct ast_channel *chan, const struct ast_control_pvt_cause_code *cause_code, int datalen);

/*!
 * \since 12
 * \brief Convert a string to a detail record AMA flag
 *
 * \param flag string form of flag
 *
 * \retval the enum (integer) form of the flag
 */
enum ama_flags ast_channel_string2amaflag(const char *flag);

/*!
 * \since 12
 * \brief Convert the enum representation of an AMA flag to a string representation
 *
 * \param flags integer flag
 *
 * \retval A string representation of the flag
 */
const char *ast_channel_amaflags2string(enum ama_flags flags);

/* ACCESSOR FUNTIONS */
/*! \brief Set the channel name */
void ast_channel_name_set(struct ast_channel *chan, const char *name);

#define DECLARE_STRINGFIELD_SETTERS_FOR(field)	\
	void ast_channel_##field##_set(struct ast_channel *chan, const char *field); \
	void ast_channel_##field##_build_va(struct ast_channel *chan, const char *fmt, va_list ap) __attribute__((format(printf, 2, 0))); \
	void ast_channel_##field##_build(struct ast_channel *chan, const char *fmt, ...) __attribute__((format(printf, 2, 3)))

/*!
 * The following string fields result in channel snapshot creation and
 * should have the channel locked when called:
 *
 * \li language
 * \li accountcode
 * \li peeraccount
 * \li linkedid
 */
DECLARE_STRINGFIELD_SETTERS_FOR(name);
DECLARE_STRINGFIELD_SETTERS_FOR(language);
DECLARE_STRINGFIELD_SETTERS_FOR(musicclass);
DECLARE_STRINGFIELD_SETTERS_FOR(latest_musicclass);
DECLARE_STRINGFIELD_SETTERS_FOR(accountcode);
DECLARE_STRINGFIELD_SETTERS_FOR(peeraccount);
DECLARE_STRINGFIELD_SETTERS_FOR(userfield);
DECLARE_STRINGFIELD_SETTERS_FOR(call_forward);
DECLARE_STRINGFIELD_SETTERS_FOR(uniqueid);
DECLARE_STRINGFIELD_SETTERS_FOR(linkedid);
DECLARE_STRINGFIELD_SETTERS_FOR(parkinglot);
DECLARE_STRINGFIELD_SETTERS_FOR(hangupsource);
DECLARE_STRINGFIELD_SETTERS_FOR(dialcontext);

const char *ast_channel_name(const struct ast_channel *chan);
const char *ast_channel_language(const struct ast_channel *chan);
const char *ast_channel_musicclass(const struct ast_channel *chan);
const char *ast_channel_latest_musicclass(const struct ast_channel *chan);
const char *ast_channel_accountcode(const struct ast_channel *chan);
const char *ast_channel_peeraccount(const struct ast_channel *chan);
const char *ast_channel_userfield(const struct ast_channel *chan);
const char *ast_channel_call_forward(const struct ast_channel *chan);
const char *ast_channel_uniqueid(const struct ast_channel *chan);
const char *ast_channel_linkedid(const struct ast_channel *chan);
const char *ast_channel_parkinglot(const struct ast_channel *chan);
const char *ast_channel_hangupsource(const struct ast_channel *chan);
const char *ast_channel_dialcontext(const struct ast_channel *chan);

const char *ast_channel_appl(const struct ast_channel *chan);
void ast_channel_appl_set(struct ast_channel *chan, const char *value);
const char *ast_channel_blockproc(const struct ast_channel *chan);
void ast_channel_blockproc_set(struct ast_channel *chan, const char *value);
const char *ast_channel_data(const struct ast_channel *chan);
void ast_channel_data_set(struct ast_channel *chan, const char *value);

const char *ast_channel_context(const struct ast_channel *chan);
void ast_channel_context_set(struct ast_channel *chan, const char *value);
const char *ast_channel_exten(const struct ast_channel *chan);
void ast_channel_exten_set(struct ast_channel *chan, const char *value);
const char *ast_channel_macrocontext(const struct ast_channel *chan);
void ast_channel_macrocontext_set(struct ast_channel *chan, const char *value);
const char *ast_channel_macroexten(const struct ast_channel *chan);
void ast_channel_macroexten_set(struct ast_channel *chan, const char *value);

char ast_channel_dtmf_digit_to_emulate(const struct ast_channel *chan);
void ast_channel_dtmf_digit_to_emulate_set(struct ast_channel *chan, char value);
char ast_channel_sending_dtmf_digit(const struct ast_channel *chan);
void ast_channel_sending_dtmf_digit_set(struct ast_channel *chan, char value);
struct timeval ast_channel_sending_dtmf_tv(const struct ast_channel *chan);
void ast_channel_sending_dtmf_tv_set(struct ast_channel *chan, struct timeval value);
enum ama_flags ast_channel_amaflags(const struct ast_channel *chan);

/*!
 * \pre chan is locked
 */
void ast_channel_amaflags_set(struct ast_channel *chan, enum ama_flags value);
int ast_channel_epfd(const struct ast_channel *chan);
void ast_channel_epfd_set(struct ast_channel *chan, int value);
int ast_channel_fdno(const struct ast_channel *chan);
void ast_channel_fdno_set(struct ast_channel *chan, int value);
int ast_channel_hangupcause(const struct ast_channel *chan);
void ast_channel_hangupcause_set(struct ast_channel *chan, int value);
int ast_channel_macropriority(const struct ast_channel *chan);
void ast_channel_macropriority_set(struct ast_channel *chan, int value);
int ast_channel_priority(const struct ast_channel *chan);
void ast_channel_priority_set(struct ast_channel *chan, int value);
int ast_channel_rings(const struct ast_channel *chan);
void ast_channel_rings_set(struct ast_channel *chan, int value);
int ast_channel_streamid(const struct ast_channel *chan);
void ast_channel_streamid_set(struct ast_channel *chan, int value);
int ast_channel_timingfd(const struct ast_channel *chan);
void ast_channel_timingfd_set(struct ast_channel *chan, int value);
int ast_channel_visible_indication(const struct ast_channel *chan);
void ast_channel_visible_indication_set(struct ast_channel *chan, int value);
int ast_channel_hold_state(const struct ast_channel *chan);
void ast_channel_hold_state_set(struct ast_channel *chan, int value);
int ast_channel_vstreamid(const struct ast_channel *chan);
void ast_channel_vstreamid_set(struct ast_channel *chan, int value);
unsigned short ast_channel_transfercapability(const struct ast_channel *chan);
void ast_channel_transfercapability_set(struct ast_channel *chan, unsigned short value);
unsigned int ast_channel_emulate_dtmf_duration(const struct ast_channel *chan);
void ast_channel_emulate_dtmf_duration_set(struct ast_channel *chan, unsigned int value);
unsigned int ast_channel_fin(const struct ast_channel *chan);
void ast_channel_fin_set(struct ast_channel *chan, unsigned int value);
unsigned int ast_channel_fout(const struct ast_channel *chan);
void ast_channel_fout_set(struct ast_channel *chan, unsigned int value);
unsigned long ast_channel_insmpl(const struct ast_channel *chan);
void ast_channel_insmpl_set(struct ast_channel *chan, unsigned long value);
unsigned long ast_channel_outsmpl(const struct ast_channel *chan);
void ast_channel_outsmpl_set(struct ast_channel *chan, unsigned long value);
void *ast_channel_generatordata(const struct ast_channel *chan);
void ast_channel_generatordata_set(struct ast_channel *chan, void *value);
void *ast_channel_music_state(const struct ast_channel *chan);
void ast_channel_music_state_set(struct ast_channel *chan, void *value);
void *ast_channel_tech_pvt(const struct ast_channel *chan);
void ast_channel_tech_pvt_set(struct ast_channel *chan, void *value);
void *ast_channel_timingdata(const struct ast_channel *chan);
void ast_channel_timingdata_set(struct ast_channel *chan, void *value);
struct ast_audiohook_list *ast_channel_audiohooks(const struct ast_channel *chan);
void ast_channel_audiohooks_set(struct ast_channel *chan, struct ast_audiohook_list *value);
struct ast_cdr *ast_channel_cdr(const struct ast_channel *chan);
void ast_channel_cdr_set(struct ast_channel *chan, struct ast_cdr *value);
struct ast_channel *ast_channel__bridge(const struct ast_channel *chan);
void ast_channel__bridge_set(struct ast_channel *chan, struct ast_channel *value);
struct ast_channel *ast_channel_masq(const struct ast_channel *chan);
void ast_channel_masq_set(struct ast_channel *chan, struct ast_channel *value);
struct ast_channel *ast_channel_masqr(const struct ast_channel *chan);
void ast_channel_masqr_set(struct ast_channel *chan, struct ast_channel *value);
struct ast_channel_monitor *ast_channel_monitor(const struct ast_channel *chan);
void ast_channel_monitor_set(struct ast_channel *chan, struct ast_channel_monitor *value);
struct ast_filestream *ast_channel_stream(const struct ast_channel *chan);
void ast_channel_stream_set(struct ast_channel *chan, struct ast_filestream *value);
struct ast_filestream *ast_channel_vstream(const struct ast_channel *chan);
void ast_channel_vstream_set(struct ast_channel *chan, struct ast_filestream *value);
struct ast_format_cap *ast_channel_nativeformats(const struct ast_channel *chan);
void ast_channel_nativeformats_set(struct ast_channel *chan, struct ast_format_cap *value);
struct ast_framehook_list *ast_channel_framehooks(const struct ast_channel *chan);
void ast_channel_framehooks_set(struct ast_channel *chan, struct ast_framehook_list *value);
struct ast_generator *ast_channel_generator(const struct ast_channel *chan);
void ast_channel_generator_set(struct ast_channel *chan, struct ast_generator *value);
struct ast_pbx *ast_channel_pbx(const struct ast_channel *chan);
void ast_channel_pbx_set(struct ast_channel *chan, struct ast_pbx *value);
struct ast_sched_context *ast_channel_sched(const struct ast_channel *chan);
void ast_channel_sched_set(struct ast_channel *chan, struct ast_sched_context *value);
struct ast_timer *ast_channel_timer(const struct ast_channel *chan);
void ast_channel_timer_set(struct ast_channel *chan, struct ast_timer *value);
struct ast_tone_zone *ast_channel_zone(const struct ast_channel *chan);
void ast_channel_zone_set(struct ast_channel *chan, struct ast_tone_zone *value);
struct ast_trans_pvt *ast_channel_readtrans(const struct ast_channel *chan);
void ast_channel_readtrans_set(struct ast_channel *chan, struct ast_trans_pvt *value);
struct ast_trans_pvt *ast_channel_writetrans(const struct ast_channel *chan);
void ast_channel_writetrans_set(struct ast_channel *chan, struct ast_trans_pvt *value);
const struct ast_channel_tech *ast_channel_tech(const struct ast_channel *chan);
void ast_channel_tech_set(struct ast_channel *chan, const struct ast_channel_tech *value);
enum ast_channel_adsicpe ast_channel_adsicpe(const struct ast_channel *chan);
void ast_channel_adsicpe_set(struct ast_channel *chan, enum ast_channel_adsicpe value);
enum ast_channel_state ast_channel_state(const struct ast_channel *chan);
struct ast_callid *ast_channel_callid(const struct ast_channel *chan);

/*!
 * \pre chan is locked
 */
void ast_channel_callid_set(struct ast_channel *chan, struct ast_callid *value);

/* XXX Internal use only, make sure to move later */
void ast_channel_state_set(struct ast_channel *chan, enum ast_channel_state);
void ast_channel_softhangup_internal_flag_set(struct ast_channel *chan, int value);
void ast_channel_softhangup_internal_flag_add(struct ast_channel *chan, int value);
void ast_channel_softhangup_internal_flag_clear(struct ast_channel *chan, int value);
void ast_channel_callid_cleanup(struct ast_channel *chan);
int ast_channel_softhangup_internal_flag(struct ast_channel *chan);

/* Format getters */
struct ast_format *ast_channel_oldwriteformat(struct ast_channel *chan);
struct ast_format *ast_channel_rawreadformat(struct ast_channel *chan);
struct ast_format *ast_channel_rawwriteformat(struct ast_channel *chan);
struct ast_format *ast_channel_readformat(struct ast_channel *chan);
struct ast_format *ast_channel_writeformat(struct ast_channel *chan);

/* Format setters - all of these functions will increment the reference count of the format passed in */
void ast_channel_set_oldwriteformat(struct ast_channel *chan, struct ast_format *format);
void ast_channel_set_rawreadformat(struct ast_channel *chan, struct ast_format *format);
void ast_channel_set_rawwriteformat(struct ast_channel *chan, struct ast_format *format);
void ast_channel_set_readformat(struct ast_channel *chan, struct ast_format *format);
void ast_channel_set_writeformat(struct ast_channel *chan, struct ast_format *format);

/* Other struct getters */
struct ast_frame *ast_channel_dtmff(struct ast_channel *chan);
struct ast_jb *ast_channel_jb(struct ast_channel *chan);
struct ast_party_caller *ast_channel_caller(struct ast_channel *chan);
struct ast_party_connected_line *ast_channel_connected(struct ast_channel *chan);
struct ast_party_connected_line *ast_channel_connected_indicated(struct ast_channel *chan);
struct ast_party_id ast_channel_connected_effective_id(struct ast_channel *chan);
struct ast_party_dialed *ast_channel_dialed(struct ast_channel *chan);
struct ast_party_redirecting *ast_channel_redirecting(struct ast_channel *chan);
struct ast_party_id ast_channel_redirecting_effective_orig(struct ast_channel *chan);
struct ast_party_id ast_channel_redirecting_effective_from(struct ast_channel *chan);
struct ast_party_id ast_channel_redirecting_effective_to(struct ast_channel *chan);
struct timeval *ast_channel_dtmf_tv(struct ast_channel *chan);
struct timeval *ast_channel_whentohangup(struct ast_channel *chan);
struct varshead *ast_channel_varshead(struct ast_channel *chan);

void ast_channel_dtmff_set(struct ast_channel *chan, struct ast_frame *value);
void ast_channel_jb_set(struct ast_channel *chan, struct ast_jb *value);
void ast_channel_caller_set(struct ast_channel *chan, struct ast_party_caller *value);
void ast_channel_connected_set(struct ast_channel *chan, struct ast_party_connected_line *value);
void ast_channel_dialed_set(struct ast_channel *chan, struct ast_party_dialed *value);
void ast_channel_redirecting_set(struct ast_channel *chan, struct ast_party_redirecting *value);
void ast_channel_dtmf_tv_set(struct ast_channel *chan, struct timeval *value);

/*!
 * \pre chan is locked
 */
void ast_channel_whentohangup_set(struct ast_channel *chan, struct timeval *value);
void ast_channel_varshead_set(struct ast_channel *chan, struct varshead *value);
struct timeval ast_channel_creationtime(struct ast_channel *chan);
void ast_channel_creationtime_set(struct ast_channel *chan, struct timeval *value);
struct timeval ast_channel_answertime(struct ast_channel *chan);
void ast_channel_answertime_set(struct ast_channel *chan, struct timeval *value);

/* List getters */
struct ast_hangup_handler_list *ast_channel_hangup_handlers(struct ast_channel *chan);
struct ast_datastore_list *ast_channel_datastores(struct ast_channel *chan);
struct ast_autochan_list *ast_channel_autochans(struct ast_channel *chan);
struct ast_readq_list *ast_channel_readq(struct ast_channel *chan);

/* Typedef accessors */
ast_group_t ast_channel_callgroup(const struct ast_channel *chan);
/*!
 * \pre chan is locked
 */
void ast_channel_callgroup_set(struct ast_channel *chan, ast_group_t value);
ast_group_t ast_channel_pickupgroup(const struct ast_channel *chan);
/*!
 * \pre chan is locked
 */
void ast_channel_pickupgroup_set(struct ast_channel *chan, ast_group_t value);
struct ast_namedgroups *ast_channel_named_callgroups(const struct ast_channel *chan);
void ast_channel_named_callgroups_set(struct ast_channel *chan, struct ast_namedgroups *value);
struct ast_namedgroups *ast_channel_named_pickupgroups(const struct ast_channel *chan);
void ast_channel_named_pickupgroups_set(struct ast_channel *chan, struct ast_namedgroups *value);

/* Alertpipe accessors--the "internal" functions for channel.c use only */
typedef enum {
	AST_ALERT_READ_SUCCESS = 0,
	AST_ALERT_NOT_READABLE,
	AST_ALERT_READ_FAIL,
	AST_ALERT_READ_FATAL,
} ast_alert_status_t;
int ast_channel_alert_write(struct ast_channel *chan);
int ast_channel_alert_writable(struct ast_channel *chan);
ast_alert_status_t ast_channel_internal_alert_read(struct ast_channel *chan);
int ast_channel_internal_alert_readable(struct ast_channel *chan);
void ast_channel_internal_alertpipe_clear(struct ast_channel *chan);
void ast_channel_internal_alertpipe_close(struct ast_channel *chan);
int ast_channel_internal_alert_readfd(struct ast_channel *chan);
int ast_channel_internal_alertpipe_init(struct ast_channel *chan);
/*! \brief Swap the interal alertpipe between two channels
 * \note Handle all of the necessary locking before calling this
 */
void ast_channel_internal_alertpipe_swap(struct ast_channel *chan1, struct ast_channel *chan2);

/* file descriptor array accessors */
void ast_channel_internal_fd_clear(struct ast_channel *chan, int which);
void ast_channel_internal_fd_clear_all(struct ast_channel *chan);
void ast_channel_internal_fd_set(struct ast_channel *chan, int which, int value);
int ast_channel_fd(const struct ast_channel *chan, int which);
int ast_channel_fd_isset(const struct ast_channel *chan, int which);

/* epoll data internal accessors */
#ifdef HAVE_EPOLL
struct ast_epoll_data *ast_channel_internal_epfd_data(const struct ast_channel *chan, int which);
void ast_channel_internal_epfd_data_set(struct ast_channel *chan, int which , struct ast_epoll_data *value);
#endif

pthread_t ast_channel_blocker(const struct ast_channel *chan);
void ast_channel_blocker_set(struct ast_channel *chan, pthread_t value);

ast_timing_func_t ast_channel_timingfunc(const struct ast_channel *chan);
void ast_channel_timingfunc_set(struct ast_channel *chan, ast_timing_func_t value);

struct ast_bridge *ast_channel_internal_bridge(const struct ast_channel *chan);
/*!
 * \pre chan is locked
 */
void ast_channel_internal_bridge_set(struct ast_channel *chan, struct ast_bridge *value);

struct ast_bridge_channel *ast_channel_internal_bridge_channel(const struct ast_channel *chan);
void ast_channel_internal_bridge_channel_set(struct ast_channel *chan, struct ast_bridge_channel *value);

struct ast_channel *ast_channel_internal_bridged_channel(const struct ast_channel *chan);
void ast_channel_internal_bridged_channel_set(struct ast_channel *chan, struct ast_channel *value);

/*!
 * \since 11
 * \brief Retrieve a comma-separated list of channels for which dialed cause information is available
 *
 * \details
 * This function makes use of datastore operations on the channel, so
 * it is important to lock the channel before calling this function.
 *
 * \param chan The channel from which to retrieve information
 * \retval NULL on allocation failure
 * \retval Pointer to an ast_str object containing the desired information which must be freed
 */
struct ast_str *ast_channel_dialed_causes_channels(const struct ast_channel *chan);

/*!
 * \since 11
 * \brief Retrieve a ref-counted cause code information structure
 *
 * \details
 * This function makes use of datastore operations on the channel, so
 * it is important to lock the channel before calling this function.
 * This function increases the ref count of the returned object, so the
 * calling function must decrease the reference count when it is finished
 * with the object.
 *
 * \param chan The channel from which to retrieve information
 * \param chan_name The name of the channel about which to retrieve information
 * \retval NULL on search failure
 * \retval Pointer to a ref-counted ast_control_pvt_cause_code object containing the desired information
 */
struct ast_control_pvt_cause_code *ast_channel_dialed_causes_find(const struct ast_channel *chan, const char *chan_name);

/*!
 * \since 11
 * \brief Add cause code information to the channel
 *
 * \details
 * This function makes use of datastore operations on the channel, so
 * it is important to lock the channel before calling this function.
 * The passed in data is copied and so is still owned by the caller.
 *
 * \param chan The channel on which to add information
 * \param cause_code The cause information to be added to the channel
 * \param datalen The total length of the structure since its length is variable
 * \retval 0 on success
 * \retval -1 on error
 */
int ast_channel_dialed_causes_add(const struct ast_channel *chan, const struct ast_control_pvt_cause_code *cause_code, int datalen);

/*!
 * \since 11
 * \brief Clear all cause information from the channel
 *
 * \details
 * This function makes use of datastore operations on the channel, so
 * it is important to lock the channel before calling this function.
 *
 * \param chan The channel from which to clear information
 */
void ast_channel_dialed_causes_clear(const struct ast_channel *chan);

struct ast_flags *ast_channel_flags(struct ast_channel *chan);

/*!
 * \since 12.4.0
 * \brief Return whether or not any manager variables have been set
 *
 * \retval 0 if no manager variables are expected
 * \retval 1 if manager variables are expected
 */
int ast_channel_has_manager_vars(void);

/*!
 * \since 12
 * \brief Sets the variables to be stored in the \a manager_vars field of all
 * snapshots.
 * \param varc Number of variable names.
 * \param vars Array of variable names.
 */
void ast_channel_set_manager_vars(size_t varc, char **vars);

/*!
 * \since 12
 * \brief Gets the variables for a given channel, as specified by ast_channel_set_manager_vars().
 *
 * The returned variable list is an AO2 object, so ao2_cleanup() to free it.
 *
 * \param chan Channel to get variables for.
 * \return List of channel variables.
 * \return \c NULL on error
 */
struct varshead *ast_channel_get_manager_vars(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Gets the variables for a given channel, as set using pbx_builtin_setvar_helper().
 *
 * The returned variable list is an AO2 object, so ao2_cleanup() to free it.
 *
 * \param chan Channel to get variables for
 * \return List of channel variables.
 * \return \c NULL on error
 */
struct varshead *ast_channel_get_vars(struct ast_channel *chan);

/*!
 * \since 12
 * \brief A topic which publishes the events for a particular channel.
 *
 * If the given \a chan is \c NULL, ast_channel_topic_all() is returned.
 *
 * \param chan Channel, or \c NULL.
 *
 * \retval Topic for channel's events.
 * \retval ast_channel_topic_all() if \a chan is \c NULL.
 */
struct stasis_topic *ast_channel_topic(struct ast_channel *chan);

/*!
 * \since 12
 * \brief A topic which publishes the events for a particular channel.
 *
 * \ref ast_channel_snapshot messages are replaced with \ref stasis_cache_update
 *
 * If the given \a chan is \c NULL, ast_channel_topic_all_cached() is returned.
 *
 * \param chan Channel, or \c NULL.
 *
 * \retval Topic for channel's events.
 * \retval ast_channel_topic_all() if \a chan is \c NULL.
 */
struct stasis_topic *ast_channel_topic_cached(struct ast_channel *chan);

/*!
 * \brief Get the bridge associated with a channel
 * \since 12.0.0
 *
 * \param chan The channel whose bridge we want
 *
 * \details
 * The bridge returned has its reference count incremented.  Use
 * ao2_cleanup() or ao2_ref() in order to decrement the
 * reference count when you are finished with the bridge.
 *
 * \note This function expects the channel to be locked prior to
 * being called and will not grab the channel lock.
 *
 * \retval NULL No bridge present on the channel
 * \retval non-NULL The bridge the channel is in
 */
struct ast_bridge *ast_channel_get_bridge(const struct ast_channel *chan);

/*!
 * \brief Determine if a channel is in a bridge
 * \since 12.0.0
 *
 * \param chan The channel to test
 *
 * \note This function expects the channel to be locked prior to
 * being called and will not grab the channel lock.
 *
 * \retval 0 The channel is not bridged
 * \retval non-zero The channel is bridged
 */
int ast_channel_is_bridged(const struct ast_channel *chan);

/*!
 * \brief Determine if a channel is leaving a bridge, but \em not hung up
 * \since 12.4.0
 *
 * \param chan The channel to test
 *
 * \note If a channel is hung up, it is implicitly leaving any bridge it
 * may be in. This function is used to test if a channel is leaving a bridge
 * but may survive the experience, if it has a place to go to (dialplan or
 * otherwise)
 *
 * \retval 0 The channel is not leaving the bridge or is hung up
 * \retval non-zero The channel is leaving the bridge
 */
int ast_channel_is_leaving_bridge(struct ast_channel *chan);

/*!
 * \brief Get the channel's bridge peer only if the bridge is two-party.
 * \since 12.0.0
 *
 * \param chan Channel desiring the bridge peer channel.
 *
 * \note The returned peer channel is the current peer in the
 * bridge when called.
 *
 * \note Absolutely _NO_ channel locks should be held when calling this function.
 *
 * \retval NULL Channel not in a bridge or the bridge is not two-party.
 * \retval non-NULL Reffed peer channel at time of calling.
 */
struct ast_channel *ast_channel_bridge_peer(struct ast_channel *chan);

/*!
 * \brief Get a reference to the channel's bridge pointer.
 * \since 12.0.0
 *
 * \param chan The channel whose bridge channel is desired
 *
 * \note This increases the reference count of the bridge_channel.
 * Use ao2_ref() or ao2_cleanup() to decrement the refcount when
 * you are finished with it.
 *
 * \note It is expected that the channel is locked prior to
 * placing this call.
 *
 * \retval NULL The channel has no bridge_channel
 * \retval non-NULL A reference to the bridge_channel
 */
struct ast_bridge_channel *ast_channel_get_bridge_channel(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Gain control of a channel in the system
 *
 * The intention of this function is to take a channel that currently
 * is running in one thread and gain control of it in the current thread.
 * This can be used to redirect a channel to a different place in the dialplan,
 * for instance.
 *
 * \note This function is NOT intended to be used on bridged channels. If you
 * need to control a bridged channel, you can set a callback to be called
 * once the channel exits the bridge, and run your controlling logic in that
 * callback
 *
 * XXX Put name of callback-setting function in above paragraph once it is written
 *
 * \note When this function returns successfully, the yankee channel is in a state where
 * it cannot be used any further. Always use the returned channel instead.
 *
 * \note absolutely _NO_ channel locks should be held before calling this function.
 *
 * \param yankee The channel to gain control of
 * \retval NULL Could not gain control of the channel
 * \retval non-NULL The channel
 */
struct ast_channel *ast_channel_yank(struct ast_channel *yankee);

/*!
 * \since 12
 * \brief Move a channel from its current location to a new location
 *
 * The intention of this function is to have the destination channel
 * take on the identity of the source channel.
 *
 * \note This function is NOT intended to be used on bridged channels. If you
 * wish to move an unbridged channel into the place of a bridged channel, then
 * use ast_bridge_join() or ast_bridge_impart(). If you wish to move a bridged
 * channel into the place of another bridged channel, then use ast_bridge_move().
 *
 * \note When this function returns succesfully, the source channel is in a
 * state where its continued use is unreliable.
 *
 * \note absolutely _NO_ channel locks should be held before calling this function.
 *
 * \param dest The place to move the source channel
 * \param source The channel to move
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_channel_move(struct ast_channel *dest, struct ast_channel *source);

/*!
 * \since 12
 * \brief Forward channel stasis messages to the given endpoint
 *
 * \param chan The channel to forward from
 * \param endpoint The endpoint to forward to
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_channel_forward_endpoint(struct ast_channel *chan, struct ast_endpoint *endpoint);

/*!
 * \brief Return the oldest linkedid between two channels.
 *
 * A channel linkedid is derived from the channel uniqueid which is formed like this:
 * [systemname-]ctime.seq
 *
 * The systemname, and the dash are optional, followed by the epoch time followed by an
 * integer sequence.  Note that this is not a decimal number, since 1.2 is less than 1.11
 * in uniqueid land.
 *
 * To compare two uniqueids, we parse out the integer values of the time and the sequence
 * numbers and compare them, with time trumping sequence.
 *
 * \param a The linkedid value of the first channel to compare
 * \param b The linkedid value of the second channel to compare
 *
 * \retval NULL on failure
 * \retval The oldest linkedid value
 * \since 12.0.0
*/
const char *ast_channel_oldest_linkedid(const char *a, const char *b);

/*!
 * \brief Check if the channel has active audiohooks, active framehooks, or a monitor.
 * \since 12.0.0
 *
 * \param chan The channel to check.
 *
 * \retval non-zero if channel has active audiohooks, framehooks, or monitor.
 */
int ast_channel_has_audio_frame_or_monitor(struct ast_channel *chan);

/*!
 * \brief Check if the channel has any active hooks that require audio.
 * \since 12.3.0
 *
 * \param chan The channel to check.
 *
 * \retval non-zero if channel has active audiohooks, audio framehooks, or monitor.
 */
int ast_channel_has_hook_requiring_audio(struct ast_channel *chan);

/*!
 * \brief Removes the trailing identifiers from a channel name string
 * \since 12.0.0
 *
 * \param channel_name string that you wish to turn into a dial string.
 *                     This string will be edited in place.
 */
void ast_channel_name_to_dial_string(char *channel_name);

#define AST_MUTE_DIRECTION_READ (1 << 0)
#define AST_MUTE_DIRECTION_WRITE (1 << 1)

/*!
 * \brief Suppress passing of a frame type on a channel
 *
 * \note The channel should be locked before calling this function.
 *
 * \param chan The channel to suppress
 * \param direction The direction in which to suppress
 * \param frametype The type of frame (AST_FRAME_VOICE, etc) to suppress
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_channel_suppress(struct ast_channel *chan, unsigned int direction, enum ast_frame_type frametype);

/*!
 * \brief Stop suppressing of a frame type on a channel
 *
 * \note The channel should be locked before calling this function.
 *
 * \param chan The channel to stop suppressing
 * \param direction The direction in which to stop suppressing
 * \param frametype The type of frame (AST_FRAME_VOICE, etc) to stop suppressing
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_channel_unsuppress(struct ast_channel *chan, unsigned int direction, enum ast_frame_type frametype);

/*!
 * \brief Simulate a DTMF end on a broken bridge channel.
 *
 * \param chan Channel sending DTMF that has not ended.
 * \param digit DTMF digit to stop.
 * \param start DTMF digit start time.
 * \param why Reason bridge broken.
 *
 * \return Nothing
 */
void ast_channel_end_dtmf(struct ast_channel *chan, char digit, struct timeval start, const char *why);

struct ast_bridge_features;

/*!
 * \brief Gets the channel-attached features a channel has access to upon being bridged.
 *
 * \note The channel must be locked when calling this function.
 *
 * \param chan Which channel to get features for
 *
 * \retval non-NULL The features currently set for this channel
 * \retval NULL if the features have not been set
 */
struct ast_bridge_features *ast_channel_feature_hooks_get(struct ast_channel *chan);

/*!
 * \brief Appends to the channel-attached features a channel has access to upon being bridged.
 *
 * \note The channel must be locked when calling this function.
 *
 * \param chan Which channel to set features for
 * \param features The feature set to append to the channel's features
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_channel_feature_hooks_append(struct ast_channel *chan, struct ast_bridge_features *features);

/*!
 * \brief Sets the channel-attached features a channel has access to upon being bridged.
 *
 * \note The channel must be locked when calling this function.
 *
 * \param chan Which channel to set features for
 * \param features The feature set with which to replace the channel's features
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_channel_feature_hooks_replace(struct ast_channel *chan, struct ast_bridge_features *features);

#endif /* _ASTERISK_CHANNEL_H */
