/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
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
 *
 * \brief Channel Accessor API
 *
 * This file is intended to be the only file that ever accesses the
 * internals of an ast_channel. All other files should use the
 * accessor functions defined here.
 *
 * \author Terry Wilson
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <unistd.h>
#include <fcntl.h>

#include "asterisk/paths.h"
#include "asterisk/channel.h"
#include "asterisk/channel_internal.h"
#include "asterisk/data.h"
#include "asterisk/endpoints.h"
#include "asterisk/indications.h"
#include "asterisk/stasis_cache_pattern.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stringfields.h"
#include "asterisk/test.h"

/*!
 * \brief Channel UniqueId structure
 * \note channel creation time used for determining LinkedId Propagation
 */
struct ast_channel_id {
	time_t creation_time;				/*!< Creation time */
	int creation_unique;				/*!< sub-second unique value */
	char unique_id[AST_MAX_UNIQUEID];	/*!< Unique Identifier */
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
	struct ast_channel_monitor *monitor;		/*!< Channel monitoring */
	struct ast_callid *callid;			/*!< Bound call identifier pointer */
#ifdef HAVE_EPOLL
	struct ast_epoll_data *epfd_data[AST_MAX_FDS];
#endif
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

	int fds[AST_MAX_FDS];				/*!< File descriptors for channel -- Drivers will poll on
							 *   these file descriptors, so at least one must be non -1.
							 *   See \arg \ref AstFileDesc */
	int softhangup;				/*!< Whether or not we have been hung up...  Do not set this value
							 *   directly, use ast_softhangup() */
	int unbridged;              /*!< If non-zero, the bridge core needs to re-evaluate the current
	                                 bridging technology which is in use by this channel's bridge. */
	int fdno;					/*!< Which fd had an event detected on */
	int streamid;					/*!< For streaming playback, the schedule ID */
	int vstreamid;					/*!< For streaming video playback, the schedule ID */
	struct ast_format *oldwriteformat;  /*!< Original writer format */
	int timingfd;					/*!< Timing fd */
	enum ast_channel_state state;			/*!< State of line -- Don't write directly, use ast_setstate() */
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
	unsigned int finalized:1;       /*!< Whether or not the channel has been successfully allocated */
	struct ast_flags flags;				/*!< channel flags of AST_FLAG_ type */
	int alertpipe[2];
	struct ast_format_cap *nativeformats;         /*!< Kinds of data this channel can natively handle */
	struct ast_format *readformat;            /*!< Requested read format (after translation) */
	struct ast_format *writeformat;           /*!< Requested write format (before translation) */
	struct ast_format *rawreadformat;         /*!< Raw read format (before translation) */
	struct ast_format *rawwriteformat;        /*!< Raw write format (after translation) */
	unsigned int emulate_dtmf_duration;		/*!< Number of ms left to emulate DTMF for */
#ifdef HAVE_EPOLL
	int epfd;
#endif
	int visible_indication;                         /*!< Indication currently playing on the channel */
	int hold_state;							/*!< Current Hold/Unhold state */

	unsigned short transfercapability;		/*!< ISDN Transfer Capability - AST_FLAG_DIGITAL is not enough */

	struct ast_bridge *bridge;                      /*!< Bridge this channel is participating in */
	struct ast_bridge_channel *bridge_channel;/*!< The bridge_channel this channel is linked with. */
	struct ast_timer *timer;			/*!< timer object that provided timingfd */

	char context[AST_MAX_CONTEXT];			/*!< Dialplan: Current extension context */
	char exten[AST_MAX_EXTENSION];			/*!< Dialplan: Current extension number */
	char macrocontext[AST_MAX_CONTEXT];		/*!< Macro: Current non-macro context. See app_macro.c */
	char macroexten[AST_MAX_EXTENSION];		/*!< Macro: Current non-macro extension. See app_macro.c */
	char dtmf_digit_to_emulate;			/*!< Digit being emulated */
	char sending_dtmf_digit;			/*!< Digit this channel is currently sending out. (zero if not sending) */
	struct timeval sending_dtmf_tv;		/*!< The time this channel started sending the current digit. (Invalid if sending_dtmf_digit is zero.) */
	struct stasis_cp_single *topics;		/*!< Topic for all channel's events */
	struct stasis_forward *endpoint_forward;	/*!< Subscription for event forwarding to endpoint's topic */
	struct stasis_forward *endpoint_cache_forward; /*!< Subscription for cache updates to endpoint's topic */
};

/*! \brief The monotonically increasing integer counter for channel uniqueids */
static int uniqueint;

/* AST_DATA definitions, which will probably have to be re-thought since the channel will be opaque */

#if 0	/* XXX AstData: ast_callerid no longer exists. (Equivalent code not readily apparent.) */
#define DATA_EXPORT_CALLERID(MEMBER)				\
	MEMBER(ast_callerid, cid_dnid, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_num, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_name, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_ani, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_pres, AST_DATA_INTEGER)	\
	MEMBER(ast_callerid, cid_ani2, AST_DATA_INTEGER)	\
	MEMBER(ast_callerid, cid_tag, AST_DATA_STRING)

AST_DATA_STRUCTURE(ast_callerid, DATA_EXPORT_CALLERID);
#endif

#define DATA_EXPORT_CHANNEL(MEMBER)						\
	MEMBER(ast_channel, blockproc, AST_DATA_STRING)				\
	MEMBER(ast_channel, appl, AST_DATA_STRING)				\
	MEMBER(ast_channel, data, AST_DATA_STRING)				\
	MEMBER(ast_channel, name, AST_DATA_STRING) \
	MEMBER(ast_channel, language, AST_DATA_STRING)				\
	MEMBER(ast_channel, musicclass, AST_DATA_STRING)			\
	MEMBER(ast_channel, accountcode, AST_DATA_STRING)			\
	MEMBER(ast_channel, peeraccount, AST_DATA_STRING)			\
	MEMBER(ast_channel, userfield, AST_DATA_STRING)				\
	MEMBER(ast_channel, call_forward, AST_DATA_STRING)			\
	MEMBER(ast_channel, parkinglot, AST_DATA_STRING)			\
	MEMBER(ast_channel, hangupsource, AST_DATA_STRING)			\
	MEMBER(ast_channel, dialcontext, AST_DATA_STRING)			\
	MEMBER(ast_channel, rings, AST_DATA_INTEGER)				\
	MEMBER(ast_channel, priority, AST_DATA_INTEGER)				\
	MEMBER(ast_channel, macropriority, AST_DATA_INTEGER)			\
	MEMBER(ast_channel, adsicpe, AST_DATA_INTEGER)				\
	MEMBER(ast_channel, fin, AST_DATA_UNSIGNED_INTEGER)			\
	MEMBER(ast_channel, fout, AST_DATA_UNSIGNED_INTEGER)			\
	MEMBER(ast_channel, emulate_dtmf_duration, AST_DATA_UNSIGNED_INTEGER)	\
	MEMBER(ast_channel, visible_indication, AST_DATA_INTEGER)		\
	MEMBER(ast_channel, context, AST_DATA_STRING)				\
	MEMBER(ast_channel, exten, AST_DATA_STRING)				\
	MEMBER(ast_channel, macrocontext, AST_DATA_STRING)			\
	MEMBER(ast_channel, macroexten, AST_DATA_STRING)

