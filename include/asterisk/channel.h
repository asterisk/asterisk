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

#define AST_MAX_EXTENSION	80	/*!< Max length of an extension */
#define AST_MAX_CONTEXT		80	/*!< Max length of a context */
#define AST_CHANNEL_NAME	80	/*!< Max length of an ast_channel name */
#define MAX_LANGUAGE		40	/*!< Max length of the language setting */
#define MAX_MUSICCLASS		80	/*!< Max length of the music class setting */

#include "asterisk/frame.h"
#include "asterisk/sched.h"
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

#define DATASTORE_INHERIT_FOREVER	INT_MAX

#define AST_MAX_FDS		10
/*
 * We have AST_MAX_FDS file descriptors in a channel.
 * Some of them have a fixed use:
 */
#define AST_ALERT_FD	(AST_MAX_FDS-1)		/*!< used for alertpipe */
#define AST_TIMING_FD	(AST_MAX_FDS-2)		/*!< used for timingfd */
#define AST_AGENT_FD	(AST_MAX_FDS-3)		/*!< used by agents for pass through */
#define AST_GENERATOR_FD	(AST_MAX_FDS-4)	/*!< used by generator */

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
	void (*release)(struct ast_channel *chan, void *data);
	/*! This function gets called with the channel unlocked, but is called in
	 *  the context of the channel thread so we know the channel is not going
	 *  to disappear.  This callback is responsible for locking the channel as
	 *  necessary. */
	int (*generate)(struct ast_channel *chan, void *data, int len, int samples);
	/*! This gets called when DTMF_END frames are read from the channel */
	void (*digit)(struct ast_channel *chan, char digit);
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
 * \brief Structure for all kinds of caller ID identifications.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * Also, NULL and "" must be considered equivalent.
 *
 * \note SIP and IAX2 has utf8 encoded Unicode caller ID names.
 * In some cases, we also have an alternative (RPID) E.164 number that can be used
 * as caller ID on numeric E.164 phone networks (DAHDI or SIP/IAX2 to PSTN gateway).
 *
 * \todo Implement settings for transliteration between UTF8 caller ID names in
 *       to Ascii Caller ID's (DAHDI). Östen Åsklund might be transliterated into
 *       Osten Asklund or Oesten Aasklund depending upon language and person...
 *       We need automatic routines for incoming calls and static settings for
 *       our own accounts.
 */
struct ast_callerid {
	/*!
	 * \brief Malloc'd Dialed Number Identifier
	 * (Field will eventually move to struct ast_channel.dialed.number)
	 */
	char *cid_dnid;

	/*!
	 * \brief Malloc'd Caller Number
	 * (Field will eventually move to struct ast_channel.caller.id.number)
	 */
	char *cid_num;

	/*!
	 * \brief Malloc'd Caller Name (ASCII)
	 * (Field will eventually move to struct ast_channel.caller.id.name)
	 */
	char *cid_name;

	/*!
	 * \brief Malloc'd Automatic Number Identification (ANI)
	 * (Field will eventually move to struct ast_channel.caller.ani)
	 */
	char *cid_ani;

	/*!
	 * \brief Callerid Q.931 encoded number presentation/screening fields
	 * (Field will eventually move to struct ast_channel.caller.id.number_presentation)
	 */
	int cid_pres;

	/*!
	 * \brief Callerid ANI 2 (Info digits)
	 * (Field will eventually move to struct ast_channel.caller.ani2)
	 */
	int cid_ani2;

	/*!
	 * \brief Callerid Q.931 encoded type-of-number/numbering-plan fields
	 * \note Currently this value is mostly just passed around the system.
	 * The H.323 interfaces set the value from received messages and uses the value for sent messages.
	 * The DAHDI PRI interfaces set the value from received messages but does not use it for sent messages.
	 * You can read it and set it but only H.323 uses it.
	 * (Field will eventually move to struct ast_channel.caller.id.number_type)
	 */
	int cid_ton;

	/*!
	 * \brief Callerid Transit Network Select
	 * \note Currently this value is just passed around the system.
	 * You can read it and set it but it is never used for anything.
	 * (Field will eventually move to struct ast_channel.dialed.transit_network_select)
	 */
	int cid_tns;

	/*!
	 * \brief Callerid "Tag"
	 * A user-settable field used to help associate some extrinsic information
	 * about the channel or user of the channel to the caller ID. This information
	 * is not transmitted over the wire and so is only useful within an Asterisk
	 * environment.
	 * (Field will eventually move to struct ast_channel.caller.id.tag)
	 */
	char *cid_tag;

	/*!
	 * \brief Caller id subaddress.
	 * (Field will eventually move to struct ast_channel.caller.id.subaddress)
	 */
	struct ast_party_subaddress subaddress;
	/*!
	 * \brief Dialed/Called subaddress.
	 * (Field will eventually move to struct ast_channel.dialed.subaddress)
	 */
	struct ast_party_subaddress dialed_subaddress;
};

/*!
 * \since 1.8
 * \brief Information needed to identify an endpoint in a call.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_id {
	/*! \brief Subscriber phone number (Malloced) */
	char *number;

	/*! \brief Subscriber name (Malloced) */
	char *name;

	/*! \brief User-set "tag" */
	char *tag;

	/*! \brief Subscriber subaddress. */
	struct ast_party_subaddress subaddress;

	/*! \brief Q.931 encoded type-of-number/numbering-plan fields */
	int number_type;

	/*! \brief Q.931 encoded number presentation/screening fields */
	int number_presentation;
};

