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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

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
#include "asterisk/threadstorage.h"
#include "asterisk/slinfactory.h"
#include "asterisk/audiohook.h"
#include "asterisk/framehook.h"
#include "asterisk/timing.h"
#include "asterisk/autochan.h"
#include "asterisk/stringfields.h"
#include "asterisk/global_datastores.h"
#include "asterisk/data.h"
#include "asterisk/channel_internal.h"
#include "asterisk/features.h"
#include "asterisk/test.h"

/*** DOCUMENTATION
 ***/

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
static int chancount;

unsigned long global_fin, global_fout;

AST_THREADSTORAGE(state2str_threadbuf);
#define STATE2STR_BUFSIZE   32

/*! Default amount of time to use when emulating a digit as a begin and end
 *  100ms */
#define AST_DEFAULT_EMULATE_DTMF_DURATION 100

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

/*! \brief All active channels on the system */
static struct ao2_container *channels;

/*! \brief map AST_CAUSE's to readable string representations
 *
 * \ref causes.h
*/
struct causes_map {
	int cause;
	const char *name;
	const char *desc;
};

static const struct causes_map causes[] = {
	{ AST_CAUSE_UNALLOCATED, "UNALLOCATED", "Unallocated (unassigned) number" },
	{ AST_CAUSE_NO_ROUTE_TRANSIT_NET, "NO_ROUTE_TRANSIT_NET", "No route to specified transmit network" },
	{ AST_CAUSE_NO_ROUTE_DESTINATION, "NO_ROUTE_DESTINATION", "No route to destination" },
	{ AST_CAUSE_MISDIALLED_TRUNK_PREFIX, "MISDIALLED_TRUNK_PREFIX", "Misdialed trunk prefix" },
	{ AST_CAUSE_CHANNEL_UNACCEPTABLE, "CHANNEL_UNACCEPTABLE", "Channel unacceptable" },
	{ AST_CAUSE_CALL_AWARDED_DELIVERED, "CALL_AWARDED_DELIVERED", "Call awarded and being delivered in an established channel" },
	{ AST_CAUSE_PRE_EMPTED, "PRE_EMPTED", "Pre-empted" },
	{ AST_CAUSE_NUMBER_PORTED_NOT_HERE, "NUMBER_PORTED_NOT_HERE", "Number ported elsewhere" },
	{ AST_CAUSE_NORMAL_CLEARING, "NORMAL_CLEARING", "Normal Clearing" },
	{ AST_CAUSE_USER_BUSY, "USER_BUSY", "User busy" },
	{ AST_CAUSE_NO_USER_RESPONSE, "NO_USER_RESPONSE", "No user responding" },
	{ AST_CAUSE_NO_ANSWER, "NO_ANSWER", "User alerting, no answer" },
	{ AST_CAUSE_SUBSCRIBER_ABSENT, "SUBSCRIBER_ABSENT", "Subscriber absent" },
	{ AST_CAUSE_CALL_REJECTED, "CALL_REJECTED", "Call Rejected" },
	{ AST_CAUSE_NUMBER_CHANGED, "NUMBER_CHANGED", "Number changed" },
	{ AST_CAUSE_REDIRECTED_TO_NEW_DESTINATION, "REDIRECTED_TO_NEW_DESTINATION", "Redirected to new destination" },
	{ AST_CAUSE_ANSWERED_ELSEWHERE, "ANSWERED_ELSEWHERE", "Answered elsewhere" },
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

/*! \brief Show channel types - CLI command */
static char *handle_cli_core_show_channeltypes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT  "%-15.15s  %-40.40s %-12.12s %-12.12s %-12.12s\n"
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
	ast_cli(a->fd, FORMAT, "-----------", "-----------", "-----------", "-----------", "-----------");

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
		ast_getformatname_multiple(buf, sizeof(buf), cl->tech->capabilities),
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

static struct ast_frame *kill_read(struct ast_channel *chan)
{
	/* Hangup channel. */
	return NULL;
}

static struct ast_frame *kill_exception(struct ast_channel *chan)
{
	/* Hangup channel. */
	return NULL;
}

static int kill_write(struct ast_channel *chan, struct ast_frame *frame)
{
	/* Hangup channel. */
	return -1;
}

static int kill_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	/* No problem fixing up the channel. */
	return 0;
}

static int kill_hangup(struct ast_channel *chan)
{
	ast_channel_tech_pvt_set(chan, NULL);
	return 0;
}

/*!
 * \brief Kill the channel channel driver technology descriptor.
 *
 * \details
 * The purpose of this channel technology is to encourage the
 * channel to hangup as quickly as possible.
 *
 * \note Used by DTMF atxfer and zombie channels.
 */
const struct ast_channel_tech ast_kill_tech = {
	.type = "Kill",
	.description = "Kill channel (should not see this)",
	.read = kill_read,
	.exception = kill_exception,
	.write = kill_write,
	.fixup = kill_fixup,
	.hangup = kill_hangup,
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
	if ((!AST_LIST_EMPTY(&traced->trace) && strcasecmp(AST_LIST_FIRST(&traced->trace)->context, ast_channel_context(chan))) ||
	    (AST_LIST_EMPTY(&traced->trace))) {
		/* Just do some debug logging */
		if (AST_LIST_EMPTY(&traced->trace))
			ast_debug(1, "Setting initial trace context to %s\n", ast_channel_context(chan));
		else
			ast_debug(1, "Changing trace context from %s to %s\n", AST_LIST_FIRST(&traced->trace)->context, ast_channel_context(chan));
		/* alloc or bail out */
		trace = ast_malloc(sizeof(*trace));
		if (!trace)
			return -1;
		/* save the current location and store it in the trace list */
		ast_copy_string(trace->context, ast_channel_context(chan), sizeof(trace->context));
		ast_copy_string(trace->exten, ast_channel_exten(chan), sizeof(trace->exten));
		trace->priority = ast_channel_priority(chan);
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
	if (ast_channel_softhangup_internal_flag(chan))		/* yes if soft hangup flag set */
		return 1;
	if (ast_tvzero(*ast_channel_whentohangup(chan)))	/* no if no hangup scheduled */
		return 0;
	if (ast_tvdiff_ms(*ast_channel_whentohangup(chan), ast_tvnow()) > 0)		/* no if hangup time has not come yet. */
		return 0;
	ast_debug(4, "Hangup time has come: %" PRIi64 "\n", ast_tvdiff_ms(*ast_channel_whentohangup(chan), ast_tvnow()));
	ast_test_suite_event_notify("HANGUP_TIME", "Channel: %s", ast_channel_name(chan));
	ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_TIMEOUT);	/* record event */
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

void ast_channel_softhangup_withcause_locked(struct ast_channel *chan, int causecode)
{
	ast_channel_lock(chan);

	if (causecode > 0) {
		ast_debug(1, "Setting hangupcause of channel %s to %d (is %d now)\n",
			ast_channel_name(chan), causecode, ast_channel_hangupcause(chan));

		ast_channel_hangupcause_set(chan, causecode);
	}

	ast_softhangup_nolock(chan, AST_SOFTHANGUP_EXPLICIT);

	ast_channel_unlock(chan);
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

int ast_undestroyed_channels(void)
{
	return ast_atomic_fetchadd_int(&chancount, 0);
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
	if (ast_tvzero(offset)) {
		ast_channel_whentohangup_set(chan, &offset);
	} else {
		struct timeval tv = ast_tvadd(offset, ast_tvnow());
		ast_channel_whentohangup_set(chan, &tv);
	}
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

	if (ast_tvzero(*ast_channel_whentohangup(chan)))
		return ast_tvzero(offset) ? 0 : -1;

	if (ast_tvzero(offset))
		return 1;

	whentohangup = ast_tvadd(offset, ast_tvnow());

	return ast_tvdiff_ms(whentohangup, *ast_channel_whentohangup(chan));
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
		snprintf(buf, STATE2STR_BUFSIZE, "Unknown (%u)", state);
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
struct ast_format *ast_best_codec(struct ast_format_cap *cap, struct ast_format *result)
{
	/* This just our opinion, expressed in code.  We are asked to choose
	   the best codec to use, given no information */
	static const enum ast_format_id prefs[] =
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
		AST_FORMAT_SLINEAR192,
		AST_FORMAT_SLINEAR96,
		AST_FORMAT_SLINEAR48,
		AST_FORMAT_SLINEAR44,
		AST_FORMAT_SLINEAR32,
		AST_FORMAT_SLINEAR24,
		AST_FORMAT_SLINEAR16,
		AST_FORMAT_SLINEAR12,
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
		AST_FORMAT_SPEEX32,
		AST_FORMAT_SPEEX16,
		AST_FORMAT_SPEEX,
		/*! SILK is pretty awesome. */
		AST_FORMAT_SILK,
		/*! CELT supports crazy high sample rates */
		AST_FORMAT_CELT,
		/*! Ick, LPC10 sounds terrible, but at least we have code for it, if you're tacky enough
		    to use it */
		AST_FORMAT_LPC10,
		/*! G.729a is faster than 723 and slightly less expensive */
		AST_FORMAT_G729A,
		/*! Down to G.723.1 which is proprietary but at least designed for voice */
		AST_FORMAT_G723_1,
	};
	char buf[512];
	int x;

	/* Find the first preferred codec in the format given */
	for (x = 0; x < ARRAY_LEN(prefs); x++) {
		if (ast_format_cap_best_byid(cap, prefs[x], result)) {
			return result;
		}
	}

	ast_format_clear(result);
	ast_log(LOG_WARNING, "Don't know any of %s formats\n", ast_getformatname_multiple(buf, sizeof(buf), cap));

	return NULL;
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
		       const char *function, const char *name_fmt, va_list ap)
{
	struct ast_channel *tmp;
	struct varshead *headp;
	char *tech = "", *tech2 = NULL;
	struct ast_format_cap *nativeformats;
	struct ast_sched_context *schedctx;
	struct ast_timer *timer;
	struct timeval now;

	/* If shutting down, don't allocate any new channels */
	if (ast_shutting_down()) {
		ast_log(LOG_WARNING, "Channel allocation failed: Refusing due to active shutdown\n");
		return NULL;
	}

	if (!(tmp = ast_channel_internal_alloc(ast_channel_destructor))) {
		/* Channel structure allocation failure. */
		return NULL;
	}
	if (!(nativeformats = ast_format_cap_alloc())) {
		ao2_ref(tmp, -1);
		/* format capabilities structure allocation failure */
		return NULL;
	}
	ast_channel_nativeformats_set(tmp, nativeformats);

	/*
	 * Init file descriptors to unopened state so
	 * the destructor can know not to close them.
	 */
	ast_channel_timingfd_set(tmp, -1);
	ast_channel_internal_alertpipe_clear(tmp);
	ast_channel_internal_fd_clear_all(tmp);

#ifdef HAVE_EPOLL
	ast_channel_epfd_set(tmp, epoll_create(25));
#endif

	if (!(schedctx = ast_sched_context_create())) {
		ast_log(LOG_WARNING, "Channel allocation failed: Unable to create schedule context\n");
		return ast_channel_unref(tmp);
	}
	ast_channel_sched_set(tmp, schedctx);

	ast_party_dialed_init(ast_channel_dialed(tmp));
	ast_party_caller_init(ast_channel_caller(tmp));
	ast_party_connected_line_init(ast_channel_connected(tmp));
	ast_party_redirecting_init(ast_channel_redirecting(tmp));

	if (cid_name) {
		ast_channel_caller(tmp)->id.name.valid = 1;
		ast_channel_caller(tmp)->id.name.str = ast_strdup(cid_name);
		if (!ast_channel_caller(tmp)->id.name.str) {
			return ast_channel_unref(tmp);
		}
	}
	if (cid_num) {
		ast_channel_caller(tmp)->id.number.valid = 1;
		ast_channel_caller(tmp)->id.number.str = ast_strdup(cid_num);
		if (!ast_channel_caller(tmp)->id.number.str) {
			return ast_channel_unref(tmp);
		}
	}

	if ((timer = ast_timer_open())) {
		ast_channel_timer_set(tmp, timer);
		if (strcmp(ast_timer_get_name(ast_channel_timer(tmp)), "timerfd")) {
			needqueue = 0;
		}
		ast_channel_timingfd_set(tmp, ast_timer_fd(ast_channel_timer(tmp)));
	}

	/*
	 * This is the last place the channel constructor can fail.
	 *
	 * The destructor takes advantage of this fact to ensure that the
	 * AST_CEL_CHANNEL_END is not posted if we have not posted the
	 * AST_CEL_CHANNEL_START yet.
	 */

	if (needqueue && ast_channel_internal_alertpipe_init(tmp)) {
		return ast_channel_unref(tmp);
	}

	/* Always watch the alertpipe */
	ast_channel_set_fd(tmp, AST_ALERT_FD, ast_channel_internal_alert_readfd(tmp));
	/* And timing pipe */
	ast_channel_set_fd(tmp, AST_TIMING_FD, ast_channel_timingfd(tmp));

	/* Initial state */
	ast_channel_state_set(tmp, state);

	ast_channel_streamid_set(tmp, -1);

	ast_channel_fin_set(tmp, global_fin);
	ast_channel_fout_set(tmp, global_fout);

	now = ast_tvnow();
	ast_channel_creationtime_set(tmp, &now);

	if (ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
		ast_channel_uniqueid_build(tmp, "%li.%d", (long) time(NULL),
			ast_atomic_fetchadd_int(&uniqueint, 1));
	} else {
		ast_channel_uniqueid_build(tmp, "%s-%li.%d", ast_config_AST_SYSTEM_NAME,
			(long) time(NULL), ast_atomic_fetchadd_int(&uniqueint, 1));
	}

	if (!ast_strlen_zero(linkedid)) {
		ast_channel_linkedid_set(tmp, linkedid);
	} else {
		ast_channel_linkedid_set(tmp, ast_channel_uniqueid(tmp));
	}

	if (!ast_strlen_zero(name_fmt)) {
		char *slash, *slash2;
		/* Almost every channel is calling this function, and setting the name via the ast_string_field_build() call.
		 * And they all use slightly different formats for their name string.
		 * This means, to set the name here, we have to accept variable args, and call the string_field_build from here.
		 * This means, that the stringfields must have a routine that takes the va_lists directly, and
		 * uses them to build the string, instead of forming the va_lists internally from the vararg ... list.
		 * This new function was written so this can be accomplished.
		 */
		ast_channel_name_build_va(tmp, name_fmt, ap);
		tech = ast_strdupa(ast_channel_name(tmp));
		if ((slash = strchr(tech, '/'))) {
			if ((slash2 = strchr(slash + 1, '/'))) {
				tech2 = slash + 1;
				*slash2 = '\0';
			}
			*slash = '\0';
		}
	} else {
		/*
		 * Start the string with '-' so it becomes an empty string
		 * in the destructor.
		 */
		ast_channel_name_set(tmp, "-**Unknown**");
	}

	/* Reminder for the future: under what conditions do we NOT want to track cdrs on channels? */

	/* These 4 variables need to be set up for the cdr_init() to work right */
	if (amaflag) {
		ast_channel_amaflags_set(tmp, amaflag);
	} else {
		ast_channel_amaflags_set(tmp, ast_default_amaflags);
	}

	if (!ast_strlen_zero(acctcode))
		ast_channel_accountcode_set(tmp, acctcode);
	else
		ast_channel_accountcode_set(tmp, ast_default_accountcode);

	ast_channel_context_set(tmp, S_OR(context, "default"));
	ast_channel_exten_set(tmp, S_OR(exten, "s"));
	ast_channel_priority_set(tmp, 1);

	ast_channel_cdr_set(tmp, ast_cdr_alloc());
	ast_cdr_init(ast_channel_cdr(tmp), tmp);
	ast_cdr_start(ast_channel_cdr(tmp));

	ast_atomic_fetchadd_int(&chancount, +1);
	ast_cel_report_event(tmp, AST_CEL_CHANNEL_START, NULL, NULL, NULL);

	headp = ast_channel_varshead(tmp);
	AST_LIST_HEAD_INIT_NOLOCK(headp);

	ast_pbx_hangup_handler_init(tmp);
	AST_LIST_HEAD_INIT_NOLOCK(ast_channel_datastores(tmp));

	AST_LIST_HEAD_INIT_NOLOCK(ast_channel_autochans(tmp));

	ast_channel_language_set(tmp, ast_defaultlanguage);

	ast_channel_tech_set(tmp, &null_tech);

	ao2_link(channels, tmp);

	/*
	 * And now, since the channel structure is built, and has its name, let's
	 * call the manager event generator with this Newchannel event. This is the
	 * proper and correct place to make this call, but you sure do have to pass
	 * a lot of data into this func to do it here!
	 */
	if (ast_get_channel_tech(tech) || (tech2 && ast_get_channel_tech(tech2))) {
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when a new channel is created.</synopsis>
				<syntax>
					<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newstate']/managerEventInstance/syntax/parameter[@name='ChannelState'])" />
					<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newstate']/managerEventInstance/syntax/parameter[@name='ChannelStateDesc'])" />
				</syntax>
			</managerEventInstance>
		***/
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
			ast_channel_name(tmp),
			state,
			ast_state2str(state),
			S_OR(cid_num, ""),
			S_OR(cid_name, ""),
			ast_channel_accountcode(tmp),
			S_OR(exten, ""),
			S_OR(context, ""),
			ast_channel_uniqueid(tmp));
	}

	ast_channel_internal_finalize(tmp);
	return tmp;
}

struct ast_channel *__ast_channel_alloc(int needqueue, int state, const char *cid_num,
					const char *cid_name, const char *acctcode,
					const char *exten, const char *context,
					const char *linkedid, const int amaflag,
					const char *file, int line, const char *function,
					const char *name_fmt, ...)
{
	va_list ap;
	struct ast_channel *result;

	va_start(ap, name_fmt);
	result = __ast_channel_alloc_ap(needqueue, state, cid_num, cid_name, acctcode, exten, context,
					linkedid, amaflag, file, line, function, name_fmt, ap);
	va_end(ap);

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

	if (!(tmp = ast_channel_internal_alloc(ast_dummy_channel_destructor))) {
		/* Dummy channel structure allocation failure. */
		return NULL;
	}

	ast_pbx_hangup_handler_init(tmp);
	AST_LIST_HEAD_INIT_NOLOCK(ast_channel_datastores(tmp));

	/*
	 * Init file descriptors to unopened state just in case
	 * autoservice is called on the channel or something tries to
	 * read a frame from it.
	 */
	ast_channel_timingfd_set(tmp, -1);
	ast_channel_internal_alertpipe_clear(tmp);
	ast_channel_internal_fd_clear_all(tmp);
#ifdef HAVE_EPOLL
	ast_channel_epfd_set(tmp, -1);
#endif

	headp = ast_channel_varshead(tmp);
	AST_LIST_HEAD_INIT_NOLOCK(headp);

	return tmp;
}

static int __ast_queue_frame(struct ast_channel *chan, struct ast_frame *fin, int head, struct ast_frame *after)
{
	struct ast_frame *f;
	struct ast_frame *cur;
	unsigned int new_frames = 0;
	unsigned int new_voice_frames = 0;
	unsigned int queued_frames = 0;
	unsigned int queued_voice_frames = 0;
	AST_LIST_HEAD_NOLOCK(,ast_frame) frames;

	ast_channel_lock(chan);

	/*
	 * Check the last frame on the queue if we are queuing the new
	 * frames after it.
	 */
	cur = AST_LIST_LAST(ast_channel_readq(chan));
	if (cur && cur->frametype == AST_FRAME_CONTROL && !head && (!after || after == cur)) {
		switch (cur->subclass.integer) {
		case AST_CONTROL_END_OF_Q:
			if (fin->frametype == AST_FRAME_CONTROL
				&& fin->subclass.integer == AST_CONTROL_HANGUP) {
				/*
				 * Destroy the end-of-Q marker frame so we can queue the hangup
				 * frame in its place.
				 */
				AST_LIST_REMOVE(ast_channel_readq(chan), cur, frame_list);
				ast_frfree(cur);

				/*
				 * This has degenerated to a normal queue append anyway.  Since
				 * we just destroyed the last frame in the queue we must make
				 * sure that "after" is NULL or bad things will happen.
				 */
				after = NULL;
				break;
			}
			/* Fall through */
		case AST_CONTROL_HANGUP:
			/* Don't queue anything. */
			ast_channel_unlock(chan);
			return 0;
		default:
			break;
		}
	}

	/* Build copies of all the new frames and count them */
	AST_LIST_HEAD_INIT_NOLOCK(&frames);
	for (cur = fin; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
		if (!(f = ast_frdup(cur))) {
			if (AST_LIST_FIRST(&frames)) {
				ast_frfree(AST_LIST_FIRST(&frames));
			}
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
	AST_LIST_TRAVERSE(ast_channel_readq(chan), cur, frame_list) {
		queued_frames++;
		if (cur->frametype == AST_FRAME_VOICE) {
			queued_voice_frames++;
		}
	}

	if ((queued_frames + new_frames > 128 || queued_voice_frames + new_voice_frames > 96)) {
		int count = 0;
		ast_log(LOG_WARNING, "Exceptionally long %squeue length queuing to %s\n", queued_frames + new_frames > 128 ? "" : "voice ", ast_channel_name(chan));
		AST_LIST_TRAVERSE_SAFE_BEGIN(ast_channel_readq(chan), cur, frame_list) {
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
		AST_LIST_INSERT_LIST_AFTER(ast_channel_readq(chan), &frames, after, frame_list);
	} else {
		if (head) {
			AST_LIST_APPEND_LIST(&frames, ast_channel_readq(chan), frame_list);
			AST_LIST_HEAD_INIT_NOLOCK(ast_channel_readq(chan));
		}
		AST_LIST_APPEND_LIST(ast_channel_readq(chan), &frames, frame_list);
	}

	if (ast_channel_alert_writable(chan)) {
		if (ast_channel_alert_write(chan)) {
			ast_log(LOG_WARNING, "Unable to write to alert pipe on %s (qlen = %u): %s!\n",
				ast_channel_name(chan), queued_frames, strerror(errno));
		}
	} else if (ast_channel_timingfd(chan) > -1) {
		ast_timer_enable_continuous(ast_channel_timer(chan));
	} else if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING)) {
		pthread_kill(ast_channel_blocker(chan), SIGURG);
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
	int res;

	/* Yeah, let's not change a lock-critical value without locking */
	ast_channel_lock(chan);
	ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a hangup is requested with no set cause.</synopsis>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "HangupRequest",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan));

	res = ast_queue_frame(chan, &f);
	ast_channel_unlock(chan);
	return res;
}

/*! \brief Queue a hangup frame for channel */
int ast_queue_hangup_with_cause(struct ast_channel *chan, int cause)
{
	struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_HANGUP };
	int res;

	if (cause >= 0) {
		f.data.uint32 = cause;
	}

	/* Yeah, let's not change a lock-critical value without locking */
	ast_channel_lock(chan);
	ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
	if (cause < 0) {
		f.data.uint32 = ast_channel_hangupcause(chan);
	}
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a hangup is requested with a specific cause code.</synopsis>
				<syntax>
					<xi:include xpointer="xpointer(/docs/managerEvent[@name='Hangup']/managerEventInstance/syntax/parameter[@name='Cause'])" />
				</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "HangupRequest",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"Cause: %d\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan),
		cause);

	res = ast_queue_frame(chan, &f);
	ast_channel_unlock(chan);
	return res;
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
		pre = ast_test_flag(ast_channel_flags(chan), AST_FLAG_DEFER_DTMF);
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_DEFER_DTMF);
	}
	return pre;
}

/*! \brief Unset defer DTMF flag on channel */
void ast_channel_undefer_dtmf(struct ast_channel *chan)
{
	if (chan)
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_DEFER_DTMF);
}

struct ast_channel *ast_channel_callback(ao2_callback_data_fn *cb_fn, void *arg,
		void *data, int ao2_flags)
{
	return ao2_callback_data(channels, ao2_flags, cb_fn, arg, data);
}

static int ast_channel_by_name_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *chan = obj;
	const char *name = arg;
	size_t name_len = *(size_t *) data;
	int ret = CMP_MATCH;

	if (ast_strlen_zero(name)) {
		ast_log(LOG_ERROR, "BUG! Must supply a channel name or partial name to match!\n");
		return CMP_STOP;
	}

	ast_channel_lock(chan);
	if ((!name_len && strcasecmp(ast_channel_name(chan), name))
		|| (name_len && strncasecmp(ast_channel_name(chan), name, name_len))) {
		ret = 0; /* name match failed, keep looking */
	}
	ast_channel_unlock(chan);

	return ret;
}

static int ast_channel_by_exten_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *chan = obj;
	char *context = arg;
	char *exten = data;
	int ret = CMP_MATCH;

	if (ast_strlen_zero(exten) || ast_strlen_zero(context)) {
		ast_log(LOG_ERROR, "BUG! Must have a context and extension to match!\n");
		return CMP_STOP;
	}

	ast_channel_lock(chan);
	if (strcasecmp(ast_channel_context(chan), context) && strcasecmp(ast_channel_macrocontext(chan), context)) {
		ret = 0; /* Context match failed, continue */
	} else if (strcasecmp(ast_channel_exten(chan), exten) && strcasecmp(ast_channel_macroexten(chan), exten)) {
		ret = 0; /* Extension match failed, continue */
	}
	ast_channel_unlock(chan);

	return ret;
}

static int ast_channel_by_uniqueid_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *chan = obj;
	char *uniqueid = arg;
	size_t id_len = *(size_t *) data;
	int ret = CMP_MATCH;

	if (ast_strlen_zero(uniqueid)) {
		ast_log(LOG_ERROR, "BUG! Must supply a uniqueid or partial uniqueid to match!\n");
		return CMP_STOP;
	}

	ast_channel_lock(chan);
	if ((!id_len && strcasecmp(ast_channel_uniqueid(chan), uniqueid))
		|| (id_len && strncasecmp(ast_channel_uniqueid(chan), uniqueid, id_len))) {
		ret = 0; /* uniqueid match failed, keep looking */
	}
	ast_channel_unlock(chan);

	return ret;
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

struct ast_channel_iterator *ast_channel_iterator_by_exten_new(const char *exten, const char *context)
{
	struct ast_channel_iterator *i;
	char *l_exten = (char *) exten;
	char *l_context = (char *) context;

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->active_iterator = (void *) ast_channel_callback(ast_channel_by_exten_cb,
		l_context, l_exten, OBJ_MULTIPLE);
	if (!i->active_iterator) {
		ast_free(i);
		return NULL;
	}

	return i;
}

struct ast_channel_iterator *ast_channel_iterator_by_name_new(const char *name, size_t name_len)
{
	struct ast_channel_iterator *i;
	char *l_name = (char *) name;

	if (!(i = ast_calloc(1, sizeof(*i)))) {
		return NULL;
	}

	i->active_iterator = (void *) ast_channel_callback(ast_channel_by_name_cb,
		l_name, &name_len,
		OBJ_MULTIPLE | (name_len == 0 /* match the whole word, so optimize */ ? OBJ_KEY : 0));
	if (!i->active_iterator) {
		ast_free(i);
		return NULL;
	}

	return i;
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

/* Legacy function, not currently used for lookups, but we need a cmp_fn */
static int ast_channel_cmp_cb(void *obj, void *arg, int flags)
{
	ast_log(LOG_ERROR, "BUG! Should never be called!\n");
	return CMP_STOP;
}

struct ast_channel *ast_channel_get_by_name_prefix(const char *name, size_t name_len)
{
	struct ast_channel *chan;
	char *l_name = (char *) name;

	chan = ast_channel_callback(ast_channel_by_name_cb, l_name, &name_len,
		(name_len == 0) /* optimize if it is a complete name match */ ? OBJ_KEY : 0);
	if (chan) {
		return chan;
	}

	if (ast_strlen_zero(l_name)) {
		/* We didn't have a name to search for so quit. */
		return NULL;
	}

	/* Now try a search for uniqueid. */
	return ast_channel_callback(ast_channel_by_uniqueid_cb, l_name, &name_len, 0);
}

struct ast_channel *ast_channel_get_by_name(const char *name)
{
	return ast_channel_get_by_name_prefix(name, 0);
}

struct ast_channel *ast_channel_get_by_exten(const char *exten, const char *context)
{
	char *l_exten = (char *) exten;
	char *l_context = (char *) context;