AST_DATA_STRUCTURE(ast_channel, DATA_EXPORT_CHANNEL);

static void channel_data_add_flags(struct ast_data *tree,
	struct ast_channel *chan)
{
	ast_data_add_bool(tree, "DEFER_DTMF", ast_test_flag(ast_channel_flags(chan), AST_FLAG_DEFER_DTMF));
	ast_data_add_bool(tree, "WRITE_INT", ast_test_flag(ast_channel_flags(chan), AST_FLAG_WRITE_INT));
	ast_data_add_bool(tree, "BLOCKING", ast_test_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING));
	ast_data_add_bool(tree, "ZOMBIE", ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE));
	ast_data_add_bool(tree, "EXCEPTION", ast_test_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION));
	ast_data_add_bool(tree, "MOH", ast_test_flag(ast_channel_flags(chan), AST_FLAG_MOH));
	ast_data_add_bool(tree, "SPYING", ast_test_flag(ast_channel_flags(chan), AST_FLAG_SPYING));
	ast_data_add_bool(tree, "IN_AUTOLOOP", ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP));
	ast_data_add_bool(tree, "OUTGOING", ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING));
	ast_data_add_bool(tree, "IN_DTMF", ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_DTMF));
	ast_data_add_bool(tree, "EMULATE_DTMF", ast_test_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF));
	ast_data_add_bool(tree, "END_DTMF_ONLY", ast_test_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY));
	ast_data_add_bool(tree, "MASQ_NOSTREAM", ast_test_flag(ast_channel_flags(chan), AST_FLAG_MASQ_NOSTREAM));
	ast_data_add_bool(tree, "BRIDGE_HANGUP_RUN", ast_test_flag(ast_channel_flags(chan), AST_FLAG_BRIDGE_HANGUP_RUN));
	ast_data_add_bool(tree, "DISABLE_WORKAROUNDS", ast_test_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_WORKAROUNDS));
	ast_data_add_bool(tree, "DISABLE_DEVSTATE_CACHE", ast_test_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_DEVSTATE_CACHE));
	ast_data_add_bool(tree, "BRIDGE_DUAL_REDIRECT_WAIT", ast_test_flag(ast_channel_flags(chan), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT));
	ast_data_add_bool(tree, "ORIGINATED", ast_test_flag(ast_channel_flags(chan), AST_FLAG_ORIGINATED));
	ast_data_add_bool(tree, "DEAD", ast_test_flag(ast_channel_flags(chan), AST_FLAG_DEAD));
}

int ast_channel_data_add_structure(struct ast_data *tree,
	struct ast_channel *chan, int add_bridged)
{
	struct ast_data *data_bridged;
	struct ast_data *data_cdr;
	struct ast_data *data_flags;
	struct ast_data *data_zones;
	struct ast_data *enum_node;
	struct ast_data *data_softhangup;
#if 0	/* XXX AstData: ast_callerid no longer exists. (Equivalent code not readily apparent.) */
	struct ast_data *data_callerid;
	char value_str[100];
#endif

	if (!tree) {
		return -1;
	}

	ast_data_add_structure(ast_channel, tree, chan);

	if (add_bridged) {
		RAII_VAR(struct ast_channel *, bc, ast_channel_bridge_peer(chan), ast_channel_cleanup);
		if (bc) {
			data_bridged = ast_data_add_node(tree, "bridged");
			if (!data_bridged) {
				return -1;
			}
			ast_channel_data_add_structure(data_bridged, bc, 0);
		}
	}

	ast_data_add_str(tree, "uniqueid", ast_channel_uniqueid(chan));
	ast_data_add_str(tree, "linkedid", ast_channel_linkedid(chan));

	ast_data_add_codec(tree, "oldwriteformat", ast_channel_oldwriteformat(chan));
	ast_data_add_codec(tree, "readformat", ast_channel_readformat(chan));
	ast_data_add_codec(tree, "writeformat", ast_channel_writeformat(chan));
	ast_data_add_codec(tree, "rawreadformat", ast_channel_rawreadformat(chan));
	ast_data_add_codec(tree, "rawwriteformat", ast_channel_rawwriteformat(chan));
	ast_data_add_codecs(tree, "nativeformats", ast_channel_nativeformats(chan));

	/* state */
	enum_node = ast_data_add_node(tree, "state");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_state2str(ast_channel_state(chan)));
	ast_data_add_int(enum_node, "value", ast_channel_state(chan));

	/* hangupcause */
	enum_node = ast_data_add_node(tree, "hangupcause");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_cause2str(ast_channel_hangupcause(chan)));
	ast_data_add_int(enum_node, "value", ast_channel_hangupcause(chan));

	/* amaflags */
	enum_node = ast_data_add_node(tree, "amaflags");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_channel_amaflags2string(ast_channel_amaflags(chan)));
	ast_data_add_int(enum_node, "value", ast_channel_amaflags(chan));

	/* transfercapability */
	enum_node = ast_data_add_node(tree, "transfercapability");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_transfercapability2str(ast_channel_transfercapability(chan)));
	ast_data_add_int(enum_node, "value", ast_channel_transfercapability(chan));

	/* _softphangup */
	data_softhangup = ast_data_add_node(tree, "softhangup");
	if (!data_softhangup) {
		return -1;
	}
	ast_data_add_bool(data_softhangup, "dev", ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_DEV);
	ast_data_add_bool(data_softhangup, "asyncgoto", ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO);
	ast_data_add_bool(data_softhangup, "shutdown", ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_SHUTDOWN);
	ast_data_add_bool(data_softhangup, "timeout", ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_TIMEOUT);
	ast_data_add_bool(data_softhangup, "appunload", ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_APPUNLOAD);
	ast_data_add_bool(data_softhangup, "explicit", ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_EXPLICIT);

	/* channel flags */
	data_flags = ast_data_add_node(tree, "flags");
	if (!data_flags) {
		return -1;
	}
	channel_data_add_flags(data_flags, chan);

	ast_data_add_uint(tree, "timetohangup", ast_channel_whentohangup(chan)->tv_sec);