/*!
 * \since 1.8
 * \brief Caller Party information.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_caller {
	struct ast_party_id id;		/*! \brief Caller party ID */

	/*! \brief Automatic Number Identification (ANI) (Malloced) */
	char *ani;

	/*! \brief Automatic Number Identification 2 (Info Digits) */
	int ani2;
};

/*!
 * \since 1.8
 * \brief Connected Line/Party information.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_connected_line {
	struct ast_party_id id;		/*! \brief Connected party ID */

	/*!
	 * \brief Automatic Number Identification (ANI) (Malloced)
	 * \note Not really part of connected line data but needed to
	 * save the corresponding caller id value.
	 */
	char *ani;

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
 * \brief Redirecting Line information.
 * RDNIS (Redirecting Directory Number Information Service)
 * Where a call diversion or transfer was invoked.
 * \note All string fields here are malloc'ed, so they need to be
 * freed when the structure is deleted.
 * \note NULL and "" must be considered equivalent.
 */
struct ast_party_redirecting {
	/*! \brief Who is redirecting the call (Sent to the party the call is redirected toward) */
	struct ast_party_id from;

	/*! \brief Call is redirecting to a new party (Sent to the caller) */
	struct ast_party_id to;

	/*! \brief Number of times the call was redirected */
	int count;

	/*! \brief enum AST_REDIRECTING_REASON value for redirection */
	int reason;
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

	format_t capabilities;  /*!< Bitmap of formats this channel can handle */

	int properties;         /*!< Technology Properties */

	/*! \brief Requester - to set up call data structures (pvt's) */
	struct ast_channel *(* const requester)(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause);

	int (* const devicestate)(void *data);	/*!< Devicestate call back */

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

	/*! \brief Call a given phone number (address, etc), but don't
	 *  take longer than timeout seconds to do so.  */
	int (* const call)(struct ast_channel *chan, char *addr, int timeout);

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

	/*! \brief Bridge two channels of the same type together */
	enum ast_bridge_result (* const bridge)(struct ast_channel *c0, struct ast_channel *c1, int flags,
						struct ast_frame **fo, struct ast_channel **rc, int timeoutms);

	/*! \brief Bridge two channels of the same type together (early) */
	enum ast_bridge_result (* const early_bridge)(struct ast_channel *c0, struct ast_channel *c1);

	/*! \brief Indicate a particular condition (e.g. AST_CONTROL_BUSY or AST_CONTROL_RINGING or AST_CONTROL_CONGESTION */
	int (* const indicate)(struct ast_channel *c, int condition, const void *data, size_t datalen);

	/*! \brief Fix up a channel:  If a channel is consumed, this is called.  Basically update any ->owner links */
	int (* const fixup)(struct ast_channel *oldchan, struct ast_channel *newchan);

	/*! \brief Set a given option */
	int (* const setoption)(struct ast_channel *chan, int option, void *data, int datalen);

	/*! \brief Query a given option */
	int (* const queryoption)(struct ast_channel *chan, int option, void *data, int *datalen);

	/*! \brief Blind transfer other side (see app_transfer.c and ast_transfer() */
	int (* const transfer)(struct ast_channel *chan, const char *newdest);

	/*! \brief Write a frame, in standard format */
	int (* const write_video)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Write a text frame, in standard format */
	int (* const write_text)(struct ast_channel *chan, struct ast_frame *frame);

	/*! \brief Find bridged channel */
	struct ast_channel *(* const bridged_channel)(struct ast_channel *chan, struct ast_channel *bridge);

	/*! \brief Provide additional read items for CHANNEL() dialplan function */
	int (* func_channel_read)(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len);

	/*! \brief Provide additional write items for CHANNEL() dialplan function */
	int (* func_channel_write)(struct ast_channel *chan, const char *function, char *data, const char *value);

	/*! \brief Retrieve base channel (agent and local) */
	struct ast_channel* (* get_base_channel)(struct ast_channel *chan);

	/*! \brief Set base channel (agent and local) */
	int (* set_base_channel)(struct ast_channel *chan, struct ast_channel *base);

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
};

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

/*!
 * \page AstChannel ast_channel locking and reference tracking
 *
 * \par Creating Channels
 * A channel is allocated using the ast_channel_alloc() function.  When created, it is
 * automatically inserted into the main channels hash table that keeps track of all
 * active channels in the system.  The hash key is based on the channel name.  Because
 * of this, if you want to change the name, you _must_ use ast_change_name(), not change
 * the name field directly.  When ast_channel_alloc() returns a channel pointer, you now
 * hold a reference to that channel.  In most cases this reference is given to ast_pbx_run().
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

/*!
 * \brief Main Channel structure associated with a channel.
 *
 * \note XXX It is important to remember to increment .cleancount each time
 *       this structure is changed. XXX
 *
 * \note When adding fields to this structure, it is important to add the field
 *       'in position' with like-aligned fields, so as to keep the compiler from
 *       having to add padding to align fields. The structure's fields are sorted
 *       in this order: pointers, structures, long, int/enum, short, char. This
 *       is especially important on 64-bit architectures, where mixing 4-byte
 *       and 8-byte fields causes 4 bytes of padding to be added before many
 *       8-byte fields.
 */