	return ast_channel_callback(ast_channel_by_exten_cb, l_context, l_exten, 0);
}

int ast_is_deferrable_frame(const struct ast_frame *frame)
{
	/* Do not add a default entry in this switch statement.  Each new
	 * frame type should be addressed directly as to whether it should
	 * be queued up or not.
	 */
	switch (frame->frametype) {
	case AST_FRAME_CONTROL:
	case AST_FRAME_TEXT:
	case AST_FRAME_IMAGE:
	case AST_FRAME_HTML:
		return 1;

	case AST_FRAME_DTMF_END:
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
int ast_safe_sleep_conditional(struct ast_channel *chan, int timeout_ms, int (*cond)(void*), void *data)
{
	struct ast_frame *f;
	struct ast_silence_generator *silgen = NULL;
	int res = 0;
	struct timeval start;
	int ms;
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_frames;

	AST_LIST_HEAD_INIT_NOLOCK(&deferred_frames);

	/* If no other generator is present, start silencegen while waiting */
	if (ast_opt_transmit_silence && !ast_channel_generatordata(chan)) {
		silgen = ast_channel_start_silence_generator(chan);
	}

	start = ast_tvnow();
	while ((ms = ast_remaining_ms(start, timeout_ms))) {
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

void ast_set_party_id_all(struct ast_set_party_id *update_id)
{
	update_id->name = 1;
	update_id->number = 1;
	update_id->subaddress = 1;
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
	if (number_value == AST_PRES_UNAVAILABLE) {
		return AST_PRES_NUMBER_NOT_AVAILABLE;
	}

	return number_value | number_screening;
}

void ast_party_id_invalidate(struct ast_party_id *id)
{
	id->name.valid = 0;
	id->number.valid = 0;
	id->subaddress.valid = 0;
}

void ast_party_id_reset(struct ast_party_id *id)
{
	ast_party_id_free(id);
	ast_party_id_init(id);
}

struct ast_party_id ast_party_id_merge(struct ast_party_id *base, struct ast_party_id *overlay)
{
	struct ast_party_id merged;

	merged = *base;
	if (overlay->name.valid) {
		merged.name = overlay->name;
	}
	if (overlay->number.valid) {
		merged.number = overlay->number;
	}
	if (overlay->subaddress.valid) {
		merged.subaddress = overlay->subaddress;
	}
	/* Note the actual structure is returned and not a pointer to it! */
	return merged;
}

void ast_party_id_merge_copy(struct ast_party_id *dest, struct ast_party_id *base, struct ast_party_id *overlay)
{
	struct ast_party_id merged;

	merged = ast_party_id_merge(base, overlay);
	ast_party_id_copy(dest, &merged);
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
	ast_party_id_init(&init->priv);
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
	ast_party_id_copy(&dest->priv, &src->priv);
	dest->ani2 = src->ani2;
}

void ast_party_caller_set_init(struct ast_party_caller *init, const struct ast_party_caller *guide)
{
	ast_party_id_set_init(&init->id, &guide->id);
	ast_party_id_set_init(&init->ani, &guide->ani);
	ast_party_id_set_init(&init->priv, &guide->priv);
	init->ani2 = guide->ani2;
}

void ast_party_caller_set(struct ast_party_caller *dest, const struct ast_party_caller *src, const struct ast_set_party_caller *update)
{
	ast_party_id_set(&dest->id, &src->id, update ? &update->id : NULL);
	ast_party_id_set(&dest->ani, &src->ani, update ? &update->ani : NULL);
	ast_party_id_set(&dest->priv, &src->priv, update ? &update->priv : NULL);
	dest->ani2 = src->ani2;
}

void ast_party_caller_free(struct ast_party_caller *doomed)
{
	ast_party_id_free(&doomed->id);
	ast_party_id_free(&doomed->ani);
	ast_party_id_free(&doomed->priv);
}

void ast_party_connected_line_init(struct ast_party_connected_line *init)
{
	ast_party_id_init(&init->id);
	ast_party_id_init(&init->ani);
	ast_party_id_init(&init->priv);
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
	ast_party_id_copy(&dest->priv, &src->priv);
	dest->ani2 = src->ani2;
	dest->source = src->source;
}

void ast_party_connected_line_set_init(struct ast_party_connected_line *init, const struct ast_party_connected_line *guide)
{
	ast_party_id_set_init(&init->id, &guide->id);
	ast_party_id_set_init(&init->ani, &guide->ani);
	ast_party_id_set_init(&init->priv, &guide->priv);
	init->ani2 = guide->ani2;
	init->source = guide->source;
}

void ast_party_connected_line_set(struct ast_party_connected_line *dest, const struct ast_party_connected_line *src, const struct ast_set_party_connected_line *update)
{
	ast_party_id_set(&dest->id, &src->id, update ? &update->id : NULL);
	ast_party_id_set(&dest->ani, &src->ani, update ? &update->ani : NULL);
	ast_party_id_set(&dest->priv, &src->priv, update ? &update->priv : NULL);
	dest->ani2 = src->ani2;
	dest->source = src->source;
}

void ast_party_connected_line_collect_caller(struct ast_party_connected_line *connected, struct ast_party_caller *caller)
{
	connected->id = caller->id;
	connected->ani = caller->ani;
	connected->priv = caller->priv;
	connected->ani2 = caller->ani2;
	connected->source = AST_CONNECTED_LINE_UPDATE_SOURCE_UNKNOWN;
}

void ast_party_connected_line_free(struct ast_party_connected_line *doomed)
{
	ast_party_id_free(&doomed->id);
	ast_party_id_free(&doomed->ani);
	ast_party_id_free(&doomed->priv);
}

void ast_party_redirecting_init(struct ast_party_redirecting *init)
{
	ast_party_id_init(&init->orig);
	ast_party_id_init(&init->from);
	ast_party_id_init(&init->to);
	ast_party_id_init(&init->priv_orig);
	ast_party_id_init(&init->priv_from);
	ast_party_id_init(&init->priv_to);
	init->count = 0;
	init->reason = AST_REDIRECTING_REASON_UNKNOWN;
	init->orig_reason = AST_REDIRECTING_REASON_UNKNOWN;
}

void ast_party_redirecting_copy(struct ast_party_redirecting *dest, const struct ast_party_redirecting *src)
{
	if (dest == src) {
		/* Don't copy to self */
		return;
	}

	ast_party_id_copy(&dest->orig, &src->orig);
	ast_party_id_copy(&dest->from, &src->from);
	ast_party_id_copy(&dest->to, &src->to);
	ast_party_id_copy(&dest->priv_orig, &src->priv_orig);
	ast_party_id_copy(&dest->priv_from, &src->priv_from);
	ast_party_id_copy(&dest->priv_to, &src->priv_to);
	dest->count = src->count;
	dest->reason = src->reason;
	dest->orig_reason = src->orig_reason;
}

void ast_party_redirecting_set_init(struct ast_party_redirecting *init, const struct ast_party_redirecting *guide)
{
	ast_party_id_set_init(&init->orig, &guide->orig);
	ast_party_id_set_init(&init->from, &guide->from);
	ast_party_id_set_init(&init->to, &guide->to);
	ast_party_id_set_init(&init->priv_orig, &guide->priv_orig);
	ast_party_id_set_init(&init->priv_from, &guide->priv_from);
	ast_party_id_set_init(&init->priv_to, &guide->priv_to);
	init->count = guide->count;
	init->reason = guide->reason;
	init->orig_reason = guide->orig_reason;
}

void ast_party_redirecting_set(struct ast_party_redirecting *dest, const struct ast_party_redirecting *src, const struct ast_set_party_redirecting *update)
{
	ast_party_id_set(&dest->orig, &src->orig, update ? &update->orig : NULL);
	ast_party_id_set(&dest->from, &src->from, update ? &update->from : NULL);
	ast_party_id_set(&dest->to, &src->to, update ? &update->to : NULL);
	ast_party_id_set(&dest->priv_orig, &src->priv_orig, update ? &update->priv_orig : NULL);
	ast_party_id_set(&dest->priv_from, &src->priv_from, update ? &update->priv_from : NULL);
	ast_party_id_set(&dest->priv_to, &src->priv_to, update ? &update->priv_to : NULL);
	dest->count = src->count;
	dest->reason = src->reason;
	dest->orig_reason = src->orig_reason;
}

void ast_party_redirecting_free(struct ast_party_redirecting *doomed)
{
	ast_party_id_free(&doomed->orig);
	ast_party_id_free(&doomed->from);
	ast_party_id_free(&doomed->to);
	ast_party_id_free(&doomed->priv_orig);
	ast_party_id_free(&doomed->priv_from);
	ast_party_id_free(&doomed->priv_to);
}

/*! \brief Free a channel structure */
static void ast_channel_destructor(void *obj)
{
	struct ast_channel *chan = obj;
#ifdef HAVE_EPOLL
	int i;
#endif
	struct ast_var_t *vardata;
	struct ast_frame *f;
	struct varshead *headp;
	struct ast_datastore *datastore;
	char device_name[AST_CHANNEL_NAME];
	struct ast_callid *callid;

	if (ast_channel_internal_is_finalized(chan)) {
		ast_cel_report_event(chan, AST_CEL_CHANNEL_END, NULL, NULL, NULL);
		ast_cel_check_retire_linkedid(chan);
	}

	ast_pbx_hangup_handler_destroy(chan);

	ast_channel_lock(chan);

	/* Get rid of each of the data stores on the channel */
	while ((datastore = AST_LIST_REMOVE_HEAD(ast_channel_datastores(chan), entry)))
		/* Free the data store */
		ast_datastore_free(datastore);

	/* While the channel is locked, take the reference to its callid while we tear down the call. */
	callid = ast_channel_callid(chan);
	ast_channel_callid_cleanup(chan);

	ast_channel_unlock(chan);

	/* Lock and unlock the channel just to be sure nobody has it locked still
	   due to a reference that was stored in a datastore. (i.e. app_chanspy) */
	ast_channel_lock(chan);
	ast_channel_unlock(chan);

	if (ast_channel_tech_pvt(chan)) {
		ast_log_callid(LOG_WARNING, callid, "Channel '%s' may not have been hung up properly\n", ast_channel_name(chan));
		ast_free(ast_channel_tech_pvt(chan));
	}

	if (ast_channel_sched(chan)) {
		ast_sched_context_destroy(ast_channel_sched(chan));
	}

	if (ast_channel_internal_is_finalized(chan)) {
		char *dashptr;

		ast_copy_string(device_name, ast_channel_name(chan), sizeof(device_name));
		if ((dashptr = strrchr(device_name, '-'))) {
			*dashptr = '\0';
		}
	} else {
		device_name[0] = '\0';
	}

	/* Stop monitoring */
	if (ast_channel_monitor(chan))
		ast_channel_monitor(chan)->stop( chan, 0 );

	/* If there is native format music-on-hold state, free it */
	if (ast_channel_music_state(chan))
		ast_moh_cleanup(chan);

	/* Free translators */
	if (ast_channel_readtrans(chan))
		ast_translator_free_path(ast_channel_readtrans(chan));
	if (ast_channel_writetrans(chan))
		ast_translator_free_path(ast_channel_writetrans(chan));
	if (ast_channel_pbx(chan))
		ast_log_callid(LOG_WARNING, callid, "PBX may not have been terminated properly on '%s'\n", ast_channel_name(chan));

	ast_party_dialed_free(ast_channel_dialed(chan));
	ast_party_caller_free(ast_channel_caller(chan));
	ast_party_connected_line_free(ast_channel_connected(chan));
	ast_party_redirecting_free(ast_channel_redirecting(chan));

	/* Close pipes if appropriate */
	ast_channel_internal_alertpipe_close(chan);
	if (ast_channel_timer(chan)) {
		ast_timer_close(ast_channel_timer(chan));
		ast_channel_timer_set(chan, NULL);
	}
#ifdef HAVE_EPOLL
	for (i = 0; i < AST_MAX_FDS; i++) {
		if (ast_channel_internal_epfd_data(chan, i)) {
			ast_free(ast_channel_internal_epfd_data(chan, i));
		}
	}
	close(ast_channel_epfd(chan));
#endif
	while ((f = AST_LIST_REMOVE_HEAD(ast_channel_readq(chan), frame_list)))
		ast_frfree(f);

	/* loop over the variables list, freeing all data and deleting list items */
	/* no need to lock the list, as the channel is already locked */
	headp = ast_channel_varshead(chan);
	while ((vardata = AST_LIST_REMOVE_HEAD(headp, entries)))
		ast_var_delete(vardata);

	ast_app_group_discard(chan);

	/* Destroy the jitterbuffer */
	ast_jb_destroy(chan);

	if (ast_channel_cdr(chan)) {
		ast_cdr_discard(ast_channel_cdr(chan));
		ast_channel_cdr_set(chan, NULL);
	}

	if (ast_channel_zone(chan)) {
		ast_channel_zone_set(chan, ast_tone_zone_unref(ast_channel_zone(chan)));
	}

	ast_channel_internal_cleanup(chan);

	if (device_name[0]) {
		/*
		 * We have a device name to notify of a new state.
		 *
		 * Queue an unknown state, because, while we know that this particular
		 * instance is dead, we don't know the state of all other possible
		 * instances.
		 */
		ast_devstate_changed_literal(AST_DEVICE_UNKNOWN, (ast_test_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_DEVSTATE_CACHE) ? AST_DEVSTATE_NOT_CACHABLE : AST_DEVSTATE_CACHABLE), device_name);
	}

	ast_channel_nativeformats_set(chan, ast_format_cap_destroy(ast_channel_nativeformats(chan)));
	if (callid) {
		ast_callid_unref(callid);
	}

	ast_channel_named_callgroups_set(chan, NULL);
	ast_channel_named_pickupgroups_set(chan, NULL);

	ast_atomic_fetchadd_int(&chancount, -1);
}

/*! \brief Free a dummy channel structure */
static void ast_dummy_channel_destructor(void *obj)
{
	struct ast_channel *chan = obj;
	struct ast_datastore *datastore;
	struct ast_var_t *vardata;
	struct varshead *headp;

	ast_pbx_hangup_handler_destroy(chan);

	/* Get rid of each of the data stores on the channel */
	while ((datastore = AST_LIST_REMOVE_HEAD(ast_channel_datastores(chan), entry))) {
		/* Free the data store */
		ast_datastore_free(datastore);
	}

	ast_party_dialed_free(ast_channel_dialed(chan));
	ast_party_caller_free(ast_channel_caller(chan));
	ast_party_connected_line_free(ast_channel_connected(chan));
	ast_party_redirecting_free(ast_channel_redirecting(chan));

	/* loop over the variables list, freeing all data and deleting list items */
	/* no need to lock the list, as the channel is already locked */
	headp = ast_channel_varshead(chan);
	while ((vardata = AST_LIST_REMOVE_HEAD(headp, entries)))
		ast_var_delete(vardata);

	if (ast_channel_cdr(chan)) {
		ast_cdr_discard(ast_channel_cdr(chan));
		ast_channel_cdr_set(chan, NULL);
	}

	ast_channel_internal_cleanup(chan);
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

	AST_LIST_TRAVERSE(ast_channel_datastores(from), datastore, entry) {
		if (datastore->inheritance > 0) {
			datastore2 = ast_datastore_alloc(datastore->info, datastore->uid);
			if (datastore2) {
				datastore2->data = datastore->info->duplicate ? datastore->info->duplicate(datastore->data) : NULL;
				datastore2->inheritance = datastore->inheritance == DATASTORE_INHERIT_FOREVER ? DATASTORE_INHERIT_FOREVER : datastore->inheritance - 1;
				AST_LIST_INSERT_TAIL(ast_channel_datastores(to), datastore2, entry);
			}
		}
	}
	return 0;
}

int ast_channel_datastore_add(struct ast_channel *chan, struct ast_datastore *datastore)
{
	int res = 0;

	AST_LIST_INSERT_HEAD(ast_channel_datastores(chan), datastore, entry);

	return res;
}

int ast_channel_datastore_remove(struct ast_channel *chan, struct ast_datastore *datastore)
{
	return AST_LIST_REMOVE(ast_channel_datastores(chan), datastore, entry) ? 0 : -1;
}

struct ast_datastore *ast_channel_datastore_find(struct ast_channel *chan, const struct ast_datastore_info *info, const char *uid)
{
	struct ast_datastore *datastore = NULL;

	if (info == NULL)
		return NULL;

	AST_LIST_TRAVERSE(ast_channel_datastores(chan), datastore, entry) {
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

	return datastore;
}

/*! Set the file descriptor on the channel */
void ast_channel_set_fd(struct ast_channel *chan, int which, int fd)
{
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	struct ast_epoll_data *aed = NULL;

	if (ast_channel_fd_isset(chan, which)) {
		epoll_ctl(ast_channel_epfd(chan), EPOLL_CTL_DEL, ast_channel_fd(chan, which), &ev);
		aed = ast_channel_internal_epfd_data(chan, which);
	}

	/* If this new fd is valid, add it to the epoll */
	if (fd > -1) {
		if (!aed && (!(aed = ast_calloc(1, sizeof(*aed)))))
			return;

		ast_channel_internal_epfd_data_set(chan, which, aed);
		aed->chan = chan;
		aed->which = which;

		ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
		ev.data.ptr = aed;
		epoll_ctl(ast_channel_epfd(chan), EPOLL_CTL_ADD, fd, &ev);
	} else if (aed) {
		/* We don't have to keep around this epoll data structure now */
		ast_free(aed);
		ast_channel_epfd_data_set(chan, which, NULL);
	}
#endif
	ast_channel_internal_fd_set(chan, which, fd);
	return;
}

/*! Add a channel to an optimized waitfor */
void ast_poll_channel_add(struct ast_channel *chan0, struct ast_channel *chan1)
{
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	int i = 0;

	if (ast_channel_epfd(chan0) == -1)
		return;

	/* Iterate through the file descriptors on chan1, adding them to chan0 */
	for (i = 0; i < AST_MAX_FDS; i++) {
		if (!ast_channel_fd_isset(chan1, i)) {
			continue;
		}
		ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
		ev.data.ptr = ast_channel_internal_epfd_data(chan1, i);
		epoll_ctl(ast_channel_epfd(chan0), EPOLL_CTL_ADD, ast_channel_fd(chan1, i), &ev);
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

	if (ast_channel_epfd(chan0) == -1)
		return;

	for (i = 0; i < AST_MAX_FDS; i++) {
		if (!ast_channel_fd_isset(chan1, i)) {
			continue;
		}
		epoll_ctl(ast_channel_epfd(chan0), EPOLL_CTL_DEL, ast_channel_fd(chan1, i), &ev);
	}

#endif
	return;
}

void ast_channel_clear_softhangup(struct ast_channel *chan, int flag)
{
	ast_channel_lock(chan);

	ast_channel_softhangup_internal_flag_clear(chan, flag);

	if (!ast_channel_softhangup_internal_flag(chan)) {
		struct ast_frame *fr;

		/* If we have completely cleared the softhangup flag,
		 * then we need to fully abort the hangup process.  This requires
		 * pulling the END_OF_Q frame out of the channel frame queue if it
		 * still happens to be there. */

		fr = AST_LIST_LAST(ast_channel_readq(chan));
		if (fr && fr->frametype == AST_FRAME_CONTROL &&
				fr->subclass.integer == AST_CONTROL_END_OF_Q) {
			AST_LIST_REMOVE(ast_channel_readq(chan), fr, frame_list);
			ast_frfree(fr);
		}
	}

	ast_channel_unlock(chan);
}

/*! \brief Softly hangup a channel, don't lock */
int ast_softhangup_nolock(struct ast_channel *chan, int cause)
{
	ast_debug(1, "Soft-Hanging up channel '%s'\n", ast_channel_name(chan));
	/* Inform channel driver that we need to be hung up, if it cares */
	ast_channel_softhangup_internal_flag_add(chan, cause);
	ast_queue_frame(chan, &ast_null_frame);
	/* Interrupt any poll call or such */
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING))
		pthread_kill(ast_channel_blocker(chan), SIGURG);
	return 0;
}

/*! \brief Softly hangup a channel, lock */
int ast_softhangup(struct ast_channel *chan, int cause)
{
	int res;

	ast_channel_lock(chan);
	res = ast_softhangup_nolock(chan, cause);
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a soft hangup is requested with a specific cause code.</synopsis>
				<syntax>
					<xi:include xpointer="xpointer(/docs/managerEvent[@name='Hangup']/managerEventInstance/syntax/parameter[@name='Cause'])" />
				</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "SoftHangupRequest",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"Cause: %d\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan),
		cause);
	ast_channel_unlock(chan);

	return res;
}

static void free_translation(struct ast_channel *clonechan)
{
	if (ast_channel_writetrans(clonechan))
		ast_translator_free_path(ast_channel_writetrans(clonechan));
	if (ast_channel_readtrans(clonechan))
		ast_translator_free_path(ast_channel_readtrans(clonechan));
	ast_channel_writetrans_set(clonechan, NULL);
	ast_channel_readtrans_set(clonechan, NULL);
	if (ast_format_cap_is_empty(ast_channel_nativeformats(clonechan))) {
		ast_format_clear(ast_channel_rawwriteformat(clonechan));
		ast_format_clear(ast_channel_rawreadformat(clonechan));
	} else {
		struct ast_format tmpfmt;
		ast_best_codec(ast_channel_nativeformats(clonechan), &tmpfmt);
		ast_format_copy(ast_channel_rawwriteformat(clonechan), &tmpfmt);
		ast_format_copy(ast_channel_rawreadformat(clonechan), &tmpfmt);
	}
}

void ast_set_hangupsource(struct ast_channel *chan, const char *source, int force)
{
	struct ast_channel *bridge;

	ast_channel_lock(chan);
	if (force || ast_strlen_zero(ast_channel_hangupsource(chan))) {
		ast_channel_hangupsource_set(chan, source);
	}
	bridge = ast_bridged_channel(chan);
	if (bridge) {
		ast_channel_ref(bridge);
	}
	ast_channel_unlock(chan);

	if (bridge) {
		ast_channel_lock(bridge);
		if (force || ast_strlen_zero(ast_channel_hangupsource(bridge))) {
			ast_channel_hangupsource_set(bridge, source);
		}
		ast_channel_unlock(bridge);
		ast_channel_unref(bridge);
	}
}

static void destroy_hooks(struct ast_channel *chan)
{
	if (ast_channel_audiohooks(chan)) {
		ast_audiohook_detach_list(ast_channel_audiohooks(chan));
		ast_channel_audiohooks_set(chan, NULL);
	}

	ast_framehook_list_destroy(chan);
}

/*! \brief Hangup a channel */
int ast_hangup(struct ast_channel *chan)
{
	char extra_str[64]; /* used for cel logging below */

	ast_autoservice_stop(chan);

	ast_channel_lock(chan);

	/*
	 * Do the masquerade if someone is setup to masquerade into us.
	 *
	 * NOTE: We must hold the channel lock after testing for a
	 * pending masquerade and setting the channel as a zombie to
	 * prevent __ast_channel_masquerade() from setting up a
	 * masquerade with a dead channel.
	 */
	while (ast_channel_masq(chan)) {
		ast_channel_unlock(chan);
		ast_do_masquerade(chan);
		ast_channel_lock(chan);
	}

	if (ast_channel_masqr(chan)) {
		/*
		 * This channel is one which will be masqueraded into something.
		 * Mark it as a zombie already so ast_do_masquerade() will know
		 * to free it later.
		 */
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE);
		destroy_hooks(chan);
		ast_channel_unlock(chan);
		return 0;
	}

	/* Mark as a zombie so a masquerade cannot be setup on this channel. */
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE);

	ast_channel_unlock(chan);

	/*
	 * XXX if running the hangup handlers here causes problems
	 * because the handlers take too long to execute, we could move
	 * the meat of this function into another thread.  A thread
	 * where channels go to die.
	 *
	 * If this is done, ast_autoservice_chan_hangup_peer() will no
	 * longer be needed.
	 */
	ast_pbx_hangup_handler_run(chan);
	ao2_unlink(channels, chan);
	ast_channel_lock(chan);

	destroy_hooks(chan);

	free_translation(chan);
	/* Close audio stream */
	if (ast_channel_stream(chan)) {
		ast_closestream(ast_channel_stream(chan));
		ast_channel_stream_set(chan, NULL);
	}
	/* Close video stream */
	if (ast_channel_vstream(chan)) {
		ast_closestream(ast_channel_vstream(chan));
		ast_channel_vstream_set(chan, NULL);
	}
	if (ast_channel_sched(chan)) {
		ast_sched_context_destroy(ast_channel_sched(chan));
		ast_channel_sched_set(chan, NULL);
	}

	if (ast_channel_generatordata(chan)) {	/* Clear any tone stuff remaining */
		if (ast_channel_generator(chan) && ast_channel_generator(chan)->release) {
			ast_channel_generator(chan)->release(chan, ast_channel_generatordata(chan));
		}
	}
	ast_channel_generatordata_set(chan, NULL);
	ast_channel_generator_set(chan, NULL);

	snprintf(extra_str, sizeof(extra_str), "%d,%s,%s", ast_channel_hangupcause(chan), ast_channel_hangupsource(chan), S_OR(pbx_builtin_getvar_helper(chan, "DIALSTATUS"), ""));
	ast_cel_report_event(chan, AST_CEL_HANGUP, NULL, extra_str, NULL);

	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING)) {
		ast_log(LOG_WARNING, "Hard hangup called by thread %ld on %s, while fd "
			"is blocked by thread %ld in procedure %s!  Expect a failure\n",
			(long) pthread_self(), ast_channel_name(chan), (long)ast_channel_blocker(chan), ast_channel_blockproc(chan));
		ast_assert(ast_test_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING) == 0);
	}

	ast_debug(1, "Hanging up channel '%s'\n", ast_channel_name(chan));
	if (ast_channel_tech(chan)->hangup) {
		ast_channel_tech(chan)->hangup(chan);
	}

	ast_channel_unlock(chan);

	ast_cc_offer(chan);
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a channel is hung up.</synopsis>
				<syntax>
					<parameter name="Cause">
						<para>A numeric cause code for why the channel was hung up.</para>
					</parameter>
					<parameter name="Cause-txt">
						<para>A description of why the channel was hung up.</para>
					</parameter>
				</syntax>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "Hangup",
		"Channel: %s\r\n"
		"Uniqueid: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"ConnectedLineNum: %s\r\n"
		"ConnectedLineName: %s\r\n"
		"AccountCode: %s\r\n"
		"Cause: %d\r\n"
		"Cause-txt: %s\r\n",
		ast_channel_name(chan),
		ast_channel_uniqueid(chan),
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, "<unknown>"),
		S_COR(ast_channel_connected(chan)->id.number.valid, ast_channel_connected(chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_connected(chan)->id.name.valid, ast_channel_connected(chan)->id.name.str, "<unknown>"),
		ast_channel_accountcode(chan),
		ast_channel_hangupcause(chan),
		ast_cause2str(ast_channel_hangupcause(chan))
		);

	if (ast_channel_cdr(chan) && !ast_test_flag(ast_channel_cdr(chan), AST_CDR_FLAG_BRIDGED) &&
		!ast_test_flag(ast_channel_cdr(chan), AST_CDR_FLAG_POST_DISABLED) &&
		(ast_channel_cdr(chan)->disposition != AST_CDR_NULL || ast_test_flag(ast_channel_cdr(chan), AST_CDR_FLAG_DIALED))) {
		ast_channel_lock(chan);
		ast_cdr_end(ast_channel_cdr(chan));
		ast_cdr_detach(ast_channel_cdr(chan));
		ast_channel_cdr_set(chan, NULL);
		ast_channel_unlock(chan);
	}

	ast_channel_unref(chan);

	return 0;
}

int ast_raw_answer(struct ast_channel *chan, int cdr_answer)
{
	int res = 0;

	ast_channel_lock(chan);

	/* You can't answer an outbound call */
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING)) {
		ast_channel_unlock(chan);
		return 0;
	}

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		ast_channel_unlock(chan);
		return -1;
	}

	ast_channel_unlock(chan);

	switch (ast_channel_state(chan)) {
	case AST_STATE_RINGING:
	case AST_STATE_RING:
		ast_channel_lock(chan);
		if (ast_channel_tech(chan)->answer) {
			res = ast_channel_tech(chan)->answer(chan);
		}
		ast_setstate(chan, AST_STATE_UP);
		if (cdr_answer) {
			ast_cdr_answer(ast_channel_cdr(chan));
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
			ast_cdr_answer(ast_channel_cdr(chan));
		}
		break;
	default:
		break;
	}

	ast_indicate(chan, -1);

	return res;
}