#if 0	/* XXX AstData: ast_callerid no longer exists. (Equivalent code not readily apparent.) */
	/* callerid */
	data_callerid = ast_data_add_node(tree, "callerid");
	if (!data_callerid) {
		return -1;
	}
	ast_data_add_structure(ast_callerid, data_callerid, &(chan->cid));
	/* insert the callerid ton */
	enum_node = ast_data_add_node(data_callerid, "cid_ton");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_int(enum_node, "value", chan->cid.cid_ton);
	snprintf(value_str, sizeof(value_str), "TON: %s/Plan: %s",
		party_number_ton2str(chan->cid.cid_ton),
		party_number_plan2str(chan->cid.cid_ton));
	ast_data_add_str(enum_node, "text", value_str);
#endif

	/* tone zone */
	if (ast_channel_zone(chan)) {
		data_zones = ast_data_add_node(tree, "zone");
		if (!data_zones) {
			return -1;
		}
		ast_tone_zone_data_add_structure(data_zones, ast_channel_zone(chan));
	}

	/* insert cdr */
	data_cdr = ast_data_add_node(tree, "cdr");
	if (!data_cdr) {
		return -1;
	}

	return 0;
}

int ast_channel_data_cmp_structure(const struct ast_data_search *tree,
	struct ast_channel *chan, const char *structure_name)
{
	return ast_data_search_cmp_structure(tree, ast_channel, chan, structure_name);
}

/* ACCESSORS */

#define DEFINE_STRINGFIELD_SETTERS_FOR(field, publish, assert_on_null) \
void ast_channel_##field##_set(struct ast_channel *chan, const char *value) \
{ \
	if ((assert_on_null)) ast_assert(!ast_strlen_zero(value)); \
	if (!strcmp(value, chan->field)) return; \
	ast_string_field_set(chan, field, value); \
	if (publish && ast_channel_internal_is_finalized(chan)) ast_channel_publish_snapshot(chan); \
} \
  \
void ast_channel_##field##_build_va(struct ast_channel *chan, const char *fmt, va_list ap) \
{ \
	ast_string_field_build_va(chan, field, fmt, ap); \
	if (publish && ast_channel_internal_is_finalized(chan)) ast_channel_publish_snapshot(chan); \
} \
void ast_channel_##field##_build(struct ast_channel *chan, const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	ast_channel_##field##_build_va(chan, fmt, ap); \
	va_end(ap); \
}