struct ast_channel {
	const struct ast_channel_tech *tech;		/*!< Technology (point to channel driver) */
	void *tech_pvt;					/*!< Private data used by the technology driver */
	void *music_state;				/*!< Music State*/
	void *generatordata;				/*!< Current generator data if there is any */
	struct ast_generator *generator;		/*!< Current active data generator */
	struct ast_channel *_bridge;			/*!< Who are we bridged to, if we're bridged.
							 *   Who is proxying for us, if we are proxied (i.e. chan_agent).
							 *   Do not access directly, use ast_bridged_channel(chan) */
	struct ast_channel *masq;			/*!< Channel that will masquerade as us */
	struct ast_channel *masqr;			/*!< Who we are masquerading as */
	const char *blockproc;				/*!< Procedure causing blocking */
	const char *appl;				/*!< Current application */
	const char *data;				/*!< Data passed to current application */
	struct sched_context *sched;			/*!< Schedule context */
	struct ast_filestream *stream;			/*!< Stream itself. */
	struct ast_filestream *vstream;			/*!< Video Stream itself. */
	int (*timingfunc)(const void *data);
	void *timingdata;
	struct ast_pbx *pbx;				/*!< PBX private structure for this channel */
	struct ast_trans_pvt *writetrans;		/*!< Write translation path */
	struct ast_trans_pvt *readtrans;		/*!< Read translation path */
	struct ast_audiohook_list *audiohooks;
	struct ast_cdr *cdr;				/*!< Call Detail Record */
	struct ast_tone_zone *zone;			/*!< Tone zone as set in indications.conf or
							 *   in the CHANNEL dialplan function */
	struct ast_channel_monitor *monitor;		/*!< Channel monitoring */
#ifdef HAVE_EPOLL
	struct ast_epoll_data *epfd_data[AST_MAX_FDS];
#endif

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);			/*!< ASCII unique channel name */
		AST_STRING_FIELD(language);		/*!< Language requested for voice prompts */
		AST_STRING_FIELD(musicclass);		/*!< Default music class */
		AST_STRING_FIELD(accountcode);		/*!< Account code for billing */
		AST_STRING_FIELD(peeraccount);		/*!< Peer account code for billing */
		AST_STRING_FIELD(userfield);		/*!< Userfield for CEL billing */
		AST_STRING_FIELD(call_forward);		/*!< Where to forward to if asked to dial on this interface */
		AST_STRING_FIELD(uniqueid);		/*!< Unique Channel Identifier */
		AST_STRING_FIELD(linkedid);		/*!< Linked Channel Identifier -- gets propagated by linkage */
		AST_STRING_FIELD(parkinglot);		/*! Default parking lot, if empty, default parking lot  */
		AST_STRING_FIELD(hangupsource);		/*! Who is responsible for hanging up this channel */
		AST_STRING_FIELD(dialcontext);		/*!< Dial: Extension context that we were called from */
	);

	struct timeval whentohangup;        		/*!< Non-zero, set to actual time when channel is to be hung up */
	pthread_t blocker;				/*!< If anyone is blocking, this is them */

	/*!
	 * \brief Channel Caller ID information.
	 * \note The caller id information is the caller id of this
	 * channel when it is used to initiate a call.
	 */
	struct ast_callerid cid;

	/*!
	 * \brief Channel Connected Line ID information.
	 * \note The connected line information identifies the channel
	 * connected/bridged to this channel.
	 */
	struct ast_party_connected_line connected;

	/*! \brief Redirecting/Diversion information */
	struct ast_party_redirecting redirecting;

	struct ast_frame dtmff;				/*!< DTMF frame */
	struct varshead varshead;			/*!< A linked list for channel variables. See \ref AstChanVar */
	ast_group_t callgroup;				/*!< Call group for call pickups */
	ast_group_t pickupgroup;			/*!< Pickup group - which calls groups can be picked up? */
	AST_LIST_HEAD_NOLOCK(, ast_frame) readq;
	struct ast_jb jb;				/*!< The jitterbuffer state */
	struct timeval dtmf_tv;				/*!< The time that an in process digit began, or the last digit ended */
	AST_LIST_HEAD_NOLOCK(datastores, ast_datastore) datastores; /*!< Data stores on the channel */
	AST_LIST_HEAD_NOLOCK(autochans, ast_autochan) autochans; /*!< Autochans on the channel */

	unsigned long insmpl;				/*!< Track the read/written samples for monitor use */
	unsigned long outsmpl;				/*!< Track the read/written samples for monitor use */

	int fds[AST_MAX_FDS];				/*!< File descriptors for channel -- Drivers will poll on
							 *   these file descriptors, so at least one must be non -1.
							 *   See \arg \ref AstFileDesc */
	int _softhangup;				/*!< Whether or not we have been hung up...  Do not set this value
							 *   directly, use ast_softhangup() */
	int fdno;					/*!< Which fd had an event detected on */
	int streamid;					/*!< For streaming playback, the schedule ID */
	int vstreamid;					/*!< For streaming video playback, the schedule ID */
	format_t oldwriteformat;		/*!< Original writer format */
	int timingfd;					/*!< Timing fd */
	enum ast_channel_state _state;			/*!< State of line -- Don't write directly, use ast_setstate() */
	int rings;					/*!< Number of rings so far */
	int priority;					/*!< Dialplan: Current extension priority */
	int macropriority;				/*!< Macro: Current non-macro priority. See app_macro.c */
	int amaflags;					/*!< Set BEFORE PBX is started to determine AMA flags */
	enum ast_channel_adsicpe adsicpe;		/*!< Whether or not ADSI is detected on CPE */
	unsigned int fin;				/*!< Frames in counters. The high bit is a debug mask, so
							 *   the counter is only in the remaining bits */
	unsigned int fout;				/*!< Frames out counters. The high bit is a debug mask, so
							 *   the counter is only in the remaining bits */
	int hangupcause;				/*!< Why is the channel hanged up. See causes.h */
	unsigned int flags;				/*!< channel flags of AST_FLAG_ type */
	int alertpipe[2];
	format_t nativeformats;         /*!< Kinds of data this channel can natively handle */
	format_t readformat;            /*!< Requested read format */
	format_t writeformat;           /*!< Requested write format */
	format_t rawreadformat;         /*!< Raw read format */
	format_t rawwriteformat;        /*!< Raw write format */
	unsigned int emulate_dtmf_duration;		/*!< Number of ms left to emulate DTMF for */