int __ast_answer(struct ast_channel *chan, unsigned int delay, int cdr_answer)
{
	int res = 0;
	enum ast_channel_state old_state;

	old_state = ast_channel_state(chan);
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
			int timeout_ms = MAX(delay, 500);
			unsigned int done = 0;
			struct timeval start;

			AST_LIST_HEAD_INIT_NOLOCK(&frames);

			start = ast_tvnow();
			for (;;) {
				int ms = ast_remaining_ms(start, timeout_ms);
				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_log(LOG_WARNING, "Error condition occurred when polling channel %s for a voice frame: %s\n", ast_channel_name(chan), strerror(errno));
					res = -1;
					break;
				}
				if (ms == 0) {
					ast_debug(2, "Didn't receive a media frame from %s within %u ms of answering. Continuing anyway\n", ast_channel_name(chan), MAX(delay, 500));
					break;
				}
				cur = ast_read(chan);
				if (!cur || ((cur->frametype == AST_FRAME_CONTROL) &&
					     (cur->subclass.integer == AST_CONTROL_HANGUP))) {
					if (cur) {
						ast_frfree(cur);
					}
					res = -1;
					ast_debug(2, "Hangup of channel %s detected in answer routine\n", ast_channel_name(chan));
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

			ast_channel_lock(chan);
			while ((cur = AST_LIST_REMOVE_HEAD(&frames, frame_list))) {
				if (res == 0) {
					ast_queue_frame_head(chan, cur);
				}
				ast_frfree(cur);
			}
			ast_channel_unlock(chan);
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

static void deactivate_generator_nolock(struct ast_channel *chan)
{
	if (ast_channel_generatordata(chan)) {
		struct ast_generator *generator = ast_channel_generator(chan);

		if (generator && generator->release) {
			generator->release(chan, ast_channel_generatordata(chan));
		}
		ast_channel_generatordata_set(chan, NULL);
		ast_channel_generator_set(chan, NULL);
		ast_channel_set_fd(chan, AST_GENERATOR_FD, -1);
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_WRITE_INT);
		ast_settimeout(chan, 0, NULL, NULL);
	}
}

void ast_deactivate_generator(struct ast_channel *chan)
{
	ast_channel_lock(chan);
	deactivate_generator_nolock(chan);
	ast_channel_unlock(chan);
}

static void generator_write_format_change(struct ast_channel *chan)
{
	struct ast_generator *generator;

	ast_channel_lock(chan);
	generator = ast_channel_generator(chan);
	if (generator && generator->write_format_change) {
		generator->write_format_change(chan, ast_channel_generatordata(chan));
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
	tmp = ast_channel_generatordata(chan);
	ast_channel_generatordata_set(chan, NULL);
	if (ast_channel_generator(chan))
		generate = ast_channel_generator(chan)->generate;
	ast_channel_unlock(chan);

	if (!tmp || !generate)
		return 0;

	res = generate(chan, tmp, 0, ast_format_rate(ast_channel_writeformat(chan)) / 50);

	ast_channel_lock(chan);
	if (ast_channel_generator(chan) && generate == ast_channel_generator(chan)->generate) {
		ast_channel_generatordata_set(chan, tmp);
	}
	ast_channel_unlock(chan);

	if (res) {
		ast_debug(1, "Auto-deactivating generator\n");
		ast_deactivate_generator(chan);
	}

	return 0;
}

int ast_activate_generator(struct ast_channel *chan, struct ast_generator *gen, void *params)
{
	int res = 0;
	void *generatordata = NULL;

	ast_channel_lock(chan);
	if (ast_channel_generatordata(chan)) {
		struct ast_generator *generator_old = ast_channel_generator(chan);

		if (generator_old && generator_old->release) {
			generator_old->release(chan, ast_channel_generatordata(chan));
		}
	}
	if (gen->alloc && !(generatordata = gen->alloc(chan, params))) {
		res = -1;
	}
	ast_channel_generatordata_set(chan, generatordata);
	if (!res) {
		ast_settimeout(chan, 50, generator_force, chan);
		ast_channel_generator_set(chan, gen);
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

	if (outfd) {
		*outfd = -99999;
	}
	if (exception) {
		*exception = 0;
	}

	if ((sz = n * AST_MAX_FDS + nfds)) {
		pfds = ast_alloca(sizeof(*pfds) * sz);
		fdmap = ast_alloca(sizeof(*fdmap) * sz);
	} else {
		/* nothing to allocate and no FDs to check */
		return NULL;
	}

	/* Perform any pending masquerades */
	for (x = 0; x < n; x++) {
		while (ast_channel_masq(c[x])) {
			ast_do_masquerade(c[x]);
		}

		ast_channel_lock(c[x]);
		if (!ast_tvzero(*ast_channel_whentohangup(c[x]))) {
			if (ast_tvzero(whentohangup))
				now = ast_tvnow();
			diff = ast_tvsub(*ast_channel_whentohangup(c[x]), now);
			if (diff.tv_sec < 0 || ast_tvzero(diff)) {
				ast_test_suite_event_notify("HANGUP_TIME", "Channel: %s", ast_channel_name(c[x]));
				/* Should already be hungup */
				ast_channel_softhangup_internal_flag_add(c[x], AST_SOFTHANGUP_TIMEOUT);
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
			max += ast_add_fd(&pfds[max], ast_channel_fd(c[x], y));
		}
		CHECK_BLOCKING(c[x]);
	}
	/* Add the individual fds */
	for (x = 0; x < nfds; x++) {
		fdmap[max].chan = -1;
		max += ast_add_fd(&pfds[max], fds[x]);
	}

	if (*ms > 0) {
		start = ast_tvnow();
	}

	if (sizeof(int) == 4) {	/* XXX fix timeout > 600000 on linux x86-32 */
		do {
			int kbrms = rms;
			if (kbrms > 600000) {
				kbrms = 600000;
			}
			res = ast_poll(pfds, max, kbrms);
			if (!res) {
				rms -= kbrms;
			}
		} while (!res && (rms > 0));
	} else {
		res = ast_poll(pfds, max, rms);
	}
	for (x = 0; x < n; x++) {
		ast_clear_flag(ast_channel_flags(c[x]), AST_FLAG_BLOCKING);
	}
	if (res < 0) { /* Simulate a timeout if we were interrupted */
		if (errno != EINTR) {
			*ms = -1;
		}
		return NULL;
	}
	if (!ast_tvzero(whentohangup)) {   /* if we have a timeout, check who expired */
		now = ast_tvnow();
		for (x = 0; x < n; x++) {
			if (!ast_tvzero(*ast_channel_whentohangup(c[x])) && ast_tvcmp(*ast_channel_whentohangup(c[x]), now) <= 0) {
				ast_test_suite_event_notify("HANGUP_TIME", "Channel: %s", ast_channel_name(c[x]));
				ast_channel_softhangup_internal_flag_add(c[x], AST_SOFTHANGUP_TIMEOUT);
				if (winner == NULL) {
					winner = c[x];
				}
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
		if (res == 0) {
			continue;
		}
		if (fdmap[x].chan >= 0) {	/* this is a channel */
			winner = c[fdmap[x].chan];	/* override previous winners */
			if (res & POLLPRI) {
				ast_set_flag(ast_channel_flags(winner), AST_FLAG_EXCEPTION);
			} else {
				ast_clear_flag(ast_channel_flags(winner), AST_FLAG_EXCEPTION);
			}
			ast_channel_fdno_set(winner, fdmap[x].fdno);
		} else {			/* this is an fd */
			if (outfd) {
				*outfd = pfds[x].fd;
			}
			if (exception) {
				*exception = (res & POLLPRI) ? -1 : 0;
			}
			winner = NULL;
		}
	}
	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0) {
			*ms = 0;
		}
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
	while (ast_channel_masq(chan)) {
		ast_do_masquerade(chan);
	}

	ast_channel_lock(chan);
	/* Figure out their timeout */
	if (!ast_tvzero(*ast_channel_whentohangup(chan))) {
		if ((diff = ast_tvdiff_ms(*ast_channel_whentohangup(chan), ast_tvnow())) < 0) {
			/* They should already be hungup! */
			ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_TIMEOUT);
			ast_channel_unlock(chan);
			return NULL;
		}
		/* If this value is smaller then the current one... make it priority */
		if (rms > diff) {
			rms = diff;
		}
	}

	ast_channel_unlock(chan);

	/* Time to make this channel block... */
	CHECK_BLOCKING(chan);

	if (*ms > 0) {
		start = ast_tvnow();
	}

	/* We don't have to add any file descriptors... they are already added, we just have to wait! */
	res = epoll_wait(ast_channel_epfd(chan), ev, 1, rms);

	/* Stop blocking */
	ast_clear_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING);

	/* Simulate a timeout if we were interrupted */
	if (res < 0) {
		if (errno != EINTR) {
			*ms = -1;
		}
		return NULL;
	}

	/* If this channel has a timeout see if it expired */
	if (!ast_tvzero(*ast_channel_whentohangup(chan))) {
		if (ast_tvdiff_ms(ast_tvnow(), *ast_channel_whentohangup(chan)) >= 0) {
			ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_TIMEOUT);
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
	ast_channel_fdno_set(chan, aed->which);
	if (ev[0].events & EPOLLPRI) {
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION);
	} else {
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION);
	}

	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0) {
			*ms = 0;
		}
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
		while (ast_channel_masq(c[i])) {
			ast_do_masquerade(c[i]);
		}

		ast_channel_lock(c[i]);
		if (!ast_tvzero(*ast_channel_whentohangup(c[i]))) {
			if (whentohangup == 0) {
				now = ast_tvnow();
			}
			if ((diff = ast_tvdiff_ms(*ast_channel_whentohangup(c[i]), now)) < 0) {
				ast_channel_softhangup_internal_flag_add(c[i], AST_SOFTHANGUP_TIMEOUT);
				ast_channel_unlock(c[i]);
				return c[i];
			}
			if (!whentohangup || whentohangup > diff) {
				whentohangup = diff;
			}
		}
		ast_channel_unlock(c[i]);
		CHECK_BLOCKING(c[i]);
	}

	rms = *ms;
	if (whentohangup) {
		rms = whentohangup;
		if (*ms >= 0 && *ms < rms) {
			rms = *ms;
		}
	}

	if (*ms > 0) {
		start = ast_tvnow();
	}

	res = epoll_wait(ast_channel_epfd(c[0]), ev, 25, rms);

	for (i = 0; i < n; i++) {
		ast_clear_flag(ast_channel_flags(c[i]), AST_FLAG_BLOCKING);
	}

	if (res < 0) {
		if (errno != EINTR) {
			*ms = -1;
		}
		return NULL;
	}

	if (whentohangup) {
		now = ast_tvnow();
		for (i = 0; i < n; i++) {
			if (!ast_tvzero(*ast_channel_whentohangup(c[i])) && ast_tvdiff_ms(now, *ast_channel_whentohangup(c[i])) >= 0) {
				ast_channel_softhangup_internal_flag_add(c[i], AST_SOFTHANGUP_TIMEOUT);
				if (!winner) {
					winner = c[i];
				}
			}
		}
	}

	if (!res) {
		*ms = 0;
		return winner;
	}

	for (i = 0; i < res; i++) {
		struct ast_epoll_data *aed = ev[i].data.ptr;

		if (!ev[i].events || !aed) {
			continue;
		}

		winner = aed->chan;
		if (ev[i].events & EPOLLPRI) {
			ast_set_flag(ast_channel_flags(winner), AST_FLAG_EXCEPTION);
		} else {
			ast_clear_flag(ast_channel_flags(winner), AST_FLAG_EXCEPTION);
		}
		ast_channel_fdno_set(winner, aed->which);
	}

	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0) {
			*ms = 0;
		}
	}

	return winner;
}

struct ast_channel *ast_waitfor_nandfds(struct ast_channel **c, int n, int *fds, int nfds,
					int *exception, int *outfd, int *ms)
{
	/* Clear all provided values in one place. */
	if (outfd) {
		*outfd = -99999;
	}
	if (exception) {
		*exception = 0;
	}

	/* If no epoll file descriptor is available resort to classic nandfds */
	if (!n || nfds || ast_channel_epfd(c[0]) == -1) {
		return ast_waitfor_nandfds_classic(c, n, fds, nfds, exception, outfd, ms);
	} else if (!nfds && n == 1) {
		return ast_waitfor_nandfds_simple(c[0], ms);
	} else {
		return ast_waitfor_nandfds_complex(c, n, ms);
	}
}
#endif

struct ast_channel *ast_waitfor_n(struct ast_channel **c, int n, int *ms)
{
	return ast_waitfor_nandfds(c, n, NULL, 0, NULL, NULL, ms);
}

int ast_waitfor(struct ast_channel *c, int ms)
{
	if (ms < 0) {
		do {
			ms = 100000;
			ast_waitfor_nandfds(&c, 1, NULL, 0, NULL, NULL, &ms);
		} while (!ms);
	} else {
		ast_waitfor_nandfds(&c, 1, NULL, 0, NULL, NULL, &ms);
	}
	return ms;
}

int ast_waitfordigit(struct ast_channel *c, int ms)
{
	return ast_waitfordigit_full(c, ms, -1, -1);
}

int ast_settimeout(struct ast_channel *c, unsigned int rate, int (*func)(const void *data), void *data)
{
	return ast_settimeout_full(c, rate, func, data, 0);
}

int ast_settimeout_full(struct ast_channel *c, unsigned int rate, int (*func)(const void *data), void *data, unsigned int is_ao2_obj)
{
	int res;
	unsigned int real_rate = rate, max_rate;

	ast_channel_lock(c);

	if (ast_channel_timingfd(c) == -1) {
		ast_channel_unlock(c);
		return -1;
	}

	if (!func) {
		rate = 0;
		data = NULL;
	}

	if (rate && rate > (max_rate = ast_timer_get_max_rate(ast_channel_timer(c)))) {
		real_rate = max_rate;
	}

	ast_debug(1, "Scheduling timer at (%u requested / %u actual) timer ticks per second\n", rate, real_rate);

	res = ast_timer_set_rate(ast_channel_timer(c), real_rate);

	if (ast_channel_timingdata(c) && ast_test_flag(ast_channel_flags(c), AST_FLAG_TIMINGDATA_IS_AO2_OBJ)) {
		ao2_ref(ast_channel_timingdata(c), -1);
	}

	ast_channel_timingfunc_set(c, func);
	ast_channel_timingdata_set(c, data);

	if (data && is_ao2_obj) {
		ao2_ref(data, 1);
		ast_set_flag(ast_channel_flags(c), AST_FLAG_TIMINGDATA_IS_AO2_OBJ);
	} else {
		ast_clear_flag(ast_channel_flags(c), AST_FLAG_TIMINGDATA_IS_AO2_OBJ);
	}

	if (func == NULL && rate == 0 && ast_channel_fdno(c) == AST_TIMING_FD) {
		/* Clearing the timing func and setting the rate to 0
		 * means that we don't want to be reading from the timingfd
		 * any more. Setting c->fdno to -1 means we won't have any
		 * errant reads from the timingfd, meaning we won't potentially
		 * miss any important frames.
		 */
		ast_channel_fdno_set(c, -1);
	}

	ast_channel_unlock(c);

	return res;
}

int ast_waitfordigit_full(struct ast_channel *c, int timeout_ms, int audiofd, int cmdfd)
{
	struct timeval start = ast_tvnow();
	int ms;

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(ast_channel_flags(c), AST_FLAG_ZOMBIE) || ast_check_hangup(c))
		return -1;

	/* Only look for the end of DTMF, don't bother with the beginning and don't emulate things */
	ast_set_flag(ast_channel_flags(c), AST_FLAG_END_DTMF_ONLY);

	/* Wait for a digit, no more than timeout_ms milliseconds total.
	 * Or, wait indefinitely if timeout_ms is <0.
	 */
	while ((ms = ast_remaining_ms(start, timeout_ms))) {
		struct ast_channel *rchan;
		int outfd = -1;

		errno = 0;
		/* While ast_waitfor_nandfds tries to help by reducing the timeout by how much was waited,
		 * it is unhelpful if it waited less than a millisecond.
		 */
		rchan = ast_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);

		if (!rchan && outfd < 0 && ms) {
			if (errno == 0 || errno == EINTR)
				continue;
			ast_log(LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
			ast_clear_flag(ast_channel_flags(c), AST_FLAG_END_DTMF_ONLY);
			return -1;
		} else if (outfd > -1) {
			/* The FD we were watching has something waiting */
			ast_log(LOG_WARNING, "The FD we were waiting for has something waiting. Waitfordigit returning numeric 1\n");
			ast_clear_flag(ast_channel_flags(c), AST_FLAG_END_DTMF_ONLY);
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
				ast_clear_flag(ast_channel_flags(c), AST_FLAG_END_DTMF_ONLY);
				return res;
			case AST_FRAME_CONTROL:
				switch (f->subclass.integer) {
				case AST_CONTROL_HANGUP:
					ast_frfree(f);
					ast_clear_flag(ast_channel_flags(c), AST_FLAG_END_DTMF_ONLY);
					return -1;
				case AST_CONTROL_PVT_CAUSE_CODE:
				case AST_CONTROL_RINGING:
				case AST_CONTROL_ANSWER:
				case AST_CONTROL_SRCUPDATE:
				case AST_CONTROL_SRCCHANGE:
				case AST_CONTROL_CONNECTED_LINE:
				case AST_CONTROL_REDIRECTING:
				case AST_CONTROL_UPDATE_RTP_PEER:
				case AST_CONTROL_HOLD:
				case AST_CONTROL_UNHOLD:
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

	ast_clear_flag(ast_channel_flags(c), AST_FLAG_END_DTMF_ONLY);

	return 0; /* Time is up */
}

static void send_dtmf_event(struct ast_channel *chan, const char *direction, const char digit, const char *begin, const char *end)
{
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a DTMF digit has started or ended on a channel.</synopsis>
				<syntax>
					<parameter name="Direction">
						<enumlist>
							<enum name="Received"/>
							<enum name="Sent"/>
						</enumlist>
					</parameter>
					<parameter name="Begin">
						<enumlist>
							<enum name="Yes"/>
							<enum name="No"/>
						</enumlist>
					</parameter>
					<parameter name="End">
						<enumlist>
							<enum name="Yes"/>
							<enum name="No"/>
						</enumlist>
					</parameter>
				</syntax>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_DTMF,
			"DTMF",
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Digit: %c\r\n"
			"Direction: %s\r\n"
			"Begin: %s\r\n"
			"End: %s\r\n",
			ast_channel_name(chan), ast_channel_uniqueid(chan), digit, direction, begin, end);
}

static void ast_read_generator_actions(struct ast_channel *chan, struct ast_frame *f)
{
	struct ast_generator *generator;
	void *gendata;
	int res;
	int samples;

	generator = ast_channel_generator(chan);
	if (!generator
		|| !generator->generate
		|| f->frametype != AST_FRAME_VOICE
		|| !ast_channel_generatordata(chan)
		|| ast_channel_timingfunc(chan)) {
		return;
	}

	/*
	 * We must generate frames in phase locked mode since
	 * we have no internal timer available.
	 */

	if (ast_format_cmp(&f->subclass.format, ast_channel_writeformat(chan)) == AST_FORMAT_CMP_NOT_EQUAL) {
		float factor;

		factor = ((float) ast_format_rate(ast_channel_writeformat(chan))) / ((float) ast_format_rate(&f->subclass.format));
		samples = (int) (((float) f->samples) * factor);
	} else {
		samples = f->samples;
	}

	gendata = ast_channel_generatordata(chan);
	ast_channel_generatordata_set(chan, NULL);     /* reset, to let writes go through */

	/*
	 * This unlock is here based on two assumptions that hold true at
	 * this point in the code. 1) this function is only called from
	 * within __ast_read() and 2) all generators call ast_write() in
	 * their generate callback.
	 *
	 * The reason this is added is so that when ast_write is called,
	 * the lock that occurs there will not recursively lock the
	 * channel.  Doing this will allow deadlock avoidance to work in
	 * deeper functions.
	 */
	ast_channel_unlock(chan);
	res = generator->generate(chan, gendata, f->datalen, samples);
	ast_channel_lock(chan);
	if (generator == ast_channel_generator(chan)) {
		ast_channel_generatordata_set(chan, gendata);
		if (res) {
			ast_debug(1, "Auto-deactivating generator\n");
			ast_deactivate_generator(chan);
		}
	}
}

static inline void queue_dtmf_readq(struct ast_channel *chan, struct ast_frame *f)
{
	struct ast_frame *fr = ast_channel_dtmff(chan);

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
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_DEFER_DTMF | AST_FLAG_EMULATE_DTMF)) {
		/* We're in the middle of emulating a digit, or DTMF has been
		 * explicitly deferred.  Skip this digit, then. */
		return 1;
	}

	if (!ast_tvzero(*ast_channel_dtmf_tv(chan)) &&
			ast_tvdiff_ms(ast_tvnow(), *ast_channel_dtmf_tv(chan)) < AST_MIN_DTMF_GAP) {
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
	int prestate;
	int cause = 0;

	/* this function is very long so make sure there is only one return
	 * point at the end (there are only two exceptions to this).
	 */

	if (ast_channel_masq(chan)) {
		ast_do_masquerade(chan);
		return &ast_null_frame;
	}

	/* if here, no masq has happened, lock the channel and proceed */
	ast_channel_lock(chan);

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		if (ast_channel_generator(chan))
			ast_deactivate_generator(chan);

		/*
		 * It is possible for chan->_softhangup to be set and there
		 * still be control frames that need to be read.  Instead of
		 * just going to 'done' in the case of ast_check_hangup(), we
		 * need to queue the end-of-Q frame so that it can mark the end
		 * of the read queue.  If there are frames to be read,
		 * ast_queue_control() will be called repeatedly, but will only
		 * queue the first end-of-Q frame.
		 */
		if (ast_channel_softhangup_internal_flag(chan)) {
			ast_queue_control(chan, AST_CONTROL_END_OF_Q);
		} else {
			goto done;
		}
	} else {
#ifdef AST_DEVMODE
		/*
		 * The ast_waitfor() code records which of the channel's file
		 * descriptors reported that data is available.  In theory,
		 * ast_read() should only be called after ast_waitfor() reports
		 * that a channel has data available for reading.  However,
		 * there still may be some edge cases throughout the code where
		 * ast_read() is called improperly.  This can potentially cause
		 * problems, so if this is a developer build, make a lot of
		 * noise if this happens so that it can be addressed.
		 *
		 * One of the potential problems is blocking on a dead channel.
		 */
		if (ast_channel_fdno(chan) == -1) {
			ast_log(LOG_ERROR,
				"ast_read() on chan '%s' called with no recorded file descriptor.\n",
				ast_channel_name(chan));
		}
#endif
	}

	prestate = ast_channel_state(chan);

	if (ast_channel_timingfd(chan) > -1 && ast_channel_fdno(chan) == AST_TIMING_FD) {
		enum ast_timer_event res;

		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION);

		res = ast_timer_get_event(ast_channel_timer(chan));

		switch (res) {
		case AST_TIMING_EVENT_EXPIRED:
			if (ast_timer_ack(ast_channel_timer(chan), 1) < 0) {
				ast_log(LOG_ERROR, "Failed to acknoweldge timer in ast_read\n");
				goto done;
			}

			if (ast_channel_timingfunc(chan)) {
				/* save a copy of func/data before unlocking the channel */
				ast_timing_func_t func = ast_channel_timingfunc(chan);
				void *data = ast_channel_timingdata(chan);
				int got_ref = 0;
				if (data && ast_test_flag(ast_channel_flags(chan), AST_FLAG_TIMINGDATA_IS_AO2_OBJ)) {
					ao2_ref(data, 1);
					got_ref = 1;
				}
				ast_channel_fdno_set(chan, -1);
				ast_channel_unlock(chan);
				func(data);
				if (got_ref) {
					ao2_ref(data, -1);
				}
			} else {
				ast_timer_set_rate(ast_channel_timer(chan), 0);
				ast_channel_fdno_set(chan, -1);
				ast_channel_unlock(chan);
			}

			/* cannot 'goto done' because the channel is already unlocked */
			return &ast_null_frame;

		case AST_TIMING_EVENT_CONTINUOUS:
			if (AST_LIST_EMPTY(ast_channel_readq(chan)) ||
				!AST_LIST_NEXT(AST_LIST_FIRST(ast_channel_readq(chan)), frame_list)) {
				ast_timer_disable_continuous(ast_channel_timer(chan));
			}
			break;
		}

	} else if (ast_channel_fd_isset(chan, AST_GENERATOR_FD) && ast_channel_fdno(chan) == AST_GENERATOR_FD) {
		/* if the AST_GENERATOR_FD is set, call the generator with args
		 * set to -1 so it can do whatever it needs to.
		 */
		void *tmp = ast_channel_generatordata(chan);
		ast_channel_generatordata_set(chan, NULL);     /* reset to let ast_write get through */
		ast_channel_generator(chan)->generate(chan, tmp, -1, -1);
		ast_channel_generatordata_set(chan, tmp);
		f = &ast_null_frame;
		ast_channel_fdno_set(chan, -1);
		goto done;
	} else if (ast_channel_fd_isset(chan, AST_JITTERBUFFER_FD) && ast_channel_fdno(chan) == AST_JITTERBUFFER_FD) {
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION);
	}

	/* Read and ignore anything on the alertpipe, but read only
	   one sizeof(blah) per frame that we send from it */
	if (ast_channel_internal_alert_read(chan) == AST_ALERT_READ_FATAL) {
		f = &ast_null_frame;
		goto done;
	}

	/* Check for pending read queue */
	if (!AST_LIST_EMPTY(ast_channel_readq(chan))) {
		int skip_dtmf = should_skip_dtmf(chan);

		AST_LIST_TRAVERSE_SAFE_BEGIN(ast_channel_readq(chan), f, frame_list) {
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
			ast_channel_alert_write(chan);
		}

		/* Interpret hangup and end-of-Q frames to return NULL */
		/* XXX why not the same for frames from the channel ? */
		if (f->frametype == AST_FRAME_CONTROL) {
			switch (f->subclass.integer) {
			case AST_CONTROL_HANGUP:
				ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
				cause = f->data.uint32;
				/* Fall through */
			case AST_CONTROL_END_OF_Q:
				ast_frfree(f);
				f = NULL;
				break;
			default:
				break;
			}
		}
	} else {
		ast_channel_blocker_set(chan, pthread_self());
		if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION)) {
			if (ast_channel_tech(chan)->exception)
				f = ast_channel_tech(chan)->exception(chan);
			else {
				ast_log(LOG_WARNING, "Exception flag set on '%s', but no exception handler\n", ast_channel_name(chan));
				f = &ast_null_frame;
			}
			/* Clear the exception flag */
			ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EXCEPTION);
		} else if (ast_channel_tech(chan) && ast_channel_tech(chan)->read)
			f = ast_channel_tech(chan)->read(chan);
		else
			ast_log(LOG_WARNING, "No read routine on channel %s\n", ast_channel_name(chan));
	}

	/* Perform the framehook read event here. After the frame enters the framehook list
	 * there is no telling what will happen, <insert mad scientist laugh here>!!! */
	f = ast_framehook_list_read_event(ast_channel_framehooks(chan), f);

	/*
	 * Reset the recorded file descriptor that triggered this read so that we can
	 * easily detect when ast_read() is called without properly using ast_waitfor().
	 */
	ast_channel_fdno_set(chan, -1);

	if (f) {
		struct ast_frame *readq_tail = AST_LIST_LAST(ast_channel_readq(chan));
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
				if (!ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING)) {
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
					ast_party_connected_line_copy(&connected, ast_channel_connected(chan));
					if (ast_connected_line_parse_data(read_action_payload->payload,
						read_action_payload->payload_size, &connected)) {
						ast_party_connected_line_free(&connected);
						break;
					}
					ast_channel_unlock(chan);
					if (ast_channel_connected_line_sub(NULL, chan, &connected, 0) &&
						ast_channel_connected_line_macro(NULL, chan, &connected, 1, 0)) {
						ast_indicate_data(chan, AST_CONTROL_CONNECTED_LINE,
							read_action_payload->payload,
							read_action_payload->payload_size);
					}
					ast_party_connected_line_free(&connected);
					ast_channel_lock(chan);
					break;
				}
				ast_frfree(f);
				f = &ast_null_frame;
			}
			break;
		case AST_FRAME_DTMF_END:
			send_dtmf_event(chan, "Received", f->subclass.integer, "No", "Yes");
			ast_log(LOG_DTMF, "DTMF end '%c' received on %s, duration %ld ms\n", f->subclass.integer, ast_channel_name(chan), f->len);
			/* Queue it up if DTMF is deferred, or if DTMF emulation is forced. */
			if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_DEFER_DTMF) || ast_test_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF)) {
				queue_dtmf_readq(chan, f);
				ast_frfree(f);
				f = &ast_null_frame;
			} else if (!ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_DTMF | AST_FLAG_END_DTMF_ONLY)) {
				if (!ast_tvzero(*ast_channel_dtmf_tv(chan)) &&
				    ast_tvdiff_ms(ast_tvnow(), *ast_channel_dtmf_tv(chan)) < AST_MIN_DTMF_GAP) {
					/* If it hasn't been long enough, defer this digit */
					queue_dtmf_readq(chan, f);
					ast_frfree(f);
					f = &ast_null_frame;
				} else {
					/* There was no begin, turn this into a begin and send the end later */
					struct timeval tv = ast_tvnow();
					f->frametype = AST_FRAME_DTMF_BEGIN;
					ast_set_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF);
					ast_channel_dtmf_digit_to_emulate_set(chan, f->subclass.integer);
					ast_channel_dtmf_tv_set(chan, &tv);
					if (f->len) {
						if (f->len > option_dtmfminduration)
							ast_channel_emulate_dtmf_duration_set(chan, f->len);
						else
							ast_channel_emulate_dtmf_duration_set(chan, option_dtmfminduration);
					} else
						ast_channel_emulate_dtmf_duration_set(chan, AST_DEFAULT_EMULATE_DTMF_DURATION);
					ast_log(LOG_DTMF, "DTMF begin emulation of '%c' with duration %u queued on %s\n", f->subclass.integer, ast_channel_emulate_dtmf_duration(chan), ast_channel_name(chan));
				}
				if (ast_channel_audiohooks(chan)) {
					struct ast_frame *old_frame = f;
					/*!
					 * \todo XXX It is possible to write a digit to the audiohook twice
					 * if the digit was originally read while the channel was in autoservice. */
					f = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_READ, f);
					if (old_frame != f)
						ast_frfree(old_frame);
				}
			} else {
				struct timeval now = ast_tvnow();
				if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_DTMF)) {
					ast_log(LOG_DTMF, "DTMF end accepted with begin '%c' on %s\n", f->subclass.integer, ast_channel_name(chan));
					ast_clear_flag(ast_channel_flags(chan), AST_FLAG_IN_DTMF);
					if (!f->len)
						f->len = ast_tvdiff_ms(now, *ast_channel_dtmf_tv(chan));

					/* detect tones that were received on
					 * the wire with durations shorter than
					 * option_dtmfminduration and set f->len
					 * to the actual duration of the DTMF
					 * frames on the wire.  This will cause
					 * dtmf emulation to be triggered later
					 * on.
					 */
					if (ast_tvdiff_ms(now, *ast_channel_dtmf_tv(chan)) < option_dtmfminduration) {
						f->len = ast_tvdiff_ms(now, *ast_channel_dtmf_tv(chan));
						ast_log(LOG_DTMF, "DTMF end '%c' detected to have actual duration %ld on the wire, emulation will be triggered on %s\n", f->subclass.integer, f->len, ast_channel_name(chan));
					}
				} else if (!f->len) {
					ast_log(LOG_DTMF, "DTMF end accepted without begin '%c' on %s\n", f->subclass.integer, ast_channel_name(chan));
					f->len = option_dtmfminduration;
				}
				if (f->len < option_dtmfminduration && !ast_test_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY)) {
					ast_log(LOG_DTMF, "DTMF end '%c' has duration %ld but want minimum %u, emulating on %s\n", f->subclass.integer, f->len, option_dtmfminduration, ast_channel_name(chan));
					ast_set_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF);
					ast_channel_dtmf_digit_to_emulate_set(chan, f->subclass.integer);
					ast_channel_emulate_dtmf_duration_set(chan, option_dtmfminduration - f->len);
					ast_frfree(f);
					f = &ast_null_frame;
				} else {
					ast_log(LOG_DTMF, "DTMF end passthrough '%c' on %s\n", f->subclass.integer, ast_channel_name(chan));
					if (f->len < option_dtmfminduration) {
						f->len = option_dtmfminduration;
					}
					ast_channel_dtmf_tv_set(chan, &now);
				}
				if (ast_channel_audiohooks(chan)) {
					struct ast_frame *old_frame = f;
					f = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_READ, f);
					if (old_frame != f)
						ast_frfree(old_frame);
				}
			}
			break;
		case AST_FRAME_DTMF_BEGIN:
			send_dtmf_event(chan, "Received", f->subclass.integer, "Yes", "No");
			ast_log(LOG_DTMF, "DTMF begin '%c' received on %s\n", f->subclass.integer, ast_channel_name(chan));
			if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_DEFER_DTMF | AST_FLAG_END_DTMF_ONLY | AST_FLAG_EMULATE_DTMF) ||
			    (!ast_tvzero(*ast_channel_dtmf_tv(chan)) &&
			      ast_tvdiff_ms(ast_tvnow(), *ast_channel_dtmf_tv(chan)) < AST_MIN_DTMF_GAP) ) {
				ast_log(LOG_DTMF, "DTMF begin ignored '%c' on %s\n", f->subclass.integer, ast_channel_name(chan));
				ast_frfree(f);
				f = &ast_null_frame;
			} else {
				struct timeval now = ast_tvnow();
				ast_set_flag(ast_channel_flags(chan), AST_FLAG_IN_DTMF);
				ast_channel_dtmf_tv_set(chan, &now);
				ast_log(LOG_DTMF, "DTMF begin passthrough '%c' on %s\n", f->subclass.integer, ast_channel_name(chan));
			}
			break;
		case AST_FRAME_NULL:
			/* The EMULATE_DTMF flag must be cleared here as opposed to when the duration
			 * is reached , because we want to make sure we pass at least one
			 * voice frame through before starting the next digit, to ensure a gap
			 * between DTMF digits. */
			if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF)) {
				struct timeval now = ast_tvnow();
				if (!ast_channel_emulate_dtmf_duration(chan)) {
					ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF);
					ast_channel_dtmf_digit_to_emulate_set(chan, 0);
				} else if (ast_tvdiff_ms(now, *ast_channel_dtmf_tv(chan)) >= ast_channel_emulate_dtmf_duration(chan)) {
					ast_channel_emulate_dtmf_duration_set(chan, 0);
					ast_frfree(f);
					f = ast_channel_dtmff(chan);
					f->frametype = AST_FRAME_DTMF_END;
					f->subclass.integer = ast_channel_dtmf_digit_to_emulate(chan);
					f->len = ast_tvdiff_ms(now, *ast_channel_dtmf_tv(chan));
					ast_channel_dtmf_tv_set(chan, &now);
					ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF);
					ast_channel_dtmf_digit_to_emulate_set(chan, 0);
					ast_log(LOG_DTMF, "DTMF end emulation of '%c' queued on %s\n", f->subclass.integer, ast_channel_name(chan));
					if (ast_channel_audiohooks(chan)) {
						struct ast_frame *old_frame = f;
						f = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_READ, f);
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
			if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF) && !ast_channel_emulate_dtmf_duration(chan)) {
				ast_clear_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF);
				ast_channel_dtmf_digit_to_emulate_set(chan, 0);
			}

			if (dropaudio || ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_DTMF)) {
				if (dropaudio)
					ast_read_generator_actions(chan, f);
				ast_frfree(f);
				f = &ast_null_frame;
			}

			if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF) && !ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_DTMF)) {
				struct timeval now = ast_tvnow();
				if (ast_tvdiff_ms(now, *ast_channel_dtmf_tv(chan)) >= ast_channel_emulate_dtmf_duration(chan)) {
					ast_channel_emulate_dtmf_duration_set(chan, 0);
					ast_frfree(f);
					f = ast_channel_dtmff(chan);
					f->frametype = AST_FRAME_DTMF_END;
					f->subclass.integer = ast_channel_dtmf_digit_to_emulate(chan);
					f->len = ast_tvdiff_ms(now, *ast_channel_dtmf_tv(chan));
					ast_channel_dtmf_tv_set(chan, &now);
					if (ast_channel_audiohooks(chan)) {
						struct ast_frame *old_frame = f;
						f = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_READ, f);
						if (old_frame != f)
							ast_frfree(old_frame);
					}
					ast_log(LOG_DTMF, "DTMF end emulation of '%c' queued on %s\n", f->subclass.integer, ast_channel_name(chan));
				} else {
					/* Drop voice frames while we're still in the middle of the digit */
					ast_frfree(f);
					f = &ast_null_frame;
				}
			} else if ((f->frametype == AST_FRAME_VOICE) && !ast_format_cap_iscompatible(ast_channel_nativeformats(chan), &f->subclass.format)) {
				/* This frame is not one of the current native formats -- drop it on the floor */
				char to[200];
				ast_log(LOG_NOTICE, "Dropping incompatible voice frame on %s of format %s since our native format has changed to %s\n",
					ast_channel_name(chan), ast_getformatname(&f->subclass.format), ast_getformatname_multiple(to, sizeof(to), ast_channel_nativeformats(chan)));
				ast_frfree(f);
				f = &ast_null_frame;
			} else if ((f->frametype == AST_FRAME_VOICE)) {
				/* Send frame to audiohooks if present */
				if (ast_channel_audiohooks(chan)) {
					struct ast_frame *old_frame = f;
					f = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_READ, f);
					if (old_frame != f)
						ast_frfree(old_frame);
				}
				if (ast_channel_monitor(chan) && ast_channel_monitor(chan)->read_stream ) {
					/* XXX what does this do ? */
#ifndef MONITOR_CONSTANT_DELAY
					int jump = ast_channel_outsmpl(chan) - ast_channel_insmpl(chan) - 4 * f->samples;
					if (jump >= 0) {
						jump = calc_monitor_jump((ast_channel_outsmpl(chan) - ast_channel_insmpl(chan)), ast_format_rate(&f->subclass.format), ast_format_rate(&ast_channel_monitor(chan)->read_stream->fmt->format));
						if (ast_seekstream(ast_channel_monitor(chan)->read_stream, jump, SEEK_FORCECUR) == -1) {
							ast_log(LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
						}
						ast_channel_insmpl_set(chan, ast_channel_insmpl(chan) + (ast_channel_outsmpl(chan) - ast_channel_insmpl(chan)) + f->samples);
					} else {
						ast_channel_insmpl_set(chan, ast_channel_insmpl(chan) + f->samples);
					}
#else
					int jump = calc_monitor_jump((ast_channel_outsmpl(chan) - ast_channel_insmpl(chan)), ast_format_rate(f->subclass.codec), ast_format_rate(ast_channel_monitor(chan)->read_stream->fmt->format));
					if (jump - MONITOR_DELAY >= 0) {
						if (ast_seekstream(ast_channel_monitor(chan)->read_stream, jump - f->samples, SEEK_FORCECUR) == -1)
							ast_log(LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
						ast_channel_insmpl(chan) += ast_channel_outsmpl(chan) - ast_channel_insmpl(chan);
					} else
						ast_channel_insmpl(chan) += f->samples;
#endif
					if (ast_channel_monitor(chan)->state == AST_MONITOR_RUNNING) {
						if (ast_writestream(ast_channel_monitor(chan)->read_stream, f) < 0)
							ast_log(LOG_WARNING, "Failed to write data to channel monitor read stream\n");
					}
				}

				if (ast_channel_readtrans(chan) && (f = ast_translate(ast_channel_readtrans(chan), f, 1)) == NULL) {
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
		if (!ast_channel_softhangup_internal_flag(chan)) {
			ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
		}
		if (cause)
			ast_channel_hangupcause_set(chan, cause);
		if (ast_channel_generator(chan))
			ast_deactivate_generator(chan);
		/* We no longer End the CDR here */
	}

	/* High bit prints debugging */
	if (ast_channel_fin(chan) & DEBUGCHAN_FLAG)
		ast_frame_dump(ast_channel_name(chan), f, "<<");
	ast_channel_fin_set(chan, FRAMECOUNT_INC(ast_channel_fin(chan)));

done:
	if (ast_channel_music_state(chan) && ast_channel_generator(chan) && ast_channel_generator(chan)->digit && f && f->frametype == AST_FRAME_DTMF_END)
		ast_channel_generator(chan)->digit(chan, f->subclass.integer);

	if (ast_channel_audiohooks(chan) && ast_audiohook_write_list_empty(ast_channel_audiohooks(chan))) {
		/* The list gets recreated if audiohooks are added again later */
		ast_audiohook_detach_list(ast_channel_audiohooks(chan));
		ast_channel_audiohooks_set(chan, NULL);
	}
	ast_channel_unlock(chan);
	return f;
}

int ast_internal_timing_enabled(struct ast_channel *chan)
{
	return ast_channel_timingfd(chan) > -1;
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
	case AST_CONTROL_END_OF_Q:
	case AST_CONTROL_MCID:
	case AST_CONTROL_UPDATE_RTP_PEER:
	case AST_CONTROL_PVT_CAUSE_CODE:
		break;

	case AST_CONTROL_INCOMPLETE:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_BUSY:
	case AST_CONTROL_RINGING:
	case AST_CONTROL_RING:
	case AST_CONTROL_HOLD:
		/* You can hear these */
		return 1;

	case AST_CONTROL_UNHOLD:
		/* This is a special case.  You stop hearing this. */
		break;
	}

	return 0;
}