DEFINE_STRINGFIELD_SETTERS_FOR(name, 0, 1);
DEFINE_STRINGFIELD_SETTERS_FOR(language, 1, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(musicclass, 0, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(latest_musicclass, 0, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(accountcode, 1, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(peeraccount, 1, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(userfield, 0, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(call_forward, 0, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(parkinglot, 0, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(hangupsource, 0, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(dialcontext, 0, 0);

#define DEFINE_STRINGFIELD_GETTER_FOR(field) const char *ast_channel_##field(const struct ast_channel *chan) \
{ \
	return chan->field; \
}

DEFINE_STRINGFIELD_GETTER_FOR(name);
DEFINE_STRINGFIELD_GETTER_FOR(language);
DEFINE_STRINGFIELD_GETTER_FOR(musicclass);
DEFINE_STRINGFIELD_GETTER_FOR(latest_musicclass);
DEFINE_STRINGFIELD_GETTER_FOR(accountcode);
DEFINE_STRINGFIELD_GETTER_FOR(peeraccount);
DEFINE_STRINGFIELD_GETTER_FOR(userfield);
DEFINE_STRINGFIELD_GETTER_FOR(call_forward);
DEFINE_STRINGFIELD_GETTER_FOR(parkinglot);
DEFINE_STRINGFIELD_GETTER_FOR(hangupsource);
DEFINE_STRINGFIELD_GETTER_FOR(dialcontext);

const char *ast_channel_uniqueid(const struct ast_channel *chan)
{
	ast_assert(chan->uniqueid.unique_id[0] != '\0');
	return chan->uniqueid.unique_id;
}

const char *ast_channel_linkedid(const struct ast_channel *chan)
{
	ast_assert(chan->linkedid.unique_id[0] != '\0');
	return chan->linkedid.unique_id;
}

const char *ast_channel_appl(const struct ast_channel *chan)
{
	return chan->appl;
}
void ast_channel_appl_set(struct ast_channel *chan, const char *value)
{
	chan->appl = value;
}
const char *ast_channel_blockproc(const struct ast_channel *chan)
{
	return chan->blockproc;
}
void ast_channel_blockproc_set(struct ast_channel *chan, const char *value)
{
	chan->blockproc = value;
}
const char *ast_channel_data(const struct ast_channel *chan)
{
	return chan->data;
}
void ast_channel_data_set(struct ast_channel *chan, const char *value)
{
	chan->data = value;
}

const char *ast_channel_context(const struct ast_channel *chan)
{
	return chan->context;
}
void ast_channel_context_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->context, value, sizeof(chan->context));
}
const char *ast_channel_exten(const struct ast_channel *chan)
{
	return chan->exten;
}
void ast_channel_exten_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->exten, value, sizeof(chan->exten));
}
const char *ast_channel_macrocontext(const struct ast_channel *chan)
{
	return chan->macrocontext;
}
void ast_channel_macrocontext_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->macrocontext, value, sizeof(chan->macrocontext));
}
const char *ast_channel_macroexten(const struct ast_channel *chan)
{
	return chan->macroexten;
}
void ast_channel_macroexten_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->macroexten, value, sizeof(chan->macroexten));
}

char ast_channel_dtmf_digit_to_emulate(const struct ast_channel *chan)
{
	return chan->dtmf_digit_to_emulate;
}
void ast_channel_dtmf_digit_to_emulate_set(struct ast_channel *chan, char value)
{
	chan->dtmf_digit_to_emulate = value;
}

char ast_channel_sending_dtmf_digit(const struct ast_channel *chan)
{
	return chan->sending_dtmf_digit;
}
void ast_channel_sending_dtmf_digit_set(struct ast_channel *chan, char value)
{
	chan->sending_dtmf_digit = value;
}

struct timeval ast_channel_sending_dtmf_tv(const struct ast_channel *chan)
{
	return chan->sending_dtmf_tv;
}
void ast_channel_sending_dtmf_tv_set(struct ast_channel *chan, struct timeval value)
{
	chan->sending_dtmf_tv = value;
}

enum ama_flags ast_channel_amaflags(const struct ast_channel *chan)
{
	return chan->amaflags;
}

void ast_channel_amaflags_set(struct ast_channel *chan, enum ama_flags value)
{
	if (chan->amaflags == value) {
		return;
	}
	chan->amaflags = value;
	ast_channel_publish_snapshot(chan);
}

#ifdef HAVE_EPOLL
int ast_channel_epfd(const struct ast_channel *chan)
{
	return chan->epfd;
}
void ast_channel_epfd_set(struct ast_channel *chan, int value)
{
	chan->epfd = value;
}
#endif
int ast_channel_fdno(const struct ast_channel *chan)
{
	return chan->fdno;
}
void ast_channel_fdno_set(struct ast_channel *chan, int value)
{
	chan->fdno = value;
}
int ast_channel_hangupcause(const struct ast_channel *chan)
{
	return chan->hangupcause;
}
void ast_channel_hangupcause_set(struct ast_channel *chan, int value)
{
	chan->hangupcause = value;
}
int ast_channel_macropriority(const struct ast_channel *chan)
{
	return chan->macropriority;
}
void ast_channel_macropriority_set(struct ast_channel *chan, int value)
{
	chan->macropriority = value;
}
int ast_channel_priority(const struct ast_channel *chan)
{
	return chan->priority;
}
void ast_channel_priority_set(struct ast_channel *chan, int value)
{
	chan->priority = value;
}
int ast_channel_rings(const struct ast_channel *chan)
{
	return chan->rings;
}
void ast_channel_rings_set(struct ast_channel *chan, int value)
{
	chan->rings = value;
}
int ast_channel_streamid(const struct ast_channel *chan)
{
	return chan->streamid;
}
void ast_channel_streamid_set(struct ast_channel *chan, int value)
{
	chan->streamid = value;
}
int ast_channel_timingfd(const struct ast_channel *chan)
{
	return chan->timingfd;
}
void ast_channel_timingfd_set(struct ast_channel *chan, int value)
{
	chan->timingfd = value;
}
int ast_channel_visible_indication(const struct ast_channel *chan)
{
	return chan->visible_indication;
}
void ast_channel_visible_indication_set(struct ast_channel *chan, int value)
{
	chan->visible_indication = value;
}
int ast_channel_hold_state(const struct ast_channel *chan)
{
	return chan->hold_state;
}
void ast_channel_hold_state_set(struct ast_channel *chan, int value)
{
	chan->hold_state = value;
}
int ast_channel_vstreamid(const struct ast_channel *chan)
{
	return chan->vstreamid;
}
void ast_channel_vstreamid_set(struct ast_channel *chan, int value)
{
	chan->vstreamid = value;
}
unsigned short ast_channel_transfercapability(const struct ast_channel *chan)
{
	return chan->transfercapability;
}
void ast_channel_transfercapability_set(struct ast_channel *chan, unsigned short value)
{
	chan->transfercapability = value;
}
unsigned int ast_channel_emulate_dtmf_duration(const struct ast_channel *chan)
{
	return chan->emulate_dtmf_duration;
}
void ast_channel_emulate_dtmf_duration_set(struct ast_channel *chan, unsigned int value)
{
	chan->emulate_dtmf_duration = value;
}
unsigned int ast_channel_fin(const struct ast_channel *chan)
{
	return chan->fin;
}
void ast_channel_fin_set(struct ast_channel *chan, unsigned int value)
{
	chan->fin = value;
}
unsigned int ast_channel_fout(const struct ast_channel *chan)
{
	return chan->fout;
}
void ast_channel_fout_set(struct ast_channel *chan, unsigned int value)
{
	chan->fout = value;
}
unsigned long ast_channel_insmpl(const struct ast_channel *chan)
{
	return chan->insmpl;
}
void ast_channel_insmpl_set(struct ast_channel *chan, unsigned long value)
{
	chan->insmpl = value;
}
unsigned long ast_channel_outsmpl(const struct ast_channel *chan)
{
	return chan->outsmpl;
}
void ast_channel_outsmpl_set(struct ast_channel *chan, unsigned long value)
{
	chan->outsmpl = value;
}
void *ast_channel_generatordata(const struct ast_channel *chan)
{
	return chan->generatordata;
}
void ast_channel_generatordata_set(struct ast_channel *chan, void *value)
{
	chan->generatordata = value;
}
void *ast_channel_music_state(const struct ast_channel *chan)
{
	return chan->music_state;
}
void ast_channel_music_state_set(struct ast_channel *chan, void *value)
{
	chan->music_state = value;
}
void *ast_channel_tech_pvt(const struct ast_channel *chan)
{
	return chan->tech_pvt;
}
void ast_channel_tech_pvt_set(struct ast_channel *chan, void *value)
{
	chan->tech_pvt = value;
}
void *ast_channel_timingdata(const struct ast_channel *chan)
{
	return chan->timingdata;
}
void ast_channel_timingdata_set(struct ast_channel *chan, void *value)
{
	chan->timingdata = value;
}
struct ast_audiohook_list *ast_channel_audiohooks(const struct ast_channel *chan)
{
	return chan->audiohooks;
}
void ast_channel_audiohooks_set(struct ast_channel *chan, struct ast_audiohook_list *value)
{
	chan->audiohooks = value;
}
struct ast_cdr *ast_channel_cdr(const struct ast_channel *chan)
{
	return chan->cdr;
}
void ast_channel_cdr_set(struct ast_channel *chan, struct ast_cdr *value)
{
	chan->cdr = value;
}
struct ast_channel *ast_channel_masq(const struct ast_channel *chan)
{
	return chan->masq;
}
void ast_channel_masq_set(struct ast_channel *chan, struct ast_channel *value)
{
	chan->masq = value;
}
struct ast_channel *ast_channel_masqr(const struct ast_channel *chan)
{
	return chan->masqr;
}
void ast_channel_masqr_set(struct ast_channel *chan, struct ast_channel *value)
{
	chan->masqr = value;
}
struct ast_channel_monitor *ast_channel_monitor(const struct ast_channel *chan)
{
	return chan->monitor;
}
void ast_channel_monitor_set(struct ast_channel *chan, struct ast_channel_monitor *value)
{
	chan->monitor = value;
}
struct ast_filestream *ast_channel_stream(const struct ast_channel *chan)
{
	return chan->stream;
}
void ast_channel_stream_set(struct ast_channel *chan, struct ast_filestream *value)
{
	chan->stream = value;
}
struct ast_filestream *ast_channel_vstream(const struct ast_channel *chan)
{
	return chan->vstream;
}
void ast_channel_vstream_set(struct ast_channel *chan, struct ast_filestream *value)
{
	chan->vstream = value;
}
struct ast_format_cap *ast_channel_nativeformats(const struct ast_channel *chan)
{
	return chan->nativeformats;
}
void ast_channel_nativeformats_set(struct ast_channel *chan, struct ast_format_cap *value)
{
	ao2_replace(chan->nativeformats, value);
}
struct ast_framehook_list *ast_channel_framehooks(const struct ast_channel *chan)
{
	return chan->framehooks;
}
void ast_channel_framehooks_set(struct ast_channel *chan, struct ast_framehook_list *value)
{
	chan->framehooks = value;
}
struct ast_generator *ast_channel_generator(const struct ast_channel *chan)
{
	return chan->generator;
}
void ast_channel_generator_set(struct ast_channel *chan, struct ast_generator *value)
{
	chan->generator = value;
}
struct ast_pbx *ast_channel_pbx(const struct ast_channel *chan)
{
	return chan->pbx;
}
void ast_channel_pbx_set(struct ast_channel *chan, struct ast_pbx *value)
{
	chan->pbx = value;
}
struct ast_sched_context *ast_channel_sched(const struct ast_channel *chan)
{
	return chan->sched;
}
void ast_channel_sched_set(struct ast_channel *chan, struct ast_sched_context *value)
{
	chan->sched = value;
}
struct ast_timer *ast_channel_timer(const struct ast_channel *chan)
{
	return chan->timer;
}
void ast_channel_timer_set(struct ast_channel *chan, struct ast_timer *value)
{
	chan->timer = value;
}
struct ast_tone_zone *ast_channel_zone(const struct ast_channel *chan)
{
	return chan->zone;
}
void ast_channel_zone_set(struct ast_channel *chan, struct ast_tone_zone *value)
{
	chan->zone = value;
}
struct ast_trans_pvt *ast_channel_readtrans(const struct ast_channel *chan)
{
	return chan->readtrans;
}
void ast_channel_readtrans_set(struct ast_channel *chan, struct ast_trans_pvt *value)
{
	chan->readtrans = value;
}
struct ast_trans_pvt *ast_channel_writetrans(const struct ast_channel *chan)
{
	return chan->writetrans;
}
void ast_channel_writetrans_set(struct ast_channel *chan, struct ast_trans_pvt *value)
{
	chan->writetrans = value;
}
const struct ast_channel_tech *ast_channel_tech(const struct ast_channel *chan)
{
	return chan->tech;
}
void ast_channel_tech_set(struct ast_channel *chan, const struct ast_channel_tech *value)
{
	chan->tech = value;
}
enum ast_channel_adsicpe ast_channel_adsicpe(const struct ast_channel *chan)
{
	return chan->adsicpe;
}
void ast_channel_adsicpe_set(struct ast_channel *chan, enum ast_channel_adsicpe value)
{
	chan->adsicpe = value;
}
enum ast_channel_state ast_channel_state(const struct ast_channel *chan)
{
	return chan->state;
}
struct ast_callid *ast_channel_callid(const struct ast_channel *chan)
{
	if (chan->callid) {
		ast_callid_ref(chan->callid);
		return chan->callid;
	}
	return NULL;
}
void ast_channel_callid_set(struct ast_channel *chan, struct ast_callid *callid)
{
	char call_identifier_from[AST_CALLID_BUFFER_LENGTH];
	char call_identifier_to[AST_CALLID_BUFFER_LENGTH];
	call_identifier_from[0] = '\0';
	ast_callid_strnprint(call_identifier_to, sizeof(call_identifier_to), callid);
	if (chan->callid) {
		ast_callid_strnprint(call_identifier_from, sizeof(call_identifier_from), chan->callid);
		ast_debug(3, "Channel Call ID changing from %s to %s\n", call_identifier_from, call_identifier_to);
		/* unbind if already set */
		ast_callid_unref(chan->callid);
	}

	chan->callid = ast_callid_ref(callid);

	ast_test_suite_event_notify("CallIDChange",
		"State: CallIDChange\r\n"
		"Channel: %s\r\n"
		"CallID: %s\r\n"
		"PriorCallID: %s",
		ast_channel_name(chan),
		call_identifier_to,
		call_identifier_from);
}

void ast_channel_state_set(struct ast_channel *chan, enum ast_channel_state value)
{
	chan->state = value;
}
void ast_channel_set_oldwriteformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->oldwriteformat, format);
}
void ast_channel_set_rawreadformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->rawreadformat, format);
}
void ast_channel_set_rawwriteformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->rawwriteformat, format);
}
void ast_channel_set_readformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->readformat, format);
}
void ast_channel_set_writeformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->writeformat, format);
}
struct ast_format *ast_channel_oldwriteformat(struct ast_channel *chan)
{
	return chan->oldwriteformat;
}
struct ast_format *ast_channel_rawreadformat(struct ast_channel *chan)
{
	return chan->rawreadformat;
}
struct ast_format *ast_channel_rawwriteformat(struct ast_channel *chan)
{
	return chan->rawwriteformat;
}
struct ast_format *ast_channel_readformat(struct ast_channel *chan)
{
	return chan->readformat;
}
struct ast_format *ast_channel_writeformat(struct ast_channel *chan)
{
	return chan->writeformat;
}
struct ast_hangup_handler_list *ast_channel_hangup_handlers(struct ast_channel *chan)
{
	return &chan->hangup_handlers;
}
struct ast_datastore_list *ast_channel_datastores(struct ast_channel *chan)
{
	return &chan->datastores;
}
struct ast_autochan_list *ast_channel_autochans(struct ast_channel *chan)
{
	return &chan->autochans;
}
struct ast_readq_list *ast_channel_readq(struct ast_channel *chan)
{
	return &chan->readq;
}
struct ast_frame *ast_channel_dtmff(struct ast_channel *chan)
{
	return &chan->dtmff;
}
struct ast_jb *ast_channel_jb(struct ast_channel *chan)
{
	return &chan->jb;
}
struct ast_party_caller *ast_channel_caller(struct ast_channel *chan)
{
	return &chan->caller;
}
struct ast_party_connected_line *ast_channel_connected(struct ast_channel *chan)
{
	return &chan->connected;
}
struct ast_party_connected_line *ast_channel_connected_indicated(struct ast_channel *chan)
{
	return &chan->connected_indicated;
}
struct ast_party_id ast_channel_connected_effective_id(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->connected.id, &chan->connected.priv);
}
struct ast_party_dialed *ast_channel_dialed(struct ast_channel *chan)
{
	return &chan->dialed;
}
struct ast_party_redirecting *ast_channel_redirecting(struct ast_channel *chan)
{
	return &chan->redirecting;
}
struct ast_party_id ast_channel_redirecting_effective_orig(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->redirecting.orig, &chan->redirecting.priv_orig);
}
struct ast_party_id ast_channel_redirecting_effective_from(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->redirecting.from, &chan->redirecting.priv_from);
}
struct ast_party_id ast_channel_redirecting_effective_to(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->redirecting.to, &chan->redirecting.priv_to);
}
struct timeval *ast_channel_dtmf_tv(struct ast_channel *chan)
{
	return &chan->dtmf_tv;
}
struct timeval *ast_channel_whentohangup(struct ast_channel *chan)
{
	return &chan->whentohangup;
}
struct varshead *ast_channel_varshead(struct ast_channel *chan)
{
	return &chan->varshead;
}
void ast_channel_dtmff_set(struct ast_channel *chan, struct ast_frame *value)
{
	chan->dtmff = *value;
}
void ast_channel_jb_set(struct ast_channel *chan, struct ast_jb *value)
{
	chan->jb = *value;
}
void ast_channel_caller_set(struct ast_channel *chan, struct ast_party_caller *value)
{
	chan->caller = *value;
}
void ast_channel_connected_set(struct ast_channel *chan, struct ast_party_connected_line *value)
{
	chan->connected = *value;
}
void ast_channel_dialed_set(struct ast_channel *chan, struct ast_party_dialed *value)
{
	chan->dialed = *value;
}
void ast_channel_redirecting_set(struct ast_channel *chan, struct ast_party_redirecting *value)
{
	chan->redirecting = *value;
}
void ast_channel_dtmf_tv_set(struct ast_channel *chan, struct timeval *value)
{
	chan->dtmf_tv = *value;
}
void ast_channel_whentohangup_set(struct ast_channel *chan, struct timeval *value)
{
	chan->whentohangup = *value;
}
void ast_channel_varshead_set(struct ast_channel *chan, struct varshead *value)
{
	chan->varshead = *value;
}
struct timeval ast_channel_creationtime(struct ast_channel *chan)
{
	return chan->creationtime;
}
void ast_channel_creationtime_set(struct ast_channel *chan, struct timeval *value)
{
	chan->creationtime = *value;
}