#ifdef HAVE_EPOLL
	int epfd;
#endif
	int visible_indication;                         /*!< Indication currently playing on the channel */

	unsigned short transfercapability;		/*!< ISDN Transfer Capability - AST_FLAG_DIGITAL is not enough */

	struct ast_bridge *bridge;                      /*!< Bridge this channel is participating in */
	struct ast_timer *timer;			/*!< timer object that provided timingfd */

	char context[AST_MAX_CONTEXT];			/*!< Dialplan: Current extension context */
	char exten[AST_MAX_EXTENSION];			/*!< Dialplan: Current extension number */
	char macrocontext[AST_MAX_CONTEXT];		/*!< Macro: Current non-macro context. See app_macro.c */
	char macroexten[AST_MAX_EXTENSION];		/*!< Macro: Current non-macro extension. See app_macro.c */
	char emulate_dtmf_digit;			/*!< Digit being emulated */
};

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
	/*! This channel is in a native bridge */
	AST_FLAG_NBRIDGE =       (1 << 8),
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
	/*! Flag to show channels that this call is hangup due to the fact that the call
	    was indeed answered, but in another channel */
	AST_FLAG_ANSWERED_ELSEWHERE = (1 << 15),
	/*! This flag indicates that on a masquerade, an active stream should not
	 *  be carried over */
	AST_FLAG_MASQ_NOSTREAM = (1 << 16),
	/*! This flag indicates that the hangup exten was run when the bridge terminated,
	 *  a message aimed at preventing a subsequent hangup exten being run at the pbx_run
	 *  level */
	AST_FLAG_BRIDGE_HANGUP_RUN = (1 << 17),
	/*! This flag indicates that the hangup exten should NOT be run when the
	 *  bridge terminates, this will allow the hangup in the pbx loop to be run instead.
	 *  */
	AST_FLAG_BRIDGE_HANGUP_DONT = (1 << 18),
	/*! Disable certain workarounds.  This reintroduces certain bugs, but allows
	 *  some non-traditional dialplans (like AGI) to continue to function.
	 */
	AST_FLAG_DISABLE_WORKAROUNDS = (1 << 20),
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
	AST_FEATURE_NO_H_EXTEN =   (1 << 7),
	AST_FEATURE_WARNING_ACTIVE = (1 << 8),
};

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
	const char *cid_num;
	const char *cid_name;
	const char *account;
	struct ast_variable *vars;
	struct ast_channel *parent_channel;
};

enum {
	/*! Soft hangup by device */
	AST_SOFTHANGUP_DEV =       (1 << 0),
	/*! Soft hangup for async goto */
	AST_SOFTHANGUP_ASYNCGOTO = (1 << 1),
	AST_SOFTHANGUP_SHUTDOWN =  (1 << 2),
	AST_SOFTHANGUP_TIMEOUT =   (1 << 3),
	AST_SOFTHANGUP_APPUNLOAD = (1 << 4),
	AST_SOFTHANGUP_EXPLICIT =  (1 << 5),
	AST_SOFTHANGUP_UNBRIDGE =  (1 << 6),
};


