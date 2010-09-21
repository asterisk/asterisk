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
 *
 * \brief Channel Management
 *
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include <sys/time.h>
#include <signal.h>
#include <math.h>

#include "asterisk/paths.h"	/* use ast_config_AST_SYSTEM_NAME */

#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/mod_format.h"
#include "asterisk/sched.h"
#include "asterisk/channel.h"
#include "asterisk/musiconhold.h"
#include "asterisk/say.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/translate.h"
#include "asterisk/manager.h"
#include "asterisk/cel.h"
#include "asterisk/chanvars.h"
#include "asterisk/linkedlists.h"
#include "asterisk/indications.h"
#include "asterisk/monitor.h"
#include "asterisk/causes.h"
#include "asterisk/callerid.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/transcap.h"
#include "asterisk/devicestate.h"
#include "asterisk/sha1.h"
#include "asterisk/threadstorage.h"
#include "asterisk/slinfactory.h"
#include "asterisk/audiohook.h"
#include "asterisk/framehook.h"
#include "asterisk/timing.h"
#include "asterisk/autochan.h"
#include "asterisk/stringfields.h"
#include "asterisk/global_datastores.h"
#include "asterisk/data.h"

#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#endif

#if defined(KEEP_TILL_CHANNEL_PARTY_NUMBER_INFO_NEEDED)
#if defined(HAVE_PRI)
#include "libpri.h"
#endif	/* defined(HAVE_PRI) */
#endif	/* defined(KEEP_TILL_CHANNEL_PARTY_NUMBER_INFO_NEEDED) */

struct ast_epoll_data {
	struct ast_channel *chan;
	int which;
};

/* uncomment if you have problems with 'monitoring' synchronized files */
#if 0
#define MONITOR_CONSTANT_DELAY
#define MONITOR_DELAY	150 * 8		/*!< 150 ms of MONITORING DELAY */
#endif

/*! \brief Prevent new channel allocation if shutting down. */
static int shutting_down;

static int uniqueint;

unsigned long global_fin, global_fout;

AST_THREADSTORAGE(state2str_threadbuf);
#define STATE2STR_BUFSIZE   32

/*! Default amount of time to use when emulating a digit as a begin and end 
 *  100ms */
#define AST_DEFAULT_EMULATE_DTMF_DURATION 100

/*! Minimum allowed digit length - 80ms */
#define AST_MIN_DTMF_DURATION 80

/*! Minimum amount of time between the end of the last digit and the beginning 
 *  of a new one - 45ms */
#define AST_MIN_DTMF_GAP 45

/*! \brief List of channel drivers */
struct chanlist {
	const struct ast_channel_tech *tech;
	AST_LIST_ENTRY(chanlist) list;
};

#ifdef CHANNEL_TRACE
/*! \brief Structure to hold channel context backtrace data */
struct ast_chan_trace_data {
	int enabled;
	AST_LIST_HEAD_NOLOCK(, ast_chan_trace) trace;
};

/*! \brief Structure to save contexts where an ast_chan has been into */
struct ast_chan_trace {
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	int priority;
	AST_LIST_ENTRY(ast_chan_trace) entry;
};
#endif

/*! \brief the list of registered channel types */
static AST_RWLIST_HEAD_STATIC(backends, chanlist);

#ifdef LOW_MEMORY
#define NUM_CHANNEL_BUCKETS 61
#else
#define NUM_CHANNEL_BUCKETS 1567
#endif

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
	MEMBER(ast_channel, name, AST_DATA_STRING)				\
	MEMBER(ast_channel, language, AST_DATA_STRING)				\
	MEMBER(ast_channel, musicclass, AST_DATA_STRING)			\
	MEMBER(ast_channel, accountcode, AST_DATA_STRING)			\
	MEMBER(ast_channel, peeraccount, AST_DATA_STRING)			\
	MEMBER(ast_channel, userfield, AST_DATA_STRING)				\
	MEMBER(ast_channel, call_forward, AST_DATA_STRING)			\
	MEMBER(ast_channel, uniqueid, AST_DATA_STRING)				\
	MEMBER(ast_channel, linkedid, AST_DATA_STRING)				\
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


/*! \brief All active channels on the system */
static struct ao2_container *channels;

/*! \brief map AST_CAUSE's to readable string representations 
 *
 * \ref causes.h
*/
static const struct {
	int cause;
	const char *name;
	const char *desc;
} causes[] = {
	{ AST_CAUSE_UNALLOCATED, "UNALLOCATED", "Unallocated (unassigned) number" },
	{ AST_CAUSE_NO_ROUTE_TRANSIT_NET, "NO_ROUTE_TRANSIT_NET", "No route to specified transmit network" },
	{ AST_CAUSE_NO_ROUTE_DESTINATION, "NO_ROUTE_DESTINATION", "No route to destination" },
	{ AST_CAUSE_CHANNEL_UNACCEPTABLE, "CHANNEL_UNACCEPTABLE", "Channel unacceptable" },
	{ AST_CAUSE_CALL_AWARDED_DELIVERED, "CALL_AWARDED_DELIVERED", "Call awarded and being delivered in an established channel" },
	{ AST_CAUSE_NORMAL_CLEARING, "NORMAL_CLEARING", "Normal Clearing" },
	{ AST_CAUSE_USER_BUSY, "USER_BUSY", "User busy" },
	{ AST_CAUSE_NO_USER_RESPONSE, "NO_USER_RESPONSE", "No user responding" },
	{ AST_CAUSE_NO_ANSWER, "NO_ANSWER", "User alerting, no answer" },
	{ AST_CAUSE_CALL_REJECTED, "CALL_REJECTED", "Call Rejected" },
	{ AST_CAUSE_NUMBER_CHANGED, "NUMBER_CHANGED", "Number changed" },
	{ AST_CAUSE_DESTINATION_OUT_OF_ORDER, "DESTINATION_OUT_OF_ORDER", "Destination out of order" },
	{ AST_CAUSE_INVALID_NUMBER_FORMAT, "INVALID_NUMBER_FORMAT", "Invalid number format" },
	{ AST_CAUSE_FACILITY_REJECTED, "FACILITY_REJECTED", "Facility rejected" },
	{ AST_CAUSE_RESPONSE_TO_STATUS_ENQUIRY, "RESPONSE_TO_STATUS_ENQUIRY", "Response to STATus ENQuiry" },
	{ AST_CAUSE_NORMAL_UNSPECIFIED, "NORMAL_UNSPECIFIED", "Normal, unspecified" },
	{ AST_CAUSE_NORMAL_CIRCUIT_CONGESTION, "NORMAL_CIRCUIT_CONGESTION", "Circuit/channel congestion" },
	{ AST_CAUSE_NETWORK_OUT_OF_ORDER, "NETWORK_OUT_OF_ORDER", "Network out of order" },
	{ AST_CAUSE_NORMAL_TEMPORARY_FAILURE, "NORMAL_TEMPORARY_FAILURE", "Temporary failure" },
	{ AST_CAUSE_SWITCH_CONGESTION, "SWITCH_CONGESTION", "Switching equipment congestion" },
	{ AST_CAUSE_ACCESS_INFO_DISCARDED, "ACCESS_INFO_DISCARDED", "Access information discarded" },
	{ AST_CAUSE_REQUESTED_CHAN_UNAVAIL, "REQUESTED_CHAN_UNAVAIL", "Requested channel not available" },
	{ AST_CAUSE_PRE_EMPTED, "PRE_EMPTED", "Pre-empted" },
	{ AST_CAUSE_FACILITY_NOT_SUBSCRIBED, "FACILITY_NOT_SUBSCRIBED", "Facility not subscribed" },
	{ AST_CAUSE_OUTGOING_CALL_BARRED, "OUTGOING_CALL_BARRED", "Outgoing call barred" },
	{ AST_CAUSE_INCOMING_CALL_BARRED, "INCOMING_CALL_BARRED", "Incoming call barred" },
	{ AST_CAUSE_BEARERCAPABILITY_NOTAUTH, "BEARERCAPABILITY_NOTAUTH", "Bearer capability not authorized" },
	{ AST_CAUSE_BEARERCAPABILITY_NOTAVAIL, "BEARERCAPABILITY_NOTAVAIL", "Bearer capability not available" },
	{ AST_CAUSE_BEARERCAPABILITY_NOTIMPL, "BEARERCAPABILITY_NOTIMPL", "Bearer capability not implemented" },
	{ AST_CAUSE_CHAN_NOT_IMPLEMENTED, "CHAN_NOT_IMPLEMENTED", "Channel not implemented" },
	{ AST_CAUSE_FACILITY_NOT_IMPLEMENTED, "FACILITY_NOT_IMPLEMENTED", "Facility not implemented" },
	{ AST_CAUSE_INVALID_CALL_REFERENCE, "INVALID_CALL_REFERENCE", "Invalid call reference value" },
	{ AST_CAUSE_INCOMPATIBLE_DESTINATION, "INCOMPATIBLE_DESTINATION", "Incompatible destination" },
	{ AST_CAUSE_INVALID_MSG_UNSPECIFIED, "INVALID_MSG_UNSPECIFIED", "Invalid message unspecified" },
	{ AST_CAUSE_MANDATORY_IE_MISSING, "MANDATORY_IE_MISSING", "Mandatory information element is missing" },
	{ AST_CAUSE_MESSAGE_TYPE_NONEXIST, "MESSAGE_TYPE_NONEXIST", "Message type nonexist." },
	{ AST_CAUSE_WRONG_MESSAGE, "WRONG_MESSAGE", "Wrong message" },
	{ AST_CAUSE_IE_NONEXIST, "IE_NONEXIST", "Info. element nonexist or not implemented" },
	{ AST_CAUSE_INVALID_IE_CONTENTS, "INVALID_IE_CONTENTS", "Invalid information element contents" },
	{ AST_CAUSE_WRONG_CALL_STATE, "WRONG_CALL_STATE", "Message not compatible with call state" },
	{ AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE, "RECOVERY_ON_TIMER_EXPIRE", "Recover on timer expiry" },
	{ AST_CAUSE_MANDATORY_IE_LENGTH_ERROR, "MANDATORY_IE_LENGTH_ERROR", "Mandatory IE length error" },
	{ AST_CAUSE_PROTOCOL_ERROR, "PROTOCOL_ERROR", "Protocol error, unspecified" },
	{ AST_CAUSE_INTERWORKING, "INTERWORKING", "Interworking, unspecified" },
};

struct ast_variable *ast_channeltype_list(void)
{
	struct chanlist *cl;
	struct ast_variable *var = NULL, *prev = NULL;

	AST_RWLIST_RDLOCK(&backends);
	AST_RWLIST_TRAVERSE(&backends, cl, list) {
		if (prev)  {
			if ((prev->next = ast_variable_new(cl->tech->type, cl->tech->description, "")))
				prev = prev->next;
		} else {
			var = ast_variable_new(cl->tech->type, cl->tech->description, "");
			prev = var;
		}
	}
	AST_RWLIST_UNLOCK(&backends);

	return var;
}

static void channel_data_add_flags(struct ast_data *tree,
	struct ast_channel *chan)
{
	ast_data_add_bool(tree, "DEFER_DTMF", ast_test_flag(chan, AST_FLAG_DEFER_DTMF));
	ast_data_add_bool(tree, "WRITE_INT", ast_test_flag(chan, AST_FLAG_WRITE_INT));
	ast_data_add_bool(tree, "BLOCKING", ast_test_flag(chan, AST_FLAG_BLOCKING));
	ast_data_add_bool(tree, "ZOMBIE", ast_test_flag(chan, AST_FLAG_ZOMBIE));
	ast_data_add_bool(tree, "EXCEPTION", ast_test_flag(chan, AST_FLAG_EXCEPTION));
	ast_data_add_bool(tree, "MOH", ast_test_flag(chan, AST_FLAG_MOH));
	ast_data_add_bool(tree, "SPYING", ast_test_flag(chan, AST_FLAG_SPYING));
	ast_data_add_bool(tree, "NBRIDGE", ast_test_flag(chan, AST_FLAG_NBRIDGE));
	ast_data_add_bool(tree, "IN_AUTOLOOP", ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP));
	ast_data_add_bool(tree, "OUTGOING", ast_test_flag(chan, AST_FLAG_OUTGOING));
	ast_data_add_bool(tree, "IN_DTMF", ast_test_flag(chan, AST_FLAG_IN_DTMF));
	ast_data_add_bool(tree, "EMULATE_DTMF", ast_test_flag(chan, AST_FLAG_EMULATE_DTMF));
	ast_data_add_bool(tree, "END_DTMF_ONLY", ast_test_flag(chan, AST_FLAG_END_DTMF_ONLY));
	ast_data_add_bool(tree, "ANSWERED_ELSEWHERE", ast_test_flag(chan, AST_FLAG_ANSWERED_ELSEWHERE));
	ast_data_add_bool(tree, "MASQ_NOSTREAM", ast_test_flag(chan, AST_FLAG_MASQ_NOSTREAM));
	ast_data_add_bool(tree, "BRIDGE_HANGUP_RUN", ast_test_flag(chan, AST_FLAG_BRIDGE_HANGUP_RUN));
	ast_data_add_bool(tree, "BRIDGE_HANGUP_DONT", ast_test_flag(chan, AST_FLAG_BRIDGE_HANGUP_DONT));
	ast_data_add_bool(tree, "DISABLE_WORKAROUNDS", ast_test_flag(chan, AST_FLAG_DISABLE_WORKAROUNDS));
}

#if defined(KEEP_TILL_CHANNEL_PARTY_NUMBER_INFO_NEEDED)
static const char *party_number_ton2str(int ton)
{
#if defined(HAVE_PRI)
	switch ((ton >> 4) & 0x07) {
	case PRI_TON_INTERNATIONAL:
		return "International";
	case PRI_TON_NATIONAL:
		return "National";
	case PRI_TON_NET_SPECIFIC:
		return "Network Specific";
	case PRI_TON_SUBSCRIBER:
		return "Subscriber";
	case PRI_TON_ABBREVIATED:
		return "Abbreviated";
	case PRI_TON_RESERVED:
		return "Reserved";
	case PRI_TON_UNKNOWN:
	default:
		break;
	}
#endif	/* defined(HAVE_PRI) */
	return "Unknown";
}
#endif	/* defined(KEEP_TILL_CHANNEL_PARTY_NUMBER_INFO_NEEDED) */

#if defined(KEEP_TILL_CHANNEL_PARTY_NUMBER_INFO_NEEDED)
static const char *party_number_plan2str(int plan)
{
#if defined(HAVE_PRI)
	switch (plan & 0x0F) {
	default:
	case PRI_NPI_UNKNOWN:
		break;
	case PRI_NPI_E163_E164:
		return "Public (E.163/E.164)";
	case PRI_NPI_X121:
		return "Data (X.121)";
	case PRI_NPI_F69:
		return "Telex (F.69)";
	case PRI_NPI_NATIONAL:
		return "National Standard";
	case PRI_NPI_PRIVATE:
		return "Private";
	case PRI_NPI_RESERVED:
		return "Reserved";
	}
#endif	/* defined(HAVE_PRI) */
	return "Unknown";
}
#endif	/* defined(KEEP_TILL_CHANNEL_PARTY_NUMBER_INFO_NEEDED) */

int ast_channel_data_add_structure(struct ast_data *tree,
	struct ast_channel *chan, int add_bridged)
{
	struct ast_channel *bc;
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
		bc = ast_bridged_channel(chan);
		if (bc) {
			data_bridged = ast_data_add_node(tree, "bridged");
			if (!data_bridged) {
				return -1;
			}
			ast_channel_data_add_structure(data_bridged, bc, 0);
		}
	}

	ast_data_add_codecs(tree, "oldwriteformat", chan->oldwriteformat);
	ast_data_add_codecs(tree, "nativeformats", chan->nativeformats);
	ast_data_add_codecs(tree, "readformat", chan->readformat);
	ast_data_add_codecs(tree, "writeformat", chan->writeformat);
	ast_data_add_codecs(tree, "rawreadformat", chan->rawreadformat);
	ast_data_add_codecs(tree, "rawwriteformat", chan->rawwriteformat);

	/* state */
	enum_node = ast_data_add_node(tree, "state");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_state2str(chan->_state));
	ast_data_add_int(enum_node, "value", chan->_state);

	/* hangupcause */
	enum_node = ast_data_add_node(tree, "hangupcause");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_cause2str(chan->hangupcause));
	ast_data_add_int(enum_node, "value", chan->hangupcause);

	/* amaflags */
	enum_node = ast_data_add_node(tree, "amaflags");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_cdr_flags2str(chan->amaflags));
	ast_data_add_int(enum_node, "value", chan->amaflags);

	/* transfercapability */
	enum_node = ast_data_add_node(tree, "transfercapability");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_transfercapability2str(chan->transfercapability));
	ast_data_add_int(enum_node, "value", chan->transfercapability);

	/* _softphangup */
	data_softhangup = ast_data_add_node(tree, "softhangup");
	if (!data_softhangup) {
		return -1;
	}
	ast_data_add_bool(data_softhangup, "dev", chan->_softhangup & AST_SOFTHANGUP_DEV);
	ast_data_add_bool(data_softhangup, "asyncgoto", chan->_softhangup & AST_SOFTHANGUP_ASYNCGOTO);
	ast_data_add_bool(data_softhangup, "shutdown", chan->_softhangup & AST_SOFTHANGUP_SHUTDOWN);
	ast_data_add_bool(data_softhangup, "timeout", chan->_softhangup & AST_SOFTHANGUP_TIMEOUT);
	ast_data_add_bool(data_softhangup, "appunload", chan->_softhangup & AST_SOFTHANGUP_APPUNLOAD);
	ast_data_add_bool(data_softhangup, "explicit", chan->_softhangup & AST_SOFTHANGUP_EXPLICIT);
	ast_data_add_bool(data_softhangup, "unbridge", chan->_softhangup & AST_SOFTHANGUP_UNBRIDGE);

	/* channel flags */
	data_flags = ast_data_add_node(tree, "flags");
	if (!data_flags) {
		return -1;
	}
	channel_data_add_flags(data_flags, chan);

	ast_data_add_uint(tree, "timetohangup", chan->whentohangup.tv_sec);

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
	if (chan->zone) {
		data_zones = ast_data_add_node(tree, "zone");
		if (!data_zones) {
			return -1;
		}
		ast_tone_zone_data_add_structure(data_zones, chan->zone);
	}

	/* insert cdr */
	data_cdr = ast_data_add_node(tree, "cdr");
	if (!data_cdr) {
		return -1;
	}

	ast_cdr_data_add_structure(data_cdr, chan->cdr, 1);

	return 0;
}

int ast_channel_data_cmp_structure(const struct ast_data_search *tree,
	struct ast_channel *chan, const char *structure_name)
{
	return ast_data_search_cmp_structure(tree, ast_channel, chan, structure_name);
}

/*! \brief Show channel types - CLI command */
static char *handle_cli_core_show_channeltypes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT  "%-10.10s  %-40.40s %-12.12s %-12.12s %-12.12s\n"
	struct chanlist *cl;
	int count_chan = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show channeltypes";
		e->usage =
			"Usage: core show channeltypes\n"
			"       Lists available channel types registered in your\n"
			"       Asterisk server.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, FORMAT, "Type", "Description",       "Devicestate", "Indications", "Transfer");
	ast_cli(a->fd, FORMAT, "----------", "-----------", "-----------", "-----------", "--------");

	AST_RWLIST_RDLOCK(&backends);
	AST_RWLIST_TRAVERSE(&backends, cl, list) {
		ast_cli(a->fd, FORMAT, cl->tech->type, cl->tech->description,
			(cl->tech->devicestate) ? "yes" : "no",
			(cl->tech->indicate) ? "yes" : "no",
			(cl->tech->transfer) ? "yes" : "no");
		count_chan++;
	}
	AST_RWLIST_UNLOCK(&backends);

	ast_cli(a->fd, "----------\n%d channel drivers registered.\n", count_chan);

	return CLI_SUCCESS;

#undef FORMAT
}

static char *complete_channeltypes(struct ast_cli_args *a)
{
	struct chanlist *cl;
	int which = 0;
	int wordlen;
	char *ret = NULL;

	if (a->pos != 3)
		return NULL;

	wordlen = strlen(a->word);

	AST_RWLIST_RDLOCK(&backends);
	AST_RWLIST_TRAVERSE(&backends, cl, list) {
		if (!strncasecmp(a->word, cl->tech->type, wordlen) && ++which > a->n) {
			ret = ast_strdup(cl->tech->type);
			break;
		}
	}
	AST_RWLIST_UNLOCK(&backends);
	
	return ret;
}