struct timeval ast_channel_answertime(struct ast_channel *chan)
{
	return chan->answertime;
}

void ast_channel_answertime_set(struct ast_channel *chan, struct timeval *value)
{
	chan->answertime = *value;
}

/* Evil softhangup accessors */
int ast_channel_softhangup_internal_flag(struct ast_channel *chan)
{
	return chan->softhangup;
}
void ast_channel_softhangup_internal_flag_set(struct ast_channel *chan, int value)
{
	chan->softhangup = value;
}
void ast_channel_softhangup_internal_flag_add(struct ast_channel *chan, int value)
{
	chan->softhangup |= value;
}
void ast_channel_softhangup_internal_flag_clear(struct ast_channel *chan, int value)
{
	chan ->softhangup &= ~value;
}

int ast_channel_unbridged_nolock(struct ast_channel *chan)
{
	return chan->unbridged;
}

int ast_channel_unbridged(struct ast_channel *chan)
{
	int res;
	ast_channel_lock(chan);
	res = ast_channel_unbridged_nolock(chan);
	ast_channel_unlock(chan);
	return res;
}

void ast_channel_set_unbridged_nolock(struct ast_channel *chan, int value)
{
	chan->unbridged = value;
	ast_queue_frame(chan, &ast_null_frame);
}