void ast_channel_hangupcause_hash_set(struct ast_channel *chan, const struct ast_control_pvt_cause_code *cause_code, int datalen)
{
	char causevar[256];

	if (ast_channel_dialed_causes_add(chan, cause_code, datalen)) {
		ast_log(LOG_WARNING, "Unable to store hangup cause for %s on %s\n", cause_code->chan_name, ast_channel_name(chan));
	}

	if (cause_code->emulate_sip_cause) {
		snprintf(causevar, sizeof(causevar), "HASH(SIP_CAUSE,%s)", cause_code->chan_name);
		ast_func_write(chan, causevar, cause_code->code);
	}
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
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		res = -1;
		goto indicate_cleanup;
	}

	if (!ast_framehook_list_is_empty(ast_channel_framehooks(chan))) {
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
		if (!(awesome_frame = ast_framehook_list_write_event(ast_channel_framehooks(chan), awesome_frame))
			|| awesome_frame->frametype != AST_FRAME_CONTROL) {

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

			ast_party_connected_line_set_init(&connected, ast_channel_connected(chan));
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

			ast_party_redirecting_set_init(&redirecting, ast_channel_redirecting(chan));
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

	if (is_visible_indication(condition)) {
		/* A new visible indication is requested. */
		ast_channel_visible_indication_set(chan, condition);
	} else if (condition == AST_CONTROL_UNHOLD || _condition < 0) {
		/* Visible indication is cleared/stopped. */
		ast_channel_visible_indication_set(chan, 0);
	}

	if (ast_channel_tech(chan)->indicate) {
		/* See if the channel driver can handle this condition. */
		res = ast_channel_tech(chan)->indicate(chan, condition, data, datalen);
	} else {
		res = -1;
	}

	if (!res) {
		/* The channel driver successfully handled this indication */
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
		ts = ast_get_indication_tone(ast_channel_zone(chan), "ring");
		/* It is common practice for channel drivers to return -1 if trying
		 * to indicate ringing on a channel which is up. The idea is to let the
		 * core generate the ringing inband. However, we don't want the
		 * warning message about not being able to handle the specific indication
		 * to print nor do we want ast_indicate_data to return an "error" for this
		 * condition
		 */
		if (ast_channel_state(chan) == AST_STATE_UP) {
			res = 0;
		}
		break;
	case AST_CONTROL_BUSY:
		ts = ast_get_indication_tone(ast_channel_zone(chan), "busy");
		break;
	case AST_CONTROL_INCOMPLETE:
	case AST_CONTROL_CONGESTION:
		ts = ast_get_indication_tone(ast_channel_zone(chan), "congestion");
		break;
	case AST_CONTROL_PVT_CAUSE_CODE:
		ast_channel_hangupcause_hash_set(chan, data, datalen);
		res = 0;
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
	case AST_CONTROL_END_OF_Q:
	case AST_CONTROL_MCID:
	case AST_CONTROL_UPDATE_RTP_PEER:
		/* Nothing left to do for these. */
		res = 0;
		break;
	}

	if (ts) {
		/* We have a tone to play, yay. */
		ast_debug(1, "Driver for channel '%s' does not support indication %u, emulating it\n", ast_channel_name(chan), condition);
		res = ast_playtones_start(chan, 0, ts->data, 1);
		ts = ast_tone_zone_sound_unref(ts);
	}

	if (res) {
		/* not handled */
		ast_log(LOG_WARNING, "Unable to handle indication %u for '%s'\n", condition, ast_channel_name(chan));
	}

indicate_cleanup:
	ast_channel_unlock(chan);
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
	int res;
	char *buf = NULL;
	struct timeval start = ast_tvnow();
	int ms;

	while ((ms = ast_remaining_ms(start, timeout))) {
		struct ast_frame *f;

		if (ast_check_hangup(chan)) {
			break;
		}
		res = ast_waitfor(chan, ms);
		if (res <= 0)  {/* timeout or error */
			break;
		}
		f = ast_read(chan);
		if (f == NULL) {
			break; /* no frame */
		}
		if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_frfree(f);
			break;
		} else if (f->frametype == AST_FRAME_TEXT) {		/* what we want */
			buf = ast_strndup((char *) f->data.ptr, f->datalen);	/* dup and break */
			ast_frfree(f);
			break;
		}
		ast_frfree(f);
	}
	return buf;
}

int ast_sendtext(struct ast_channel *chan, const char *text)
{
	int res = 0;

	ast_channel_lock(chan);
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		ast_channel_unlock(chan);
		return -1;
	}

	if (ast_strlen_zero(text)) {
		ast_channel_unlock(chan);
		return 0;
	}

	CHECK_BLOCKING(chan);
	if (ast_channel_tech(chan)->write_text && (ast_format_cap_has_type(ast_channel_nativeformats(chan), AST_FORMAT_TYPE_TEXT))) {
		struct ast_frame f;

		f.frametype = AST_FRAME_TEXT;
		f.src = "DIALPLAN";
		f.mallocd = AST_MALLOCD_DATA;
		f.datalen = strlen(text);
		f.data.ptr = ast_strdup(text);
		f.offset = 0;
		f.seqno = 0;

		ast_format_set(&f.subclass.format, AST_FORMAT_T140, 0);
		res = ast_channel_tech(chan)->write_text(chan, &f);
	} else if (ast_channel_tech(chan)->send_text) {
		res = ast_channel_tech(chan)->send_text(chan, text);
	}
	ast_clear_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING);
	ast_channel_unlock(chan);
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

	if (!ast_channel_tech(chan)->send_digit_begin)
		return 0;

	ast_channel_lock(chan);
	ast_channel_sending_dtmf_digit_set(chan, digit);
	ast_channel_sending_dtmf_tv_set(chan, ast_tvnow());
	ast_channel_unlock(chan);

	if (!ast_channel_tech(chan)->send_digit_begin(chan, digit))
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
		ast_debug(1, "Unable to generate DTMF tone '%c' for '%s'\n", digit, ast_channel_name(chan));
	}

	return 0;
}

int ast_senddigit_end(struct ast_channel *chan, char digit, unsigned int duration)
{
	int res = -1;

	if (ast_channel_tech(chan)->send_digit_end)
		res = ast_channel_tech(chan)->send_digit_end(chan, digit, duration);

	ast_channel_lock(chan);
	if (ast_channel_sending_dtmf_digit(chan) == digit) {
		ast_channel_sending_dtmf_digit_set(chan, 0);
	}
	ast_channel_unlock(chan);

	if (res && ast_channel_generator(chan))
		ast_playtones_stop(chan);

	return 0;
}

int ast_senddigit(struct ast_channel *chan, char digit, unsigned int duration)
{
	if (ast_channel_tech(chan)->send_digit_begin) {
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
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_debug(1, "Prodding channel '%s'\n", ast_channel_name(chan));
		ast_format_copy(&a.subclass.format, ast_channel_rawwriteformat(chan));
		a.data.ptr = nothing + AST_FRIENDLY_OFFSET;
		a.src = "ast_prod"; /* this better match check in ast_write */
		if (ast_write(chan, &a))
			ast_log(LOG_WARNING, "Prodding channel '%s' failed\n", ast_channel_name(chan));
	}
	return 0;
}