/*! \brief Channel reload reasons for manager events at load or reload of configuration */
enum channelreloadreason {
	CHANNEL_MODULE_LOAD,
	CHANNEL_MODULE_RELOAD,
	CHANNEL_CLI_RELOAD,
	CHANNEL_MANAGER_RELOAD,
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
 * \note By default, new channels are set to the "s" extension
 *       and "default" context.
 */
struct ast_channel * attribute_malloc __attribute__((format(printf, 13, 14)))
	__ast_channel_alloc(int needqueue, int state, const char *cid_num,
			    const char *cid_name, const char *acctcode,
			    const char *exten, const char *context,
			    const char *linkedid, const int amaflag,
			    const char *file, int line, const char *function,
			    const char *name_fmt, ...);

#define ast_channel_alloc(needqueue, state, cid_num, cid_name, acctcode, exten, context, linkedid, amaflag, ...) \
	__ast_channel_alloc(needqueue, state, cid_num, cid_name, acctcode, exten, context, linkedid, amaflag, \
			    __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

/*!
 * \brief Create a fake channel structure
 *
 * \retval NULL failure
 * \retval non-NULL successfully allocated channel
 *
 * \note This function should ONLY be used to create a fake channel
 *       that can then be populated with data for use in variable
 *       substitution when a real channel does not exist.
 */
#if defined(REF_DEBUG) || defined(__AST_DEBUG_MALLOC)
#define ast_dummy_channel_alloc()	__ast_dummy_channel_alloc(__FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_channel *__ast_dummy_channel_alloc(const char *file, int line, const char *function);
#else
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
 * \brief Queue a control frame with payload
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
 *
 * \since 1.8
 */
struct ast_channel *ast_channel_release(struct ast_channel *chan);

/*!
 * \brief Requests a channel
 *
 * \param type type of channel to request
 * \param format requested channel format (codec)
 * \param requestor channel asking for data
 * \param data data to pass to the channel requester
 * \param status status
 *
 * \details
 * Request a channel of a given type, with data as optional information used
 * by the low level module
 *
 * \retval NULL failure
 * \retval non-NULL channel on success
 */
struct ast_channel *ast_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *status);

/*!
 * \brief Request a channel of a given type, with data as optional information used
 *        by the low level module and attempt to place a call on it
 *
 * \param type type of channel to request
 * \param format requested channel format
 * \param requestor channel asking for data
 * \param data data to pass to the channel requester
 * \param timeout maximum amount of time to wait for an answer
 * \param reason why unsuccessful (if unsuccessful)
 * \param cid_num Caller-ID Number
 * \param cid_name Caller-ID Name (ascii)
 *
 * \return Returns an ast_channel on success or no answer, NULL on failure.  Check the value of chan->_state
 * to know if the call was answered or not.
 */
struct ast_channel *ast_request_and_dial(const char *type, format_t format, const struct ast_channel *requestor, void *data,
	int timeout, int *reason, const char *cid_num, const char *cid_name);

/*!
 * \brief Request a channel of a given type, with data as optional information used
 * by the low level module and attempt to place a call on it
 * \param type type of channel to request
 * \param format requested channel format
 * \param requestor channel requesting data
 * \param data data to pass to the channel requester
 * \param timeout maximum amount of time to wait for an answer
 * \param reason why unsuccessful (if unsuccessful)
 * \param cid_num Caller-ID Number
 * \param cid_name Caller-ID Name (ascii)
 * \param oh Outgoing helper
 * \return Returns an ast_channel on success or no answer, NULL on failure.  Check the value of chan->_state
 * to know if the call was answered or not.
 */
struct ast_channel *__ast_request_and_dial(const char *type, format_t format, const struct ast_channel *requestor, void *data,
	int timeout, int *reason, const char *cid_num, const char *cid_name, struct outgoing_helper *oh);

/*!
 * \brief Forwards a call to a new channel specified by the original channel's call_forward str.  If possible, the new forwarded channel is created and returned while the original one is terminated.
 * \param caller in channel that requested orig
 * \param orig channel being replaced by the call forward channel
 * \param timeout maximum amount of time to wait for setup of new forward channel
 * \param format requested channel format
 * \param oh outgoing helper used with original channel
 * \param outstate reason why unsuccessful (if uncuccessful)
 * \return Returns the forwarded call's ast_channel on success or NULL on failure
 */
struct ast_channel *ast_call_forward(struct ast_channel *caller, struct ast_channel *orig, int *timeout, format_t format, struct outgoing_helper *oh, int *outstate);

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

#ifdef CHANNEL_TRACE
/*!
 * \brief Update the context backtrace if tracing is enabled
 * \return Returns 0 on success, -1 on failure
 */
int ast_channel_trace_update(struct ast_channel *chan);

/*!
 * \brief Enable context tracing in the channel
 * \return Returns 0 on success, -1 on failure
 */
int ast_channel_trace_enable(struct ast_channel *chan);

/*!
 * \brief Disable context tracing in the channel.
 * \note Does not remove current trace entries
 * \return Returns 0 on success, -1 on failure
 */
int ast_channel_trace_disable(struct ast_channel *chan);

/*!
 * \brief Whether or not context tracing is enabled
 * \return Returns -1 when the trace is enabled. 0 if not.
 */
int ast_channel_trace_is_enabled(struct ast_channel *chan);

/*!
 * \brief Put the channel backtrace in a string
 * \return Returns the amount of lines in the backtrace. -1 on error.
 */
int ast_channel_trace_serialize(struct ast_channel *chan, struct ast_str **out);
#endif

/*!
 * \brief Hang up a channel
 * \note This function performs a hard hangup on a channel.  Unlike the soft-hangup, this function
 * performs all stream stopping, etc, on the channel that needs to end.
 * chan is no longer valid after this call.
 * \param chan channel to hang up
 * \return Returns 0 on success, -1 on failure.
 */
int ast_hangup(struct ast_channel *chan);

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
 * \brief Set the source of the hangup in this channel and it's bridge
 *
 * \param chan channel to set the field on
 * \param source a string describing the source of the hangup for this channel
 * \param force
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
 * \note This function does not require that the channel is locked before
 *       calling it.
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
 * \note This function does not require that the channel is locked before
 * calling it.
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
 * \brief Answer a channel
 *
 * \param chan channel to answer
 * \param cdr_answer flag to control whether any associated CDR should be marked as 'answered'
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
int ast_raw_answer(struct ast_channel *chan, int cdr_answer);

/*!
 * \brief Answer a channel, with a selectable delay before returning
 *
 * \param chan channel to answer
 * \param delay maximum amount of time to wait for incoming media
 * \param cdr_answer flag to control whether any associated CDR should be marked as 'answered'
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
int __ast_answer(struct ast_channel *chan, unsigned int delay, int cdr_answer);

/*!
 * \brief Make a call
 * \param chan which channel to make the call on
 * \param addr destination of the call
 * \param timeout time to wait on for connect
 * \details
 * Place a call, take no longer than timeout ms.
 * \return -1 on failure, 0 on not enough time
 * (does not automatically stop ringing), and
 * the number of seconds the connect took otherwise.
 */
int ast_call(struct ast_channel *chan, char *addr, int timeout);

/*!
 * \brief Indicates condition of channel
 * \note Indicate a condition such as AST_CONTROL_BUSY, AST_CONTROL_RINGING, or AST_CONTROL_CONGESTION on a channel
 * \param chan channel to change the indication
 * \param condition which condition to indicate on the channel
 * \return Returns 0 on success, -1 on failure
 */
int ast_indicate(struct ast_channel *chan, int condition);

/*!
 * \brief Indicates condition of channel, with payload
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
 * \param ms length of time in milliseconds to sleep
 * \details
 * Waits for a specified amount of time, servicing the channel as required.
 * \return returns -1 on hangup, otherwise 0.
 */
int ast_safe_sleep(struct ast_channel *chan, int ms);

/*!
 * \brief Wait for a specified amount of time, looking for hangups and a condition argument
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep
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
 * \brief Sets read format on channel chan
 * Set read format for channel to whichever component of "format" is best.
 * \param chan channel to change
 * \param format format to change to
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_read_format(struct ast_channel *chan, format_t format);

/*!
 * \brief Sets write format on channel chan
 * Set write format for channel to whichever component of "format" is best.
 * \param chan channel to change
 * \param format new format for writing
 * \return Returns 0 on success, -1 on failure
 */
int ast_set_write_format(struct ast_channel *chan, format_t format);

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
 * \param ms how many milliseconds to wait
 * \return Returns <0 on error, 0 on no entry, and the digit on success.
 */
int ast_waitfordigit(struct ast_channel *c, int ms);

/*!
 * \brief Wait for a digit
 * Same as ast_waitfordigit() with audio fd for outputting read audio and ctrlfd to monitor for reading.
 * \param c channel to wait for a digit on
 * \param ms how many milliseconds to wait
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
/*! \brief Return all voice frames on channel 0 */
#define AST_BRIDGE_REC_CHANNEL_0		(1 << 2)
/*! \brief Return all voice frames on channel 1 */
#define AST_BRIDGE_REC_CHANNEL_1		(1 << 3)
/*! \brief Ignore all signal frames except NULL */
#define AST_BRIDGE_IGNORE_SIGS			(1 << 4)


/*!
 * \brief Makes two channel formats compatible
 * \param c0 first channel to make compatible
 * \param c1 other channel to make compatible
 * \details
 * Set two channels to compatible formats -- call before ast_channel_bridge in general.
 * \return Returns 0 on success and -1 if it could not be done
 */
int ast_channel_make_compatible(struct ast_channel *c0, struct ast_channel *c1);

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
 * \brief Bridge two channels together
 * \param c0 first channel to bridge
 * \param c1 second channel to bridge
 * \param config config for the channels
 * \param fo destination frame(?)
 * \param rc destination channel(?)
 * \details
 * Bridge two channels (c0 and c1) together.  If an important frame occurs, we return that frame in
 * *rf (remember, it could be NULL) and which channel (0 or 1) in rc
 */
/* int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc); */
int ast_channel_bridge(struct ast_channel *c0,struct ast_channel *c1,
	struct ast_bridge_config *config, struct ast_frame **fo, struct ast_channel **rc);

/*!
 * \brief Weird function made for call transfers
 *
 * \param original channel to make a copy of
 * \param clone copy of the original channel
 *
 * \details
 * This is a very strange and freaky function used primarily for transfer.  Suppose that
 * "original" and "clone" are two channels in random situations.  This function takes
 * the guts out of "clone" and puts them into the "original" channel, then alerts the
 * channel driver of the change, asking it to fixup any private information (like the
 * p->owner pointer) that is affected by the change.  The physical layer of the original
 * channel is hung up.
 *
 * \note Neither channel passed here needs to be locked before calling this function.
 */
int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clone);

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