void ast_channel_set_unbridged(struct ast_channel *chan, int value)
{
	ast_channel_lock(chan);
	ast_channel_set_unbridged_nolock(chan, value);
	ast_channel_unlock(chan);
}

void ast_channel_callid_cleanup(struct ast_channel *chan)
{
	if (chan->callid) {
		chan->callid = ast_callid_unref(chan->callid);
	}
}

/* Typedef accessors */
ast_group_t ast_channel_callgroup(const struct ast_channel *chan)
{
	return chan->callgroup;
}
void ast_channel_callgroup_set(struct ast_channel *chan, ast_group_t value)
{
	chan->callgroup = value;
}
ast_group_t ast_channel_pickupgroup(const struct ast_channel *chan)
{
	return chan->pickupgroup;
}
void ast_channel_pickupgroup_set(struct ast_channel *chan, ast_group_t value)
{
	chan->pickupgroup = value;
}
struct ast_namedgroups *ast_channel_named_callgroups(const struct ast_channel *chan)
{
	return chan->named_callgroups;
}
void ast_channel_named_callgroups_set(struct ast_channel *chan, struct ast_namedgroups *value)
{
	ast_unref_namedgroups(chan->named_callgroups);
	chan->named_callgroups = ast_ref_namedgroups(value);
}
struct ast_namedgroups *ast_channel_named_pickupgroups(const struct ast_channel *chan)
{
	return chan->named_pickupgroups;
}
void ast_channel_named_pickupgroups_set(struct ast_channel *chan, struct ast_namedgroups *value)
{
	ast_unref_namedgroups(chan->named_pickupgroups);
	chan->named_pickupgroups = ast_ref_namedgroups(value);
}

/* Alertpipe functions */
int ast_channel_alert_write(struct ast_channel *chan)
{
	char blah = 0x7F;
	return ast_channel_alert_writable(chan) && write(chan->alertpipe[1], &blah, sizeof(blah)) != sizeof(blah);
}

ast_alert_status_t ast_channel_internal_alert_read(struct ast_channel *chan)
{
	int flags;
	char blah;

	if (!ast_channel_internal_alert_readable(chan)) {
		return AST_ALERT_NOT_READABLE;
	}

	flags = fcntl(chan->alertpipe[0], F_GETFL);
	/* For some odd reason, the alertpipe occasionally loses nonblocking status,
	 * which immediately causes a deadlock scenario.  Detect and prevent this. */
	if ((flags & O_NONBLOCK) == 0) {
		ast_log(LOG_ERROR, "Alertpipe on channel %s lost O_NONBLOCK?!!\n", ast_channel_name(chan));
		if (fcntl(chan->alertpipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "Unable to set alertpipe nonblocking! (%d: %s)\n", errno, strerror(errno));
			return AST_ALERT_READ_FATAL;
		}
	}
	if (read(chan->alertpipe[0], &blah, sizeof(blah)) < 0) {
		if (errno != EINTR && errno != EAGAIN) {
			ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
			return AST_ALERT_READ_FAIL;
		}
	}

	return AST_ALERT_READ_SUCCESS;
}

int ast_channel_alert_writable(struct ast_channel *chan)
{
	return chan->alertpipe[1] > -1;
}