int ast_write_video(struct ast_channel *chan, struct ast_frame *fr)
{
	int res;
	if (!ast_channel_tech(chan)->write_video)
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

static const struct ast_datastore_info plc_ds_info = {
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
			ast_debug(1, "Deadlock avoided for write to channel '%s'\n", ast_channel_name(chan));
			return 0;
		}
		usleep(1);
	}
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE) || ast_check_hangup(chan))
		goto done;

	/* Handle any pending masquerades */
	while (ast_channel_masq(chan)) {
		ast_channel_unlock(chan);
		ast_do_masquerade(chan);
		ast_channel_lock(chan);
	}
	if (ast_channel_masqr(chan)) {
		res = 0;	/* XXX explain, why 0 ? */
		goto done;
	}

	/* Perform the framehook write event here. After the frame enters the framehook list
	 * there is no telling what will happen, how awesome is that!!! */
	if (!(fr = ast_framehook_list_write_event(ast_channel_framehooks(chan), fr))) {
		res = 0;
		goto done;
	}

	if (ast_channel_generatordata(chan) && (!fr->src || strcasecmp(fr->src, "ast_prod"))) {
		if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_WRITE_INT)) {
				ast_deactivate_generator(chan);
		} else {
			if (fr->frametype == AST_FRAME_DTMF_END) {
				/* There is a generator running while we're in the middle of a digit.
				 * It's probably inband DTMF, so go ahead and pass it so it can
				 * stop the generator */
				ast_clear_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING);
				ast_channel_unlock(chan);
				res = ast_senddigit_end(chan, fr->subclass.integer, fr->len);
				ast_channel_lock(chan);
				CHECK_BLOCKING(chan);
			} else if (fr->frametype == AST_FRAME_CONTROL && fr->subclass.integer == AST_CONTROL_UNHOLD) {
				/* This is a side case where Echo is basically being called and the person put themselves on hold and took themselves off hold */
				res = (ast_channel_tech(chan)->indicate == NULL) ? 0 :
					ast_channel_tech(chan)->indicate(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
			}
			res = 0;	/* XXX explain, why 0 ? */
			goto done;
		}
	}
	/* High bit prints debugging */
	if (ast_channel_fout(chan) & DEBUGCHAN_FLAG)
		ast_frame_dump(ast_channel_name(chan), fr, ">>");
	CHECK_BLOCKING(chan);
	switch (fr->frametype) {
	case AST_FRAME_CONTROL:
		res = (ast_channel_tech(chan)->indicate == NULL) ? 0 :
			ast_channel_tech(chan)->indicate(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		break;
	case AST_FRAME_DTMF_BEGIN:
		if (ast_channel_audiohooks(chan)) {
			struct ast_frame *old_frame = fr;
			fr = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_WRITE, fr);
			if (old_frame != fr)
				f = fr;
		}
		send_dtmf_event(chan, "Sent", fr->subclass.integer, "Yes", "No");
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING);
		ast_channel_unlock(chan);
		res = ast_senddigit_begin(chan, fr->subclass.integer);
		ast_channel_lock(chan);
		CHECK_BLOCKING(chan);
		break;
	case AST_FRAME_DTMF_END:
		if (ast_channel_audiohooks(chan)) {
			struct ast_frame *new_frame = fr;

			new_frame = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_WRITE, fr);
			if (new_frame != fr) {
				ast_frfree(new_frame);
			}
		}
		send_dtmf_event(chan, "Sent", fr->subclass.integer, "No", "Yes");
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING);
		ast_channel_unlock(chan);
		res = ast_senddigit_end(chan, fr->subclass.integer, fr->len);
		ast_channel_lock(chan);
		CHECK_BLOCKING(chan);
		break;
	case AST_FRAME_TEXT:
		if (fr->subclass.integer == AST_FORMAT_T140) {
			res = (ast_channel_tech(chan)->write_text == NULL) ? 0 :
				ast_channel_tech(chan)->write_text(chan, fr);
		} else {
			res = (ast_channel_tech(chan)->send_text == NULL) ? 0 :
				ast_channel_tech(chan)->send_text(chan, (char *) fr->data.ptr);
		}
		break;
	case AST_FRAME_HTML:
		res = (ast_channel_tech(chan)->send_html == NULL) ? 0 :
			ast_channel_tech(chan)->send_html(chan, fr->subclass.integer, (char *) fr->data.ptr, fr->datalen);
		break;
	case AST_FRAME_VIDEO:
		/* XXX Handle translation of video codecs one day XXX */
		res = (ast_channel_tech(chan)->write_video == NULL) ? 0 :
			ast_channel_tech(chan)->write_video(chan, fr);
		break;
	case AST_FRAME_MODEM:
		res = (ast_channel_tech(chan)->write == NULL) ? 0 :
			ast_channel_tech(chan)->write(chan, fr);
		break;
	case AST_FRAME_VOICE:
		if (ast_channel_tech(chan)->write == NULL)
			break;	/*! \todo XXX should return 0 maybe ? */

		if (ast_opt_generic_plc && fr->subclass.format.id == AST_FORMAT_SLINEAR) {
			apply_plc(chan, fr);
		}

		/* If the frame is in the raw write format, then it's easy... just use the frame - otherwise we will have to translate */
		if (ast_format_cmp(&fr->subclass.format, ast_channel_rawwriteformat(chan)) != AST_FORMAT_CMP_NOT_EQUAL) {
			f = fr;
		} else {
			if ((!ast_format_cap_iscompatible(ast_channel_nativeformats(chan), &fr->subclass.format)) &&
			    (ast_format_cmp(ast_channel_writeformat(chan), &fr->subclass.format) != AST_FORMAT_CMP_EQUAL)) {
				char nf[512];

				/*
				 * XXX Something is not right.  We are not compatible with this
				 * frame.  Bad things can happen.  Problems range from no audio,
				 * one-way audio, to unexplained line hangups.  As a last resort
				 * try to adjust the format.  Ideally, we do not want to do this
				 * because it indicates a deeper problem.  For now, we log these
				 * events to reduce user impact and help identify the problem
				 * areas.
				 */
				ast_log(LOG_WARNING, "Codec mismatch on channel %s setting write format to %s from %s native formats %s\n",
					ast_channel_name(chan), ast_getformatname(&fr->subclass.format), ast_getformatname(ast_channel_writeformat(chan)),
					ast_getformatname_multiple(nf, sizeof(nf), ast_channel_nativeformats(chan)));
				ast_set_write_format_by_id(chan, fr->subclass.format.id);
			}

			f = (ast_channel_writetrans(chan)) ? ast_translate(ast_channel_writetrans(chan), fr, 0) : fr;
		}

		if (!f) {
			res = 0;
			break;
		}

		if (ast_channel_audiohooks(chan)) {
			struct ast_frame *prev = NULL, *new_frame, *cur, *dup;
			int freeoldlist = 0;

			if (f != fr) {
				freeoldlist = 1;
			}

			/* Since ast_audiohook_write may return a new frame, and the cur frame is
			 * an item in a list of frames, create a new list adding each cur frame back to it
			 * regardless if the cur frame changes or not. */
			for (cur = f; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
				new_frame = ast_audiohook_write_list(chan, ast_channel_audiohooks(chan), AST_AUDIOHOOK_DIRECTION_WRITE, cur);

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
		if (ast_channel_monitor(chan) && ast_channel_monitor(chan)->write_stream) {
			struct ast_frame *cur;

			for (cur = f; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
			/* XXX must explain this code */
#ifndef MONITOR_CONSTANT_DELAY
				int jump = ast_channel_insmpl(chan) - ast_channel_outsmpl(chan) - 4 * cur->samples;
				if (jump >= 0) {
					jump = calc_monitor_jump((ast_channel_insmpl(chan) - ast_channel_outsmpl(chan)), ast_format_rate(&f->subclass.format), ast_format_rate(&ast_channel_monitor(chan)->read_stream->fmt->format));
					if (ast_seekstream(ast_channel_monitor(chan)->write_stream, jump, SEEK_FORCECUR) == -1) {
						ast_log(LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
					}
					ast_channel_outsmpl_set(chan, ast_channel_outsmpl(chan) + (ast_channel_insmpl(chan) - ast_channel_outsmpl(chan)) + cur->samples);
				} else {
					ast_channel_outsmpl_set(chan, ast_channel_outsmpl(chan) + cur->samples);
				}
#else
				int jump = calc_monitor_jump((ast_channel_insmpl(chan) - ast_channel_outsmpl(chan)), ast_format_rate(f->subclass.codec), ast_format_rate(ast_channel_monitor(chan)->read_stream->fmt->format));
				if (jump - MONITOR_DELAY >= 0) {
					if (ast_seekstream(ast_channel_monitor(chan)->write_stream, jump - cur->samples, SEEK_FORCECUR) == -1) {
						ast_log(LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
					}
					ast_channel_outsmpl_set(chan, ast_channel_outsmpl(chan) + ast_channel_insmpl(chan) - ast_channel_outsmpl(chan));
				} else {
					ast_channel_outsmpl_set(chan, ast_channel_outsmpl(chan) + cur->samples);
				}
#endif
				if (ast_channel_monitor(chan)->state == AST_MONITOR_RUNNING) {
					if (ast_writestream(ast_channel_monitor(chan)->write_stream, cur) < 0)
						ast_log(LOG_WARNING, "Failed to write data to channel monitor write stream\n");
				}
			}
		}

		/* the translator on chan->writetrans may have returned multiple frames
		   from the single frame we passed in; if so, feed each one of them to the
		   channel, freeing each one after it has been written */
		if ((f != fr) && AST_LIST_NEXT(f, frame_list)) {
			struct ast_frame *cur, *next = NULL;
			unsigned int skip = 0;

			cur = f;
			while (cur) {
				next = AST_LIST_NEXT(cur, frame_list);
				AST_LIST_NEXT(cur, frame_list) = NULL;
				if (!skip) {
					if ((res = ast_channel_tech(chan)->write(chan, cur)) < 0) {
						ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
						skip = 1;
					} else if (next) {
						/* don't do this for the last frame in the list,
						   as the code outside the loop will do it once
						*/
						ast_channel_fout_set(chan, FRAMECOUNT_INC(ast_channel_fout(chan)));
					}
				}
				ast_frfree(cur);
				cur = next;
			}

			/* reset f so the code below doesn't attempt to free it */
			f = NULL;
		} else {
			res = ast_channel_tech(chan)->write(chan, f);
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
		res = ast_channel_tech(chan)->write(chan, fr);
		break;
	}

	if (f && f != fr)
		ast_frfree(f);
	ast_clear_flag(ast_channel_flags(chan), AST_FLAG_BLOCKING);

	/* Consider a write failure to force a soft hangup */
	if (res < 0) {
		ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
	} else {
		ast_channel_fout_set(chan, FRAMECOUNT_INC(ast_channel_fout(chan)));
	}
done:
	if (ast_channel_audiohooks(chan) && ast_audiohook_write_list_empty(ast_channel_audiohooks(chan))) {
		/* The list gets recreated if audiohooks are added again later */
		ast_audiohook_detach_list(ast_channel_audiohooks(chan));
		ast_channel_audiohooks_set(chan, NULL);
	}
	ast_channel_unlock(chan);
	return res;
}

struct set_format_trans_access {
	struct ast_trans_pvt *(*get)(const struct ast_channel *chan);
	void (*set)(struct ast_channel *chan, struct ast_trans_pvt *value);
};

static const struct set_format_trans_access set_format_readtrans = {
	.get = ast_channel_readtrans,
	.set = ast_channel_readtrans_set,
};

static const struct set_format_trans_access set_format_writetrans = {
	.get = ast_channel_writetrans,
	.set = ast_channel_writetrans_set,
};

static int set_format(struct ast_channel *chan,
	struct ast_format_cap *cap_set,
	struct ast_format *rawformat,
	struct ast_format *format,
	const struct set_format_trans_access *trans,
	const int direction)
{
	struct ast_trans_pvt *trans_pvt;
	struct ast_format_cap *cap_native = ast_channel_nativeformats(chan);
	struct ast_format best_set_fmt;
	struct ast_format best_native_fmt;
	int res;
	char from[200], to[200];

	ast_best_codec(cap_set, &best_set_fmt);

	/* See if the underlying channel driver is capable of performing transcoding for us */
	if (!ast_channel_setoption(chan, direction ? AST_OPTION_FORMAT_WRITE : AST_OPTION_FORMAT_READ, &best_set_fmt, sizeof(best_set_fmt), 0)) {
		ast_debug(1, "Channel driver natively set channel %s to %s format %s\n", ast_channel_name(chan),
			  direction ? "write" : "read", ast_getformatname(&best_set_fmt));

		ast_channel_lock(chan);
		ast_format_copy(format, &best_set_fmt);
		ast_format_copy(rawformat, &best_set_fmt);
		ast_format_cap_set(ast_channel_nativeformats(chan), &best_set_fmt);
		ast_channel_unlock(chan);

		trans_pvt = trans->get(chan);
		if (trans_pvt) {
			ast_translator_free_path(trans_pvt);
			trans->set(chan, NULL);
		}

		/* If there is a generator on the channel, it needs to know about this
		 * change if it is the write format. */
		if (direction && ast_channel_generatordata(chan)) {
			generator_write_format_change(chan);
		}
		return 0;
	}

	/* Find a translation path from the native format to one of the desired formats */
	if (!direction) {
		/* reading */
		res = ast_translator_best_choice(cap_set, cap_native, &best_set_fmt, &best_native_fmt);
	} else {
		/* writing */
		res = ast_translator_best_choice(cap_native, cap_set, &best_native_fmt, &best_set_fmt);
	}

	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to find a codec translation path from %s to %s\n",
			ast_getformatname_multiple(from, sizeof(from), cap_native),
			ast_getformatname_multiple(to, sizeof(to), cap_set));
		return -1;
	}

	/* Now we have a good choice for both. */
	ast_channel_lock(chan);

	if ((ast_format_cmp(rawformat, &best_native_fmt) != AST_FORMAT_CMP_NOT_EQUAL) &&
		(ast_format_cmp(format, &best_set_fmt) != AST_FORMAT_CMP_NOT_EQUAL) &&
		((ast_format_cmp(rawformat, format) != AST_FORMAT_CMP_NOT_EQUAL) || trans->get(chan))) {
		/* the channel is already in these formats, so nothing to do */
		ast_channel_unlock(chan);
		return 0;
	}

	ast_format_copy(rawformat, &best_native_fmt);
	/* User perspective is fmt */
	ast_format_copy(format, &best_set_fmt);

	/* Free any translation we have right now */
	trans_pvt = trans->get(chan);
	if (trans_pvt) {
		ast_translator_free_path(trans_pvt);
		trans->set(chan, NULL);
	}

	/* Build a translation path from the raw format to the desired format */
	if (ast_format_cmp(format, rawformat) != AST_FORMAT_CMP_NOT_EQUAL) {
		/*
		 * If we were able to swap the native format to the format that
		 * has been requested, then there is no need to try to build
		 * a translation path.
		 */
		res = 0;
	} else {
		if (!direction) {
			/* reading */
			trans_pvt = ast_translator_build_path(format, rawformat);
		} else {
			/* writing */
			trans_pvt = ast_translator_build_path(rawformat, format);
		}
		trans->set(chan, trans_pvt);
		res = trans_pvt ? 0 : -1;
	}
	ast_channel_unlock(chan);

	ast_debug(1, "Set channel %s to %s format %s\n",
		ast_channel_name(chan),
		direction ? "write" : "read",
		ast_getformatname(&best_set_fmt));

	/* If there is a generator on the channel, it needs to know about this
	 * change if it is the write format. */
	if (direction && ast_channel_generatordata(chan)) {
		generator_write_format_change(chan);
	}
	return res;
}

int ast_set_read_format(struct ast_channel *chan, struct ast_format *format)
{
	struct ast_format_cap *cap = ast_format_cap_alloc_nolock();
	int res;

	if (!cap) {
		return -1;
	}
	ast_format_cap_add(cap, format);

	res = set_format(chan,
		cap,
		ast_channel_rawreadformat(chan),
		ast_channel_readformat(chan),
		&set_format_readtrans,
		0);

	ast_format_cap_destroy(cap);
	return res;
}

int ast_set_read_format_by_id(struct ast_channel *chan, enum ast_format_id id)
{
	struct ast_format_cap *cap = ast_format_cap_alloc_nolock();
	struct ast_format tmp_format;
	int res;

	if (!cap) {
		return -1;
	}
	ast_format_cap_add(cap, ast_format_set(&tmp_format, id, 0));

	res = set_format(chan,
		cap,
		ast_channel_rawreadformat(chan),
		ast_channel_readformat(chan),
		&set_format_readtrans,
		0);

	ast_format_cap_destroy(cap);
	return res;
}

int ast_set_read_format_from_cap(struct ast_channel *chan, struct ast_format_cap *cap)
{
	return set_format(chan,
		cap,
		ast_channel_rawreadformat(chan),
		ast_channel_readformat(chan),
		&set_format_readtrans,
		0);
}

int ast_set_write_format(struct ast_channel *chan, struct ast_format *format)
{
	struct ast_format_cap *cap = ast_format_cap_alloc_nolock();
	int res;

	if (!cap) {
		return -1;
	}
	ast_format_cap_add(cap, format);

	res = set_format(chan,
		cap,
		ast_channel_rawwriteformat(chan),
		ast_channel_writeformat(chan),
		&set_format_writetrans,
		1);

	ast_format_cap_destroy(cap);
	return res;
}

int ast_set_write_format_by_id(struct ast_channel *chan, enum ast_format_id id)
{
	struct ast_format_cap *cap = ast_format_cap_alloc_nolock();
	struct ast_format tmp_format;
	int res;

	if (!cap) {
		return -1;
	}
	ast_format_cap_add(cap, ast_format_set(&tmp_format, id, 0));

	res = set_format(chan,
		cap,
		ast_channel_rawwriteformat(chan),
		ast_channel_writeformat(chan),
		&set_format_writetrans,
		1);

	ast_format_cap_destroy(cap);
	return res;
}

int ast_set_write_format_from_cap(struct ast_channel *chan, struct ast_format_cap *cap)
{
	return set_format(chan,
		cap,
		ast_channel_rawwriteformat(chan),
		ast_channel_writeformat(chan),
		&set_format_writetrans,
		1);
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

/*!
 * \internal
 * \brief Helper function to inherit info from parent channel.
 *
 * \param new_chan Channel inheriting information.
 * \param parent Channel new_chan inherits information.
 * \param orig Channel being replaced by the call forward channel.
 *
 * \return Nothing
 */
static void call_forward_inherit(struct ast_channel *new_chan, struct ast_channel *parent, struct ast_channel *orig)
{
	if (!ast_test_flag(ast_channel_flags(parent), AST_FLAG_ZOMBIE) && !ast_check_hangup(parent)) {
		struct ast_party_redirecting redirecting;

		/*
		 * The parent is not a ZOMBIE or hungup so update it with the
		 * original channel's redirecting information.
		 */
		ast_party_redirecting_init(&redirecting);
		ast_channel_lock(orig);
		ast_party_redirecting_copy(&redirecting, ast_channel_redirecting(orig));
		ast_channel_unlock(orig);
		if (ast_channel_redirecting_sub(orig, parent, &redirecting, 0) &&
			ast_channel_redirecting_macro(orig, parent, &redirecting, 1, 0)) {
			ast_channel_update_redirecting(parent, &redirecting, NULL);
		}
		ast_party_redirecting_free(&redirecting);
	}

	/* Safely inherit variables and datastores from the parent channel. */
	ast_channel_lock_both(parent, new_chan);
	ast_channel_inherit_variables(parent, new_chan);
	ast_channel_datastore_inherit(parent, new_chan);
	ast_channel_unlock(new_chan);
	ast_channel_unlock(parent);
}

struct ast_channel *ast_call_forward(struct ast_channel *caller, struct ast_channel *orig, int *timeout, struct ast_format_cap *cap, struct outgoing_helper *oh, int *outstate)
{
	char tmpchan[256];
	struct ast_channel *new_chan = NULL;
	char *data, *type;
	int cause = 0;
	int res;

	/* gather data and request the new forward channel */
	ast_copy_string(tmpchan, ast_channel_call_forward(orig), sizeof(tmpchan));
	if ((data = strchr(tmpchan, '/'))) {
		*data++ = '\0';
		type = tmpchan;
	} else {
		const char *forward_context;
		ast_channel_lock(orig);
		forward_context = pbx_builtin_getvar_helper(orig, "FORWARD_CONTEXT");
		snprintf(tmpchan, sizeof(tmpchan), "%s@%s", ast_channel_call_forward(orig), S_OR(forward_context, ast_channel_context(orig)));
		ast_channel_unlock(orig);
		data = tmpchan;
		type = "Local";
	}
	if (!(new_chan = ast_request(type, cap, orig, data, &cause))) {
		ast_log(LOG_NOTICE, "Unable to create channel for call forward to '%s/%s' (cause = %d)\n", type, data, cause);
		handle_cause(cause, outstate);
		ast_hangup(orig);
		return NULL;
	}

	/* Copy/inherit important information into new channel */
	if (oh) {
		if (oh->vars) {
			ast_set_variables(new_chan, oh->vars);
		}
		if (oh->parent_channel) {
			call_forward_inherit(new_chan, oh->parent_channel, orig);
		}
		if (oh->account) {
			ast_channel_lock(new_chan);
			ast_cdr_setaccount(new_chan, oh->account);
			ast_channel_unlock(new_chan);
		}
	} else if (caller) { /* no outgoing helper so use caller if available */
		call_forward_inherit(new_chan, caller, orig);
	}

	ast_channel_lock_both(orig, new_chan);
	ast_copy_flags(ast_channel_cdr(new_chan), ast_channel_cdr(orig), AST_CDR_FLAG_ORIGINATED);
	ast_channel_accountcode_set(new_chan, ast_channel_accountcode(orig));
	ast_party_connected_line_copy(ast_channel_connected(new_chan), ast_channel_connected(orig));
	ast_party_redirecting_copy(ast_channel_redirecting(new_chan), ast_channel_redirecting(orig));
	ast_channel_unlock(new_chan);
	ast_channel_unlock(orig);

	/* call new channel */
	res = ast_call(new_chan, data, 0);
	if (timeout) {
		*timeout = res;
	}
	if (res) {
		ast_log(LOG_NOTICE, "Unable to call forward to channel %s/%s\n", type, (char *)data);
		ast_hangup(orig);
		ast_hangup(new_chan);
		return NULL;
	}
	ast_hangup(orig);

	return new_chan;
}

struct ast_channel *__ast_request_and_dial(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *addr, int timeout, int *outstate, const char *cid_num, const char *cid_name, struct outgoing_helper *oh)
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

	chan = ast_request(type, cap, requestor, addr, &cause);
	if (!chan) {
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, addr);
		handle_cause(cause, outstate);
		return NULL;
	}

	if (oh) {
		if (oh->vars) {
			ast_set_variables(chan, oh->vars);
		}
		if (!ast_strlen_zero(oh->cid_num) && !ast_strlen_zero(oh->cid_name)) {
			/*
			 * Use the oh values instead of the function parameters for the
			 * outgoing CallerID.
			 */
			cid_num = oh->cid_num;
			cid_name = oh->cid_name;
		}
		if (oh->parent_channel) {
			/* Safely inherit variables and datastores from the parent channel. */
			ast_channel_lock_both(oh->parent_channel, chan);
			ast_channel_inherit_variables(oh->parent_channel, chan);
			ast_channel_datastore_inherit(oh->parent_channel, chan);
			ast_channel_unlock(oh->parent_channel);
			ast_channel_unlock(chan);
		}
		if (oh->account) {
			ast_channel_lock(chan);
			ast_cdr_setaccount(chan, oh->account);
			ast_channel_unlock(chan);
		}
	}

	/*
	 * I seems strange to set the CallerID on an outgoing call leg
	 * to whom we are calling, but this function's callers are doing
	 * various Originate methods.  This call leg goes to the local
	 * user.  Once the local user answers, the dialplan needs to be
	 * able to access the CallerID from the CALLERID function as if
	 * the local user had placed this call.
	 */
	ast_set_callerid(chan, cid_num, cid_name, cid_num);

	ast_set_flag(ast_channel_cdr(chan), AST_CDR_FLAG_ORIGINATED);
	ast_party_connected_line_set_init(&connected, ast_channel_connected(chan));
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

	if (ast_call(chan, addr, 0)) {	/* ast_call failed... */
		ast_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, addr);
	} else {
		struct timeval start = ast_tvnow();
		res = 1;	/* mark success in case chan->_state is already AST_STATE_UP */
		while (timeout && ast_channel_state(chan) != AST_STATE_UP) {
			struct ast_frame *f;
			int ms = ast_remaining_ms(start, timeout);

			res = ast_waitfor(chan, ms);
			if (res == 0) { /* timeout, treat it like ringing */
				*outstate = AST_CONTROL_RINGING;
				break;
			}
			if (res < 0) /* error or done */
				break;
			if (!ast_strlen_zero(ast_channel_call_forward(chan))) {
				if (!(chan = ast_call_forward(NULL, chan, NULL, cap, oh, outstate))) {
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
					ast_cdr_busy(ast_channel_cdr(chan));
					*outstate = f->subclass.integer;
					timeout = 0;
					break;

				case AST_CONTROL_INCOMPLETE:
					ast_cdr_failed(ast_channel_cdr(chan));
					*outstate = AST_CONTROL_CONGESTION;
					timeout = 0;
					break;

				case AST_CONTROL_CONGESTION:
					ast_cdr_failed(ast_channel_cdr(chan));
					*outstate = f->subclass.integer;
					timeout = 0;
					break;

				case AST_CONTROL_ANSWER:
					ast_cdr_answer(ast_channel_cdr(chan));
					*outstate = f->subclass.integer;
					timeout = 0;		/* trick to force exit from the while() */
					break;

				case AST_CONTROL_PVT_CAUSE_CODE:
					ast_channel_hangupcause_hash_set(chan, f->data.ptr, f->datalen);
					break;

				case AST_CONTROL_PROGRESS:
					if (oh && oh->connect_on_early_media) {
						*outstate = f->subclass.integer;
						timeout = 0;		/* trick to force exit from the while() */
						break;
					}
					/* Fallthrough */
				/* Ignore these */
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
			ast_channel_context_set(chan, oh->context);
		if (!ast_strlen_zero(oh->exten))
			ast_channel_exten_set(chan, oh->exten);
		if (oh->priority)
			ast_channel_priority_set(chan, oh->priority);
	}
	if (ast_channel_state(chan) == AST_STATE_UP)
		*outstate = AST_CONTROL_ANSWER;

	if (res <= 0) {
		struct ast_cdr *chancdr;
		ast_channel_lock(chan);
		if (AST_CONTROL_RINGING == last_subclass) {
			ast_channel_hangupcause_set(chan, AST_CAUSE_NO_ANSWER);
		}
		if (!ast_channel_cdr(chan) && (chancdr = ast_cdr_alloc())) {
			ast_channel_cdr_set(chan, chancdr);
			ast_cdr_init(ast_channel_cdr(chan), chan);
		}
		if (ast_channel_cdr(chan)) {
			char tmp[256];

			snprintf(tmp, sizeof(tmp), "%s/%s", type, addr);
			ast_cdr_setapp(ast_channel_cdr(chan), "Dial", tmp);
			ast_cdr_update(chan);
			ast_cdr_start(ast_channel_cdr(chan));
			ast_cdr_end(ast_channel_cdr(chan));
			/* If the cause wasn't handled properly */
			if (ast_cdr_disposition(ast_channel_cdr(chan), ast_channel_hangupcause(chan))) {
				ast_cdr_failed(ast_channel_cdr(chan));
			}
		}
		ast_channel_unlock(chan);
		ast_hangup(chan);
		chan = NULL;
	}
	return chan;
}

struct ast_channel *ast_request_and_dial(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *addr, int timeout, int *outstate, const char *cidnum, const char *cidname)
{
	return __ast_request_and_dial(type, cap, requestor, addr, timeout, outstate, cidnum, cidname, NULL);
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

struct ast_channel *ast_request(const char *type, struct ast_format_cap *request_cap, const struct ast_channel *requestor, const char *addr, int *cause)
{
	struct chanlist *chan;
	struct ast_channel *c;
	int res;
	int foo;

	if (!cause)
		cause = &foo;
	*cause = AST_CAUSE_NOTDEFINED;

	if (AST_RWLIST_RDLOCK(&backends)) {
		ast_log(LOG_WARNING, "Unable to lock technology backend list\n");
		return NULL;
	}

	AST_RWLIST_TRAVERSE(&backends, chan, list) {
		struct ast_format_cap *tmp_cap;
		struct ast_format tmp_fmt;
		struct ast_format best_audio_fmt;
		struct ast_format_cap *joint_cap;

		if (strcasecmp(type, chan->tech->type))
			continue;

		ast_format_clear(&best_audio_fmt);
		/* find the best audio format to use */
		if ((tmp_cap = ast_format_cap_get_type(request_cap, AST_FORMAT_TYPE_AUDIO))) {
			/* We have audio - is it possible to connect the various calls to each other?
				(Avoid this check for calls without audio, like text+video calls)
			*/
			res = ast_translator_best_choice(tmp_cap, chan->tech->capabilities, &tmp_fmt, &best_audio_fmt);
			ast_format_cap_destroy(tmp_cap);
			if (res < 0) {
				char tmp1[256], tmp2[256];
				ast_log(LOG_WARNING, "No translator path exists for channel type %s (native %s) to %s\n", type,
					ast_getformatname_multiple(tmp1, sizeof(tmp1), chan->tech->capabilities),
					ast_getformatname_multiple(tmp2, sizeof(tmp2), request_cap));
				*cause = AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
				AST_RWLIST_UNLOCK(&backends);
				return NULL;
			}
		}
		AST_RWLIST_UNLOCK(&backends);
		if (!chan->tech->requester)
			return NULL;

		/* XXX Only the audio format calculated as being the best for translation
		 * purposes is used for the request. This needs to be re-evaluated.  It may be
		 * a better choice to send all the audio formats capable of being translated
		 * during the request and allow the channel drivers to pick the best one. */
		if (!(joint_cap = ast_format_cap_dup(request_cap))) {
			return NULL;
		}
		ast_format_cap_remove_bytype(joint_cap, AST_FORMAT_TYPE_AUDIO);
		ast_format_cap_add(joint_cap, &best_audio_fmt);

		if (!(c = chan->tech->requester(type, joint_cap, requestor, addr, cause))) {
			ast_format_cap_destroy(joint_cap);
			return NULL;
		}

		/* Set newly created channel callid to same as the requestor */
		if (requestor) {
			struct ast_callid *callid = ast_channel_callid(requestor);
			if (callid) {
				ast_channel_callid_set(c, callid);
				callid = ast_callid_unref(callid);
			}
		}

		joint_cap = ast_format_cap_destroy(joint_cap);

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

int ast_pre_call(struct ast_channel *chan, const char *sub_args)
{
	int (*pre_call)(struct ast_channel *chan, const char *sub_args);

	ast_channel_lock(chan);
	pre_call = ast_channel_tech(chan)->pre_call;
	if (pre_call) {
		int res;

		res = pre_call(chan, sub_args);
		ast_channel_unlock(chan);
		return res;
	}
	ast_channel_unlock(chan);
	return ast_app_exec_sub(NULL, chan, sub_args, 0);
}

int ast_call(struct ast_channel *chan, const char *addr, int timeout)
{
	/* Place an outgoing call, but don't wait any longer than timeout ms before returning.
	   If the remote end does not answer within the timeout, then do NOT hang up, but
	   return anyway.  */
	int res = -1;
	/* Stop if we're a zombie or need a soft hangup */
	ast_channel_lock(chan);
	if (!ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE) && !ast_check_hangup(chan)) {
		if (ast_channel_cdr(chan)) {
			ast_set_flag(ast_channel_cdr(chan), AST_CDR_FLAG_DIALED);
		}
		if (ast_channel_tech(chan)->call)
			res = ast_channel_tech(chan)->call(chan, addr, timeout);
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);
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
	if (!ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE) && !ast_check_hangup(chan)) {
		if (ast_channel_tech(chan)->transfer) {
			res = ast_channel_tech(chan)->transfer(chan, dest);
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
	if (ast_test_flag(ast_channel_flags(c), AST_FLAG_ZOMBIE) || ast_check_hangup(c))
		return -1;
	if (!len)
		return -1;
	for (;;) {
		int d;
		if (ast_channel_stream(c)) {
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
	return (ast_channel_tech(chan)->send_html) ? 1 : 0;
}

int ast_channel_sendhtml(struct ast_channel *chan, int subclass, const char *data, int datalen)
{
	if (ast_channel_tech(chan)->send_html)
		return ast_channel_tech(chan)->send_html(chan, subclass, data, datalen);
	return -1;
}

int ast_channel_sendurl(struct ast_channel *chan, const char *url)
{
	return ast_channel_sendhtml(chan, AST_HTML_URL, url, strlen(url) + 1);
}

/*! \brief Set up translation from one channel to another */
static int ast_channel_make_compatible_helper(struct ast_channel *from, struct ast_channel *to)
{
	struct ast_format_cap *src_cap = ast_channel_nativeformats(from); /* shallow copy, do not destroy */
	struct ast_format_cap *dst_cap = ast_channel_nativeformats(to);   /* shallow copy, do not destroy */
	struct ast_format best_src_fmt;
	struct ast_format best_dst_fmt;
	int use_slin;

	/* See if the channel driver can natively make these two channels compatible */
	if (ast_channel_tech(from)->bridge && ast_channel_tech(from)->bridge == ast_channel_tech(to)->bridge &&
	    !ast_channel_setoption(from, AST_OPTION_MAKE_COMPATIBLE, to, sizeof(struct ast_channel *), 0)) {
		return 0;
	}

	if ((ast_format_cmp(ast_channel_readformat(from), ast_channel_writeformat(to)) != AST_FORMAT_CMP_NOT_EQUAL) &&
		(ast_format_cmp(ast_channel_readformat(to), ast_channel_writeformat(from)) != AST_FORMAT_CMP_NOT_EQUAL)) {
		/* Already compatible!  Moving on ... */
		return 0;
	}

	/* If there's no audio in this call, don't bother with trying to find a translation path */
	if (!ast_format_cap_has_type(src_cap, AST_FORMAT_TYPE_AUDIO) || !ast_format_cap_has_type(dst_cap, AST_FORMAT_TYPE_AUDIO))
		return 0;

	if (ast_translator_best_choice(dst_cap, src_cap, &best_src_fmt, &best_dst_fmt) < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s to %s\n", ast_channel_name(from), ast_channel_name(to));
		return -1;
	}

	/* if the best path is not 'pass through', then
	 * transcoding is needed; if desired, force transcode path
	 * to use SLINEAR between channels, but only if there is
	 * no direct conversion available. If generic PLC is
	 * desired, then transcoding via SLINEAR is a requirement
	 */
	use_slin = ast_format_is_slinear(&best_src_fmt) || ast_format_is_slinear(&best_dst_fmt) ? 1 : 0;
	if ((ast_format_cmp(&best_src_fmt, &best_dst_fmt) == AST_FORMAT_CMP_NOT_EQUAL) &&
		(ast_opt_generic_plc || ast_opt_transcode_via_slin) &&
	    (ast_translate_path_steps(&best_dst_fmt, &best_src_fmt) != 1 || use_slin)) {

		int best_sample_rate = ast_format_rate(&best_src_fmt) > ast_format_rate(&best_dst_fmt) ?
			ast_format_rate(&best_src_fmt) : ast_format_rate(&best_dst_fmt);

		/* pick the best signed linear format based upon what preserves the sample rate the best. */
		ast_format_set(&best_dst_fmt, ast_format_slin_by_rate(best_sample_rate), 0);
	}

	if (ast_set_read_format(from, &best_dst_fmt) < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %s\n", ast_channel_name(from), ast_getformatname(&best_dst_fmt));
		return -1;
	}
	if (ast_set_write_format(to, &best_dst_fmt) < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %s\n", ast_channel_name(to), ast_getformatname(&best_dst_fmt));
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

static int __ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clonechan, struct ast_datastore *xfer_ds)
{
	int res = -1;
	struct ast_channel *final_orig, *final_clone, *base;

	for (;;) {
		final_orig = original;
		final_clone = clonechan;

		ast_channel_lock_both(original, clonechan);

		if (ast_test_flag(ast_channel_flags(original), AST_FLAG_ZOMBIE)
			|| ast_test_flag(ast_channel_flags(clonechan), AST_FLAG_ZOMBIE)) {
			/* Zombies! Run! */
			ast_log(LOG_WARNING,
				"Can't setup masquerade. One or both channels is dead. (%s <-- %s)\n",
				ast_channel_name(original), ast_channel_name(clonechan));
			ast_channel_unlock(clonechan);
			ast_channel_unlock(original);
			return -1;
		}

		/*
		 * Each of these channels may be sitting behind a channel proxy
		 * (i.e. chan_agent) and if so, we don't really want to
		 * masquerade it, but its proxy
		 */
		if (ast_channel_internal_bridged_channel(original)
			&& (ast_channel_internal_bridged_channel(original) != ast_bridged_channel(original))
			&& (ast_channel_internal_bridged_channel(ast_channel_internal_bridged_channel(original)) != original)) {
			final_orig = ast_channel_internal_bridged_channel(original);
		}
		if (ast_channel_internal_bridged_channel(clonechan)
			&& (ast_channel_internal_bridged_channel(clonechan) != ast_bridged_channel(clonechan))
			&& (ast_channel_internal_bridged_channel(ast_channel_internal_bridged_channel(clonechan)) != clonechan)) {
			final_clone = ast_channel_internal_bridged_channel(clonechan);
		}
		if (ast_channel_tech(final_clone)->get_base_channel
			&& (base = ast_channel_tech(final_clone)->get_base_channel(final_clone))) {
			final_clone = base;
		}

		if ((final_orig != original) || (final_clone != clonechan)) {
			/*
			 * Lots and lots of deadlock avoidance.  The main one we're
			 * competing with is ast_write(), which locks channels
			 * recursively, when working with a proxy channel.
			 */
			if (ast_channel_trylock(final_orig)) {
				ast_channel_unlock(clonechan);
				ast_channel_unlock(original);

				/* Try again */
				continue;
			}
			if (ast_channel_trylock(final_clone)) {
				ast_channel_unlock(final_orig);
				ast_channel_unlock(clonechan);
				ast_channel_unlock(original);

				/* Try again */
				continue;
			}
			ast_channel_unlock(clonechan);
			ast_channel_unlock(original);
			original = final_orig;
			clonechan = final_clone;

			if (ast_test_flag(ast_channel_flags(original), AST_FLAG_ZOMBIE)
				|| ast_test_flag(ast_channel_flags(clonechan), AST_FLAG_ZOMBIE)) {
				/* Zombies! Run! */
				ast_log(LOG_WARNING,
					"Can't setup masquerade. One or both channels is dead. (%s <-- %s)\n",
					ast_channel_name(original), ast_channel_name(clonechan));
				ast_channel_unlock(clonechan);
				ast_channel_unlock(original);
				return -1;
			}
		}
		break;
	}

	if (original == clonechan) {
		ast_log(LOG_WARNING, "Can't masquerade channel '%s' into itself!\n", ast_channel_name(original));
		ast_channel_unlock(clonechan);
		ast_channel_unlock(original);
		return -1;
	}

	ast_debug(1, "Planning to masquerade channel %s into the structure of %s\n",
		ast_channel_name(clonechan), ast_channel_name(original));

	if (!ast_channel_masqr(original) && !ast_channel_masq(original) && !ast_channel_masq(clonechan) && !ast_channel_masqr(clonechan)) {
		ast_channel_masq_set(original, clonechan);
		ast_channel_masqr_set(clonechan, original);
		if (xfer_ds) {
			ast_channel_datastore_add(original, xfer_ds);
		}
		ast_queue_frame(original, &ast_null_frame);
		ast_queue_frame(clonechan, &ast_null_frame);
		ast_debug(1, "Done planning to masquerade channel %s into the structure of %s\n", ast_channel_name(clonechan), ast_channel_name(original));
		res = 0;
	} else if (ast_channel_masq(original)) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
			ast_channel_name(ast_channel_masq(original)), ast_channel_name(original));
	} else if (ast_channel_masqr(original)) {
		/* not yet as a previously planned masq hasn't yet happened */
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
			ast_channel_name(original), ast_channel_name(ast_channel_masqr(original)));
	} else if (ast_channel_masq(clonechan)) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
			ast_channel_name(ast_channel_masq(clonechan)), ast_channel_name(clonechan));
	} else { /* (clonechan->masqr) */
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n",
		ast_channel_name(clonechan), ast_channel_name(ast_channel_masqr(clonechan)));
	}

	ast_channel_unlock(clonechan);
	ast_channel_unlock(original);

	return res;
}

int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clone)
{
	return __ast_channel_masquerade(original, clone, NULL);
}

/*!
 * \internal
 * \brief Copy the source connected line information to the destination for a transfer.
 * \since 1.8
 *
 * \param dest Destination connected line
 * \param src Source connected line
 *
 * \return Nothing
 */
static void party_connected_line_copy_transfer(struct ast_party_connected_line *dest, const struct ast_party_connected_line *src)
{
	struct ast_party_connected_line connected;

	connected = *((struct ast_party_connected_line *) src);
	connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_TRANSFER;

	/* Make sure empty strings will be erased. */
	if (!connected.id.name.str) {
		connected.id.name.str = "";
	}
	if (!connected.id.number.str) {
		connected.id.number.str = "";
	}
	if (!connected.id.subaddress.str) {
		connected.id.subaddress.str = "";
	}
	if (!connected.id.tag) {
		connected.id.tag = "";
	}

	ast_party_connected_line_copy(dest, &connected);
}

/*! Transfer masquerade connected line exchange data. */
struct xfer_masquerade_ds {
	/*! New ID for the target of the transfer (Masquerade original channel) */
	struct ast_party_connected_line target_id;
	/*! New ID for the transferee of the transfer (Masquerade clone channel) */
	struct ast_party_connected_line transferee_id;
	/*! TRUE if the target call is held. (Masquerade original channel) */
	int target_held;
	/*! TRUE if the transferee call is held. (Masquerade clone channel) */
	int transferee_held;
};

/*!
 * \internal
 * \brief Destroy the transfer connected line exchange datastore information.
 * \since 1.8
 *
 * \param data The datastore payload to destroy.
 *
 * \return Nothing
 */
static void xfer_ds_destroy(void *data)
{
	struct xfer_masquerade_ds *ds = data;

	ast_party_connected_line_free(&ds->target_id);
	ast_party_connected_line_free(&ds->transferee_id);
	ast_free(ds);
}

static const struct ast_datastore_info xfer_ds_info = {
	.type = "xfer_colp",
	.destroy = xfer_ds_destroy,
};

int ast_channel_transfer_masquerade(
	struct ast_channel *target_chan,
	const struct ast_party_connected_line *target_id,
	int target_held,
	struct ast_channel *transferee_chan,
	const struct ast_party_connected_line *transferee_id,
	int transferee_held)
{
	struct ast_datastore *xfer_ds;
	struct xfer_masquerade_ds *xfer_colp;
	int res;

	xfer_ds = ast_datastore_alloc(&xfer_ds_info, NULL);
	if (!xfer_ds) {
		return -1;
	}

	xfer_colp = ast_calloc(1, sizeof(*xfer_colp));
	if (!xfer_colp) {
		ast_datastore_free(xfer_ds);
		return -1;
	}
	party_connected_line_copy_transfer(&xfer_colp->target_id, target_id);
	xfer_colp->target_held = target_held;
	party_connected_line_copy_transfer(&xfer_colp->transferee_id, transferee_id);
	xfer_colp->transferee_held = transferee_held;
	xfer_ds->data = xfer_colp;

	res = __ast_channel_masquerade(target_chan, transferee_chan, xfer_ds);
	if (res) {
		ast_datastore_free(xfer_ds);
	}
	return res;
}

/*! \brief this function simply changes the name of the channel and issues a manager_event
 *         with out unlinking and linking the channel from the ao2_container.  This should
 *         only be used when the channel has already been unlinked from the ao2_container.
 */
static void __ast_change_name_nolink(struct ast_channel *chan, const char *newname)
{
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when the name of a channel is changed.</synopsis>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "Rename", "Channel: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", ast_channel_name(chan), newname, ast_channel_uniqueid(chan));
	ast_channel_name_set(chan, newname);
}

void ast_change_name(struct ast_channel *chan, const char *newname)
{
	/* We must re-link, as the hash value will change here. */
	ao2_lock(channels);
	ast_channel_lock(chan);
	ao2_unlink(channels, chan);
	__ast_change_name_nolink(chan, newname);
	ao2_link(channels, chan);
	ast_channel_unlock(chan);
	ao2_unlock(channels);
}