/*! Pick the best codec
 * Choose the best codec...  Uhhh...   Yah. */
format_t ast_best_codec(format_t fmts);


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

/*! Initiate system shutdown -- prevents new channels from being allocated.
 * \param hangup  If "hangup" is non-zero, all existing channels will receive soft
 *  hangups */
void ast_begin_shutdown(int hangup);

/*! Cancels an existing shutdown and returns to normal operation */
void ast_cancel_shutdown(void);

/*! \return number of active/allocated channels */
int ast_active_channels(void);

/*! \return non-zero if Asterisk is being shut down */
int ast_shutting_down(void);

/*! Activate a given generator */
int ast_activate_generator(struct ast_channel *chan, struct ast_generator *gen, void *params);

/*! Deactivate an active generator */
void ast_deactivate_generator(struct ast_channel *chan);

/*!
 * \brief Set caller ID number, name and ANI
 *
 * \note The channel does not need to be locked before calling this function.
 */
void ast_set_callerid(struct ast_channel *chan, const char *cid_num, const char *cid_name, const char *cid_ani);

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
 * \brief Start masquerading a channel
 * \note absolutely _NO_ channel locks should be held before calling this function.
 * \details
 * XXX This is a seriously whacked out operation.  We're essentially putting the guts of
 *     the clone channel into the original channel.  Start by killing off the original
 *     channel's backend.   I'm not sure we're going to keep this function, because
 *     while the features are nice, the cost is very high in terms of pure nastiness. XXX
 * \param chan Channel to masquerade
 */
int ast_do_masquerade(struct ast_channel *chan);