int ast_channel_internal_alert_readable(struct ast_channel *chan)
{
	return chan->alertpipe[0] > -1;
}

void ast_channel_internal_alertpipe_clear(struct ast_channel *chan)
{
	chan->alertpipe[0] = chan->alertpipe[1] = -1;
}

void ast_channel_internal_alertpipe_close(struct ast_channel *chan)
{
	if (ast_channel_internal_alert_readable(chan)) {
		close(chan->alertpipe[0]);
	}
	if (ast_channel_alert_writable(chan)) {
		close(chan->alertpipe[1]);
	}
}

int ast_channel_internal_alertpipe_init(struct ast_channel *chan)
{
	if (pipe(chan->alertpipe)) {
		ast_log(LOG_WARNING, "Channel allocation failed: Can't create alert pipe! Try increasing max file descriptors with ulimit -n\n");
		return -1;
	} else {
		int flags = fcntl(chan->alertpipe[0], F_GETFL);
		if (fcntl(chan->alertpipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "Channel allocation failed: Unable to set alertpipe nonblocking! (%d: %s)\n", errno, strerror(errno));
			return -1;
		}
		flags = fcntl(chan->alertpipe[1], F_GETFL);
		if (fcntl(chan->alertpipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "Channel allocation failed: Unable to set alertpipe nonblocking! (%d: %s)\n", errno, strerror(errno));
			return -1;
		}
	}
	return 0;
}

int ast_channel_internal_alert_readfd(struct ast_channel *chan)
{
	return chan->alertpipe[0];
}

void ast_channel_internal_alertpipe_swap(struct ast_channel *chan1, struct ast_channel *chan2)
{
	int i;
	for (i = 0; i < ARRAY_LEN(chan1->alertpipe); i++) {
		SWAP(chan1->alertpipe[i], chan2->alertpipe[i]);
	}
}

/* file descriptor array accessors */
void ast_channel_internal_fd_set(struct ast_channel *chan, int which, int value)
{
	chan->fds[which] = value;
}
void ast_channel_internal_fd_clear(struct ast_channel *chan, int which)
{
	ast_channel_internal_fd_set(chan, which, -1);
}
void ast_channel_internal_fd_clear_all(struct ast_channel *chan)
{
	int i;
	for (i = 0; i < AST_MAX_FDS; i++) {
		ast_channel_internal_fd_clear(chan, i);
	}
}
int ast_channel_fd(const struct ast_channel *chan, int which)
{
	return chan->fds[which];
}
int ast_channel_fd_isset(const struct ast_channel *chan, int which)
{
	return ast_channel_fd(chan, which) > -1;
}

#ifdef HAVE_EPOLL
struct ast_epoll_data *ast_channel_internal_epfd_data(const struct ast_channel *chan, int which)
{
	return chan->epfd_data[which];
}
void ast_channel_internal_epfd_data_set(struct ast_channel *chan, int which , struct ast_epoll_data *value)
{
	chan->epfd_data[which] = value;
}
#endif

pthread_t ast_channel_blocker(const struct ast_channel *chan)
{
	return chan->blocker;
}
void ast_channel_blocker_set(struct ast_channel *chan, pthread_t value)
{
	chan->blocker = value;
}

ast_timing_func_t ast_channel_timingfunc(const struct ast_channel *chan)
{
	return chan->timingfunc;
}
void ast_channel_timingfunc_set(struct ast_channel *chan, ast_timing_func_t value)
{
	chan->timingfunc = value;
}

struct ast_bridge *ast_channel_internal_bridge(const struct ast_channel *chan)
{
	return chan->bridge;
}
void ast_channel_internal_bridge_set(struct ast_channel *chan, struct ast_bridge *value)
{
	chan->bridge = value;
	ast_channel_publish_snapshot(chan);
}

struct ast_bridge_channel *ast_channel_internal_bridge_channel(const struct ast_channel *chan)
{
	return chan->bridge_channel;
}
void ast_channel_internal_bridge_channel_set(struct ast_channel *chan, struct ast_bridge_channel *value)
{
	chan->bridge_channel = value;
}

struct ast_flags *ast_channel_flags(struct ast_channel *chan)
{
	return &chan->flags;
}

static int collect_names_cb(void *obj, void *arg, int flags) {
	struct ast_control_pvt_cause_code *cause_code = obj;
	struct ast_str **str = arg;

	ast_str_append(str, 0, "%s%s", (ast_str_strlen(*str) ? "," : ""), cause_code->chan_name);

	return 0;
}

struct ast_str *ast_channel_dialed_causes_channels(const struct ast_channel *chan)
{
	struct ast_str *chanlist = ast_str_create(128);

	if (!chanlist) {
		return NULL;
	}

	ao2_callback(chan->dialed_causes, 0, collect_names_cb, &chanlist);

	return chanlist;
}

struct ast_control_pvt_cause_code *ast_channel_dialed_causes_find(const struct ast_channel *chan, const char *chan_name)
{
	return ao2_find(chan->dialed_causes, chan_name, OBJ_KEY);
}

int ast_channel_dialed_causes_add(const struct ast_channel *chan, const struct ast_control_pvt_cause_code *cause_code, int datalen)
{
	struct ast_control_pvt_cause_code *ao2_cause_code;
	ao2_find(chan->dialed_causes, cause_code->chan_name, OBJ_KEY | OBJ_UNLINK | OBJ_NODATA);
	ao2_cause_code = ao2_alloc(datalen, NULL);

	if (ao2_cause_code) {
		memcpy(ao2_cause_code, cause_code, datalen);
		ao2_link(chan->dialed_causes, ao2_cause_code);
		ao2_ref(ao2_cause_code, -1);
		return 0;
	} else {
		return -1;
	}
}