void ast_channel_inherit_variables(const struct ast_channel *parent, struct ast_channel *child)
{
	struct ast_var_t *current, *newvar;
	const char *varname;

	AST_LIST_TRAVERSE(ast_channel_varshead((struct ast_channel *) parent), current, entries) {
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
				AST_LIST_INSERT_TAIL(ast_channel_varshead(child), newvar, entries);
				ast_debug(1, "Inheriting variable %s from %s to %s.\n",
					ast_var_name(newvar), ast_channel_name(parent), ast_channel_name(child));
			}
			break;
		case 2:
			newvar = ast_var_assign(varname, ast_var_value(current));
			if (newvar) {
				AST_LIST_INSERT_TAIL(ast_channel_varshead(child), newvar, entries);
				ast_debug(1, "Inheriting variable %s from %s to %s.\n",
					ast_var_name(newvar), ast_channel_name(parent), ast_channel_name(child));
			}
			break;
		default:
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
	AST_LIST_APPEND_LIST(ast_channel_varshead(original), ast_channel_varshead(clonechan), entries);

	/* then, dup the varshead list into the clone */

	AST_LIST_TRAVERSE(ast_channel_varshead(original), current, entries) {
		newvar = ast_var_assign(current->name, current->value);
		if (newvar)
			AST_LIST_INSERT_TAIL(ast_channel_varshead(clonechan), newvar, entries);
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
	ast_assert(linkedid != NULL);
	/* if the linkedid for this channel is being changed from something, check... */
	if (ast_channel_linkedid(chan) && !strcmp(ast_channel_linkedid(chan), linkedid)) {
		return;
	}

	ast_cel_check_retire_linkedid(chan);
	ast_channel_linkedid_set(chan, linkedid);
	ast_cel_linkedid_ref(linkedid);
}

/*!
  \brief Propagate the oldest linkedid between associated channels

*/
void ast_channel_set_linkgroup(struct ast_channel *chan, struct ast_channel *peer)
{
	const char* linkedid=NULL;
	struct ast_channel *bridged;

	linkedid = oldest_linkedid(ast_channel_linkedid(chan), ast_channel_linkedid(peer));
	linkedid = oldest_linkedid(linkedid, ast_channel_uniqueid(chan));
	linkedid = oldest_linkedid(linkedid, ast_channel_uniqueid(peer));
	if (ast_channel_internal_bridged_channel(chan)) {
		bridged = ast_bridged_channel(chan);
		if (bridged && bridged != peer) {
			linkedid = oldest_linkedid(linkedid, ast_channel_linkedid(bridged));
			linkedid = oldest_linkedid(linkedid, ast_channel_uniqueid(bridged));
		}
	}
	if (ast_channel_internal_bridged_channel(peer)) {
		bridged = ast_bridged_channel(peer);
		if (bridged && bridged != chan) {
			linkedid = oldest_linkedid(linkedid, ast_channel_linkedid(bridged));
			linkedid = oldest_linkedid(linkedid, ast_channel_uniqueid(bridged));
		}
	}

	/* just in case setting a stringfield to itself causes problems */
	linkedid = ast_strdupa(linkedid);

	ast_channel_change_linkedid(chan, linkedid);
	ast_channel_change_linkedid(peer, linkedid);
	if (ast_channel_internal_bridged_channel(chan)) {
		bridged = ast_bridged_channel(chan);
		if (bridged && bridged != peer) {
			ast_channel_change_linkedid(bridged, linkedid);
		}
	}
	if (ast_channel_internal_bridged_channel(peer)) {
		bridged = ast_bridged_channel(peer);
		if (bridged && bridged != chan) {
			ast_channel_change_linkedid(bridged, linkedid);
		}
	}
}

/* copy accountcode and peeraccount across during a link */
static void ast_set_owners_and_peers(struct ast_channel *chan1,
									 struct ast_channel *chan2)
{
	if (!ast_strlen_zero(ast_channel_accountcode(chan1)) && ast_strlen_zero(ast_channel_peeraccount(chan2))) {
		ast_debug(1, "setting peeraccount to %s for %s from data on channel %s\n",
				ast_channel_accountcode(chan1), ast_channel_name(chan2), ast_channel_name(chan1));
		ast_channel_peeraccount_set(chan2, ast_channel_accountcode(chan1));
	}
	if (!ast_strlen_zero(ast_channel_accountcode(chan2)) && ast_strlen_zero(ast_channel_peeraccount(chan1))) {
		ast_debug(1, "setting peeraccount to %s for %s from data on channel %s\n",
				ast_channel_accountcode(chan2), ast_channel_name(chan1), ast_channel_name(chan2));
		ast_channel_peeraccount_set(chan1, ast_channel_accountcode(chan2));
	}
	if (!ast_strlen_zero(ast_channel_peeraccount(chan1)) && ast_strlen_zero(ast_channel_accountcode(chan2))) {
		ast_debug(1, "setting accountcode to %s for %s from data on channel %s\n",
				ast_channel_peeraccount(chan1), ast_channel_name(chan2), ast_channel_name(chan1));
		ast_channel_accountcode_set(chan2, ast_channel_peeraccount(chan1));
	}
	if (!ast_strlen_zero(ast_channel_peeraccount(chan2)) && ast_strlen_zero(ast_channel_accountcode(chan1))) {
		ast_debug(1, "setting accountcode to %s for %s from data on channel %s\n",
				ast_channel_peeraccount(chan2), ast_channel_name(chan1), ast_channel_name(chan2));
		ast_channel_accountcode_set(chan1, ast_channel_peeraccount(chan2));
	}
	if (0 != strcmp(ast_channel_accountcode(chan1), ast_channel_peeraccount(chan2))) {
		ast_debug(1, "changing peeraccount from %s to %s on %s to match channel %s\n",
				ast_channel_peeraccount(chan2), ast_channel_peeraccount(chan1), ast_channel_name(chan2), ast_channel_name(chan1));
		ast_channel_peeraccount_set(chan2, ast_channel_accountcode(chan1));
	}
	if (0 != strcmp(ast_channel_accountcode(chan2), ast_channel_peeraccount(chan1))) {
		ast_debug(1, "changing peeraccount from %s to %s on %s to match channel %s\n",
				ast_channel_peeraccount(chan1), ast_channel_peeraccount(chan2), ast_channel_name(chan1), ast_channel_name(chan2));
		ast_channel_peeraccount_set(chan1, ast_channel_accountcode(chan2));
	}
}

/*!
 * \pre chan is locked
 */
static void report_new_callerid(struct ast_channel *chan)
{
	int pres;

	pres = ast_party_id_presentation(&ast_channel_caller(chan)->id);
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a channel receives new Caller ID information.</synopsis>
			<syntax>
				<parameter name="CID-CallingPres">
					<para>A description of the Caller ID presentation.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "NewCallerid",
		"Channel: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"Uniqueid: %s\r\n"
		"CID-CallingPres: %d (%s)\r\n",
		ast_channel_name(chan),
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, ""),
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, ""),
		ast_channel_uniqueid(chan),
		pres,
		ast_describe_caller_presentation(pres)
		);
}

/*!
 * \internal
 * \brief Transfer COLP between target and transferee channels.
 * \since 1.8
 *
 * \param transferee Transferee channel to exchange connected line information.
 * \param colp Connected line information to exchange.
 *
 * \return Nothing
 */
static void masquerade_colp_transfer(struct ast_channel *transferee, struct xfer_masquerade_ds *colp)
{
	struct ast_control_read_action_payload *frame_payload;
	int payload_size;
	int frame_size;
	unsigned char connected_line_data[1024];

	/* Release any hold on the target. */
	if (colp->target_held) {
		ast_queue_control(transferee, AST_CONTROL_UNHOLD);
	}

	/*
	 * Since transferee may not actually be bridged to another channel,
	 * there is no way for us to queue a frame so that its connected
	 * line status will be updated.  Instead, we use the somewhat
	 * hackish approach of using a special control frame type that
	 * instructs ast_read() to perform a specific action.  In this
	 * case, the frame we queue tells ast_read() to call the
	 * connected line interception macro configured for transferee.
	 */

	/* Reset any earlier private connected id representation */
	ast_party_id_reset(&colp->target_id.priv);
	ast_party_id_reset(&colp->transferee_id.priv);

	payload_size = ast_connected_line_build_data(connected_line_data,
		sizeof(connected_line_data), &colp->target_id, NULL);
	if (payload_size != -1) {
		frame_size = payload_size + sizeof(*frame_payload);
		frame_payload = ast_alloca(frame_size);
		frame_payload->action = AST_FRAME_READ_ACTION_CONNECTED_LINE_MACRO;
		frame_payload->payload_size = payload_size;
		memcpy(frame_payload->payload, connected_line_data, payload_size);
		ast_queue_control_data(transferee, AST_CONTROL_READ_ACTION, frame_payload,
			frame_size);
	}
	/*
	 * In addition to queueing the read action frame so that the
	 * connected line info on transferee will be updated, we also are
	 * going to queue a plain old connected line update on transferee to
	 * update the target.
	 */
	ast_channel_queue_connected_line_update(transferee, &colp->transferee_id, NULL);
}

/*!
 * \brief Masquerade a channel
 *
 * \note Assumes _NO_ channels and _NO_ channel pvt's are locked.  If a channel is locked while calling
 *       this function, it invalidates our channel container locking order.  All channels
 *       must be unlocked before it is permissible to lock the channels' ao2 container.
 */
int ast_do_masquerade(struct ast_channel *original)
{
	int x;
	int origstate;
	unsigned int orig_disablestatecache;
	unsigned int clone_disablestatecache;
	int visible_indication;
	int moh_is_playing;
	int clone_was_zombie = 0;/*!< TRUE if the clonechan was a zombie before the masquerade. */
	struct ast_frame *current;
	const struct ast_channel_tech *t;
	void *t_pvt;
	union {
		struct ast_hangup_handler_list handlers;
		struct ast_party_dialed dialed;
		struct ast_party_caller caller;
		struct ast_party_connected_line connected;
		struct ast_party_redirecting redirecting;
	} exchange;
	struct ast_channel *clonechan, *chans[2];
	struct ast_channel *bridged;
	struct ast_cdr *cdr;
	struct ast_datastore *xfer_ds;
	struct xfer_masquerade_ds *xfer_colp;
	struct ast_format rformat;
	struct ast_format wformat;
	struct ast_format tmp_format;
	char newn[AST_CHANNEL_NAME];
	char orig[AST_CHANNEL_NAME];
	char masqn[AST_CHANNEL_NAME];
	char zombn[AST_CHANNEL_NAME];
	char clone_sending_dtmf_digit;
	struct timeval clone_sending_dtmf_tv;

	/* XXX This operation is a bit odd.  We're essentially putting the guts of
	 * the clone channel into the original channel.  Start by killing off the
	 * original channel's backend.  While the features are nice, which is the
	 * reason we're keeping it, it's still awesomely weird. XXX */

	/*
	 * The reasoning for the channels ao2_container lock here is
	 * complex.
	 *
	 * There is a race condition that exists for this function.
	 * Since all pvt and channel locks must be let go before calling
	 * ast_do_masquerade, it is possible that it could be called
	 * multiple times for the same channel.  In order to prevent the
	 * race condition with competing threads to do the masquerade
	 * and new masquerade attempts, the channels container must be
	 * locked for the entire masquerade.  The original and clonechan
	 * need to be unlocked earlier to avoid potential deadlocks with
	 * the chan_local deadlock avoidance method.
	 *
	 * The container lock blocks competing masquerade attempts from
	 * starting as well as being necessary for proper locking order
	 * because the channels must to be unlinked to change their
	 * names.
	 *
	 * The original and clonechan locks must be held while the
	 * channel contents are shuffled around for the masquerade.
	 *
	 * The masq and masqr pointers need to be left alone until the
	 * masquerade has restabilized the channels to prevent another
	 * masquerade request until the AST_FLAG_ZOMBIE can be set on
	 * the clonechan.
	 */
	ao2_lock(channels);

	/*
	 * Lock the original channel to determine if the masquerade is
	 * still required.
	 */
	ast_channel_lock(original);

	clonechan = ast_channel_masq(original);
	if (!clonechan) {
		/*
		 * The masq is already completed by another thread or never
		 * needed to be done to begin with.
		 */
		ast_channel_unlock(original);
		ao2_unlock(channels);
		return 0;
	}

	/* Bump the refs to ensure that they won't dissapear on us. */
	ast_channel_ref(original);
	ast_channel_ref(clonechan);

	/* unlink from channels container as name (which is the hash value) will change */
	ao2_unlink(channels, original);
	ao2_unlink(channels, clonechan);

	/* Get any transfer masquerade connected line exchange data. */
	xfer_ds = ast_channel_datastore_find(original, &xfer_ds_info, NULL);
	if (xfer_ds) {
		ast_channel_datastore_remove(original, xfer_ds);
		xfer_colp = xfer_ds->data;
	} else {
		xfer_colp = NULL;
	}

	moh_is_playing = ast_test_flag(ast_channel_flags(original), AST_FLAG_MOH);

	/*
	 * Stop any visible indication on the original channel so we can
	 * transfer it to the clonechan taking the original's place.
	 */
	visible_indication = ast_channel_visible_indication(original);
	ast_channel_unlock(original);
	ast_indicate(original, -1);

	/*
	 * Release any hold on the transferee channel before going any
	 * further with the masquerade.
	 */
	if (xfer_colp && xfer_colp->transferee_held) {
		ast_indicate(clonechan, AST_CONTROL_UNHOLD);
	}

	/* Start the masquerade channel contents rearangement. */
	ast_channel_lock_both(original, clonechan);

	ast_debug(4, "Actually Masquerading %s(%u) into the structure of %s(%u)\n",
		ast_channel_name(clonechan), ast_channel_state(clonechan), ast_channel_name(original), ast_channel_state(original));

	chans[0] = clonechan;
	chans[1] = original;
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a masquerade occurs between two channels, wherein the Clone channel's internal information replaces the Original channel's information.</synopsis>
			<syntax>
				<parameter name="Clone">
					<para>The name of the channel whose information will be going into the Original channel.</para>
				</parameter>
				<parameter name="CloneState">
					<para>The current state of the clone channel.</para>
				</parameter>
				<parameter name="Original">
					<para>The name of the channel whose information will be replaced by the Clone channel's information.</para>
				</parameter>
				<parameter name="OriginalState">
					<para>The current state of the original channel.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	ast_manager_event_multichan(EVENT_FLAG_CALL, "Masquerade", 2, chans,
		"Clone: %s\r\n"
		"CloneState: %s\r\n"
		"Original: %s\r\n"
		"OriginalState: %s\r\n",
		ast_channel_name(clonechan), ast_state2str(ast_channel_state(clonechan)), ast_channel_name(original), ast_state2str(ast_channel_state(original)));

	/*
	 * Remember the original read/write formats.  We turn off any
	 * translation on either one
	 */
	ast_format_copy(&rformat, ast_channel_readformat(original));
	ast_format_copy(&wformat, ast_channel_writeformat(original));
	free_translation(clonechan);
	free_translation(original);

	/* Save the current DTMF digit being sent if any. */
	clone_sending_dtmf_digit = ast_channel_sending_dtmf_digit(clonechan);
	clone_sending_dtmf_tv = ast_channel_sending_dtmf_tv(clonechan);

	/* Save the original name */
	ast_copy_string(orig, ast_channel_name(original), sizeof(orig));
	/* Save the new name */
	ast_copy_string(newn, ast_channel_name(clonechan), sizeof(newn));
	/* Create the masq name */
	snprintf(masqn, sizeof(masqn), "%s<MASQ>", newn);

	/* Mangle the name of the clone channel */
	__ast_change_name_nolink(clonechan, masqn);

	/* Copy the name from the clone channel */
	__ast_change_name_nolink(original, newn);

	/* share linked id's */
	ast_channel_set_linkgroup(original, clonechan);

	/* Swap the technologies */
	t = ast_channel_tech(original);
	ast_channel_tech_set(original, ast_channel_tech(clonechan));
	ast_channel_tech_set(clonechan, t);

	t_pvt = ast_channel_tech_pvt(original);
	ast_channel_tech_pvt_set(original, ast_channel_tech_pvt(clonechan));
	ast_channel_tech_pvt_set(clonechan, t_pvt);

	/* Swap the cdrs */
	cdr = ast_channel_cdr(original);
	ast_channel_cdr_set(original, ast_channel_cdr(clonechan));
	ast_channel_cdr_set(clonechan, cdr);

	/* Swap the alertpipes */
	ast_channel_internal_alertpipe_swap(original, clonechan);

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

		AST_LIST_HEAD_INIT_NOLOCK(&tmp_readq);
		AST_LIST_APPEND_LIST(&tmp_readq, ast_channel_readq(original), frame_list);
		AST_LIST_APPEND_LIST(ast_channel_readq(original), ast_channel_readq(clonechan), frame_list);

		while ((current = AST_LIST_REMOVE_HEAD(&tmp_readq, frame_list))) {
			AST_LIST_INSERT_TAIL(ast_channel_readq(original), current, frame_list);
			if (ast_channel_alert_write(original)) {
				ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
			}
		}
	}

	/* Swap the raw formats */
	ast_format_copy(&tmp_format, ast_channel_rawreadformat(original));
	ast_format_copy(ast_channel_rawreadformat(original), ast_channel_rawreadformat(clonechan));
	ast_format_copy(ast_channel_rawreadformat(clonechan), &tmp_format);

	ast_format_copy(&tmp_format, ast_channel_rawwriteformat(original));
	ast_format_copy(ast_channel_rawwriteformat(original), ast_channel_rawwriteformat(clonechan));
	ast_format_copy(ast_channel_rawwriteformat(clonechan), &tmp_format);

	ast_channel_softhangup_internal_flag_set(clonechan, AST_SOFTHANGUP_DEV);

	/* And of course, so does our current state.  Note we need not
	   call ast_setstate since the event manager doesn't really consider
	   these separate.  We do this early so that the clone has the proper
	   state of the original channel. */
	origstate = ast_channel_state(original);
	ast_channel_state_set(original, ast_channel_state(clonechan));
	ast_channel_state_set(clonechan, origstate);

	/* And the swap the cachable state too. Otherwise we'd start caching
	 * Local channels and ignoring real ones. */
	orig_disablestatecache = ast_test_flag(ast_channel_flags(original), AST_FLAG_DISABLE_DEVSTATE_CACHE);
	clone_disablestatecache = ast_test_flag(ast_channel_flags(clonechan), AST_FLAG_DISABLE_DEVSTATE_CACHE);
	if (orig_disablestatecache != clone_disablestatecache) {
		if (orig_disablestatecache) {
			ast_clear_flag(ast_channel_flags(original), AST_FLAG_DISABLE_DEVSTATE_CACHE);
			ast_set_flag(ast_channel_flags(clonechan), AST_FLAG_DISABLE_DEVSTATE_CACHE);
		} else {
			ast_set_flag(ast_channel_flags(original), AST_FLAG_DISABLE_DEVSTATE_CACHE);
			ast_clear_flag(ast_channel_flags(clonechan), AST_FLAG_DISABLE_DEVSTATE_CACHE);
		}
	}

	/* Mangle the name of the clone channel */
	snprintf(zombn, sizeof(zombn), "%s<ZOMBIE>", orig); /* quick, hide the brains! */
	__ast_change_name_nolink(clonechan, zombn);

	/* Update the type. */
	t_pvt = ast_channel_monitor(original);
	ast_channel_monitor_set(original, ast_channel_monitor(clonechan));
	ast_channel_monitor_set(clonechan, t_pvt);

	/* Keep the same language.  */
	ast_channel_language_set(original, ast_channel_language(clonechan));

	/* Keep the same parkinglot. */
	ast_channel_parkinglot_set(original, ast_channel_parkinglot(clonechan));

	/* Copy the FD's other than the generator fd */
	for (x = 0; x < AST_MAX_FDS; x++) {
		if (x != AST_GENERATOR_FD)
			ast_channel_set_fd(original, x, ast_channel_fd(clonechan, x));
	}

	ast_app_group_update(clonechan, original);

	/* Swap hangup handlers. */
	exchange.handlers = *ast_channel_hangup_handlers(original);
	*ast_channel_hangup_handlers(original) = *ast_channel_hangup_handlers(clonechan);
	*ast_channel_hangup_handlers(clonechan) = exchange.handlers;

	/* Move data stores over */
	if (AST_LIST_FIRST(ast_channel_datastores(clonechan))) {
		struct ast_datastore *ds;
		/* We use a safe traversal here because some fixup routines actually
		 * remove the datastore from the list and free them.
		 */
		AST_LIST_TRAVERSE_SAFE_BEGIN(ast_channel_datastores(clonechan), ds, entry) {
			if (ds->info->chan_fixup)
				ds->info->chan_fixup(ds->data, clonechan, original);
		}
		AST_LIST_TRAVERSE_SAFE_END;
		AST_LIST_APPEND_LIST(ast_channel_datastores(original), ast_channel_datastores(clonechan), entry);
	}

	ast_autochan_new_channel(clonechan, original);

	clone_variables(original, clonechan);
	/* Presense of ADSI capable CPE follows clone */
	ast_channel_adsicpe_set(original, ast_channel_adsicpe(clonechan));
	/* Bridge remains the same */
	/* CDR fields remain the same */
	/* XXX What about blocking, softhangup, blocker, and lock and blockproc? XXX */
	/* Application and data remain the same */
	/* Clone exception  becomes real one, as with fdno */
	ast_set_flag(ast_channel_flags(original), ast_test_flag(ast_channel_flags(clonechan), AST_FLAG_EXCEPTION | AST_FLAG_OUTGOING));
	ast_channel_fdno_set(original, ast_channel_fdno(clonechan));
	/* Schedule context remains the same */
	/* Stream stuff stays the same */
	/* Keep the original state.  The fixup code will need to work with it most likely */

	/*
	 * Just swap the whole structures, nevermind the allocations,
	 * they'll work themselves out.
	 */
	exchange.dialed = *ast_channel_dialed(original);
	ast_channel_dialed_set(original, ast_channel_dialed(clonechan));
	ast_channel_dialed_set(clonechan, &exchange.dialed);

	/* Reset any earlier private caller id representations */
	ast_party_id_reset(&ast_channel_caller(original)->priv);
	ast_party_id_reset(&ast_channel_caller(clonechan)->priv);

	exchange.caller = *ast_channel_caller(original);
	ast_channel_caller_set(original, ast_channel_caller(clonechan));
	ast_channel_caller_set(clonechan, &exchange.caller);

	/* Reset any earlier private connected id representations */
	ast_party_id_reset(&ast_channel_connected(original)->priv);
	ast_party_id_reset(&ast_channel_connected(clonechan)->priv);

	exchange.connected = *ast_channel_connected(original);
	ast_channel_connected_set(original, ast_channel_connected(clonechan));
	ast_channel_connected_set(clonechan, &exchange.connected);

	/* Reset any earlier private redirecting orig, from or to representations */
	ast_party_id_reset(&ast_channel_redirecting(original)->priv_orig);
	ast_party_id_reset(&ast_channel_redirecting(clonechan)->priv_orig);
	ast_party_id_reset(&ast_channel_redirecting(original)->priv_from);
	ast_party_id_reset(&ast_channel_redirecting(clonechan)->priv_from);
	ast_party_id_reset(&ast_channel_redirecting(original)->priv_to);
	ast_party_id_reset(&ast_channel_redirecting(clonechan)->priv_to);

	exchange.redirecting = *ast_channel_redirecting(original);
	ast_channel_redirecting_set(original, ast_channel_redirecting(clonechan));
	ast_channel_redirecting_set(clonechan, &exchange.redirecting);

	report_new_callerid(original);

	/* Restore original timing file descriptor */
	ast_channel_set_fd(original, AST_TIMING_FD, ast_channel_timingfd(original));

	/* Our native formats are different now */
	ast_format_cap_copy(ast_channel_nativeformats(original), ast_channel_nativeformats(clonechan));

	/* Context, extension, priority, app data, jump table,  remain the same */
	/* pvt switches.  pbx stays the same, as does next */

	/* Set the write format */
	ast_set_write_format(original, &wformat);

	/* Set the read format */
	ast_set_read_format(original, &rformat);

	/* Copy the music class */
	ast_channel_musicclass_set(original, ast_channel_musicclass(clonechan));

	/* copy over accuntcode and set peeraccount across the bridge */
	ast_channel_accountcode_set(original, S_OR(ast_channel_accountcode(clonechan), ""));
	if (ast_channel_internal_bridged_channel(original)) {
		/* XXX - should we try to lock original's bridged channel here? */
		ast_channel_peeraccount_set(ast_channel_internal_bridged_channel(original), S_OR(ast_channel_accountcode(clonechan), ""));
		ast_cel_report_event(original, AST_CEL_BRIDGE_UPDATE, NULL, NULL, NULL);
	}

	ast_debug(1, "Putting channel %s in %s/%s formats\n", ast_channel_name(original),
		ast_getformatname(&wformat), ast_getformatname(&rformat));

	/* Fixup the original clonechan's physical side */
	if (ast_channel_tech(original)->fixup && ast_channel_tech(original)->fixup(clonechan, original)) {
		ast_log(LOG_WARNING, "Channel type '%s' could not fixup channel %s, strange things may happen. (clonechan)\n",
			ast_channel_tech(original)->type, ast_channel_name(original));
	}

	/* Fixup the original original's physical side */
	if (ast_channel_tech(clonechan)->fixup && ast_channel_tech(clonechan)->fixup(original, clonechan)) {
		ast_log(LOG_WARNING, "Channel type '%s' could not fixup channel %s, strange things may happen. (original)\n",
			ast_channel_tech(clonechan)->type, ast_channel_name(clonechan));
	}

	/*
	 * Now, at this point, the "clone" channel is totally F'd up.
	 * We mark it as a zombie so nothing tries to touch it.  If it's
	 * already been marked as a zombie, then we must free it (since
	 * it already is considered invalid).
	 *
	 * This must be done before we unlock clonechan to prevent
	 * setting up another masquerade on the clonechan.
	 */
	if (ast_test_flag(ast_channel_flags(clonechan), AST_FLAG_ZOMBIE)) {
		clone_was_zombie = 1;
	} else {
		ast_set_flag(ast_channel_flags(clonechan), AST_FLAG_ZOMBIE);
		ast_queue_frame(clonechan, &ast_null_frame);
	}

	/* clear the masquerade channels */
	ast_channel_masq_set(original, NULL);
	ast_channel_masqr_set(clonechan, NULL);

	/*
	 * When we unlock original here, it can be immediately setup to
	 * masquerade again or hungup.  The new masquerade or hangup
	 * will not actually happen until we release the channels
	 * container lock.
	 */
	ast_channel_unlock(original);
	ast_channel_unlock(clonechan);

	if (clone_sending_dtmf_digit) {
		/*
		 * The clonechan was sending a DTMF digit that was not completed
		 * before the masquerade.
		 */
		ast_bridge_end_dtmf(original, clone_sending_dtmf_digit, clone_sending_dtmf_tv,
			"masquerade");
	}

	/*
	 * If an indication is currently playing, maintain it on the
	 * channel that is taking the place of original.
	 *
	 * This is needed because the masquerade is swapping out the
	 * internals of the channel, and the new channel private data
	 * needs to be made aware of the current visible indication
	 * (RINGING, CONGESTION, etc.)
	 */
	if (visible_indication) {
		ast_indicate(original, visible_indication);
	}

	/* if moh is playing on the original channel then it needs to be
	   maintained on the channel that is replacing it. */
	if (moh_is_playing) {
		ast_moh_start(original, NULL, NULL);
	}

	ast_channel_lock(original);

	/* Signal any blocker */
	if (ast_test_flag(ast_channel_flags(original), AST_FLAG_BLOCKING)) {
		pthread_kill(ast_channel_blocker(original), SIGURG);
	}

	ast_debug(1, "Done Masquerading %s (%u)\n", ast_channel_name(original), ast_channel_state(original));

	if ((bridged = ast_bridged_channel(original))) {
		ast_channel_ref(bridged);
		ast_channel_unlock(original);
		ast_indicate(bridged, AST_CONTROL_SRCCHANGE);
		ast_channel_unref(bridged);
	} else {
		ast_channel_unlock(original);
	}
	ast_indicate(original, AST_CONTROL_SRCCHANGE);

	if (xfer_colp) {
		/*
		 * After the masquerade, the original channel pointer actually
		 * points to the new transferee channel and the bridged channel
		 * is still the intended transfer target party.
		 */
		masquerade_colp_transfer(original, xfer_colp);
	}

	if (xfer_ds) {
		ast_datastore_free(xfer_ds);
	}

	if (!clone_was_zombie) {
		ao2_link(channels, clonechan);
	}
	ao2_link(channels, original);
	ao2_unlock(channels);

	if (clone_was_zombie) {
		/* Restart the ast_hangup() that was deferred because of this masquerade. */
		ast_debug(1, "Destroying channel clone '%s'\n", ast_channel_name(clonechan));
		ast_hangup(clonechan);
	}

	/* Release our held safety references. */
	ast_channel_unref(original);
	ast_channel_unref(clonechan);

	return 0;
}

void ast_set_callerid(struct ast_channel *chan, const char *cid_num, const char *cid_name, const char *cid_ani)
{
	ast_channel_lock(chan);

	if (cid_num) {
		ast_channel_caller(chan)->id.number.valid = 1;
		ast_free(ast_channel_caller(chan)->id.number.str);
		ast_channel_caller(chan)->id.number.str = ast_strdup(cid_num);
	}
	if (cid_name) {
		ast_channel_caller(chan)->id.name.valid = 1;
		ast_free(ast_channel_caller(chan)->id.name.str);
		ast_channel_caller(chan)->id.name.str = ast_strdup(cid_name);
	}
	if (cid_ani) {
		ast_channel_caller(chan)->ani.number.valid = 1;
		ast_free(ast_channel_caller(chan)->ani.number.str);
		ast_channel_caller(chan)->ani.number.str = ast_strdup(cid_ani);
	}
	if (ast_channel_cdr(chan)) {
		ast_cdr_setcid(ast_channel_cdr(chan), chan);
	}

	report_new_callerid(chan);

	ast_channel_unlock(chan);
}

void ast_channel_set_caller(struct ast_channel *chan, const struct ast_party_caller *caller, const struct ast_set_party_caller *update)
{
	if (ast_channel_caller(chan) == caller) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	ast_party_caller_set(ast_channel_caller(chan), caller, update);
	ast_channel_unlock(chan);
}

void ast_channel_set_caller_event(struct ast_channel *chan, const struct ast_party_caller *caller, const struct ast_set_party_caller *update)
{
	const char *pre_set_number;
	const char *pre_set_name;

	if (ast_channel_caller(chan) == caller) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	pre_set_number =
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL);
	pre_set_name = S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL);
	ast_party_caller_set(ast_channel_caller(chan), caller, update);
	if (S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL)
			!= pre_set_number
		|| S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL)
			!= pre_set_name) {
		/* The caller id name or number changed. */
		report_new_callerid(chan);
	}
	if (ast_channel_cdr(chan)) {
		ast_cdr_setcid(ast_channel_cdr(chan), chan);
	}
	ast_channel_unlock(chan);
}

int ast_setstate(struct ast_channel *chan, enum ast_channel_state state)
{
	int oldstate = ast_channel_state(chan);
	char name[AST_CHANNEL_NAME], *dashptr;

	if (oldstate == state)
		return 0;

	ast_copy_string(name, ast_channel_name(chan), sizeof(name));
	if ((dashptr = strrchr(name, '-'))) {
		*dashptr = '\0';
	}

	ast_channel_state_set(chan, state);

	/* We have to pass AST_DEVICE_UNKNOWN here because it is entirely possible that the channel driver
	 * for this channel is using the callback method for device state. If we pass in an actual state here
	 * we override what they are saying the state is and things go amuck. */
	ast_devstate_changed_literal(AST_DEVICE_UNKNOWN, (ast_test_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_DEVSTATE_CACHE) ? AST_DEVSTATE_NOT_CACHABLE : AST_DEVSTATE_CACHABLE), name);

	/* setstate used to conditionally report Newchannel; this is no more */
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a channel's state changes.</synopsis>
			<syntax>
				<parameter name="ChannelState">
					<para>A numeric code for the channel's current state, related to ChannelStateDesc</para>
				</parameter>
				<parameter name="ChannelStateDesc">
					<enumlist>
						<enum name="Down"/>
						<enum name="Rsrvd"/>
						<enum name="OffHook"/>
						<enum name="Dialing"/>
						<enum name="Ring"/>
						<enum name="Ringing"/>
						<enum name="Up"/>
						<enum name="Busy"/>
						<enum name="Dialing Offhook"/>
						<enum name="Pre-ring"/>
						<enum name="Unknown"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	ast_manager_event(chan, EVENT_FLAG_CALL, "Newstate",
		"Channel: %s\r\n"
		"ChannelState: %u\r\n"
		"ChannelStateDesc: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"ConnectedLineNum: %s\r\n"
		"ConnectedLineName: %s\r\n"
		"Uniqueid: %s\r\n",
		ast_channel_name(chan), ast_channel_state(chan), ast_state2str(ast_channel_state(chan)),
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, ""),
		S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, ""),
		S_COR(ast_channel_connected(chan)->id.number.valid, ast_channel_connected(chan)->id.number.str, ""),
		S_COR(ast_channel_connected(chan)->id.name.valid, ast_channel_connected(chan)->id.name.str, ""),
		ast_channel_uniqueid(chan));

	return 0;
}