/*! \brief Show details about a channel driver - CLI command */
static char *handle_cli_core_show_channeltype(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chanlist *cl = NULL;
	char buf[512];

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show channeltype";
		e->usage =
			"Usage: core show channeltype <name>\n"
			"	Show details about the specified channel type, <name>.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_channeltypes(a);
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	
	AST_RWLIST_RDLOCK(&backends);

	AST_RWLIST_TRAVERSE(&backends, cl, list) {
		if (!strncasecmp(cl->tech->type, a->argv[3], strlen(cl->tech->type)))
			break;
	}


	if (!cl) {
		ast_cli(a->fd, "\n%s is not a registered channel driver.\n", a->argv[3]);
		AST_RWLIST_UNLOCK(&backends);
		return CLI_FAILURE;
	}

	ast_cli(a->fd,
		"-- Info about channel driver: %s --\n"
		"  Device State: %s\n"
		"    Indication: %s\n"
		"     Transfer : %s\n"
		"  Capabilities: %s\n"
		"   Digit Begin: %s\n"
		"     Digit End: %s\n"
		"    Send HTML : %s\n"
		" Image Support: %s\n"
		"  Text Support: %s\n",
		cl->tech->type,
		(cl->tech->devicestate) ? "yes" : "no",
		(cl->tech->indicate) ? "yes" : "no",
		(cl->tech->transfer) ? "yes" : "no",
		ast_getformatname_multiple(buf, sizeof(buf), (cl->tech->capabilities) ? cl->tech->capabilities : -1),
		(cl->tech->send_digit_begin) ? "yes" : "no",
		(cl->tech->send_digit_end) ? "yes" : "no",
		(cl->tech->send_html) ? "yes" : "no",
		(cl->tech->send_image) ? "yes" : "no",
		(cl->tech->send_text) ? "yes" : "no"
		
	);

	AST_RWLIST_UNLOCK(&backends);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_channel[] = {
	AST_CLI_DEFINE(handle_cli_core_show_channeltypes, "List available channel types"),
	AST_CLI_DEFINE(handle_cli_core_show_channeltype,  "Give more details on that channel type")
};

#ifdef CHANNEL_TRACE
/*! \brief Destructor for the channel trace datastore */
static void ast_chan_trace_destroy_cb(void *data)
{
	struct ast_chan_trace *trace;
	struct ast_chan_trace_data *traced = data;
	while ((trace = AST_LIST_REMOVE_HEAD(&traced->trace, entry))) {
		ast_free(trace);
	}
	ast_free(traced);
}

/*! \brief Datastore to put the linked list of ast_chan_trace and trace status */
static const struct ast_datastore_info ast_chan_trace_datastore_info = {
	.type = "ChanTrace",
	.destroy = ast_chan_trace_destroy_cb
};

/*! \brief Put the channel backtrace in a string */
int ast_channel_trace_serialize(struct ast_channel *chan, struct ast_str **buf)
{
	int total = 0;
	struct ast_chan_trace *trace;
	struct ast_chan_trace_data *traced;
	struct ast_datastore *store;

	ast_channel_lock(chan);
	store = ast_channel_datastore_find(chan, &ast_chan_trace_datastore_info, NULL);
	if (!store) {
		ast_channel_unlock(chan);
		return total;
	}
	traced = store->data;
	ast_str_reset(*buf);
	AST_LIST_TRAVERSE(&traced->trace, trace, entry) {
		if (ast_str_append(buf, 0, "[%d] => %s, %s, %d\n", total, trace->context, trace->exten, trace->priority) < 0) {
			ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
			total = -1;
			break;
		}
		total++;
	}
	ast_channel_unlock(chan);
	return total;
}

/* !\brief Whether or not context tracing is enabled */
int ast_channel_trace_is_enabled(struct ast_channel *chan)
{
	struct ast_datastore *store = ast_channel_datastore_find(chan, &ast_chan_trace_datastore_info, NULL);
	if (!store)
		return 0;
	return ((struct ast_chan_trace_data *)store->data)->enabled;
}

/*! \brief Update the context backtrace data if tracing is enabled */
static int ast_channel_trace_data_update(struct ast_channel *chan, struct ast_chan_trace_data *traced)
{
	struct ast_chan_trace *trace;
	if (!traced->enabled)
		return 0;
	/* If the last saved context does not match the current one
	   OR we have not saved any context so far, then save the current context */
	if ((!AST_LIST_EMPTY(&traced->trace) && strcasecmp(AST_LIST_FIRST(&traced->trace)->context, chan->context)) || 
	    (AST_LIST_EMPTY(&traced->trace))) {
		/* Just do some debug logging */
		if (AST_LIST_EMPTY(&traced->trace))
			ast_log(LOG_DEBUG, "Setting initial trace context to %s\n", chan->context);
		else
			ast_log(LOG_DEBUG, "Changing trace context from %s to %s\n", AST_LIST_FIRST(&traced->trace)->context, chan->context);
		/* alloc or bail out */
		trace = ast_malloc(sizeof(*trace));
		if (!trace) 
			return -1;
		/* save the current location and store it in the trace list */
		ast_copy_string(trace->context, chan->context, sizeof(trace->context));
		ast_copy_string(trace->exten, chan->exten, sizeof(trace->exten));
		trace->priority = chan->priority;
		AST_LIST_INSERT_HEAD(&traced->trace, trace, entry);
	}
	return 0;
}

/*! \brief Update the context backtrace if tracing is enabled */
int ast_channel_trace_update(struct ast_channel *chan)
{
	struct ast_datastore *store = ast_channel_datastore_find(chan, &ast_chan_trace_datastore_info, NULL);
	if (!store)
		return 0;
	return ast_channel_trace_data_update(chan, store->data);
}

/*! \brief Enable context tracing in the channel */
int ast_channel_trace_enable(struct ast_channel *chan)
{
	struct ast_datastore *store = ast_channel_datastore_find(chan, &ast_chan_trace_datastore_info, NULL);
	struct ast_chan_trace_data *traced;
	if (!store) {
		store = ast_datastore_alloc(&ast_chan_trace_datastore_info, "ChanTrace");
		if (!store) 
			return -1;
		traced = ast_calloc(1, sizeof(*traced));
		if (!traced) {
			ast_datastore_free(store);
			return -1;
		}	
		store->data = traced;
		AST_LIST_HEAD_INIT_NOLOCK(&traced->trace);
		ast_channel_datastore_add(chan, store);
	}	
	((struct ast_chan_trace_data *)store->data)->enabled = 1;
	ast_channel_trace_data_update(chan, store->data);
	return 0;
}

/*! \brief Disable context tracing in the channel */
int ast_channel_trace_disable(struct ast_channel *chan)
{
	struct ast_datastore *store = ast_channel_datastore_find(chan, &ast_chan_trace_datastore_info, NULL);
	if (!store)
		return 0;
	((struct ast_chan_trace_data *)store->data)->enabled = 0;
	return 0;
}
#endif /* CHANNEL_TRACE */

/*! \brief Checks to see if a channel is needing hang up */
int ast_check_hangup(struct ast_channel *chan)
{
	if (chan->_softhangup)		/* yes if soft hangup flag set */
		return 1;
	if (ast_tvzero(chan->whentohangup))	/* no if no hangup scheduled */
		return 0;
	if (ast_tvdiff_ms(chan->whentohangup, ast_tvnow()) > 0) 	/* no if hangup time has not come yet. */
		return 0;
	ast_debug(4, "Hangup time has come: %" PRIi64 "\n", ast_tvdiff_ms(chan->whentohangup, ast_tvnow()));
	chan->_softhangup |= AST_SOFTHANGUP_TIMEOUT;	/* record event */
	return 1;
}

int ast_check_hangup_locked(struct ast_channel *chan)
{
	int res;
	ast_channel_lock(chan);
	res = ast_check_hangup(chan);
	ast_channel_unlock(chan);
	return res;
}

static int ast_channel_softhangup_cb(void *obj, void *arg, int flags)
{
	struct ast_channel *chan = obj;

	ast_softhangup(chan, AST_SOFTHANGUP_SHUTDOWN);

	return 0;
}

void ast_begin_shutdown(int hangup)
{
	shutting_down = 1;

	if (hangup) {
		ao2_callback(channels, OBJ_NODATA | OBJ_MULTIPLE, ast_channel_softhangup_cb, NULL);
	}
}

/*! \brief returns number of active/allocated channels */
int ast_active_channels(void)
{
	return channels ? ao2_container_count(channels) : 0;
}

/*! \brief Cancel a shutdown in progress */
void ast_cancel_shutdown(void)
{
	shutting_down = 0;
}

/*! \brief Returns non-zero if Asterisk is being shut down */
int ast_shutting_down(void)
{
	return shutting_down;
}

/*! \brief Set when to hangup channel */
void ast_channel_setwhentohangup_tv(struct ast_channel *chan, struct timeval offset)
{
	chan->whentohangup = ast_tvzero(offset) ? offset : ast_tvadd(offset, ast_tvnow());
	ast_queue_frame(chan, &ast_null_frame);
	return;
}

void ast_channel_setwhentohangup(struct ast_channel *chan, time_t offset)
{
	struct timeval when = { offset, };
	ast_channel_setwhentohangup_tv(chan, when);
}

/*! \brief Compare a offset with when to hangup channel */
int ast_channel_cmpwhentohangup_tv(struct ast_channel *chan, struct timeval offset)
{
	struct timeval whentohangup;

	if (ast_tvzero(chan->whentohangup))
		return ast_tvzero(offset) ? 0 : -1;

	if (ast_tvzero(offset))
		return 1;

	whentohangup = ast_tvadd(offset, ast_tvnow());

	return ast_tvdiff_ms(whentohangup, chan->whentohangup);
}

int ast_channel_cmpwhentohangup(struct ast_channel *chan, time_t offset)
{
	struct timeval when = { offset, };
	return ast_channel_cmpwhentohangup_tv(chan, when);
}

/*! \brief Register a new telephony channel in Asterisk */
int ast_channel_register(const struct ast_channel_tech *tech)
{
	struct chanlist *chan;

	AST_RWLIST_WRLOCK(&backends);

	AST_RWLIST_TRAVERSE(&backends, chan, list) {
		if (!strcasecmp(tech->type, chan->tech->type)) {
			ast_log(LOG_WARNING, "Already have a handler for type '%s'\n", tech->type);
			AST_RWLIST_UNLOCK(&backends);
			return -1;
		}
	}
	
	if (!(chan = ast_calloc(1, sizeof(*chan)))) {
		AST_RWLIST_UNLOCK(&backends);
		return -1;
	}
	chan->tech = tech;
	AST_RWLIST_INSERT_HEAD(&backends, chan, list);

	ast_debug(1, "Registered handler for '%s' (%s)\n", chan->tech->type, chan->tech->description);

	ast_verb(2, "Registered channel type '%s' (%s)\n", chan->tech->type, chan->tech->description);

	AST_RWLIST_UNLOCK(&backends);

	return 0;
}

/*! \brief Unregister channel driver */
void ast_channel_unregister(const struct ast_channel_tech *tech)
{
	struct chanlist *chan;

	ast_debug(1, "Unregistering channel type '%s'\n", tech->type);

	AST_RWLIST_WRLOCK(&backends);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&backends, chan, list) {
		if (chan->tech == tech) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(chan);
			ast_verb(2, "Unregistered channel type '%s'\n", tech->type);
			break;	
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	AST_RWLIST_UNLOCK(&backends);
}

/*! \brief Get handle to channel driver based on name */
const struct ast_channel_tech *ast_get_channel_tech(const char *name)
{
	struct chanlist *chanls;
	const struct ast_channel_tech *ret = NULL;

	AST_RWLIST_RDLOCK(&backends);

	AST_RWLIST_TRAVERSE(&backends, chanls, list) {
		if (!strcasecmp(name, chanls->tech->type)) {
			ret = chanls->tech;
			break;
		}
	}

	AST_RWLIST_UNLOCK(&backends);
	
	return ret;
}

/*! \brief Gives the string form of a given hangup cause */
const char *ast_cause2str(int cause)
{
	int x;

	for (x = 0; x < ARRAY_LEN(causes); x++) {
		if (causes[x].cause == cause)
			return causes[x].desc;
	}

	return "Unknown";
}

/*! \brief Convert a symbolic hangup cause to number */
int ast_str2cause(const char *name)
{
	int x;

	for (x = 0; x < ARRAY_LEN(causes); x++)
		if (!strncasecmp(causes[x].name, name, strlen(causes[x].name)))
			return causes[x].cause;

	return -1;
}

/*! \brief Gives the string form of a given channel state.
	\note This function is not reentrant.
 */
const char *ast_state2str(enum ast_channel_state state)
{
	char *buf;

	switch (state) {
	case AST_STATE_DOWN:
		return "Down";
	case AST_STATE_RESERVED:
		return "Rsrvd";
	case AST_STATE_OFFHOOK:
		return "OffHook";
	case AST_STATE_DIALING:
		return "Dialing";
	case AST_STATE_RING:
		return "Ring";
	case AST_STATE_RINGING:
		return "Ringing";
	case AST_STATE_UP:
		return "Up";
	case AST_STATE_BUSY:
		return "Busy";
	case AST_STATE_DIALING_OFFHOOK:
		return "Dialing Offhook";
	case AST_STATE_PRERING:
		return "Pre-ring";
	default:
		if (!(buf = ast_threadstorage_get(&state2str_threadbuf, STATE2STR_BUFSIZE)))
			return "Unknown";
		snprintf(buf, STATE2STR_BUFSIZE, "Unknown (%d)", state);
		return buf;
	}
}

/*! \brief Gives the string form of a given transfer capability */
char *ast_transfercapability2str(int transfercapability)
{
	switch (transfercapability) {
	case AST_TRANS_CAP_SPEECH:
		return "SPEECH";
	case AST_TRANS_CAP_DIGITAL:
		return "DIGITAL";
	case AST_TRANS_CAP_RESTRICTED_DIGITAL:
		return "RESTRICTED_DIGITAL";
	case AST_TRANS_CAP_3_1K_AUDIO:
		return "3K1AUDIO";
	case AST_TRANS_CAP_DIGITAL_W_TONES:
		return "DIGITAL_W_TONES";
	case AST_TRANS_CAP_VIDEO:
		return "VIDEO";
	default:
		return "UNKNOWN";
	}
}

/*! \brief Pick the best audio codec */
format_t ast_best_codec(format_t fmts)
{
	/* This just our opinion, expressed in code.  We are asked to choose
	   the best codec to use, given no information */
	int x;
	static const format_t prefs[] =
	{
		/*! Okay, ulaw is used by all telephony equipment, so start with it */
		AST_FORMAT_ULAW,
		/*! Unless of course, you're a silly European, so then prefer ALAW */
		AST_FORMAT_ALAW,
		AST_FORMAT_G719,
		AST_FORMAT_SIREN14,
		AST_FORMAT_SIREN7,
		AST_FORMAT_TESTLAW,
		/*! G.722 is better then all below, but not as common as the above... so give ulaw and alaw priority */
		AST_FORMAT_G722,
		/*! Okay, well, signed linear is easy to translate into other stuff */
		AST_FORMAT_SLINEAR16,
		AST_FORMAT_SLINEAR,
		/*! G.726 is standard ADPCM, in RFC3551 packing order */
		AST_FORMAT_G726,
		/*! G.726 is standard ADPCM, in AAL2 packing order */
		AST_FORMAT_G726_AAL2,
		/*! ADPCM has great sound quality and is still pretty easy to translate */
		AST_FORMAT_ADPCM,
		/*! Okay, we're down to vocoders now, so pick GSM because it's small and easier to
		    translate and sounds pretty good */
		AST_FORMAT_GSM,
		/*! iLBC is not too bad */
		AST_FORMAT_ILBC,
		/*! Speex is free, but computationally more expensive than GSM */
		AST_FORMAT_SPEEX16,
		AST_FORMAT_SPEEX,
		/*! Ick, LPC10 sounds terrible, but at least we have code for it, if you're tacky enough
		    to use it */
		AST_FORMAT_LPC10,
		/*! G.729a is faster than 723 and slightly less expensive */
		AST_FORMAT_G729A,
		/*! Down to G.723.1 which is proprietary but at least designed for voice */
		AST_FORMAT_G723_1,
	};
	char buf[512];

	/* Strip out video */
	fmts &= AST_FORMAT_AUDIO_MASK;
	
	/* Find the first preferred codec in the format given */
	for (x = 0; x < ARRAY_LEN(prefs); x++) {
		if (fmts & prefs[x])
			return prefs[x];
	}

	ast_log(LOG_WARNING, "Don't know any of %s formats\n", ast_getformatname_multiple(buf, sizeof(buf), fmts));

	return 0;
}

static const struct ast_channel_tech null_tech = {
	.type = "NULL",
	.description = "Null channel (should not see this)",
};

static void ast_channel_destructor(void *obj);
static void ast_dummy_channel_destructor(void *obj);

/*! \brief Create a new channel structure */
static struct ast_channel * attribute_malloc __attribute__((format(printf, 13, 0)))
__ast_channel_alloc_ap(int needqueue, int state, const char *cid_num, const char *cid_name,
		       const char *acctcode, const char *exten, const char *context,
		       const char *linkedid, const int amaflag, const char *file, int line,
		       const char *function, const char *name_fmt, va_list ap1, va_list ap2)
{
	struct ast_channel *tmp;
	int x;
	int flags;
	struct varshead *headp;
	char *tech = "";

	/* If shutting down, don't allocate any new channels */
	if (shutting_down) {
		ast_log(LOG_WARNING, "Channel allocation failed: Refusing due to active shutdown\n");
		return NULL;
	}

#if defined(REF_DEBUG)
	tmp = __ao2_alloc_debug(sizeof(*tmp), ast_channel_destructor, "", file, line,
		function, 1);
#elif defined(__AST_DEBUG_MALLOC)
	tmp = __ao2_alloc_debug(sizeof(*tmp), ast_channel_destructor, "", file, line,
		function, 0);
#else
	tmp = ao2_alloc(sizeof(*tmp), ast_channel_destructor);
#endif
	if (!tmp) {
		/* Channel structure allocation failure. */
		return NULL;
	}

	/*
	 * Init file descriptors to unopened state so
	 * the destructor can know not to close them.
	 */
	tmp->timingfd = -1;
	for (x = 0; x < ARRAY_LEN(tmp->alertpipe); ++x) {
		tmp->alertpipe[x] = -1;
	}
	for (x = 0; x < ARRAY_LEN(tmp->fds); ++x) {
		tmp->fds[x] = -1;
	}
#ifdef HAVE_EPOLL
	tmp->epfd = epoll_create(25);
#endif

	if (!(tmp->sched = sched_context_create())) {
		ast_log(LOG_WARNING, "Channel allocation failed: Unable to create schedule context\n");
		return ast_channel_unref(tmp);
	}
	
	ast_party_dialed_init(&tmp->dialed);
	ast_party_caller_init(&tmp->caller);
	ast_party_connected_line_init(&tmp->connected);
	ast_party_redirecting_init(&tmp->redirecting);

	if (cid_name) {
		tmp->caller.id.name.valid = 1;
		tmp->caller.id.name.str = ast_strdup(cid_name);
		if (!tmp->caller.id.name.str) {
			return ast_channel_unref(tmp);
		}
	}
	if (cid_num) {
		tmp->caller.id.number.valid = 1;
		tmp->caller.id.number.str = ast_strdup(cid_num);
		if (!tmp->caller.id.number.str) {
			return ast_channel_unref(tmp);
		}
	}

	if ((tmp->timer = ast_timer_open())) {
		needqueue = 0;
		tmp->timingfd = ast_timer_fd(tmp->timer);
	}

	if (needqueue) {
		if (pipe(tmp->alertpipe)) {
			ast_log(LOG_WARNING, "Channel allocation failed: Can't create alert pipe! Try increasing max file descriptors with ulimit -n\n");
			return ast_channel_unref(tmp);
		} else {
			flags = fcntl(tmp->alertpipe[0], F_GETFL);
			if (fcntl(tmp->alertpipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
				ast_log(LOG_WARNING, "Channel allocation failed: Unable to set alertpipe nonblocking! (%d: %s)\n", errno, strerror(errno));
				return ast_channel_unref(tmp);
			}
			flags = fcntl(tmp->alertpipe[1], F_GETFL);
			if (fcntl(tmp->alertpipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
				ast_log(LOG_WARNING, "Channel allocation failed: Unable to set alertpipe nonblocking! (%d: %s)\n", errno, strerror(errno));
				return ast_channel_unref(tmp);
			}
		}
	}

	/*
	 * This is the last place the channel constructor can fail.
	 *
	 * The destructor takes advantage of this fact to ensure that the
	 * AST_CEL_CHANNEL_END is not posted if we have not posted the
	 * AST_CEL_CHANNEL_START yet.
	 */
	if ((ast_string_field_init(tmp, 128))) {
		return ast_channel_unref(tmp);
	}

	/* Always watch the alertpipe */
	ast_channel_set_fd(tmp, AST_ALERT_FD, tmp->alertpipe[0]);
	/* And timing pipe */
	ast_channel_set_fd(tmp, AST_TIMING_FD, tmp->timingfd);

	/* Initial state */
	tmp->_state = state;

	tmp->streamid = -1;
	
	tmp->fin = global_fin;
	tmp->fout = global_fout;

	if (ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
		ast_string_field_build(tmp, uniqueid, "%li.%d", (long) time(NULL), 
			ast_atomic_fetchadd_int(&uniqueint, 1));
	} else {
		ast_string_field_build(tmp, uniqueid, "%s-%li.%d", ast_config_AST_SYSTEM_NAME, 
			(long) time(NULL), ast_atomic_fetchadd_int(&uniqueint, 1));
	}

	if (!ast_strlen_zero(linkedid)) {
		ast_string_field_set(tmp, linkedid, linkedid);
	} else {
		ast_string_field_set(tmp, linkedid, tmp->uniqueid);
	}

	if (!ast_strlen_zero(name_fmt)) {
		char *slash;
		/* Almost every channel is calling this function, and setting the name via the ast_string_field_build() call.
		 * And they all use slightly different formats for their name string.
		 * This means, to set the name here, we have to accept variable args, and call the string_field_build from here.
		 * This means, that the stringfields must have a routine that takes the va_lists directly, and 
		 * uses them to build the string, instead of forming the va_lists internally from the vararg ... list.
		 * This new function was written so this can be accomplished.
		 */
		ast_string_field_build_va(tmp, name, name_fmt, ap1, ap2);
		tech = ast_strdupa(tmp->name);
		if ((slash = strchr(tech, '/'))) {
			*slash = '\0';
		}
	} else {
		/*
		 * Start the string with '-' so it becomes an empty string
		 * in the destructor.
		 */
		ast_string_field_set(tmp, name, "-**Unknown**");
	}

	/* Reminder for the future: under what conditions do we NOT want to track cdrs on channels? */

	/* These 4 variables need to be set up for the cdr_init() to work right */
	if (amaflag)
		tmp->amaflags = amaflag;
	else
		tmp->amaflags = ast_default_amaflags;
	
	if (!ast_strlen_zero(acctcode))
		ast_string_field_set(tmp, accountcode, acctcode);
	else
		ast_string_field_set(tmp, accountcode, ast_default_accountcode);
		
	if (!ast_strlen_zero(context))
		ast_copy_string(tmp->context, context, sizeof(tmp->context));
	else
		strcpy(tmp->context, "default");

	if (!ast_strlen_zero(exten))
		ast_copy_string(tmp->exten, exten, sizeof(tmp->exten));
	else
		strcpy(tmp->exten, "s");

	tmp->priority = 1;

	tmp->cdr = ast_cdr_alloc();
	ast_cdr_init(tmp->cdr, tmp);
	ast_cdr_start(tmp->cdr);

	ast_cel_report_event(tmp, AST_CEL_CHANNEL_START, NULL, NULL, NULL);

	headp = &tmp->varshead;
	AST_LIST_HEAD_INIT_NOLOCK(headp);
	
	AST_LIST_HEAD_INIT_NOLOCK(&tmp->datastores);

	AST_LIST_HEAD_INIT_NOLOCK(&tmp->autochans);
	
	ast_string_field_set(tmp, language, defaultlanguage);

	tmp->tech = &null_tech;

	ao2_link(channels, tmp);

	/*
	 * And now, since the channel structure is built, and has its name, let's
	 * call the manager event generator with this Newchannel event. This is the
	 * proper and correct place to make this call, but you sure do have to pass
	 * a lot of data into this func to do it here!
	 */
	if (ast_get_channel_tech(tech)) {
		ast_manager_event(tmp, EVENT_FLAG_CALL, "Newchannel",
			"Channel: %s\r\n"
			"ChannelState: %d\r\n"
			"ChannelStateDesc: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n"
			"AccountCode: %s\r\n"
			"Exten: %s\r\n"
			"Context: %s\r\n"
			"Uniqueid: %s\r\n",
			tmp->name, 
			state, 
			ast_state2str(state),
			S_OR(cid_num, ""),
			S_OR(cid_name, ""),
			tmp->accountcode,
			S_OR(exten, ""),
			S_OR(context, ""),
			tmp->uniqueid);
	}

	return tmp;
}

struct ast_channel *__ast_channel_alloc(int needqueue, int state, const char *cid_num,
					const char *cid_name, const char *acctcode,
					const char *exten, const char *context,
					const char *linkedid, const int amaflag,
					const char *file, int line, const char *function,
					const char *name_fmt, ...)
{
	va_list ap1, ap2;
	struct ast_channel *result;

	va_start(ap1, name_fmt);
	va_start(ap2, name_fmt);
	result = __ast_channel_alloc_ap(needqueue, state, cid_num, cid_name, acctcode, exten, context,
					linkedid, amaflag, file, line, function, name_fmt, ap1, ap2);
	va_end(ap1);
	va_end(ap2);

	return result;
}

/* only do the minimum amount of work needed here to make a channel
 * structure that can be used to expand channel vars */
#if defined(REF_DEBUG) || defined(__AST_DEBUG_MALLOC)
struct ast_channel *__ast_dummy_channel_alloc(const char *file, int line, const char *function)
#else
struct ast_channel *ast_dummy_channel_alloc(void)
#endif
{
	struct ast_channel *tmp;
	struct varshead *headp;

#if defined(REF_DEBUG)
	tmp = __ao2_alloc_debug(sizeof(*tmp), ast_dummy_channel_destructor, "dummy channel",
		file, line, function, 1);
#elif defined(__AST_DEBUG_MALLOC)
	tmp = __ao2_alloc_debug(sizeof(*tmp), ast_dummy_channel_destructor, "dummy channel",
		file, line, function, 0);
#else
	tmp = ao2_alloc(sizeof(*tmp), ast_dummy_channel_destructor);
#endif
	if (!tmp) {
		/* Dummy channel structure allocation failure. */
		return NULL;
	}

	if ((ast_string_field_init(tmp, 128))) {
		return ast_channel_unref(tmp);
	}

	headp = &tmp->varshead;
	AST_LIST_HEAD_INIT_NOLOCK(headp);

	return tmp;
}

static int __ast_queue_frame(struct ast_channel *chan, struct ast_frame *fin, int head, struct ast_frame *after)
{
	struct ast_frame *f;
	struct ast_frame *cur;
	int blah = 1;
	unsigned int new_frames = 0;
	unsigned int new_voice_frames = 0;
	unsigned int queued_frames = 0;
	unsigned int queued_voice_frames = 0;
	AST_LIST_HEAD_NOLOCK(, ast_frame) frames;

	ast_channel_lock(chan);

	/* See if the last frame on the queue is a hangup, if so don't queue anything */
	if ((cur = AST_LIST_LAST(&chan->readq)) &&
	    (cur->frametype == AST_FRAME_CONTROL) &&
	    (cur->subclass.integer == AST_CONTROL_HANGUP)) {
		ast_channel_unlock(chan);
		return 0;
	}

	/* Build copies of all the frames and count them */
	AST_LIST_HEAD_INIT_NOLOCK(&frames);
	for (cur = fin; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
		if (!(f = ast_frdup(cur))) {
			ast_frfree(AST_LIST_FIRST(&frames));
			ast_channel_unlock(chan);
			return -1;
		}

		AST_LIST_INSERT_TAIL(&frames, f, frame_list);
		new_frames++;
		if (f->frametype == AST_FRAME_VOICE) {
			new_voice_frames++;
		}
	}

	/* Count how many frames exist on the queue */
	AST_LIST_TRAVERSE(&chan->readq, cur, frame_list) {
		queued_frames++;
		if (cur->frametype == AST_FRAME_VOICE) {
			queued_voice_frames++;
		}
	}

	if ((queued_frames + new_frames > 128 || queued_voice_frames + new_voice_frames > 96)) {
		int count = 0;
		ast_log(LOG_WARNING, "Exceptionally long %squeue length queuing to %s\n", queued_frames + new_frames > 128 ? "" : "voice ", chan->name);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&chan->readq, cur, frame_list) {
			/* Save the most recent frame */
			if (!AST_LIST_NEXT(cur, frame_list)) {
				break;
			} else if (cur->frametype == AST_FRAME_VOICE || cur->frametype == AST_FRAME_VIDEO || cur->frametype == AST_FRAME_NULL) {
				if (++count > 64) {
					break;
				}
				AST_LIST_REMOVE_CURRENT(frame_list);
				ast_frfree(cur);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	if (after) {
		AST_LIST_INSERT_LIST_AFTER(&chan->readq, &frames, after, frame_list);
	} else {
		if (head) {
			AST_LIST_APPEND_LIST(&frames, &chan->readq, frame_list);
			AST_LIST_HEAD_INIT_NOLOCK(&chan->readq);
		}
		AST_LIST_APPEND_LIST(&chan->readq, &frames, frame_list);
	}

	if (chan->alertpipe[1] > -1) {
		if (write(chan->alertpipe[1], &blah, new_frames * sizeof(blah)) != (new_frames * sizeof(blah))) {
			ast_log(LOG_WARNING, "Unable to write to alert pipe on %s (qlen = %d): %s!\n",
				chan->name, queued_frames, strerror(errno));
		}
	} else if (chan->timingfd > -1) {
		ast_timer_enable_continuous(chan->timer);
	} else if (ast_test_flag(chan, AST_FLAG_BLOCKING)) {
		pthread_kill(chan->blocker, SIGURG);
	}

	ast_channel_unlock(chan);

	return 0;
}

int ast_queue_frame(struct ast_channel *chan, struct ast_frame *fin)
{
	return __ast_queue_frame(chan, fin, 0, NULL);
}

int ast_queue_frame_head(struct ast_channel *chan, struct ast_frame *fin)
{
	return __ast_queue_frame(chan, fin, 1, NULL);
}

/*! \brief Queue a hangup frame for channel */
int ast_queue_hangup(struct ast_channel *chan)
{
	struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_HANGUP };
	/* Yeah, let's not change a lock-critical value without locking */
	if (!ast_channel_trylock(chan)) {
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
		ast_channel_unlock(chan);
	}
	return ast_queue_frame(chan, &f);
}

/*! \brief Queue a hangup frame for channel */
int ast_queue_hangup_with_cause(struct ast_channel *chan, int cause)
{
	struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_HANGUP };

	if (cause >= 0)
		f.data.uint32 = cause;

	/* Yeah, let's not change a lock-critical value without locking */
	if (!ast_channel_trylock(chan)) {
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
		if (cause < 0)
			f.data.uint32 = chan->hangupcause;

		ast_channel_unlock(chan);
	}

	return ast_queue_frame(chan, &f);
}

/*! \brief Queue a control frame */
int ast_queue_control(struct ast_channel *chan, enum ast_control_frame_type control)
{
	struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = control };
	return ast_queue_frame(chan, &f);
}

/*! \brief Queue a control frame with payload */
int ast_queue_control_data(struct ast_channel *chan, enum ast_control_frame_type control,
			   const void *data, size_t datalen)
{
	struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = control, .data.ptr = (void *) data, .datalen = datalen };
	return ast_queue_frame(chan, &f);
}

/*! \brief Set defer DTMF flag on channel */
int ast_channel_defer_dtmf(struct ast_channel *chan)
{
	int pre = 0;

	if (chan) {
		pre = ast_test_flag(chan, AST_FLAG_DEFER_DTMF);
		ast_set_flag(chan, AST_FLAG_DEFER_DTMF);
	}
	return pre;
}

/*! \brief Unset defer DTMF flag on channel */
void ast_channel_undefer_dtmf(struct ast_channel *chan)
{
	if (chan)
		ast_clear_flag(chan, AST_FLAG_DEFER_DTMF);
}

struct ast_channel *ast_channel_callback(ao2_callback_data_fn *cb_fn, void *arg,
		void *data, int ao2_flags)
{
	return ao2_callback_data(channels, ao2_flags, cb_fn, arg, data);
}

struct ast_channel_iterator {
	/* storage for non-dynamically allocated iterator */
	struct ao2_iterator simple_iterator;
	/* pointer to the actual iterator (simple_iterator or a dynamically
	 * allocated iterator)
	 */
	struct ao2_iterator *active_iterator;
};

struct ast_channel_iterator *ast_channel_iterator_destroy(struct ast_channel_iterator *i)
{
	ao2_iterator_destroy(i->active_iterator);
	ast_free(i);

	return NULL;
}

static struct ast_channel_iterator *channel_iterator_search(const char *name,
							    size_t name_len, const char *exten,
							    const char *context)
{
	struct ast_channel_iterator *i;
	struct ast_channel tmp_chan = {
		.name = name,
		/* This is sort of a hack.  Basically, we're using an arbitrary field
		 * in ast_channel to pass the name_len for a prefix match.  If this
		 * gets changed, then the compare callback must be changed, too. */
		.rings = name_len,
	};

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	if (exten) {
		ast_copy_string(tmp_chan.exten, exten, sizeof(tmp_chan.exten));
	}

	if (context) {
		ast_copy_string(tmp_chan.context, context, sizeof(tmp_chan.context));
	}

	if (!(i->active_iterator = ao2_find(channels, &tmp_chan,
					    OBJ_MULTIPLE | ((!ast_strlen_zero(name) && (name_len == 0)) ? OBJ_POINTER : 0)))) {
		    ast_free(i);
		    return NULL;
	}

	return i;
}

struct ast_channel_iterator *ast_channel_iterator_by_exten_new(const char *exten, const char *context)
{
	return channel_iterator_search(NULL, 0, exten, context);
}

struct ast_channel_iterator *ast_channel_iterator_by_name_new(const char *name, size_t name_len)
{
	return channel_iterator_search(name, name_len, NULL, NULL);
}

struct ast_channel_iterator *ast_channel_iterator_all_new(void)
{
	struct ast_channel_iterator *i;

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->simple_iterator = ao2_iterator_init(channels, 0);
	i->active_iterator = &i->simple_iterator;

	return i;
}

struct ast_channel *ast_channel_iterator_next(struct ast_channel_iterator *i)
{
	return ao2_iterator_next(i->active_iterator);
}

static int ast_channel_cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_channel *chan = obj, *cmp_args = arg;
	size_t name_len;
	int ret = CMP_MATCH;

	/* This is sort of a hack.  Basically, we're using an arbitrary field
	 * in ast_channel to pass the name_len for a prefix match.  If this
	 * gets changed, then the uses of ao2_find() must be changed, too. */
	name_len = cmp_args->rings;

	ast_channel_lock(chan);

	if (!ast_strlen_zero(cmp_args->name)) { /* match by name */
		if ((!name_len && strcasecmp(chan->name, cmp_args->name)) ||
				(name_len && strncasecmp(chan->name, cmp_args->name, name_len))) {
			ret = 0; /* name match failed */
		}
	} else if (!ast_strlen_zero(cmp_args->exten)) {
		if (cmp_args->context && strcasecmp(chan->context, cmp_args->context) &&
				strcasecmp(chan->macrocontext, cmp_args->context)) {
			ret = 0; /* context match failed */
		}
		if (ret && strcasecmp(chan->exten, cmp_args->exten) &&
				strcasecmp(chan->macroexten, cmp_args->exten)) {
			ret = 0; /* exten match failed */
		}
	} else if (!ast_strlen_zero(cmp_args->uniqueid)) {
		if ((!name_len && strcasecmp(chan->uniqueid, cmp_args->uniqueid)) ||
				(name_len && strncasecmp(chan->uniqueid, cmp_args->uniqueid, name_len))) {
			ret = 0; /* uniqueid match failed */
		}
	} else {
		ret = 0;
	}

	ast_channel_unlock(chan);

	return ret;
}

static struct ast_channel *ast_channel_get_full(const char *name, size_t name_len,
						const char *exten, const char *context)
{
	struct ast_channel tmp_chan = {
		.name = name,
		/* This is sort of a hack.  Basically, we're using an arbitrary field
		 * in ast_channel to pass the name_len for a prefix match.  If this
		 * gets changed, then the compare callback must be changed, too. */
		.rings = name_len,
	};
	struct ast_channel *chan;

	if (exten) {
		ast_copy_string(tmp_chan.exten, exten, sizeof(tmp_chan.exten));
	}

	if (context) {
		ast_copy_string(tmp_chan.context, context, sizeof(tmp_chan.context));
	}

	if ((chan = ao2_find(channels, &tmp_chan,
			     (!ast_strlen_zero(name) && (name_len == 0)) ? OBJ_POINTER : 0))) {
		return chan;
	}

	if (!name) {
		return NULL;
	}

	/* If name was specified, but the result was NULL, 
	 * try a search on uniqueid, instead. */

	{
		struct ast_channel tmp_chan2 = {
			.uniqueid = name,
			.rings = name_len,
		};

		return ao2_find(channels, &tmp_chan2, 0);
	}
}

struct ast_channel *ast_channel_get_by_name(const char *name)
{
	return ast_channel_get_full(name, 0, NULL, NULL);
}

struct ast_channel *ast_channel_get_by_name_prefix(const char *name, size_t name_len)
{
	return ast_channel_get_full(name, name_len, NULL, NULL);
}

struct ast_channel *ast_channel_get_by_exten(const char *exten, const char *context)
{
	return ast_channel_get_full(NULL, 0, exten, context);
}

int ast_is_deferrable_frame(const struct ast_frame *frame)
{
	/* Do not add a default entry in this switch statement.  Each new
	 * frame type should be addressed directly as to whether it should
	 * be queued up or not.
	 */
	switch (frame->frametype) {
	case AST_FRAME_DTMF_END:
	case AST_FRAME_CONTROL:
	case AST_FRAME_TEXT:
	case AST_FRAME_IMAGE:
	case AST_FRAME_HTML:
		return 1;

	case AST_FRAME_DTMF_BEGIN:
	case AST_FRAME_VOICE:
	case AST_FRAME_VIDEO:
	case AST_FRAME_NULL:
	case AST_FRAME_IAX:
	case AST_FRAME_CNG:
	case AST_FRAME_MODEM:
		return 0;
	}
	return 0;
}

/*! \brief Wait, look for hangups and condition arg */
int ast_safe_sleep_conditional(struct ast_channel *chan, int ms, int (*cond)(void*), void *data)
{
	struct ast_frame *f;
	struct ast_silence_generator *silgen = NULL;
	int res = 0;
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_frames;

	AST_LIST_HEAD_INIT_NOLOCK(&deferred_frames);

	/* If no other generator is present, start silencegen while waiting */
	if (ast_opt_transmit_silence && !chan->generatordata) {
		silgen = ast_channel_start_silence_generator(chan);
	}

	while (ms > 0) {
		struct ast_frame *dup_f = NULL;
		if (cond && ((*cond)(data) == 0)) {
			break;
		}
		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			res = -1;
			break;
		}
		if (ms > 0) {
			f = ast_read(chan);
			if (!f) {
				res = -1;
				break;
			}

			if (!ast_is_deferrable_frame(f)) {
				ast_frfree(f);
				continue;
			}
			
			if ((dup_f = ast_frisolate(f))) {
				if (dup_f != f) {
					ast_frfree(f);
				}
				AST_LIST_INSERT_HEAD(&deferred_frames, dup_f, frame_list);
			}
		}
	}

	/* stop silgen if present */
	if (silgen) {
		ast_channel_stop_silence_generator(chan, silgen);
	}

	/* We need to free all the deferred frames, but we only need to
	 * queue the deferred frames if there was no error and no
	 * hangup was received
	 */
	ast_channel_lock(chan);
	while ((f = AST_LIST_REMOVE_HEAD(&deferred_frames, frame_list))) {
		if (!res) {
			ast_queue_frame_head(chan, f);
		}
		ast_frfree(f);
	}
	ast_channel_unlock(chan);

	return res;
}

/*! \brief Wait, look for hangups */
int ast_safe_sleep(struct ast_channel *chan, int ms)
{
	return ast_safe_sleep_conditional(chan, ms, NULL, NULL);
}

struct ast_channel *ast_channel_release(struct ast_channel *chan)
{
	/* Safe, even if already unlinked. */
	ao2_unlink(channels, chan);
	return ast_channel_unref(chan);
}

void ast_party_name_init(struct ast_party_name *init)
{
	init->str = NULL;
	init->char_set = AST_PARTY_CHAR_SET_ISO8859_1;
	init->presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
	init->valid = 0;
}

void ast_party_name_copy(struct ast_party_name *dest, const struct ast_party_name *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_free(dest->str);
	dest->str = ast_strdup(src->str);
	dest->char_set = src->char_set;
	dest->presentation = src->presentation;
	dest->valid = src->valid;
}

void ast_party_name_set_init(struct ast_party_name *init, const struct ast_party_name *guide)
{
	init->str = NULL;
	init->char_set = guide->char_set;
	init->presentation = guide->presentation;
	init->valid = guide->valid;
}

void ast_party_name_set(struct ast_party_name *dest, const struct ast_party_name *src)
{
	if (dest == src) {
		/* Don't set to self */
		return;
	}

	if (src->str && src->str != dest->str) {
		ast_free(dest->str);
		dest->str = ast_strdup(src->str);
	}

	dest->char_set = src->char_set;
	dest->presentation = src->presentation;
	dest->valid = src->valid;
}

void ast_party_name_free(struct ast_party_name *doomed)
{
	ast_free(doomed->str);
	doomed->str = NULL;
}

void ast_party_number_init(struct ast_party_number *init)
{
	init->str = NULL;
	init->plan = 0;/* Unknown */
	init->presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
	init->valid = 0;
}

void ast_party_number_copy(struct ast_party_number *dest, const struct ast_party_number *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_free(dest->str);
	dest->str = ast_strdup(src->str);
	dest->plan = src->plan;
	dest->presentation = src->presentation;
	dest->valid = src->valid;
}

void ast_party_number_set_init(struct ast_party_number *init, const struct ast_party_number *guide)
{
	init->str = NULL;
	init->plan = guide->plan;
	init->presentation = guide->presentation;
	init->valid = guide->valid;
}

void ast_party_number_set(struct ast_party_number *dest, const struct ast_party_number *src)
{
	if (dest == src) {
		/* Don't set to self */
		return;
	}

	if (src->str && src->str != dest->str) {
		ast_free(dest->str);
		dest->str = ast_strdup(src->str);
	}

	dest->plan = src->plan;
	dest->presentation = src->presentation;
	dest->valid = src->valid;
}

void ast_party_number_free(struct ast_party_number *doomed)
{
	ast_free(doomed->str);
	doomed->str = NULL;
}

void ast_party_subaddress_init(struct ast_party_subaddress *init)
{
	init->str = NULL;
	init->type = 0;
	init->odd_even_indicator = 0;
	init->valid = 0;
}

void ast_party_subaddress_copy(struct ast_party_subaddress *dest, const struct ast_party_subaddress *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_free(dest->str);
	dest->str = ast_strdup(src->str);
	dest->type = src->type;
	dest->odd_even_indicator = src->odd_even_indicator;
	dest->valid = src->valid;
}

void ast_party_subaddress_set_init(struct ast_party_subaddress *init, const struct ast_party_subaddress *guide)
{
	init->str = NULL;
	init->type = guide->type;
	init->odd_even_indicator = guide->odd_even_indicator;
	init->valid = guide->valid;
}

void ast_party_subaddress_set(struct ast_party_subaddress *dest, const struct ast_party_subaddress *src)
{
	if (dest == src) {
		/* Don't set to self */
		return;
	}

	if (src->str && src->str != dest->str) {
		ast_free(dest->str);
		dest->str = ast_strdup(src->str);
	}

	dest->type = src->type;
	dest->odd_even_indicator = src->odd_even_indicator;
	dest->valid = src->valid;
}

void ast_party_subaddress_free(struct ast_party_subaddress *doomed)
{
	ast_free(doomed->str);
	doomed->str = NULL;
}

void ast_party_id_init(struct ast_party_id *init)
{
	ast_party_name_init(&init->name);
	ast_party_number_init(&init->number);
	ast_party_subaddress_init(&init->subaddress);
	init->tag = NULL;
}

void ast_party_id_copy(struct ast_party_id *dest, const struct ast_party_id *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_party_name_copy(&dest->name, &src->name);
	ast_party_number_copy(&dest->number, &src->number);
	ast_party_subaddress_copy(&dest->subaddress, &src->subaddress);

	ast_free(dest->tag);
	dest->tag = ast_strdup(src->tag);
}

void ast_party_id_set_init(struct ast_party_id *init, const struct ast_party_id *guide)
{
	ast_party_name_set_init(&init->name, &guide->name);
	ast_party_number_set_init(&init->number, &guide->number);
	ast_party_subaddress_set_init(&init->subaddress, &guide->subaddress);
	init->tag = NULL;
}

void ast_party_id_set(struct ast_party_id *dest, const struct ast_party_id *src, const struct ast_set_party_id *update)
{
	if (dest == src) {
		/* Don't set to self */
		return;
	}

	if (!update || update->name) {
		ast_party_name_set(&dest->name, &src->name);
	}
	if (!update || update->number) {
		ast_party_number_set(&dest->number, &src->number);
	}
	if (!update || update->subaddress) {
		ast_party_subaddress_set(&dest->subaddress, &src->subaddress);
	}

	if (src->tag && src->tag != dest->tag) {
		ast_free(dest->tag);
		dest->tag = ast_strdup(src->tag);
	}
}

void ast_party_id_free(struct ast_party_id *doomed)
{
	ast_party_name_free(&doomed->name);
	ast_party_number_free(&doomed->number);
	ast_party_subaddress_free(&doomed->subaddress);

	ast_free(doomed->tag);
	doomed->tag = NULL;
}

int ast_party_id_presentation(const struct ast_party_id *id)
{
	int number_priority;
	int number_value;
	int number_screening;
	int name_priority;
	int name_value;

	/* Determine name presentation priority. */
	if (!id->name.valid) {
		name_value = AST_PRES_UNAVAILABLE;
		name_priority = 3;
	} else {
		name_value = id->name.presentation & AST_PRES_RESTRICTION;
		switch (name_value) {
		case AST_PRES_RESTRICTED:
			name_priority = 0;
			break;
		case AST_PRES_ALLOWED:
			name_priority = 1;
			break;
		case AST_PRES_UNAVAILABLE:
			name_priority = 2;
			break;
		default:
			name_value = AST_PRES_UNAVAILABLE;
			name_priority = 3;
			break;
		}
	}

	/* Determine number presentation priority. */
	if (!id->number.valid) {
		number_screening = AST_PRES_USER_NUMBER_UNSCREENED;
		number_value = AST_PRES_UNAVAILABLE;
		number_priority = 3;
	} else {
		number_screening = id->number.presentation & AST_PRES_NUMBER_TYPE;
		number_value = id->number.presentation & AST_PRES_RESTRICTION;
		switch (number_value) {
		case AST_PRES_RESTRICTED:
			number_priority = 0;
			break;
		case AST_PRES_ALLOWED:
			number_priority = 1;
			break;
		case AST_PRES_UNAVAILABLE:
			number_priority = 2;
			break;
		default:
			number_screening = AST_PRES_USER_NUMBER_UNSCREENED;
			number_value = AST_PRES_UNAVAILABLE;
			number_priority = 3;
			break;
		}
	}

	/* Select the wining presentation value. */
	if (name_priority < number_priority) {
		number_value = name_value;
	}

	return number_value | number_screening;
}

void ast_party_dialed_init(struct ast_party_dialed *init)
{
	init->number.str = NULL;
	init->number.plan = 0;/* Unknown */
	ast_party_subaddress_init(&init->subaddress);
	init->transit_network_select = 0;
}

void ast_party_dialed_copy(struct ast_party_dialed *dest, const struct ast_party_dialed *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_free(dest->number.str);
	dest->number.str = ast_strdup(src->number.str);
	dest->number.plan = src->number.plan;
	ast_party_subaddress_copy(&dest->subaddress, &src->subaddress);
	dest->transit_network_select = src->transit_network_select;
}

void ast_party_dialed_set_init(struct ast_party_dialed *init, const struct ast_party_dialed *guide)
{
	init->number.str = NULL;
	init->number.plan = guide->number.plan;
	ast_party_subaddress_set_init(&init->subaddress, &guide->subaddress);
	init->transit_network_select = guide->transit_network_select;
}

void ast_party_dialed_set(struct ast_party_dialed *dest, const struct ast_party_dialed *src)
{
	if (src->number.str && src->number.str != dest->number.str) {
		ast_free(dest->number.str);
		dest->number.str = ast_strdup(src->number.str);
	}
	dest->number.plan = src->number.plan;

	ast_party_subaddress_set(&dest->subaddress, &src->subaddress);

	dest->transit_network_select = src->transit_network_select;
}

void ast_party_dialed_free(struct ast_party_dialed *doomed)
{
	ast_free(doomed->number.str);
	doomed->number.str = NULL;
	ast_party_subaddress_free(&doomed->subaddress);
}

void ast_party_caller_init(struct ast_party_caller *init)
{
	ast_party_id_init(&init->id);
	ast_party_id_init(&init->ani);
	init->ani2 = 0;
}

void ast_party_caller_copy(struct ast_party_caller *dest, const struct ast_party_caller *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_party_id_copy(&dest->id, &src->id);
	ast_party_id_copy(&dest->ani, &src->ani);
	dest->ani2 = src->ani2;
}

void ast_party_caller_set_init(struct ast_party_caller *init, const struct ast_party_caller *guide)
{
	ast_party_id_set_init(&init->id, &guide->id);
	ast_party_id_set_init(&init->ani, &guide->ani);
	init->ani2 = guide->ani2;
}

void ast_party_caller_set(struct ast_party_caller *dest, const struct ast_party_caller *src, const struct ast_set_party_caller *update)
{
	ast_party_id_set(&dest->id, &src->id, update ? &update->id : NULL);
	ast_party_id_set(&dest->ani, &src->ani, update ? &update->ani : NULL);
	dest->ani2 = src->ani2;
}

void ast_party_caller_free(struct ast_party_caller *doomed)
{
	ast_party_id_free(&doomed->id);
	ast_party_id_free(&doomed->ani);
}

void ast_party_connected_line_init(struct ast_party_connected_line *init)
{
	ast_party_id_init(&init->id);
	ast_party_id_init(&init->ani);
	init->ani2 = 0;
	init->source = AST_CONNECTED_LINE_UPDATE_SOURCE_UNKNOWN;
}

void ast_party_connected_line_copy(struct ast_party_connected_line *dest, const struct ast_party_connected_line *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_party_id_copy(&dest->id, &src->id);
	ast_party_id_copy(&dest->ani, &src->ani);
	dest->ani2 = src->ani2;
	dest->source = src->source;
}

void ast_party_connected_line_set_init(struct ast_party_connected_line *init, const struct ast_party_connected_line *guide)
{
	ast_party_id_set_init(&init->id, &guide->id);
	ast_party_id_set_init(&init->ani, &guide->ani);
	init->ani2 = guide->ani2;
	init->source = guide->source;
}

void ast_party_connected_line_set(struct ast_party_connected_line *dest, const struct ast_party_connected_line *src, const struct ast_set_party_connected_line *update)
{
	ast_party_id_set(&dest->id, &src->id, update ? &update->id : NULL);
	ast_party_id_set(&dest->ani, &src->ani, update ? &update->ani : NULL);
	dest->ani2 = src->ani2;
	dest->source = src->source;
}

void ast_party_connected_line_collect_caller(struct ast_party_connected_line *connected, struct ast_party_caller *caller)
{
	connected->id = caller->id;
	connected->ani = caller->ani;
	connected->ani2 = caller->ani2;
	connected->source = AST_CONNECTED_LINE_UPDATE_SOURCE_UNKNOWN;
}

void ast_party_connected_line_free(struct ast_party_connected_line *doomed)
{
	ast_party_id_free(&doomed->id);
	ast_party_id_free(&doomed->ani);
}

void ast_party_redirecting_init(struct ast_party_redirecting *init)
{
	ast_party_id_init(&init->from);
	ast_party_id_init(&init->to);
	init->count = 0;
	init->reason = AST_REDIRECTING_REASON_UNKNOWN;
}

void ast_party_redirecting_copy(struct ast_party_redirecting *dest, const struct ast_party_redirecting *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_party_id_copy(&dest->from, &src->from);
	ast_party_id_copy(&dest->to, &src->to);
	dest->count = src->count;
	dest->reason = src->reason;
}

void ast_party_redirecting_set_init(struct ast_party_redirecting *init, const struct ast_party_redirecting *guide)
{
	ast_party_id_set_init(&init->from, &guide->from);
	ast_party_id_set_init(&init->to, &guide->to);
	init->count = guide->count;
	init->reason = guide->reason;
}

void ast_party_redirecting_set(struct ast_party_redirecting *dest, const struct ast_party_redirecting *src, const struct ast_set_party_redirecting *update)
{
	ast_party_id_set(&dest->from, &src->from, update ? &update->from : NULL);
	ast_party_id_set(&dest->to, &src->to, update ? &update->to : NULL);
	dest->reason = src->reason;
	dest->count = src->count;
}