/*!
 * \brief Find bridged channel
 *
 * \note This function does _not_ return a reference to the bridged channel.
 * The reason for this is mostly historical.  It _should_ return a reference,
 * but it will take a lot of work to make the code base account for that.
 * So, for now, the old rules still apply for how to handle this function.
 * If this function is being used from the channel thread that owns the channel,
 * then a reference is already held, and channel locking is not required to
 * guarantee that the channel will stay around.  If this function is used
 * outside of the associated channel thread, the channel parameter 'chan'
 * MUST be locked before calling this function.  Also, 'chan' must remain locked
 * for the entire time that the result of this function is being used.
 *
 * \param chan Current channel
 *
 * \return A pointer to the bridged channel
*/
struct ast_channel *ast_bridged_channel(struct ast_channel *chan);

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
 * \brief Check if the channel can run in internal timing mode.
 * \param chan The channel to check
 * \return boolean
 *
 * \details
 * This function will return 1 if internal timing is enabled and the timing
 * device is available.
 */
int ast_internal_timing_enabled(struct ast_channel *chan);

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

#ifndef HAVE_TIMERSUB
static inline void timersub(struct timeval *tvend, struct timeval *tvstart, struct timeval *tvdiff)
{
	tvdiff->tv_sec = tvend->tv_sec - tvstart->tv_sec;
	tvdiff->tv_usec = tvend->tv_usec - tvstart->tv_usec;
	if (tvdiff->tv_usec < 0) {
		tvdiff->tv_sec --;
		tvdiff->tv_usec += 1000000;
	}

}
#endif

/*!
 * \brief Waits for activity on a group of channels
 * \param nfds the maximum number of file descriptors in the sets
 * \param rfds file descriptors to check for read availability
 * \param wfds file descriptors to check for write availability
 * \param efds file descriptors to check for exceptions (OOB data)
 * \param tvp timeout while waiting for events
 * \details
 * This is the same as a standard select(), except it guarantees the
 * behaviour where the passed struct timeval is updated with how much
 * time was not slept while waiting for the specified events
 */
static inline int ast_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tvp)
{
#ifdef __linux__
	return select(nfds, rfds, wfds, efds, tvp);
#else
	if (tvp) {
		struct timeval tv, tvstart, tvend, tvlen;
		int res;

		tv = *tvp;
		gettimeofday(&tvstart, NULL);
		res = select(nfds, rfds, wfds, efds, tvp);
		gettimeofday(&tvend, NULL);
		timersub(&tvend, &tvstart, &tvlen);
		timersub(&tv, &tvlen, tvp);
		if (tvp->tv_sec < 0 || (tvp->tv_sec == 0 && tvp->tv_usec < 0)) {
			tvp->tv_sec = 0;
			tvp->tv_usec = 0;
		}
		return res;
	}
	else
		return select(nfds, rfds, wfds, efds, NULL);
#endif
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
	if (ast_test_flag(c, AST_FLAG_BLOCKING)) {\
		if (option_debug) \
			ast_log(LOG_DEBUG, "Thread %ld Blocking '%s', already blocked by thread %ld in procedure %s\n", (long) pthread_self(), (c)->name, (long) (c)->blocker, (c)->blockproc); \
	} else { \
		(c)->blocker = pthread_self(); \
		(c)->blockproc = __PRETTY_FUNCTION__; \
		ast_set_flag(c, AST_FLAG_BLOCKING); \
	} } while (0)

ast_group_t ast_get_group(const char *s);

/*! \brief print call- and pickup groups into buffer */
char *ast_print_group(char *buf, int buflen, ast_group_t group);

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
 * \arg i the itereator to destroy
 *
 * This function is used to destroy a channel iterator that was retrieved by
 * using one of the channel_iterator_new() functions.
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
 * \arg exten The extension that channels must be in
 * \arg context The context that channels must be in (optional)
 *
 * After creating an iterator using this function, the ast_channel_iterator_next()
 * function can be used to iterate through all channels that are currently
 * in the specified context and extension.
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
 * \arg name channel name or channel uniqueid to match
 * \arg name_len number of characters in the channel name to match on.  This
 *      would be used to match based on name prefix.  If matching on the full
 *      channel name is desired, then this parameter should be 0.
 *
 * After creating an iterator using this function, the ast_channel_iterator_next()
 * function can be used to iterate through all channels that exist that have
 * the specified name or name prefix.
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
 * After creating an iterator using this function, the ast_channel_iterator_next()
 * function can be used to iterate through all channels that exist.
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
 * \arg i the channel iterator that was created using one of the
 *  channel_iterator_new() functions.
 *
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
 * This function executes a callback one time for each active channel on the
 * system.  The channel is provided as an argument to the function.
 *
 * \since 1.8
 */
struct ast_channel *ast_channel_callback(ao2_callback_data_fn *cb_fn, void *arg,
		void *data, int ao2_flags);

/*! @{ Channel search functions */

/*!
 * \brief Find a channel by name
 *
 * \arg name the name or uniqueid of the channel to search for
 *
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
 * \arg name The channel name or uniqueid prefix to search for
 * \arg name_len Only search for up to this many characters from the name
 *
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
 * \arg exten the extension to search for
 * \arg context the context to search for (optional)
 *
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
  \brief propagate the linked id between chan and peer
 */
void ast_channel_set_linkgroup(struct ast_channel *chan, struct ast_channel *peer);


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
 * \brief Initialize the given party subadress structure using the given guide
 * for a set update operation.
 *
 * \details
 * The initialization is needed to allow a set operation to know if a
 * value needs to be updated.  Simple integers need the guide's original
 * value in case the set operation is not trying to set a new value.
 * String values are simply set to NULL pointers if they are not going
 * to be updated.
 *
 * \param init Party Subaddress structure to initialize.
 * \param guide Source party subaddress to use as a guide in initializing.
 *
 * \return Nothing
 */
