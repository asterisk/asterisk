/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#ifndef CHANNEL_PRIVATE_H_
#define CHANNEL_PRIVATE_H_

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief Channel UniqueId structure
 * \note channel creation time used for determining LinkedId Propagation
 */
struct ast_channel_id {
	time_t creation_time;				/*!< Creation time */
	int creation_unique;				/*!< sub-second unique value */
	char unique_id[AST_MAX_UNIQUEID];	/*!< Unique Identifier */
	char tenant_id[AST_MAX_TENANT_ID];	/*!< Multi-tenant identifier */
};

/*!
 * \brief Main Channel structure associated with a channel.
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
	struct ast_channel *masq;			/*!< Channel that will masquerade as us */
	struct ast_channel *masqr;			/*!< Who we are masquerading as */
	const char *blockproc;				/*!< Procedure causing blocking */
	const char *appl;				/*!< Current application */
	const char *data;				/*!< Data passed to current application */
	struct ast_sched_context *sched;                /*!< Schedule context */
	struct ast_filestream *stream;			/*!< Stream itself. */
	struct ast_filestream *vstream;			/*!< Video Stream itself. */
	ast_timing_func_t timingfunc;
	void *timingdata;
	struct ast_pbx *pbx;				/*!< PBX private structure for this channel */
	struct ast_trans_pvt *writetrans;		/*!< Write translation path */
	struct ast_trans_pvt *readtrans;		/*!< Read translation path */
	struct ast_audiohook_list *audiohooks;
	struct ast_framehook_list *framehooks;
	struct ast_cdr *cdr;				/*!< Call Detail Record */
	struct ast_tone_zone *zone;			/*!< Tone zone as set in indications.conf or
							 *   in the CHANNEL dialplan function */
	ast_callid callid;			/*!< Bound call identifier pointer */
	struct ao2_container *dialed_causes;		/*!< Contains tech-specific and Asterisk cause data from dialed channels */

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);         /*!< ASCII unique channel name */
		AST_STRING_FIELD(language);     /*!< Language requested for voice prompts */
		AST_STRING_FIELD(musicclass);   /*!< Default music class */
		AST_STRING_FIELD(latest_musicclass);   /*!< Latest active music class */
		AST_STRING_FIELD(accountcode);  /*!< Account code for billing */
		AST_STRING_FIELD(peeraccount);  /*!< Peer account code for billing */
		AST_STRING_FIELD(userfield);    /*!< Userfield for CEL billing */
		AST_STRING_FIELD(call_forward); /*!< Where to forward to if asked to dial on this interface */
		AST_STRING_FIELD(parkinglot);   /*! Default parking lot, if empty, default parking lot  */
		AST_STRING_FIELD(hangupsource); /*! Who is responsible for hanging up this channel */
		AST_STRING_FIELD(dialcontext);  /*!< Dial: Extension context that we were called from */
	);

	struct ast_channel_id uniqueid;		/*!< Unique Channel Identifier - can be specified on creation */
	struct ast_channel_id linkedid;		/*!< Linked Channel Identifier - oldest propagated when bridged */

	struct timeval whentohangup; /*!< Non-zero, set to actual time when channel is to be hung up */
	pthread_t blocker;           /*!< If anyone is blocking, this is them */

	/*!
	 * \brief Dialed/Called information.
	 * \note Set on incoming channels to indicate the originally dialed party.
	 * \note Dialed Number Identifier (DNID)
	 */
	struct ast_party_dialed dialed;

	/*!
	 * \brief Channel Caller ID information.
	 * \note The caller id information is the caller id of this
	 * channel when it is used to initiate a call.
	 */
	struct ast_party_caller caller;

	/*!
	 * \brief Channel Connected Line ID information.
	 * \note The connected line information identifies the channel
	 * connected/bridged to this channel.
	 */
	struct ast_party_connected_line connected;

	/*!
	 * \brief Channel Connected Line ID information that was last indicated.
	 */
	struct ast_party_connected_line connected_indicated;

	/*! \brief Redirecting/Diversion information */
	struct ast_party_redirecting redirecting;

	struct ast_frame dtmff;				/*!< DTMF frame */
	struct varshead varshead;			/*!< A linked list for channel variables. See \ref AstChanVar */
	ast_group_t callgroup;				/*!< Call group for call pickups */
	ast_group_t pickupgroup;			/*!< Pickup group - which calls groups can be picked up? */
	struct ast_namedgroups *named_callgroups;	/*!< Named call group for call pickups */
	struct ast_namedgroups *named_pickupgroups;	/*!< Named pickup group - which call groups can be picked up? */
	struct timeval creationtime;			/*!< The time of channel creation */
	struct timeval answertime;				/*!< The time the channel was answered */
	struct ast_readq_list readq;
	struct ast_jb jb;				/*!< The jitterbuffer state */
	struct timeval dtmf_tv;				/*!< The time that an in process digit began, or the last digit ended */
	struct ast_hangup_handler_list hangup_handlers;/*!< Hangup handlers on the channel. */
	struct ast_datastore_list datastores; /*!< Data stores on the channel */
	struct ast_autochan_list autochans; /*!< Autochans on the channel */
	unsigned long insmpl;				/*!< Track the read/written samples for monitor use */
	unsigned long outsmpl;				/*!< Track the read/written samples for monitor use */

	int blocker_tid;					/*!< If anyone is blocking, this is their thread id */
	AST_VECTOR(, int) fds;				/*!< File descriptors for channel -- Drivers will poll on
							 *   these file descriptors, so at least one must be non -1.
							 *   See \arg \ref AstFileDesc */
	int softhangup;				/*!< Whether or not we have been hung up...  Do not set this value
							 *   directly, use ast_softhangup() */
	int fdno;					/*!< Which fd had an event detected on */
	int streamid;					/*!< For streaming playback, the schedule ID */
	int vstreamid;					/*!< For streaming video playback, the schedule ID */
	struct ast_format *oldwriteformat;  /*!< Original writer format */
	int timingfd;					/*!< Timing fd */
	enum ast_channel_state state;			/*!< State of line -- Don't write directly, use ast_setstate() */
	int rings;					/*!< Number of rings so far */
	int priority;					/*!< Dialplan: Current extension priority */
	int amaflags;					/*!< Set BEFORE PBX is started to determine AMA flags */
	enum ast_channel_adsicpe adsicpe;		/*!< Whether or not ADSI is detected on CPE */
	unsigned int fin;				/*!< Frames in counters. The high bit is a debug mask, so
							 *   the counter is only in the remaining bits */
	unsigned int fout;				/*!< Frames out counters. The high bit is a debug mask, so
							 *   the counter is only in the remaining bits */
	int hangupcause;                /*!< Why is the channel hanged up. See causes.h */
	int tech_hangupcause;           /*!< Technology-specific off-nominal hangup cause.
                                     * Leave set to 0 for nominal call termination.
                                     */
	unsigned int finalized:1;       /*!< Whether or not the channel has been successfully allocated */
	struct ast_flags flags;				/*!< channel flags of AST_FLAG_ type */
	int alertpipe[2];
	struct ast_format_cap *nativeformats;         /*!< Kinds of data this channel can natively handle */
	struct ast_format *readformat;            /*!< Requested read format (after translation) */
	struct ast_format *writeformat;           /*!< Requested write format (before translation) */
	struct ast_format *rawreadformat;         /*!< Raw read format (before translation) */
	struct ast_format *rawwriteformat;        /*!< Raw write format (after translation) */
	unsigned int emulate_dtmf_duration;		/*!< Number of ms left to emulate DTMF for */
	int visible_indication;                         /*!< Indication currently playing on the channel */
	int hold_state;							/*!< Current Hold/Unhold state */

	unsigned short transfercapability;		/*!< ISDN Transfer Capability - AST_FLAG_DIGITAL is not enough */

	struct ast_bridge *bridge;                      /*!< Bridge this channel is participating in */
	struct ast_bridge_channel *bridge_channel;/*!< The bridge_channel this channel is linked with. */
	struct ast_timer *timer;			/*!< timer object that provided timingfd */

	char context[AST_MAX_CONTEXT];			/*!< Dialplan: Current extension context */
	char exten[AST_MAX_EXTENSION];			/*!< Dialplan: Current extension number */
	char lastcontext[AST_MAX_CONTEXT];		/*!< Dialplan: Previous extension context */
	char lastexten[AST_MAX_EXTENSION];		/*!< Dialplan: Previous extension number */
	char unbridged;							/*!< non-zero if the bridge core needs to re-evaluate the current
											 bridging technology which is in use by this channel's bridge. */
	char is_t38_active;						/*!< non-zero if T.38 is active on this channel. */
	char dtmf_digit_to_emulate;			/*!< Digit being emulated */
	char sending_dtmf_digit;			/*!< Digit this channel is currently sending out. (zero if not sending) */
	struct timeval sending_dtmf_tv;		/*!< The time this channel started sending the current digit. (Invalid if sending_dtmf_digit is zero.) */
	struct stasis_topic *topic;		/*!< Topic for this channel */
	struct stasis_forward *channel_forward; /*!< Subscription for event forwarding to all channel topic */
	struct stasis_forward *endpoint_forward;	/*!< Subscription for event forwarding to endpoint's topic */
	struct ast_stream_topology *stream_topology; /*!< Stream topology */
	void *stream_topology_change_source; /*!< Source that initiated a stream topology change */
	struct ast_stream *default_streams[AST_MEDIA_TYPE_END]; /*!< Default streams indexed by media type */
	struct ast_channel_snapshot *snapshot; /*!< The current up to date snapshot of the channel */
	struct ast_flags snapshot_segment_flags; /*!< Flags regarding the segments of the snapshot */
	int linked_in_container; /*!< Whether this channel is linked in a storage container */
};

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* CHANNEL_PRIVATE_H_ */