void ast_party_redirecting_free(struct ast_party_redirecting *doomed)
{
	ast_party_id_free(&doomed->from);
	ast_party_id_free(&doomed->to);
}

/*! \brief Free a channel structure */
static void ast_channel_destructor(void *obj)
{
	struct ast_channel *chan = obj;
	int fd;
#ifdef HAVE_EPOLL
	int i;
#endif
	struct ast_var_t *vardata;
	struct ast_frame *f;
	struct varshead *headp;
	struct ast_datastore *datastore;
	char device_name[AST_CHANNEL_NAME];

	if (chan->name) {
		/* The string fields were initialized. */
		ast_cel_report_event(chan, AST_CEL_CHANNEL_END, NULL, NULL, NULL);
		ast_cel_check_retire_linkedid(chan);
	}

	/* Get rid of each of the data stores on the channel */
	ast_channel_lock(chan);
	while ((datastore = AST_LIST_REMOVE_HEAD(&chan->datastores, entry)))
		/* Free the data store */
		ast_datastore_free(datastore);
	ast_channel_unlock(chan);

	/* Lock and unlock the channel just to be sure nobody has it locked still
	   due to a reference that was stored in a datastore. (i.e. app_chanspy) */
	ast_channel_lock(chan);
	ast_channel_unlock(chan);

	if (chan->tech_pvt) {
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n", chan->name);
		ast_free(chan->tech_pvt);
	}

	if (chan->sched)
		sched_context_destroy(chan->sched);

	if (chan->name) {
		char *dashptr;

		/* The string fields were initialized. */
		ast_copy_string(device_name, chan->name, sizeof(device_name));
		if ((dashptr = strrchr(device_name, '-'))) {
			*dashptr = '\0';
		}
	} else {
		device_name[0] = '\0';
	}

	/* Stop monitoring */
	if (chan->monitor)
		chan->monitor->stop( chan, 0 );

	/* If there is native format music-on-hold state, free it */
	if (chan->music_state)
		ast_moh_cleanup(chan);

	/* Free translators */
	if (chan->readtrans)
		ast_translator_free_path(chan->readtrans);
	if (chan->writetrans)
		ast_translator_free_path(chan->writetrans);
	if (chan->pbx)
		ast_log(LOG_WARNING, "PBX may not have been terminated properly on '%s'\n", chan->name);

	ast_party_dialed_free(&chan->dialed);
	ast_party_caller_free(&chan->caller);
	ast_party_connected_line_free(&chan->connected);
	ast_party_redirecting_free(&chan->redirecting);

	/* Close pipes if appropriate */
	if ((fd = chan->alertpipe[0]) > -1)
		close(fd);
	if ((fd = chan->alertpipe[1]) > -1)
		close(fd);
	if (chan->timer) {
		ast_timer_close(chan->timer);
	}
#ifdef HAVE_EPOLL
	for (i = 0; i < AST_MAX_FDS; i++) {
		if (chan->epfd_data[i])
			free(chan->epfd_data[i]);
	}
	close(chan->epfd);
#endif
	while ((f = AST_LIST_REMOVE_HEAD(&chan->readq, frame_list)))
		ast_frfree(f);
	
	/* loop over the variables list, freeing all data and deleting list items */
	/* no need to lock the list, as the channel is already locked */
	headp = &chan->varshead;
	while ((vardata = AST_LIST_REMOVE_HEAD(headp, entries)))
		ast_var_delete(vardata);

	ast_app_group_discard(chan);

	/* Destroy the jitterbuffer */
	ast_jb_destroy(chan);

	if (chan->cdr) {
		ast_cdr_discard(chan->cdr);
		chan->cdr = NULL;
	}

	if (chan->zone) {
		chan->zone = ast_tone_zone_unref(chan->zone);
	}

	ast_string_field_free_memory(chan);

	if (device_name[0]) {
		/*
		 * We have a device name to notify of a new state.
		 *
		 * Queue an unknown state, because, while we know that this particular
		 * instance is dead, we don't know the state of all other possible
		 * instances.
		 */
		ast_devstate_changed_literal(AST_DEVICE_UNKNOWN, device_name);
	}
}

/*! \brief Free a dummy channel structure */
static void ast_dummy_channel_destructor(void *obj)
{
	struct ast_channel *chan = obj;
	struct ast_var_t *vardata;
	struct varshead *headp;

	headp = &chan->varshead;

	ast_party_dialed_free(&chan->dialed);
	ast_party_caller_free(&chan->caller);
	ast_party_connected_line_free(&chan->connected);
	ast_party_redirecting_free(&chan->redirecting);

	/* loop over the variables list, freeing all data and deleting list items */
	/* no need to lock the list, as the channel is already locked */
	while ((vardata = AST_LIST_REMOVE_HEAD(headp, entries)))
		ast_var_delete(vardata);

	if (chan->cdr) {
		ast_cdr_discard(chan->cdr);
		chan->cdr = NULL;
	}

	ast_string_field_free_memory(chan);
}

struct ast_datastore *ast_channel_datastore_alloc(const struct ast_datastore_info *info, const char *uid)
{
	return ast_datastore_alloc(info, uid);
}

int ast_channel_datastore_free(struct ast_datastore *datastore)
{
	return ast_datastore_free(datastore);
}

int ast_channel_datastore_inherit(struct ast_channel *from, struct ast_channel *to)
{
	struct ast_datastore *datastore = NULL, *datastore2;

	AST_LIST_TRAVERSE(&from->datastores, datastore, entry) {
		if (datastore->inheritance > 0) {
			datastore2 = ast_datastore_alloc(datastore->info, datastore->uid);
			if (datastore2) {
				datastore2->data = datastore->info->duplicate ? datastore->info->duplicate(datastore->data) : NULL;
				datastore2->inheritance = datastore->inheritance == DATASTORE_INHERIT_FOREVER ? DATASTORE_INHERIT_FOREVER : datastore->inheritance - 1;
				AST_LIST_INSERT_TAIL(&to->datastores, datastore2, entry);
			}
		}
	}
	return 0;
}

int ast_channel_datastore_add(struct ast_channel *chan, struct ast_datastore *datastore)
{
	int res = 0;

	AST_LIST_INSERT_HEAD(&chan->datastores, datastore, entry);

	return res;
}

int ast_channel_datastore_remove(struct ast_channel *chan, struct ast_datastore *datastore)
{
	return AST_LIST_REMOVE(&chan->datastores, datastore, entry) ? 0 : -1;
}

struct ast_datastore *ast_channel_datastore_find(struct ast_channel *chan, const struct ast_datastore_info *info, const char *uid)
{
	struct ast_datastore *datastore = NULL;
	
	if (info == NULL)
		return NULL;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&chan->datastores, datastore, entry) {
		if (datastore->info != info) {
			continue;
		}

		if (uid == NULL) {
			/* matched by type only */
			break;
		}

		if ((datastore->uid != NULL) && !strcasecmp(uid, datastore->uid)) {
			/* Matched by type AND uid */
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return datastore;
}

/*! Set the file descriptor on the channel */
void ast_channel_set_fd(struct ast_channel *chan, int which, int fd)
{
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	struct ast_epoll_data *aed = NULL;

	if (chan->fds[which] > -1) {
		epoll_ctl(chan->epfd, EPOLL_CTL_DEL, chan->fds[which], &ev);
		aed = chan->epfd_data[which];
	}

	/* If this new fd is valid, add it to the epoll */
	if (fd > -1) {
		if (!aed && (!(aed = ast_calloc(1, sizeof(*aed)))))
			return;
		
		chan->epfd_data[which] = aed;
		aed->chan = chan;
		aed->which = which;
		
		ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
		ev.data.ptr = aed;
		epoll_ctl(chan->epfd, EPOLL_CTL_ADD, fd, &ev);
	} else if (aed) {
		/* We don't have to keep around this epoll data structure now */
		free(aed);
		chan->epfd_data[which] = NULL;
	}
#endif
	chan->fds[which] = fd;
	return;
}

/*! Add a channel to an optimized waitfor */
void ast_poll_channel_add(struct ast_channel *chan0, struct ast_channel *chan1)
{
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	int i = 0;

	if (chan0->epfd == -1)
		return;

	/* Iterate through the file descriptors on chan1, adding them to chan0 */
	for (i = 0; i < AST_MAX_FDS; i++) {
		if (chan1->fds[i] == -1)
			continue;
		ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
		ev.data.ptr = chan1->epfd_data[i];
		epoll_ctl(chan0->epfd, EPOLL_CTL_ADD, chan1->fds[i], &ev);
	}

#endif
	return;
}

/*! Delete a channel from an optimized waitfor */
void ast_poll_channel_del(struct ast_channel *chan0, struct ast_channel *chan1)
{
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	int i = 0;

	if (chan0->epfd == -1)
		return;

	for (i = 0; i < AST_MAX_FDS; i++) {
		if (chan1->fds[i] == -1)
			continue;
		epoll_ctl(chan0->epfd, EPOLL_CTL_DEL, chan1->fds[i], &ev);
	}

#endif
	return;
}

/*! \brief Softly hangup a channel, don't lock */
int ast_softhangup_nolock(struct ast_channel *chan, int cause)
{
	ast_debug(1, "Soft-Hanging up channel '%s'\n", chan->name);
	/* Inform channel driver that we need to be hung up, if it cares */
	chan->_softhangup |= cause;
	ast_queue_frame(chan, &ast_null_frame);
	/* Interrupt any poll call or such */
	if (ast_test_flag(chan, AST_FLAG_BLOCKING))
		pthread_kill(chan->blocker, SIGURG);
	return 0;
}

/*! \brief Softly hangup a channel, lock */
int ast_softhangup(struct ast_channel *chan, int cause)
{
	int res;

	ast_channel_lock(chan);
	res = ast_softhangup_nolock(chan, cause);
	ast_channel_unlock(chan);

	return res;
}

static void free_translation(struct ast_channel *clonechan)
{
	if (clonechan->writetrans)
		ast_translator_free_path(clonechan->writetrans);
	if (clonechan->readtrans)
		ast_translator_free_path(clonechan->readtrans);
	clonechan->writetrans = NULL;
	clonechan->readtrans = NULL;
	clonechan->rawwriteformat = clonechan->nativeformats;
	clonechan->rawreadformat = clonechan->nativeformats;
}

void ast_set_hangupsource(struct ast_channel *chan, const char *source, int force)
{
	struct ast_channel *bridge;

	ast_channel_lock(chan);
	if (force || ast_strlen_zero(chan->hangupsource)) {
		ast_string_field_set(chan, hangupsource, source);
	}
	bridge = ast_bridged_channel(chan);
	ast_channel_unlock(chan);

	if (bridge && (force || ast_strlen_zero(bridge->hangupsource))) {
		ast_channel_lock(bridge);
		ast_string_field_set(chan, hangupsource, source);
		ast_channel_unlock(bridge);
	}
}

/*! \brief Hangup a channel */
int ast_hangup(struct ast_channel *chan)
{
	int res = 0;
	char extra_str[64]; /* used for cel logging below */

	/* Don't actually hang up a channel that will masquerade as someone else, or
	   if someone is going to masquerade as us */
	ast_channel_lock(chan);

	if (chan->audiohooks) {
		ast_audiohook_detach_list(chan->audiohooks);
		chan->audiohooks = NULL;
	}

	ast_framehook_list_destroy(chan);

	ast_autoservice_stop(chan);

	if (chan->masq) {
		ast_channel_unlock(chan);
		if (ast_do_masquerade(chan)) {
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
		}
		ast_channel_lock(chan);
	}

	if (chan->masq) {
		ast_log(LOG_WARNING, "%s getting hung up, but someone is trying to masq into us?!?\n", chan->name);
		ast_channel_unlock(chan);
		return 0;
	}
	/* If this channel is one which will be masqueraded into something,
	   mark it as a zombie already, so we know to free it later */
	if (chan->masqr) {
		ast_set_flag(chan, AST_FLAG_ZOMBIE);
		ast_channel_unlock(chan);
		return 0;
	}
	ast_channel_unlock(chan);

	ao2_unlink(channels, chan);

	ast_channel_lock(chan);
	free_translation(chan);
	/* Close audio stream */
	if (chan->stream) {
		ast_closestream(chan->stream);
		chan->stream = NULL;
	}
	/* Close video stream */
	if (chan->vstream) {
		ast_closestream(chan->vstream);
		chan->vstream = NULL;
	}
	if (chan->sched) {
		sched_context_destroy(chan->sched);
		chan->sched = NULL;
	}

	if (chan->generatordata)	/* Clear any tone stuff remaining */
		if (chan->generator && chan->generator->release)
			chan->generator->release(chan, chan->generatordata);
	chan->generatordata = NULL;
	chan->generator = NULL;

	snprintf(extra_str, sizeof(extra_str), "%d,%s,%s", chan->hangupcause, chan->hangupsource, S_OR(pbx_builtin_getvar_helper(chan, "DIALSTATUS"), ""));
	ast_cel_report_event(chan, AST_CEL_HANGUP, NULL, extra_str, NULL);

	if (ast_test_flag(chan, AST_FLAG_BLOCKING)) {
		ast_log(LOG_WARNING, "Hard hangup called by thread %ld on %s, while fd "
					"is blocked by thread %ld in procedure %s!  Expect a failure\n",
					(long)pthread_self(), chan->name, (long)chan->blocker, chan->blockproc);
		ast_assert(ast_test_flag(chan, AST_FLAG_BLOCKING) == 0);
	}
	if (!ast_test_flag(chan, AST_FLAG_ZOMBIE)) {
		ast_debug(1, "Hanging up channel '%s'\n", chan->name);
		if (chan->tech->hangup)
			res = chan->tech->hangup(chan);
	} else {
		ast_debug(1, "Hanging up zombie '%s'\n", chan->name);
	}
			
	ast_channel_unlock(chan);
	ast_cc_offer(chan);
	ast_manager_event(chan, EVENT_FLAG_CALL, "Hangup",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"Cause: %d\r\n"
		"Cause-txt: %s\r\n",
		chan->name,
		chan->uniqueid,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, "<unknown>"),
		S_COR(chan->caller.id.name.valid, chan->caller.id.name.str, "<unknown>"),
		chan->hangupcause,
		ast_cause2str(chan->hangupcause)
		);

	if (chan->cdr && !ast_test_flag(chan->cdr, AST_CDR_FLAG_BRIDGED) && 
		!ast_test_flag(chan->cdr, AST_CDR_FLAG_POST_DISABLED) && 
	    (chan->cdr->disposition != AST_CDR_NULL || ast_test_flag(chan->cdr, AST_CDR_FLAG_DIALED))) {
		ast_channel_lock(chan);
			
		ast_cdr_end(chan->cdr);
		ast_cdr_detach(chan->cdr);
		chan->cdr = NULL;
		ast_channel_unlock(chan);
	}

	chan = ast_channel_release(chan);

	return res;
}

int ast_raw_answer(struct ast_channel *chan, int cdr_answer)
{
	int res = 0;

	ast_channel_lock(chan);

	/* You can't answer an outbound call */
	if (ast_test_flag(chan, AST_FLAG_OUTGOING)) {
		ast_channel_unlock(chan);
		return 0;
	}

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		ast_channel_unlock(chan);
		return -1;
	}

	ast_channel_unlock(chan);

	switch (chan->_state) {
	case AST_STATE_RINGING:
	case AST_STATE_RING:
		ast_channel_lock(chan);
		if (chan->tech->answer) {
			res = chan->tech->answer(chan);
		}
		ast_setstate(chan, AST_STATE_UP);
		if (cdr_answer) {
			ast_cdr_answer(chan->cdr);
		}
		ast_cel_report_event(chan, AST_CEL_ANSWER, NULL, NULL, NULL);
		ast_channel_unlock(chan);
		break;
	case AST_STATE_UP:
		ast_cel_report_event(chan, AST_CEL_ANSWER, NULL, NULL, NULL);
		/* Calling ast_cdr_answer when it it has previously been called
		 * is essentially a no-op, so it is safe.
		 */
		if (cdr_answer) {
			ast_cdr_answer(chan->cdr);
		}
		break;
	default:
		break;
	}

	ast_indicate(chan, -1);
	chan->visible_indication = 0;

	return res;
}

int __ast_answer(struct ast_channel *chan, unsigned int delay, int cdr_answer)
{
	int res = 0;
	enum ast_channel_state old_state;

	old_state = chan->_state;
	if ((res = ast_raw_answer(chan, cdr_answer))) {
		return res;
	}

	switch (old_state) {
	case AST_STATE_RINGING:
	case AST_STATE_RING:
		/* wait for media to start flowing, but don't wait any longer
		 * than 'delay' or 500 milliseconds, whichever is longer
		 */
		do {
			AST_LIST_HEAD_NOLOCK(, ast_frame) frames;
			struct ast_frame *cur, *new;
			int ms = MAX(delay, 500);
			unsigned int done = 0;

			AST_LIST_HEAD_INIT_NOLOCK(&frames);

			for (;;) {
				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_log(LOG_WARNING, "Error condition occurred when polling channel %s for a voice frame: %s\n", chan->name, strerror(errno));
					res = -1;
					break;
				}
				if (ms == 0) {
					ast_debug(2, "Didn't receive a media frame from %s within %d ms of answering. Continuing anyway\n", chan->name, MAX(delay, 500));
					break;
				}
				cur = ast_read(chan);
				if (!cur || ((cur->frametype == AST_FRAME_CONTROL) &&
					     (cur->subclass.integer == AST_CONTROL_HANGUP))) {
					if (cur) {
						ast_frfree(cur);
					}
					res = -1;
					ast_debug(2, "Hangup of channel %s detected in answer routine\n", chan->name);
					break;
				}

				if ((new = ast_frisolate(cur)) != cur) {
					ast_frfree(cur);
				}

				AST_LIST_INSERT_HEAD(&frames, new, frame_list);

				/* if a specific delay period was requested, continue
				 * until that delay has passed. don't stop just because
				 * incoming media has arrived.
				 */
				if (delay) {
					continue;
				}

				switch (new->frametype) {
					/* all of these frametypes qualify as 'media' */
				case AST_FRAME_VOICE:
				case AST_FRAME_VIDEO:
				case AST_FRAME_TEXT:
				case AST_FRAME_DTMF_BEGIN:
				case AST_FRAME_DTMF_END:
				case AST_FRAME_IMAGE:
				case AST_FRAME_HTML:
				case AST_FRAME_MODEM:
					done = 1;
					break;
				case AST_FRAME_CONTROL:
				case AST_FRAME_IAX:
				case AST_FRAME_NULL:
				case AST_FRAME_CNG:
					break;
				}

				if (done) {
					break;
				}
			}

			if (res == 0) {
				ast_channel_lock(chan);
				while ((cur = AST_LIST_REMOVE_HEAD(&frames, frame_list))) {
					ast_queue_frame_head(chan, cur);
					ast_frfree(cur);
				}
				ast_channel_unlock(chan);
			}
		} while (0);
		break;
	default:
		break;
	}

	return res;
}

int ast_answer(struct ast_channel *chan)
{
	return __ast_answer(chan, 0, 1);
}

void ast_deactivate_generator(struct ast_channel *chan)
{
	ast_channel_lock(chan);
	if (chan->generatordata) {
		if (chan->generator && chan->generator->release)
			chan->generator->release(chan, chan->generatordata);
		chan->generatordata = NULL;
		chan->generator = NULL;
		ast_channel_set_fd(chan, AST_GENERATOR_FD, -1);
		ast_clear_flag(chan, AST_FLAG_WRITE_INT);
		ast_settimeout(chan, 0, NULL, NULL);
	}
	ast_channel_unlock(chan);
}

static int generator_force(const void *data)
{
	/* Called if generator doesn't have data */
	void *tmp;
	int res;
	int (*generate)(struct ast_channel *chan, void *tmp, int datalen, int samples) = NULL;
	struct ast_channel *chan = (struct ast_channel *)data;

	ast_channel_lock(chan);
	tmp = chan->generatordata;
	chan->generatordata = NULL;
	if (chan->generator)
		generate = chan->generator->generate;
	ast_channel_unlock(chan);

	if (!tmp || !generate)
		return 0;

	res = generate(chan, tmp, 0, ast_format_rate(chan->writeformat & AST_FORMAT_AUDIO_MASK) / 50);

	chan->generatordata = tmp;

	if (res) {
		ast_debug(1, "Auto-deactivating generator\n");
		ast_deactivate_generator(chan);
	}

	return 0;
}

int ast_activate_generator(struct ast_channel *chan, struct ast_generator *gen, void *params)
{
	int res = 0;

	ast_channel_lock(chan);
	if (chan->generatordata) {
		if (chan->generator && chan->generator->release)
			chan->generator->release(chan, chan->generatordata);
		chan->generatordata = NULL;
	}
	if (gen->alloc && !(chan->generatordata = gen->alloc(chan, params))) {
		res = -1;
	}
	if (!res) {
		ast_settimeout(chan, 50, generator_force, chan);
		chan->generator = gen;
	}
	ast_channel_unlock(chan);

	ast_prod(chan);

	return res;
}

/*! \brief Wait for x amount of time on a file descriptor to have input.  */
int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception)
{
	int winner = -1;
	ast_waitfor_nandfds(NULL, 0, fds, n, exception, &winner, ms);
	return winner;
}

/*! \brief Wait for x amount of time on a file descriptor to have input.  */
#ifdef HAVE_EPOLL
static struct ast_channel *ast_waitfor_nandfds_classic(struct ast_channel **c, int n, int *fds, int nfds,
					int *exception, int *outfd, int *ms)
#else
struct ast_channel *ast_waitfor_nandfds(struct ast_channel **c, int n, int *fds, int nfds,
					int *exception, int *outfd, int *ms)
#endif
{
	struct timeval start = { 0 , 0 };
	struct pollfd *pfds = NULL;
	int res;
	long rms;
	int x, y, max;
	int sz;
	struct timeval now = { 0, 0 };
	struct timeval whentohangup = { 0, 0 }, diff;
	struct ast_channel *winner = NULL;
	struct fdmap {
		int chan;
		int fdno;
	} *fdmap = NULL;

	if ((sz = n * AST_MAX_FDS + nfds)) {
		pfds = alloca(sizeof(*pfds) * sz);
		fdmap = alloca(sizeof(*fdmap) * sz);
	}

	if (outfd)
		*outfd = -99999;
	if (exception)
		*exception = 0;
	
	/* Perform any pending masquerades */
	for (x = 0; x < n; x++) {
		if (c[x]->masq && ast_do_masquerade(c[x])) {
			ast_log(LOG_WARNING, "Masquerade failed\n");
			*ms = -1;
			return NULL;
		}

		ast_channel_lock(c[x]);
		if (!ast_tvzero(c[x]->whentohangup)) {
			if (ast_tvzero(whentohangup))
				now = ast_tvnow();
			diff = ast_tvsub(c[x]->whentohangup, now);
			if (diff.tv_sec < 0 || ast_tvzero(diff)) {
				/* Should already be hungup */
				c[x]->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
				ast_channel_unlock(c[x]);
				return c[x];
			}
			if (ast_tvzero(whentohangup) || ast_tvcmp(diff, whentohangup) < 0)
				whentohangup = diff;
		}
		ast_channel_unlock(c[x]);
	}
	/* Wait full interval */
	rms = *ms;
	/* INT_MAX, not LONG_MAX, because it matters on 64-bit */
	if (!ast_tvzero(whentohangup) && whentohangup.tv_sec < INT_MAX / 1000) {
		rms = whentohangup.tv_sec * 1000 + whentohangup.tv_usec / 1000;              /* timeout in milliseconds */
		if (*ms >= 0 && *ms < rms) {                                                 /* original *ms still smaller */
			rms =  *ms;
		}
	} else if (!ast_tvzero(whentohangup) && rms < 0) {
		/* Tiny corner case... call would need to last >24 days */
		rms = INT_MAX;
	}
	/*
	 * Build the pollfd array, putting the channels' fds first,
	 * followed by individual fds. Order is important because
	 * individual fd's must have priority over channel fds.
	 */
	max = 0;
	for (x = 0; x < n; x++) {
		for (y = 0; y < AST_MAX_FDS; y++) {
			fdmap[max].fdno = y;  /* fd y is linked to this pfds */
			fdmap[max].chan = x;  /* channel x is linked to this pfds */
			max += ast_add_fd(&pfds[max], c[x]->fds[y]);
		}
		CHECK_BLOCKING(c[x]);
	}
	/* Add the individual fds */
	for (x = 0; x < nfds; x++) {
		fdmap[max].chan = -1;
		max += ast_add_fd(&pfds[max], fds[x]);
	}

	if (*ms > 0)
		start = ast_tvnow();
	
	if (sizeof(int) == 4) {	/* XXX fix timeout > 600000 on linux x86-32 */
		do {
			int kbrms = rms;
			if (kbrms > 600000)
				kbrms = 600000;
			res = ast_poll(pfds, max, kbrms);
			if (!res)
				rms -= kbrms;
		} while (!res && (rms > 0));
	} else {
		res = ast_poll(pfds, max, rms);
	}
	for (x = 0; x < n; x++)
		ast_clear_flag(c[x], AST_FLAG_BLOCKING);
	if (res < 0) { /* Simulate a timeout if we were interrupted */
		if (errno != EINTR)
			*ms = -1;
		return NULL;
	}
	if (!ast_tvzero(whentohangup)) {   /* if we have a timeout, check who expired */
		now = ast_tvnow();
		for (x = 0; x < n; x++) {
			if (!ast_tvzero(c[x]->whentohangup) && ast_tvcmp(c[x]->whentohangup, now) <= 0) {
				c[x]->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
				if (winner == NULL)
					winner = c[x];
			}
		}
	}
	if (res == 0) { /* no fd ready, reset timeout and done */
		*ms = 0;	/* XXX use 0 since we may not have an exact timeout. */
		return winner;
	}
	/*
	 * Then check if any channel or fd has a pending event.
	 * Remember to check channels first and fds last, as they
	 * must have priority on setting 'winner'
	 */
	for (x = 0; x < max; x++) {
		res = pfds[x].revents;
		if (res == 0)
			continue;
		if (fdmap[x].chan >= 0) {	/* this is a channel */
			winner = c[fdmap[x].chan];	/* override previous winners */
			if (res & POLLPRI)
				ast_set_flag(winner, AST_FLAG_EXCEPTION);
			else
				ast_clear_flag(winner, AST_FLAG_EXCEPTION);
			winner->fdno = fdmap[x].fdno;
		} else {			/* this is an fd */
			if (outfd)
				*outfd = pfds[x].fd;
			if (exception)
				*exception = (res & POLLPRI) ? -1 : 0;
			winner = NULL;
		}
	}
	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0)
			*ms = 0;
	}
	return winner;
}

#ifdef HAVE_EPOLL
static struct ast_channel *ast_waitfor_nandfds_simple(struct ast_channel *chan, int *ms)
{
	struct timeval start = { 0 , 0 };
	int res = 0;
	struct epoll_event ev[1];
	long diff, rms = *ms;
	struct ast_channel *winner = NULL;
	struct ast_epoll_data *aed = NULL;


	/* See if this channel needs to be masqueraded */
	if (chan->masq && ast_do_masquerade(chan)) {
		ast_log(LOG_WARNING, "Failed to perform masquerade on %s\n", chan->name);
		*ms = -1;
		return NULL;
	}

	ast_channel_lock(chan);
	/* Figure out their timeout */
	if (!ast_tvzero(chan->whentohangup)) {
		if ((diff = ast_tvdiff_ms(chan->whentohangup, ast_tvnow())) < 0) {
			/* They should already be hungup! */
			chan->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
			ast_channel_unlock(chan);
			return NULL;
		}
		/* If this value is smaller then the current one... make it priority */
		if (rms > diff)
			rms = diff;
	}

	ast_channel_unlock(chan);

	/* Time to make this channel block... */
	CHECK_BLOCKING(chan);

	if (*ms > 0)
		start = ast_tvnow();

	/* We don't have to add any file descriptors... they are already added, we just have to wait! */
	res = epoll_wait(chan->epfd, ev, 1, rms);

	/* Stop blocking */
	ast_clear_flag(chan, AST_FLAG_BLOCKING);

	/* Simulate a timeout if we were interrupted */
	if (res < 0) {
		if (errno != EINTR)
			*ms = -1;
		return NULL;
	}

	/* If this channel has a timeout see if it expired */
	if (!ast_tvzero(chan->whentohangup)) {
		if (ast_tvdiff_ms(ast_tvnow(), chan->whentohangup) >= 0) {
			chan->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
			winner = chan;
		}
	}

	/* No fd ready, reset timeout and be done for now */
	if (!res) {
		*ms = 0;
		return winner;
	}

	/* See what events are pending */
	aed = ev[0].data.ptr;
	chan->fdno = aed->which;
	if (ev[0].events & EPOLLPRI)
		ast_set_flag(chan, AST_FLAG_EXCEPTION);
	else
		ast_clear_flag(chan, AST_FLAG_EXCEPTION);

	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0)
			*ms = 0;
	}

	return chan;
}

static struct ast_channel *ast_waitfor_nandfds_complex(struct ast_channel **c, int n, int *ms)
{
	struct timeval start = { 0 , 0 };
	int res = 0, i;
	struct epoll_event ev[25] = { { 0, } };
	struct timeval now = { 0, 0 };
	long whentohangup = 0, diff = 0, rms = *ms;
	struct ast_channel *winner = NULL;

	for (i = 0; i < n; i++) {
		if (c[i]->masq && ast_do_masquerade(c[i])) {
			ast_log(LOG_WARNING, "Masquerade failed\n");
			*ms = -1;
			return NULL;
		}

		ast_channel_lock(c[i]);
		if (!ast_tvzero(c[i]->whentohangup)) {
			if (whentohangup == 0)
				now = ast_tvnow();
			if ((diff = ast_tvdiff_ms(c[i]->whentohangup, now)) < 0) {
				c[i]->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
				ast_channel_unlock(c[i]);
				return c[i];
			}
			if (!whentohangup || whentohangup > diff)
				whentohangup = diff;
		}
		ast_channel_unlock(c[i]);
		CHECK_BLOCKING(c[i]);
	}

	rms = *ms;
	if (whentohangup) {
		rms = whentohangup;
		if (*ms >= 0 && *ms < rms)
			rms = *ms;
	}

	if (*ms > 0)
		start = ast_tvnow();

	res = epoll_wait(c[0]->epfd, ev, 25, rms);

	for (i = 0; i < n; i++)
		ast_clear_flag(c[i], AST_FLAG_BLOCKING);

	if (res < 0) {
		if (errno != EINTR)
			*ms = -1;
		return NULL;
	}

	if (whentohangup) {
		now = ast_tvnow();
		for (i = 0; i < n; i++) {
			if (!ast_tvzero(c[i]->whentohangup) && ast_tvdiff_ms(now, c[i]->whentohangup) >= 0) {
				c[i]->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
				if (!winner)
					winner = c[i];
			}
		}
	}

	if (!res) {
		*ms = 0;
		return winner;
	}

	for (i = 0; i < res; i++) {
		struct ast_epoll_data *aed = ev[i].data.ptr;

		if (!ev[i].events || !aed)
			continue;

		winner = aed->chan;
		if (ev[i].events & EPOLLPRI)
			ast_set_flag(winner, AST_FLAG_EXCEPTION);
		else
			ast_clear_flag(winner, AST_FLAG_EXCEPTION);
		winner->fdno = aed->which;
	}

	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0)
			*ms = 0;
	}

	return winner;
}

struct ast_channel *ast_waitfor_nandfds(struct ast_channel **c, int n, int *fds, int nfds,
					int *exception, int *outfd, int *ms)
{
	/* Clear all provided values in one place. */
	if (outfd)
		*outfd = -99999;
	if (exception)
		*exception = 0;

	/* If no epoll file descriptor is available resort to classic nandfds */
	if (!n || nfds || c[0]->epfd == -1)
		return ast_waitfor_nandfds_classic(c, n, fds, nfds, exception, outfd, ms);
	else if (!nfds && n == 1)
		return ast_waitfor_nandfds_simple(c[0], ms);
	else
		return ast_waitfor_nandfds_complex(c, n, ms);
}
#endif

struct ast_channel *ast_waitfor_n(struct ast_channel **c, int n, int *ms)
{
	return ast_waitfor_nandfds(c, n, NULL, 0, NULL, NULL, ms);
}

int ast_waitfor(struct ast_channel *c, int ms)
{
	int oldms = ms;	/* -1 if no timeout */

	ast_waitfor_nandfds(&c, 1, NULL, 0, NULL, NULL, &ms);
	if ((ms < 0) && (oldms < 0))
		ms = 0;
	return ms;
}

/* XXX never to be called with ms = -1 */
int ast_waitfordigit(struct ast_channel *c, int ms)
{
	return ast_waitfordigit_full(c, ms, -1, -1);
}

int ast_settimeout(struct ast_channel *c, unsigned int rate, int (*func)(const void *data), void *data)
{
	int res;
	unsigned int real_rate = rate, max_rate;

	ast_channel_lock(c);

	if (c->timingfd == -1) {
		ast_channel_unlock(c);
		return -1;
	}

	if (!func) {
		rate = 0;
		data = NULL;
	}

	if (rate && rate > (max_rate = ast_timer_get_max_rate(c->timer))) {
		real_rate = max_rate;
	}

	ast_debug(1, "Scheduling timer at (%u requested / %u actual) timer ticks per second\n", rate, real_rate);

	res = ast_timer_set_rate(c->timer, real_rate);

	c->timingfunc = func;
	c->timingdata = data;

	ast_channel_unlock(c);

	return res;
}

int ast_waitfordigit_full(struct ast_channel *c, int ms, int audiofd, int cmdfd)
{
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c, AST_FLAG_ZOMBIE) || ast_check_hangup(c))
		return -1;

	/* Only look for the end of DTMF, don't bother with the beginning and don't emulate things */
	ast_set_flag(c, AST_FLAG_END_DTMF_ONLY);

	/* Wait for a digit, no more than ms milliseconds total. */
	
	while (ms) {
		struct ast_channel *rchan;
		int outfd=-1;

		errno = 0;
		rchan = ast_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);
		
		if (!rchan && outfd < 0 && ms) {
			if (errno == 0 || errno == EINTR)
				continue;
			ast_log(LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
			ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
			return -1;
		} else if (outfd > -1) {
			/* The FD we were watching has something waiting */
			ast_log(LOG_WARNING, "The FD we were waiting for has something waiting. Waitfordigit returning numeric 1\n");
			ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
			return 1;
		} else if (rchan) {
			int res;
			struct ast_frame *f = ast_read(c);
			if (!f)
				return -1;

			switch (f->frametype) {
			case AST_FRAME_DTMF_BEGIN:
				break;
			case AST_FRAME_DTMF_END:
				res = f->subclass.integer;
				ast_frfree(f);
				ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
				return res;
			case AST_FRAME_CONTROL:
				switch (f->subclass.integer) {
				case AST_CONTROL_HANGUP:
					ast_frfree(f);
					ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
					return -1;
				case AST_CONTROL_RINGING:
				case AST_CONTROL_ANSWER:
				case AST_CONTROL_SRCUPDATE:
				case AST_CONTROL_SRCCHANGE:
				case AST_CONTROL_CONNECTED_LINE:
				case AST_CONTROL_REDIRECTING:
				case -1:
					/* Unimportant */
					break;
				default:
					ast_log(LOG_WARNING, "Unexpected control subclass '%d'\n", f->subclass.integer);
					break;
				}
				break;
			case AST_FRAME_VOICE:
				/* Write audio if appropriate */
				if (audiofd > -1) {
					if (write(audiofd, f->data.ptr, f->datalen) < 0) {
						ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
					}
				}
			default:
				/* Ignore */
				break;
			}
			ast_frfree(f);
		}
	}

	ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);

	return 0; /* Time is up */
}

static void send_dtmf_event(struct ast_channel *chan, const char *direction, const char digit, const char *begin, const char *end)
{
	ast_manager_event(chan, EVENT_FLAG_DTMF,
			"DTMF",
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Digit: %c\r\n"
			"Direction: %s\r\n"
			"Begin: %s\r\n"
			"End: %s\r\n",
			chan->name, chan->uniqueid, digit, direction, begin, end);
}