void ast_party_subaddress_set_init(struct ast_party_subaddress *init, const struct ast_party_subaddress *guide);

/*!
 * \since 1.8
 * \brief Set the source party subaddress information into the destination party subaddress.
 *
 * \param dest Destination party subaddress
 * \param src Source party subaddress
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
 * \brief Initialize the given party id structure.
 * \since 1.8
 *
 * \param init Party id structure to initialize.
 *
 * \return Nothing
 */
void ast_party_id_init(struct ast_party_id *init);

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
void ast_party_caller_copy(struct ast_callerid *dest, const struct ast_callerid *src);

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
 * \param src The source connected line to use as a guide to set the dest
 * \param dest The connected line one wishes to update
 *
 * \return Nada
 */
void ast_party_connected_line_set(struct ast_party_connected_line *dest, const struct ast_party_connected_line *src);

/*!
 * \since 1.8
 * \brief Collect the caller party information into a connected line structure.
 *
 * \param connected Collected caller information for the connected line
 * \param cid Caller information.
 *
 * \return Nothing
 *
 * \warning This is a shallow copy.
 * \warning DO NOT call ast_party_connected_line_free() on the filled in
 * connected line structure!
 */
void ast_party_connected_line_collect_caller(struct ast_party_connected_line *connected, struct ast_callerid *cid);

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
void ast_connected_line_copy_from_caller(struct ast_party_connected_line *dest, const struct ast_callerid *src);

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
void ast_connected_line_copy_to_caller(struct ast_callerid *dest, const struct ast_party_connected_line *src);

/*!
 * \since 1.8
 * \brief Set the connected line information in the Asterisk channel
 *
 * \param chan Asterisk channel to set connected line information
 * \param connected Connected line information
 *
 * \return Nothing
 *
 * \note The channel does not need to be locked before calling this function.
 */
void ast_channel_set_connected_line(struct ast_channel *chan, const struct ast_party_connected_line *connected);

/*!
 * \since 1.8
 * \brief Build the connected line information data frame.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param connected Connected line information
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
int ast_connected_line_build_data(unsigned char *data, size_t datalen, const struct ast_party_connected_line *connected);

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
 *
 * \return Nothing
 */
void ast_channel_update_connected_line(struct ast_channel *chan, const struct ast_party_connected_line *connected);

/*!
 * \since 1.8
 * \brief Queue a connected line update frame on a channel
 *
 * \param chan Asterisk channel to indicate connected line information
 * \param connected Connected line information
 *
 * \return Nothing
 */
void ast_channel_queue_connected_line_update(struct ast_channel *chan, const struct ast_party_connected_line *connected);

/*!
 * \since 1.8
 * \brief Set the redirecting id information in the Asterisk channel
 *
 * \param chan Asterisk channel to set redirecting id information
 * \param redirecting Redirecting id information
 *
 * \return Nothing
 *
 * \note The channel does not need to be locked before calling this function.
 */
void ast_channel_set_redirecting(struct ast_channel *chan, const struct ast_party_redirecting *redirecting);

/*!
 * \since 1.8
 * \brief Build the redirecting id data frame.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param redirecting Redirecting id information
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
int ast_redirecting_build_data(unsigned char *data, size_t datalen, const struct ast_party_redirecting *redirecting);

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
 *
 * \return Nothing
 */
void ast_channel_update_redirecting(struct ast_channel *chan, const struct ast_party_redirecting *redirecting);

/*!
 * \since 1.8
 * \brief Queue a redirecting update frame on a channel
 *
 * \param chan Asterisk channel to indicate redirecting id information
 * \param redirecting Redirecting id information
 *
 * \return Nothing
 */
void ast_channel_queue_redirecting_update(struct ast_channel *chan, const struct ast_party_redirecting *redirecting);

/*!
 * \since 1.8
 * \brief Run a connected line interception macro and update a channel's connected line
 * information
 *
 * Whenever we want to update a channel's connected line information, we may need to run
 * a macro so that an administrator can manipulate the information before sending it
 * out. This function both runs the macro and sends the update to the channel.
 *
 * \param autoservice_chan Channel to place into autoservice while the macro is running.
 * 	It is perfectly safe for this to be NULL
 * \param macro_chan The channel to run the macro on. Also the channel from which we
 * 	determine which macro we need to run.
 * \param connected_info Either an ast_party_connected_line or ast_frame pointer of type
 * 	AST_CONTROL_CONNECTED_LINE
 * \param caller If true, then run CONNECTED_LINE_CALLER_SEND_MACRO, otherwise run
 * 	CONNECTED_LINE_CALLEE_SEND_MACRO
 * \param frame If true, then connected_info is an ast_frame pointer, otherwise it is an
 * 	ast_party_connected_line pointer.
 * \retval 0 Success
 * \retval -1 Either the macro does not exist, or there was an error while attempting to
 * 	run the macro
 *
 * \todo Have multiple return codes based on the MACRO_RESULT
 * \todo Make constants so that caller and frame can be more expressive than just '1' and
 * 	'0'
 */
int ast_channel_connected_line_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const void *connected_info, int caller, int frame);

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
 * \param is_caller If true, then run REDIRECTING_CALLER_SEND_MACRO, otherwise run
 * REDIRECTING_CALLEE_SEND_MACRO
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

#endif /* _ASTERISK_CHANNEL_H */