/*! \brief Find bridged channel */
struct ast_channel *ast_bridged_channel(struct ast_channel *chan)
{
	struct ast_channel *bridged;
	bridged = ast_channel_internal_bridged_channel(chan);
	if (bridged && ast_channel_tech(bridged)->bridged_channel)
		bridged = ast_channel_tech(bridged)->bridged_channel(chan, bridged);
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
			ast_say_number(chan, min, AST_DIGIT_ANY, ast_channel_language(chan), NULL);
			ast_stream_and_wait(chan, "queue-minutes", "");
		}
		if (sec) {
			ast_say_number(chan, sec, AST_DIGIT_ANY, ast_channel_language(chan), NULL);
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
	struct ast_format_cap *o0nativeformats;
	struct ast_format_cap *o1nativeformats;
	int watch_c0_dtmf;
	int watch_c1_dtmf;
	void *pvt0, *pvt1;
	/* Indicates whether a frame was queued into a jitterbuffer */
	int frame_put_in_jb = 0;
	int jb_in_use;
	int to;

	o0nativeformats = ast_format_cap_dup(ast_channel_nativeformats(c0));
	o1nativeformats = ast_format_cap_dup(ast_channel_nativeformats(c1));

	if (!o0nativeformats || !o1nativeformats) {
		ast_format_cap_destroy(o0nativeformats); /* NULL safe */
		ast_format_cap_destroy(o1nativeformats); /* NULL safe */
		return AST_BRIDGE_FAILED;
	}

	cs[0] = c0;
	cs[1] = c1;
	pvt0 = ast_channel_tech_pvt(c0);
	pvt1 = ast_channel_tech_pvt(c1);
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

		if ((ast_channel_tech_pvt(c0) != pvt0) || (ast_channel_tech_pvt(c1) != pvt1) ||
		    (!ast_format_cap_identical(o0nativeformats, ast_channel_nativeformats(c0))) ||
		    (!ast_format_cap_identical(o1nativeformats, ast_channel_nativeformats(c1)))) {
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
			if ((ast_channel_softhangup_internal_flag(c0) | ast_channel_softhangup_internal_flag(c1)) & AST_SOFTHANGUP_UNBRIDGE) {/* Bit operators are intentional. */
				if (ast_channel_softhangup_internal_flag(c0) & AST_SOFTHANGUP_UNBRIDGE) {
					ast_channel_clear_softhangup(c0, AST_SOFTHANGUP_UNBRIDGE);
				}
				if (ast_channel_softhangup_internal_flag(c1) & AST_SOFTHANGUP_UNBRIDGE) {
					ast_channel_clear_softhangup(c1, AST_SOFTHANGUP_UNBRIDGE);
				}
				ast_channel_lock_both(c0, c1);
				ast_channel_internal_bridged_channel_set(c0, c1);
				ast_channel_internal_bridged_channel_set(c1, c0);
				ast_channel_unlock(c0);
				ast_channel_unlock(c1);
			}
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			ast_debug(1, "Didn't get a frame from channel: %s\n", ast_channel_name(who));
			break;
		}

		other = (who == c0) ? c1 : c0; /* the 'other' channel */
		/* Try add the frame info the who's bridged channel jitterbuff */
		if (jb_in_use)
			frame_put_in_jb = !ast_jb_put(other, f);

		if ((f->frametype == AST_FRAME_CONTROL) && !(config->flags & AST_BRIDGE_IGNORE_SIGS)) {
			int bridge_exit = 0;

			switch (f->subclass.integer) {
			case AST_CONTROL_PVT_CAUSE_CODE:
			case AST_CONTROL_AOC:
			case AST_CONTROL_MCID:
				ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				break;
			case AST_CONTROL_REDIRECTING:
				if (ast_channel_redirecting_sub(who, other, f, 1) &&
					ast_channel_redirecting_macro(who, other, f, other == c0, 1)) {
					ast_indicate_data(other, f->subclass.integer, f->data.ptr, f->datalen);
				}
				break;
			case AST_CONTROL_CONNECTED_LINE:
				if (ast_channel_connected_line_sub(who, other, f, 1) &&
					ast_channel_connected_line_macro(who, other, f, other == c0, 1)) {
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
				ast_debug(1, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass.integer, ast_channel_name(who));
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
					ast_channel_name(who));

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

	ast_format_cap_destroy(o0nativeformats);
	ast_format_cap_destroy(o1nativeformats);

	return res;
}

/*! \brief Bridge two channels together (early) */
int ast_channel_early_bridge(struct ast_channel *c0, struct ast_channel *c1)
{
	/* Make sure we can early bridge, if not error out */
	if (!ast_channel_tech(c0)->early_bridge || (c1 && (!ast_channel_tech(c1)->early_bridge || ast_channel_tech(c0)->early_bridge != ast_channel_tech(c1)->early_bridge)))
		return -1;

	return ast_channel_tech(c0)->early_bridge(c0, c1);
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
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a bridge changes between two channels.</synopsis>
			<syntax>
				<parameter name="Bridgestate">
					<enumlist>
						<enum name="Link"/>
						<enum name="Unlink"/>
					</enumlist>
				</parameter>
				<parameter name="Bridgetype">
					<enumlist>
						<enum name="core"/>
						<enum name="native"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
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
		ast_channel_name(c0), ast_channel_name(c1),
		ast_channel_uniqueid(c0), ast_channel_uniqueid(c1),
		S_COR(ast_channel_caller(c0)->id.number.valid, ast_channel_caller(c0)->id.number.str, ""),
		S_COR(ast_channel_caller(c1)->id.number.valid, ast_channel_caller(c1)->id.number.str, ""));
}

static void update_bridge_vars(struct ast_channel *c0, struct ast_channel *c1)
{
	const char *c0_name;
	const char *c1_name;
	const char *c0_pvtid = NULL;
	const char *c1_pvtid = NULL;

	ast_channel_lock(c1);
	c1_name = ast_strdupa(ast_channel_name(c1));
	if (ast_channel_tech(c1)->get_pvt_uniqueid) {
		c1_pvtid = ast_strdupa(ast_channel_tech(c1)->get_pvt_uniqueid(c1));
	}
	ast_channel_unlock(c1);

	ast_channel_lock(c0);
	if (!ast_strlen_zero(pbx_builtin_getvar_helper(c0, "BRIDGEPEER"))) {
		pbx_builtin_setvar_helper(c0, "BRIDGEPEER", c1_name);
	}
	if (c1_pvtid) {
		pbx_builtin_setvar_helper(c0, "BRIDGEPVTCALLID", c1_pvtid);
	}
	c0_name = ast_strdupa(ast_channel_name(c0));
	if (ast_channel_tech(c0)->get_pvt_uniqueid) {
		c0_pvtid = ast_strdupa(ast_channel_tech(c0)->get_pvt_uniqueid(c0));
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
	enum ast_bridge_result res = AST_BRIDGE_COMPLETE;
	struct ast_format_cap *o0nativeformats;
	struct ast_format_cap *o1nativeformats;
	long time_left_ms=0;
	char caller_warning = 0;
	char callee_warning = 0;

	*fo = NULL;

	if (ast_channel_internal_bridged_channel(c0)) {
		ast_log(LOG_WARNING, "%s is already in a bridge with %s\n",
			ast_channel_name(c0), ast_channel_name(ast_channel_internal_bridged_channel(c0)));
		return -1;
	}
	if (ast_channel_internal_bridged_channel(c1)) {
		ast_log(LOG_WARNING, "%s is already in a bridge with %s\n",
			ast_channel_name(c1), ast_channel_name(ast_channel_internal_bridged_channel(c1)));
		return -1;
	}

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(ast_channel_flags(c0), AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c0) ||
	    ast_test_flag(ast_channel_flags(c1), AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c1))
		return -1;

	o0nativeformats = ast_format_cap_dup(ast_channel_nativeformats(c0));
	o1nativeformats = ast_format_cap_dup(ast_channel_nativeformats(c1));
	if (!o0nativeformats || !o1nativeformats) {
		ast_format_cap_destroy(o0nativeformats);
		ast_format_cap_destroy(o1nativeformats);
		ast_log(LOG_WARNING, "failed to copy native formats\n");
		return -1;
	}

	caller_warning = ast_test_flag(&config->features_caller, AST_FEATURE_PLAY_WARNING);
	callee_warning = ast_test_flag(&config->features_callee, AST_FEATURE_PLAY_WARNING);

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
	ast_channel_lock_both(c0, c1);
	ast_channel_internal_bridged_channel_set(c0, c1);
	ast_channel_internal_bridged_channel_set(c1, c0);
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	ast_set_owners_and_peers(c0, c1);

	if (config->feature_timer && !ast_tvzero(config->nexteventts)) {
		config->nexteventts = ast_tvadd(config->feature_start_time, ast_samp2tv(config->feature_timer, 1000));
	} else if (config->timelimit) {
		time_left_ms = config->timelimit - ast_tvdiff_ms(ast_tvnow(), config->start_time);
		config->nexteventts = ast_tvadd(config->start_time, ast_samp2tv(config->timelimit, 1000));
		if ((caller_warning || callee_warning) && config->play_warning) {
			long next_warn = config->play_warning;
			if (time_left_ms < config->play_warning && config->warning_freq > 0) {
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

	if (!ast_channel_tech(c0)->send_digit_begin)
		ast_set_flag(ast_channel_flags(c1), AST_FLAG_END_DTMF_ONLY);
	if (!ast_channel_tech(c1)->send_digit_begin)
		ast_set_flag(ast_channel_flags(c0), AST_FLAG_END_DTMF_ONLY);
	manager_bridge_event(1, 1, c0, c1);

	/* Before we enter in and bridge these two together tell them both the source of audio has changed */
	ast_indicate(c0, AST_CONTROL_SRCUPDATE);
	ast_indicate(c1, AST_CONTROL_SRCUPDATE);

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
			if (time_left_ms < 0) {
				time_left_ms = 0;
			}

			if (time_left_ms < to) {
				to = time_left_ms;
			}

			if (time_left_ms <= 0) {
				if (caller_warning && config->end_sound)
					bridge_playfile(c0, c1, config->end_sound, 0);
				if (callee_warning && config->end_sound)
					bridge_playfile(c1, c0, config->end_sound, 0);
				*fo = NULL;
				res = AST_BRIDGE_COMPLETE;
				ast_test_suite_event_notify("BRIDGE_TIMELIMIT", "Channel1: %s\r\nChannel2: %s", ast_channel_name(c0), ast_channel_name(c1));
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

		if ((ast_channel_softhangup_internal_flag(c0) | ast_channel_softhangup_internal_flag(c1)) & AST_SOFTHANGUP_UNBRIDGE) {/* Bit operators are intentional. */
			if (ast_channel_softhangup_internal_flag(c0) & AST_SOFTHANGUP_UNBRIDGE) {
				ast_channel_clear_softhangup(c0, AST_SOFTHANGUP_UNBRIDGE);
			}
			if (ast_channel_softhangup_internal_flag(c1) & AST_SOFTHANGUP_UNBRIDGE) {
				ast_channel_clear_softhangup(c1, AST_SOFTHANGUP_UNBRIDGE);
			}
			ast_channel_lock_both(c0, c1);
			ast_channel_internal_bridged_channel_set(c0, c1);
			ast_channel_internal_bridged_channel_set(c1, c0);
			ast_channel_unlock(c0);
			ast_channel_unlock(c1);
			ast_debug(1, "Unbridge signal received. Ending native bridge.\n");
			continue;
		}

		/* Stop if we're a zombie or need a soft hangup */
		if (ast_test_flag(ast_channel_flags(c0), AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c0) ||
		    ast_test_flag(ast_channel_flags(c1), AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c1)) {
			*fo = NULL;
			res = AST_BRIDGE_COMPLETE;
			ast_debug(1, "Bridge stops because we're zombie or need a soft hangup: c0=%s, c1=%s, flags: %s,%s,%s,%s\n",
				ast_channel_name(c0), ast_channel_name(c1),
				ast_test_flag(ast_channel_flags(c0), AST_FLAG_ZOMBIE) ? "Yes" : "No",
				ast_check_hangup(c0) ? "Yes" : "No",
				ast_test_flag(ast_channel_flags(c1), AST_FLAG_ZOMBIE) ? "Yes" : "No",
				ast_check_hangup(c1) ? "Yes" : "No");
			break;
		}

		update_bridge_vars(c0, c1);

		bridge_play_sounds(c0, c1);

		if (ast_channel_tech(c0)->bridge &&
			/* if < 1 ms remains use generic bridging for accurate timing */
			(!config->timelimit || to > 1000 || to == -1) &&
		    (ast_channel_tech(c0)->bridge == ast_channel_tech(c1)->bridge) &&
		    !ast_channel_monitor(c0) && !ast_channel_monitor(c1) &&
		    !ast_channel_audiohooks(c0) && !ast_channel_audiohooks(c1) &&
		    ast_framehook_list_is_empty(ast_channel_framehooks(c0)) && ast_framehook_list_is_empty(ast_channel_framehooks(c1)) &&
		    !ast_channel_masq(c0) && !ast_channel_masqr(c0) && !ast_channel_masq(c1) && !ast_channel_masqr(c1)) {
			int timeoutms = to - 1000 > 0 ? to - 1000 : to;
			/* Looks like they share a bridge method and nothing else is in the way */
			ast_set_flag(ast_channel_flags(c0), AST_FLAG_NBRIDGE);
			ast_set_flag(ast_channel_flags(c1), AST_FLAG_NBRIDGE);
			if ((res = ast_channel_tech(c0)->bridge(c0, c1, config->flags, fo, rc, timeoutms)) == AST_BRIDGE_COMPLETE) {
				manager_bridge_event(0, 1, c0, c1);
				ast_debug(1, "Returning from native bridge, channels: %s, %s\n", ast_channel_name(c0), ast_channel_name(c1));

				ast_clear_flag(ast_channel_flags(c0), AST_FLAG_NBRIDGE);
				ast_clear_flag(ast_channel_flags(c1), AST_FLAG_NBRIDGE);

				if ((ast_channel_softhangup_internal_flag(c0) | ast_channel_softhangup_internal_flag(c1)) & AST_SOFTHANGUP_UNBRIDGE) {/* Bit operators are intentional. */
					continue;
				}

				ast_channel_lock_both(c0, c1);
				ast_channel_internal_bridged_channel_set(c0, NULL);
				ast_channel_internal_bridged_channel_set(c1, NULL);
				ast_channel_unlock(c0);
				ast_channel_unlock(c1);
				ast_format_cap_destroy(o0nativeformats);
				ast_format_cap_destroy(o1nativeformats);
				return res;
			} else {
				ast_clear_flag(ast_channel_flags(c0), AST_FLAG_NBRIDGE);
				ast_clear_flag(ast_channel_flags(c1), AST_FLAG_NBRIDGE);
			}
			switch (res) {
			case AST_BRIDGE_RETRY:
				if (config->play_warning) {
					ast_set_flag(config, AST_FEATURE_WARNING_ACTIVE);
				}
				continue;
			default:
				ast_verb(3, "Native bridging %s and %s ended\n", ast_channel_name(c0), ast_channel_name(c1));
				/* fallthrough */
			case AST_BRIDGE_FAILED_NOWARN:
				break;
			}
		}

		if (((ast_format_cmp(ast_channel_readformat(c1), ast_channel_writeformat(c0)) == AST_FORMAT_CMP_NOT_EQUAL) ||
			(ast_format_cmp(ast_channel_readformat(c0), ast_channel_writeformat(c1)) == AST_FORMAT_CMP_NOT_EQUAL) ||
		    !ast_format_cap_identical(ast_channel_nativeformats(c0), o0nativeformats) ||
			!ast_format_cap_identical(ast_channel_nativeformats(c1), o1nativeformats)) &&
		    !(ast_channel_generator(c0) || ast_channel_generator(c1))) {
			if (ast_channel_make_compatible(c0, c1)) {
				ast_log(LOG_WARNING, "Can't make %s and %s compatible\n", ast_channel_name(c0), ast_channel_name(c1));
				manager_bridge_event(0, 1, c0, c1);
				ast_format_cap_destroy(o0nativeformats);
				ast_format_cap_destroy(o1nativeformats);
				return AST_BRIDGE_FAILED;
			}

			ast_format_cap_copy(o0nativeformats, ast_channel_nativeformats(c0));
			ast_format_cap_copy(o1nativeformats, ast_channel_nativeformats(c1));
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

	ast_clear_flag(ast_channel_flags(c0), AST_FLAG_END_DTMF_ONLY);
	ast_clear_flag(ast_channel_flags(c1), AST_FLAG_END_DTMF_ONLY);

	/* Now that we have broken the bridge the source will change yet again */
	ast_indicate(c0, AST_CONTROL_SRCUPDATE);
	ast_indicate(c1, AST_CONTROL_SRCUPDATE);

	ast_channel_lock_both(c0, c1);
	ast_channel_internal_bridged_channel_set(c0, NULL);
	ast_channel_internal_bridged_channel_set(c1, NULL);
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	manager_bridge_event(0, 1, c0, c1);
	ast_debug(1, "Bridge stops bridging channels %s and %s\n", ast_channel_name(c0), ast_channel_name(c1));

	ast_format_cap_destroy(o0nativeformats);
	ast_format_cap_destroy(o1nativeformats);
	return res;
}

/*! \brief Sets an option on a channel */
int ast_channel_setoption(struct ast_channel *chan, int option, void *data, int datalen, int block)
{
	int res;

	ast_channel_lock(chan);
	if (!ast_channel_tech(chan)->setoption) {
		errno = ENOSYS;
		ast_channel_unlock(chan);
		return -1;
	}

	if (block)
		ast_log(LOG_ERROR, "XXX Blocking not implemented yet XXX\n");

	res = ast_channel_tech(chan)->setoption(chan, option, data, datalen);
	ast_channel_unlock(chan);

	return res;
}

int ast_channel_queryoption(struct ast_channel *chan, int option, void *data, int *datalen, int block)
{
	int res;

	ast_channel_lock(chan);
	if (!ast_channel_tech(chan)->queryoption) {
		errno = ENOSYS;
		ast_channel_unlock(chan);
		return -1;
	}

	if (block)
		ast_log(LOG_ERROR, "XXX Blocking not implemented yet XXX\n");

	res = ast_channel_tech(chan)->queryoption(chan, option, data, datalen);
	ast_channel_unlock(chan);

	return res;
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
	struct ast_format origwfmt;
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
		ast_set_write_format(chan, &ts->origwfmt);
	ast_free(ts);
}

static void *tonepair_alloc(struct ast_channel *chan, void *params)
{
	struct tonepair_state *ts;
	struct tonepair_def *td = params;

	if (!(ts = ast_calloc(1, sizeof(*ts))))
		return NULL;
	ast_format_copy(&ts->origwfmt, ast_channel_writeformat(chan));
	if (ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set '%s' to signed linear format (write)\n", ast_channel_name(chan));
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
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_WRITE_INT);
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
	ast_format_set(&ts->f.subclass.format, AST_FORMAT_SLINEAR, 0);
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
	.alloc = tonepair_alloc,
	.release = tonepair_release,
	.generate = tonepair_generator,
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
	while (ast_channel_generatordata(chan) && ast_waitfor(chan, 100) >= 0) {
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

/*! \brief Named group member structure */
struct namedgroup_member {
	/*! Pre-built hash of group member name. */
	unsigned int hash;
	/*! Group member name. (End allocation of name string.) */
	char name[1];
};

/*! \brief Comparison function used for named group container */
static int namedgroup_cmp_cb(void *obj, void *arg, int flags)
{
	const struct namedgroup_member *an = obj;
	const struct namedgroup_member *bn = arg;

	return strcmp(an->name, bn->name) ? 0 : CMP_MATCH | CMP_STOP;
}

/*! \brief Hashing function used for named group container */
static int namedgroup_hash_cb(const void *obj, const int flags)
{
	const struct namedgroup_member *member = obj;

	return member->hash;
}

struct ast_namedgroups *ast_get_namedgroups(const char *s)
{
	struct ao2_container *namedgroups;
	char *piece;
	char *c;

	if (!s) {
		return NULL;
	}

	/*! \brief Remove leading and trailing whitespace */
	c = ast_trim_blanks(ast_strdupa(ast_skip_blanks(s)));
	if (ast_strlen_zero(c)) {
		return NULL;
	}

	namedgroups = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 19,
		namedgroup_hash_cb, namedgroup_cmp_cb);
	if (!namedgroups) {
		return NULL;
	}

	while ((piece = strsep(&c, ","))) {
		struct namedgroup_member *member;
		size_t len;

		/* remove leading/trailing whitespace */
		piece = ast_strip(piece);

		len = strlen(piece);
		if (!len) {
			continue;
		}

		member = ao2_alloc_options(sizeof(*member) + len, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!member) {
			ao2_ref(namedgroups, -1);
			return NULL;
		}
		strcpy(member->name, piece);/* Safe */
		member->hash = ast_str_hash(member->name);

		/* every group name may exist only once, delete duplicates */
		ao2_find(namedgroups, member, OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
		ao2_link(namedgroups, member);
		ao2_ref(member, -1);
	}

	if (!ao2_container_count(namedgroups)) {
		/* There were no group names specified. */
		ao2_ref(namedgroups, -1);
		namedgroups = NULL;
	}

	return (struct ast_namedgroups *) namedgroups;
}

struct ast_namedgroups *ast_unref_namedgroups(struct ast_namedgroups *groups)
{
	ao2_cleanup(groups);
	return NULL;
}

struct ast_namedgroups *ast_ref_namedgroups(struct ast_namedgroups *groups)
{
	if (groups) {
		ao2_ref(groups, 1);
	}
	return groups;
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
	const char *name = (flags & OBJ_KEY) ? obj : ast_channel_name((struct ast_channel *) obj);

	/* If the name isn't set, return 0 so that the ao2_find() search will
	 * start in the first bucket. */
	if (ast_strlen_zero(name)) {
		return 0;
	}

	return ast_str_case_hash(name);
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
			ast_log(LOG_ERROR, "Unable to add channel structure for channel: %s\n", ast_channel_name(c));
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

static void channels_shutdown(void)
{
	ast_data_unregister(NULL);
	ast_cli_unregister_multiple(cli_channel, ARRAY_LEN(cli_channel));
	if (channels) {
		ao2_ref(channels, -1);
		channels = NULL;
	}
}

void ast_channels_init(void)
{
	channels = ao2_container_alloc(NUM_CHANNEL_BUCKETS,
			ast_channel_hash_cb, ast_channel_cmp_cb);

	ast_cli_register_multiple(cli_channel, ARRAY_LEN(cli_channel));

	ast_data_register_multiple_core(channel_providers, ARRAY_LEN(channel_providers));

	ast_plc_reload();

	ast_register_atexit(channels_shutdown);
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

char *ast_print_namedgroups(struct ast_str **buf, struct ast_namedgroups *group)
{
	struct ao2_container *grp = (struct ao2_container *) group;
	struct namedgroup_member *ng;
	int first = 1;
	struct ao2_iterator it;

	if (!grp) {
		return ast_str_buffer(*buf);
	}

	for (it = ao2_iterator_init(grp, 0); (ng = ao2_iterator_next(&it)); ao2_ref(ng, -1)) {
		if (!first) {
			ast_str_append(buf, 0, ", ");
		} else {
			first = 0;
		}
		ast_str_append(buf, 0, "%s", ng->name);
	}
	ao2_iterator_destroy(&it);

	return ast_str_buffer(*buf);
}

static int namedgroup_match(void *obj, void *arg, int flags)
{
	void *match;

	match = ao2_find(arg, obj, OBJ_POINTER);
	ao2_cleanup(match);

	return match ? CMP_MATCH | CMP_STOP : 0;
}

int ast_namedgroups_intersect(struct ast_namedgroups *a, struct ast_namedgroups *b)
{
	void *match;
	struct ao2_container *group_a = (struct ao2_container *) a;
	struct ao2_container *group_b = (struct ao2_container *) b;

	if (!a || !b) {
		return 0;
	}

	/*
	 * Do groups a and b intersect?  Since a and b are hash tables,
	 * the average time complexity is:
	 * O(a.count <= b.count ? a.count : b.count)
	 */
	if (ao2_container_count(group_b) < ao2_container_count(group_a)) {
		/* Traverse over the smaller group. */
		SWAP(group_a, group_b);
	}
	match = ao2_callback(group_a, 0, namedgroup_match, group_b);
	ao2_cleanup(match);

	return match != NULL;
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
		.data.ptr = buf,
		.samples = samples,
		.datalen = sizeof(buf),
	};
	ast_format_set(&frame.subclass.format, AST_FORMAT_SLINEAR, 0);

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
	struct ast_format old_write_format;
};

struct ast_silence_generator *ast_channel_start_silence_generator(struct ast_channel *chan)
{
	struct ast_silence_generator *state;

	if (!(state = ast_calloc(1, sizeof(*state)))) {
		return NULL;
	}

	ast_format_copy(&state->old_write_format, ast_channel_writeformat(chan));

	if (ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could not set write format to SLINEAR\n");
		ast_free(state);
		return NULL;
	}

	ast_activate_generator(chan, &silence_generator, state);

	ast_debug(1, "Started silence generator on '%s'\n", ast_channel_name(chan));

	return state;
}

static int deactivate_silence_generator(struct ast_channel *chan)
{
	ast_channel_lock(chan);

	if (!ast_channel_generatordata(chan)) {
		ast_debug(1, "Trying to stop silence generator when there is no generator on '%s'\n",
			ast_channel_name(chan));
		ast_channel_unlock(chan);
		return 0;
	}
	if (ast_channel_generator(chan) != &silence_generator) {
		ast_debug(1, "Trying to stop silence generator when it is not the current generator on '%s'\n",
			ast_channel_name(chan));
		ast_channel_unlock(chan);
		return 0;
	}
	deactivate_generator_nolock(chan);

	ast_channel_unlock(chan);

	return 1;
}

void ast_channel_stop_silence_generator(struct ast_channel *chan, struct ast_silence_generator *state)
{
	if (!state) {
		return;
	}

	if (deactivate_silence_generator(chan)) {
		ast_debug(1, "Stopped silence generator on '%s'\n", ast_channel_name(chan));
		if (ast_set_write_format(chan, &state->old_write_format) < 0)
			ast_log(LOG_ERROR, "Could not return write format to its original state\n");
	}
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
	if (ast_channel_connected(chan) == connected) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	ast_party_connected_line_set(ast_channel_connected(chan), connected, update);
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
	/*!
	 * \brief Combined name and number presentation ie.
	 * \note Not sent if value is zero.
	 */
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
	if (ies->combined_presentation && (!update || update->number)) {
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
	/*
	 * No more party id combined number and name presentation values
	 * need to be created.
	 */
	AST_CONNECTED_LINE_NAME_VALID,
	AST_CONNECTED_LINE_NAME_CHAR_SET,
	AST_CONNECTED_LINE_NAME_PRESENTATION,
	AST_CONNECTED_LINE_NUMBER_VALID,
	AST_CONNECTED_LINE_NUMBER_PRESENTATION,
	AST_CONNECTED_LINE_PRIV_NUMBER,
	AST_CONNECTED_LINE_PRIV_NUMBER_PLAN,
	AST_CONNECTED_LINE_PRIV_NUMBER_VALID,
	AST_CONNECTED_LINE_PRIV_NUMBER_PRESENTATION,
	AST_CONNECTED_LINE_PRIV_NAME,
	AST_CONNECTED_LINE_PRIV_NAME_VALID,
	AST_CONNECTED_LINE_PRIV_NAME_CHAR_SET,
	AST_CONNECTED_LINE_PRIV_NAME_PRESENTATION,
	AST_CONNECTED_LINE_PRIV_SUBADDRESS,
	AST_CONNECTED_LINE_PRIV_SUBADDRESS_TYPE,
	AST_CONNECTED_LINE_PRIV_SUBADDRESS_ODD_EVEN,
	AST_CONNECTED_LINE_PRIV_SUBADDRESS_VALID,
	AST_CONNECTED_LINE_PRIV_TAG,
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

	static const struct ast_party_id_ies priv_ies = {
		.name.str = AST_CONNECTED_LINE_PRIV_NAME,
		.name.char_set = AST_CONNECTED_LINE_PRIV_NAME_CHAR_SET,
		.name.presentation = AST_CONNECTED_LINE_PRIV_NAME_PRESENTATION,
		.name.valid = AST_CONNECTED_LINE_PRIV_NAME_VALID,

		.number.str = AST_CONNECTED_LINE_PRIV_NUMBER,
		.number.plan = AST_CONNECTED_LINE_PRIV_NUMBER_PLAN,
		.number.presentation = AST_CONNECTED_LINE_PRIV_NUMBER_PRESENTATION,
		.number.valid = AST_CONNECTED_LINE_PRIV_NUMBER_VALID,

		.subaddress.str = AST_CONNECTED_LINE_PRIV_SUBADDRESS,
		.subaddress.type = AST_CONNECTED_LINE_PRIV_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_CONNECTED_LINE_PRIV_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_CONNECTED_LINE_PRIV_SUBADDRESS_VALID,

		.tag = AST_CONNECTED_LINE_PRIV_TAG,
		.combined_presentation = 0,/* Not sent. */
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

	res = party_id_build_data(data + pos, datalen - pos, &connected->priv,
		"connected line priv", &priv_ies, update ? &update->priv : NULL);
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
/* Private connected line party id name */
		case AST_CONNECTED_LINE_PRIV_NAME:
			ast_free(connected->priv.name.str);
			connected->priv.name.str = ast_malloc(ie_len + 1);
			if (connected->priv.name.str) {
				memcpy(connected->priv.name.str, data + pos, ie_len);
				connected->priv.name.str[ie_len] = 0;
			}
			break;
		case AST_CONNECTED_LINE_PRIV_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.name.char_set = data[pos];
			break;
		case AST_CONNECTED_LINE_PRIV_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.name.presentation = data[pos];
			break;
		case AST_CONNECTED_LINE_PRIV_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.name.valid = data[pos];
			break;
/* Private connected line party id number */
		case AST_CONNECTED_LINE_PRIV_NUMBER:
			ast_free(connected->priv.number.str);
			connected->priv.number.str = ast_malloc(ie_len + 1);
			if (connected->priv.number.str) {
				memcpy(connected->priv.number.str, data + pos, ie_len);
				connected->priv.number.str[ie_len] = 0;
			}
			break;
		case AST_CONNECTED_LINE_PRIV_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.number.plan = data[pos];
			break;
		case AST_CONNECTED_LINE_PRIV_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.number.presentation = data[pos];
			break;
		case AST_CONNECTED_LINE_PRIV_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.number.valid = data[pos];
			break;
/* Private connected line party id subaddress */
		case AST_CONNECTED_LINE_PRIV_SUBADDRESS:
			ast_free(connected->priv.subaddress.str);
			connected->priv.subaddress.str = ast_malloc(ie_len + 1);
			if (connected->priv.subaddress.str) {
				memcpy(connected->priv.subaddress.str, data + pos, ie_len);
				connected->priv.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_CONNECTED_LINE_PRIV_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.subaddress.type = data[pos];
			break;
		case AST_CONNECTED_LINE_PRIV_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid connected line private subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_CONNECTED_LINE_PRIV_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid connected line private subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			connected->priv.subaddress.valid = data[pos];
			break;
/* Private connected line party tag */
		case AST_CONNECTED_LINE_PRIV_TAG:
			ast_free(connected->priv.tag);
			connected->priv.tag = ast_malloc(ie_len + 1);
			if (connected->priv.tag) {
				memcpy(connected->priv.tag, data + pos, ie_len);
				connected->priv.tag[ie_len] = 0;
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
			ast_debug(1, "Unknown connected line element: %u (%u)\n",
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
		ast_debug(1, "Connected line frame has newer version: %u\n",
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
	if (ast_channel_redirecting(chan) == redirecting) {
		/* Don't set to self */
		return;
	}

	ast_channel_lock(chan);
	ast_party_redirecting_set(ast_channel_redirecting(chan), redirecting, update);
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
	AST_REDIRECTING_FROM_ID_PRESENTATION,/* Combined number and name presentation. */
	AST_REDIRECTING_TO_NUMBER,
	AST_REDIRECTING_TO_NAME,
	AST_REDIRECTING_TO_NUMBER_PLAN,
	AST_REDIRECTING_TO_ID_PRESENTATION,/* Combined number and name presentation. */
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
	/*
	 * No more party id combined number and name presentation values
	 * need to be created.
	 */
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
	AST_REDIRECTING_ORIG_NUMBER,
	AST_REDIRECTING_ORIG_NUMBER_VALID,
	AST_REDIRECTING_ORIG_NUMBER_PLAN,
	AST_REDIRECTING_ORIG_NUMBER_PRESENTATION,
	AST_REDIRECTING_ORIG_NAME,
	AST_REDIRECTING_ORIG_NAME_VALID,
	AST_REDIRECTING_ORIG_NAME_CHAR_SET,
	AST_REDIRECTING_ORIG_NAME_PRESENTATION,
	AST_REDIRECTING_ORIG_SUBADDRESS,
	AST_REDIRECTING_ORIG_SUBADDRESS_TYPE,
	AST_REDIRECTING_ORIG_SUBADDRESS_ODD_EVEN,
	AST_REDIRECTING_ORIG_SUBADDRESS_VALID,
	AST_REDIRECTING_ORIG_TAG,
	AST_REDIRECTING_ORIG_REASON,
	AST_REDIRECTING_PRIV_TO_NUMBER,
	AST_REDIRECTING_PRIV_TO_NUMBER_PLAN,
	AST_REDIRECTING_PRIV_TO_NUMBER_VALID,
	AST_REDIRECTING_PRIV_TO_NUMBER_PRESENTATION,
	AST_REDIRECTING_PRIV_TO_NAME,
	AST_REDIRECTING_PRIV_TO_NAME_VALID,
	AST_REDIRECTING_PRIV_TO_NAME_CHAR_SET,
	AST_REDIRECTING_PRIV_TO_NAME_PRESENTATION,
	AST_REDIRECTING_PRIV_TO_SUBADDRESS,
	AST_REDIRECTING_PRIV_TO_SUBADDRESS_TYPE,
	AST_REDIRECTING_PRIV_TO_SUBADDRESS_ODD_EVEN,
	AST_REDIRECTING_PRIV_TO_SUBADDRESS_VALID,
	AST_REDIRECTING_PRIV_TO_TAG,
	AST_REDIRECTING_PRIV_FROM_NUMBER,
	AST_REDIRECTING_PRIV_FROM_NUMBER_PLAN,
	AST_REDIRECTING_PRIV_FROM_NUMBER_VALID,
	AST_REDIRECTING_PRIV_FROM_NUMBER_PRESENTATION,
	AST_REDIRECTING_PRIV_FROM_NAME,
	AST_REDIRECTING_PRIV_FROM_NAME_VALID,
	AST_REDIRECTING_PRIV_FROM_NAME_CHAR_SET,
	AST_REDIRECTING_PRIV_FROM_NAME_PRESENTATION,
	AST_REDIRECTING_PRIV_FROM_SUBADDRESS,
	AST_REDIRECTING_PRIV_FROM_SUBADDRESS_TYPE,
	AST_REDIRECTING_PRIV_FROM_SUBADDRESS_ODD_EVEN,
	AST_REDIRECTING_PRIV_FROM_SUBADDRESS_VALID,
	AST_REDIRECTING_PRIV_FROM_TAG,
	AST_REDIRECTING_PRIV_ORIG_NUMBER,
	AST_REDIRECTING_PRIV_ORIG_NUMBER_VALID,
	AST_REDIRECTING_PRIV_ORIG_NUMBER_PLAN,
	AST_REDIRECTING_PRIV_ORIG_NUMBER_PRESENTATION,
	AST_REDIRECTING_PRIV_ORIG_NAME,
	AST_REDIRECTING_PRIV_ORIG_NAME_VALID,
	AST_REDIRECTING_PRIV_ORIG_NAME_CHAR_SET,
	AST_REDIRECTING_PRIV_ORIG_NAME_PRESENTATION,
	AST_REDIRECTING_PRIV_ORIG_SUBADDRESS,
	AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_TYPE,
	AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_ODD_EVEN,
	AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_VALID,
	AST_REDIRECTING_PRIV_ORIG_TAG,
};

int ast_redirecting_build_data(unsigned char *data, size_t datalen, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update)
{
	int32_t value;
	size_t pos = 0;
	int res;

	static const struct ast_party_id_ies orig_ies = {
		.name.str = AST_REDIRECTING_ORIG_NAME,
		.name.char_set = AST_REDIRECTING_ORIG_NAME_CHAR_SET,
		.name.presentation = AST_REDIRECTING_ORIG_NAME_PRESENTATION,
		.name.valid = AST_REDIRECTING_ORIG_NAME_VALID,

		.number.str = AST_REDIRECTING_ORIG_NUMBER,
		.number.plan = AST_REDIRECTING_ORIG_NUMBER_PLAN,
		.number.presentation = AST_REDIRECTING_ORIG_NUMBER_PRESENTATION,
		.number.valid = AST_REDIRECTING_ORIG_NUMBER_VALID,

		.subaddress.str = AST_REDIRECTING_ORIG_SUBADDRESS,
		.subaddress.type = AST_REDIRECTING_ORIG_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_REDIRECTING_ORIG_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_REDIRECTING_ORIG_SUBADDRESS_VALID,

		.tag = AST_REDIRECTING_ORIG_TAG,
		.combined_presentation = 0,/* Not sent. */
	};
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
	static const struct ast_party_id_ies priv_orig_ies = {
		.name.str = AST_REDIRECTING_PRIV_ORIG_NAME,
		.name.char_set = AST_REDIRECTING_PRIV_ORIG_NAME_CHAR_SET,
		.name.presentation = AST_REDIRECTING_PRIV_ORIG_NAME_PRESENTATION,
		.name.valid = AST_REDIRECTING_PRIV_ORIG_NAME_VALID,

		.number.str = AST_REDIRECTING_PRIV_ORIG_NUMBER,
		.number.plan = AST_REDIRECTING_PRIV_ORIG_NUMBER_PLAN,
		.number.presentation = AST_REDIRECTING_PRIV_ORIG_NUMBER_PRESENTATION,
		.number.valid = AST_REDIRECTING_PRIV_ORIG_NUMBER_VALID,

		.subaddress.str = AST_REDIRECTING_PRIV_ORIG_SUBADDRESS,
		.subaddress.type = AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_VALID,

		.tag = AST_REDIRECTING_PRIV_ORIG_TAG,
		.combined_presentation = 0,/* Not sent. */
	};
	static const struct ast_party_id_ies priv_from_ies = {
		.name.str = AST_REDIRECTING_PRIV_FROM_NAME,
		.name.char_set = AST_REDIRECTING_PRIV_FROM_NAME_CHAR_SET,
		.name.presentation = AST_REDIRECTING_PRIV_FROM_NAME_PRESENTATION,
		.name.valid = AST_REDIRECTING_PRIV_FROM_NAME_VALID,

		.number.str = AST_REDIRECTING_PRIV_FROM_NUMBER,
		.number.plan = AST_REDIRECTING_PRIV_FROM_NUMBER_PLAN,
		.number.presentation = AST_REDIRECTING_PRIV_FROM_NUMBER_PRESENTATION,
		.number.valid = AST_REDIRECTING_PRIV_FROM_NUMBER_VALID,

		.subaddress.str = AST_REDIRECTING_PRIV_FROM_SUBADDRESS,
		.subaddress.type = AST_REDIRECTING_PRIV_FROM_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_REDIRECTING_PRIV_FROM_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_REDIRECTING_PRIV_FROM_SUBADDRESS_VALID,

		.tag = AST_REDIRECTING_PRIV_FROM_TAG,
		.combined_presentation = 0,/* Not sent. */
	};
	static const struct ast_party_id_ies priv_to_ies = {
		.name.str = AST_REDIRECTING_PRIV_TO_NAME,
		.name.char_set = AST_REDIRECTING_PRIV_TO_NAME_CHAR_SET,
		.name.presentation = AST_REDIRECTING_PRIV_TO_NAME_PRESENTATION,
		.name.valid = AST_REDIRECTING_PRIV_TO_NAME_VALID,

		.number.str = AST_REDIRECTING_PRIV_TO_NUMBER,
		.number.plan = AST_REDIRECTING_PRIV_TO_NUMBER_PLAN,
		.number.presentation = AST_REDIRECTING_PRIV_TO_NUMBER_PRESENTATION,
		.number.valid = AST_REDIRECTING_PRIV_TO_NUMBER_VALID,

		.subaddress.str = AST_REDIRECTING_PRIV_TO_SUBADDRESS,
		.subaddress.type = AST_REDIRECTING_PRIV_TO_SUBADDRESS_TYPE,
		.subaddress.odd_even_indicator = AST_REDIRECTING_PRIV_TO_SUBADDRESS_ODD_EVEN,
		.subaddress.valid = AST_REDIRECTING_PRIV_TO_SUBADDRESS_VALID,

		.tag = AST_REDIRECTING_PRIV_TO_TAG,
		.combined_presentation = 0,/* Not sent. */
	};

	/* Redirecting frame version */
	if (datalen < pos + (sizeof(data[0]) * 2) + 1) {
		ast_log(LOG_WARNING, "No space left for redirecting frame version\n");
		return -1;
	}
	data[pos++] = AST_REDIRECTING_VERSION;
	data[pos++] = 1;
	data[pos++] = 2;/* Version 1 did not have a version ie */

	res = party_id_build_data(data + pos, datalen - pos, &redirecting->orig,
		"redirecting-orig", &orig_ies, update ? &update->orig : NULL);
	if (res < 0) {
		return -1;
	}
	pos += res;

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

	res = party_id_build_data(data + pos, datalen - pos, &redirecting->priv_orig,
		"redirecting-priv-orig", &priv_orig_ies, update ? &update->priv_orig : NULL);
	if (res < 0) {
		return -1;
	}
	pos += res;

	res = party_id_build_data(data + pos, datalen - pos, &redirecting->priv_from,
		"redirecting-priv-from", &priv_from_ies, update ? &update->priv_from : NULL);
	if (res < 0) {
		return -1;
	}
	pos += res;

	res = party_id_build_data(data + pos, datalen - pos, &redirecting->priv_to,
		"redirecting-priv-to", &priv_to_ies, update ? &update->priv_to : NULL);
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

	/* Redirecting original reason */
	if (datalen < pos + (sizeof(data[0]) * 2) + sizeof(value)) {
		ast_log(LOG_WARNING, "No space left for redirecting original reason\n");
		return -1;
	}
	data[pos++] = AST_REDIRECTING_ORIG_REASON;
	data[pos++] = sizeof(value);
	value = htonl(redirecting->orig_reason);
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
/* Redirecting-orig party id name */
		case AST_REDIRECTING_ORIG_NAME:
			ast_free(redirecting->orig.name.str);
			redirecting->orig.name.str = ast_malloc(ie_len + 1);
			if (redirecting->orig.name.str) {
				memcpy(redirecting->orig.name.str, data + pos, ie_len);
				redirecting->orig.name.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_ORIG_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.name.char_set = data[pos];
			break;
		case AST_REDIRECTING_ORIG_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.name.presentation = data[pos];
			break;
		case AST_REDIRECTING_ORIG_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.name.valid = data[pos];
			break;
/* Redirecting-orig party id number */
		case AST_REDIRECTING_ORIG_NUMBER:
			ast_free(redirecting->orig.number.str);
			redirecting->orig.number.str = ast_malloc(ie_len + 1);
			if (redirecting->orig.number.str) {
				memcpy(redirecting->orig.number.str, data + pos, ie_len);
				redirecting->orig.number.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_ORIG_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.number.plan = data[pos];
			break;
		case AST_REDIRECTING_ORIG_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.number.presentation = data[pos];
			break;
		case AST_REDIRECTING_ORIG_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.number.valid = data[pos];
			break;
/* Redirecting-orig party id subaddress */
		case AST_REDIRECTING_ORIG_SUBADDRESS:
			ast_free(redirecting->orig.subaddress.str);
			redirecting->orig.subaddress.str = ast_malloc(ie_len + 1);
			if (redirecting->orig.subaddress.str) {
				memcpy(redirecting->orig.subaddress.str, data + pos, ie_len);
				redirecting->orig.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_ORIG_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.subaddress.type = data[pos];
			break;
		case AST_REDIRECTING_ORIG_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid redirecting-orig subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_REDIRECTING_ORIG_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid redirecting-orig subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->orig.subaddress.valid = data[pos];
			break;
/* Redirecting-orig party id tag */
		case AST_REDIRECTING_ORIG_TAG:
			ast_free(redirecting->orig.tag);
			redirecting->orig.tag = ast_malloc(ie_len + 1);
			if (redirecting->orig.tag) {
				memcpy(redirecting->orig.tag, data + pos, ie_len);
				redirecting->orig.tag[ie_len] = 0;
			}
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
/* Private redirecting-orig party id name */
		case AST_REDIRECTING_PRIV_ORIG_NAME:
			ast_free(redirecting->priv_orig.name.str);
			redirecting->priv_orig.name.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_orig.name.str) {
				memcpy(redirecting->priv_orig.name.str, data + pos, ie_len);
				redirecting->priv_orig.name.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_ORIG_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.name.char_set = data[pos];
			break;
		case AST_REDIRECTING_PRIV_ORIG_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.name.presentation = data[pos];
			break;
		case AST_REDIRECTING_PRIV_ORIG_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.name.valid = data[pos];
			break;
/* Private redirecting-orig party id number */
		case AST_REDIRECTING_PRIV_ORIG_NUMBER:
			ast_free(redirecting->priv_orig.number.str);
			redirecting->priv_orig.number.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_orig.number.str) {
				memcpy(redirecting->priv_orig.number.str, data + pos, ie_len);
				redirecting->priv_orig.number.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_ORIG_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.number.plan = data[pos];
			break;
		case AST_REDIRECTING_PRIV_ORIG_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.number.presentation = data[pos];
			break;
		case AST_REDIRECTING_PRIV_ORIG_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.number.valid = data[pos];
			break;
/* Private redirecting-orig party id subaddress */
		case AST_REDIRECTING_PRIV_ORIG_SUBADDRESS:
			ast_free(redirecting->priv_orig.subaddress.str);
			redirecting->priv_orig.subaddress.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_orig.subaddress.str) {
				memcpy(redirecting->priv_orig.subaddress.str, data + pos, ie_len);
				redirecting->priv_orig.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.subaddress.type = data[pos];
			break;
		case AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid private redirecting-orig subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_REDIRECTING_PRIV_ORIG_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-orig subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_orig.subaddress.valid = data[pos];
			break;
/* Private redirecting-orig party id tag */
		case AST_REDIRECTING_PRIV_ORIG_TAG:
			ast_free(redirecting->priv_orig.tag);
			redirecting->priv_orig.tag = ast_malloc(ie_len + 1);
			if (redirecting->priv_orig.tag) {
				memcpy(redirecting->priv_orig.tag, data + pos, ie_len);
				redirecting->priv_orig.tag[ie_len] = 0;
			}
			break;
/* Private redirecting-from party id name */
		case AST_REDIRECTING_PRIV_FROM_NAME:
			ast_free(redirecting->priv_from.name.str);
			redirecting->priv_from.name.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_from.name.str) {
				memcpy(redirecting->priv_from.name.str, data + pos, ie_len);
				redirecting->priv_from.name.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_FROM_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.name.char_set = data[pos];
			break;
		case AST_REDIRECTING_PRIV_FROM_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.name.presentation = data[pos];
			break;
		case AST_REDIRECTING_PRIV_FROM_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.name.valid = data[pos];
			break;
/* Private redirecting-from party id number */
		case AST_REDIRECTING_PRIV_FROM_NUMBER:
			ast_free(redirecting->priv_from.number.str);
			redirecting->priv_from.number.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_from.number.str) {
				memcpy(redirecting->priv_from.number.str, data + pos, ie_len);
				redirecting->priv_from.number.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_FROM_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.number.plan = data[pos];
			break;
		case AST_REDIRECTING_PRIV_FROM_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.number.presentation = data[pos];
			break;
		case AST_REDIRECTING_PRIV_FROM_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.number.valid = data[pos];
			break;
/* Private redirecting-from party id subaddress */
		case AST_REDIRECTING_PRIV_FROM_SUBADDRESS:
			ast_free(redirecting->priv_from.subaddress.str);
			redirecting->priv_from.subaddress.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_from.subaddress.str) {
				memcpy(redirecting->priv_from.subaddress.str, data + pos, ie_len);
				redirecting->priv_from.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_FROM_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.subaddress.type = data[pos];
			break;
		case AST_REDIRECTING_PRIV_FROM_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid private redirecting-from subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_REDIRECTING_PRIV_FROM_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-from subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_from.subaddress.valid = data[pos];
			break;
/* Private redirecting-from party id tag */
		case AST_REDIRECTING_PRIV_FROM_TAG:
			ast_free(redirecting->priv_from.tag);
			redirecting->priv_from.tag = ast_malloc(ie_len + 1);
			if (redirecting->priv_from.tag) {
				memcpy(redirecting->priv_from.tag, data + pos, ie_len);
				redirecting->priv_from.tag[ie_len] = 0;
			}
			break;
/* Private redirecting-to party id name */
		case AST_REDIRECTING_PRIV_TO_NAME:
			ast_free(redirecting->priv_to.name.str);
			redirecting->priv_to.name.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_to.name.str) {
				memcpy(redirecting->priv_to.name.str, data + pos, ie_len);
				redirecting->priv_to.name.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_TO_NAME_CHAR_SET:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to name char set (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.name.char_set = data[pos];
			break;
		case AST_REDIRECTING_PRIV_TO_NAME_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to name presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.name.presentation = data[pos];
			break;
		case AST_REDIRECTING_PRIV_TO_NAME_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to name valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.name.valid = data[pos];
			break;
/* Private redirecting-to party id number */
		case AST_REDIRECTING_PRIV_TO_NUMBER:
			ast_free(redirecting->priv_to.number.str);
			redirecting->priv_to.number.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_to.number.str) {
				memcpy(redirecting->priv_to.number.str, data + pos, ie_len);
				redirecting->priv_to.number.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_TO_NUMBER_PLAN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to numbering plan (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.number.plan = data[pos];
			break;
		case AST_REDIRECTING_PRIV_TO_NUMBER_PRESENTATION:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to number presentation (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.number.presentation = data[pos];
			break;
		case AST_REDIRECTING_PRIV_TO_NUMBER_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to number valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.number.valid = data[pos];
			break;
/* Private redirecting-to party id subaddress */
		case AST_REDIRECTING_PRIV_TO_SUBADDRESS:
			ast_free(redirecting->priv_to.subaddress.str);
			redirecting->priv_to.subaddress.str = ast_malloc(ie_len + 1);
			if (redirecting->priv_to.subaddress.str) {
				memcpy(redirecting->priv_to.subaddress.str, data + pos, ie_len);
				redirecting->priv_to.subaddress.str[ie_len] = 0;
			}
			break;
		case AST_REDIRECTING_PRIV_TO_SUBADDRESS_TYPE:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to type of subaddress (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.subaddress.type = data[pos];
			break;
		case AST_REDIRECTING_PRIV_TO_SUBADDRESS_ODD_EVEN:
			if (ie_len != 1) {
				ast_log(LOG_WARNING,
					"Invalid private redirecting-to subaddress odd-even indicator (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.subaddress.odd_even_indicator = data[pos];
			break;
		case AST_REDIRECTING_PRIV_TO_SUBADDRESS_VALID:
			if (ie_len != 1) {
				ast_log(LOG_WARNING, "Invalid private redirecting-to subaddress valid (%u)\n",
					(unsigned) ie_len);
				break;
			}
			redirecting->priv_to.subaddress.valid = data[pos];
			break;
/* Private redirecting-to party id tag */
		case AST_REDIRECTING_PRIV_TO_TAG:
			ast_free(redirecting->priv_to.tag);
			redirecting->priv_to.tag = ast_malloc(ie_len + 1);
			if (redirecting->priv_to.tag) {
				memcpy(redirecting->priv_to.tag, data + pos, ie_len);
				redirecting->priv_to.tag[ie_len] = 0;
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
/* Redirecting orig-reason */
		case AST_REDIRECTING_ORIG_REASON:
			if (ie_len != sizeof(value)) {
				ast_log(LOG_WARNING, "Invalid redirecting original reason (%u)\n",
					(unsigned) ie_len);
				break;
			}
			memcpy(&value, data + pos, sizeof(value));
			redirecting->orig_reason = ntohl(value);
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
			ast_debug(1, "Unknown redirecting element: %u (%u)\n",
				(unsigned) ie_id, (unsigned) ie_len);
			break;
		}
	}

	switch (frame_version) {
	case 1:
		/*
		 * The other end is an earlier version that we need to adjust
		 * for compatibility.
		 *
		 * The earlier version did not have the orig party id or
		 * orig_reason value.
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
		ast_debug(1, "Redirecting frame has newer version: %u\n",
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
	static int deprecation_warning = 0;
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

	if (ast_strlen_zero(macro)) {
		ast_channel_unlock(macro_chan);
		return -1;
	}

	if (!deprecation_warning) {
		deprecation_warning = 1;
		ast_log(LOG_WARNING, "Usage of CONNECTED_LINE_CALLE[ER]_SEND_MACRO is deprecated.  Please use CONNECTED_LINE_SEND_SUB instead.\n");
	}
	if (is_frame) {
		const struct ast_frame *frame = connected_info;

		ast_connected_line_parse_data(frame->data.ptr, frame->datalen, ast_channel_connected(macro_chan));
	} else {
		const struct ast_party_connected_line *connected = connected_info;

		ast_party_connected_line_copy(ast_channel_connected(macro_chan), connected);
	}
	ast_channel_unlock(macro_chan);

	retval = ast_app_run_macro(autoservice_chan, macro_chan, macro, macro_args);
	if (!retval) {
		struct ast_party_connected_line saved_connected;

		ast_party_connected_line_init(&saved_connected);
		ast_channel_lock(macro_chan);
		ast_party_connected_line_copy(&saved_connected, ast_channel_connected(macro_chan));
		ast_channel_unlock(macro_chan);
		ast_channel_update_connected_line(macro_chan, &saved_connected, NULL);
		ast_party_connected_line_free(&saved_connected);
	}

	return retval;
}

int ast_channel_redirecting_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const void *redirecting_info, int is_caller, int is_frame)
{
	static int deprecation_warning = 0;
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

	if (ast_strlen_zero(macro)) {
		ast_channel_unlock(macro_chan);
		return -1;
	}

	if (!deprecation_warning) {
		deprecation_warning = 1;
		ast_log(LOG_WARNING, "Usage of REDIRECTING_CALLE[ER]_SEND_MACRO is deprecated.  Please use REDIRECTING_SEND_SUB instead.\n");
	}
	if (is_frame) {
		const struct ast_frame *frame = redirecting_info;

		ast_redirecting_parse_data(frame->data.ptr, frame->datalen, ast_channel_redirecting(macro_chan));
	} else {
		const struct ast_party_redirecting *redirecting = redirecting_info;

		ast_party_redirecting_copy(ast_channel_redirecting(macro_chan), redirecting);
	}
	ast_channel_unlock(macro_chan);

	retval = ast_app_run_macro(autoservice_chan, macro_chan, macro, macro_args);
	if (!retval) {
		struct ast_party_redirecting saved_redirecting;

		ast_party_redirecting_init(&saved_redirecting);
		ast_channel_lock(macro_chan);
		ast_party_redirecting_copy(&saved_redirecting, ast_channel_redirecting(macro_chan));
		ast_channel_unlock(macro_chan);
		ast_channel_update_redirecting(macro_chan, &saved_redirecting, NULL);
		ast_party_redirecting_free(&saved_redirecting);
	}

	return retval;
}

int ast_channel_connected_line_sub(struct ast_channel *autoservice_chan, struct ast_channel *sub_chan, const void *connected_info, int is_frame)
{
	const char *sub;
	const char *sub_args;
	int retval;

	ast_channel_lock(sub_chan);
	sub = pbx_builtin_getvar_helper(sub_chan, "CONNECTED_LINE_SEND_SUB");
	sub = ast_strdupa(S_OR(sub, ""));
	sub_args = pbx_builtin_getvar_helper(sub_chan, "CONNECTED_LINE_SEND_SUB_ARGS");
	sub_args = ast_strdupa(S_OR(sub_args, ""));

	if (ast_strlen_zero(sub)) {
		ast_channel_unlock(sub_chan);
		return -1;
	}

	if (is_frame) {
		const struct ast_frame *frame = connected_info;

		ast_connected_line_parse_data(frame->data.ptr, frame->datalen, ast_channel_connected(sub_chan));
	} else {
		const struct ast_party_connected_line *connected = connected_info;

		ast_party_connected_line_copy(ast_channel_connected(sub_chan), connected);
	}
	ast_channel_unlock(sub_chan);

	retval = ast_app_run_sub(autoservice_chan, sub_chan, sub, sub_args, 0);
	if (!retval) {
		struct ast_party_connected_line saved_connected;

		ast_party_connected_line_init(&saved_connected);
		ast_channel_lock(sub_chan);
		ast_party_connected_line_copy(&saved_connected, ast_channel_connected(sub_chan));
		ast_channel_unlock(sub_chan);
		ast_channel_update_connected_line(sub_chan, &saved_connected, NULL);
		ast_party_connected_line_free(&saved_connected);
	}

	return retval;
}

int ast_channel_redirecting_sub(struct ast_channel *autoservice_chan, struct ast_channel *sub_chan, const void *redirecting_info, int is_frame)
{
	const char *sub;
	const char *sub_args;
	int retval;

	ast_channel_lock(sub_chan);
	sub = pbx_builtin_getvar_helper(sub_chan, "REDIRECTING_SEND_SUB");
	sub = ast_strdupa(S_OR(sub, ""));
	sub_args = pbx_builtin_getvar_helper(sub_chan, "REDIRECTING_SEND_SUB_ARGS");
	sub_args = ast_strdupa(S_OR(sub_args, ""));

	if (ast_strlen_zero(sub)) {
		ast_channel_unlock(sub_chan);
		return -1;
	}

	if (is_frame) {
		const struct ast_frame *frame = redirecting_info;

		ast_redirecting_parse_data(frame->data.ptr, frame->datalen, ast_channel_redirecting(sub_chan));
	} else {
		const struct ast_party_redirecting *redirecting = redirecting_info;

		ast_party_redirecting_copy(ast_channel_redirecting(sub_chan), redirecting);
	}
	ast_channel_unlock(sub_chan);

	retval = ast_app_run_sub(autoservice_chan, sub_chan, sub, sub_args, 0);
	if (!retval) {
		struct ast_party_redirecting saved_redirecting;

		ast_party_redirecting_init(&saved_redirecting);
		ast_channel_lock(sub_chan);
		ast_party_redirecting_copy(&saved_redirecting, ast_channel_redirecting(sub_chan));
		ast_channel_unlock(sub_chan);
		ast_channel_update_redirecting(sub_chan, &saved_redirecting, NULL);
		ast_party_redirecting_free(&saved_redirecting);
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
	ast_copy_string(device_name, ast_channel_name(chan), name_buffer_length);
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

	ast_copy_string(agent_type, ast_channel_name(chan), size);
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
	va_list ap;
	struct ast_channel *result;


	va_start(ap, name_fmt);
	result = __ast_channel_alloc_ap(needqueue, state, cid_num, cid_name, acctcode, exten, context,
					linkedid, amaflag, __FILE__, __LINE__, __FUNCTION__, name_fmt, ap);
	va_end(ap);

	return result;
}

void ast_channel_unlink(struct ast_channel *chan)
{
	ao2_unlink(channels, chan);
}