static void ast_read_generator_actions(struct ast_channel *chan, struct ast_frame *f)
{
	if (chan->generator && chan->generator->generate && chan->generatordata &&  !ast_internal_timing_enabled(chan)) {
		void *tmp = chan->generatordata;
		int (*generate)(struct ast_channel *chan, void *tmp, int datalen, int samples) = chan->generator->generate;
		int res;
		int samples;

		if (chan->timingfunc) {
			ast_debug(1, "Generator got voice, switching to phase locked mode\n");
			ast_settimeout(chan, 0, NULL, NULL);
		}

		chan->generatordata = NULL;     /* reset, to let writes go through */

		if (f->subclass.codec != chan->writeformat) {
			float factor;
			factor = ((float) ast_format_rate(chan->writeformat)) / ((float) ast_format_rate(f->subclass.codec));
			samples = (int) ( ((float) f->samples) * factor );
		} else {
			samples = f->samples;
		}
		
		/* This unlock is here based on two assumptions that hold true at this point in the
		 * code. 1) this function is only called from within __ast_read() and 2) all generators
		 * call ast_write() in their generate callback.
		 *
		 * The reason this is added is so that when ast_write is called, the lock that occurs 
		 * there will not recursively lock the channel. Doing this will cause intended deadlock 
		 * avoidance not to work in deeper functions
		 */
		ast_channel_unlock(chan);
		res = generate(chan, tmp, f->datalen, samples);
		ast_channel_lock(chan);
		chan->generatordata = tmp;
		if (res) {
			ast_debug(1, "Auto-deactivating generator\n");
			ast_deactivate_generator(chan);
		}

	} else if (f->frametype == AST_FRAME_CNG) {
		if (chan->generator && !chan->timingfunc && (chan->timingfd > -1)) {
			ast_debug(1, "Generator got CNG, switching to timed mode\n");
			ast_settimeout(chan, 50, generator_force, chan);
		}
	}
}

static inline void queue_dtmf_readq(struct ast_channel *chan, struct ast_frame *f)
{
	struct ast_frame *fr = &chan->dtmff;

	fr->frametype = AST_FRAME_DTMF_END;
	fr->subclass.integer = f->subclass.integer;
	fr->len = f->len;

	/* The only time this function will be called is for a frame that just came
	 * out of the channel driver.  So, we want to stick it on the tail of the
	 * readq. */

	ast_queue_frame(chan, fr);
}

/*!
 * \brief Determine whether or not we should ignore DTMF in the readq
 */
static inline int should_skip_dtmf(struct ast_channel *chan)
{
	if (ast_test_flag(chan, AST_FLAG_DEFER_DTMF | AST_FLAG_EMULATE_DTMF)) {
		/* We're in the middle of emulating a digit, or DTMF has been
		 * explicitly deferred.  Skip this digit, then. */
		return 1;
	}
			
	if (!ast_tvzero(chan->dtmf_tv) && 
			ast_tvdiff_ms(ast_tvnow(), chan->dtmf_tv) < AST_MIN_DTMF_GAP) {
		/* We're not in the middle of a digit, but it hasn't been long enough
		 * since the last digit, so we'll have to skip DTMF for now. */
		return 1;
	}

	return 0;
}

/*!
 * \brief calculates the number of samples to jump forward with in a monitor stream.
 
 * \note When using ast_seekstream() with the read and write streams of a monitor,
 * the number of samples to seek forward must be of the same sample rate as the stream
 * or else the jump will not be calculated correctly.
 *
 * \retval number of samples to seek forward after rate conversion.
 */
static inline int calc_monitor_jump(int samples, int sample_rate, int seek_rate)
{
	int diff = sample_rate - seek_rate;

	if (diff > 0) {
		samples = samples / (float) (sample_rate / seek_rate);
	} else if (diff < 0) {
		samples = samples * (float) (seek_rate / sample_rate);
	}

	return samples;
}

static struct ast_frame *__ast_read(struct ast_channel *chan, int dropaudio)
{
	struct ast_frame *f = NULL;	/* the return value */
	int blah;
	int prestate;
	int cause = 0;

	/* this function is very long so make sure there is only one return
	 * point at the end (there are only two exceptions to this).
	 */

	if (chan->masq) {
		if (ast_do_masquerade(chan))
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
		else
			f =  &ast_null_frame;
		return f;
	}

	/* if here, no masq has happened, lock the channel and proceed */
	ast_channel_lock(chan);

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		if (chan->generator)
			ast_deactivate_generator(chan);
		goto done;
	}

#ifdef AST_DEVMODE
	/* 
	 * The ast_waitfor() code records which of the channel's file descriptors reported that
	 * data is available.  In theory, ast_read() should only be called after ast_waitfor()
	 * reports that a channel has data available for reading.  However, there still may be
	 * some edge cases throughout the code where ast_read() is called improperly.  This can
	 * potentially cause problems, so if this is a developer build, make a lot of noise if
	 * this happens so that it can be addressed. 
	 */
	if (chan->fdno == -1) {
		ast_log(LOG_ERROR, "ast_read() called with no recorded file descriptor.\n");
	}
#endif

	prestate = chan->_state;

	/* Read and ignore anything on the alertpipe, but read only
	   one sizeof(blah) per frame that we send from it */
	if (chan->alertpipe[0] > -1) {
		int flags = fcntl(chan->alertpipe[0], F_GETFL);
		/* For some odd reason, the alertpipe occasionally loses nonblocking status,
		 * which immediately causes a deadlock scenario.  Detect and prevent this. */
		if ((flags & O_NONBLOCK) == 0) {
			ast_log(LOG_ERROR, "Alertpipe on channel %s lost O_NONBLOCK?!!\n", chan->name);
			if (fcntl(chan->alertpipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
				ast_log(LOG_WARNING, "Unable to set alertpipe nonblocking! (%d: %s)\n", errno, strerror(errno));
				f = &ast_null_frame;
				goto done;
			}
		}
		if (read(chan->alertpipe[0], &blah, sizeof(blah)) < 0) {
			if (errno != EINTR && errno != EAGAIN)
				ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
		}
	}

	if (chan->timingfd > -1 && chan->fdno == AST_TIMING_FD) {
		enum ast_timer_event res;

		ast_clear_flag(chan, AST_FLAG_EXCEPTION);

		res = ast_timer_get_event(chan->timer);

		switch (res) {
		case AST_TIMING_EVENT_EXPIRED:
			ast_timer_ack(chan->timer, 1);

			if (chan->timingfunc) {
				/* save a copy of func/data before unlocking the channel */
				int (*func)(const void *) = chan->timingfunc;
				void *data = chan->timingdata;
				chan->fdno = -1;
				ast_channel_unlock(chan);
				func(data);
			} else {
				ast_timer_set_rate(chan->timer, 0);
				chan->fdno = -1;
				ast_channel_unlock(chan);
			}

			/* cannot 'goto done' because the channel is already unlocked */
			return &ast_null_frame;

		case AST_TIMING_EVENT_CONTINUOUS:
			if (AST_LIST_EMPTY(&chan->readq) || 
				!AST_LIST_NEXT(AST_LIST_FIRST(&chan->readq), frame_list)) {
				ast_timer_disable_continuous(chan->timer);
			}
			break;
		}

	} else if (chan->fds[AST_GENERATOR_FD] > -1 && chan->fdno == AST_GENERATOR_FD) {
		/* if the AST_GENERATOR_FD is set, call the generator with args
		 * set to -1 so it can do whatever it needs to.
		 */
		void *tmp = chan->generatordata;
		chan->generatordata = NULL;     /* reset to let ast_write get through */
		chan->generator->generate(chan, tmp, -1, -1);
		chan->generatordata = tmp;
		f = &ast_null_frame;
		chan->fdno = -1;
		goto done;
	}

	/* Check for pending read queue */
	if (!AST_LIST_EMPTY(&chan->readq)) {
		int skip_dtmf = should_skip_dtmf(chan);

		AST_LIST_TRAVERSE_SAFE_BEGIN(&chan->readq, f, frame_list) {
			/* We have to be picky about which frame we pull off of the readq because
			 * there are cases where we want to leave DTMF frames on the queue until
			 * some later time. */

			if ( (f->frametype == AST_FRAME_DTMF_BEGIN || f->frametype == AST_FRAME_DTMF_END) && skip_dtmf) {
				continue;
			}

			AST_LIST_REMOVE_CURRENT(frame_list);
			break;
		}
		AST_LIST_TRAVERSE_SAFE_END;
		
		if (!f) {
			/* There were no acceptable frames on the readq. */
			f = &ast_null_frame;
			if (chan->alertpipe[0] > -1) {
				int poke = 0;
				/* Restore the state of the alertpipe since we aren't ready for any
				 * of the frames in the readq. */
				if (write(chan->alertpipe[1], &poke, sizeof(poke)) != sizeof(poke)) {
					ast_log(LOG_ERROR, "Failed to write to alertpipe: %s\n", strerror(errno));
				}
			}
		}

		/* Interpret hangup and return NULL */
		/* XXX why not the same for frames from the channel ? */
		if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_HANGUP) {
			cause = f->data.uint32;
			ast_frfree(f);
			f = NULL;
		}
	} else {
		chan->blocker = pthread_self();
		if (ast_test_flag(chan, AST_FLAG_EXCEPTION)) {
			if (chan->tech->exception)
				f = chan->tech->exception(chan);
			else {
				ast_log(LOG_WARNING, "Exception flag set on '%s', but no exception handler\n", chan->name);
				f = &ast_null_frame;
			}
			/* Clear the exception flag */
			ast_clear_flag(chan, AST_FLAG_EXCEPTION);
		} else if (chan->tech->read)
			f = chan->tech->read(chan);
		else
			ast_log(LOG_WARNING, "No read routine on channel %s\n", chan->name);
	}

	/*
	 * Reset the recorded file descriptor that triggered this read so that we can
	 * easily detect when ast_read() is called without properly using ast_waitfor().
	 */
	chan->fdno = -1;

	/* Perform the framehook read event here. After the frame enters the framehook list
	 * there is no telling what will happen, <insert mad scientist laugh here>!!! */
	f = ast_framehook_list_read_event(chan->framehooks, f);

	if (f) {
		struct ast_frame *readq_tail = AST_LIST_LAST(&chan->readq);
		struct ast_control_read_action_payload *read_action_payload;
		struct ast_party_connected_line connected;

		/* if the channel driver returned more than one frame, stuff the excess
		   into the readq for the next ast_read call
		*/
		if (AST_LIST_NEXT(f, frame_list)) {
			ast_queue_frame(chan, AST_LIST_NEXT(f, frame_list));
			ast_frfree(AST_LIST_NEXT(f, frame_list));
			AST_LIST_NEXT(f, frame_list) = NULL;
		}

		switch (f->frametype) {
		case AST_FRAME_CONTROL:
			if (f->subclass.integer == AST_CONTROL_ANSWER) {
				if (!ast_test_flag(chan, AST_FLAG_OUTGOING)) {
					ast_debug(1, "Ignoring answer on an inbound call!\n");
					ast_frfree(f);
					f = &ast_null_frame;
				} else if (prestate == AST_STATE_UP && ast_bridged_channel(chan)) {
					ast_debug(1, "Dropping duplicate answer!\n");
					ast_frfree(f);
					f = &ast_null_frame;
				} else {
					/* Answer the CDR */
					ast_setstate(chan, AST_STATE_UP);
					/* removed a call to ast_cdr_answer(chan->cdr) from here. */
					ast_cel_report_event(chan, AST_CEL_ANSWER, NULL, NULL, NULL);
				}
			} else if (f->subclass.integer == AST_CONTROL_READ_ACTION) {
				read_action_payload = f->data.ptr;
				switch (read_action_payload->action) {
				case AST_FRAME_READ_ACTION_CONNECTED_LINE_MACRO:
					ast_party_connected_line_init(&connected);
					ast_party_connected_line_copy(&connected, &chan->connected);
					if (ast_connected_line_parse_data(read_action_payload->payload,
						read_action_payload->payload_size, &connected)) {
						ast_party_connected_line_free(&connected);
						break;
					}
					if (ast_channel_connected_line_macro(NULL, chan, &connected, 1, 0)) {
						ast_indicate_data(chan, AST_CONTROL_CONNECTED_LINE,
							read_action_payload->payload,
							read_action_payload->payload_size);
					}
					ast_party_connected_line_free(&connected);
					break;
				}
				ast_frfree(f);
				f = &ast_null_frame;
			}
			break;
		case AST_FRAME_DTMF_END:
			send_dtmf_event(chan, "Received", f->subclass.integer, "No", "Yes");
			ast_log(LOG_DTMF, "DTMF end '%c' received on %s, duration %ld ms\n", f->subclass.integer, chan->name, f->len);
			/* Queue it up if DTMF is deferred, or if DTMF emulation is forced. */
			if (ast_test_flag(chan, AST_FLAG_DEFER_DTMF) || ast_test_flag(chan, AST_FLAG_EMULATE_DTMF)) {
				queue_dtmf_readq(chan, f);
				ast_frfree(f);
				f = &ast_null_frame;
			} else if (!ast_test_flag(chan, AST_FLAG_IN_DTMF | AST_FLAG_END_DTMF_ONLY)) {
				if (!ast_tvzero(chan->dtmf_tv) && 
				    ast_tvdiff_ms(ast_tvnow(), chan->dtmf_tv) < AST_MIN_DTMF_GAP) {
					/* If it hasn't been long enough, defer this digit */
					queue_dtmf_readq(chan, f);
					ast_frfree(f);
					f = &ast_null_frame;
				} else {
					/* There was no begin, turn this into a begin and send the end later */
					f->frametype = AST_FRAME_DTMF_BEGIN;
					ast_set_flag(chan, AST_FLAG_EMULATE_DTMF);
					chan->emulate_dtmf_digit = f->subclass.integer;
					chan->dtmf_tv = ast_tvnow();
					if (f->len) {
						if (f->len > AST_MIN_DTMF_DURATION)
							chan->emulate_dtmf_duration = f->len;
						else 
							chan->emulate_dtmf_duration = AST_MIN_DTMF_DURATION;
					} else
						chan->emulate_dtmf_duration = AST_DEFAULT_EMULATE_DTMF_DURATION;
					ast_log(LOG_DTMF, "DTMF begin emulation of '%c' with duration %u queued on %s\n", f->subclass.integer, chan->emulate_dtmf_duration, chan->name);
				}
				if (chan->audiohooks) {
					struct ast_frame *old_frame = f;
					/*!
					 * \todo XXX It is possible to write a digit to the audiohook twice
					 * if the digit was originally read while the channel was in autoservice. */
					f = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_READ, f);
					if (old_frame != f)
						ast_frfree(old_frame);
				}
			} else {
				struct timeval now = ast_tvnow();
				if (ast_test_flag(chan, AST_FLAG_IN_DTMF)) {
					ast_log(LOG_DTMF, "DTMF end accepted with begin '%c' on %s\n", f->subclass.integer, chan->name);
					ast_clear_flag(chan, AST_FLAG_IN_DTMF);
					if (!f->len)
						f->len = ast_tvdiff_ms(now, chan->dtmf_tv);

					/* detect tones that were received on
					 * the wire with durations shorter than
					 * AST_MIN_DTMF_DURATION and set f->len
					 * to the actual duration of the DTMF
					 * frames on the wire.  This will cause
					 * dtmf emulation to be triggered later
					 * on.
					 */
					if (ast_tvdiff_ms(now, chan->dtmf_tv) < AST_MIN_DTMF_DURATION) {
						f->len = ast_tvdiff_ms(now, chan->dtmf_tv);
						ast_log(LOG_DTMF, "DTMF end '%c' detected to have actual duration %ld on the wire, emulation will be triggered on %s\n", f->subclass.integer, f->len, chan->name);
					}
				} else if (!f->len) {
					ast_log(LOG_DTMF, "DTMF end accepted without begin '%c' on %s\n", f->subclass.integer, chan->name);
					f->len = AST_MIN_DTMF_DURATION;
				}
				if (f->len < AST_MIN_DTMF_DURATION && !ast_test_flag(chan, AST_FLAG_END_DTMF_ONLY)) {
					ast_log(LOG_DTMF, "DTMF end '%c' has duration %ld but want minimum %d, emulating on %s\n", f->subclass.integer, f->len, AST_MIN_DTMF_DURATION, chan->name);
					ast_set_flag(chan, AST_FLAG_EMULATE_DTMF);
					chan->emulate_dtmf_digit = f->subclass.integer;
					chan->emulate_dtmf_duration = AST_MIN_DTMF_DURATION - f->len;
					ast_frfree(f);
					f = &ast_null_frame;
				} else {
					ast_log(LOG_DTMF, "DTMF end passthrough '%c' on %s\n", f->subclass.integer, chan->name);
					if (f->len < AST_MIN_DTMF_DURATION) {
						f->len = AST_MIN_DTMF_DURATION;
					}
					chan->dtmf_tv = now;
				}
				if (chan->audiohooks) {
					struct ast_frame *old_frame = f;
					f = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_READ, f);
					if (old_frame != f)
						ast_frfree(old_frame);
				}
			}
			break;
		case AST_FRAME_DTMF_BEGIN:
			send_dtmf_event(chan, "Received", f->subclass.integer, "Yes", "No");
			ast_log(LOG_DTMF, "DTMF begin '%c' received on %s\n", f->subclass.integer, chan->name);
			if ( ast_test_flag(chan, AST_FLAG_DEFER_DTMF | AST_FLAG_END_DTMF_ONLY | AST_FLAG_EMULATE_DTMF) || 
			    (!ast_tvzero(chan->dtmf_tv) && 
			      ast_tvdiff_ms(ast_tvnow(), chan->dtmf_tv) < AST_MIN_DTMF_GAP) ) {
				ast_log(LOG_DTMF, "DTMF begin ignored '%c' on %s\n", f->subclass.integer, chan->name);
				ast_frfree(f);
				f = &ast_null_frame;
			} else {
				ast_set_flag(chan, AST_FLAG_IN_DTMF);
				chan->dtmf_tv = ast_tvnow();
				ast_log(LOG_DTMF, "DTMF begin passthrough '%c' on %s\n", f->subclass.integer, chan->name);
			}
			break;
		case AST_FRAME_NULL:
			/* The EMULATE_DTMF flag must be cleared here as opposed to when the duration
			 * is reached , because we want to make sure we pass at least one
			 * voice frame through before starting the next digit, to ensure a gap
			 * between DTMF digits. */
			if (ast_test_flag(chan, AST_FLAG_EMULATE_DTMF)) {
				struct timeval now = ast_tvnow();
				if (!chan->emulate_dtmf_duration) {
					ast_clear_flag(chan, AST_FLAG_EMULATE_DTMF);
					chan->emulate_dtmf_digit = 0;
				} else if (ast_tvdiff_ms(now, chan->dtmf_tv) >= chan->emulate_dtmf_duration) {
					chan->emulate_dtmf_duration = 0;
					ast_frfree(f);
					f = &chan->dtmff;
					f->frametype = AST_FRAME_DTMF_END;
					f->subclass.integer = chan->emulate_dtmf_digit;
					f->len = ast_tvdiff_ms(now, chan->dtmf_tv);
					chan->dtmf_tv = now;
					ast_clear_flag(chan, AST_FLAG_EMULATE_DTMF);
					chan->emulate_dtmf_digit = 0;
					ast_log(LOG_DTMF, "DTMF end emulation of '%c' queued on %s\n", f->subclass.integer, chan->name);
					if (chan->audiohooks) {
						struct ast_frame *old_frame = f;
						f = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_READ, f);
						if (old_frame != f) {
							ast_frfree(old_frame);
						}
					}
				}
			}
			break;
		case AST_FRAME_VOICE:
			/* The EMULATE_DTMF flag must be cleared here as opposed to when the duration
			 * is reached , because we want to make sure we pass at least one
			 * voice frame through before starting the next digit, to ensure a gap
			 * between DTMF digits. */
			if (ast_test_flag(chan, AST_FLAG_EMULATE_DTMF) && !chan->emulate_dtmf_duration) {
				ast_clear_flag(chan, AST_FLAG_EMULATE_DTMF);
				chan->emulate_dtmf_digit = 0;
			}

			if (dropaudio || ast_test_flag(chan, AST_FLAG_IN_DTMF)) {
				if (dropaudio)
					ast_read_generator_actions(chan, f);
				ast_frfree(f);
				f = &ast_null_frame;
			}

			if (ast_test_flag(chan, AST_FLAG_EMULATE_DTMF) && !ast_test_flag(chan, AST_FLAG_IN_DTMF)) {
				struct timeval now = ast_tvnow();
				if (ast_tvdiff_ms(now, chan->dtmf_tv) >= chan->emulate_dtmf_duration) {
					chan->emulate_dtmf_duration = 0;
					ast_frfree(f);
					f = &chan->dtmff;
					f->frametype = AST_FRAME_DTMF_END;
					f->subclass.integer = chan->emulate_dtmf_digit;
					f->len = ast_tvdiff_ms(now, chan->dtmf_tv);
					chan->dtmf_tv = now;
					if (chan->audiohooks) {
						struct ast_frame *old_frame = f;
						f = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_READ, f);
						if (old_frame != f)
							ast_frfree(old_frame);
					}
					ast_log(LOG_DTMF, "DTMF end emulation of '%c' queued on %s\n", f->subclass.integer, chan->name);
				} else {
					/* Drop voice frames while we're still in the middle of the digit */
					ast_frfree(f);
					f = &ast_null_frame;
				}
			} else if ((f->frametype == AST_FRAME_VOICE) && !(f->subclass.codec & chan->nativeformats)) {
				/* This frame is not one of the current native formats -- drop it on the floor */
				char to[200];
				ast_log(LOG_NOTICE, "Dropping incompatible voice frame on %s of format %s since our native format has changed to %s\n",
					chan->name, ast_getformatname(f->subclass.codec), ast_getformatname_multiple(to, sizeof(to), chan->nativeformats));
				ast_frfree(f);
				f = &ast_null_frame;
			} else if ((f->frametype == AST_FRAME_VOICE)) {
				/* Send frame to audiohooks if present */
				if (chan->audiohooks) {
					struct ast_frame *old_frame = f;
					f = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_READ, f);
					if (old_frame != f)
						ast_frfree(old_frame);
				}
				if (chan->monitor && chan->monitor->read_stream ) {
					/* XXX what does this do ? */
#ifndef MONITOR_CONSTANT_DELAY
					int jump = chan->outsmpl - chan->insmpl - 4 * f->samples;
					if (jump >= 0) {
						jump = calc_monitor_jump((chan->outsmpl - chan->insmpl), ast_format_rate(f->subclass.codec), ast_format_rate(chan->monitor->read_stream->fmt->format));
						if (ast_seekstream(chan->monitor->read_stream, jump, SEEK_FORCECUR) == -1)
							ast_log(LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
						chan->insmpl += (chan->outsmpl - chan->insmpl) + f->samples;
					} else
						chan->insmpl+= f->samples;
#else
					int jump = calc_monitor_jump((chan->outsmpl - chan->insmpl), ast_format_rate(f->subclass.codec), ast_format_rate(chan->monitor->read_stream->fmt->format));
					if (jump - MONITOR_DELAY >= 0) {
						if (ast_seekstream(chan->monitor->read_stream, jump - f->samples, SEEK_FORCECUR) == -1)
							ast_log(LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
						chan->insmpl += chan->outsmpl - chan->insmpl;
					} else
						chan->insmpl += f->samples;
#endif
					if (chan->monitor->state == AST_MONITOR_RUNNING) {
						if (ast_writestream(chan->monitor->read_stream, f) < 0)
							ast_log(LOG_WARNING, "Failed to write data to channel monitor read stream\n");
					}
				}

				if (chan->readtrans && (f = ast_translate(chan->readtrans, f, 1)) == NULL) {
					f = &ast_null_frame;
				}

				/* it is possible for the translation process on chan->readtrans to have
				   produced multiple frames from the single input frame we passed it; if
				   this happens, queue the additional frames *before* the frames we may
				   have queued earlier. if the readq was empty, put them at the head of
				   the queue, and if it was not, put them just after the frame that was
				   at the end of the queue.
				*/
				if (AST_LIST_NEXT(f, frame_list)) {
					if (!readq_tail) {
						ast_queue_frame_head(chan, AST_LIST_NEXT(f, frame_list));
					} else {
						__ast_queue_frame(chan, AST_LIST_NEXT(f, frame_list), 0, readq_tail);
					}
					ast_frfree(AST_LIST_NEXT(f, frame_list));
					AST_LIST_NEXT(f, frame_list) = NULL;
				}

				/* Run generator sitting on the line if timing device not available
				* and synchronous generation of outgoing frames is necessary       */
				ast_read_generator_actions(chan, f);
			}
			break;
		default:
			/* Just pass it on! */
			break;
		}
	} else {
		/* Make sure we always return NULL in the future */
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
		if (cause)
			chan->hangupcause = cause;
		if (chan->generator)
			ast_deactivate_generator(chan);
		/* We no longer End the CDR here */
	}

	/* High bit prints debugging */
	if (chan->fin & DEBUGCHAN_FLAG)
		ast_frame_dump(chan->name, f, "<<");
	chan->fin = FRAMECOUNT_INC(chan->fin);

done:
	if (chan->music_state && chan->generator && chan->generator->digit && f && f->frametype == AST_FRAME_DTMF_END)
		chan->generator->digit(chan, f->subclass.integer);

	if (chan->audiohooks && ast_audiohook_write_list_empty(chan->audiohooks)) {
		/* The list gets recreated if audiohooks are added again later */
		ast_audiohook_detach_list(chan->audiohooks);
		chan->audiohooks = NULL;
	}
	ast_channel_unlock(chan);
	return f;
}

int ast_internal_timing_enabled(struct ast_channel *chan)
{
	return (ast_opt_internal_timing && chan->timingfd > -1);
}

struct ast_frame *ast_read(struct ast_channel *chan)
{
	return __ast_read(chan, 0);
}

struct ast_frame *ast_read_noaudio(struct ast_channel *chan)
{
	return __ast_read(chan, 1);
}

int ast_indicate(struct ast_channel *chan, int condition)
{
	return ast_indicate_data(chan, condition, NULL, 0);
}

static int attribute_const is_visible_indication(enum ast_control_frame_type condition)
{
	/* Don't include a default case here so that we get compiler warnings
	 * when a new type is added. */

	switch (condition) {
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_SRCUPDATE:
	case AST_CONTROL_SRCCHANGE:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_WINK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_OFFHOOK:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_ANSWER:
	case AST_CONTROL_HANGUP:
	case AST_CONTROL_CONNECTED_LINE:
	case AST_CONTROL_REDIRECTING:
	case AST_CONTROL_TRANSFER:
	case AST_CONTROL_T38_PARAMETERS:
	case _XXX_AST_CONTROL_T38:
	case AST_CONTROL_CC:
	case AST_CONTROL_READ_ACTION:
	case AST_CONTROL_AOC:
		break;

	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_BUSY:
	case AST_CONTROL_RINGING:
	case AST_CONTROL_RING:
	case AST_CONTROL_HOLD:
	case AST_CONTROL_UNHOLD:
		return 1;
	}

	return 0;
}

int ast_indicate_data(struct ast_channel *chan, int _condition,
		const void *data, size_t datalen)
{
	/* By using an enum, we'll get compiler warnings for values not handled 
	 * in switch statements. */
	enum ast_control_frame_type condition = _condition;
	struct ast_tone_zone_sound *ts = NULL;
	int res;
	/* this frame is used by framehooks. if it is set, we must free it at the end of this function */
	struct ast_frame *awesome_frame = NULL;

	ast_channel_lock(chan);

	/* Don't bother if the channel is about to go away, anyway. */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		ast_channel_unlock(chan);
		res = -1;
		goto indicate_cleanup;
	}

	if (!ast_framehook_list_is_empty(chan->framehooks)) {
		/* Do framehooks now, do it, go, go now */
		struct ast_frame frame = {
			.frametype = AST_FRAME_CONTROL,
			.subclass.integer = condition,
			.data.ptr = (void *) data, /* this cast from const is only okay because we do the ast_frdup below */
			.datalen = datalen
		};

		/* we have now committed to freeing this frame */
		awesome_frame = ast_frdup(&frame);

		/* who knows what we will get back! the anticipation is killing me. */
		if (!(awesome_frame = ast_framehook_list_read_event(chan->framehooks, &frame))) {
			ast_channel_unlock(chan);
			res = 0;
			goto indicate_cleanup;
		}

		condition = awesome_frame->subclass.integer;
		data = awesome_frame->data.ptr;
		datalen = awesome_frame->datalen;
	}

	switch (condition) {
	case AST_CONTROL_CONNECTED_LINE:
		{
			struct ast_party_connected_line connected;

			ast_party_connected_line_set_init(&connected, &chan->connected);
			res = ast_connected_line_parse_data(data, datalen, &connected);
			if (!res) {
				ast_channel_set_connected_line(chan, &connected, NULL);
			}
			ast_party_connected_line_free(&connected);
		}
		break;

	case AST_CONTROL_REDIRECTING:
		{
			struct ast_party_redirecting redirecting;

			ast_party_redirecting_set_init(&redirecting, &chan->redirecting);
			res = ast_redirecting_parse_data(data, datalen, &redirecting);
			if (!res) {
				ast_channel_set_redirecting(chan, &redirecting, NULL);
			}
			ast_party_redirecting_free(&redirecting);
		}
		break;
	
	default:
		break;
	}

	if (chan->tech->indicate) {
		/* See if the channel driver can handle this condition. */
		res = chan->tech->indicate(chan, condition, data, datalen);
	} else {
		res = -1;
	}

	ast_channel_unlock(chan);

	if (!res) {
		/* The channel driver successfully handled this indication */
		if (is_visible_indication(condition)) {
			chan->visible_indication = condition;
		}
		res = 0;
		goto indicate_cleanup;
	}

	/* The channel driver does not support this indication, let's fake
	 * it by doing our own tone generation if applicable. */

	/*!\note If we compare the enumeration type, which does not have any
	 * negative constants, the compiler may optimize this code away.
	 * Therefore, we must perform an integer comparison here. */
	if (_condition < 0) {
		/* Stop any tones that are playing */
		ast_playtones_stop(chan);
		res = 0;
		goto indicate_cleanup;
	}

	/* Handle conditions that we have tones for. */
	switch (condition) {
	case _XXX_AST_CONTROL_T38:
		/* deprecated T.38 control frame */
		res = -1;
		goto indicate_cleanup;
	case AST_CONTROL_T38_PARAMETERS:
		/* there is no way to provide 'default' behavior for these
		 * control frames, so we need to return failure, but there
		 * is also no value in the log message below being emitted
		 * since failure to handle these frames is not an 'error'
		 * so just return right now. in addition, we want to return
		 * whatever value the channel driver returned, in case it
		 * has some meaning.*/
		goto indicate_cleanup;
	case AST_CONTROL_RINGING:
		ts = ast_get_indication_tone(chan->zone, "ring");
		/* It is common practice for channel drivers to return -1 if trying
		 * to indicate ringing on a channel which is up. The idea is to let the
		 * core generate the ringing inband. However, we don't want the
		 * warning message about not being able to handle the specific indication
		 * to print nor do we want ast_indicate_data to return an "error" for this
		 * condition
		 */
		if (chan->_state == AST_STATE_UP) {
			res = 0;
		}
		break;
	case AST_CONTROL_BUSY:
		ts = ast_get_indication_tone(chan->zone, "busy");
		break;
	case AST_CONTROL_CONGESTION:
		ts = ast_get_indication_tone(chan->zone, "congestion");
		break;
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_SRCUPDATE:
	case AST_CONTROL_SRCCHANGE:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_WINK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_OFFHOOK:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_ANSWER:
	case AST_CONTROL_HANGUP:
	case AST_CONTROL_RING:
	case AST_CONTROL_HOLD:
	case AST_CONTROL_UNHOLD:
	case AST_CONTROL_TRANSFER:
	case AST_CONTROL_CONNECTED_LINE:
	case AST_CONTROL_REDIRECTING:
	case AST_CONTROL_CC:
	case AST_CONTROL_READ_ACTION:
	case AST_CONTROL_AOC:
		/* Nothing left to do for these. */
		res = 0;
		break;
	}

	if (ts) {
		/* We have a tone to play, yay. */
		ast_debug(1, "Driver for channel '%s' does not support indication %d, emulating it\n", chan->name, condition);
		ast_playtones_start(chan, 0, ts->data, 1);
		ts = ast_tone_zone_sound_unref(ts);
		res = 0;
		chan->visible_indication = condition;
	}

	if (res) {
		/* not handled */
		ast_log(LOG_WARNING, "Unable to handle indication %d for '%s'\n", condition, chan->name);
	}

indicate_cleanup:
	if (awesome_frame) {
		ast_frfree(awesome_frame);
	}

	return res;
}

int ast_recvchar(struct ast_channel *chan, int timeout)
{
	int c;
	char *buf = ast_recvtext(chan, timeout);
	if (buf == NULL)
		return -1;	/* error or timeout */
	c = *(unsigned char *)buf;
	ast_free(buf);
	return c;
}

char *ast_recvtext(struct ast_channel *chan, int timeout)
{
	int res, done = 0;
	char *buf = NULL;
	
	while (!done) {
		struct ast_frame *f;
		if (ast_check_hangup(chan))
			break;
		res = ast_waitfor(chan, timeout);
		if (res <= 0) /* timeout or error */
			break;
		timeout = res;	/* update timeout */
		f = ast_read(chan);
		if (f == NULL)
			break; /* no frame */
		if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_HANGUP)
			done = 1;	/* force a break */
		else if (f->frametype == AST_FRAME_TEXT) {		/* what we want */
			buf = ast_strndup((char *) f->data.ptr, f->datalen);	/* dup and break */
			done = 1;
		}
		ast_frfree(f);
	}
	return buf;
}

int ast_sendtext(struct ast_channel *chan, const char *text)
{
	int res = 0;
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan))
		return -1;
	CHECK_BLOCKING(chan);
	if (chan->tech->send_text)
		res = chan->tech->send_text(chan, text);
	ast_clear_flag(chan, AST_FLAG_BLOCKING);
	return res;
}

int ast_senddigit_begin(struct ast_channel *chan, char digit)
{
	/* Device does not support DTMF tones, lets fake
	 * it by doing our own generation. */
	static const char * const dtmf_tones[] = {
		"941+1336", /* 0 */
		"697+1209", /* 1 */
		"697+1336", /* 2 */
		"697+1477", /* 3 */
		"770+1209", /* 4 */
		"770+1336", /* 5 */
		"770+1477", /* 6 */
		"852+1209", /* 7 */
		"852+1336", /* 8 */
		"852+1477", /* 9 */
		"697+1633", /* A */
		"770+1633", /* B */
		"852+1633", /* C */
		"941+1633", /* D */
		"941+1209", /* * */
		"941+1477"  /* # */
	};

	if (!chan->tech->send_digit_begin)
		return 0;

	if (!chan->tech->send_digit_begin(chan, digit))
		return 0;

	if (digit >= '0' && digit <='9')
		ast_playtones_start(chan, 0, dtmf_tones[digit-'0'], 0);
	else if (digit >= 'A' && digit <= 'D')
		ast_playtones_start(chan, 0, dtmf_tones[digit-'A'+10], 0);
	else if (digit == '*')
		ast_playtones_start(chan, 0, dtmf_tones[14], 0);
	else if (digit == '#')
		ast_playtones_start(chan, 0, dtmf_tones[15], 0);
	else {
		/* not handled */
		ast_debug(1, "Unable to generate DTMF tone '%c' for '%s'\n", digit, chan->name);
	}

	return 0;
}

int ast_senddigit_end(struct ast_channel *chan, char digit, unsigned int duration)
{
	int res = -1;

	if (chan->tech->send_digit_end)
		res = chan->tech->send_digit_end(chan, digit, duration);

	if (res && chan->generator)
		ast_playtones_stop(chan);
	
	return 0;
}

int ast_senddigit(struct ast_channel *chan, char digit, unsigned int duration)
{
	if (chan->tech->send_digit_begin) {
		ast_senddigit_begin(chan, digit);
		ast_safe_sleep(chan, (duration >= AST_DEFAULT_EMULATE_DTMF_DURATION ? duration : AST_DEFAULT_EMULATE_DTMF_DURATION));
	}
	
	return ast_senddigit_end(chan, digit, (duration >= AST_DEFAULT_EMULATE_DTMF_DURATION ? duration : AST_DEFAULT_EMULATE_DTMF_DURATION));
}

int ast_prod(struct ast_channel *chan)
{
	struct ast_frame a = { AST_FRAME_VOICE };
	char nothing[128];

	/* Send an empty audio frame to get things moving */
	if (chan->_state != AST_STATE_UP) {
		ast_debug(1, "Prodding channel '%s'\n", chan->name);
		a.subclass.codec = chan->rawwriteformat;
		a.data.ptr = nothing + AST_FRIENDLY_OFFSET;
		a.src = "ast_prod"; /* this better match check in ast_write */
		if (ast_write(chan, &a))
			ast_log(LOG_WARNING, "Prodding channel '%s' failed\n", chan->name);
	}
	return 0;
}

int ast_write_video(struct ast_channel *chan, struct ast_frame *fr)
{
	int res;
	if (!chan->tech->write_video)
		return 0;
	res = ast_write(chan, fr);
	if (!res)
		res = 1;
	return res;
}

struct plc_ds {
	/* A buffer in which to store SLIN PLC
	 * samples generated by the generic PLC
	 * functionality in plc.c
	 */
	int16_t *samples_buf;
	/* The current number of samples in the
	 * samples_buf
	 */
	size_t num_samples;
	plc_state_t plc_state;
};

static void plc_ds_destroy(void *data)
{
	struct plc_ds *plc = data;
	ast_free(plc->samples_buf);
	ast_free(plc);
}

static struct ast_datastore_info plc_ds_info = {
	.type = "plc",
	.destroy = plc_ds_destroy,
};