void ast_channel_dialed_causes_clear(const struct ast_channel *chan)
{
	ao2_callback(chan->dialed_causes, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
}

/* \brief Hash function for pvt cause code frames */
static int pvt_cause_hash_fn(const void *vpc, const int flags)
{
	const struct ast_control_pvt_cause_code *pc = vpc;
	return ast_str_hash(ast_tech_to_upper(ast_strdupa(pc->chan_name)));
}

/* \brief Comparison function for pvt cause code frames */
static int pvt_cause_cmp_fn(void *obj, void *vstr, int flags)
{
	struct ast_control_pvt_cause_code *pc = obj;
	char *str = ast_tech_to_upper(ast_strdupa(vstr));
	char *pc_str = ast_tech_to_upper(ast_strdupa(pc->chan_name));
	return !strcmp(pc_str, str) ? CMP_MATCH | CMP_STOP : 0;
}

#define DIALED_CAUSES_BUCKETS 37

struct ast_channel *__ast_channel_internal_alloc(void (*destructor)(void *obj), const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *file, int line, const char *function)
{
	struct ast_channel *tmp;
#if defined(REF_DEBUG)
	tmp = __ao2_alloc_debug(sizeof(*tmp), destructor,
		AO2_ALLOC_OPT_LOCK_MUTEX, "", file, line, function, 1);
#elif defined(__AST_DEBUG_MALLOC)
	tmp = __ao2_alloc_debug(sizeof(*tmp), destructor,
		AO2_ALLOC_OPT_LOCK_MUTEX, "", file, line, function, 0);
#else
	tmp = ao2_alloc(sizeof(*tmp), destructor);
#endif

	if ((ast_string_field_init(tmp, 128))) {
		return ast_channel_unref(tmp);
	}

	if (!(tmp->dialed_causes = ao2_container_alloc(DIALED_CAUSES_BUCKETS, pvt_cause_hash_fn, pvt_cause_cmp_fn))) {
		return ast_channel_unref(tmp);
	}

	/* set the creation time in the uniqueid */
	tmp->uniqueid.creation_time = time(NULL);
	tmp->uniqueid.creation_unique = ast_atomic_fetchadd_int(&uniqueint, 1);

	/* use provided id or default to historical {system-}time.# format */
	if (assignedids && !ast_strlen_zero(assignedids->uniqueid)) {
		ast_copy_string(tmp->uniqueid.unique_id, assignedids->uniqueid, sizeof(tmp->uniqueid.unique_id));
	} else if (ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
		snprintf(tmp->uniqueid.unique_id, sizeof(tmp->uniqueid.unique_id), "%li.%d",
			(long)(tmp->uniqueid.creation_time),
			tmp->uniqueid.creation_unique);
	} else {
		snprintf(tmp->uniqueid.unique_id, sizeof(tmp->uniqueid.unique_id), "%s-%li.%d",
			ast_config_AST_SYSTEM_NAME,
			(long)(tmp->uniqueid.creation_time),
			tmp->uniqueid.creation_unique);
	}

	/* copy linked id from parent channel if known */
	if (requestor) {
		tmp->linkedid = requestor->linkedid;
	} else {
		tmp->linkedid = tmp->uniqueid;
	}

	return tmp;
}

struct ast_channel *ast_channel_internal_oldest_linkedid(struct ast_channel *a, struct ast_channel *b)
{
	ast_assert(a->linkedid.creation_time != 0);
	ast_assert(b->linkedid.creation_time != 0);

	if (a->linkedid.creation_time < b->linkedid.creation_time) {
		return a;
	}
	if (b->linkedid.creation_time < a->linkedid.creation_time) {
		return b;
	}
	if (a->linkedid.creation_unique < b->linkedid.creation_unique) {
		return a;
	}
	return b;
}

void ast_channel_internal_copy_linkedid(struct ast_channel *dest, struct ast_channel *source)
{
	if (dest->linkedid.creation_time == source->linkedid.creation_time
		&& dest->linkedid.creation_unique == source->linkedid.creation_unique
		&& !strcmp(dest->linkedid.unique_id, source->linkedid.unique_id)) {
		return;
	}
	dest->linkedid = source->linkedid;
	ast_channel_publish_snapshot(dest);
}

void ast_channel_internal_swap_uniqueid_and_linkedid(struct ast_channel *a, struct ast_channel *b)
{
	struct ast_channel_id temp;

	temp = a->uniqueid;
	a->uniqueid = b->uniqueid;
	b->uniqueid = temp;

	temp = a->linkedid;
	a->linkedid = b->linkedid;
	b->linkedid = temp;
}

void ast_channel_internal_swap_topics(struct ast_channel *a, struct ast_channel *b)
{
	struct stasis_cp_single *temp;

	temp = a->topics;
	a->topics = b->topics;
	b->topics = temp;
}

void ast_channel_internal_set_fake_ids(struct ast_channel *chan, const char *uniqueid, const char *linkedid)
{
	ast_copy_string(chan->uniqueid.unique_id, uniqueid, sizeof(chan->uniqueid.unique_id));
	ast_copy_string(chan->linkedid.unique_id, linkedid, sizeof(chan->linkedid.unique_id));
}

void ast_channel_internal_cleanup(struct ast_channel *chan)
{
	if (chan->dialed_causes) {
		ao2_t_ref(chan->dialed_causes, -1,
			"done with dialed causes since the channel is going away");
		chan->dialed_causes = NULL;
	}

	ast_string_field_free_memory(chan);

	chan->endpoint_forward = stasis_forward_cancel(chan->endpoint_forward);
	chan->endpoint_cache_forward = stasis_forward_cancel(chan->endpoint_cache_forward);

	stasis_cp_single_unsubscribe(chan->topics);
	chan->topics = NULL;
}

void ast_channel_internal_finalize(struct ast_channel *chan)
{
	chan->finalized = 1;
}

int ast_channel_internal_is_finalized(struct ast_channel *chan)
{
	return chan->finalized;
}

struct stasis_topic *ast_channel_topic(struct ast_channel *chan)
{
	if (!chan) {
		return ast_channel_topic_all();
	}

	return stasis_cp_single_topic(chan->topics);
}

struct stasis_topic *ast_channel_topic_cached(struct ast_channel *chan)
{
	if (!chan) {
		return ast_channel_topic_all_cached();
	}

	return stasis_cp_single_topic_cached(chan->topics);
}

int ast_channel_forward_endpoint(struct ast_channel *chan,
	struct ast_endpoint *endpoint)
{
	ast_assert(chan != NULL);
	ast_assert(endpoint != NULL);

	chan->endpoint_forward =
		stasis_forward_all(ast_channel_topic(chan),
			ast_endpoint_topic(endpoint));
	if (!chan->endpoint_forward) {
		return -1;
	}

	chan->endpoint_cache_forward = stasis_forward_all(ast_channel_topic_cached(chan),
		ast_endpoint_topic(endpoint));
	if (!chan->endpoint_cache_forward) {
		chan->endpoint_forward = stasis_forward_cancel(chan->endpoint_forward);
		return -1;
	}

	return 0;
}

int ast_channel_internal_setup_topics(struct ast_channel *chan)
{
	const char *topic_name = chan->uniqueid.unique_id;
	ast_assert(chan->topics == NULL);

	if (ast_strlen_zero(topic_name)) {
		topic_name = "<dummy-channel>";
	}

	chan->topics = stasis_cp_single_create(
		ast_channel_cache_all(), topic_name);
	if (!chan->topics) {
		return -1;
	}

	return 0;
}