static void adjust_frame_for_plc(struct ast_channel *chan, struct ast_frame *frame, struct ast_datastore *datastore)
{
	int num_new_samples = frame->samples;
	struct plc_ds *plc = datastore->data;

	/* As a general note, let me explain the somewhat odd calculations used when taking
	 * the frame offset into account here. According to documentation in frame.h, the frame's
	 * offset field indicates the number of bytes that the audio is offset. The plc->samples_buf
	 * is not an array of bytes, but rather an array of 16-bit integers since it holds SLIN
	 * samples. So I had two choices to make here with the offset.
	 * 
	 * 1. Make the offset AST_FRIENDLY_OFFSET bytes. The main downside for this is that
	 *    I can't just add AST_FRIENDLY_OFFSET to the plc->samples_buf and have the pointer
	 *    arithmetic come out right. I would have to do some odd casting or division for this to
	 *    work as I wanted.
	 * 2. Make the offset AST_FRIENDLY_OFFSET * 2 bytes. This allows the pointer arithmetic
	 *    to work out better with the plc->samples_buf. The downside here is that the buffer's
	 *    allocation contains an extra 64 bytes of unused space.
	 * 
	 * I decided to go with option 2. This is why in the calloc statement and the statement that
	 * sets the frame's offset, AST_FRIENDLY_OFFSET is multiplied by 2.
	 */

	/* If this audio frame has no samples to fill in, ignore it */
	if (!num_new_samples) {
		return;
	}

	/* First, we need to be sure that our buffer is large enough to accomodate
	 * the samples we need to fill in. This will likely only occur on the first
	 * frame we write.
	 */
	if (plc->num_samples < num_new_samples) {
		ast_free(plc->samples_buf);
		plc->samples_buf = ast_calloc(1, (num_new_samples * sizeof(*plc->samples_buf)) + (AST_FRIENDLY_OFFSET * 2));
		if (!plc->samples_buf) {
			ast_channel_datastore_remove(chan, datastore);
			ast_datastore_free(datastore);
			return;
		}
		plc->num_samples = num_new_samples;
	}

	if (frame->datalen == 0) {
		plc_fillin(&plc->plc_state, plc->samples_buf + AST_FRIENDLY_OFFSET, frame->samples);
		frame->data.ptr = plc->samples_buf + AST_FRIENDLY_OFFSET;
		frame->datalen = num_new_samples * 2;
		frame->offset = AST_FRIENDLY_OFFSET * 2;
	} else {
		plc_rx(&plc->plc_state, frame->data.ptr, frame->samples);
	}
}

static void apply_plc(struct ast_channel *chan, struct ast_frame *frame)
{
	struct ast_datastore *datastore;
	struct plc_ds *plc;

	datastore = ast_channel_datastore_find(chan, &plc_ds_info, NULL);
	if (datastore) {
		plc = datastore->data;
		adjust_frame_for_plc(chan, frame, datastore);
		return;
	}

	datastore = ast_datastore_alloc(&plc_ds_info, NULL);
	if (!datastore) {
		return;
	}
	plc = ast_calloc(1, sizeof(*plc));
	if (!plc) {
		ast_datastore_free(datastore);
		return;
	}
	datastore->data = plc;
	ast_channel_datastore_add(chan, datastore);
	adjust_frame_for_plc(chan, frame, datastore);
}

int ast_write(struct ast_channel *chan, struct ast_frame *fr)
{
	int res = -1;
	struct ast_frame *f = NULL;
	int count = 0;

	/*Deadlock avoidance*/
	while(ast_channel_trylock(chan)) {
		/*cannot goto done since the channel is not locked*/
		if(count++ > 10) {
			ast_debug(1, "Deadlock avoided for write to channel '%s'\n", chan->name);
			return 0;
		}
		usleep(1);
	}
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan))
		goto done;

	/* Handle any pending masquerades */
	if (chan->masq) {
		ast_channel_unlock(chan);
		if (ast_do_masquerade(chan)) {
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
			return res; /* no need to goto done: chan is already unlocked for masq */
		}
		ast_channel_lock(chan);
	}
	if (chan->masqr) {
		res = 0;	/* XXX explain, why 0 ? */
		goto done;
	}

	/* Perform the framehook write event here. After the frame enters the framehook list
	 * there is no telling what will happen, how awesome is that!!! */
	if (!(fr = ast_framehook_list_write_event(chan->framehooks, fr))) {
		res = 0;
		goto done;
	}

	if (chan->generatordata && (!fr->src || strcasecmp(fr->src, "ast_prod"))) {
		if (ast_test_flag(chan, AST_FLAG_WRITE_INT)) {
				ast_deactivate_generator(chan);
		} else {
			if (fr->frametype == AST_FRAME_DTMF_END) {
				/* There is a generator running while we're in the middle of a digit.
				 * It's probably inband DTMF, so go ahead and pass it so it can
				 * stop the generator */
				ast_clear_flag(chan, AST_FLAG_BLOCKING);
				ast_channel_unlock(chan);
				res = ast_senddigit_end(chan, fr->subclass.integer, fr->len);
				ast_channel_lock(chan);
				CHECK_BLOCKING(chan);
			} else if (fr->frametype == AST_FRAME_CONTROL && fr->subclass.integer == AST_CONTROL_UNHOLD) {
				/* This is a side case where Echo is basically being called and the person put themselves on hold and took themselves off hold */
				res = (chan->tech->indicate == NULL) ? 0 :
					chan->tech->indicate(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
			}
			res = 0;	/* XXX explain, why 0 ? */
			goto done;
		}
	}
	/* High bit prints debugging */
	if (chan->fout & DEBUGCHAN_FLAG)
		ast_frame_dump(chan->name, fr, ">>");
	CHECK_BLOCKING(chan);
	switch (fr->frametype) {
	case AST_FRAME_CONTROL:
		res = (chan->tech->indicate == NULL) ? 0 :
			chan->tech->indicate(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		break;
	case AST_FRAME_DTMF_BEGIN:
		if (chan->audiohooks) {
			struct ast_frame *old_frame = fr;
			fr = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_WRITE, fr);
			if (old_frame != fr)
				f = fr;
		}
		send_dtmf_event(chan, "Sent", fr->subclass.integer, "Yes", "No");
		ast_clear_flag(chan, AST_FLAG_BLOCKING);
		ast_channel_unlock(chan);
		res = ast_senddigit_begin(chan, fr->subclass.integer);
		ast_channel_lock(chan);
		CHECK_BLOCKING(chan);
		break;
	case AST_FRAME_DTMF_END:
		if (chan->audiohooks) {
			struct ast_frame *new_frame = fr;

			new_frame = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_WRITE, fr);
			if (new_frame != fr) {
				ast_frfree(new_frame);
			}
		}
		send_dtmf_event(chan, "Sent", fr->subclass.integer, "No", "Yes");
		ast_clear_flag(chan, AST_FLAG_BLOCKING);
		ast_channel_unlock(chan);
		res = ast_senddigit_end(chan, fr->subclass.integer, fr->len);
		ast_channel_lock(chan);
		CHECK_BLOCKING(chan);
		break;
	case AST_FRAME_TEXT:
		if (fr->subclass.integer == AST_FORMAT_T140) {
			res = (chan->tech->write_text == NULL) ? 0 :
				chan->tech->write_text(chan, fr);
		} else {
			res = (chan->tech->send_text == NULL) ? 0 :
				chan->tech->send_text(chan, (char *) fr->data.ptr);
		}
		break;
	case AST_FRAME_HTML:
		res = (chan->tech->send_html == NULL) ? 0 :
			chan->tech->send_html(chan, fr->subclass.integer, (char *) fr->data.ptr, fr->datalen);
		break;
	case AST_FRAME_VIDEO:
		/* XXX Handle translation of video codecs one day XXX */
		res = (chan->tech->write_video == NULL) ? 0 :
			chan->tech->write_video(chan, fr);
		break;
	case AST_FRAME_MODEM:
		res = (chan->tech->write == NULL) ? 0 :
			chan->tech->write(chan, fr);
		break;
	case AST_FRAME_VOICE:
		if (chan->tech->write == NULL)
			break;	/*! \todo XXX should return 0 maybe ? */

		if (ast_opt_generic_plc && fr->subclass.codec == AST_FORMAT_SLINEAR) {
			apply_plc(chan, fr);
		}

		/* If the frame is in the raw write format, then it's easy... just use the frame - otherwise we will have to translate */
		if (fr->subclass.codec == chan->rawwriteformat)
			f = fr;
		else
			f = (chan->writetrans) ? ast_translate(chan->writetrans, fr, 0) : fr;

		if (!f) {
			res = 0;
			break;
		}

		if (chan->audiohooks) {
			struct ast_frame *prev = NULL, *new_frame, *cur, *dup;
			int freeoldlist = 0;

			if (f != fr) {
				freeoldlist = 1;
			}

			/* Since ast_audiohook_write may return a new frame, and the cur frame is
			 * an item in a list of frames, create a new list adding each cur frame back to it
			 * regardless if the cur frame changes or not. */
			for (cur = f; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
				new_frame = ast_audiohook_write_list(chan, chan->audiohooks, AST_AUDIOHOOK_DIRECTION_WRITE, cur);

				/* if this frame is different than cur, preserve the end of the list,
				 * free the old frames, and set cur to be the new frame */
				if (new_frame != cur) {

					/* doing an ast_frisolate here seems silly, but we are not guaranteed the new_frame
					 * isn't part of local storage, meaning if ast_audiohook_write is called multiple
					 * times it may override the previous frame we got from it unless we dup it */
					if ((dup = ast_frisolate(new_frame))) {
						AST_LIST_NEXT(dup, frame_list) = AST_LIST_NEXT(cur, frame_list);
						if (freeoldlist) {
							AST_LIST_NEXT(cur, frame_list) = NULL;
							ast_frfree(cur);
						}
						if (new_frame != dup) {
							ast_frfree(new_frame);
						}
						cur = dup;
					}
				}

				/* now, regardless if cur is new or not, add it to the new list,
				 * if the new list has not started, cur will become the first item. */
				if (prev) {
					AST_LIST_NEXT(prev, frame_list) = cur;
				} else {
					f = cur; /* set f to be the beginning of our new list */
				}
				prev = cur;
			}
		}
		
		/* If Monitor is running on this channel, then we have to write frames out there too */
		/* the translator on chan->writetrans may have returned multiple frames
		   from the single frame we passed in; if so, feed each one of them to the
		   monitor */
		if (chan->monitor && chan->monitor->write_stream) {
			struct ast_frame *cur;

			for (cur = f; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
			/* XXX must explain this code */
#ifndef MONITOR_CONSTANT_DELAY
				int jump = chan->insmpl - chan->outsmpl - 4 * cur->samples;
				if (jump >= 0) {
					jump = calc_monitor_jump((chan->insmpl - chan->outsmpl), ast_format_rate(f->subclass.codec), ast_format_rate(chan->monitor->read_stream->fmt->format));
					if (ast_seekstream(chan->monitor->write_stream, jump, SEEK_FORCECUR) == -1)
						ast_log(LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
					chan->outsmpl += (chan->insmpl - chan->outsmpl) + cur->samples;
				} else {
					chan->outsmpl += cur->samples;
				}
#else
				int jump = calc_monitor_jump((chan->insmpl - chan->outsmpl), ast_format_rate(f->subclass.codec), ast_format_rate(chan->monitor->read_stream->fmt->format));
				if (jump - MONITOR_DELAY >= 0) {
					if (ast_seekstream(chan->monitor->write_stream, jump - cur->samples, SEEK_FORCECUR) == -1)
						ast_log(LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
					chan->outsmpl += chan->insmpl - chan->outsmpl;
				} else {
					chan->outsmpl += cur->samples;
				}
#endif
				if (chan->monitor->state == AST_MONITOR_RUNNING) {
					if (ast_writestream(chan->monitor->write_stream, cur) < 0)
						ast_log(LOG_WARNING, "Failed to write data to channel monitor write stream\n");
				}
			}
		}

		/* the translator on chan->writetrans may have returned multiple frames
		   from the single frame we passed in; if so, feed each one of them to the
		   channel, freeing each one after it has been written */
		if ((f != fr) && AST_LIST_NEXT(f, frame_list)) {
			struct ast_frame *cur, *next;
			unsigned int skip = 0;

			for (cur = f, next = AST_LIST_NEXT(cur, frame_list);
			     cur;
			     cur = next, next = cur ? AST_LIST_NEXT(cur, frame_list) : NULL) {
				if (!skip) {
					if ((res = chan->tech->write(chan, cur)) < 0) {
						chan->_softhangup |= AST_SOFTHANGUP_DEV;
						skip = 1;
					} else if (next) {
						/* don't do this for the last frame in the list,
						   as the code outside the loop will do it once
						*/
						chan->fout = FRAMECOUNT_INC(chan->fout);
					}
				}
				ast_frfree(cur);
			}

			/* reset f so the code below doesn't attempt to free it */
			f = NULL;
		} else {
			res = chan->tech->write(chan, f);
		}
		break;
	case AST_FRAME_NULL:
	case AST_FRAME_IAX:
		/* Ignore these */
		res = 0;
		break;
	default:
		/* At this point, fr is the incoming frame and f is NULL.  Channels do
		 * not expect to get NULL as a frame pointer and will segfault.  Hence,
		 * we output the original frame passed in. */
		res = chan->tech->write(chan, fr);
		break;
	}

	if (f && f != fr)
		ast_frfree(f);
	ast_clear_flag(chan, AST_FLAG_BLOCKING);

	/* Consider a write failure to force a soft hangup */
	if (res < 0) {
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
	} else {
		chan->fout = FRAMECOUNT_INC(chan->fout);
	}
done:
	if (chan->audiohooks && ast_audiohook_write_list_empty(chan->audiohooks)) {
		/* The list gets recreated if audiohooks are added again later */
		ast_audiohook_detach_list(chan->audiohooks);
		chan->audiohooks = NULL;
	}
	ast_channel_unlock(chan);
	return res;
}

static int set_format(struct ast_channel *chan, format_t fmt, format_t *rawformat, format_t *format,
		      struct ast_trans_pvt **trans, const int direction)
{
	format_t native, native_fmt = ast_best_codec(fmt);
	int res;
	char from[200], to[200];
	
	/* Make sure we only consider audio */
	fmt &= AST_FORMAT_AUDIO_MASK;
	
	native = chan->nativeformats;

	if (!fmt || !native)	/* No audio requested */
		return 0;	/* Let's try a call without any sounds (video, text) */

	/* See if the underlying channel driver is capable of performing transcoding for us */
	if (!ast_channel_setoption(chan, direction ? AST_OPTION_FORMAT_WRITE : AST_OPTION_FORMAT_READ, &native_fmt, sizeof(int*), 0)) {
		ast_debug(1, "Channel driver natively set channel %s to %s format %s\n", chan->name,
			  direction ? "write" : "read", ast_getformatname(native_fmt));
		chan->nativeformats = *rawformat = *format = native_fmt;
		if (*trans) {
			ast_translator_free_path(*trans);
		}
		*trans = NULL;
		return 0;
	}

	/* Find a translation path from the native format to one of the desired formats */
	if (!direction)
		/* reading */
		res = ast_translator_best_choice(&fmt, &native);
	else
		/* writing */
		res = ast_translator_best_choice(&native, &fmt);

	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to find a codec translation path from %s to %s\n",
			ast_getformatname_multiple(from, sizeof(from), native),
			ast_getformatname_multiple(to, sizeof(to), fmt));
		return -1;
	}
	
	/* Now we have a good choice for both. */
	ast_channel_lock(chan);

	if ((*rawformat == native) && (*format == fmt) && ((*rawformat == *format) || (*trans))) {
		/* the channel is already in these formats, so nothing to do */
		ast_channel_unlock(chan);
		return 0;
	}

	*rawformat = native;
	/* User perspective is fmt */
	*format = fmt;
	/* Free any read translation we have right now */
	if (*trans)
		ast_translator_free_path(*trans);
	/* Build a translation path from the raw format to the desired format */
	if (!direction)
		/* reading */
		*trans = ast_translator_build_path(*format, *rawformat);
	else
		/* writing */
		*trans = ast_translator_build_path(*rawformat, *format);
	ast_channel_unlock(chan);
	ast_debug(1, "Set channel %s to %s format %s\n", chan->name,
		direction ? "write" : "read", ast_getformatname(fmt));
	return 0;
}

int ast_set_read_format(struct ast_channel *chan, format_t fmt)
{
	return set_format(chan, fmt, &chan->rawreadformat, &chan->readformat,
			  &chan->readtrans, 0);
}

int ast_set_write_format(struct ast_channel *chan, format_t fmt)
{
	return set_format(chan, fmt, &chan->rawwriteformat, &chan->writeformat,
			  &chan->writetrans, 1);
}

const char *ast_channel_reason2str(int reason)
{
	switch (reason) /* the following appear to be the only ones actually returned by request_and_dial */
	{
	case 0:
		return "Call Failure (not BUSY, and not NO_ANSWER, maybe Circuit busy or down?)";
	case AST_CONTROL_HANGUP:
		return "Hangup";
	case AST_CONTROL_RING:
		return "Local Ring";
	case AST_CONTROL_RINGING:
		return "Remote end Ringing";
	case AST_CONTROL_ANSWER:
		return "Remote end has Answered";
	case AST_CONTROL_BUSY:
		return "Remote end is Busy";
	case AST_CONTROL_CONGESTION:
		return "Congestion (circuits busy)";
	default:
		return "Unknown Reason!!";
	}
}

static void handle_cause(int cause, int *outstate)
{
	if (outstate) {
		/* compute error and return */
		if (cause == AST_CAUSE_BUSY)
			*outstate = AST_CONTROL_BUSY;
		else if (cause == AST_CAUSE_CONGESTION)
			*outstate = AST_CONTROL_CONGESTION;
		else
			*outstate = 0;
	}
}

struct ast_channel *ast_call_forward(struct ast_channel *caller, struct ast_channel *orig, int *timeout, format_t format, struct outgoing_helper *oh, int *outstate)
{
	char tmpchan[256];
	struct ast_channel *new = NULL;
	struct ast_party_redirecting *apr = &orig->redirecting;
	char *data, *type;
	int cause = 0;

	/* gather data and request the new forward channel */
	ast_copy_string(tmpchan, orig->call_forward, sizeof(tmpchan));
	if ((data = strchr(tmpchan, '/'))) {
		*data++ = '\0';
		type = tmpchan;
	} else {
		const char *forward_context;
		ast_channel_lock(orig);
		forward_context = pbx_builtin_getvar_helper(orig, "FORWARD_CONTEXT");
		snprintf(tmpchan, sizeof(tmpchan), "%s@%s", orig->call_forward, S_OR(forward_context, orig->context));
		ast_channel_unlock(orig);
		data = tmpchan;
		type = "Local";
	}
	if (!(new = ast_request(type, format, orig, data, &cause))) {
		ast_log(LOG_NOTICE, "Unable to create channel for call forward to '%s/%s' (cause = %d)\n", type, data, cause);
		handle_cause(cause, outstate);
		ast_hangup(orig);
		return NULL;
	}

	ast_channel_set_redirecting(new, apr, NULL);

	/* Copy/inherit important information into new channel */
	if (oh) {
		if (oh->vars) {
			ast_set_variables(new, oh->vars);
		}
		if (!ast_strlen_zero(oh->cid_num) && !ast_strlen_zero(oh->cid_name)) {
			ast_set_callerid(new, oh->cid_num, oh->cid_name, oh->cid_num);
		}
		if (oh->parent_channel) {
			ast_channel_update_redirecting(oh->parent_channel, apr, NULL);
			ast_channel_inherit_variables(oh->parent_channel, new);
			ast_channel_datastore_inherit(oh->parent_channel, new);
		}
		if (oh->account) {
			ast_cdr_setaccount(new, oh->account);
		}
	} else if (caller) { /* no outgoing helper so use caller if avaliable */
		ast_channel_update_redirecting(caller, apr, NULL);
		ast_channel_inherit_variables(caller, new);
		ast_channel_datastore_inherit(caller, new);
	}

	ast_channel_lock(orig);
	while (ast_channel_trylock(new)) {
		CHANNEL_DEADLOCK_AVOIDANCE(orig);
	}
	ast_copy_flags(new->cdr, orig->cdr, AST_CDR_FLAG_ORIGINATED);
	ast_string_field_set(new, accountcode, orig->accountcode);
	ast_party_caller_copy(&new->caller, &orig->caller);
	ast_party_connected_line_copy(&new->connected, &orig->connected);
	ast_channel_unlock(new);
	ast_channel_unlock(orig);

	/* call new channel */
	if ((*timeout = ast_call(new, data, 0))) {
		ast_log(LOG_NOTICE, "Unable to call forward to channel %s/%s\n", type, (char *)data);
		ast_hangup(orig);
		ast_hangup(new);
		return NULL;
	}
	ast_hangup(orig);

	return new;
}

struct ast_channel *__ast_request_and_dial(const char *type, format_t format, const struct ast_channel *requestor, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name, struct outgoing_helper *oh)
{
	int dummy_outstate;
	int cause = 0;
	struct ast_channel *chan;
	int res = 0;
	int last_subclass = 0;
	struct ast_party_connected_line connected;
	
	if (outstate)
		*outstate = 0;
	else
		outstate = &dummy_outstate;	/* make outstate always a valid pointer */

	chan = ast_request(type, format, requestor, data, &cause);
	if (!chan) {
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		handle_cause(cause, outstate);
		return NULL;
	}

	if (oh) {
		if (oh->vars)	
			ast_set_variables(chan, oh->vars);
		/* XXX why is this necessary, for the parent_channel perhaps ? */
		if (!ast_strlen_zero(oh->cid_num) && !ast_strlen_zero(oh->cid_name))
			ast_set_callerid(chan, oh->cid_num, oh->cid_name, oh->cid_num);
		if (oh->parent_channel) {
			ast_channel_inherit_variables(oh->parent_channel, chan);
			ast_channel_datastore_inherit(oh->parent_channel, chan);
		}
		if (oh->account)
			ast_cdr_setaccount(chan, oh->account);	
	}

	ast_set_callerid(chan, cid_num, cid_name, cid_num);
	ast_set_flag(chan->cdr, AST_CDR_FLAG_ORIGINATED);
	ast_party_connected_line_set_init(&connected, &chan->connected);
	if (cid_num) {
		connected.id.number.valid = 1;
		connected.id.number.str = (char *) cid_num;
		connected.id.number.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
	}
	if (cid_name) {
		connected.id.name.valid = 1;
		connected.id.name.str = (char *) cid_name;
		connected.id.name.presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
	}
	ast_channel_set_connected_line(chan, &connected, NULL);

	if (ast_call(chan, data, 0)) {	/* ast_call failed... */
		ast_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, (char *)data);
	} else {
		res = 1;	/* mark success in case chan->_state is already AST_STATE_UP */
		while (timeout && chan->_state != AST_STATE_UP) {
			struct ast_frame *f;
			res = ast_waitfor(chan, timeout);
			if (res == 0) { /* timeout, treat it like ringing */
				*outstate = AST_CONTROL_RINGING;
				break;
			}
			if (res < 0) /* error or done */
				break;
			if (timeout > -1)
				timeout = res;
			if (!ast_strlen_zero(chan->call_forward)) {
				if (!(chan = ast_call_forward(NULL, chan, &timeout, format, oh, outstate))) {
					return NULL;
				}
				continue;
			}

			f = ast_read(chan);
			if (!f) {
				*outstate = AST_CONTROL_HANGUP;
				res = 0;
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				switch (f->subclass.integer) {
				case AST_CONTROL_RINGING:	/* record but keep going */
					*outstate = f->subclass.integer;
					break;

				case AST_CONTROL_BUSY:
					ast_cdr_busy(chan->cdr);
					*outstate = f->subclass.integer;
					timeout = 0;
					break;

				case AST_CONTROL_CONGESTION:
					ast_cdr_failed(chan->cdr);
					*outstate = f->subclass.integer;
					timeout = 0;
					break;

				case AST_CONTROL_ANSWER:
					ast_cdr_answer(chan->cdr);
					*outstate = f->subclass.integer;
					timeout = 0;		/* trick to force exit from the while() */
					break;

				/* Ignore these */
				case AST_CONTROL_PROGRESS:
				case AST_CONTROL_PROCEEDING:
				case AST_CONTROL_HOLD:
				case AST_CONTROL_UNHOLD:
				case AST_CONTROL_VIDUPDATE:
				case AST_CONTROL_SRCUPDATE:
				case AST_CONTROL_SRCCHANGE:
				case AST_CONTROL_CONNECTED_LINE:
				case AST_CONTROL_REDIRECTING:
				case AST_CONTROL_CC:
				case -1:			/* Ignore -- just stopping indications */
					break;

				default:
					ast_log(LOG_NOTICE, "Don't know what to do with control frame %d\n", f->subclass.integer);
				}
				last_subclass = f->subclass.integer;
			}
			ast_frfree(f);
		}
	}

	/* Final fixups */
	if (oh) {
		if (!ast_strlen_zero(oh->context))
			ast_copy_string(chan->context, oh->context, sizeof(chan->context));
		if (!ast_strlen_zero(oh->exten))
			ast_copy_string(chan->exten, oh->exten, sizeof(chan->exten));
		if (oh->priority)	
			chan->priority = oh->priority;
	}
	if (chan->_state == AST_STATE_UP)
		*outstate = AST_CONTROL_ANSWER;

	if (res <= 0) {
		if ( AST_CONTROL_RINGING == last_subclass ) 
			chan->hangupcause = AST_CAUSE_NO_ANSWER;
		if (!chan->cdr && (chan->cdr = ast_cdr_alloc()))
			ast_cdr_init(chan->cdr, chan);
		if (chan->cdr) {
			char tmp[256];
			snprintf(tmp, sizeof(tmp), "%s/%s", type, (char *)data);
			ast_cdr_setapp(chan->cdr,"Dial",tmp);
			ast_cdr_update(chan);
			ast_cdr_start(chan->cdr);
			ast_cdr_end(chan->cdr);
			/* If the cause wasn't handled properly */
			if (ast_cdr_disposition(chan->cdr,chan->hangupcause))
				ast_cdr_failed(chan->cdr);
		}
		ast_hangup(chan);
		chan = NULL;
	}
	return chan;
}

struct ast_channel *ast_request_and_dial(const char *type, format_t format, const struct ast_channel *requestor, void *data, int timeout, int *outstate, const char *cidnum, const char *cidname)
{
	return __ast_request_and_dial(type, format, requestor, data, timeout, outstate, cidnum, cidname, NULL);
}

static int set_security_requirements(const struct ast_channel *requestor, struct ast_channel *out)
{
	int ops[2][2] = {
		{AST_OPTION_SECURE_SIGNALING, 0},
		{AST_OPTION_SECURE_MEDIA, 0},
	};
	int i;
	struct ast_channel *r = (struct ast_channel *) requestor; /* UGLY */
	struct ast_datastore *ds;

	if (!requestor || !out) {
		return 0;
	}

	ast_channel_lock(r);
	if ((ds = ast_channel_datastore_find(r, &secure_call_info, NULL))) {
		struct ast_secure_call_store *encrypt = ds->data;
		ops[0][1] = encrypt->signaling;
		ops[1][1] = encrypt->media;
	} else {
		ast_channel_unlock(r);
		return 0;
	}
	ast_channel_unlock(r);

	for (i = 0; i < 2; i++) {
		if (ops[i][1]) {
			if (ast_channel_setoption(out, ops[i][0], &ops[i][1], sizeof(ops[i][1]), 0)) {
				/* We require a security feature, but the channel won't provide it */
				return -1;
			}
		} else {
			/* We don't care if we can't clear the option on a channel that doesn't support it */
			ast_channel_setoption(out, ops[i][0], &ops[i][1], sizeof(ops[i][1]), 0);
		}
	}

	return 0;
}

struct ast_channel *ast_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause)
{
	struct chanlist *chan;
	struct ast_channel *c;
	format_t capabilities;
	format_t fmt;
	int res;
	int foo;
	format_t videoformat = format & AST_FORMAT_VIDEO_MASK;
	format_t textformat = format & AST_FORMAT_TEXT_MASK;

	if (!cause)
		cause = &foo;
	*cause = AST_CAUSE_NOTDEFINED;

	if (AST_RWLIST_RDLOCK(&backends)) {
		ast_log(LOG_WARNING, "Unable to lock technology backend list\n");
		return NULL;
	}

	AST_RWLIST_TRAVERSE(&backends, chan, list) {
		if (strcasecmp(type, chan->tech->type))
			continue;

		capabilities = chan->tech->capabilities;
		fmt = format & AST_FORMAT_AUDIO_MASK;
		if (fmt) {
			/* We have audio - is it possible to connect the various calls to each other? 
				(Avoid this check for calls without audio, like text+video calls)
			*/
			res = ast_translator_best_choice(&fmt, &capabilities);
			if (res < 0) {
				char tmp1[256], tmp2[256];
				ast_log(LOG_WARNING, "No translator path exists for channel type %s (native %s) to %s\n", type,
					ast_getformatname_multiple(tmp1, sizeof(tmp1), chan->tech->capabilities),
					ast_getformatname_multiple(tmp2, sizeof(tmp2), format));
				*cause = AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
				AST_RWLIST_UNLOCK(&backends);
				return NULL;
			}
		}
		AST_RWLIST_UNLOCK(&backends);
		if (!chan->tech->requester)
			return NULL;

		if (!(c = chan->tech->requester(type, capabilities | videoformat | textformat, requestor, data, cause)))
			return NULL;

		if (set_security_requirements(requestor, c)) {
			ast_log(LOG_WARNING, "Setting security requirements failed\n");
			c = ast_channel_release(c);
			*cause = AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
			return NULL;
		}

		/* no need to generate a Newchannel event here; it is done in the channel_alloc call */
		return c;
	}

	ast_log(LOG_WARNING, "No channel type registered for '%s'\n", type);
	*cause = AST_CAUSE_NOSUCHDRIVER;
	AST_RWLIST_UNLOCK(&backends);

	return NULL;
}

int ast_call(struct ast_channel *chan, char *addr, int timeout)
{
	/* Place an outgoing call, but don't wait any longer than timeout ms before returning.
	   If the remote end does not answer within the timeout, then do NOT hang up, but
	   return anyway.  */
	int res = -1;
	/* Stop if we're a zombie or need a soft hangup */
	ast_channel_lock(chan);
	if (!ast_test_flag(chan, AST_FLAG_ZOMBIE) && !ast_check_hangup(chan)) {
		if (chan->cdr) {
			ast_set_flag(chan->cdr, AST_CDR_FLAG_DIALED);
		}
		if (chan->tech->call)
			res = chan->tech->call(chan, addr, timeout);
		ast_set_flag(chan, AST_FLAG_OUTGOING);
	}
	ast_channel_unlock(chan);
	return res;
}

/*!
  \brief Transfer a call to dest, if the channel supports transfer

  Called by:
	\arg app_transfer
	\arg the manager interface
*/
int ast_transfer(struct ast_channel *chan, char *dest)
{
	int res = -1;

	/* Stop if we're a zombie or need a soft hangup */
	ast_channel_lock(chan);
	if (!ast_test_flag(chan, AST_FLAG_ZOMBIE) && !ast_check_hangup(chan)) {
		if (chan->tech->transfer) {
			res = chan->tech->transfer(chan, dest);
			if (!res)
				res = 1;
		} else
			res = 0;
	}
	ast_channel_unlock(chan);

	if (res <= 0) {
		return res;
	}

	for (;;) {
		struct ast_frame *fr;

		res = ast_waitfor(chan, -1);

		if (res < 0 || !(fr = ast_read(chan))) {
			res = -1;
			break;
		}

		if (fr->frametype == AST_FRAME_CONTROL && fr->subclass.integer == AST_CONTROL_TRANSFER) {
			enum ast_control_transfer *message = fr->data.ptr;

			if (*message == AST_TRANSFER_SUCCESS) {
				res = 1;
			} else {
				res = -1;
			}

			ast_frfree(fr);
			break;
		}

		ast_frfree(fr);
	}

	return res;
}

int ast_readstring(struct ast_channel *c, char *s, int len, int timeout, int ftimeout, char *enders)
{
	return ast_readstring_full(c, s, len, timeout, ftimeout, enders, -1, -1);
}

int ast_readstring_full(struct ast_channel *c, char *s, int len, int timeout, int ftimeout, char *enders, int audiofd, int ctrlfd)
{
	int pos = 0;	/* index in the buffer where we accumulate digits */
	int to = ftimeout;

	struct ast_silence_generator *silgen = NULL;

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c, AST_FLAG_ZOMBIE) || ast_check_hangup(c))
		return -1;
	if (!len)
		return -1;
	for (;;) {
		int d;
		if (c->stream) {
			d = ast_waitstream_full(c, AST_DIGIT_ANY, audiofd, ctrlfd);
			ast_stopstream(c);
			if (!silgen && ast_opt_transmit_silence)
				silgen = ast_channel_start_silence_generator(c);
			usleep(1000);
			if (!d)
				d = ast_waitfordigit_full(c, to, audiofd, ctrlfd);
		} else {
			if (!silgen && ast_opt_transmit_silence)
				silgen = ast_channel_start_silence_generator(c);
			d = ast_waitfordigit_full(c, to, audiofd, ctrlfd);
		}
		if (d < 0) {
			ast_channel_stop_silence_generator(c, silgen);
			return AST_GETDATA_FAILED;
		}
		if (d == 0) {
			s[pos] = '\0';
			ast_channel_stop_silence_generator(c, silgen);
			return AST_GETDATA_TIMEOUT;
		}
		if (d == 1) {
			s[pos] = '\0';
			ast_channel_stop_silence_generator(c, silgen);
			return AST_GETDATA_INTERRUPTED;
		}
		if (strchr(enders, d) && (pos == 0)) {
			s[pos] = '\0';
			ast_channel_stop_silence_generator(c, silgen);
			return AST_GETDATA_EMPTY_END_TERMINATED;
		}
		if (!strchr(enders, d)) {
			s[pos++] = d;
		}
		if (strchr(enders, d) || (pos >= len)) {
			s[pos] = '\0';
			ast_channel_stop_silence_generator(c, silgen);
			return AST_GETDATA_COMPLETE;
		}
		to = timeout;
	}
	/* Never reached */
	return 0;
}

int ast_channel_supports_html(struct ast_channel *chan)
{
	return (chan->tech->send_html) ? 1 : 0;
}

int ast_channel_sendhtml(struct ast_channel *chan, int subclass, const char *data, int datalen)
{
	if (chan->tech->send_html)
		return chan->tech->send_html(chan, subclass, data, datalen);
	return -1;
}

int ast_channel_sendurl(struct ast_channel *chan, const char *url)
{
	return ast_channel_sendhtml(chan, AST_HTML_URL, url, strlen(url) + 1);
}

/*! \brief Set up translation from one channel to another */
static int ast_channel_make_compatible_helper(struct ast_channel *from, struct ast_channel *to)
{
	format_t src, dst;
	int use_slin;

	/* See if the channel driver can natively make these two channels compatible */
	if (from->tech->bridge && from->tech->bridge == to->tech->bridge &&
	    !ast_channel_setoption(from, AST_OPTION_MAKE_COMPATIBLE, to, sizeof(struct ast_channel *), 0)) {
		return 0;
	}

	if (from->readformat == to->writeformat && from->writeformat == to->readformat) {
		/* Already compatible!  Moving on ... */
		return 0;
	}

	/* Set up translation from the 'from' channel to the 'to' channel */
	src = from->nativeformats;
	dst = to->nativeformats;

	/* If there's no audio in this call, don't bother with trying to find a translation path */
	if ((src & AST_FORMAT_AUDIO_MASK) == 0 || (dst & AST_FORMAT_AUDIO_MASK) == 0)
		return 0;

	if (ast_translator_best_choice(&dst, &src) < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s to %s\n", from->name, to->name);
		return -1;
	}

	/* if the best path is not 'pass through', then
	 * transcoding is needed; if desired, force transcode path
	 * to use SLINEAR between channels, but only if there is
	 * no direct conversion available. If generic PLC is
	 * desired, then transcoding via SLINEAR is a requirement
	 */
	use_slin = (src == AST_FORMAT_SLINEAR || dst == AST_FORMAT_SLINEAR);
	if ((src != dst) && (ast_opt_generic_plc || ast_opt_transcode_via_slin) &&
	    (ast_translate_path_steps(dst, src) != 1 || use_slin))
		dst = AST_FORMAT_SLINEAR;
	if (ast_set_read_format(from, dst) < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %s\n", from->name, ast_getformatname(dst));
		return -1;
	}
	if (ast_set_write_format(to, dst) < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %s\n", to->name, ast_getformatname(dst));
		return -1;
	}
	return 0;
}

int ast_channel_make_compatible(struct ast_channel *chan, struct ast_channel *peer)
{
	/* Some callers do not check return code, and we must try to set all call legs correctly */
	int rc = 0;

	/* Set up translation from the chan to the peer */
	rc = ast_channel_make_compatible_helper(chan, peer);

	if (rc < 0)
		return rc;

	/* Set up translation from the peer to the chan */
	rc = ast_channel_make_compatible_helper(peer, chan);

	return rc;
}

int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clonechan)
{
	int res = -1;
	struct ast_channel *final_orig, *final_clone, *base;

retrymasq:
	final_orig = original;
	final_clone = clonechan;

	ast_channel_lock(original);
	while (ast_channel_trylock(clonechan)) {
		ast_channel_unlock(original);
		usleep(1);
		ast_channel_lock(original);
	}

	/* each of these channels may be sitting behind a channel proxy (i.e. chan_agent)
	   and if so, we don't really want to masquerade it, but its proxy */
	if (original->_bridge && (original->_bridge != ast_bridged_channel(original)) && (original->_bridge->_bridge != original))
		final_orig = original->_bridge;

	if (clonechan->_bridge && (clonechan->_bridge != ast_bridged_channel(clonechan)) && (clonechan->_bridge->_bridge != clonechan))
		final_clone = clonechan->_bridge;
	
	if (final_clone->tech->get_base_channel && (base = final_clone->tech->get_base_channel(final_clone))) {
		final_clone = base;
	}

	if ((final_orig != original) || (final_clone != clonechan)) {
		/* Lots and lots of deadlock avoidance.  The main one we're competing with
		 * is ast_write(), which locks channels recursively, when working with a
		 * proxy channel. */
		if (ast_channel_trylock(final_orig)) {
			ast_channel_unlock(clonechan);
			ast_channel_unlock(original);
			goto retrymasq;
		}
		if (ast_channel_trylock(final_clone)) {
			ast_channel_unlock(final_orig);
			ast_channel_unlock(clonechan);
			ast_channel_unlock(original);
			goto retrymasq;
		}
		ast_channel_unlock(clonechan);
		ast_channel_unlock(original);
		original = final_orig;
		clonechan = final_clone;
	}

	if (original == clonechan) {
		ast_log(LOG_WARNING, "Can't masquerade channel '%s' into itself!\n", original->name);
		ast_channel_unlock(clonechan);
		ast_channel_unlock(original);
		return -1;
	}

	ast_debug(1, "Planning to masquerade channel %s into the structure of %s\n",
		clonechan->name, original->name);

	if (!original->masqr && !original->masq && !clonechan->masq && !clonechan->masqr) {
		original->masq = clonechan;
		clonechan->masqr = original;
		ast_queue_frame(original, &ast_null_frame);
		ast_queue_frame(clonechan, &ast_null_frame);
		ast_debug(1, "Done planning to masquerade channel %s into the structure of %s\n", clonechan->name, original->name);
		res = 0;
	} else if (original->masq) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
			original->masq->name, original->name);
	} else if (original->masqr) {
		/* not yet as a previously planned masq hasn't yet happened */
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
			original->name, original->masqr->name);
	} else if (clonechan->masq) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
			clonechan->masq->name, clonechan->name);
	} else { /* (clonechan->masqr) */
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
		clonechan->name, clonechan->masqr->name);
	}

	ast_channel_unlock(clonechan);
	ast_channel_unlock(original);

	return res;
}

/*! \brief this function simply changes the name of the channel and issues a manager_event
 *         with out unlinking and linking the channel from the ao2_container.  This should
 *         only be used when the channel has already been unlinked from the ao2_container.
 */
static void __ast_change_name_nolink(struct ast_channel *chan, const char *newname)
{
	ast_manager_event(chan, EVENT_FLAG_CALL, "Rename", "Channel: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", chan->name, newname, chan->uniqueid);
	ast_string_field_set(chan, name, newname);
}

void ast_change_name(struct ast_channel *chan, const char *newname)
{
	/* We must re-link, as the hash value will change here. */
	ao2_unlink(channels, chan);
	ast_channel_lock(chan);
	__ast_change_name_nolink(chan, newname);
	ast_channel_unlock(chan);
	ao2_link(channels, chan);
}

void ast_channel_inherit_variables(const struct ast_channel *parent, struct ast_channel *child)
{
	struct ast_var_t *current, *newvar;
	const char *varname;

	AST_LIST_TRAVERSE(&parent->varshead, current, entries) {
		int vartype = 0;

		varname = ast_var_full_name(current);
		if (!varname)
			continue;

		if (varname[0] == '_') {
			vartype = 1;
			if (varname[1] == '_')
				vartype = 2;
		}

		switch (vartype) {
		case 1:
			newvar = ast_var_assign(&varname[1], ast_var_value(current));
			if (newvar) {
				AST_LIST_INSERT_TAIL(&child->varshead, newvar, entries);
				ast_debug(1, "Copying soft-transferable variable %s.\n", ast_var_name(newvar));
			}
			break;
		case 2:
			newvar = ast_var_assign(varname, ast_var_value(current));
			if (newvar) {
				AST_LIST_INSERT_TAIL(&child->varshead, newvar, entries);
				ast_debug(1, "Copying hard-transferable variable %s.\n", ast_var_name(newvar));
			}
			break;
		default:
			ast_debug(1, "Not copying variable %s.\n", ast_var_name(current));
			break;
		}
	}
}

/*!
  \brief Clone channel variables from 'clone' channel into 'original' channel

  All variables except those related to app_groupcount are cloned.
  Variables are actually _removed_ from 'clone' channel, presumably
  because it will subsequently be destroyed.

  \note Assumes locks will be in place on both channels when called.
*/
static void clone_variables(struct ast_channel *original, struct ast_channel *clonechan)
{
	struct ast_var_t *current, *newvar;
	/* Append variables from clone channel into original channel */
	/* XXX Is this always correct?  We have to in order to keep MACROS working XXX */
	if (AST_LIST_FIRST(&clonechan->varshead))
		AST_LIST_APPEND_LIST(&original->varshead, &clonechan->varshead, entries);

	/* then, dup the varshead list into the clone */
	
	AST_LIST_TRAVERSE(&original->varshead, current, entries) {
		newvar = ast_var_assign(current->name, current->value);
		if (newvar)
			AST_LIST_INSERT_TAIL(&clonechan->varshead, newvar, entries);
	}
}



/* return the oldest of two linkedids.  linkedid is derived from
   uniqueid which is formed like this: [systemname-]ctime.seq

   The systemname, and the dash are optional, followed by the epoch
   time followed by an integer sequence.  Note that this is not a
   decimal number, since 1.2 is less than 1.11 in uniqueid land.

   To compare two uniqueids, we parse out the integer values of the
   time and the sequence numbers and compare them, with time trumping
   sequence.
*/
static const char *oldest_linkedid(const char *a, const char *b)
{
	const char *satime, *saseq;
	const char *sbtime, *sbseq;
	const char *dash;

	unsigned int atime, aseq, btime, bseq;

	if (ast_strlen_zero(a))
		return b;

	if (ast_strlen_zero(b))
		return a;

	satime = a;
	sbtime = b;

	/* jump over the system name */
	if ((dash = strrchr(satime, '-'))) {
		satime = dash+1;
	}
	if ((dash = strrchr(sbtime, '-'))) {
		sbtime = dash+1;
	}

	/* the sequence comes after the '.' */
	saseq = strchr(satime, '.');
	sbseq = strchr(sbtime, '.');
	if (!saseq || !sbseq)
		return NULL;
	saseq++;
	sbseq++;

	/* convert it all to integers */
	atime = atoi(satime); /* note that atoi is ignoring the '.' after the time string */
	btime = atoi(sbtime); /* note that atoi is ignoring the '.' after the time string */
	aseq = atoi(saseq);
	bseq = atoi(sbseq);

	/* and finally compare */
	if (atime == btime) {
		return (aseq < bseq) ? a : b;
	}
	else {
		return (atime < btime) ? a : b;
	}
}

/*! Set the channel's linkedid to the given string, and also check to
 *  see if the channel's old linkedid is now being retired */
static void ast_channel_change_linkedid(struct ast_channel *chan, const char *linkedid)
{
	/* if the linkedid for this channel is being changed from something, check... */
	if (!ast_strlen_zero(chan->linkedid) && 0 != strcmp(chan->linkedid, linkedid)) {
		ast_cel_check_retire_linkedid(chan);
	}

	ast_string_field_set(chan, linkedid, linkedid);
}


/*!
  \brief Propagate the oldest linkedid between associated channels

*/
void ast_channel_set_linkgroup(struct ast_channel *chan, struct ast_channel *peer)
{
	const char* linkedid=NULL;
	struct ast_channel *bridged;

	linkedid = oldest_linkedid(chan->linkedid, peer->linkedid);
	linkedid = oldest_linkedid(linkedid, chan->uniqueid);
	linkedid = oldest_linkedid(linkedid, peer->uniqueid);
	if (chan->_bridge) {
		bridged = ast_bridged_channel(chan);
		if (bridged != peer) {
			linkedid = oldest_linkedid(linkedid, bridged->linkedid);
			linkedid = oldest_linkedid(linkedid, bridged->uniqueid);
		}
	}
	if (peer->_bridge) {
		bridged = ast_bridged_channel(peer);
		if (bridged != chan) {
			linkedid = oldest_linkedid(linkedid, bridged->linkedid);
			linkedid = oldest_linkedid(linkedid, bridged->uniqueid);
		}
	}

	/* just in case setting a stringfield to itself causes problems */
	linkedid = ast_strdupa(linkedid);

	ast_channel_change_linkedid(chan, linkedid);
	ast_channel_change_linkedid(peer, linkedid);
	if (chan->_bridge) {
		bridged = ast_bridged_channel(chan);
		if (bridged != peer) {
			ast_channel_change_linkedid(bridged, linkedid);
		}
	}
	if (peer->_bridge) {
		bridged = ast_bridged_channel(peer);
		if (bridged != chan) {
			ast_channel_change_linkedid(bridged, linkedid);
		}
	}
}

/* copy accountcode and peeraccount across during a link */
static void ast_set_owners_and_peers(struct ast_channel *chan1,
									 struct ast_channel *chan2)
{
	if (!ast_strlen_zero(chan1->accountcode) && ast_strlen_zero(chan2->peeraccount)) {
		ast_log(LOG_DEBUG, "setting peeraccount to %s for %s from data on channel %s\n",
				chan1->accountcode, chan2->name, chan1->name);
		ast_string_field_set(chan2, peeraccount, chan1->accountcode);
	}
	if (!ast_strlen_zero(chan2->accountcode) && ast_strlen_zero(chan1->peeraccount)) {
		ast_log(LOG_DEBUG, "setting peeraccount to %s for %s from data on channel %s\n",
				chan2->accountcode, chan1->name, chan2->name);
		ast_string_field_set(chan1, peeraccount, chan2->accountcode);
	}
	if (!ast_strlen_zero(chan1->peeraccount) && ast_strlen_zero(chan2->accountcode)) {
		ast_log(LOG_DEBUG, "setting accountcode to %s for %s from data on channel %s\n",
				chan1->peeraccount, chan2->name, chan1->name);
		ast_string_field_set(chan2, accountcode, chan1->peeraccount);
	}
	if (!ast_strlen_zero(chan2->peeraccount) && ast_strlen_zero(chan1->accountcode)) {
		ast_log(LOG_DEBUG, "setting accountcode to %s for %s from data on channel %s\n",
				chan2->peeraccount, chan1->name, chan2->name);
		ast_string_field_set(chan1, accountcode, chan2->peeraccount);
	}
	if (0 != strcmp(chan1->accountcode, chan2->peeraccount)) {
		ast_log(LOG_DEBUG, "changing peeraccount from %s to %s on %s to match channel %s\n",
				chan2->peeraccount, chan1->peeraccount, chan2->name, chan1->name);
		ast_string_field_set(chan2, peeraccount, chan1->accountcode);
	}
	if (0 != strcmp(chan2->accountcode, chan1->peeraccount)) {
		ast_log(LOG_DEBUG, "changing peeraccount from %s to %s on %s to match channel %s\n",
				chan1->peeraccount, chan2->peeraccount, chan1->name, chan2->name);
		ast_string_field_set(chan1, peeraccount, chan2->accountcode);
	}
}

/*!
 * \pre chan is locked
 */
static void report_new_callerid(struct ast_channel *chan)
{
	int pres;

	pres = ast_party_id_presentation(&chan->caller.id);
	ast_manager_event(chan, EVENT_FLAG_CALL, "NewCallerid",
		"Channel: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"Uniqueid: %s\r\n"
		"CID-CallingPres: %d (%s)\r\n",
		chan->name,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, ""),
		S_COR(chan->caller.id.name.valid, chan->caller.id.name.str, ""),
		chan->uniqueid,
		pres,
		ast_describe_caller_presentation(pres)
		);
}

/*!
  \brief Masquerade a channel

  \note Assumes _NO_ channels and _NO_ channel pvt's are locked.  If a channel is locked while calling
        this function, it invalidates our channel container locking order.  All channels
		must be unlocked before it is permissible to lock the channels' ao2 container.
*/
int ast_do_masquerade(struct ast_channel *original)
{
	format_t x;
	int i;
	int res=0;
	int origstate;
	struct ast_frame *current;
	const struct ast_channel_tech *t;
	void *t_pvt;
	union {
		struct ast_party_dialed dialed;
		struct ast_party_caller caller;
		struct ast_party_connected_line connected;
		struct ast_party_redirecting redirecting;
	} exchange;
	struct ast_channel *clonechan, *chans[2];
	struct ast_channel *bridged;
	struct ast_cdr *cdr;
	format_t rformat = original->readformat;
	format_t wformat = original->writeformat;
	char newn[AST_CHANNEL_NAME];
	char orig[AST_CHANNEL_NAME];
	char masqn[AST_CHANNEL_NAME];
	char zombn[AST_CHANNEL_NAME];

	/* XXX This operation is a bit odd.  We're essentially putting the guts of
	 * the clone channel into the original channel.  Start by killing off the
	 * original channel's backend.  While the features are nice, which is the
	 * reason we're keeping it, it's still awesomely weird. XXX */

	/* The reasoning for the channels ao2_container lock here is complex.
	 * 
	 * In order to check for a race condition, the original channel must
	 * be locked.  If it is determined that the masquerade should proceed
	 * the original channel can absolutely not be unlocked until the end
	 * of the function.  Since after determining the masquerade should
	 * continue requires the channels to be unlinked from the ao2_container,
	 * the container lock must be held first to achieve proper locking order.
	 */
	ao2_lock(channels);

	/* lock the original channel to determine if the masquerade is require or not */
	ast_channel_lock(original);

	/* This checks to see if the masquerade has already happened or not.  There is a
	 * race condition that exists for this function. Since all pvt and channel locks
	 * must be let go before calling do_masquerade, it is possible that it could be
	 * called multiple times for the same channel.  This check verifies whether
	 * or not the masquerade has already been completed by another thread */
	if (!original->masq) {
		ast_channel_unlock(original);
		ao2_unlock(channels);
		return 0; /* masq already completed by another thread, or never needed to be done to begin with */
	}

	/* now that we have verified no race condition exists, set the clone channel */
	clonechan = original->masq;

	/* since this function already holds the global container lock, unlocking original
	 * for deadlock avoidance will not result in any sort of masquerade race condition.
	 * If masq is called by a different thread while this happens, it will be stuck waiting
	 * until we unlock the container. */
	while (ast_channel_trylock(clonechan)) {
		CHANNEL_DEADLOCK_AVOIDANCE(original);
	}

	/* clear the masquerade channels */
	original->masq = NULL;
	clonechan->masqr = NULL;

	/* unlink from channels container as name (which is the hash value) will change */
	ao2_unlink(channels, original);
	ao2_unlink(channels, clonechan);

	ast_debug(4, "Actually Masquerading %s(%d) into the structure of %s(%d)\n",
		clonechan->name, clonechan->_state, original->name, original->_state);

	chans[0] = clonechan;
	chans[1] = original;
	ast_manager_event_multichan(EVENT_FLAG_CALL, "Masquerade", 2, chans, "Clone: %s\r\nCloneState: %s\r\nOriginal: %s\r\nOriginalState: %s\r\n",
		      clonechan->name, ast_state2str(clonechan->_state), original->name, ast_state2str(original->_state));

	/* Having remembered the original read/write formats, we turn off any translation on either
	   one */
	free_translation(clonechan);
	free_translation(original);

	/* Save the original name */
	ast_copy_string(orig, original->name, sizeof(orig));
	/* Save the new name */
	ast_copy_string(newn, clonechan->name, sizeof(newn));
	/* Create the masq name */
	snprintf(masqn, sizeof(masqn), "%s<MASQ>", newn);

	/* Mangle the name of the clone channel */
	__ast_change_name_nolink(clonechan, masqn);

	/* Copy the name from the clone channel */
	__ast_change_name_nolink(original, newn);

	/* share linked id's */
	ast_channel_set_linkgroup(original, clonechan);

	/* Swap the technologies */
	t = original->tech;
	original->tech = clonechan->tech;
	clonechan->tech = t;

	/* Swap the cdrs */
	cdr = original->cdr;
	original->cdr = clonechan->cdr;
	clonechan->cdr = cdr;

	t_pvt = original->tech_pvt;
	original->tech_pvt = clonechan->tech_pvt;
	clonechan->tech_pvt = t_pvt;

	/* Swap the alertpipes */
	for (i = 0; i < 2; i++) {
		x = original->alertpipe[i];
		original->alertpipe[i] = clonechan->alertpipe[i];
		clonechan->alertpipe[i] = x;
	}

	/* 
	 * Swap the readq's.  The end result should be this:
	 *
	 *  1) All frames should be on the new (original) channel.
	 *  2) Any frames that were already on the new channel before this
	 *     masquerade need to be at the end of the readq, after all of the
	 *     frames on the old (clone) channel.
	 *  3) The alertpipe needs to get poked for every frame that was already
	 *     on the new channel, since we are now using the alert pipe from the
	 *     old (clone) channel.
	 */
	{
		AST_LIST_HEAD_NOLOCK(, ast_frame) tmp_readq;
		AST_LIST_HEAD_SET_NOLOCK(&tmp_readq, NULL);

		AST_LIST_APPEND_LIST(&tmp_readq, &original->readq, frame_list);
		AST_LIST_APPEND_LIST(&original->readq, &clonechan->readq, frame_list);

		while ((current = AST_LIST_REMOVE_HEAD(&tmp_readq, frame_list))) {
			AST_LIST_INSERT_TAIL(&original->readq, current, frame_list);
			if (original->alertpipe[1] > -1) {
				int poke = 0;

				if (write(original->alertpipe[1], &poke, sizeof(poke)) < 0) {
					ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
				}
			}
		}
	}

	/* Swap the raw formats */
	x = original->rawreadformat;
	original->rawreadformat = clonechan->rawreadformat;
	clonechan->rawreadformat = x;
	x = original->rawwriteformat;
	original->rawwriteformat = clonechan->rawwriteformat;
	clonechan->rawwriteformat = x;

	clonechan->_softhangup = AST_SOFTHANGUP_DEV;

	/* And of course, so does our current state.  Note we need not
	   call ast_setstate since the event manager doesn't really consider
	   these separate.  We do this early so that the clone has the proper
	   state of the original channel. */
	origstate = original->_state;
	original->_state = clonechan->_state;
	clonechan->_state = origstate;

	if (clonechan->tech->fixup && clonechan->tech->fixup(original, clonechan)) {
		ast_log(LOG_WARNING, "Fixup failed on channel %s, strange things may happen.\n", clonechan->name);
	}

	/* Start by disconnecting the original's physical side */
	if (clonechan->tech->hangup && clonechan->tech->hangup(clonechan)) {
		ast_log(LOG_WARNING, "Hangup failed!  Strange things may happen!\n");
		res = -1;
		goto done;
	}

	/* Mangle the name of the clone channel */
	snprintf(zombn, sizeof(zombn), "%s<ZOMBIE>", orig); /* quick, hide the brains! */
	__ast_change_name_nolink(clonechan, zombn);

	/* Update the type. */
	t_pvt = original->monitor;
	original->monitor = clonechan->monitor;
	clonechan->monitor = t_pvt;

	/* Keep the same language.  */
	ast_string_field_set(original, language, clonechan->language);
	/* Copy the FD's other than the generator fd */
	for (x = 0; x < AST_MAX_FDS; x++) {
		if (x != AST_GENERATOR_FD)
			ast_channel_set_fd(original, x, clonechan->fds[x]);
	}

	ast_app_group_update(clonechan, original);

	/* Move data stores over */
	if (AST_LIST_FIRST(&clonechan->datastores)) {
		struct ast_datastore *ds;
		/* We use a safe traversal here because some fixup routines actually
		 * remove the datastore from the list and free them.
		 */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&clonechan->datastores, ds, entry) {
			if (ds->info->chan_fixup)
				ds->info->chan_fixup(ds->data, clonechan, original);
		}
		AST_LIST_TRAVERSE_SAFE_END;
		AST_LIST_APPEND_LIST(&original->datastores, &clonechan->datastores, entry);
	}

	ast_autochan_new_channel(clonechan, original);

	clone_variables(original, clonechan);
	/* Presense of ADSI capable CPE follows clone */
	original->adsicpe = clonechan->adsicpe;
	/* Bridge remains the same */
	/* CDR fields remain the same */
	/* XXX What about blocking, softhangup, blocker, and lock and blockproc? XXX */
	/* Application and data remain the same */
	/* Clone exception  becomes real one, as with fdno */
	ast_set_flag(original, ast_test_flag(clonechan, AST_FLAG_EXCEPTION | AST_FLAG_OUTGOING));
	original->fdno = clonechan->fdno;
	/* Schedule context remains the same */
	/* Stream stuff stays the same */
	/* Keep the original state.  The fixup code will need to work with it most likely */

	/*
	 * Just swap the whole structures, nevermind the allocations,
	 * they'll work themselves out.
	 */
	exchange.dialed = original->dialed;
	original->dialed = clonechan->dialed;
	clonechan->dialed = exchange.dialed;

	exchange.caller = original->caller;
	original->caller = clonechan->caller;
	clonechan->caller = exchange.caller;

	exchange.connected = original->connected;
	original->connected = clonechan->connected;
	clonechan->connected = exchange.connected;

	exchange.redirecting = original->redirecting;
	original->redirecting = clonechan->redirecting;
	clonechan->redirecting = exchange.redirecting;

	report_new_callerid(original);

	/* Restore original timing file descriptor */
	ast_channel_set_fd(original, AST_TIMING_FD, original->timingfd);

	/* Our native formats are different now */
	original->nativeformats = clonechan->nativeformats;

	/* Context, extension, priority, app data, jump table,  remain the same */
	/* pvt switches.  pbx stays the same, as does next */

	/* Set the write format */
	ast_set_write_format(original, wformat);

	/* Set the read format */
	ast_set_read_format(original, rformat);

	/* Copy the music class */
	ast_string_field_set(original, musicclass, clonechan->musicclass);

	/* copy over accuntcode and set peeraccount across the bridge */
	ast_string_field_set(original, accountcode, S_OR(clonechan->accountcode, ""));
	if (original->_bridge) {
		/* XXX - should we try to lock original->_bridge here? */
		ast_string_field_set(original->_bridge, peeraccount, S_OR(clonechan->accountcode, ""));
		ast_cel_report_event(original, AST_CEL_BRIDGE_UPDATE, NULL, NULL, NULL);
	}

	ast_debug(1, "Putting channel %s in %s/%s formats\n", original->name,
		ast_getformatname(wformat), ast_getformatname(rformat));

	/* Okay.  Last thing is to let the channel driver know about all this mess, so he
	   can fix up everything as best as possible */
	if (original->tech->fixup) {
		if (original->tech->fixup(clonechan, original)) {
			ast_log(LOG_WARNING, "Channel for type '%s' could not fixup channel %s\n",
				original->tech->type, original->name);
			res = -1;
			goto done;
		}
	} else
		ast_log(LOG_WARNING, "Channel type '%s' does not have a fixup routine (for %s)!  Bad things may happen.\n",
			original->tech->type, original->name);

	/* 
	 * If an indication is currently playing, maintain it on the channel 
	 * that is taking the place of original 
	 *
	 * This is needed because the masquerade is swapping out in the internals
	 * of this channel, and the new channel private data needs to be made
	 * aware of the current visible indication (RINGING, CONGESTION, etc.)
	 */
	if (original->visible_indication) {
		ast_indicate(original, original->visible_indication);
	}

	/* Now, at this point, the "clone" channel is totally F'd up.  We mark it as
	   a zombie so nothing tries to touch it.  If it's already been marked as a
	   zombie, then free it now (since it already is considered invalid). */
	if (ast_test_flag(clonechan, AST_FLAG_ZOMBIE)) {
		ast_debug(1, "Destroying channel clone '%s'\n", clonechan->name);
		ast_channel_unlock(clonechan);
		ast_manager_event(clonechan, EVENT_FLAG_CALL, "Hangup",
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Cause: %d\r\n"
			"Cause-txt: %s\r\n",
			clonechan->name,
			clonechan->uniqueid,
			clonechan->hangupcause,
			ast_cause2str(clonechan->hangupcause)
			);
		clonechan = ast_channel_release(clonechan);
	} else {
		ast_debug(1, "Released clone lock on '%s'\n", clonechan->name);
		ast_set_flag(clonechan, AST_FLAG_ZOMBIE);
		ast_queue_frame(clonechan, &ast_null_frame);
	}

	/* Signal any blocker */
	if (ast_test_flag(original, AST_FLAG_BLOCKING))
		pthread_kill(original->blocker, SIGURG);
	ast_debug(1, "Done Masquerading %s (%d)\n", original->name, original->_state);

	if ((bridged = ast_bridged_channel(original))) {
		ast_channel_lock(bridged);
		ast_indicate(bridged, AST_CONTROL_SRCCHANGE);
		ast_channel_unlock(bridged);
	}

	ast_indicate(original, AST_CONTROL_SRCCHANGE);

done:
	/* it is possible for the clone channel to disappear during this */
	if (clonechan) {
		ast_channel_unlock(original);
		ast_channel_unlock(clonechan);
		ao2_link(channels, clonechan);
		ao2_link(channels, original);
	} else {
		ast_channel_unlock(original);
		ao2_link(channels, original);
	}

	ao2_unlock(channels);

	return 0;
}

void ast_set_callerid(struct ast_channel *chan, const char *cid_num, const char *cid_name, const char *cid_ani)
{
	ast_channel_lock(chan);

	if (cid_num) {
		chan->caller.id.number.valid = 1;
		ast_free(chan->caller.id.number.str);
		chan->caller.id.number.str = ast_strdup(cid_num);
	}
	if (cid_name) {
		chan->caller.id.name.valid = 1;
		ast_free(chan->caller.id.name.str);
		chan->caller.id.name.str = ast_strdup(cid_name);
	}
	if (cid_ani) {
		chan->caller.ani.number.valid = 1;
		ast_free(chan->caller.ani.number.str);
		chan->caller.ani.number.str = ast_strdup(cid_ani);
	}

	report_new_callerid(chan);

	ast_channel_unlock(chan);
}

void ast_channel_set_caller(struct ast_channel *chan, const struct ast_party_caller *caller, const struct ast_set_party_caller *update)
{
	if (&chan->caller == caller) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	ast_party_caller_set(&chan->caller, caller, update);
	ast_channel_unlock(chan);
}

void ast_channel_set_caller_event(struct ast_channel *chan, const struct ast_party_caller *caller, const struct ast_set_party_caller *update)
{
	struct ast_party_caller pre_set;

	if (&chan->caller == caller) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	pre_set = chan->caller;
	ast_party_caller_set(&chan->caller, caller, update);
	if (S_COR(pre_set.id.number.valid, pre_set.id.number.str, NULL)
			!= S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL)
		|| S_COR(pre_set.id.name.valid, pre_set.id.name.str, NULL)
			!= S_COR(chan->caller.id.name.valid, chan->caller.id.name.str, NULL)) {
		/* The caller id name or number changed. */
		report_new_callerid(chan);
	}
	ast_channel_unlock(chan);
}

int ast_setstate(struct ast_channel *chan, enum ast_channel_state state)
{
	int oldstate = chan->_state;
	char name[AST_CHANNEL_NAME], *dashptr;

	if (oldstate == state)
		return 0;

	ast_copy_string(name, chan->name, sizeof(name));
	if ((dashptr = strrchr(name, '-'))) {
		*dashptr = '\0';
	}

	chan->_state = state;

	/* We have to pass AST_DEVICE_UNKNOWN here because it is entirely possible that the channel driver
	 * for this channel is using the callback method for device state. If we pass in an actual state here
	 * we override what they are saying the state is and things go amuck. */
	ast_devstate_changed_literal(AST_DEVICE_UNKNOWN, name);

	/* setstate used to conditionally report Newchannel; this is no more */
	ast_manager_event(chan, EVENT_FLAG_CALL, "Newstate",
		"Channel: %s\r\n"
		"ChannelState: %d\r\n"
		"ChannelStateDesc: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"Uniqueid: %s\r\n",
		chan->name, chan->_state, ast_state2str(chan->_state),
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, ""),
		S_COR(chan->caller.id.name.valid, chan->caller.id.name.str, ""),
		chan->uniqueid);

	return 0;
}

/*! \brief Find bridged channel */
struct ast_channel *ast_bridged_channel(struct ast_channel *chan)
{
	struct ast_channel *bridged;
	bridged = chan->_bridge;
	if (bridged && bridged->tech->bridged_channel)
		bridged = bridged->tech->bridged_channel(chan, bridged);
	return bridged;
}

static void bridge_playfile(struct ast_channel *chan, struct ast_channel *peer, const char *sound, int remain)
{
	int min = 0, sec = 0, check;

	check = ast_autoservice_start(peer);
	if (check)
		return;

	if (remain > 0) {
		if (remain / 60 > 1) {
			min = remain / 60;
			sec = remain % 60;
		} else {
			sec = remain;
		}
	}
	
	if (!strcmp(sound,"timeleft")) {	/* Queue support */
		ast_stream_and_wait(chan, "vm-youhave", "");
		if (min) {
			ast_say_number(chan, min, AST_DIGIT_ANY, chan->language, NULL);
			ast_stream_and_wait(chan, "queue-minutes", "");
		}
		if (sec) {
			ast_say_number(chan, sec, AST_DIGIT_ANY, chan->language, NULL);
			ast_stream_and_wait(chan, "queue-seconds", "");
		}
	} else {
		ast_stream_and_wait(chan, sound, "");
	}

	ast_autoservice_stop(peer);
}

static enum ast_bridge_result ast_generic_bridge(struct ast_channel *c0, struct ast_channel *c1,
						 struct ast_bridge_config *config, struct ast_frame **fo,
						 struct ast_channel **rc)
{
	/* Copy voice back and forth between the two channels. */
	struct ast_channel *cs[3];
	struct ast_frame *f;
	enum ast_bridge_result res = AST_BRIDGE_COMPLETE;
	format_t o0nativeformats;
	format_t o1nativeformats;
	int watch_c0_dtmf;
	int watch_c1_dtmf;
	void *pvt0, *pvt1;
	/* Indicates whether a frame was queued into a jitterbuffer */
	int frame_put_in_jb = 0;
	int jb_in_use;
	int to;
	
	cs[0] = c0;
	cs[1] = c1;
	pvt0 = c0->tech_pvt;
	pvt1 = c1->tech_pvt;
	o0nativeformats = c0->nativeformats;
	o1nativeformats = c1->nativeformats;
	watch_c0_dtmf = config->flags & AST_BRIDGE_DTMF_CHANNEL_0;
	watch_c1_dtmf = config->flags & AST_BRIDGE_DTMF_CHANNEL_1;

	/* Check the need of a jitterbuffer for each channel */
	jb_in_use = ast_jb_do_usecheck(c0, c1);
	if (jb_in_use)
		ast_jb_empty_and_reset(c0, c1);

	ast_poll_channel_add(c0, c1);

	if (config->feature_timer > 0 && ast_tvzero(config->nexteventts)) {
		/* nexteventts is not set when the bridge is not scheduled to
 		 * break, so calculate when the bridge should possibly break
 		 * if a partial feature match timed out */
		config->nexteventts = ast_tvadd(ast_tvnow(), ast_samp2tv(config->feature_timer, 1000));
	}

	for (;;) {
		struct ast_channel *who, *other;

		if ((c0->tech_pvt != pvt0) || (c1->tech_pvt != pvt1) ||
		    (o0nativeformats != c0->nativeformats) ||
		    (o1nativeformats != c1->nativeformats)) {
			/* Check for Masquerade, codec changes, etc */
			res = AST_BRIDGE_RETRY;
			break;
		}
		if (config->nexteventts.tv_sec) {
			to = ast_tvdiff_ms(config->nexteventts, ast_tvnow());
			if (to <= 0) {
				if (config->timelimit && !config->feature_timer && !ast_test_flag(config, AST_FEATURE_WARNING_ACTIVE)) {
					res = AST_BRIDGE_RETRY;
					/* generic bridge ending to play warning */
					ast_set_flag(config, AST_FEATURE_WARNING_ACTIVE);
				} else if (config->feature_timer) {
					/* feature timer expired - make sure we do not play warning */
					ast_clear_flag(config, AST_FEATURE_WARNING_ACTIVE);
					res = AST_BRIDGE_RETRY;
				} else {
					res = AST_BRIDGE_COMPLETE;
				}
				break;
			}
		} else {
			/* If a feature has been started and the bridge is configured to 
 			 * to not break, leave the channel bridge when the feature timer
			 * time has elapsed so the DTMF will be sent to the other side. 
 			 */
			if (!ast_tvzero(config->nexteventts)) {
				int diff = ast_tvdiff_ms(config->nexteventts, ast_tvnow());
				if (diff <= 0) {
					res = AST_BRIDGE_RETRY;
					break;
				}
			}
			to = -1;
		}
		/* Calculate the appropriate max sleep interval - in general, this is the time,
		   left to the closest jb delivery moment */
		if (jb_in_use)
			to = ast_jb_get_when_to_wakeup(c0, c1, to);
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			/* No frame received within the specified timeout - check if we have to deliver now */
			if (jb_in_use)
				ast_jb_get_and_deliver(c0, c1);
			if (c0->_softhangup == AST_SOFTHANGUP_UNBRIDGE || c1->_softhangup == AST_SOFTHANGUP_UNBRIDGE) {
				if (c0->_softhangup == AST_SOFTHANGUP_UNBRIDGE)
					c0->_softhangup = 0;
				if (c1->_softhangup == AST_SOFTHANGUP_UNBRIDGE)
					c1->_softhangup = 0;
				c0->_bridge = c1;
				c1->_bridge = c0;
			}
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			ast_debug(1, "Didn't get a frame from channel: %s\n",who->name);
			break;
		}

		other = (who == c0) ? c1 : c0; /* the 'other' channel */
		/* Try add the frame info the who's bridged channel jitterbuff */
		if (jb_in_use)
			frame_put_in_jb = !ast_jb_put(other, f);

		if ((f->frametype == AST_FRAME_CONTROL) && !(config->flags & AST_BRIDGE_IGNORE_SIGS)) {
			int bridge_exit = 0;

			switch (f->subclass.integer) {
			case AST_CONTROL_AOC:
				ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				break;
			case AST_CONTROL_REDIRECTING:
				if (ast_channel_redirecting_macro(who, other, f, other == c0, 1)) {
					ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				}
				break;
			case AST_CONTROL_CONNECTED_LINE:
				if (ast_channel_connected_line_macro(who, other, f, other == c0, 1)) {
					ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				}
				break;
			case AST_CONTROL_HOLD:
			case AST_CONTROL_UNHOLD:
			case AST_CONTROL_VIDUPDATE:
			case AST_CONTROL_SRCUPDATE:
			case AST_CONTROL_SRCCHANGE:
			case AST_CONTROL_T38_PARAMETERS:
				ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				if (jb_in_use) {
					ast_jb_empty_and_reset(c0, c1);
				}
				break;
			default:
				*fo = f;
				*rc = who;
				bridge_exit = 1;
				ast_debug(1, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass.integer, who->name);
				break;
			}
			if (bridge_exit)
				break;
		}
		if ((f->frametype == AST_FRAME_VOICE) ||
		    (f->frametype == AST_FRAME_DTMF_BEGIN) ||
		    (f->frametype == AST_FRAME_DTMF) ||
		    (f->frametype == AST_FRAME_VIDEO) ||
		    (f->frametype == AST_FRAME_IMAGE) ||
		    (f->frametype == AST_FRAME_HTML) ||
		    (f->frametype == AST_FRAME_MODEM) ||
		    (f->frametype == AST_FRAME_TEXT)) {
			/* monitored dtmf causes exit from bridge */
			int monitored_source = (who == c0) ? watch_c0_dtmf : watch_c1_dtmf;

			if (monitored_source &&
				(f->frametype == AST_FRAME_DTMF_END ||
				f->frametype == AST_FRAME_DTMF_BEGIN)) {
				*fo = f;
				*rc = who;
				ast_debug(1, "Got DTMF %s on channel (%s)\n", 
					f->frametype == AST_FRAME_DTMF_END ? "end" : "begin",
					who->name);

				break;
			}
			/* Write immediately frames, not passed through jb */
			if (!frame_put_in_jb)
				ast_write(other, f);
				
			/* Check if we have to deliver now */
			if (jb_in_use)
				ast_jb_get_and_deliver(c0, c1);
		}
		/* XXX do we want to pass on also frames not matched above ? */
		ast_frfree(f);

#ifndef HAVE_EPOLL
		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
#endif
	}

	ast_poll_channel_del(c0, c1);

	return res;
}

/*! \brief Bridge two channels together (early) */
int ast_channel_early_bridge(struct ast_channel *c0, struct ast_channel *c1)
{
	/* Make sure we can early bridge, if not error out */
	if (!c0->tech->early_bridge || (c1 && (!c1->tech->early_bridge || c0->tech->early_bridge != c1->tech->early_bridge)))
		return -1;

	return c0->tech->early_bridge(c0, c1);
}

/*! \brief Send manager event for bridge link and unlink events.
 * \param onoff Link/Unlinked 
 * \param type 1 for core, 2 for native
 * \param c0 first channel in bridge
 * \param c1 second channel in bridge
*/
static void manager_bridge_event(int onoff, int type, struct ast_channel *c0, struct ast_channel *c1)
{
	struct ast_channel *chans[2] = { c0, c1 };
	ast_manager_event_multichan(EVENT_FLAG_CALL, "Bridge", 2, chans,
		"Bridgestate: %s\r\n"
		"Bridgetype: %s\r\n"
		"Channel1: %s\r\n"
		"Channel2: %s\r\n"
		"Uniqueid1: %s\r\n"
		"Uniqueid2: %s\r\n"
		"CallerID1: %s\r\n"
		"CallerID2: %s\r\n",
		onoff ? "Link" : "Unlink",
		type == 1 ? "core" : "native",
		c0->name, c1->name,
		c0->uniqueid, c1->uniqueid,
		S_COR(c0->caller.id.number.valid, c0->caller.id.number.str, ""),
		S_COR(c1->caller.id.number.valid, c1->caller.id.number.str, ""));
}

static void update_bridge_vars(struct ast_channel *c0, struct ast_channel *c1)
{
	const char *c0_name;
	const char *c1_name;
	const char *c0_pvtid = NULL;
	const char *c1_pvtid = NULL;

	ast_channel_lock(c1);
	c1_name = ast_strdupa(c1->name);
	if (c1->tech->get_pvt_uniqueid) {
		c1_pvtid = ast_strdupa(c1->tech->get_pvt_uniqueid(c1));
	}
	ast_channel_unlock(c1);

	ast_channel_lock(c0);
	if (!ast_strlen_zero(pbx_builtin_getvar_helper(c0, "BRIDGEPEER"))) {
		pbx_builtin_setvar_helper(c0, "BRIDGEPEER", c1_name);
	}
	if (c1_pvtid) {
		pbx_builtin_setvar_helper(c0, "BRIDGEPVTCALLID", c1_pvtid);
	}
	c0_name = ast_strdupa(c0->name);
	if (c0->tech->get_pvt_uniqueid) {
		c0_pvtid = ast_strdupa(c0->tech->get_pvt_uniqueid(c0));
	}
	ast_channel_unlock(c0);

	ast_channel_lock(c1);
	if (!ast_strlen_zero(pbx_builtin_getvar_helper(c1, "BRIDGEPEER"))) {
		pbx_builtin_setvar_helper(c1, "BRIDGEPEER", c0_name);
	}
	if (c0_pvtid) {
		pbx_builtin_setvar_helper(c1, "BRIDGEPVTCALLID", c0_pvtid);
	}
	ast_channel_unlock(c1);
}

static void bridge_play_sounds(struct ast_channel *c0, struct ast_channel *c1)
{
	const char *s, *sound;

	/* See if we need to play an audio file to any side of the bridge */

	ast_channel_lock(c0);
	if ((s = pbx_builtin_getvar_helper(c0, "BRIDGE_PLAY_SOUND"))) {
		sound = ast_strdupa(s);
		ast_channel_unlock(c0);
		bridge_playfile(c0, c1, sound, 0);
		pbx_builtin_setvar_helper(c0, "BRIDGE_PLAY_SOUND", NULL);
	} else {
		ast_channel_unlock(c0);
	}

	ast_channel_lock(c1);
	if ((s = pbx_builtin_getvar_helper(c1, "BRIDGE_PLAY_SOUND"))) {
		sound = ast_strdupa(s);
		ast_channel_unlock(c1);
		bridge_playfile(c1, c0, sound, 0);
		pbx_builtin_setvar_helper(c1, "BRIDGE_PLAY_SOUND", NULL);
	} else {
		ast_channel_unlock(c1);
	}
}

/*! \brief Bridge two channels together */
enum ast_bridge_result ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1,
					  struct ast_bridge_config *config, struct ast_frame **fo, struct ast_channel **rc)
{
	struct ast_channel *who = NULL, *chans[2] = { c0, c1 };
	enum ast_bridge_result res = AST_BRIDGE_COMPLETE;
	format_t o0nativeformats;
	format_t o1nativeformats;
	long time_left_ms=0;
	char caller_warning = 0;
	char callee_warning = 0;

	if (c0->_bridge) {
		ast_log(LOG_WARNING, "%s is already in a bridge with %s\n",
			c0->name, c0->_bridge->name);
		return -1;
	}
	if (c1->_bridge) {
		ast_log(LOG_WARNING, "%s is already in a bridge with %s\n",
			c1->name, c1->_bridge->name);
		return -1;
	}
	
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c0, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c0) ||
	    ast_test_flag(c1, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c1))
		return -1;

	*fo = NULL;

	if (ast_tvzero(config->start_time)) {
		config->start_time = ast_tvnow();
		if (config->start_sound) {
			if (caller_warning) {
				bridge_playfile(c0, c1, config->start_sound, config->timelimit / 1000);
			}
			if (callee_warning) {
				bridge_playfile(c1, c0, config->start_sound, config->timelimit / 1000);
			}
		}
	}

	/* Keep track of bridge */
	c0->_bridge = c1;
	c1->_bridge = c0;

	ast_set_owners_and_peers(c0, c1);

	o0nativeformats = c0->nativeformats;
	o1nativeformats = c1->nativeformats;

	if (config->feature_timer && !ast_tvzero(config->nexteventts)) {
		config->nexteventts = ast_tvadd(config->feature_start_time, ast_samp2tv(config->feature_timer, 1000));
	} else if (config->timelimit) {
		time_left_ms = config->timelimit - ast_tvdiff_ms(ast_tvnow(), config->start_time);
		caller_warning = ast_test_flag(&config->features_caller, AST_FEATURE_PLAY_WARNING);
		callee_warning = ast_test_flag(&config->features_callee, AST_FEATURE_PLAY_WARNING);
		config->nexteventts = ast_tvadd(config->start_time, ast_samp2tv(config->timelimit, 1000));
		if ((caller_warning || callee_warning) && config->play_warning) {
			long next_warn = config->play_warning;
			if (time_left_ms < config->play_warning) {
				/* At least one warning was played, which means we are returning after feature */
				long warns_passed = (config->play_warning - time_left_ms) / config->warning_freq;
				/* It is 'warns_passed * warning_freq' NOT '(warns_passed + 1) * warning_freq',
					because nexteventts will be updated once again in the 'if (!to)' block */
				next_warn = config->play_warning - warns_passed * config->warning_freq;
			}
			config->nexteventts = ast_tvsub(config->nexteventts, ast_samp2tv(next_warn, 1000));
		}
	} else {
		config->nexteventts.tv_sec = 0;
		config->nexteventts.tv_usec = 0;
	}

	if (!c0->tech->send_digit_begin)
		ast_set_flag(c1, AST_FLAG_END_DTMF_ONLY);
	if (!c1->tech->send_digit_begin)
		ast_set_flag(c0, AST_FLAG_END_DTMF_ONLY);
	manager_bridge_event(1, 1, c0, c1);

	/* Before we enter in and bridge these two together tell them both the source of audio has changed */
	ast_indicate(c0, AST_CONTROL_SRCCHANGE);
	ast_indicate(c1, AST_CONTROL_SRCCHANGE);

	for (/* ever */;;) {
		struct timeval now = { 0, };
		int to;

		to = -1;

		if (!ast_tvzero(config->nexteventts)) {
			now = ast_tvnow();
			to = ast_tvdiff_ms(config->nexteventts, now);
			if (to <= 0) {
				if (!config->timelimit) {
					res = AST_BRIDGE_COMPLETE;
					break;
				}
				to = 0;
			}
		}

		if (config->timelimit) {
			time_left_ms = config->timelimit - ast_tvdiff_ms(now, config->start_time);
			if (time_left_ms < to)
				to = time_left_ms;

			if (time_left_ms <= 0) {
				if (caller_warning && config->end_sound)
					bridge_playfile(c0, c1, config->end_sound, 0);
				if (callee_warning && config->end_sound)
					bridge_playfile(c1, c0, config->end_sound, 0);
				*fo = NULL;
				if (who)
					*rc = who;
				res = 0;
				break;
			}

			if (!to) {
				if (time_left_ms >= 5000 && config->warning_sound && config->play_warning && ast_test_flag(config, AST_FEATURE_WARNING_ACTIVE)) {
					int t = (time_left_ms + 500) / 1000; /* round to nearest second */
					if (caller_warning)
						bridge_playfile(c0, c1, config->warning_sound, t);
					if (callee_warning)
						bridge_playfile(c1, c0, config->warning_sound, t);
				}

				if (config->warning_freq && (time_left_ms > (config->warning_freq + 5000))) {
					config->nexteventts = ast_tvadd(config->nexteventts, ast_samp2tv(config->warning_freq, 1000));
				} else {
					config->nexteventts = ast_tvadd(config->start_time, ast_samp2tv(config->timelimit, 1000));
				}
			}
			ast_clear_flag(config, AST_FEATURE_WARNING_ACTIVE);
		}

		if (c0->_softhangup == AST_SOFTHANGUP_UNBRIDGE || c1->_softhangup == AST_SOFTHANGUP_UNBRIDGE) {
			if (c0->_softhangup == AST_SOFTHANGUP_UNBRIDGE)
				c0->_softhangup = 0;
			if (c1->_softhangup == AST_SOFTHANGUP_UNBRIDGE)
				c1->_softhangup = 0;
			c0->_bridge = c1;
			c1->_bridge = c0;
			ast_debug(1, "Unbridge signal received. Ending native bridge.\n");
			continue;
		}

		/* Stop if we're a zombie or need a soft hangup */
		if (ast_test_flag(c0, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c0) ||
		    ast_test_flag(c1, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c1)) {
			*fo = NULL;
			if (who)
				*rc = who;
			res = 0;
			ast_debug(1, "Bridge stops because we're zombie or need a soft hangup: c0=%s, c1=%s, flags: %s,%s,%s,%s\n",
				c0->name, c1->name,
				ast_test_flag(c0, AST_FLAG_ZOMBIE) ? "Yes" : "No",
				ast_check_hangup(c0) ? "Yes" : "No",
				ast_test_flag(c1, AST_FLAG_ZOMBIE) ? "Yes" : "No",
				ast_check_hangup(c1) ? "Yes" : "No");
			break;
		}

		update_bridge_vars(c0, c1);

		bridge_play_sounds(c0, c1);

		if (c0->tech->bridge &&
			/* if < 1 ms remains use generic bridging for accurate timing */
			(!config->timelimit || to > 1000 || to == 0) &&
		    (c0->tech->bridge == c1->tech->bridge) &&
		    !c0->monitor && !c1->monitor &&
		    !c0->audiohooks && !c1->audiohooks &&
		    !c0->masq && !c0->masqr && !c1->masq && !c1->masqr) {
			int timeoutms = to - 1000 > 0 ? to - 1000 : to;
			/* Looks like they share a bridge method and nothing else is in the way */
			ast_set_flag(c0, AST_FLAG_NBRIDGE);
			ast_set_flag(c1, AST_FLAG_NBRIDGE);
			if ((res = c0->tech->bridge(c0, c1, config->flags, fo, rc, timeoutms)) == AST_BRIDGE_COMPLETE) {
				ast_manager_event_multichan(EVENT_FLAG_CALL, "Unlink", 2, chans,
					"Channel1: %s\r\n"
					"Channel2: %s\r\n"
					"Uniqueid1: %s\r\n"
					"Uniqueid2: %s\r\n"
					"CallerID1: %s\r\n"
					"CallerID2: %s\r\n",
					c0->name, c1->name,
					c0->uniqueid, c1->uniqueid,
					S_COR(c0->caller.id.number.valid, c0->caller.id.number.str, "<unknown>"),
					S_COR(c1->caller.id.number.valid, c1->caller.id.number.str, "<unknown>"));

				ast_debug(1, "Returning from native bridge, channels: %s, %s\n", c0->name, c1->name);

				ast_clear_flag(c0, AST_FLAG_NBRIDGE);
				ast_clear_flag(c1, AST_FLAG_NBRIDGE);

				if (c0->_softhangup == AST_SOFTHANGUP_UNBRIDGE || c1->_softhangup == AST_SOFTHANGUP_UNBRIDGE)
					continue;

				c0->_bridge = NULL;
				c1->_bridge = NULL;
				return res;
			} else {
				ast_clear_flag(c0, AST_FLAG_NBRIDGE);
				ast_clear_flag(c1, AST_FLAG_NBRIDGE);
			}
			switch (res) {
			case AST_BRIDGE_RETRY:
				if (config->play_warning) {
					ast_set_flag(config, AST_FEATURE_WARNING_ACTIVE);
				}
				continue;
			default:
				ast_verb(3, "Native bridging %s and %s ended\n", c0->name, c1->name);
				/* fallthrough */
			case AST_BRIDGE_FAILED_NOWARN:
				break;
			}
		}

		if (((c0->writeformat != c1->readformat) || (c0->readformat != c1->writeformat) ||
		    (c0->nativeformats != o0nativeformats) || (c1->nativeformats != o1nativeformats)) &&
		    !(c0->generator || c1->generator)) {
			if (ast_channel_make_compatible(c0, c1)) {
				ast_log(LOG_WARNING, "Can't make %s and %s compatible\n", c0->name, c1->name);
				manager_bridge_event(0, 1, c0, c1);
				return AST_BRIDGE_FAILED;
			}
			o0nativeformats = c0->nativeformats;
			o1nativeformats = c1->nativeformats;
		}

		update_bridge_vars(c0, c1);

		res = ast_generic_bridge(c0, c1, config, fo, rc);
		if (res != AST_BRIDGE_RETRY) {
			break;
		} else if (config->feature_timer) {
			/* feature timer expired but has not been updated, sending to ast_bridge_call to do so */
			break;
		}
	}

	ast_clear_flag(c0, AST_FLAG_END_DTMF_ONLY);
	ast_clear_flag(c1, AST_FLAG_END_DTMF_ONLY);

	/* Now that we have broken the bridge the source will change yet again */
	ast_indicate(c0, AST_CONTROL_SRCUPDATE);
	ast_indicate(c1, AST_CONTROL_SRCUPDATE);

	c0->_bridge = NULL;
	c1->_bridge = NULL;

	ast_manager_event_multichan(EVENT_FLAG_CALL, "Unlink", 2, chans,
		"Channel1: %s\r\n"
		"Channel2: %s\r\n"
		"Uniqueid1: %s\r\n"
		"Uniqueid2: %s\r\n"
		"CallerID1: %s\r\n"
		"CallerID2: %s\r\n",
		c0->name, c1->name,
		c0->uniqueid, c1->uniqueid,
		S_COR(c0->caller.id.number.valid, c0->caller.id.number.str, "<unknown>"),
		S_COR(c1->caller.id.number.valid, c1->caller.id.number.str, "<unknown>"));
	ast_debug(1, "Bridge stops bridging channels %s and %s\n", c0->name, c1->name);

	return res;
}

/*! \brief Sets an option on a channel */
int ast_channel_setoption(struct ast_channel *chan, int option, void *data, int datalen, int block)
{
	if (!chan->tech->setoption) {
		errno = ENOSYS;
		return -1;
	}

	if (block)
		ast_log(LOG_ERROR, "XXX Blocking not implemented yet XXX\n");

	return chan->tech->setoption(chan, option, data, datalen);
}

int ast_channel_queryoption(struct ast_channel *chan, int option, void *data, int *datalen, int block)
{
	if (!chan->tech->queryoption) {
		errno = ENOSYS;
		return -1;
	}

	if (block)
		ast_log(LOG_ERROR, "XXX Blocking not implemented yet XXX\n");

	return chan->tech->queryoption(chan, option, data, datalen);
}

struct tonepair_def {
	int freq1;
	int freq2;
	int duration;
	int vol;
};

struct tonepair_state {
	int fac1;
	int fac2;
	int v1_1;
	int v2_1;
	int v3_1;
	int v1_2;
	int v2_2;
	int v3_2;
	format_t origwfmt;
	int pos;
	int duration;
	int modulate;
	struct ast_frame f;
	unsigned char offset[AST_FRIENDLY_OFFSET];
	short data[4000];
};

static void tonepair_release(struct ast_channel *chan, void *params)
{
	struct tonepair_state *ts = params;

	if (chan)
		ast_set_write_format(chan, ts->origwfmt);
	ast_free(ts);
}

static void *tonepair_alloc(struct ast_channel *chan, void *params)
{
	struct tonepair_state *ts;
	struct tonepair_def *td = params;

	if (!(ts = ast_calloc(1, sizeof(*ts))))
		return NULL;
	ts->origwfmt = chan->writeformat;
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set '%s' to signed linear format (write)\n", chan->name);
		tonepair_release(NULL, ts);
		ts = NULL;
	} else {
		ts->fac1 = 2.0 * cos(2.0 * M_PI * (td->freq1 / 8000.0)) * 32768.0;
		ts->v1_1 = 0;
		ts->v2_1 = sin(-4.0 * M_PI * (td->freq1 / 8000.0)) * td->vol;
		ts->v3_1 = sin(-2.0 * M_PI * (td->freq1 / 8000.0)) * td->vol;
		ts->v2_1 = 0;
		ts->fac2 = 2.0 * cos(2.0 * M_PI * (td->freq2 / 8000.0)) * 32768.0;
		ts->v2_2 = sin(-4.0 * M_PI * (td->freq2 / 8000.0)) * td->vol;
		ts->v3_2 = sin(-2.0 * M_PI * (td->freq2 / 8000.0)) * td->vol;
		ts->duration = td->duration;
		ts->modulate = 0;
	}
	/* Let interrupts interrupt :) */
	ast_set_flag(chan, AST_FLAG_WRITE_INT);
	return ts;
}

static int tonepair_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct tonepair_state *ts = data;
	int x;

	/* we need to prepare a frame with 16 * timelen samples as we're
	 * generating SLIN audio
	 */
	len = samples * 2;

	if (len > sizeof(ts->data) / 2 - 1) {
		ast_log(LOG_WARNING, "Can't generate that much data!\n");
		return -1;
	}
	memset(&ts->f, 0, sizeof(ts->f));
 	for (x=0;x<len/2;x++) {
 		ts->v1_1 = ts->v2_1;
 		ts->v2_1 = ts->v3_1;
 		ts->v3_1 = (ts->fac1 * ts->v2_1 >> 15) - ts->v1_1;
 		
 		ts->v1_2 = ts->v2_2;
 		ts->v2_2 = ts->v3_2;
 		ts->v3_2 = (ts->fac2 * ts->v2_2 >> 15) - ts->v1_2;
 		if (ts->modulate) {
 			int p;
 			p = ts->v3_2 - 32768;
 			if (p < 0) p = -p;
 			p = ((p * 9) / 10) + 1;
 			ts->data[x] = (ts->v3_1 * p) >> 15;
 		} else
 			ts->data[x] = ts->v3_1 + ts->v3_2; 
 	}
	ts->f.frametype = AST_FRAME_VOICE;
	ts->f.subclass.codec = AST_FORMAT_SLINEAR;
	ts->f.datalen = len;
	ts->f.samples = samples;
	ts->f.offset = AST_FRIENDLY_OFFSET;
	ts->f.data.ptr = ts->data;
	ast_write(chan, &ts->f);
	ts->pos += x;
	if (ts->duration > 0) {
		if (ts->pos >= ts->duration * 8)
			return -1;
	}
	return 0;
}

static struct ast_generator tonepair = {
	alloc: tonepair_alloc,
	release: tonepair_release,
	generate: tonepair_generator,
};

int ast_tonepair_start(struct ast_channel *chan, int freq1, int freq2, int duration, int vol)
{
	struct tonepair_def d = { 0, };

	d.freq1 = freq1;
	d.freq2 = freq2;
	d.duration = duration;
	d.vol = (vol < 1) ? 8192 : vol; /* force invalid to 8192 */
	if (ast_activate_generator(chan, &tonepair, &d))
		return -1;
	return 0;
}

void ast_tonepair_stop(struct ast_channel *chan)
{
	ast_deactivate_generator(chan);
}

int ast_tonepair(struct ast_channel *chan, int freq1, int freq2, int duration, int vol)
{
	int res;

	if ((res = ast_tonepair_start(chan, freq1, freq2, duration, vol)))
		return res;

	/* Give us some wiggle room */
	while (chan->generatordata && ast_waitfor(chan, 100) >= 0) {
		struct ast_frame *f = ast_read(chan);
		if (f)
			ast_frfree(f);
		else
			return -1;
	}
	return 0;
}

ast_group_t ast_get_group(const char *s)
{
	char *piece;
	char *c;
	int start=0, finish=0, x;
	ast_group_t group = 0;

	if (ast_strlen_zero(s))
		return 0;

	c = ast_strdupa(s);
	
	while ((piece = strsep(&c, ","))) {
		if (sscanf(piece, "%30d-%30d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(piece, "%30d", &start)) {
			/* Just one */
			finish = start;
		} else {
			ast_log(LOG_ERROR, "Syntax error parsing group configuration '%s' at '%s'. Ignoring.\n", s, piece);
			continue;
		}
		for (x = start; x <= finish; x++) {
			if ((x > 63) || (x < 0)) {
				ast_log(LOG_WARNING, "Ignoring invalid group %d (maximum group is 63)\n", x);
			} else
				group |= ((ast_group_t) 1 << x);
		}
	}
	return group;
}

static int (*ast_moh_start_ptr)(struct ast_channel *, const char *, const char *) = NULL;
static void (*ast_moh_stop_ptr)(struct ast_channel *) = NULL;
static void (*ast_moh_cleanup_ptr)(struct ast_channel *) = NULL;

void ast_install_music_functions(int (*start_ptr)(struct ast_channel *, const char *, const char *),
				 void (*stop_ptr)(struct ast_channel *),
				 void (*cleanup_ptr)(struct ast_channel *))
{
	ast_moh_start_ptr = start_ptr;
	ast_moh_stop_ptr = stop_ptr;
	ast_moh_cleanup_ptr = cleanup_ptr;
}

void ast_uninstall_music_functions(void)
{
	ast_moh_start_ptr = NULL;
	ast_moh_stop_ptr = NULL;
	ast_moh_cleanup_ptr = NULL;
}

/*! \brief Turn on music on hold on a given channel */
int ast_moh_start(struct ast_channel *chan, const char *mclass, const char *interpclass)
{
	if (ast_moh_start_ptr)
		return ast_moh_start_ptr(chan, mclass, interpclass);

	ast_verb(3, "Music class %s requested but no musiconhold loaded.\n", mclass ? mclass : (interpclass ? interpclass : "default"));

	return 0;
}

/*! \brief Turn off music on hold on a given channel */
void ast_moh_stop(struct ast_channel *chan)
{
	if (ast_moh_stop_ptr)
		ast_moh_stop_ptr(chan);
}

void ast_moh_cleanup(struct ast_channel *chan)
{
	if (ast_moh_cleanup_ptr)
		ast_moh_cleanup_ptr(chan);
}

static int ast_channel_hash_cb(const void *obj, const int flags)
{
	const struct ast_channel *chan = obj;

	/* If the name isn't set, return 0 so that the ao2_find() search will
	 * start in the first bucket. */
	if (ast_strlen_zero(chan->name)) {
		return 0;
	}

	return ast_str_case_hash(chan->name);
}

int ast_plc_reload(void)
{
	struct ast_variable *var;
	struct ast_flags config_flags = { 0 };
	struct ast_config *cfg = ast_config_load("codecs.conf", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID)
		return 0;
	for (var = ast_variable_browse(cfg, "plc"); var; var = var->next) {
		if (!strcasecmp(var->name, "genericplc")) {
			ast_set2_flag(&ast_options, ast_true(var->value), AST_OPT_FLAG_GENERIC_PLC);
		}
	}
	ast_config_destroy(cfg);
	return 0;
}

/*!
 * \internal
 * \brief Implements the channels provider.
 */
static int data_channels_provider_handler(const struct ast_data_search *search,
	struct ast_data *root)
{
	struct ast_channel *c;
	struct ast_channel_iterator *iter = NULL;
	struct ast_data *data_channel;

	for (iter = ast_channel_iterator_all_new();
		iter && (c = ast_channel_iterator_next(iter)); ast_channel_unref(c)) {
		ast_channel_lock(c);

		data_channel = ast_data_add_node(root, "channel");
		if (!data_channel) {
			ast_channel_unlock(c);
			continue;
		}

		if (ast_channel_data_add_structure(data_channel, c, 1) < 0) {
			ast_log(LOG_ERROR, "Unable to add channel structure for channel: %s\n", c->name);
		}

		ast_channel_unlock(c);

		if (!ast_data_search_match(search, data_channel)) {
			ast_data_remove_node(root, data_channel);
		}
	}
	if (iter) {
		ast_channel_iterator_destroy(iter);
	}

	return 0;
}

/*!
 * \internal
 * \brief Implements the channeltypes provider.
 */
static int data_channeltypes_provider_handler(const struct ast_data_search *search,
	struct ast_data *data_root)
{
	struct chanlist *cl;
	struct ast_data *data_type;

	AST_RWLIST_RDLOCK(&backends);
	AST_RWLIST_TRAVERSE(&backends, cl, list) {
		data_type = ast_data_add_node(data_root, "type");
		if (!data_type) {
			continue;
		}
		ast_data_add_str(data_type, "name", cl->tech->type);
		ast_data_add_str(data_type, "description", cl->tech->description);
		ast_data_add_bool(data_type, "devicestate", cl->tech->devicestate ? 1 : 0);
		ast_data_add_bool(data_type, "indications", cl->tech->indicate ? 1 : 0);
		ast_data_add_bool(data_type, "transfer", cl->tech->transfer ? 1 : 0);
		ast_data_add_bool(data_type, "send_digit_begin", cl->tech->send_digit_begin ? 1 : 0);
		ast_data_add_bool(data_type, "send_digit_end", cl->tech->send_digit_end ? 1 : 0);
		ast_data_add_bool(data_type, "call", cl->tech->call ? 1 : 0);
		ast_data_add_bool(data_type, "hangup", cl->tech->hangup ? 1 : 0);
		ast_data_add_bool(data_type, "answer", cl->tech->answer ? 1 : 0);
		ast_data_add_bool(data_type, "read", cl->tech->read ? 1 : 0);
		ast_data_add_bool(data_type, "write", cl->tech->write ? 1 : 0);
		ast_data_add_bool(data_type, "send_text", cl->tech->send_text ? 1 : 0);
		ast_data_add_bool(data_type, "send_image", cl->tech->send_image ? 1 : 0);
		ast_data_add_bool(data_type, "send_html", cl->tech->send_html ? 1 : 0);
		ast_data_add_bool(data_type, "exception", cl->tech->exception ? 1 : 0);
		ast_data_add_bool(data_type, "bridge", cl->tech->bridge ? 1 : 0);
		ast_data_add_bool(data_type, "early_bridge", cl->tech->early_bridge ? 1 : 0);
		ast_data_add_bool(data_type, "fixup", cl->tech->fixup ? 1 : 0);
		ast_data_add_bool(data_type, "setoption", cl->tech->setoption ? 1 : 0);
		ast_data_add_bool(data_type, "queryoption", cl->tech->queryoption ? 1 : 0);
		ast_data_add_bool(data_type, "write_video", cl->tech->write_video ? 1 : 0);
		ast_data_add_bool(data_type, "write_text", cl->tech->write_text ? 1 : 0);
		ast_data_add_bool(data_type, "bridged_channel", cl->tech->bridged_channel ? 1 : 0);
		ast_data_add_bool(data_type, "func_channel_read", cl->tech->func_channel_read ? 1 : 0);
		ast_data_add_bool(data_type, "func_channel_write", cl->tech->func_channel_write ? 1 : 0);
		ast_data_add_bool(data_type, "get_base_channel", cl->tech->get_base_channel ? 1 : 0);
		ast_data_add_bool(data_type, "set_base_channel", cl->tech->set_base_channel ? 1 : 0);
		ast_data_add_bool(data_type, "get_pvt_uniqueid", cl->tech->get_pvt_uniqueid ? 1 : 0);
		ast_data_add_bool(data_type, "cc_callback", cl->tech->cc_callback ? 1 : 0);

		ast_data_add_codecs(data_type, "capabilities", cl->tech->capabilities);

		if (!ast_data_search_match(search, data_type)) {
			ast_data_remove_node(data_root, data_type);
		}
	}
	AST_RWLIST_UNLOCK(&backends);

	return 0;
}

/*!
 * \internal
 * \brief /asterisk/core/channels provider.
 */
static const struct ast_data_handler channels_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = data_channels_provider_handler
};

/*!
 * \internal
 * \brief /asterisk/core/channeltypes provider.
 */
static const struct ast_data_handler channeltypes_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = data_channeltypes_provider_handler
};

static const struct ast_data_entry channel_providers[] = {
	AST_DATA_ENTRY("/asterisk/core/channels", &channels_provider),
	AST_DATA_ENTRY("/asterisk/core/channeltypes", &channeltypes_provider),
};

void ast_channels_init(void)
{
	channels = ao2_container_alloc(NUM_CHANNEL_BUCKETS,
			ast_channel_hash_cb, ast_channel_cmp_cb);

	ast_cli_register_multiple(cli_channel, ARRAY_LEN(cli_channel));

	ast_data_register_multiple_core(channel_providers, ARRAY_LEN(channel_providers));

	ast_plc_reload();
}

/*! \brief Print call group and pickup group ---*/
char *ast_print_group(char *buf, int buflen, ast_group_t group)
{
	unsigned int i;
	int first = 1;
	char num[3];

	buf[0] = '\0';
	
	if (!group)	/* Return empty string if no group */
		return buf;

	for (i = 0; i <= 63; i++) {	/* Max group is 63 */
		if (group & ((ast_group_t) 1 << i)) {
	   		if (!first) {
				strncat(buf, ", ", buflen - strlen(buf) - 1);
			} else {
				first = 0;
	  		}
			snprintf(num, sizeof(num), "%u", i);
			strncat(buf, num, buflen - strlen(buf) - 1);
		}
	}
	return buf;
}

void ast_set_variables(struct ast_channel *chan, struct ast_variable *vars)
{
	struct ast_variable *cur;

	for (cur = vars; cur; cur = cur->next)
		pbx_builtin_setvar_helper(chan, cur->name, cur->value);	
}

static void *silence_generator_alloc(struct ast_channel *chan, void *data)
{
	/* just store the data pointer in the channel structure */
	return data;
}

static void silence_generator_release(struct ast_channel *chan, void *data)
{
	/* nothing to do */
}

static int silence_generator_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	short buf[samples];
	struct ast_frame frame = {
		.frametype = AST_FRAME_VOICE,
		.subclass.codec = AST_FORMAT_SLINEAR,
		.data.ptr = buf,
		.samples = samples,
		.datalen = sizeof(buf),
	};

	memset(buf, 0, sizeof(buf));

	if (ast_write(chan, &frame))
		return -1;

	return 0;
}

static struct ast_generator silence_generator = {
	.alloc = silence_generator_alloc,
	.release = silence_generator_release,
	.generate = silence_generator_generate,
};

struct ast_silence_generator {
	int old_write_format;
};

struct ast_silence_generator *ast_channel_start_silence_generator(struct ast_channel *chan)
{
	struct ast_silence_generator *state;

	if (!(state = ast_calloc(1, sizeof(*state)))) {
		return NULL;
	}

	state->old_write_format = chan->writeformat;

	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could not set write format to SLINEAR\n");
		ast_free(state);
		return NULL;
	}

	ast_activate_generator(chan, &silence_generator, state);

	ast_debug(1, "Started silence generator on '%s'\n", chan->name);

	return state;
}

void ast_channel_stop_silence_generator(struct ast_channel *chan, struct ast_silence_generator *state)
{
	if (!state)
		return;

	ast_deactivate_generator(chan);

	ast_debug(1, "Stopped silence generator on '%s'\n", chan->name);

	if (ast_set_write_format(chan, state->old_write_format) < 0)
		ast_log(LOG_ERROR, "Could not return write format to its original state\n");

	ast_free(state);
}


/*! \ brief Convert channel reloadreason (ENUM) to text string for manager event */
const char *channelreloadreason2txt(enum channelreloadreason reason)
{
	switch (reason) {
	case CHANNEL_MODULE_LOAD:
		return "LOAD (Channel module load)";

	case CHANNEL_MODULE_RELOAD:
		return "RELOAD (Channel module reload)";

	case CHANNEL_CLI_RELOAD:
		return "CLIRELOAD (Channel module reload by CLI command)";

	default:
		return "MANAGERRELOAD (Channel module reload by manager)";
	}
};

/*
 * Wrappers for various ast_say_*() functions that call the full version
 * of the same functions.
 * The proper place would be say.c, but that file is optional and one
 * must be able to build asterisk even without it (using a loadable 'say'
 * implementation that only supplies the 'full' version of the functions.
 */

int ast_say_number(struct ast_channel *chan, int num,
	const char *ints, const char *language, const char *options)
{
	return ast_say_number_full(chan, num, ints, language, options, -1, -1);
}

int ast_say_enumeration(struct ast_channel *chan, int num,
	const char *ints, const char *language, const char *options)
{
	return ast_say_enumeration_full(chan, num, ints, language, options, -1, -1);
}

int ast_say_digits(struct ast_channel *chan, int num,
	const char *ints, const char *lang)
{
	return ast_say_digits_full(chan, num, ints, lang, -1, -1);
}

int ast_say_digit_str(struct ast_channel *chan, const char *str,
	const char *ints, const char *lang)
{
	return ast_say_digit_str_full(chan, str, ints, lang, -1, -1);
}

int ast_say_character_str(struct ast_channel *chan, const char *str,
	const char *ints, const char *lang)
{
	return ast_say_character_str_full(chan, str, ints, lang, -1, -1);
}

int ast_say_phonetic_str(struct ast_channel *chan, const char *str,
	const char *ints, const char *lang)
{
	return ast_say_phonetic_str_full(chan, str, ints, lang, -1, -1);
}

int ast_say_digits_full(struct ast_channel *chan, int num,
	const char *ints, const char *lang, int audiofd, int ctrlfd)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "%d", num);

	return ast_say_digit_str_full(chan, buf, ints, lang, audiofd, ctrlfd);
}

void ast_connected_line_copy_from_caller(struct ast_party_connected_line *dest, const struct ast_party_caller *src)
{
	ast_party_id_copy(&dest->id, &src->id);
	ast_party_id_copy(&dest->ani, &src->ani);
	dest->ani2 = src->ani2;
}

void ast_connected_line_copy_to_caller(struct ast_party_caller *dest, const struct ast_party_connected_line *src)
{
	ast_party_id_copy(&dest->id, &src->id);
	ast_party_id_copy(&dest->ani, &src->ani);

	dest->ani2 = src->ani2;
}

void ast_channel_set_connected_line(struct ast_channel *chan, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update)
{
	if (&chan->connected == connected) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	ast_party_connected_line_set(&chan->connected, connected, update);
	ast_channel_unlock(chan);
}

/*! \note Should follow struct ast_party_name */
struct ast_party_name_ies {
	/*! \brief Subscriber name ie */
	int str;
	/*! \brief Character set ie. */
	int char_set;
	/*! \brief presentation-indicator ie */
	int presentation;
	/*! \brief valid/present ie */
	int valid;
};

/*!
 * \internal
 * \since 1.8
 * \brief Build a party name information data frame component.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param name Party name information
 * \param label Name of particular party name
 * \param ies Data frame ie values for the party name components
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
static int party_name_build_data(unsigned char *data, size_t datalen, const struct ast_party_name *name, const char *label, const struct ast_party_name_ies *ies)
{
	size_t length;
	size_t pos = 0;

	/*
	 * The size of integer values must be fixed in case the frame is
	 * shipped to another machine.
	 */
	if (name->str) {
		length = strlen(name->str);
		if (datalen < pos + (sizeof(data[0]) * 2) + length) {
			ast_log(LOG_WARNING, "No space left for %s name\n", label);
			return -1;
		}
		data[pos++] = ies->str;
		data[pos++] = length;
		memcpy(data + pos, name->str, length);
		pos += length;
	}

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s name char set\n", label);
		return -1;
	}
	data[pos++] = ies->char_set;
	data[pos++] = 1;
	data[pos++] = name->char_set;

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s name presentation\n", label);
		return -1;
	}
	data[pos++] = ies->presentation;
	data[pos++] = 1;
	data[pos++] = name->presentation;

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s name valid\n", label);
		return -1;
	}
	data[pos++] = ies->valid;
	data[pos++] = 1;
	data[pos++] = name->valid;

	return pos;
}

/*! \note Should follow struct ast_party_number */
struct ast_party_number_ies {
	/*! \brief Subscriber phone number ie */
	int str;
	/*! \brief Type-Of-Number and Numbering-Plan ie */
	int plan;
	/*! \brief presentation-indicator ie */
	int presentation;
	/*! \brief valid/present ie */
	int valid;
};

/*!
 * \internal
 * \since 1.8
 * \brief Build a party number information data frame component.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param number Party number information
 * \param label Name of particular party number
 * \param ies Data frame ie values for the party number components
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
static int party_number_build_data(unsigned char *data, size_t datalen, const struct ast_party_number *number, const char *label, const struct ast_party_number_ies *ies)
{
	size_t length;
	size_t pos = 0;

	/*
	 * The size of integer values must be fixed in case the frame is
	 * shipped to another machine.
	 */
	if (number->str) {
		length = strlen(number->str);
		if (datalen < pos + (sizeof(data[0]) * 2) + length) {
			ast_log(LOG_WARNING, "No space left for %s number\n", label);
			return -1;
		}
		data[pos++] = ies->str;
		data[pos++] = length;
		memcpy(data + pos, number->str, length);
		pos += length;
	}

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s numbering plan\n", label);
		return -1;
	}
	data[pos++] = ies->plan;
	data[pos++] = 1;
	data[pos++] = number->plan;

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s number presentation\n", label);
		return -1;
	}
	data[pos++] = ies->presentation;
	data[pos++] = 1;
	data[pos++] = number->presentation;

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s number valid\n", label);
		return -1;
	}
	data[pos++] = ies->valid;
	data[pos++] = 1;
	data[pos++] = number->valid;

	return pos;
}

/*! \note Should follow struct ast_party_subaddress */
struct ast_party_subaddress_ies {
	/*! \brief subaddress ie. */
	int str;
	/*! \brief subaddress type ie */
	int type;
	/*! \brief odd/even indicator ie */
	int odd_even_indicator;
	/*! \brief valid/present ie */
	int valid;
};

/*!
 * \internal
 * \since 1.8
 * \brief Build a party subaddress information data frame component.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param subaddress Party subaddress information
 * \param label Name of particular party subaddress
 * \param ies Data frame ie values for the party subaddress components
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
static int party_subaddress_build_data(unsigned char *data, size_t datalen, const struct ast_party_subaddress *subaddress, const char *label, const struct ast_party_subaddress_ies *ies)
{
	size_t length;
	size_t pos = 0;

	/*
	 * The size of integer values must be fixed in case the frame is
	 * shipped to another machine.
	 */
	if (subaddress->str) {
		length = strlen(subaddress->str);
		if (datalen < pos + (sizeof(data[0]) * 2) + length) {
			ast_log(LOG_WARNING, "No space left for %s subaddress\n", label);
			return -1;
		}
		data[pos++] = ies->str;
		data[pos++] = length;
		memcpy(data + pos, subaddress->str, length);
		pos += length;
	}

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s type of subaddress\n", label);
		return -1;
	}
	data[pos++] = ies->type;
	data[pos++] = 1;
	data[pos++] = subaddress->type;

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING,
			"No space left for %s subaddress odd-even indicator\n", label);
		return -1;
	}
	data[pos++] = ies->odd_even_indicator;
	data[pos++] = 1;
	data[pos++] = subaddress->odd_even_indicator;

	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for %s subaddress valid\n", label);
		return -1;
	}
	data[pos++] = ies->valid;
	data[pos++] = 1;
	data[pos++] = subaddress->valid;

	return pos;
}

/*! \note Should follow struct ast_party_id */
struct ast_party_id_ies {
	/*! \brief Subscriber name ies */
	struct ast_party_name_ies name;
	/*! \brief Subscriber phone number ies */
	struct ast_party_number_ies number;
	/*! \brief Subscriber subaddress ies. */
	struct ast_party_subaddress_ies subaddress;
	/*! \brief User party id tag ie. */
	int tag;
	/*! \brief Combined name and number presentation ie. */
	int combined_presentation;
};

/*!
 * \internal
 * \since 1.8
 * \brief Build a party id information data frame component.
 *
 * \param data Buffer to fill with the frame data
 * \param datalen Size of the buffer to fill
 * \param id Party id information
 * \param label Name of particular party id
 * \param ies Data frame ie values for the party id components
 * \param update What id information to build.  NULL if all.
 *
 * \retval -1 if error
 * \retval Amount of data buffer used
 */
static int party_id_build_data(unsigned char *data, size_t datalen,
	const struct ast_party_id *id, const char *label, const struct ast_party_id_ies *ies,
	const struct ast_set_party_id *update)
{
	size_t length;
	size_t pos = 0;
	int res;

	/*
	 * The size of integer values must be fixed in case the frame is
	 * shipped to another machine.
	 */

	if (!update || update->name) {
		res = party_name_build_data(data + pos, datalen - pos, &id->name, label,
			&ies->name);
		if (res < 0) {
			return -1;
		}
		pos += res;
	}

	if (!update || update->number) {
		res = party_number_build_data(data + pos, datalen - pos, &id->number, label,
			&ies->number);
		if (res < 0) {
			return -1;
		}
		pos += res;
	}

	if (!update || update->subaddress) {
		res = party_subaddress_build_data(data + pos, datalen - pos, &id->subaddress,
			label, &ies->subaddress);
		if (res < 0) {
			return -1;
		}
		pos += res;
	}

	/* *************** Party id user tag **************************** */
	if (id->tag) {
		length = strlen(id->tag);
		if (datalen < pos + (sizeof(data[0]) * 2) + length) {
			ast_log(LOG_WARNING, "No space left for %s tag\n", label);
			return -1;
		}
		data[pos++] = ies->tag;
		data[pos++] = length;
		memcpy(data + pos, id->tag, length);
		pos += length;
	}

	/* *************** Party id combined presentation *************** */
	if (!update || update->number) {
		int presentation;

		if (!update || update->name) {
			presentation = ast_party_id_presentation(id);
		} else {
			/*
			 * We must compromise because not all the information is available
			 * to determine a combined presentation value.
			 * We will only send the number presentation instead.
			 */
			presentation = id->number.presentation;
		}

		if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
			ast_log(LOG_WARNING, "No space left for %s combined presentation\n", label);
			return -1;
		}
		data[pos++] = ies->combined_presentation;
		data[pos++] = 1;
		data[pos++] = presentation;
	}

	return pos;
}

/*!
 * \brief Element identifiers for connected line indication frame data
 * \note Only add to the end of this enum.
 */
enum {
	AST_CONNECTED_LINE_NUMBER,
	AST_CONNECTED_LINE_NAME,
	AST_CONNECTED_LINE_NUMBER_PLAN,
	AST_CONNECTED_LINE_ID_PRESENTATION,/* Combined number and name presentation. */
	AST_CONNECTED_LINE_SOURCE,
	AST_CONNECTED_LINE_SUBADDRESS,
	AST_CONNECTED_LINE_SUBADDRESS_TYPE,
	AST_CONNECTED_LINE_SUBADDRESS_ODD_EVEN,
	AST_CONNECTED_LINE_SUBADDRESS_VALID,
	AST_CONNECTED_LINE_TAG,
	AST_CONNECTED_LINE_VERSION,
	AST_CONNECTED_LINE_NAME_VALID,
	AST_CONNECTED_LINE_NAME_CHAR_SET,
	AST_CONNECTED_LINE_NAME_PRESENTATION,
	AST_CONNECTED_LINE_NUMBER_VALID,
	AST_CONNECTED_LINE_NUMBER_PRESENTATION,
};

int ast_connected_line_build_data(unsigned char *data, size_t datalen, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update)
{
	int32_t value;
	size_t pos = 0;
	int res;

	static const struct ast_party_id_ies ies = {
		.name.str = AST_CONNECTED_LINE_NAME,
		.name.char_set = AST_CONNECTED_LINE_NAME_CHAR_SET,
		.name.presentation = AST_CONNECTED_LINE_NAME_PRESENTATION,
		.name.valid = AST_CONNECTED_LINE_NAME_VALID,

		.number.str = AST_CONNECTED_LINE_NUMBER,
		.number.plan = AST_CONNECTED_LINE_NUMBER_PLAN,
		.number.presentation = AST_CONNECTED_LINE_NUMBER_PRESENTATION,
		.number.valid = AST_CONNECTED_LINE_NUMBER_VALID,

		.subaddress.str = AST_CONNECTED_LINE_SUBADDRESS,
		.subaddress.type = AST_CONNECTED_LINE_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_CONNECTED_LINE_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_CONNECTED_LINE_SUBADDRESS_VALID,

		.tag = AST_CONNECTED_LINE_TAG,
		.combined_presentation = AST_CONNECTED_LINE_ID_PRESENTATION,
	};

	/*
	 * The size of integer values must be fixed in case the frame is
	 * shipped to another machine.
	 */

	/* Connected line frame version */
	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for connected line frame version\n");
		return -1;
	}
	data[pos++] = AST_CONNECTED_LINE_VERSION;
	data[pos++] = 1;
	data[pos++] = 2;/* Version 1 did not have a version ie */

	res = party_id_build_data(data + pos, datalen - pos, &connected->id,
		"connected line", &ies, update ? &update->id : NULL);
	if (res < 0) {
		return -1;
	}
	pos += res;

	/* Connected line source */
	if (datalen < pos + (sizeof(data[0]) * 2) + sizeof(value)) {
		ast_log(LOG_WARNING, "No space left for connected line source\n");
		return -1;
	}
	data[pos++] = AST_CONNECTED_LINE_SOURCE;
	data[pos++] = sizeof(value);
	value = htonl(connected->source);
	memcpy(data + pos, &value, sizeof(value));
	pos += sizeof(value);

	return pos;
}

int ast_connected_line_parse_data(const unsigned char *data, size_t datalen, struct ast_party_connected_line *connected)
{
	size_t pos;
	unsigned char ie_len;
	unsigned char ie_id;
	int32_t value;
	int frame_version = 1;
	int combined_presentation = 0;
	int got_combined_presentation = 0;/* TRUE if got a combined name and number presentation value. */

	for (pos = 0; pos < datalen; pos += ie_len) {
		if (datalen < pos + sizeof(ie_id) + sizeof(ie_len)) {
			ast_log(LOG_WARNING, "Invalid connected line update\n");
			return -1;
		}
		ie_id = data[pos++];
		ie_len = data[pos++];
		if (datalen < pos + ie_len) {
			ast_log(LOG_WARNING, "Invalid connected line update\n");
			return -1;
		}

		switch (ie_id) {
/* Connected line party frame version */
		case AST_CONNECTED_LINE_VERSION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line frame version (%u)\n",
					(unsigned) ie_len);
				break;
			}
			frame_version = data[pos];
			break;
/* Connected line party id name */
		case AST_CONNECTED_LINE_NAME:
			ast_free(connected->id.name.str);
			connected->id.name.str = ast_malloc(ie_len + 1);
			if (connected->id.name.str) {
				memcpy(connected->id.name.str, data + pos, ie_len);
				connected->id.name.str[ie_len] = 0;
			}
			break;
		case AST_CONNECTED_LINE_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.name.char_set = data[pos];
			break;
		case AST_CONNECTED_LINE_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.name.presentation = data[pos];
			break;
		case AST_CONNECTED_LINE_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.name.valid = data[pos];
			break;
/* Connected line party id number */
		case AST_CONNECTED_LINE_NUMBER:
			ast_free(connected->id.number.str);
			connected->id.number.str = ast_malloc(ie_len + 1);
			if (connected->id.number.str) {
				memcpy(connected->id.number.str, data + pos, ie_len);
				connected->id.number.str[ie_len] = 0;
			}
			break;
		case AST_CONNECTED_LINE_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.number.plan = data[pos];
			break;
		case AST_CONNECTED_LINE_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.number.presentation = data[pos];
			break;
		case AST_CONNECTED_LINE_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.number.valid = data[pos];
			break;
/* Connected line party id combined presentation */
		case AST_CONNECTED_LINE_ID_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line combined presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			combined_presentation = data[pos];
			got_combined_presentation = 1;
			break;
/* Connected line party id subaddress */
		case AST_CONNECTED_LINE_SUBADDRESS:
			ast_free(connected->id.subaddress.str);
			connected->id.subaddress.str = ast_malloc(ie_len + 1);
			if (connected->id.subaddress.str) {
				memcpy(connected->id.subaddress.str, data + pos, ie_len);
				connected->id.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_CONNECTED_LINE_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.subaddress.type = data[pos];
			break;
		case AST_CONNECTED_LINE_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid connected line subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_CONNECTED_LINE_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->id.subaddress.valid = data[pos];
			break;
/* Connected line party tag */
		case AST_CONNECTED_LINE_TAG:
			ast_free(connected->id.tag);
			connected->id.tag = ast_malloc(ie_len + 1);
			if (connected->id.tag) {
				memcpy(connected->id.tag, data + pos, ie_len);
				connected->id.tag[ie_len] = 0;
			}
			break;
/* Connected line party source */
		case AST_CONNECTED_LINE_SOURCE:
			if (ie_len != sizeof(value)) {
				ast_log(LOG_WARNING, "Invalid connected line source (%u)\n",
					(unsigned) ie_len);
				break;
			}
			memcpy(&value, data + pos, sizeof(value));
			connected->source = ntohl(value);
			break;
/* Connected line party unknown element */
		default:
			ast_log(LOG_DEBUG, "Unknown connected line element: %u (%u)\n",
				(unsigned) ie_id, (unsigned) ie_len);
			break;
		}
	}

	switch (frame_version) {
	case 1:
		/*
		 * The other end is an earlier version that we need to adjust
		 * for compatibility.
		 */
		connected->id.name.valid = 1;
		connected->id.name.char_set = AST_PARTY_CHAR_SET_ISO8859_1;
		connected->id.number.valid = 1;
		if (got_combined_presentation) {
			connected->id.name.presentation = combined_presentation;
			connected->id.number.presentation = combined_presentation;
		}
		break;
	case 2:
		/* The other end is at the same level as we are. */
		break;
	default:
		/*
		 * The other end is newer than we are.
		 * We need to assume that they are compatible with us.
		 */
		ast_log(LOG_DEBUG, "Connected line frame has newer version: %u\n",
			(unsigned) frame_version);
		break;
	}

	return 0;
}

void ast_channel_update_connected_line(struct ast_channel *chan, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update)
{
	unsigned char data[1024];	/* This should be large enough */
	size_t datalen;

	datalen = ast_connected_line_build_data(data, sizeof(data), connected, update);
	if (datalen == (size_t) -1) {
		return;
	}

	ast_indicate_data(chan, AST_CONTROL_CONNECTED_LINE, data, datalen);
}

void ast_channel_queue_connected_line_update(struct ast_channel *chan, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update)
{
	unsigned char data[1024];	/* This should be large enough */
	size_t datalen;

	datalen = ast_connected_line_build_data(data, sizeof(data), connected, update);
	if (datalen == (size_t) -1) {
		return;
	}

	ast_queue_control_data(chan, AST_CONTROL_CONNECTED_LINE, data, datalen);
}

void ast_channel_set_redirecting(struct ast_channel *chan, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update)
{
	if (&chan->redirecting == redirecting) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	ast_party_redirecting_set(&chan->redirecting, redirecting, update);
	ast_channel_unlock(chan);
}

/*!
 * \brief Element identifiers for redirecting indication frame data
 * \note Only add to the end of this enum.
 */
enum {
	AST_REDIRECTING_FROM_NUMBER,
	AST_REDIRECTING_FROM_NAME,
	AST_REDIRECTING_FROM_NUMBER_PLAN,
	AST_REDIRECTING_FROM_ID_PRESENTATION,
	AST_REDIRECTING_TO_NUMBER,
	AST_REDIRECTING_TO_NAME,
	AST_REDIRECTING_TO_NUMBER_PLAN,
	AST_REDIRECTING_TO_ID_PRESENTATION,
	AST_REDIRECTING_REASON,
	AST_REDIRECTING_COUNT,
	AST_REDIRECTING_FROM_SUBADDRESS,
	AST_REDIRECTING_FROM_SUBADDRESS_TYPE,
	AST_REDIRECTING_FROM_SUBADDRESS_ODD_EVEN,
	AST_REDIRECTING_FROM_SUBADDRESS_VALID,
	AST_REDIRECTING_TO_SUBADDRESS,
	AST_REDIRECTING_TO_SUBADDRESS_TYPE,
	AST_REDIRECTING_TO_SUBADDRESS_ODD_EVEN,
	AST_REDIRECTING_TO_SUBADDRESS_VALID,
	AST_REDIRECTING_FROM_TAG,
	AST_REDIRECTING_TO_TAG,
	AST_REDIRECTING_VERSION,
	AST_REDIRECTING_FROM_NAME_VALID,
	AST_REDIRECTING_FROM_NAME_CHAR_SET,
	AST_REDIRECTING_FROM_NAME_PRESENTATION,
	AST_REDIRECTING_FROM_NUMBER_VALID,
	AST_REDIRECTING_FROM_NUMBER_PRESENTATION,
	AST_REDIRECTING_TO_NAME_VALID,
	AST_REDIRECTING_TO_NAME_CHAR_SET,
	AST_REDIRECTING_TO_NAME_PRESENTATION,
	AST_REDIRECTING_TO_NUMBER_VALID,
	AST_REDIRECTING_TO_NUMBER_PRESENTATION,
};

int ast_redirecting_build_data(unsigned char *data, size_t datalen, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update)
{
	int32_t value;
	size_t pos = 0;
	int res;

	static const struct ast_party_id_ies from_ies = {
		.name.str = AST_REDIRECTING_FROM_NAME,
		.name.char_set = AST_REDIRECTING_FROM_NAME_CHAR_SET,
		.name.presentation = AST_REDIRECTING_FROM_NAME_PRESENTATION,
		.name.valid = AST_REDIRECTING_FROM_NAME_VALID,

		.number.str = AST_REDIRECTING_FROM_NUMBER,
		.number.plan = AST_REDIRECTING_FROM_NUMBER_PLAN,
		.number.presentation = AST_REDIRECTING_FROM_NUMBER_PRESENTATION,
		.number.valid = AST_REDIRECTING_FROM_NUMBER_VALID,

		.subaddress.str = AST_REDIRECTING_FROM_SUBADDRESS,
		.subaddress.type = AST_REDIRECTING_FROM_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_REDIRECTING_FROM_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_REDIRECTING_FROM_SUBADDRESS_VALID,

		.tag = AST_REDIRECTING_FROM_TAG,
		.combined_presentation = AST_REDIRECTING_FROM_ID_PRESENTATION,
	};
	static const struct ast_party_id_ies to_ies = {
		.name.str = AST_REDIRECTING_TO_NAME,
		.name.char_set = AST_REDIRECTING_TO_NAME_CHAR_SET,
		.name.presentation = AST_REDIRECTING_TO_NAME_PRESENTATION,
		.name.valid = AST_REDIRECTING_TO_NAME_VALID,

		.number.str = AST_REDIRECTING_TO_NUMBER,
		.number.plan = AST_REDIRECTING_TO_NUMBER_PLAN,
		.number.presentation = AST_REDIRECTING_TO_NUMBER_PRESENTATION,
		.number.valid = AST_REDIRECTING_TO_NUMBER_VALID,

		.subaddress.str = AST_REDIRECTING_TO_SUBADDRESS,
		.subaddress.type = AST_REDIRECTING_TO_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_REDIRECTING_TO_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_REDIRECTING_TO_SUBADDRESS_VALID,

		.tag = AST_REDIRECTING_TO_TAG,
		.combined_presentation = AST_REDIRECTING_TO_ID_PRESENTATION,
	};

	/* Redirecting frame version */
	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for redirecting frame version\n");
		return -1;
	}
	data[pos++] = AST_REDIRECTING_VERSION;
	data[pos++] = 1;
	data[pos++] = 2;/* Version 1 did not have a version ie */

	res = party_id_build_data(data + pos, datalen - pos, &redirecting->from,
		"redirecting-from", &from_ies, update ? &update->from : NULL);
	if (res < 0) {
		return -1;
	}
	pos += res;

	res = party_id_build_data(data + pos, datalen - pos, &redirecting->to,
		"redirecting-to", &to_ies, update ? &update->to : NULL);
	if (res < 0) {
		return -1;
	}
	pos += res;

	/* Redirecting reason */
	if (datalen < pos + (sizeof(data[0]) * 2) + sizeof(value)) {
		ast_log(LOG_WARNING, "No space left for redirecting reason\n");
		return -1;
	}
	data[pos++] = AST_REDIRECTING_REASON;
	data[pos++] = sizeof(value);
	value = htonl(redirecting->reason);
	memcpy(data + pos, &value, sizeof(value));
	pos += sizeof(value);

	/* Redirecting count */
	if (datalen < pos + (sizeof(data[0]) * 2) + sizeof(value)) {
		ast_log(LOG_WARNING, "No space left for redirecting count\n");
		return -1;
	}
	data[pos++] = AST_REDIRECTING_COUNT;
	data[pos++] = sizeof(value);
	value = htonl(redirecting->count);
	memcpy(data + pos, &value, sizeof(value));
	pos += sizeof(value);

	return pos;
}

int ast_redirecting_parse_data(const unsigned char *data, size_t datalen, struct ast_party_redirecting *redirecting)
{
	size_t pos;
	unsigned char ie_len;
	unsigned char ie_id;
	int32_t value;
	int frame_version = 1;
	int from_combined_presentation = 0;
	int got_from_combined_presentation = 0;/* TRUE if got a combined name and number presentation value. */
	int to_combined_presentation = 0;
	int got_to_combined_presentation = 0;/* TRUE if got a combined name and number presentation value. */

	for (pos = 0; pos < datalen; pos += ie_len) {
		if (datalen < pos + sizeof(ie_id) + sizeof(ie_len)) {
			ast_log(LOG_WARNING, "Invalid redirecting update\n");
			return -1;
		}
		ie_id = data[pos++];
		ie_len = data[pos++];
		if (datalen < pos + ie_len) {
			ast_log(LOG_WARNING, "Invalid redirecting update\n");
			return -1;
		}

		switch (ie_id) {
/* Redirecting frame version */
		case AST_REDIRECTING_VERSION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting frame version (%u)\n",
					(unsigned) ie_len);
				break;
			}
			frame_version = data[pos];
			break;
/* Redirecting-from party id name */
		case AST_REDIRECTING_FROM_NAME:
			ast_free(redirecting->from.name.str);
			redirecting->from.name.str = ast_malloc(ie_len + 1);
			if (redirecting->from.name.str) {
				memcpy(redirecting->from.name.str, data + pos, ie_len);
				redirecting->from.name.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_FROM_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.name.char_set = data[pos];
			break;
		case AST_REDIRECTING_FROM_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.name.presentation = data[pos];
			break;
		case AST_REDIRECTING_FROM_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.name.valid = data[pos];
			break;
/* Redirecting-from party id number */
		case AST_REDIRECTING_FROM_NUMBER:
			ast_free(redirecting->from.number.str);
			redirecting->from.number.str = ast_malloc(ie_len + 1);
			if (redirecting->from.number.str) {
				memcpy(redirecting->from.number.str, data + pos, ie_len);
				redirecting->from.number.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_FROM_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.number.plan = data[pos];
			break;
		case AST_REDIRECTING_FROM_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.number.presentation = data[pos];
			break;
		case AST_REDIRECTING_FROM_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.number.valid = data[pos];
			break;
/* Redirecting-from party id combined presentation */
		case AST_REDIRECTING_FROM_ID_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from combined presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			from_combined_presentation = data[pos];
			got_from_combined_presentation = 1;
			break;
/* Redirecting-from party id subaddress */
		case AST_REDIRECTING_FROM_SUBADDRESS:
			ast_free(redirecting->from.subaddress.str);
			redirecting->from.subaddress.str = ast_malloc(ie_len + 1);
			if (redirecting->from.subaddress.str) {
				memcpy(redirecting->from.subaddress.str, data + pos, ie_len);
				redirecting->from.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_FROM_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.subaddress.type = data[pos];
			break;
		case AST_REDIRECTING_FROM_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid redirecting-from subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_REDIRECTING_FROM_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-from subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->from.subaddress.valid = data[pos];
			break;
/* Redirecting-from party id tag */
		case AST_REDIRECTING_FROM_TAG:
			ast_free(redirecting->from.tag);
			redirecting->from.tag = ast_malloc(ie_len + 1);
			if (redirecting->from.tag) {
				memcpy(redirecting->from.tag, data + pos, ie_len);
				redirecting->from.tag[ie_len] = 0;
			}
			break;
/* Redirecting-to party id name */
		case AST_REDIRECTING_TO_NAME:
			ast_free(redirecting->to.name.str);
			redirecting->to.name.str = ast_malloc(ie_len + 1);
			if (redirecting->to.name.str) {
				memcpy(redirecting->to.name.str, data + pos, ie_len);
				redirecting->to.name.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_TO_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.name.char_set = data[pos];
			break;
		case AST_REDIRECTING_TO_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.name.presentation = data[pos];
			break;
		case AST_REDIRECTING_TO_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.name.valid = data[pos];
			break;
/* Redirecting-to party id number */
		case AST_REDIRECTING_TO_NUMBER:
			ast_free(redirecting->to.number.str);
			redirecting->to.number.str = ast_malloc(ie_len + 1);
			if (redirecting->to.number.str) {
				memcpy(redirecting->to.number.str, data + pos, ie_len);
				redirecting->to.number.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_TO_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.number.plan = data[pos];
			break;
		case AST_REDIRECTING_TO_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.number.presentation = data[pos];
			break;
		case AST_REDIRECTING_TO_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.number.valid = data[pos];
			break;
/* Redirecting-to party id combined presentation */
		case AST_REDIRECTING_TO_ID_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to combined presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			to_combined_presentation = data[pos];
			got_to_combined_presentation = 1;
			break;
/* Redirecting-to party id subaddress */
		case AST_REDIRECTING_TO_SUBADDRESS:
			ast_free(redirecting->to.subaddress.str);
			redirecting->to.subaddress.str = ast_malloc(ie_len + 1);
			if (redirecting->to.subaddress.str) {
				memcpy(redirecting->to.subaddress.str, data + pos, ie_len);
				redirecting->to.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_TO_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.subaddress.type = data[pos];
			break;
		case AST_REDIRECTING_TO_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid redirecting-to subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_REDIRECTING_TO_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-to subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->to.subaddress.valid = data[pos];
			break;
/* Redirecting-to party id tag */
		case AST_REDIRECTING_TO_TAG:
			ast_free(redirecting->to.tag);
			redirecting->to.tag = ast_malloc(ie_len + 1);
			if (redirecting->to.tag) {
				memcpy(redirecting->to.tag, data + pos, ie_len);
				redirecting->to.tag[ie_len] = 0;
			}
			break;
/* Redirecting reason */
		case AST_REDIRECTING_REASON:
			if (ie_len != sizeof(value)) {
				ast_log(LOG_WARNING, "Invalid redirecting reason (%u)\n",
					(unsigned) ie_len);
				break;
			}
			memcpy(&value, data + pos, sizeof(value));
			redirecting->reason = ntohl(value);
			break;
/* Redirecting count */
		case AST_REDIRECTING_COUNT:
			if (ie_len != sizeof(value)) {
				ast_log(LOG_WARNING, "Invalid redirecting count (%u)\n",
					(unsigned) ie_len);
				break;
			}
			memcpy(&value, data + pos, sizeof(value));
			redirecting->count = ntohl(value);
			break;
/* Redirecting unknown element */
		default:
			ast_log(LOG_DEBUG, "Unknown redirecting element: %u (%u)\n",
				(unsigned) ie_id, (unsigned) ie_len);
			break;
		}
	}

	switch (frame_version) {
	case 1:
		/*
		 * The other end is an earlier version that we need to adjust
		 * for compatibility.
		 */
		redirecting->from.name.valid = 1;
		redirecting->from.name.char_set = AST_PARTY_CHAR_SET_ISO8859_1;
		redirecting->from.number.valid = 1;
		if (got_from_combined_presentation) {
			redirecting->from.name.presentation = from_combined_presentation;
			redirecting->from.number.presentation = from_combined_presentation;
		}

		redirecting->to.name.valid = 1;
		redirecting->to.name.char_set = AST_PARTY_CHAR_SET_ISO8859_1;
		redirecting->to.number.valid = 1;
		if (got_to_combined_presentation) {
			redirecting->to.name.presentation = to_combined_presentation;
			redirecting->to.number.presentation = to_combined_presentation;
		}
		break;
	case 2:
		/* The other end is at the same level as we are. */
		break;
	default:
		/*
		 * The other end is newer than we are.
		 * We need to assume that they are compatible with us.
		 */
		ast_log(LOG_DEBUG, "Redirecting frame has newer version: %u\n",
			(unsigned) frame_version);
		break;
	}

	return 0;
}

void ast_channel_update_redirecting(struct ast_channel *chan, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update)
{
	unsigned char data[1024];	/* This should be large enough */
	size_t datalen;

	datalen = ast_redirecting_build_data(data, sizeof(data), redirecting, update);
	if (datalen == (size_t) -1) {
		return;
	}

	ast_indicate_data(chan, AST_CONTROL_REDIRECTING, data, datalen);
}

void ast_channel_queue_redirecting_update(struct ast_channel *chan, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update)
{
	unsigned char data[1024];	/* This should be large enough */
	size_t datalen;

	datalen = ast_redirecting_build_data(data, sizeof(data), redirecting, update);
	if (datalen == (size_t) -1) {
		return;
	}

	ast_queue_control_data(chan, AST_CONTROL_REDIRECTING, data, datalen);
}

int ast_channel_connected_line_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const void *connected_info, int is_caller, int is_frame)
{
	const char *macro;
	const char *macro_args;
	int retval;

	ast_channel_lock(macro_chan);
	macro = pbx_builtin_getvar_helper(macro_chan, is_caller
		? "CONNECTED_LINE_CALLER_SEND_MACRO" : "CONNECTED_LINE_CALLEE_SEND_MACRO");
	macro = ast_strdupa(S_OR(macro, ""));
	macro_args = pbx_builtin_getvar_helper(macro_chan, is_caller
		? "CONNECTED_LINE_CALLER_SEND_MACRO_ARGS" : "CONNECTED_LINE_CALLEE_SEND_MACRO_ARGS");
	macro_args = ast_strdupa(S_OR(macro_args, ""));
	ast_channel_unlock(macro_chan);

	if (ast_strlen_zero(macro)) {
		return -1;
	}

	if (is_frame) {
		const struct ast_frame *frame = connected_info;

		ast_connected_line_parse_data(frame->data.ptr, frame->datalen, &macro_chan->connected);
	} else {
		const struct ast_party_connected_line *connected = connected_info;

		ast_party_connected_line_copy(&macro_chan->connected, connected);
	}

	if (!(retval = ast_app_run_macro(autoservice_chan, macro_chan, macro, macro_args))) {
		ast_channel_update_connected_line(macro_chan, &macro_chan->connected, NULL);
	}

	return retval;
}

int ast_channel_redirecting_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const void *redirecting_info, int is_caller, int is_frame)
{
	const char *macro;
	const char *macro_args;
	int retval;

	ast_channel_lock(macro_chan);
	macro = pbx_builtin_getvar_helper(macro_chan, is_caller
		? "REDIRECTING_CALLER_SEND_MACRO" : "REDIRECTING_CALLEE_SEND_MACRO");
	macro = ast_strdupa(S_OR(macro, ""));
	macro_args = pbx_builtin_getvar_helper(macro_chan, is_caller
		? "REDIRECTING_CALLER_SEND_MACRO_ARGS" : "REDIRECTING_CALLEE_SEND_MACRO_ARGS");
	macro_args = ast_strdupa(S_OR(macro_args, ""));
	ast_channel_unlock(macro_chan);

	if (ast_strlen_zero(macro)) {
		return -1;
	}

	if (is_frame) {
		const struct ast_frame *frame = redirecting_info;

		ast_redirecting_parse_data(frame->data.ptr, frame->datalen, &macro_chan->redirecting);
	} else {
		const struct ast_party_redirecting *redirecting = redirecting_info;

		ast_party_redirecting_copy(&macro_chan->redirecting, redirecting);
	}

	retval = ast_app_run_macro(autoservice_chan, macro_chan, macro, macro_args);
	if (!retval) {
		ast_channel_update_redirecting(macro_chan, &macro_chan->redirecting, NULL);
	}

	return retval;
}

static void *channel_cc_params_copy(void *data)
{
	const struct ast_cc_config_params *src = data;
	struct ast_cc_config_params *dest = ast_cc_config_params_init();
	if (!dest) {
		return NULL;
	}
	ast_cc_copy_config_params(dest, src);
	return dest;
}

static void channel_cc_params_destroy(void *data)
{
	struct ast_cc_config_params *cc_params = data;
	ast_cc_config_params_destroy(cc_params);
}

static const struct ast_datastore_info cc_channel_datastore_info = {
	.type = "Call Completion",
	.duplicate = channel_cc_params_copy,
	.destroy = channel_cc_params_destroy,
};

int ast_channel_cc_params_init(struct ast_channel *chan,
		const struct ast_cc_config_params *base_params)
{
	struct ast_cc_config_params *cc_params;
	struct ast_datastore *cc_datastore;

	if (!(cc_params = ast_cc_config_params_init())) {
		return -1;
	}

	if (!(cc_datastore = ast_datastore_alloc(&cc_channel_datastore_info, NULL))) {
		ast_cc_config_params_destroy(cc_params);
		return -1;
	}

	if (base_params) {
		ast_cc_copy_config_params(cc_params, base_params);
	}
	cc_datastore->data = cc_params;
	ast_channel_datastore_add(chan, cc_datastore);
	return 0;
}

struct ast_cc_config_params *ast_channel_get_cc_config_params(struct ast_channel *chan)
{
	struct ast_datastore *cc_datastore;

	if (!(cc_datastore = ast_channel_datastore_find(chan, &cc_channel_datastore_info, NULL))) {
		/* If we can't find the datastore, it almost definitely means that the channel type being
		 * used has not had its driver modified to parse CC config parameters. The best action
		 * to take here is to create the parameters on the spot with the defaults set.
		 */
		if (ast_channel_cc_params_init(chan, NULL)) {
			return NULL;
		}
		if (!(cc_datastore = ast_channel_datastore_find(chan, &cc_channel_datastore_info, NULL))) {
			/* Should be impossible */
			return NULL;
		}
	}

	ast_assert(cc_datastore->data != NULL);
	return cc_datastore->data;
}

int ast_channel_get_device_name(struct ast_channel *chan, char *device_name, size_t name_buffer_length)
{
	int len = name_buffer_length;
	char *dash;
	if (!ast_channel_queryoption(chan, AST_OPTION_DEVICE_NAME, device_name, &len, 0)) {
		return 0;
	}

	/* Dang. Do it the old-fashioned way */
	ast_copy_string(device_name, chan->name, name_buffer_length);
	if ((dash = strrchr(device_name, '-'))) {
		*dash = '\0';
	}

	return 0;
}

int ast_channel_get_cc_agent_type(struct ast_channel *chan, char *agent_type, size_t size)
{
	int len = size;
	char *slash;

	if (!ast_channel_queryoption(chan, AST_OPTION_CC_AGENT_TYPE, agent_type, &len, 0)) {
		return 0;
	}

	ast_copy_string(agent_type, chan->name, size);
	if ((slash = strchr(agent_type, '/'))) {
		*slash = '\0';
	}
	return 0;
}

/* DO NOT PUT ADDITIONAL FUNCTIONS BELOW THIS BOUNDARY
 *
 * ONLY FUNCTIONS FOR PROVIDING BACKWARDS ABI COMPATIBILITY BELONG HERE
 *
 */

/* Provide binary compatibility for modules that call ast_channel_alloc() directly;
 * newly compiled modules will call __ast_channel_alloc() via the macros in channel.h
 */
#undef ast_channel_alloc
struct ast_channel __attribute__((format(printf, 10, 11)))
	*ast_channel_alloc(int needqueue, int state, const char *cid_num,
			   const char *cid_name, const char *acctcode,
			   const char *exten, const char *context,
			   const char *linkedid, const int amaflag,
			   const char *name_fmt, ...);
struct ast_channel *ast_channel_alloc(int needqueue, int state, const char *cid_num,
				      const char *cid_name, const char *acctcode,
				      const char *exten, const char *context,
				      const char *linkedid, const int amaflag,
				      const char *name_fmt, ...)
{
	va_list ap1, ap2;
	struct ast_channel *result;


	va_start(ap1, name_fmt);
	va_start(ap2, name_fmt);
	result = __ast_channel_alloc_ap(needqueue, state, cid_num, cid_name, acctcode, exten, context,
					linkedid, amaflag, __FILE__, __LINE__, __FUNCTION__, name_fmt, ap1, ap2);
	va_end(ap1);
	va_end(ap2);

	return result;
}
