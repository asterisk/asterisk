/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
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
 * \brief PRI signaling module
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
/*** DOCUMENTATION
	<managerEvent language="en_US" name="MCID">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Published when a malicious call ID request arrives.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="MCallerIDNumValid">
				</parameter>
				<parameter name="MCallerIDNum">
				</parameter>
				<parameter name="MCallerIDton">
				</parameter>
				<parameter name="MCallerIDNumPlan">
				</parameter>
				<parameter name="MCallerIDNumPres">
				</parameter>
				<parameter name="MCallerIDNameValid">
				</parameter>
				<parameter name="MCallerIDName">
				</parameter>
				<parameter name="MCallerIDNameCharSet">
				</parameter>
				<parameter name="MCallerIDNamePres">
				</parameter>
				<parameter name="MCallerIDSubaddr">
				</parameter>
				<parameter name="MCallerIDSubaddrType">
				</parameter>
				<parameter name="MCallerIDSubaddrOdd">
				</parameter>
				<parameter name="MCallerIDPres">
				</parameter>
				<parameter name="MConnectedIDNumValid">
				</parameter>
				<parameter name="MConnectedIDNum">
				</parameter>
				<parameter name="MConnectedIDton">
				</parameter>
				<parameter name="MConnectedIDNumPlan">
				</parameter>
				<parameter name="MConnectedIDNumPres">
				</parameter>
				<parameter name="MConnectedIDNameValid">
				</parameter>
				<parameter name="MConnectedIDName">
				</parameter>
				<parameter name="MConnectedIDNameCharSet">
				</parameter>
				<parameter name="MConnectedIDNamePres">
				</parameter>
				<parameter name="MConnectedIDSubaddr">
				</parameter>
				<parameter name="MConnectedIDSubaddrType">
				</parameter>
				<parameter name="MConnectedIDSubaddrOdd">
				</parameter>
				<parameter name="MConnectedIDPres">
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

#include "asterisk.h"

#ifdef HAVE_PRI

#include <errno.h>
#include <ctype.h>
#include <signal.h>

#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/say.h"
#include "asterisk/manager.h"
#include "asterisk/astdb.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/transcap.h"
#include "asterisk/features.h"
#include "asterisk/aoc.h"
#include "asterisk/bridge.h"
#include "asterisk/stasis_channels.h"

#include "sig_pri.h"
#ifndef PRI_EVENT_FACILITY
#error "Upgrade your libpri"
#endif

/*** DOCUMENTATION
 ***/


/* define this to send PRI user-user information elements */
#undef SUPPORT_USERUSER

/*!
 * Define to make always pick a channel if allowed.  Useful for
 * testing channel shifting.
 */
//#define ALWAYS_PICK_CHANNEL	1

#if defined(HAVE_PRI_CCSS)
struct sig_pri_cc_agent_prv {
	/*! Asterisk span D channel control structure. */
	struct sig_pri_span *pri;
	/*! CC id value to use with libpri. -1 if invalid. */
	long cc_id;
	/*! TRUE if CC has been requested and we are waiting for the response. */
	unsigned char cc_request_response_pending;
};

struct sig_pri_cc_monitor_instance {
	/*! \brief Asterisk span D channel control structure. */
	struct sig_pri_span *pri;
	/*! CC id value to use with libpri. (-1 if already canceled). */
	long cc_id;
	/*! CC core id value. */
	int core_id;
	/*! Device name(Channel name less sequence number) */
	char name[1];
};

/*! Upper level agent/monitor type name. */
static const char *sig_pri_cc_type_name;
/*! Container of sig_pri monitor instances. */
static struct ao2_container *sig_pri_cc_monitors;
#endif	/* defined(HAVE_PRI_CCSS) */

static int pri_matchdigittimeout = 3000;

static int pri_gendigittimeout = 8000;

#define DCHAN_NOTINALARM  (1 << 0)
#define DCHAN_UP          (1 << 1)

/* Defines to help decode the encoded event channel id. */
#define PRI_CHANNEL(p)	((p) & 0xff)
#define PRI_SPAN(p)		(((p) >> 8) & 0xff)
#define PRI_EXPLICIT	(1 << 16)
#define PRI_CIS_CALL	(1 << 17)	/* Call is using the D channel only. */
#define PRI_HELD_CALL	(1 << 18)


#define DCHAN_AVAILABLE	(DCHAN_NOTINALARM | DCHAN_UP)

static int pri_active_dchan_index(struct sig_pri_span *pri);

static const char *sig_pri_call_level2str(enum sig_pri_call_level level)
{
	switch (level) {
	case SIG_PRI_CALL_LEVEL_IDLE:
		return "Idle";
	case SIG_PRI_CALL_LEVEL_SETUP:
		return "Setup";
	case SIG_PRI_CALL_LEVEL_OVERLAP:
		return "Overlap";
	case SIG_PRI_CALL_LEVEL_PROCEEDING:
		return "Proceeding";
	case SIG_PRI_CALL_LEVEL_ALERTING:
		return "Alerting";
	case SIG_PRI_CALL_LEVEL_DEFER_DIAL:
		return "DeferDial";
	case SIG_PRI_CALL_LEVEL_CONNECT:
		return "Connect";
	}
	return "Unknown";
}

static inline void pri_rel(struct sig_pri_span *pri)
{
	ast_mutex_unlock(&pri->lock);
}

static unsigned int PVT_TO_CHANNEL(struct sig_pri_chan *p)
{
	int res = (((p)->prioffset) | ((p)->logicalspan << 8) | (p->mastertrunkgroup ? PRI_EXPLICIT : 0));
	ast_debug(5, "prioffset: %d mastertrunkgroup: %d logicalspan: %d result: %d\n",
		p->prioffset, p->mastertrunkgroup, p->logicalspan, res);

	return res;
}

static void sig_pri_handle_dchan_exception(struct sig_pri_span *pri, int index)
{
	if (sig_pri_callbacks.handle_dchan_exception) {
		sig_pri_callbacks.handle_dchan_exception(pri, index);
	}
}

static void sig_pri_set_dialing(struct sig_pri_chan *p, int is_dialing)
{
	if (sig_pri_callbacks.set_dialing) {
		sig_pri_callbacks.set_dialing(p->chan_pvt, is_dialing);
	}
}

static void sig_pri_set_digital(struct sig_pri_chan *p, int is_digital)
{
	p->digital = is_digital;
	if (sig_pri_callbacks.set_digital) {
		sig_pri_callbacks.set_digital(p->chan_pvt, is_digital);
	}
}

static void sig_pri_set_outgoing(struct sig_pri_chan *p, int is_outgoing)
{
	p->outgoing = is_outgoing;
	if (sig_pri_callbacks.set_outgoing) {
		sig_pri_callbacks.set_outgoing(p->chan_pvt, is_outgoing);
	}
}

void sig_pri_set_alarm(struct sig_pri_chan *p, int in_alarm)
{
	if (sig_pri_is_alarm_ignored(p->pri)) {
		/* Always set not in alarm */
		in_alarm = 0;
	}

	/*
	 * Clear the channel restart state when the channel alarm
	 * changes to prevent the state from getting stuck when the link
	 * goes down.
	 */
	p->resetting = SIG_PRI_RESET_IDLE;

	p->inalarm = in_alarm;
	if (sig_pri_callbacks.set_alarm) {
		sig_pri_callbacks.set_alarm(p->chan_pvt, in_alarm);
	}
}

static const char *sig_pri_get_orig_dialstring(struct sig_pri_chan *p)
{
	if (sig_pri_callbacks.get_orig_dialstring) {
		return sig_pri_callbacks.get_orig_dialstring(p->chan_pvt);
	}
	ast_log(LOG_ERROR, "get_orig_dialstring callback not defined\n");
	return "";
}

#if defined(HAVE_PRI_CCSS)
static void sig_pri_make_cc_dialstring(struct sig_pri_chan *p, char *buf, size_t buf_size)
{
	if (sig_pri_callbacks.make_cc_dialstring) {
		sig_pri_callbacks.make_cc_dialstring(p->chan_pvt, buf, buf_size);
	} else {
		ast_log(LOG_ERROR, "make_cc_dialstring callback not defined\n");
		buf[0] = '\0';
	}
}
#endif	/* defined(HAVE_PRI_CCSS) */

static void sig_pri_dial_digits(struct sig_pri_chan *p, const char *dial_string)
{
	if (sig_pri_callbacks.dial_digits) {
		sig_pri_callbacks.dial_digits(p->chan_pvt, dial_string);
	}
}

/*!
 * \internal
 * \brief Reevaluate the PRI span device state.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 *
 * \return Nothing
 *
 * \note Assumes the pri->lock is already obtained.
 */
static void sig_pri_span_devstate_changed(struct sig_pri_span *pri)
{
	if (sig_pri_callbacks.update_span_devstate) {
		sig_pri_callbacks.update_span_devstate(pri);
	}
}

/*!
 * \internal
 * \brief Set the caller id information in the parent module.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 *
 * \return Nothing
 */
static void sig_pri_set_caller_id(struct sig_pri_chan *p)
{
	struct ast_party_caller caller;

	if (sig_pri_callbacks.set_callerid) {
		ast_party_caller_init(&caller);

		caller.id.name.str = p->cid_name;
		caller.id.name.presentation = p->callingpres;
		caller.id.name.valid = 1;

		caller.id.number.str = p->cid_num;
		caller.id.number.plan = p->cid_ton;
		caller.id.number.presentation = p->callingpres;
		caller.id.number.valid = 1;

		if (!ast_strlen_zero(p->cid_subaddr)) {
			caller.id.subaddress.valid = 1;
			//caller.id.subaddress.type = 0;/* nsap */
			//caller.id.subaddress.odd_even_indicator = 0;
			caller.id.subaddress.str = p->cid_subaddr;
		}
		caller.id.tag = p->user_tag;

		caller.ani.number.str = p->cid_ani;
		//caller.ani.number.plan = p->xxx;
		//caller.ani.number.presentation = p->xxx;
		caller.ani.number.valid = 1;

		caller.ani2 = p->cid_ani2;
		sig_pri_callbacks.set_callerid(p->chan_pvt, &caller);
	}
}

/*!
 * \internal
 * \brief Set the Dialed Number Identifier.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 * \param dnid Dialed Number Identifier string.
 *
 * \return Nothing
 */
static void sig_pri_set_dnid(struct sig_pri_chan *p, const char *dnid)
{
	if (sig_pri_callbacks.set_dnid) {
		sig_pri_callbacks.set_dnid(p->chan_pvt, dnid);
	}
}

/*!
 * \internal
 * \brief Set the Redirecting Directory Number Information Service (RDNIS).
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 * \param rdnis Redirecting Directory Number Information Service (RDNIS) string.
 *
 * \return Nothing
 */
static void sig_pri_set_rdnis(struct sig_pri_chan *p, const char *rdnis)
{
	if (sig_pri_callbacks.set_rdnis) {
		sig_pri_callbacks.set_rdnis(p->chan_pvt, rdnis);
	}
}

static void sig_pri_unlock_private(struct sig_pri_chan *p)
{
	if (sig_pri_callbacks.unlock_private) {
		sig_pri_callbacks.unlock_private(p->chan_pvt);
	}
}

static void sig_pri_lock_private(struct sig_pri_chan *p)
{
	if (sig_pri_callbacks.lock_private) {
		sig_pri_callbacks.lock_private(p->chan_pvt);
	}
}

static void sig_pri_deadlock_avoidance_private(struct sig_pri_chan *p)
{
	if (sig_pri_callbacks.deadlock_avoidance_private) {
		sig_pri_callbacks.deadlock_avoidance_private(p->chan_pvt);
	} else {
		/* Fallback to the old way if callback not present. */
		sig_pri_unlock_private(p);
		sched_yield();
		sig_pri_lock_private(p);
	}
}

static void pri_grab(struct sig_pri_chan *p, struct sig_pri_span *pri)
{
	/* Grab the lock first */
	while (ast_mutex_trylock(&pri->lock)) {
		/* Avoid deadlock */
		sig_pri_deadlock_avoidance_private(p);
	}
	/* Then break the poll */
	if (pri->master != AST_PTHREADT_NULL) {
		pthread_kill(pri->master, SIGURG);
	}
}

/*!
 * \internal
 * \brief Convert PRI redirecting reason to asterisk version.
 * \since 1.8
 *
 * \param pri_reason PRI redirecting reason.
 *
 * \return Equivalent asterisk redirecting reason value.
 */
static enum AST_REDIRECTING_REASON pri_to_ast_reason(int pri_reason)
{
	enum AST_REDIRECTING_REASON ast_reason;

	switch (pri_reason) {
	case PRI_REDIR_FORWARD_ON_BUSY:
		ast_reason = AST_REDIRECTING_REASON_USER_BUSY;
		break;
	case PRI_REDIR_FORWARD_ON_NO_REPLY:
		ast_reason = AST_REDIRECTING_REASON_NO_ANSWER;
		break;
	case PRI_REDIR_DEFLECTION:
		ast_reason = AST_REDIRECTING_REASON_DEFLECTION;
		break;
	case PRI_REDIR_UNCONDITIONAL:
		ast_reason = AST_REDIRECTING_REASON_UNCONDITIONAL;
		break;
	case PRI_REDIR_UNKNOWN:
	default:
		ast_reason = AST_REDIRECTING_REASON_UNKNOWN;
		break;
	}

	return ast_reason;
}

/*!
 * \internal
 * \brief Convert asterisk redirecting reason to PRI version.
 * \since 1.8
 *
 * \param ast_reason Asterisk redirecting reason.
 *
 * \return Equivalent PRI redirecting reason value.
 */
static int ast_to_pri_reason(enum AST_REDIRECTING_REASON ast_reason)
{
	int pri_reason;

	switch (ast_reason) {
	case AST_REDIRECTING_REASON_USER_BUSY:
		pri_reason = PRI_REDIR_FORWARD_ON_BUSY;
		break;
	case AST_REDIRECTING_REASON_NO_ANSWER:
		pri_reason = PRI_REDIR_FORWARD_ON_NO_REPLY;
		break;
	case AST_REDIRECTING_REASON_UNCONDITIONAL:
		pri_reason = PRI_REDIR_UNCONDITIONAL;
		break;
	case AST_REDIRECTING_REASON_DEFLECTION:
		pri_reason = PRI_REDIR_DEFLECTION;
		break;
	case AST_REDIRECTING_REASON_UNKNOWN:
	default:
		pri_reason = PRI_REDIR_UNKNOWN;
		break;
	}

	return pri_reason;
}

/*!
 * \internal
 * \brief Convert PRI number presentation to asterisk version.
 * \since 1.8
 *
 * \param pri_presentation PRI number presentation.
 *
 * \return Equivalent asterisk number presentation value.
 */
static int pri_to_ast_presentation(int pri_presentation)
{
	int ast_presentation;

	switch (pri_presentation) {
	case PRI_PRES_ALLOWED | PRI_PRES_USER_NUMBER_UNSCREENED:
		ast_presentation = AST_PRES_ALLOWED | AST_PRES_USER_NUMBER_UNSCREENED;
		break;
	case PRI_PRES_ALLOWED | PRI_PRES_USER_NUMBER_PASSED_SCREEN:
		ast_presentation = AST_PRES_ALLOWED | AST_PRES_USER_NUMBER_PASSED_SCREEN;
		break;
	case PRI_PRES_ALLOWED | PRI_PRES_USER_NUMBER_FAILED_SCREEN:
		ast_presentation = AST_PRES_ALLOWED | AST_PRES_USER_NUMBER_FAILED_SCREEN;
		break;
	case PRI_PRES_ALLOWED | PRI_PRES_NETWORK_NUMBER:
		ast_presentation = AST_PRES_ALLOWED | AST_PRES_NETWORK_NUMBER;
		break;

	case PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_UNSCREENED:
		ast_presentation = AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_UNSCREENED;
		break;
	case PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_PASSED_SCREEN:
		ast_presentation = AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_PASSED_SCREEN;
		break;
	case PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_FAILED_SCREEN:
		ast_presentation = AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_FAILED_SCREEN;
		break;
	case PRI_PRES_RESTRICTED | PRI_PRES_NETWORK_NUMBER:
		ast_presentation = AST_PRES_RESTRICTED | AST_PRES_NETWORK_NUMBER;
		break;

	case PRI_PRES_UNAVAILABLE | PRI_PRES_USER_NUMBER_UNSCREENED:
	case PRI_PRES_UNAVAILABLE | PRI_PRES_USER_NUMBER_PASSED_SCREEN:
	case PRI_PRES_UNAVAILABLE | PRI_PRES_USER_NUMBER_FAILED_SCREEN:
	case PRI_PRES_UNAVAILABLE | PRI_PRES_NETWORK_NUMBER:
		ast_presentation = AST_PRES_NUMBER_NOT_AVAILABLE;
		break;

	default:
		ast_presentation = AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_UNSCREENED;
		break;
	}

	return ast_presentation;
}

/*!
 * \internal
 * \brief Convert asterisk number presentation to PRI version.
 * \since 1.8
 *
 * \param ast_presentation Asterisk number presentation.
 *
 * \return Equivalent PRI number presentation value.
 */
static int ast_to_pri_presentation(int ast_presentation)
{
	int pri_presentation;

	switch (ast_presentation) {
	case AST_PRES_ALLOWED | AST_PRES_USER_NUMBER_UNSCREENED:
		pri_presentation = PRI_PRES_ALLOWED | PRI_PRES_USER_NUMBER_UNSCREENED;
		break;
	case AST_PRES_ALLOWED | AST_PRES_USER_NUMBER_PASSED_SCREEN:
		pri_presentation = PRI_PRES_ALLOWED | PRI_PRES_USER_NUMBER_PASSED_SCREEN;
		break;
	case AST_PRES_ALLOWED | AST_PRES_USER_NUMBER_FAILED_SCREEN:
		pri_presentation = PRI_PRES_ALLOWED | PRI_PRES_USER_NUMBER_FAILED_SCREEN;
		break;
	case AST_PRES_ALLOWED | AST_PRES_NETWORK_NUMBER:
		pri_presentation = PRI_PRES_ALLOWED | PRI_PRES_NETWORK_NUMBER;
		break;

	case AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_UNSCREENED:
		pri_presentation = PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_UNSCREENED;
		break;
	case AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_PASSED_SCREEN:
		pri_presentation = PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_PASSED_SCREEN;
		break;
	case AST_PRES_RESTRICTED | AST_PRES_USER_NUMBER_FAILED_SCREEN:
		pri_presentation = PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_FAILED_SCREEN;
		break;
	case AST_PRES_RESTRICTED | AST_PRES_NETWORK_NUMBER:
		pri_presentation = PRI_PRES_RESTRICTED | PRI_PRES_NETWORK_NUMBER;
		break;

	case AST_PRES_UNAVAILABLE | AST_PRES_USER_NUMBER_UNSCREENED:
	case AST_PRES_UNAVAILABLE | AST_PRES_USER_NUMBER_PASSED_SCREEN:
	case AST_PRES_UNAVAILABLE | AST_PRES_USER_NUMBER_FAILED_SCREEN:
	case AST_PRES_UNAVAILABLE | AST_PRES_NETWORK_NUMBER:
		pri_presentation = PRES_NUMBER_NOT_AVAILABLE;
		break;

	default:
		pri_presentation = PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_UNSCREENED;
		break;
	}

	return pri_presentation;
}

/*!
 * \internal
 * \brief Convert PRI name char_set to asterisk version.
 * \since 1.8
 *
 * \param pri_char_set PRI name char_set.
 *
 * \return Equivalent asterisk name char_set value.
 */
static enum AST_PARTY_CHAR_SET pri_to_ast_char_set(int pri_char_set)
{
	enum AST_PARTY_CHAR_SET ast_char_set;

	switch (pri_char_set) {
	default:
	case PRI_CHAR_SET_UNKNOWN:
		ast_char_set = AST_PARTY_CHAR_SET_UNKNOWN;
		break;
	case PRI_CHAR_SET_ISO8859_1:
		ast_char_set = AST_PARTY_CHAR_SET_ISO8859_1;
		break;
	case PRI_CHAR_SET_WITHDRAWN:
		ast_char_set = AST_PARTY_CHAR_SET_WITHDRAWN;
		break;
	case PRI_CHAR_SET_ISO8859_2:
		ast_char_set = AST_PARTY_CHAR_SET_ISO8859_2;
		break;
	case PRI_CHAR_SET_ISO8859_3:
		ast_char_set = AST_PARTY_CHAR_SET_ISO8859_3;
		break;
	case PRI_CHAR_SET_ISO8859_4:
		ast_char_set = AST_PARTY_CHAR_SET_ISO8859_4;
		break;
	case PRI_CHAR_SET_ISO8859_5:
		ast_char_set = AST_PARTY_CHAR_SET_ISO8859_5;
		break;
	case PRI_CHAR_SET_ISO8859_7:
		ast_char_set = AST_PARTY_CHAR_SET_ISO8859_7;
		break;
	case PRI_CHAR_SET_ISO10646_BMPSTRING:
		ast_char_set = AST_PARTY_CHAR_SET_ISO10646_BMPSTRING;
		break;
	case PRI_CHAR_SET_ISO10646_UTF_8STRING:
		ast_char_set = AST_PARTY_CHAR_SET_ISO10646_UTF_8STRING;
		break;
	}

	return ast_char_set;
}

/*!
 * \internal
 * \brief Convert asterisk name char_set to PRI version.
 * \since 1.8
 *
 * \param ast_char_set Asterisk name char_set.
 *
 * \return Equivalent PRI name char_set value.
 */
static int ast_to_pri_char_set(enum AST_PARTY_CHAR_SET ast_char_set)
{
	int pri_char_set;

	switch (ast_char_set) {
	default:
	case AST_PARTY_CHAR_SET_UNKNOWN:
		pri_char_set = PRI_CHAR_SET_UNKNOWN;
		break;
	case AST_PARTY_CHAR_SET_ISO8859_1:
		pri_char_set = PRI_CHAR_SET_ISO8859_1;
		break;
	case AST_PARTY_CHAR_SET_WITHDRAWN:
		pri_char_set = PRI_CHAR_SET_WITHDRAWN;
		break;
	case AST_PARTY_CHAR_SET_ISO8859_2:
		pri_char_set = PRI_CHAR_SET_ISO8859_2;
		break;
	case AST_PARTY_CHAR_SET_ISO8859_3:
		pri_char_set = PRI_CHAR_SET_ISO8859_3;
		break;
	case AST_PARTY_CHAR_SET_ISO8859_4:
		pri_char_set = PRI_CHAR_SET_ISO8859_4;
		break;
	case AST_PARTY_CHAR_SET_ISO8859_5:
		pri_char_set = PRI_CHAR_SET_ISO8859_5;
		break;
	case AST_PARTY_CHAR_SET_ISO8859_7:
		pri_char_set = PRI_CHAR_SET_ISO8859_7;
		break;
	case AST_PARTY_CHAR_SET_ISO10646_BMPSTRING:
		pri_char_set = PRI_CHAR_SET_ISO10646_BMPSTRING;
		break;
	case AST_PARTY_CHAR_SET_ISO10646_UTF_8STRING:
		pri_char_set = PRI_CHAR_SET_ISO10646_UTF_8STRING;
		break;
	}

	return pri_char_set;
}

#if defined(HAVE_PRI_SUBADDR)
/*!
 * \internal
 * \brief Fill in the asterisk party subaddress from the given PRI party subaddress.
 * \since 1.8
 *
 * \param ast_subaddress Asterisk party subaddress structure.
 * \param pri_subaddress PRI party subaddress structure.
 *
 * \return Nothing
 *
 */
static void sig_pri_set_subaddress(struct ast_party_subaddress *ast_subaddress, const struct pri_party_subaddress *pri_subaddress)
{
	ast_free(ast_subaddress->str);
	if (pri_subaddress->length <= 0) {
		ast_party_subaddress_init(ast_subaddress);
		return;
	}

	if (!pri_subaddress->type) {
		/* NSAP */
		ast_subaddress->str = ast_strdup((char *) pri_subaddress->data);
	} else {
		char *cnum;
		char *ptr;
		int x;
		int len;

		/* User Specified */
		cnum = ast_malloc(2 * pri_subaddress->length + 1);
		if (!cnum) {
			ast_party_subaddress_init(ast_subaddress);
			return;
		}

		ptr = cnum;
		len = pri_subaddress->length - 1; /* -1 account for zero based indexing */
		for (x = 0; x < len; ++x) {
			ptr += sprintf(ptr, "%02hhx", (unsigned char)pri_subaddress->data[x]);
		}

		if (pri_subaddress->odd_even_indicator) {
			/* ODD */
			sprintf(ptr, "%01hhx", (unsigned char)((pri_subaddress->data[len]) >> 4));
		} else {
			/* EVEN */
			sprintf(ptr, "%02hhx", (unsigned char)pri_subaddress->data[len]);
		}
		ast_subaddress->str = cnum;
	}
	ast_subaddress->type = pri_subaddress->type;
	ast_subaddress->odd_even_indicator = pri_subaddress->odd_even_indicator;
	ast_subaddress->valid = 1;
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

#if defined(HAVE_PRI_SUBADDR)
static unsigned char ast_pri_pack_hex_char(char c)
{
	unsigned char res;

	if (c < '0') {
		res = 0;
	} else if (c < ('9' + 1)) {
		res = c - '0';
	} else if (c < 'A') {
		res = 0;
	} else if (c < ('F' + 1)) {
		res = c - 'A' + 10;
	} else if (c < 'a') {
		res = 0;
	} else if (c < ('f' + 1)) {
		res = c - 'a' + 10;
	} else {
		res = 0;
	}
	return res;
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

#if defined(HAVE_PRI_SUBADDR)
/*!
 * \internal
 * \brief Convert a null terminated hexadecimal string to a packed hex byte array.
 * \details left justified, with 0 padding if odd length.
 * \since 1.8
 *
 * \param dst pointer to packed byte array.
 * \param src pointer to null terminated hexadecimal string.
 * \param maxlen destination array size.
 *
 * \return Length of byte array
 *
 * \note The dst is not an ASCIIz string.
 * \note The src is an ASCIIz hex string.
 */
static int ast_pri_pack_hex_string(unsigned char *dst, char *src, int maxlen)
{
	int res = 0;
	int len = strlen(src);

	if (len > (2 * maxlen)) {
		len = 2 * maxlen;
	}

	res = len / 2 + len % 2;

	while (len > 1) {
		*dst = ast_pri_pack_hex_char(*src) << 4;
		src++;
		*dst |= ast_pri_pack_hex_char(*src);
		dst++, src++;
		len -= 2;
	}
	if (len) { /* 1 left */
		*dst = ast_pri_pack_hex_char(*src) << 4;
	}
	return res;
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

#if defined(HAVE_PRI_SUBADDR)
/*!
 * \internal
 * \brief Fill in the PRI party subaddress from the given asterisk party subaddress.
 * \since 1.8
 *
 * \param pri_subaddress PRI party subaddress structure.
 * \param ast_subaddress Asterisk party subaddress structure.
 *
 * \return Nothing
 *
 * \note Assumes that pri_subaddress has been previously memset to zero.
 */
static void sig_pri_party_subaddress_from_ast(struct pri_party_subaddress *pri_subaddress, const struct ast_party_subaddress *ast_subaddress)
{
	if (ast_subaddress->valid && !ast_strlen_zero(ast_subaddress->str)) {
		pri_subaddress->type = ast_subaddress->type;
		if (!ast_subaddress->type) {
			/* 0 = NSAP */
			ast_copy_string((char *) pri_subaddress->data, ast_subaddress->str,
				sizeof(pri_subaddress->data));
			pri_subaddress->length = strlen((char *) pri_subaddress->data);
			pri_subaddress->odd_even_indicator = 0;
			pri_subaddress->valid = 1;
		} else {
			/* 2 = User Specified */
			/*
			 * Copy HexString to packed HexData,
			 * if odd length then right pad trailing byte with 0
			 */
			int length = ast_pri_pack_hex_string(pri_subaddress->data,
				ast_subaddress->str, sizeof(pri_subaddress->data));

			pri_subaddress->length = length; /* packed data length */

			length = strlen(ast_subaddress->str);
			if (length > 2 * sizeof(pri_subaddress->data)) {
				pri_subaddress->odd_even_indicator = 0;
			} else {
				pri_subaddress->odd_even_indicator = (length & 1);
			}
			pri_subaddress->valid = 1;
		}
	}
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

/*!
 * \internal
 * \brief Fill in the PRI party name from the given asterisk party name.
 * \since 1.8
 *
 * \param pri_name PRI party name structure.
 * \param ast_name Asterisk party name structure.
 *
 * \return Nothing
 *
 * \note Assumes that pri_name has been previously memset to zero.
 */
static void sig_pri_party_name_from_ast(struct pri_party_name *pri_name, const struct ast_party_name *ast_name)
{
	if (!ast_name->valid) {
		return;
	}
	pri_name->valid = 1;
	pri_name->presentation = ast_to_pri_presentation(ast_name->presentation);
	pri_name->char_set = ast_to_pri_char_set(ast_name->char_set);
	if (!ast_strlen_zero(ast_name->str)) {
		ast_copy_string(pri_name->str, ast_name->str, sizeof(pri_name->str));
	}
}

/*!
 * \internal
 * \brief Fill in the PRI party number from the given asterisk party number.
 * \since 1.8
 *
 * \param pri_number PRI party number structure.
 * \param ast_number Asterisk party number structure.
 *
 * \return Nothing
 *
 * \note Assumes that pri_number has been previously memset to zero.
 */
static void sig_pri_party_number_from_ast(struct pri_party_number *pri_number, const struct ast_party_number *ast_number)
{
	if (!ast_number->valid) {
		return;
	}
	pri_number->valid = 1;
	pri_number->presentation = ast_to_pri_presentation(ast_number->presentation);
	pri_number->plan = ast_number->plan;
	if (!ast_strlen_zero(ast_number->str)) {
		ast_copy_string(pri_number->str, ast_number->str, sizeof(pri_number->str));
	}
}

/*!
 * \internal
 * \brief Fill in the PRI party id from the given asterisk party id.
 * \since 1.8
 *
 * \param pri_id PRI party id structure.
 * \param ast_id Asterisk party id structure.
 *
 * \return Nothing
 *
 * \note Assumes that pri_id has been previously memset to zero.
 */
static void sig_pri_party_id_from_ast(struct pri_party_id *pri_id, const struct ast_party_id *ast_id)
{
	sig_pri_party_name_from_ast(&pri_id->name, &ast_id->name);
	sig_pri_party_number_from_ast(&pri_id->number, &ast_id->number);
#if defined(HAVE_PRI_SUBADDR)
	sig_pri_party_subaddress_from_ast(&pri_id->subaddress, &ast_id->subaddress);
#endif	/* defined(HAVE_PRI_SUBADDR) */
}

/*!
 * \internal
 * \brief Update the PRI redirecting information for the current call.
 * \since 1.8
 *
 * \param pvt sig_pri private channel structure.
 * \param ast Asterisk channel
 *
 * \return Nothing
 *
 * \note Assumes that the PRI lock is already obtained.
 */
static void sig_pri_redirecting_update(struct sig_pri_chan *pvt, struct ast_channel *ast)
{
	struct pri_party_redirecting pri_redirecting;
	const struct ast_party_redirecting *ast_redirecting;
	struct ast_party_id redirecting_from = ast_channel_redirecting_effective_from(ast);
	struct ast_party_id redirecting_to = ast_channel_redirecting_effective_to(ast);
	struct ast_party_id redirecting_orig = ast_channel_redirecting_effective_orig(ast);

	memset(&pri_redirecting, 0, sizeof(pri_redirecting));
	ast_redirecting = ast_channel_redirecting(ast);
	sig_pri_party_id_from_ast(&pri_redirecting.from, &redirecting_from);
	sig_pri_party_id_from_ast(&pri_redirecting.to, &redirecting_to);
	sig_pri_party_id_from_ast(&pri_redirecting.orig_called, &redirecting_orig);
	pri_redirecting.count = ast_redirecting->count;
	pri_redirecting.orig_reason = ast_to_pri_reason(ast_redirecting->orig_reason.code);
	pri_redirecting.reason = ast_to_pri_reason(ast_redirecting->reason.code);

	pri_redirecting_update(pvt->pri->pri, pvt->call, &pri_redirecting);
}

/*!
 * \internal
 * \brief Reset DTMF detector.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 *
 * \return Nothing
 */
static void sig_pri_dsp_reset_and_flush_digits(struct sig_pri_chan *p)
{
	if (sig_pri_callbacks.dsp_reset_and_flush_digits) {
		sig_pri_callbacks.dsp_reset_and_flush_digits(p->chan_pvt);
	}
}

static int sig_pri_set_echocanceller(struct sig_pri_chan *p, int enable)
{
	if (sig_pri_callbacks.set_echocanceller) {
		return sig_pri_callbacks.set_echocanceller(p->chan_pvt, enable);
	} else {
		return -1;
	}
}

static void sig_pri_fixup_chans(struct sig_pri_chan *old_chan, struct sig_pri_chan *new_chan)
{
	if (sig_pri_callbacks.fixup_chans) {
		sig_pri_callbacks.fixup_chans(old_chan->chan_pvt, new_chan->chan_pvt);
	}
}

static int sig_pri_play_tone(struct sig_pri_chan *p, enum sig_pri_tone tone)
{
	if (sig_pri_callbacks.play_tone) {
		return sig_pri_callbacks.play_tone(p->chan_pvt, tone);
	} else {
		return -1;
	}
}

static struct ast_channel *sig_pri_new_ast_channel(struct sig_pri_chan *p, int state,
	enum sig_pri_law law, int transfercapability, char *exten,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *c;

	if (sig_pri_callbacks.new_ast_channel) {
		c = sig_pri_callbacks.new_ast_channel(p->chan_pvt, state, law, exten, assignedids, requestor);
	} else {
		return NULL;
	}
	if (!c) {
		return NULL;
	}

	ast_assert(p->owner == NULL || p->owner == c);
	p->owner = c;
	p->isidlecall = 0;
	p->alreadyhungup = 0;
	ast_channel_transfercapability_set(c, transfercapability);
	pbx_builtin_setvar_helper(c, "TRANSFERCAPABILITY",
		ast_transfercapability2str(transfercapability));
	if (transfercapability & AST_TRANS_CAP_DIGITAL) {
		sig_pri_set_digital(p, 1);
	}
	if (p->pri) {
		ast_mutex_lock(&p->pri->lock);
		sig_pri_span_devstate_changed(p->pri);
		ast_mutex_unlock(&p->pri->lock);
	}

	return c;
}

/*!
 * \internal
 * \brief Open the PRI channel media path.
 * \since 1.8
 *
 * \param p Channel private control structure.
 *
 * \return Nothing
 */
static void sig_pri_open_media(struct sig_pri_chan *p)
{
	if (p->no_b_channel) {
		return;
	}

	if (sig_pri_callbacks.open_media) {
		sig_pri_callbacks.open_media(p->chan_pvt);
	}
}

/*!
 * \internal
 * \brief Post an AMI B channel association event.
 * \since 1.8
 *
 * \param p Channel private control structure.
 *
 * \note Assumes the private and owner are locked.
 *
 * \return Nothing
 */
static void sig_pri_ami_channel_event(struct sig_pri_chan *p)
{
	if (sig_pri_callbacks.ami_channel_event) {
		sig_pri_callbacks.ami_channel_event(p->chan_pvt, p->owner);
	}
}

struct ast_channel *sig_pri_request(struct sig_pri_chan *p, enum sig_pri_law law,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor,
	int transfercapability)
{
	struct ast_channel *ast;

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);

	sig_pri_set_outgoing(p, 1);
	ast = sig_pri_new_ast_channel(p, AST_STATE_RESERVED, law, transfercapability,
		p->exten, assignedids, requestor);
	if (!ast) {
		sig_pri_set_outgoing(p, 0);
	}
	return ast;
}

int pri_is_up(struct sig_pri_span *pri)
{
	int x;
	for (x = 0; x < SIG_PRI_NUM_DCHANS; x++) {
		if (pri->dchanavail[x] == DCHAN_AVAILABLE)
			return 1;
	}
	return 0;
}

static const char *pri_order(int level)
{
	switch (level) {
	case 0:
		return "Primary";
	case 1:
		return "Secondary";
	case 2:
		return "Tertiary";
	case 3:
		return "Quaternary";
	default:
		return "<Unknown>";
	}
}

/* Returns index of the active dchan */
static int pri_active_dchan_index(struct sig_pri_span *pri)
{
	int x;

	for (x = 0; x < SIG_PRI_NUM_DCHANS; x++) {
		if ((pri->dchans[x] == pri->pri))
			return x;
	}

	ast_log(LOG_WARNING, "No active dchan found!\n");
	return -1;
}

static void pri_find_dchan(struct sig_pri_span *pri)
{
	struct pri *old;
	int oldslot = -1;
	int newslot = -1;
	int idx;

	old = pri->pri;
	for (idx = 0; idx < SIG_PRI_NUM_DCHANS; ++idx) {
		if (!pri->dchans[idx]) {
			/* No more D channels defined on the span. */
			break;
		}
		if (pri->dchans[idx] == old) {
			oldslot = idx;
		}
		if (newslot < 0 && pri->dchanavail[idx] == DCHAN_AVAILABLE) {
			newslot = idx;
		}
	}
	/* At this point, idx is a count of how many D-channels are defined on the span. */

	if (1 < idx) {
		/* We have several D-channels defined on the span.  (NFAS PRI setup) */
		if (newslot < 0) {
			/* No D-channels available.  Default to the primary D-channel. */
			newslot = 0;

			if (!pri->no_d_channels) {
				pri->no_d_channels = 1;
				if (old && oldslot != newslot) {
					ast_log(LOG_WARNING,
						"Span %d: No D-channels up!  Switching selected D-channel from %s to %s.\n",
						pri->span, pri_order(oldslot), pri_order(newslot));
				} else {
					ast_log(LOG_WARNING, "Span %d: No D-channels up!\n", pri->span);
				}
			}
		} else {
			pri->no_d_channels = 0;
		}
		if (old && oldslot != newslot) {
			ast_log(LOG_NOTICE,
				"Switching selected D-channel from %s (fd %d) to %s (fd %d)!\n",
				pri_order(oldslot), pri->fds[oldslot],
				pri_order(newslot), pri->fds[newslot]);
		}
	} else {
		if (newslot < 0) {
			/* The only D-channel is not up. */
			newslot = 0;

			if (!pri->no_d_channels) {
				pri->no_d_channels = 1;

				/*
				 * This is annoying to see on non-persistent layer 2
				 * connections.  Let's not complain in that case.
				 */
				if (pri->sig != SIG_BRI_PTMP) {
					ast_log(LOG_WARNING, "Span %d: D-channel is down!\n", pri->span);
				}
			}
		} else {
			pri->no_d_channels = 0;
		}
	}
	pri->pri = pri->dchans[newslot];
}

/*!
 * \internal
 * \brief Determine if a private channel structure is in use.
 * \since 1.8
 *
 * \param pvt Channel to determine if in use.
 *
 * \return TRUE if the channel is in use.
 */
static int sig_pri_is_chan_in_use(struct sig_pri_chan *pvt)
{
	return pvt->owner || pvt->call || pvt->allocated || pvt->inalarm
		|| pvt->resetting != SIG_PRI_RESET_IDLE;
}

/*!
 * \brief Determine if a private channel structure is available.
 * \since 1.8
 *
 * \param pvt Channel to determine if available.
 *
 * \return TRUE if the channel is available.
 */
int sig_pri_is_chan_available(struct sig_pri_chan *pvt)
{
	return !sig_pri_is_chan_in_use(pvt)
#if defined(HAVE_PRI_SERVICE_MESSAGES)
		/* And not out-of-service */
		&& !pvt->service_status
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
		;
}

/*!
 * \internal
 * \brief Obtain the sig_pri owner channel lock if the owner exists.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_lock_owner(struct sig_pri_span *pri, int chanpos)
{
	for (;;) {
		if (!pri->pvts[chanpos]->owner) {
			/* There is no owner lock to get. */
			break;
		}
		if (!ast_channel_trylock(pri->pvts[chanpos]->owner)) {
			/* We got the lock */
			break;
		}

		/* Avoid deadlock */
		sig_pri_unlock_private(pri->pvts[chanpos]);
		DEADLOCK_AVOIDANCE(&pri->lock);
		sig_pri_lock_private(pri->pvts[chanpos]);
	}
}

/*!
 * \internal
 * \brief Queue the given frame onto the owner channel.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param frame Frame to queue onto the owner channel.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void pri_queue_frame(struct sig_pri_span *pri, int chanpos, struct ast_frame *frame)
{
	sig_pri_lock_owner(pri, chanpos);
	if (pri->pvts[chanpos]->owner) {
		ast_queue_frame(pri->pvts[chanpos]->owner, frame);
		ast_channel_unlock(pri->pvts[chanpos]->owner);
	}
}

/*!
 * \internal
 * \brief Queue a hold frame onto the owner channel.
 * \since 12
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_queue_hold(struct sig_pri_span *pri, int chanpos)
{
	sig_pri_lock_owner(pri, chanpos);
	if (pri->pvts[chanpos]->owner) {
		ast_queue_hold(pri->pvts[chanpos]->owner, NULL);
		ast_channel_unlock(pri->pvts[chanpos]->owner);
	}
}

/*!
 * \internal
 * \brief Queue an unhold frame onto the owner channel.
 * \since 12
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_queue_unhold(struct sig_pri_span *pri, int chanpos)
{
	sig_pri_lock_owner(pri, chanpos);
	if (pri->pvts[chanpos]->owner) {
		ast_queue_unhold(pri->pvts[chanpos]->owner);
		ast_channel_unlock(pri->pvts[chanpos]->owner);
	}
}

/*!
 * \internal
 * \brief Queue a control frame of the specified subclass onto the owner channel.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param subclass Control frame subclass to queue onto the owner channel.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void pri_queue_control(struct sig_pri_span *pri, int chanpos, int subclass)
{
	struct ast_frame f = {AST_FRAME_CONTROL, };

	if (sig_pri_callbacks.queue_control) {
		sig_pri_callbacks.queue_control(pri->pvts[chanpos]->chan_pvt, subclass);
	}

	f.subclass.integer = subclass;
	pri_queue_frame(pri, chanpos, &f);
}

/*!
 * \internal
 * \brief Queue a request to hangup control frame onto the owner channel.
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_queue_hangup(struct sig_pri_span *pri, int chanpos)
{
	if (sig_pri_callbacks.queue_control) {
		sig_pri_callbacks.queue_control(pri->pvts[chanpos]->chan_pvt, AST_CONTROL_HANGUP);
	}

	sig_pri_lock_owner(pri, chanpos);
	if (pri->pvts[chanpos]->owner) {
		ast_queue_hangup(pri->pvts[chanpos]->owner);
		ast_channel_unlock(pri->pvts[chanpos]->owner);
	}
}

/*!
 * \internal
 * \brief Queue a PVT_CAUSE_CODE frame onto the owner channel.
 * \since 11
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param cause String describing the cause to be placed into the frame.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void pri_queue_pvt_cause_data(struct sig_pri_span *pri, int chanpos, const char *cause, int ast_cause)
{
	struct ast_channel *chan;
	struct ast_control_pvt_cause_code *cause_code;

	sig_pri_lock_owner(pri, chanpos);
	chan = pri->pvts[chanpos]->owner;
	if (chan) {
		int datalen = sizeof(*cause_code) + strlen(cause);
		cause_code = ast_alloca(datalen);
		memset(cause_code, 0, datalen);
		cause_code->ast_cause = ast_cause;
		ast_copy_string(cause_code->chan_name, ast_channel_name(chan), AST_CHANNEL_NAME);
		ast_copy_string(cause_code->code, cause, datalen + 1 - sizeof(*cause_code));
		ast_queue_control_data(chan, AST_CONTROL_PVT_CAUSE_CODE, cause_code, datalen);
		ast_channel_hangupcause_hash_set(chan, cause_code, datalen);
		ast_channel_unlock(chan);
	}
}

/*!
 * \internal
 * \brief Find the channel associated with the libpri call.
 * \since 10.0
 *
 * \param pri PRI span control structure.
 * \param call LibPRI opaque call pointer to find.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int pri_find_principle_by_call(struct sig_pri_span *pri, q931_call *call)
{
	int idx;

	if (!call) {
		/* Cannot find a call without a call. */
		return -1;
	}
	for (idx = 0; idx < pri->numchans; ++idx) {
		if (pri->pvts[idx] && pri->pvts[idx]->call == call) {
			/* Found the principle */
			return idx;
		}
	}
	return -1;
}

/*!
 * \internal
 * \brief Queue the span for destruction
 * \since 13.0
 *
 * \param pri PRI span control structure.
 *
 * Asks the channel driver to queue the span for destruction at a
 * possibly later time, if (e.g.) locking considerations don't allow
 * destroying it right now.
 *
 * \return Nothing
 */
static void pri_destroy_later(struct sig_pri_span *pri)
{
	if (!sig_pri_callbacks.destroy_later) {
		return;
	}
	sig_pri_callbacks.destroy_later(pri);
}

/*!
 * \internal
 * \brief Kill the call.
 * \since 10.0
 *
 * \param pri PRI span control structure.
 * \param call LibPRI opaque call pointer to find.
 * \param cause Reason call was killed.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_kill_call(struct sig_pri_span *pri, q931_call *call, int cause)
{
	int chanpos;

	chanpos = pri_find_principle_by_call(pri, call);
	if (chanpos < 0) {
		pri_hangup(pri->pri, call, cause);
		return;
	}
	sig_pri_lock_private(pri->pvts[chanpos]);
	if (!pri->pvts[chanpos]->owner) {
		pri_hangup(pri->pri, call, cause);
		pri->pvts[chanpos]->call = NULL;
		sig_pri_unlock_private(pri->pvts[chanpos]);
		sig_pri_span_devstate_changed(pri);
		return;
	}
	ast_channel_hangupcause_set(pri->pvts[chanpos]->owner, cause);
	pri_queue_control(pri, chanpos, AST_CONTROL_HANGUP);
	sig_pri_unlock_private(pri->pvts[chanpos]);
}

/*!
 * \internal
 * \brief Find the private structure for the libpri call.
 *
 * \param pri PRI span control structure.
 * \param channel LibPRI encoded channel ID.
 * \param call LibPRI opaque call pointer.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int pri_find_principle(struct sig_pri_span *pri, int channel, q931_call *call)
{
	int x;
	int span;
	int principle;
	int prioffset;

	if (channel < 0) {
		/* Channel is not picked yet. */
		return -1;
	}

	prioffset = PRI_CHANNEL(channel);
	if (!prioffset || (channel & PRI_HELD_CALL)) {
		/* Find the call waiting call or held call. */
		return pri_find_principle_by_call(pri, call);
	}

	span = PRI_SPAN(channel);
	if (!(channel & PRI_EXPLICIT)) {
		int index;

		index = pri_active_dchan_index(pri);
		if (index == -1) {
			return -1;
		}
		span = pri->dchan_logical_span[index];
	}

	principle = -1;
	for (x = 0; x < pri->numchans; x++) {
		if (pri->pvts[x]
			&& pri->pvts[x]->prioffset == prioffset
			&& pri->pvts[x]->logicalspan == span
			&& !pri->pvts[x]->no_b_channel) {
			principle = x;
			break;
		}
	}

	return principle;
}

/*!
 * \internal
 * \brief Fixup the private structure associated with the libpri call.
 *
 * \param pri PRI span control structure.
 * \param principle Array-index into private array to move call to if not already there.
 * \param call LibPRI opaque call pointer to find if need to move call.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval principle on success.
 * \retval -1 on error.
 */
static int pri_fixup_principle(struct sig_pri_span *pri, int principle, q931_call *call)
{
	int x;

	if (principle < 0 || pri->numchans <= principle) {
		/* Out of rannge */
		return -1;
	}
	if (!call) {
		/* No call */
		return principle;
	}
	if (pri->pvts[principle] && pri->pvts[principle]->call == call) {
		/* Call is already on the specified principle. */
		return principle;
	}

	/* Find the old principle location. */
	for (x = 0; x < pri->numchans; x++) {
		struct sig_pri_chan *new_chan;
		struct sig_pri_chan *old_chan;

		if (!pri->pvts[x] || pri->pvts[x]->call != call) {
			continue;
		}

		/* Found our call */
		new_chan = pri->pvts[principle];
		old_chan = pri->pvts[x];

		/* Get locks to safely move to the new private structure. */
		sig_pri_lock_private(old_chan);
		sig_pri_lock_owner(pri, x);
		sig_pri_lock_private(new_chan);

		ast_verb(3, "Moving call (%s) from channel %d to %d.\n",
			old_chan->owner ? ast_channel_name(old_chan->owner) : "",
			old_chan->channel, new_chan->channel);
		if (!sig_pri_is_chan_available(new_chan)) {
			ast_log(LOG_WARNING,
				"Can't move call (%s) from channel %d to %d.  It is already in use.\n",
				old_chan->owner ? ast_channel_name(old_chan->owner) : "",
				old_chan->channel, new_chan->channel);
			sig_pri_unlock_private(new_chan);
			if (old_chan->owner) {
				ast_channel_unlock(old_chan->owner);
			}
			sig_pri_unlock_private(old_chan);
			return -1;
		}

		sig_pri_fixup_chans(old_chan, new_chan);

		/* Fix it all up now */
		new_chan->owner = old_chan->owner;
		old_chan->owner = NULL;

		new_chan->call = old_chan->call;
		old_chan->call = NULL;

		/* Transfer flags from the old channel. */
#if defined(HAVE_PRI_AOC_EVENTS)
		new_chan->aoc_s_request_invoke_id_valid = old_chan->aoc_s_request_invoke_id_valid;
		new_chan->waiting_for_aoce = old_chan->waiting_for_aoce;
		new_chan->holding_aoce = old_chan->holding_aoce;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
		new_chan->alreadyhungup = old_chan->alreadyhungup;
		new_chan->isidlecall = old_chan->isidlecall;
		new_chan->progress = old_chan->progress;
		new_chan->allocated = old_chan->allocated;
		new_chan->outgoing = old_chan->outgoing;
		new_chan->digital = old_chan->digital;
#if defined(HAVE_PRI_CALL_WAITING)
		new_chan->is_call_waiting = old_chan->is_call_waiting;
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
#if defined(HAVE_PRI_SETUP_ACK_INBAND)
		new_chan->no_dialed_digits = old_chan->no_dialed_digits;
#endif	/* defined(HAVE_PRI_SETUP_ACK_INBAND) */

#if defined(HAVE_PRI_AOC_EVENTS)
		old_chan->aoc_s_request_invoke_id_valid = 0;
		old_chan->waiting_for_aoce = 0;
		old_chan->holding_aoce = 0;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
		old_chan->alreadyhungup = 0;
		old_chan->isidlecall = 0;
		old_chan->progress = 0;
		old_chan->allocated = 0;
		old_chan->outgoing = 0;
		old_chan->digital = 0;
#if defined(HAVE_PRI_CALL_WAITING)
		old_chan->is_call_waiting = 0;
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
#if defined(HAVE_PRI_SETUP_ACK_INBAND)
		old_chan->no_dialed_digits = 0;
#endif	/* defined(HAVE_PRI_SETUP_ACK_INBAND) */

		/* More stuff to transfer to the new channel. */
		new_chan->call_level = old_chan->call_level;
		old_chan->call_level = SIG_PRI_CALL_LEVEL_IDLE;
#if defined(HAVE_PRI_REVERSE_CHARGE)
		new_chan->reverse_charging_indication = old_chan->reverse_charging_indication;
#endif	/* defined(HAVE_PRI_REVERSE_CHARGE) */
#if defined(HAVE_PRI_SETUP_KEYPAD)
		strcpy(new_chan->keypad_digits, old_chan->keypad_digits);
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */
		strcpy(new_chan->deferred_digits, old_chan->deferred_digits);
		strcpy(new_chan->moh_suggested, old_chan->moh_suggested);
		new_chan->moh_state = old_chan->moh_state;
		old_chan->moh_state = SIG_PRI_MOH_STATE_IDLE;
#if defined(HAVE_PRI_TRANSFER)
		new_chan->xfer_data = old_chan->xfer_data;
		old_chan->xfer_data = NULL;
#endif	/* defined(HAVE_PRI_TRANSFER) */

#if defined(HAVE_PRI_AOC_EVENTS)
		new_chan->aoc_s_request_invoke_id = old_chan->aoc_s_request_invoke_id;
		new_chan->aoc_e = old_chan->aoc_e;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
		strcpy(new_chan->user_tag, old_chan->user_tag);

		if (new_chan->no_b_channel) {
			/* Copy the real channel configuration to the no B channel interface. */
			new_chan->hidecallerid = old_chan->hidecallerid;
			new_chan->hidecalleridname = old_chan->hidecalleridname;
			new_chan->immediate = old_chan->immediate;
			new_chan->priexclusive = old_chan->priexclusive;
			new_chan->priindication_oob = old_chan->priindication_oob;
			new_chan->use_callerid = old_chan->use_callerid;
			new_chan->use_callingpres = old_chan->use_callingpres;
			new_chan->stripmsd = old_chan->stripmsd;
			strcpy(new_chan->context, old_chan->context);
			strcpy(new_chan->mohinterpret, old_chan->mohinterpret);

			/* Become a member of the old channel span/trunk-group. */
			new_chan->logicalspan = old_chan->logicalspan;
			new_chan->mastertrunkgroup = old_chan->mastertrunkgroup;
		} else if (old_chan->no_b_channel) {
			/*
			 * We are transitioning from a held/call-waiting channel to a
			 * real channel so we need to make sure that the media path is
			 * open.  (Needed especially if the channel is natively
			 * bridged.)
			 */
			sig_pri_open_media(new_chan);
		}

		if (new_chan->owner) {
			sig_pri_ami_channel_event(new_chan);
		}

		sig_pri_unlock_private(old_chan);
		if (new_chan->owner) {
			ast_channel_unlock(new_chan->owner);
		}
		sig_pri_unlock_private(new_chan);

		return principle;
	}
	ast_verb(3, "Call specified, but not found.\n");
	return -1;
}

/*!
 * \internal
 * \brief Find and fixup the private structure associated with the libpri call.
 *
 * \param pri PRI span control structure.
 * \param channel LibPRI encoded channel ID.
 * \param call LibPRI opaque call pointer.
 *
 * \details
 * This is a combination of pri_find_principle() and pri_fixup_principle()
 * to reduce code redundancy and to make handling several PRI_EVENT_xxx's
 * consistent for the current architecture.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int pri_find_fixup_principle(struct sig_pri_span *pri, int channel, q931_call *call)
{
	int chanpos;

	chanpos = pri_find_principle(pri, channel, call);
	if (chanpos < 0) {
		ast_log(LOG_WARNING, "Span %d: PRI requested channel %d/%d is unconfigured.\n",
			pri->span, PRI_SPAN(channel), PRI_CHANNEL(channel));
		sig_pri_kill_call(pri, call, PRI_CAUSE_IDENTIFIED_CHANNEL_NOTEXIST);
		return -1;
	}
	chanpos = pri_fixup_principle(pri, chanpos, call);
	if (chanpos < 0) {
		ast_log(LOG_WARNING, "Span %d: PRI requested channel %d/%d is not available.\n",
			pri->span, PRI_SPAN(channel), PRI_CHANNEL(channel));
		/*
		 * Using Q.931 section 5.2.3.1 b) as the reason for picking
		 * PRI_CAUSE_CHANNEL_UNACCEPTABLE.  Receiving a
		 * PRI_CAUSE_REQUESTED_CHAN_UNAVAIL would cause us to restart
		 * that channel (which is not specified by Q.931) and kill some
		 * other call which would be bad.
		 */
		sig_pri_kill_call(pri, call, PRI_CAUSE_CHANNEL_UNACCEPTABLE);
		return -1;
	}
	return chanpos;
}

static char * redirectingreason2str(int redirectingreason)
{
	switch (redirectingreason) {
	case 0:
		return "UNKNOWN";
	case 1:
		return "BUSY";
	case 2:
		return "NO_REPLY";
	case 0xF:
		return "UNCONDITIONAL";
	default:
		return "NOREDIRECT";
	}
}

static char *dialplan2str(int dialplan)
{
	if (dialplan == -1) {
		return("Dynamically set dialplan in ISDN");
	}
	return (pri_plan2str(dialplan));
}

/*!
 * \internal
 * \brief Apply numbering plan prefix to the given number.
 *
 * \param buf Buffer to put number into.
 * \param size Size of given buffer.
 * \param pri PRI span control structure.
 * \param number Number to apply numbering plan.
 * \param plan Numbering plan to apply.
 *
 * \return Nothing
 */
static void apply_plan_to_number(char *buf, size_t size, const struct sig_pri_span *pri, const char *number, int plan)
{
	switch (plan) {
	case PRI_INTERNATIONAL_ISDN:		/* Q.931 dialplan == 0x11 international dialplan => prepend international prefix digits */
		snprintf(buf, size, "%s%s", pri->internationalprefix, number);
		break;
	case PRI_NATIONAL_ISDN:			/* Q.931 dialplan == 0x21 national dialplan => prepend national prefix digits */
		snprintf(buf, size, "%s%s", pri->nationalprefix, number);
		break;
	case PRI_LOCAL_ISDN:			/* Q.931 dialplan == 0x41 local dialplan => prepend local prefix digits */
		snprintf(buf, size, "%s%s", pri->localprefix, number);
		break;
	case PRI_PRIVATE:			/* Q.931 dialplan == 0x49 private dialplan => prepend private prefix digits */
		snprintf(buf, size, "%s%s", pri->privateprefix, number);
		break;
	case PRI_UNKNOWN:			/* Q.931 dialplan == 0x00 unknown dialplan => prepend unknown prefix digits */
		snprintf(buf, size, "%s%s", pri->unknownprefix, number);
		break;
	default:				/* other Q.931 dialplan => don't twiddle with callingnum */
		snprintf(buf, size, "%s", number);
		break;
	}
}

/*!
 * \internal
 * \brief Apply numbering plan prefix to the given number if the number exists.
 *
 * \param buf Buffer to put number into.
 * \param size Size of given buffer.
 * \param pri PRI span control structure.
 * \param number Number to apply numbering plan.
 * \param plan Numbering plan to apply.
 *
 * \return Nothing
 */
static void apply_plan_to_existing_number(char *buf, size_t size, const struct sig_pri_span *pri, const char *number, int plan)
{
	/* Make sure a number exists so the prefix isn't placed on an empty string. */
	if (ast_strlen_zero(number)) {
		if (size) {
			*buf = '\0';
		}
		return;
	}
	apply_plan_to_number(buf, size, pri, number, plan);
}

/*!
 * \internal
 * \brief Restart the next channel we think is idle on the span.
 *
 * \param pri PRI span control structure.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void pri_check_restart(struct sig_pri_span *pri)
{
#if defined(HAVE_PRI_SERVICE_MESSAGES)
	unsigned why;
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */

	for (++pri->resetpos; pri->resetpos < pri->numchans; ++pri->resetpos) {
		if (!pri->pvts[pri->resetpos]
			|| pri->pvts[pri->resetpos]->no_b_channel
			|| sig_pri_is_chan_in_use(pri->pvts[pri->resetpos])) {
			continue;
		}
#if defined(HAVE_PRI_SERVICE_MESSAGES)
		why = pri->pvts[pri->resetpos]->service_status;
		if (why) {
			ast_log(LOG_NOTICE,
				"Span %d: channel %d out-of-service (reason: %s), not sending RESTART\n",
				pri->span, pri->pvts[pri->resetpos]->channel,
				(why & SRVST_FAREND) ? (why & SRVST_NEAREND) ? "both ends" : "far end" : "near end");
			continue;
		}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
		break;
	}
	if (pri->resetpos < pri->numchans) {
		/* Mark the channel as resetting and restart it */
		pri->pvts[pri->resetpos]->resetting = SIG_PRI_RESET_ACTIVE;
		pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[pri->resetpos]));
	} else {
		pri->resetting = 0;
		time(&pri->lastreset);
		sig_pri_span_devstate_changed(pri);
	}
}

#if defined(HAVE_PRI_CALL_WAITING)
/*!
 * \internal
 * \brief Init the private channel configuration using the span controller.
 * \since 1.8
 *
 * \param pvt Channel to init the configuration.
 * \param pri PRI span control structure.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_init_config(struct sig_pri_chan *pvt, struct sig_pri_span *pri)
{
	pvt->stripmsd = pri->ch_cfg.stripmsd;
	pvt->hidecallerid = pri->ch_cfg.hidecallerid;
	pvt->hidecalleridname = pri->ch_cfg.hidecalleridname;
	pvt->immediate = pri->ch_cfg.immediate;
	pvt->priexclusive = pri->ch_cfg.priexclusive;
	pvt->priindication_oob = pri->ch_cfg.priindication_oob;
	pvt->use_callerid = pri->ch_cfg.use_callerid;
	pvt->use_callingpres = pri->ch_cfg.use_callingpres;
	ast_copy_string(pvt->context, pri->ch_cfg.context, sizeof(pvt->context));
	ast_copy_string(pvt->mohinterpret, pri->ch_cfg.mohinterpret, sizeof(pvt->mohinterpret));

	if (sig_pri_callbacks.init_config) {
		sig_pri_callbacks.init_config(pvt->chan_pvt, pri);
	}
}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */

/*!
 * \internal
 * \brief Find an empty B-channel interface to use.
 *
 * \param pri PRI span control structure.
 * \param backwards TRUE if the search starts from higher channels.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int pri_find_empty_chan(struct sig_pri_span *pri, int backwards)
{
	int x;
	if (backwards)
		x = pri->numchans;
	else
		x = 0;
	for (;;) {
		if (backwards && (x < 0))
			break;
		if (!backwards && (x >= pri->numchans))
			break;
		if (pri->pvts[x]
			&& !pri->pvts[x]->no_b_channel
			&& sig_pri_is_chan_available(pri->pvts[x])) {
			ast_debug(1, "Found empty available channel %d/%d\n",
				pri->pvts[x]->logicalspan, pri->pvts[x]->prioffset);
			return x;
		}
		if (backwards)
			x--;
		else
			x++;
	}
	return -1;
}

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Find or create an empty no-B-channel interface to use.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int pri_find_empty_nobch(struct sig_pri_span *pri)
{
	int idx;

	for (idx = 0; idx < pri->numchans; ++idx) {
		if (pri->pvts[idx]
			&& pri->pvts[idx]->no_b_channel
			&& sig_pri_is_chan_available(pri->pvts[idx])) {
			ast_debug(1, "Found empty available no B channel interface\n");
			return idx;
		}
	}

	/* Need to create a new interface. */
	if (sig_pri_callbacks.new_nobch_intf) {
		idx = sig_pri_callbacks.new_nobch_intf(pri);
	} else {
		idx = -1;
	}
	return idx;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

static void *do_idle_thread(void *v_pvt)
{
	struct sig_pri_chan *pvt = v_pvt;
	struct ast_channel *chan = pvt->owner;
	struct ast_frame *f;
	char ex[80];
	/* Wait up to 30 seconds for an answer */
	int timeout_ms = 30000;
	int ms;
	struct timeval start;
	struct ast_callid *callid;

	if ((callid = ast_channel_callid(chan))) {
		ast_callid_threadassoc_add(callid);
		callid = ast_callid_unref(callid);
	}

	ast_verb(3, "Initiating idle call on channel %s\n", ast_channel_name(chan));
	snprintf(ex, sizeof(ex), "%d/%s", pvt->channel, pvt->pri->idledial);
	if (ast_call(chan, ex, 0)) {
		ast_log(LOG_WARNING, "Idle dial failed on '%s' to '%s'\n", ast_channel_name(chan), ex);
		ast_hangup(chan);
		return NULL;
	}
	start = ast_tvnow();
	while ((ms = ast_remaining_ms(start, timeout_ms))) {
		if (ast_waitfor(chan, ms) <= 0) {
			break;
		}

		f = ast_read(chan);
		if (!f) {
			/* Got hangup */
			break;
		}
		if (f->frametype == AST_FRAME_CONTROL) {
			switch (f->subclass.integer) {
			case AST_CONTROL_ANSWER:
				/* Launch the PBX */
				ast_channel_exten_set(chan, pvt->pri->idleext);
				ast_channel_context_set(chan, pvt->pri->idlecontext);
				ast_channel_priority_set(chan, 1);
				ast_verb(4, "Idle channel '%s' answered, sending to %s@%s\n", ast_channel_name(chan), ast_channel_exten(chan), ast_channel_context(chan));
				ast_pbx_run(chan);
				/* It's already hungup, return immediately */
				return NULL;
			case AST_CONTROL_BUSY:
				ast_verb(4, "Idle channel '%s' busy, waiting...\n", ast_channel_name(chan));
				break;
			case AST_CONTROL_CONGESTION:
				ast_verb(4, "Idle channel '%s' congested, waiting...\n", ast_channel_name(chan));
				break;
			};
		}
		ast_frfree(f);
	}
	/* Hangup the channel since nothing happend */
	ast_hangup(chan);
	return NULL;
}

static void *pri_ss_thread(void *data)
{
	struct sig_pri_chan *p = data;
	struct ast_channel *chan = p->owner;
	char exten[AST_MAX_EXTENSION];
	int res;
	int len;
	int timeout;
	struct ast_callid *callid;

	if (!chan) {
		/* We lost the owner before we could get started. */
		return NULL;
	}

	if ((callid = ast_channel_callid(chan))) {
		ast_callid_threadassoc_add(callid);
		ast_callid_unref(callid);
	}

	/*
	 * In the bizarre case where the channel has become a zombie before we
	 * even get started here, abort safely.
	 */
	if (!ast_channel_tech_pvt(chan)) {
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", ast_channel_name(chan));
		ast_hangup(chan);
		return NULL;
	}

	ast_verb(3, "Starting simple switch on '%s'\n", ast_channel_name(chan));

	sig_pri_dsp_reset_and_flush_digits(p);

	/* Now loop looking for an extension */
	ast_copy_string(exten, p->exten, sizeof(exten));
	len = strlen(exten);
	res = 0;
	while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num)) {
		if (len && !ast_ignore_pattern(ast_channel_context(chan), exten))
			sig_pri_play_tone(p, -1);
		else
			sig_pri_play_tone(p, SIG_PRI_TONE_DIALTONE);
		if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num))
			timeout = pri_matchdigittimeout;
		else
			timeout = pri_gendigittimeout;
		res = ast_waitfordigit(chan, timeout);
		if (res < 0) {
			ast_debug(1, "waitfordigit returned < 0...\n");
			ast_hangup(chan);
			return NULL;
		} else if (res) {
			exten[len++] = res;
			exten[len] = '\0';
		} else
			break;
	}
	/* if no extension was received ('unspecified') on overlap call, use the 's' extension */
	if (ast_strlen_zero(exten)) {
		ast_verb(3, "Going to extension s|1 because of empty extension received on overlap call\n");
		exten[0] = 's';
		exten[1] = '\0';
	} else {
		ast_free(ast_channel_dialed(chan)->number.str);
		ast_channel_dialed(chan)->number.str = ast_strdup(exten);

		if (p->pri->append_msn_to_user_tag && p->pri->nodetype != PRI_NETWORK) {
			/*
			 * Update the user tag for party id's from this device for this call
			 * now that we have a complete MSN from the network.
			 */
			snprintf(p->user_tag, sizeof(p->user_tag), "%s_%s", p->pri->initial_user_tag,
				exten);
			ast_free(ast_channel_caller(chan)->id.tag);
			ast_channel_caller(chan)->id.tag = ast_strdup(p->user_tag);
		}
	}
	sig_pri_play_tone(p, -1);
	if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num)) {
		/* Start the real PBX */
		ast_channel_exten_set(chan, exten);
		sig_pri_dsp_reset_and_flush_digits(p);
#if defined(JIRA_ASTERISK_15594)
		/*
		 * Conditionaled out this code to effectively revert the JIRA
		 * ASTERISK-15594 change.  It breaks overlap dialing through
		 * Asterisk.  There is not enough information available at this
		 * point to know if dialing is complete.  The
		 * ast_exists_extension(), ast_matchmore_extension(), and
		 * ast_canmatch_extension() calls are not adequate to detect a
		 * dial through extension pattern of "_9!".
		 *
		 * Workaround is to use the dialplan Proceeding() application
		 * early on non-dial through extensions.
		 */
		if ((p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
			&& !ast_matchmore_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num)) {
			sig_pri_lock_private(p);
			if (p->pri->pri) {
				pri_grab(p, p->pri);
				if (p->call_level < SIG_PRI_CALL_LEVEL_PROCEEDING) {
					p->call_level = SIG_PRI_CALL_LEVEL_PROCEEDING;
				}
				pri_proceeding(p->pri->pri, p->call, PVT_TO_CHANNEL(p), 0);
				pri_rel(p->pri);
			}
			sig_pri_unlock_private(p);
		}
#endif	/* defined(JIRA_ASTERISK_15594) */

		sig_pri_set_echocanceller(p, 1);
		ast_channel_lock(chan);
		ast_setstate(chan, AST_STATE_RING);
		ast_channel_unlock(chan);
		res = ast_pbx_run(chan);
		if (res) {
			ast_log(LOG_WARNING, "PBX exited non-zero!\n");
		}
	} else {
		ast_debug(1, "No such possible extension '%s' in context '%s'\n", exten, ast_channel_context(chan));
		ast_channel_hangupcause_set(chan, AST_CAUSE_UNALLOCATED);
		ast_hangup(chan);
		p->exten[0] = '\0';
		/* Since we send release complete here, we won't get one */
		p->call = NULL;
		ast_mutex_lock(&p->pri->lock);
		sig_pri_span_devstate_changed(p->pri);
		ast_mutex_unlock(&p->pri->lock);
	}
	return NULL;
}

void pri_event_alarm(struct sig_pri_span *pri, int index, int before_start_pri)
{
	pri->dchanavail[index] &= ~(DCHAN_NOTINALARM | DCHAN_UP);
	if (!before_start_pri) {
		pri_find_dchan(pri);
	}
}

void pri_event_noalarm(struct sig_pri_span *pri, int index, int before_start_pri)
{
	pri->dchanavail[index] |= DCHAN_NOTINALARM;
	if (!before_start_pri)
		pri_restart(pri->dchans[index]);
}

/*!
 * \internal
 * \brief Convert libpri party name into asterisk party name.
 * \since 1.8
 *
 * \param ast_name Asterisk party name structure to fill.  Must already be set initialized.
 * \param pri_name libpri party name structure containing source information.
 *
 * \note The filled in ast_name structure needs to be destroyed by
 * ast_party_name_free() when it is no longer needed.
 *
 * \return Nothing
 */
static void sig_pri_party_name_convert(struct ast_party_name *ast_name, const struct pri_party_name *pri_name)
{
	ast_name->str = ast_strdup(pri_name->str);
	ast_name->char_set = pri_to_ast_char_set(pri_name->char_set);
	ast_name->presentation = pri_to_ast_presentation(pri_name->presentation);
	ast_name->valid = 1;
}

/*!
 * \internal
 * \brief Convert libpri party number into asterisk party number.
 * \since 1.8
 *
 * \param ast_number Asterisk party number structure to fill.  Must already be set initialized.
 * \param pri_number libpri party number structure containing source information.
 * \param pri PRI span control structure.
 *
 * \note The filled in ast_number structure needs to be destroyed by
 * ast_party_number_free() when it is no longer needed.
 *
 * \return Nothing
 */
static void sig_pri_party_number_convert(struct ast_party_number *ast_number, const struct pri_party_number *pri_number, struct sig_pri_span *pri)
{
	char number[AST_MAX_EXTENSION];

	apply_plan_to_existing_number(number, sizeof(number), pri, pri_number->str,
		pri_number->plan);
	ast_number->str = ast_strdup(number);
	ast_number->plan = pri_number->plan;
	ast_number->presentation = pri_to_ast_presentation(pri_number->presentation);
	ast_number->valid = 1;
}

/*!
 * \internal
 * \brief Convert libpri party id into asterisk party id.
 * \since 1.8
 *
 * \param ast_id Asterisk party id structure to fill.  Must already be set initialized.
 * \param pri_id libpri party id structure containing source information.
 * \param pri PRI span control structure.
 *
 * \note The filled in ast_id structure needs to be destroyed by
 * ast_party_id_free() when it is no longer needed.
 *
 * \return Nothing
 */
static void sig_pri_party_id_convert(struct ast_party_id *ast_id, const struct pri_party_id *pri_id, struct sig_pri_span *pri)
{
	if (pri_id->name.valid) {
		sig_pri_party_name_convert(&ast_id->name, &pri_id->name);
	}
	if (pri_id->number.valid) {
		sig_pri_party_number_convert(&ast_id->number, &pri_id->number, pri);
	}
#if defined(HAVE_PRI_SUBADDR)
	if (pri_id->subaddress.valid) {
		sig_pri_set_subaddress(&ast_id->subaddress, &pri_id->subaddress);
	}
#endif	/* defined(HAVE_PRI_SUBADDR) */
}

/*!
 * \internal
 * \brief Convert libpri redirecting information into asterisk redirecting information.
 * \since 1.8
 *
 * \param ast_redirecting Asterisk redirecting structure to fill.
 * \param pri_redirecting libpri redirecting structure containing source information.
 * \param ast_guide Asterisk redirecting structure to use as an initialization guide.
 * \param pri PRI span control structure.
 *
 * \note The filled in ast_redirecting structure needs to be destroyed by
 * ast_party_redirecting_free() when it is no longer needed.
 *
 * \return Nothing
 */
static void sig_pri_redirecting_convert(struct ast_party_redirecting *ast_redirecting,
	const struct pri_party_redirecting *pri_redirecting,
	const struct ast_party_redirecting *ast_guide,
	struct sig_pri_span *pri)
{
	ast_party_redirecting_set_init(ast_redirecting, ast_guide);

	sig_pri_party_id_convert(&ast_redirecting->orig, &pri_redirecting->orig_called, pri);
	sig_pri_party_id_convert(&ast_redirecting->from, &pri_redirecting->from, pri);
	sig_pri_party_id_convert(&ast_redirecting->to, &pri_redirecting->to, pri);
	ast_redirecting->count = pri_redirecting->count;
	ast_redirecting->reason.code = pri_to_ast_reason(pri_redirecting->reason);
	ast_redirecting->orig_reason.code = pri_to_ast_reason(pri_redirecting->orig_reason);
}

/*!
 * \internal
 * \brief Determine if the given extension matches one of the MSNs in the pattern list.
 * \since 1.8
 *
 * \param msn_patterns Comma separated list of MSN patterns to match.
 * \param exten Extension to match in the MSN list.
 *
 * \retval 1 if matches.
 * \retval 0 if no match.
 */
static int sig_pri_msn_match(const char *msn_patterns, const char *exten)
{
	char *pattern;
	char *msn_list;
	char *list_tail;

	msn_list = ast_strdupa(msn_patterns);

	list_tail = NULL;
	pattern = strtok_r(msn_list, ",", &list_tail);
	while (pattern) {
		pattern = ast_strip(pattern);
		if (!ast_strlen_zero(pattern) && ast_extension_match(pattern, exten)) {
			/* Extension matched the pattern. */
			return 1;
		}
		pattern = strtok_r(NULL, ",", &list_tail);
	}
	/* Did not match any pattern in the list. */
	return 0;
}

#if defined(HAVE_PRI_MCID)
static void party_number_json_to_ami(struct ast_str **msg, const char *prefix, struct ast_json *number)
{
	const char *num_txt, *pres_txt;
	int plan, pres;
	if (!number) {
		ast_str_append(msg, 0,
			"%sNumValid: 0\r\n"
			"%sNum: \r\n"
			"%ston: 0\r\n",
			prefix, prefix, prefix);
		return;
	}

	num_txt = ast_json_string_get(ast_json_object_get(number, "number"));
	plan = ast_json_integer_get(ast_json_object_get(number, "plan"));
	pres = ast_json_integer_get(ast_json_object_get(number, "presentation"));
	pres_txt = ast_json_string_get(ast_json_object_get(number, "presentation_txt"));

	ast_str_append(msg, 0, "%sNumValid: 1\r\n", prefix);
	ast_str_append(msg, 0, "%sNum: %s\r\n", prefix, num_txt);
	ast_str_append(msg, 0, "%ston: %d\r\n", prefix, plan);
	ast_str_append(msg, 0, "%sNumPlan: %d\r\n", prefix, plan);
	ast_str_append(msg, 0, "%sNumPres: %d (%s)\r\n", prefix, pres, pres_txt);
}

static void party_name_json_to_ami(struct ast_str **msg, const char *prefix, struct ast_json *name)
{
	const char *name_txt, *pres_txt, *charset;
	int pres;
	if (!name) {
		ast_str_append(msg, 0,
			"%sNameValid: 0\r\n"
			"%sName: \r\n",
			prefix, prefix);
		return;
	}

	name_txt = ast_json_string_get(ast_json_object_get(name, "name"));
	charset = ast_json_string_get(ast_json_object_get(name, "character_set"));
	pres = ast_json_integer_get(ast_json_object_get(name, "presentation"));
	pres_txt = ast_json_string_get(ast_json_object_get(name, "presentation_txt"));

	ast_str_append(msg, 0, "%sNameValid: 1\r\n", prefix);
	ast_str_append(msg, 0, "%sName: %s\r\n", prefix, name_txt);
	ast_str_append(msg, 0, "%sNameCharSet: %s\r\n", prefix, charset);
	ast_str_append(msg, 0, "%sNamePres: %d (%s)\r\n", prefix, pres, pres_txt);
}

static void party_subaddress_json_to_ami(struct ast_str **msg, const char *prefix, struct ast_json *subaddress)
{
	const char *subaddress_txt, *type_txt;
	int odd;
	if (!subaddress) {
		return;
	}

	subaddress_txt = ast_json_string_get(ast_json_object_get(subaddress, "subaddress"));
	type_txt = ast_json_string_get(ast_json_object_get(subaddress, "type"));
	odd = ast_json_is_true(ast_json_object_get(subaddress, "odd")) ? 1 : 0;

	ast_str_append(msg, 0, "%sSubaddr: %s\r\n", prefix, subaddress_txt);
	ast_str_append(msg, 0, "%sSubaddrType: %s\r\n", prefix, type_txt);
	ast_str_append(msg, 0, "%sSubaddrOdd: %d\r\n", prefix, odd);
}

/*!
 * \internal
 * \brief Append the given JSON party id to the event string.
 * \since 1.8
 *
 * \param msg Event message string being built.
 * \param prefix Prefix to add to the party id lines.
 * \param party Party information to encode.
 *
 * \return Nothing
 */
static void party_json_to_ami(struct ast_str **msg, const char *prefix, struct ast_json *party)
{
	struct ast_json *presentation = ast_json_object_get(party, "presentation");
	struct ast_json *presentation_txt = ast_json_object_get(party, "presentation_txt");
	struct ast_json *name = ast_json_object_get(party, "name");
	struct ast_json *number = ast_json_object_get(party, "number");
	struct ast_json *subaddress = ast_json_object_get(party, "subaddress");

	/* Combined party presentation */
	ast_str_append(msg, 0, "%sPres: %jd (%s)\r\n", prefix,
		ast_json_integer_get(presentation),
		ast_json_string_get(presentation_txt));

	/* Party number */
	party_number_json_to_ami(msg, prefix, number);

	/* Party name */
	party_name_json_to_ami(msg, prefix, name);

	/* Party subaddress */
	party_subaddress_json_to_ami(msg, prefix, subaddress);
}

static struct ast_manager_event_blob *mcid_to_ami(struct stasis_message *msg)
{
	RAII_VAR(struct ast_str *, channel_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, party_string, ast_str_create(256), ast_free);
	struct ast_channel_blob *obj = stasis_message_data(msg);

	if (obj->snapshot) {
		channel_string = ast_manager_build_channel_state_string(obj->snapshot);
		if (!channel_string) {
			return NULL;
		}
	}

	party_json_to_ami(&party_string, "MCallerID", ast_json_object_get(obj->blob, "caller"));
	party_json_to_ami(&party_string, "MConnectedID", ast_json_object_get(obj->blob, "connected"));

	return ast_manager_event_blob_create(EVENT_FLAG_CALL, "MCID",
		"%s"
		"%s",
		S_COR(obj->snapshot, ast_str_buffer(channel_string), ""), ast_str_buffer(party_string));
}

STASIS_MESSAGE_TYPE_DEFN_LOCAL(mcid_type,
	.to_ami = mcid_to_ami,
	);

static void send_mcid(struct ast_channel *chan, struct ast_party_id *caller, struct ast_party_id *connected)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	ast_assert(caller != NULL);
	ast_assert(connected != NULL);

	blob = ast_json_pack("{s: o, s: o}",
		"caller", ast_json_party_id(caller),
		"connected", ast_json_party_id(connected));
	if (!blob) {
		return;
	}

	ast_channel_publish_blob(chan, mcid_type(), blob);
}

/*!
 * \internal
 * \brief Handle the MCID event.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param mcid MCID event parameters.
 * \param owner Asterisk channel associated with the call.
 * NULL if Asterisk no longer has the ast_channel struct.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the owner channel lock is already obtained if still present.
 *
 * \return Nothing
 */
static void sig_pri_mcid_event(struct sig_pri_span *pri, const struct pri_subcmd_mcid_req *mcid, struct ast_channel *owner)
{
	struct ast_party_id caller_party;
	struct ast_party_id connected_party;

	/* Always use libpri's called party information. */
	ast_party_id_init(&connected_party);
	sig_pri_party_id_convert(&connected_party, &mcid->answerer, pri);
	if (owner) {
		/*
		 * The owner channel is present.
		 * Pass the event to the peer as well.
		 */
		ast_queue_control(owner, AST_CONTROL_MCID);

		send_mcid(owner, &ast_channel_connected(owner)->id, &connected_party);
	} else {
		/*
		 * Since we no longer have an owner channel,
		 * we have to use the caller information supplied by libpri.
		 */
		ast_party_id_init(&caller_party);
		sig_pri_party_id_convert(&caller_party, &mcid->originator, pri);
		send_mcid(owner, &caller_party, &connected_party);
		ast_party_id_free(&caller_party);
	}
	ast_party_id_free(&connected_party);
}
#endif	/* defined(HAVE_PRI_MCID) */

#if defined(HAVE_PRI_TRANSFER)
struct xfer_rsp_data {
	struct sig_pri_span *pri;
	/*! Call to send transfer success/fail response over. */
	q931_call *call;
	/*! Invocation ID to use when sending a reply to the transfer request. */
	int invoke_id;
	/*! TRUE if the transfer response has been made. */
	int responded;
};
#endif	/* defined(HAVE_PRI_TRANSFER) */

#if defined(HAVE_PRI_TRANSFER)
/*!
 * \internal
 * \brief Send the transfer success/fail response message.
 * \since 1.8
 *
 * \param rsp Transfer response data.
 * \param is_successful TRUE if the transfer was successful.
 *
 * \note Assumes the rsp->pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_transfer_rsp(struct xfer_rsp_data *rsp, int is_successful)
{
	if (rsp->responded) {
		return;
	}
	rsp->responded = 1;

	pri_transfer_rsp(rsp->pri->pri, rsp->call, rsp->invoke_id, is_successful);
}
#endif	/* defined(HAVE_PRI_TRANSFER) */

#if defined(HAVE_PRI_CALL_HOLD) || defined(HAVE_PRI_TRANSFER)
/*!
 * \internal
 * \brief Attempt to transfer the two calls to each other.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param call_1_pri First call involved in the transfer. (transferee; usually on hold)
 * \param call_1_held TRUE if call_1_pri is on hold.
 * \param call_2_pri Second call involved in the transfer. (target; usually active/ringing)
 * \param call_2_held TRUE if call_2_pri is on hold.
 * \param xfer_data Transfer response data if non-NULL.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int sig_pri_attempt_transfer(struct sig_pri_span *pri, q931_call *call_1_pri, int call_1_held, q931_call *call_2_pri, int call_2_held, struct xfer_rsp_data *xfer_data)
{
	struct attempt_xfer_call {
		q931_call *pri;
		struct ast_channel *ast;
		int held;
		int chanpos;
	};
	int retval;
	enum ast_transfer_result xfer_res;
	struct attempt_xfer_call *call_1;
	struct attempt_xfer_call *call_2;
	struct attempt_xfer_call c1;
	struct attempt_xfer_call c2;

	c1.pri = call_1_pri;
	c1.held = call_1_held;
	call_1 = &c1;

	c2.pri = call_2_pri;
	c2.held = call_2_held;
	call_2 = &c2;

	call_1->chanpos = pri_find_principle_by_call(pri, call_1->pri);
	call_2->chanpos = pri_find_principle_by_call(pri, call_2->pri);
	if (call_1->chanpos < 0 || call_2->chanpos < 0) {
		/* Calls not found in span control. */
#if defined(HAVE_PRI_TRANSFER)
		if (xfer_data) {
			/* Transfer failed. */
			sig_pri_transfer_rsp(xfer_data, 0);
		}
#endif	/* defined(HAVE_PRI_TRANSFER) */
		return -1;
	}

	/* Get call_1 owner. */
	sig_pri_lock_private(pri->pvts[call_1->chanpos]);
	sig_pri_lock_owner(pri, call_1->chanpos);
	call_1->ast = pri->pvts[call_1->chanpos]->owner;
	if (call_1->ast) {
		ast_channel_ref(call_1->ast);
		ast_channel_unlock(call_1->ast);
	}
	sig_pri_unlock_private(pri->pvts[call_1->chanpos]);

	/* Get call_2 owner. */
	sig_pri_lock_private(pri->pvts[call_2->chanpos]);
	sig_pri_lock_owner(pri, call_2->chanpos);
	call_2->ast = pri->pvts[call_2->chanpos]->owner;
	if (call_2->ast) {
		ast_channel_ref(call_2->ast);
		ast_channel_unlock(call_2->ast);
	}
	sig_pri_unlock_private(pri->pvts[call_2->chanpos]);

	if (!call_1->ast || !call_2->ast) {
		/* At least one owner is not present. */
		if (call_1->ast) {
			ast_channel_unref(call_1->ast);
		}
		if (call_2->ast) {
			ast_channel_unref(call_2->ast);
		}
#if defined(HAVE_PRI_TRANSFER)
		if (xfer_data) {
			/* Transfer failed. */
			sig_pri_transfer_rsp(xfer_data, 0);
		}
#endif	/* defined(HAVE_PRI_TRANSFER) */
		return -1;
	}

	ast_verb(3, "TRANSFERRING %s to %s\n",
		ast_channel_name(call_1->ast), ast_channel_name(call_2->ast));

#if defined(HAVE_PRI_TRANSFER)
	if (xfer_data) {
		/*
		 * Add traps on the transferer channels in case threading causes
		 * them to hangup before ast_bridge_transfer_attended() returns
		 * and we can get the pri->lock back.
		 */
		sig_pri_lock_private(pri->pvts[call_1->chanpos]);
		pri->pvts[call_1->chanpos]->xfer_data = xfer_data;
		sig_pri_unlock_private(pri->pvts[call_1->chanpos]);
		sig_pri_lock_private(pri->pvts[call_2->chanpos]);
		pri->pvts[call_2->chanpos]->xfer_data = xfer_data;
		sig_pri_unlock_private(pri->pvts[call_2->chanpos]);
	}
#endif	/* defined(HAVE_PRI_TRANSFER) */

	ast_mutex_unlock(&pri->lock);
	xfer_res = ast_bridge_transfer_attended(call_1->ast, call_2->ast);
	ast_mutex_lock(&pri->lock);
	retval = (xfer_res != AST_BRIDGE_TRANSFER_SUCCESS) ? -1 : 0;

#if defined(HAVE_PRI_TRANSFER)
	if (xfer_data) {
		int rsp_chanpos;

		/*
		 * Remove the transferrer channel traps.
		 *
		 * We must refind chanpos because we released pri->lock.
		 */
		rsp_chanpos = pri_find_principle_by_call(pri, call_1->pri);
		if (0 <= rsp_chanpos) {
			sig_pri_lock_private(pri->pvts[rsp_chanpos]);
			pri->pvts[rsp_chanpos]->xfer_data = NULL;
			sig_pri_unlock_private(pri->pvts[rsp_chanpos]);
		}
		rsp_chanpos = pri_find_principle_by_call(pri, call_2->pri);
		if (0 <= rsp_chanpos) {
			sig_pri_lock_private(pri->pvts[rsp_chanpos]);
			pri->pvts[rsp_chanpos]->xfer_data = NULL;
			sig_pri_unlock_private(pri->pvts[rsp_chanpos]);
		}

		/* Report transfer status. */
		sig_pri_transfer_rsp(xfer_data, retval ? 0 : 1);
	}
#endif	/* defined(HAVE_PRI_TRANSFER) */
	ast_channel_unref(call_1->ast);
	ast_channel_unref(call_2->ast);
	return retval;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) || defined(HAVE_PRI_TRANSFER) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Compare the CC agent private data by libpri cc_id.
 * \since 1.8
 *
 * \param obj pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param flags flags from ao2_callback()
 *
 * \return values are a combination of enum _cb_results.
 */
static int sig_pri_cc_agent_cmp_cc_id(void *obj, void *arg, int flags)
{
	struct ast_cc_agent *agent_1 = obj;
	struct sig_pri_cc_agent_prv *agent_prv_1 = agent_1->private_data;
	struct sig_pri_cc_agent_prv *agent_prv_2 = arg;

	return (agent_prv_1 && agent_prv_1->pri == agent_prv_2->pri
		&& agent_prv_1->cc_id == agent_prv_2->cc_id) ? CMP_MATCH | CMP_STOP : 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Find the CC agent by libpri cc_id.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param cc_id CC record ID to find.
 *
 * \note
 * Since agents are refcounted, and this function returns
 * a reference to the agent, it is imperative that you decrement
 * the refcount of the agent once you have finished using it.
 *
 * \retval agent on success.
 * \retval NULL not found.
 */
static struct ast_cc_agent *sig_pri_find_cc_agent_by_cc_id(struct sig_pri_span *pri, long cc_id)
{
	struct sig_pri_cc_agent_prv finder = {
		.pri = pri,
		.cc_id = cc_id,
	};

	return ast_cc_agent_callback(0, sig_pri_cc_agent_cmp_cc_id, &finder,
		sig_pri_cc_type_name);
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Compare the CC monitor instance by libpri cc_id.
 * \since 1.8
 *
 * \param obj pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param flags flags from ao2_callback()
 *
 * \return values are a combination of enum _cb_results.
 */
static int sig_pri_cc_monitor_cmp_cc_id(void *obj, void *arg, int flags)
{
	struct sig_pri_cc_monitor_instance *monitor_1 = obj;
	struct sig_pri_cc_monitor_instance *monitor_2 = arg;

	return (monitor_1->pri == monitor_2->pri
		&& monitor_1->cc_id == monitor_2->cc_id) ? CMP_MATCH | CMP_STOP : 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Find the CC monitor instance by libpri cc_id.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param cc_id CC record ID to find.
 *
 * \note
 * Since monitor_instances are refcounted, and this function returns
 * a reference to the instance, it is imperative that you decrement
 * the refcount of the instance once you have finished using it.
 *
 * \retval monitor_instance on success.
 * \retval NULL not found.
 */
static struct sig_pri_cc_monitor_instance *sig_pri_find_cc_monitor_by_cc_id(struct sig_pri_span *pri, long cc_id)
{
	struct sig_pri_cc_monitor_instance finder = {
		.pri = pri,
		.cc_id = cc_id,
	};

	return ao2_callback(sig_pri_cc_monitors, 0, sig_pri_cc_monitor_cmp_cc_id, &finder);
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Destroy the given monitor instance.
 * \since 1.8
 *
 * \param data Monitor instance to destroy.
 *
 * \return Nothing
 */
static void sig_pri_cc_monitor_instance_destroy(void *data)
{
	struct sig_pri_cc_monitor_instance *monitor_instance = data;

	if (monitor_instance->cc_id != -1) {
		ast_mutex_lock(&monitor_instance->pri->lock);
		pri_cc_cancel(monitor_instance->pri->pri, monitor_instance->cc_id);
		ast_mutex_unlock(&monitor_instance->pri->lock);
	}
	sig_pri_callbacks.module_unref();
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Construct a new monitor instance.
 * \since 1.8
 *
 * \param core_id CC core ID.
 * \param pri PRI span control structure.
 * \param cc_id CC record ID.
 * \param device_name Name of device (Asterisk channel name less sequence number).
 *
 * \note
 * Since monitor_instances are refcounted, and this function returns
 * a reference to the instance, it is imperative that you decrement
 * the refcount of the instance once you have finished using it.
 *
 * \retval monitor_instance on success.
 * \retval NULL on error.
 */
static struct sig_pri_cc_monitor_instance *sig_pri_cc_monitor_instance_init(int core_id, struct sig_pri_span *pri, long cc_id, const char *device_name)
{
	struct sig_pri_cc_monitor_instance *monitor_instance;

	if (!sig_pri_callbacks.module_ref || !sig_pri_callbacks.module_unref) {
		return NULL;
	}

	monitor_instance = ao2_alloc(sizeof(*monitor_instance) + strlen(device_name),
		sig_pri_cc_monitor_instance_destroy);
	if (!monitor_instance) {
		return NULL;
	}

	monitor_instance->cc_id = cc_id;
	monitor_instance->pri = pri;
	monitor_instance->core_id = core_id;
	strcpy(monitor_instance->name, device_name);

	sig_pri_callbacks.module_ref();

	ao2_link(sig_pri_cc_monitors, monitor_instance);
	return monitor_instance;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Announce to the CC core that protocol CC monitor is available for this call.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param cc_id CC record ID.
 * \param service CCBS/CCNR indication.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 * \note Assumes the sig_pri_lock_owner(pri, chanpos) is already obtained.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int sig_pri_cc_available(struct sig_pri_span *pri, int chanpos, long cc_id, enum ast_cc_service_type service)
{
	struct sig_pri_chan *pvt;
	struct ast_cc_config_params *cc_params;
	struct sig_pri_cc_monitor_instance *monitor;
	enum ast_cc_monitor_policies monitor_policy;
	int core_id;
	int res;
	char device_name[AST_CHANNEL_NAME];
	char dialstring[AST_CHANNEL_NAME];

	pvt = pri->pvts[chanpos];

	core_id = ast_cc_get_current_core_id(pvt->owner);
	if (core_id == -1) {
		return -1;
	}

	cc_params = ast_channel_get_cc_config_params(pvt->owner);
	if (!cc_params) {
		return -1;
	}

	res = -1;
	monitor_policy = ast_get_cc_monitor_policy(cc_params);
	switch (monitor_policy) {
	case AST_CC_MONITOR_NEVER:
		/* CCSS is not enabled. */
		break;
	case AST_CC_MONITOR_NATIVE:
	case AST_CC_MONITOR_ALWAYS:
		/*
		 * If it is AST_CC_MONITOR_ALWAYS and native fails we will attempt the fallback
		 * later in the call to sig_pri_cc_generic_check().
		 */
		ast_channel_get_device_name(pvt->owner, device_name, sizeof(device_name));
		sig_pri_make_cc_dialstring(pvt, dialstring, sizeof(dialstring));
		monitor = sig_pri_cc_monitor_instance_init(core_id, pri, cc_id, device_name);
		if (!monitor) {
			break;
		}
		res = ast_queue_cc_frame(pvt->owner, sig_pri_cc_type_name, dialstring, service,
			monitor);
		if (res) {
			monitor->cc_id = -1;
			ao2_unlink(sig_pri_cc_monitors, monitor);
			ao2_ref(monitor, -1);
		}
		break;
	case AST_CC_MONITOR_GENERIC:
		ast_queue_cc_frame(pvt->owner, AST_CC_GENERIC_MONITOR_TYPE,
			sig_pri_get_orig_dialstring(pvt), service, NULL);
		/* Say it failed to force caller to cancel native CC. */
		break;
	}
	return res;
}
#endif	/* defined(HAVE_PRI_CCSS) */

/*!
 * \internal
 * \brief Check if generic CC monitor is needed and request it.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param service CCBS/CCNR indication.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_cc_generic_check(struct sig_pri_span *pri, int chanpos, enum ast_cc_service_type service)
{
	struct ast_channel *owner;
	struct ast_cc_config_params *cc_params;
#if defined(HAVE_PRI_CCSS)
	struct ast_cc_monitor *monitor;
	char device_name[AST_CHANNEL_NAME];
#endif	/* defined(HAVE_PRI_CCSS) */
	enum ast_cc_monitor_policies monitor_policy;
	int core_id;

	if (!pri->pvts[chanpos]->outgoing) {
		/* This is not an outgoing call so it cannot be CC monitor. */
		return;
	}

	sig_pri_lock_owner(pri, chanpos);
	owner = pri->pvts[chanpos]->owner;
	if (!owner) {
		return;
	}
	core_id = ast_cc_get_current_core_id(owner);
	if (core_id == -1) {
		/* No CC core setup */
		goto done;
	}

	cc_params = ast_channel_get_cc_config_params(owner);
	if (!cc_params) {
		/* Could not get CC config parameters. */
		goto done;
	}

#if defined(HAVE_PRI_CCSS)
	ast_channel_get_device_name(owner, device_name, sizeof(device_name));
	monitor = ast_cc_get_monitor_by_recall_core_id(core_id, device_name);
	if (monitor) {
		/* CC monitor is already present so no need for generic CC. */
		ao2_ref(monitor, -1);
		goto done;
	}
#endif	/* defined(HAVE_PRI_CCSS) */

	monitor_policy = ast_get_cc_monitor_policy(cc_params);
	switch (monitor_policy) {
	case AST_CC_MONITOR_NEVER:
		/* CCSS is not enabled. */
		break;
	case AST_CC_MONITOR_NATIVE:
		if (pri->sig == SIG_BRI_PTMP && pri->nodetype == PRI_NETWORK) {
			/* Request generic CC monitor. */
			ast_queue_cc_frame(owner, AST_CC_GENERIC_MONITOR_TYPE,
				sig_pri_get_orig_dialstring(pri->pvts[chanpos]), service, NULL);
		}
		break;
	case AST_CC_MONITOR_ALWAYS:
		if (pri->sig == SIG_BRI_PTMP && pri->nodetype != PRI_NETWORK) {
			/*
			 * Cannot monitor PTMP TE side since this is not defined.
			 * We are playing the roll of a phone in this case and
			 * a phone cannot monitor a party over the network without
			 * protocol help.
			 */
			break;
		}
		/*
		 * We are either falling back or this is a PTMP NT span.
		 * Request generic CC monitor.
		 */
		ast_queue_cc_frame(owner, AST_CC_GENERIC_MONITOR_TYPE,
			sig_pri_get_orig_dialstring(pri->pvts[chanpos]), service, NULL);
		break;
	case AST_CC_MONITOR_GENERIC:
		if (pri->sig == SIG_BRI_PTMP && pri->nodetype == PRI_NETWORK) {
			/* Request generic CC monitor. */
			ast_queue_cc_frame(owner, AST_CC_GENERIC_MONITOR_TYPE,
				sig_pri_get_orig_dialstring(pri->pvts[chanpos]), service, NULL);
		}
		break;
	}

done:
	ast_channel_unlock(owner);
}

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief The CC link canceled the CC instance.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param cc_id CC record ID.
 * \param is_agent TRUE if the cc_id is for an agent.
 *
 * \return Nothing
 */
static void sig_pri_cc_link_canceled(struct sig_pri_span *pri, long cc_id, int is_agent)
{
	if (is_agent) {
		struct ast_cc_agent *agent;

		agent = sig_pri_find_cc_agent_by_cc_id(pri, cc_id);
		if (!agent) {
			return;
		}
		ast_cc_failed(agent->core_id, "%s agent got canceled by link",
			sig_pri_cc_type_name);
		ao2_ref(agent, -1);
	} else {
		struct sig_pri_cc_monitor_instance *monitor;

		monitor = sig_pri_find_cc_monitor_by_cc_id(pri, cc_id);
		if (!monitor) {
			return;
		}
		monitor->cc_id = -1;
		ast_cc_monitor_failed(monitor->core_id, monitor->name,
			"%s monitor got canceled by link", sig_pri_cc_type_name);
		ao2_ref(monitor, -1);
	}
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Convert ast_aoc_charged_item to PRI_AOC_CHARGED_ITEM .
 * \since 1.8
 *
 * \param value Value to convert to string.
 *
 * \return PRI_AOC_CHARGED_ITEM
 */
static enum PRI_AOC_CHARGED_ITEM sig_pri_aoc_charged_item_to_pri(enum PRI_AOC_CHARGED_ITEM value)
{
	switch (value) {
	case AST_AOC_CHARGED_ITEM_NA:
		return PRI_AOC_CHARGED_ITEM_NOT_AVAILABLE;
	case AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT:
		return PRI_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT;
	case AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION:
		return PRI_AOC_CHARGED_ITEM_BASIC_COMMUNICATION;
	case AST_AOC_CHARGED_ITEM_CALL_ATTEMPT:
		return PRI_AOC_CHARGED_ITEM_CALL_ATTEMPT;
	case AST_AOC_CHARGED_ITEM_CALL_SETUP:
		return PRI_AOC_CHARGED_ITEM_CALL_SETUP;
	case AST_AOC_CHARGED_ITEM_USER_USER_INFO:
		return PRI_AOC_CHARGED_ITEM_USER_USER_INFO;
	case AST_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE:
		return PRI_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE;
	}
	return PRI_AOC_CHARGED_ITEM_NOT_AVAILABLE;
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Convert PRI_AOC_CHARGED_ITEM to ast_aoc_charged_item.
 * \since 1.8
 *
 * \param value Value to convert to string.
 *
 * \return ast_aoc_charged_item
 */
static enum ast_aoc_s_charged_item sig_pri_aoc_charged_item_to_ast(enum PRI_AOC_CHARGED_ITEM value)
{
	switch (value) {
	case PRI_AOC_CHARGED_ITEM_NOT_AVAILABLE:
		return AST_AOC_CHARGED_ITEM_NA;
	case PRI_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT:
		return AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT;
	case PRI_AOC_CHARGED_ITEM_BASIC_COMMUNICATION:
		return AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION;
	case PRI_AOC_CHARGED_ITEM_CALL_ATTEMPT:
		return AST_AOC_CHARGED_ITEM_CALL_ATTEMPT;
	case PRI_AOC_CHARGED_ITEM_CALL_SETUP:
		return AST_AOC_CHARGED_ITEM_CALL_SETUP;
	case PRI_AOC_CHARGED_ITEM_USER_USER_INFO:
		return AST_AOC_CHARGED_ITEM_USER_USER_INFO;
	case PRI_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE:
		return AST_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE;
	}
	return AST_AOC_CHARGED_ITEM_NA;
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Convert AST_AOC_MULTIPLER to PRI_AOC_MULTIPLIER.
 * \since 1.8
 *
 * \return pri enum equivalent.
 */
static int sig_pri_aoc_multiplier_from_ast(enum ast_aoc_currency_multiplier mult)
{
	switch (mult) {
	case AST_AOC_MULT_ONETHOUSANDTH:
		return PRI_AOC_MULTIPLIER_THOUSANDTH;
	case AST_AOC_MULT_ONEHUNDREDTH:
		return PRI_AOC_MULTIPLIER_HUNDREDTH;
	case AST_AOC_MULT_ONETENTH:
		return PRI_AOC_MULTIPLIER_TENTH;
	case AST_AOC_MULT_ONE:
		return PRI_AOC_MULTIPLIER_ONE;
	case AST_AOC_MULT_TEN:
		return PRI_AOC_MULTIPLIER_TEN;
	case AST_AOC_MULT_HUNDRED:
		return PRI_AOC_MULTIPLIER_HUNDRED;
	case AST_AOC_MULT_THOUSAND:
		return PRI_AOC_MULTIPLIER_THOUSAND;
	default:
		return PRI_AOC_MULTIPLIER_ONE;
	}
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Convert PRI_AOC_MULTIPLIER to AST_AOC_MULTIPLIER
 * \since 1.8
 *
 * \return ast enum equivalent.
 */
static int sig_pri_aoc_multiplier_from_pri(const int mult)
{
	switch (mult) {
	case PRI_AOC_MULTIPLIER_THOUSANDTH:
		return AST_AOC_MULT_ONETHOUSANDTH;
	case PRI_AOC_MULTIPLIER_HUNDREDTH:
		return AST_AOC_MULT_ONEHUNDREDTH;
	case PRI_AOC_MULTIPLIER_TENTH:
		return AST_AOC_MULT_ONETENTH;
	case PRI_AOC_MULTIPLIER_ONE:
		return AST_AOC_MULT_ONE;
	case PRI_AOC_MULTIPLIER_TEN:
		return AST_AOC_MULT_TEN;
	case PRI_AOC_MULTIPLIER_HUNDRED:
		return AST_AOC_MULT_HUNDRED;
	case PRI_AOC_MULTIPLIER_THOUSAND:
		return AST_AOC_MULT_THOUSAND;
	default:
		return AST_AOC_MULT_ONE;
	}
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Convert ast_aoc_time_scale representation to PRI_AOC_TIME_SCALE
 * \since 1.8
 *
 * \param value Value to convert to ast representation
 *
 * \return PRI_AOC_TIME_SCALE
 */
static enum PRI_AOC_TIME_SCALE sig_pri_aoc_scale_to_pri(enum ast_aoc_time_scale value)
{
	switch (value) {
	default:
	case AST_AOC_TIME_SCALE_HUNDREDTH_SECOND:
		return PRI_AOC_TIME_SCALE_HUNDREDTH_SECOND;
	case AST_AOC_TIME_SCALE_TENTH_SECOND:
		return PRI_AOC_TIME_SCALE_TENTH_SECOND;
	case AST_AOC_TIME_SCALE_SECOND:
		return PRI_AOC_TIME_SCALE_SECOND;
	case AST_AOC_TIME_SCALE_TEN_SECOND:
		return PRI_AOC_TIME_SCALE_TEN_SECOND;
	case AST_AOC_TIME_SCALE_MINUTE:
		return PRI_AOC_TIME_SCALE_MINUTE;
	case AST_AOC_TIME_SCALE_HOUR:
		return PRI_AOC_TIME_SCALE_HOUR;
	case AST_AOC_TIME_SCALE_DAY:
		return PRI_AOC_TIME_SCALE_DAY;
	}
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Convert PRI_AOC_TIME_SCALE to ast aoc representation
 * \since 1.8
 *
 * \param value Value to convert to ast representation
 *
 * \return ast aoc time scale
 */
static enum ast_aoc_time_scale sig_pri_aoc_scale_to_ast(enum PRI_AOC_TIME_SCALE value)
{
	switch (value) {
	default:
	case PRI_AOC_TIME_SCALE_HUNDREDTH_SECOND:
		return AST_AOC_TIME_SCALE_HUNDREDTH_SECOND;
	case PRI_AOC_TIME_SCALE_TENTH_SECOND:
		return AST_AOC_TIME_SCALE_TENTH_SECOND;
	case PRI_AOC_TIME_SCALE_SECOND:
		return AST_AOC_TIME_SCALE_SECOND;
	case PRI_AOC_TIME_SCALE_TEN_SECOND:
		return AST_AOC_TIME_SCALE_TEN_SECOND;
	case PRI_AOC_TIME_SCALE_MINUTE:
		return AST_AOC_TIME_SCALE_MINUTE;
	case PRI_AOC_TIME_SCALE_HOUR:
		return AST_AOC_TIME_SCALE_HOUR;
	case PRI_AOC_TIME_SCALE_DAY:
		return AST_AOC_TIME_SCALE_DAY;
	}
	return AST_AOC_TIME_SCALE_HUNDREDTH_SECOND;
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Handle AOC-S control frame
 * \since 1.8
 *
 * \param aoc_s AOC-S event parameters.
 * \param owner Asterisk channel associated with the call.
 * \param passthrough indicating if this message should be queued on the ast channel
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri private is locked
 * \note Assumes the owner channel lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_aoc_s_from_pri(const struct pri_subcmd_aoc_s *aoc_s, struct ast_channel *owner, int passthrough)
{
	struct ast_aoc_decoded *decoded = NULL;
	struct ast_aoc_encoded *encoded = NULL;
	size_t encoded_size = 0;
	int idx;

	if (!owner || !aoc_s) {
		return;
	}

	if (!(decoded = ast_aoc_create(AST_AOC_S, 0, 0))) {
		return;
	}

	for (idx = 0; idx < aoc_s->num_items; ++idx) {
		enum ast_aoc_s_charged_item charged_item;

		charged_item = sig_pri_aoc_charged_item_to_ast(aoc_s->item[idx].chargeable);
		if (charged_item == AST_AOC_CHARGED_ITEM_NA) {
			/* Delete the unknown charged item from the list. */
			continue;
		}
		switch (aoc_s->item[idx].rate_type) {
		case PRI_AOC_RATE_TYPE_DURATION:
			ast_aoc_s_add_rate_duration(decoded,
				charged_item,
				aoc_s->item[idx].rate.duration.amount.cost,
				sig_pri_aoc_multiplier_from_pri(aoc_s->item[idx].rate.duration.amount.multiplier),
				aoc_s->item[idx].rate.duration.currency,
				aoc_s->item[idx].rate.duration.time.length,
				sig_pri_aoc_scale_to_ast(aoc_s->item[idx].rate.duration.time.scale),
				aoc_s->item[idx].rate.duration.granularity.length,
				sig_pri_aoc_scale_to_ast(aoc_s->item[idx].rate.duration.granularity.scale),
				aoc_s->item[idx].rate.duration.charging_type);
			break;
		case PRI_AOC_RATE_TYPE_FLAT:
			ast_aoc_s_add_rate_flat(decoded,
				charged_item,
				aoc_s->item[idx].rate.flat.amount.cost,
				sig_pri_aoc_multiplier_from_pri(aoc_s->item[idx].rate.flat.amount.multiplier),
				aoc_s->item[idx].rate.flat.currency);
			break;
		case PRI_AOC_RATE_TYPE_VOLUME:
			ast_aoc_s_add_rate_volume(decoded,
				charged_item,
				aoc_s->item[idx].rate.volume.unit,
				aoc_s->item[idx].rate.volume.amount.cost,
				sig_pri_aoc_multiplier_from_pri(aoc_s->item[idx].rate.volume.amount.multiplier),
				aoc_s->item[idx].rate.volume.currency);
			break;
		case PRI_AOC_RATE_TYPE_SPECIAL_CODE:
			ast_aoc_s_add_rate_special_charge_code(decoded,
				charged_item,
				aoc_s->item[idx].rate.special);
			break;
		case PRI_AOC_RATE_TYPE_FREE:
			ast_aoc_s_add_rate_free(decoded, charged_item, 0);
			break;
		case PRI_AOC_RATE_TYPE_FREE_FROM_BEGINNING:
			ast_aoc_s_add_rate_free(decoded, charged_item, 1);
			break;
		default:
			ast_aoc_s_add_rate_na(decoded, charged_item);
			break;
		}
	}

	if (passthrough && (encoded = ast_aoc_encode(decoded, &encoded_size, owner))) {
		ast_queue_control_data(owner, AST_CONTROL_AOC, encoded, encoded_size);
	}

	ast_aoc_manager_event(decoded, owner);

	ast_aoc_destroy_decoded(decoded);
	ast_aoc_destroy_encoded(encoded);
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Generate AOC Request Response
 * \since 1.8
 *
 * \param aoc_request
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri private is locked
 * \note Assumes the owner channel lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_aoc_request_from_pri(const struct pri_subcmd_aoc_request *aoc_request, struct sig_pri_chan *pvt, q931_call *call)
{
	int request;

	if (!aoc_request) {
		return;
	}

	request = aoc_request->charging_request;

	if (request & PRI_AOC_REQUEST_S) {
		if (pvt->pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_S) {
			/* An AOC-S response must come from the other side, so save off this invoke_id
			 * and see if an AOC-S message comes in before the call is answered. */
			pvt->aoc_s_request_invoke_id = aoc_request->invoke_id;
			pvt->aoc_s_request_invoke_id_valid = 1;

		} else {
			pri_aoc_s_request_response_send(pvt->pri->pri,
				call,
				aoc_request->invoke_id,
				NULL);
		}
	}

	if (request & PRI_AOC_REQUEST_D) {
		if (pvt->pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_D) {
			pri_aoc_de_request_response_send(pvt->pri->pri,
				call,
				PRI_AOC_REQ_RSP_CHARGING_INFO_FOLLOWS,
				aoc_request->invoke_id);
		} else {
			pri_aoc_de_request_response_send(pvt->pri->pri,
				call,
				PRI_AOC_REQ_RSP_ERROR_NOT_AVAILABLE,
				aoc_request->invoke_id);
		}
	}

	if (request & PRI_AOC_REQUEST_E) {
		if (pvt->pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_E) {
			pri_aoc_de_request_response_send(pvt->pri->pri,
				call,
				PRI_AOC_REQ_RSP_CHARGING_INFO_FOLLOWS,
				aoc_request->invoke_id);
		} else {
			pri_aoc_de_request_response_send(pvt->pri->pri,
				call,
				PRI_AOC_REQ_RSP_ERROR_NOT_AVAILABLE,
				aoc_request->invoke_id);
		}
	}
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Generate AOC-D AST_CONTROL_AOC frame
 * \since 1.8
 *
 * \param aoc_e AOC-D event parameters.
 * \param owner Asterisk channel associated with the call.
 * \param passthrough indicating if this message should be queued on the ast channel
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri private is locked
 * \note Assumes the owner channel lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_aoc_d_from_pri(const struct pri_subcmd_aoc_d *aoc_d, struct ast_channel *owner, int passthrough)
{
	struct ast_aoc_decoded *decoded = NULL;
	struct ast_aoc_encoded *encoded = NULL;
	size_t encoded_size = 0;
	enum ast_aoc_charge_type type;

	if (!owner || !aoc_d) {
		return;
	}

	switch (aoc_d->charge) {
	case PRI_AOC_DE_CHARGE_CURRENCY:
		type = AST_AOC_CHARGE_CURRENCY;
		break;
	case PRI_AOC_DE_CHARGE_UNITS:
		type = AST_AOC_CHARGE_UNIT;
		break;
	case PRI_AOC_DE_CHARGE_FREE:
		type = AST_AOC_CHARGE_FREE;
		break;
	default:
		type = AST_AOC_CHARGE_NA;
		break;
	}

	if (!(decoded = ast_aoc_create(AST_AOC_D, type, 0))) {
		return;
	}

	switch (aoc_d->billing_accumulation) {
	default:
		ast_debug(1, "AOC-D billing accumulation has unknown value: %d\n",
			aoc_d->billing_accumulation);
		/* Fall through */
	case 0:/* subTotal */
		ast_aoc_set_total_type(decoded, AST_AOC_SUBTOTAL);
		break;
	case 1:/* total */
		ast_aoc_set_total_type(decoded, AST_AOC_TOTAL);
		break;
	}

	switch (aoc_d->billing_id) {
	case PRI_AOC_D_BILLING_ID_NORMAL:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_NORMAL);
		break;
	case PRI_AOC_D_BILLING_ID_REVERSE:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_REVERSE_CHARGE);
		break;
	case PRI_AOC_D_BILLING_ID_CREDIT_CARD:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CREDIT_CARD);
		break;
	case PRI_AOC_D_BILLING_ID_NOT_AVAILABLE:
	default:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_NA);
		break;
	}

	switch (aoc_d->charge) {
	case PRI_AOC_DE_CHARGE_CURRENCY:
		ast_aoc_set_currency_info(decoded,
			aoc_d->recorded.money.amount.cost,
			sig_pri_aoc_multiplier_from_pri(aoc_d->recorded.money.amount.multiplier),
			aoc_d->recorded.money.currency);
		break;
	case PRI_AOC_DE_CHARGE_UNITS:
		{
			int i;
			for (i = 0; i < aoc_d->recorded.unit.num_items; ++i) {
				/* if type or number are negative, then they are not present */
				ast_aoc_add_unit_entry(decoded,
					(aoc_d->recorded.unit.item[i].number >= 0 ? 1 : 0),
					aoc_d->recorded.unit.item[i].number,
					(aoc_d->recorded.unit.item[i].type >= 0 ? 1 : 0),
					aoc_d->recorded.unit.item[i].type);
			}
		}
		break;
	}

	if (passthrough && (encoded = ast_aoc_encode(decoded, &encoded_size, owner))) {
		ast_queue_control_data(owner, AST_CONTROL_AOC, encoded, encoded_size);
	}

	ast_aoc_manager_event(decoded, owner);

	ast_aoc_destroy_decoded(decoded);
	ast_aoc_destroy_encoded(encoded);
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief Generate AOC-E AST_CONTROL_AOC frame
 * \since 1.8
 *
 * \param aoc_e AOC-E event parameters.
 * \param owner Asterisk channel associated with the call.
 * \param passthrough indicating if this message should be queued on the ast channel
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri private is locked
 * \note Assumes the owner channel lock is already obtained.
 * \note owner channel may be NULL. In that case, generate event only
 *
 * \return Nothing
 */
static void sig_pri_aoc_e_from_pri(const struct pri_subcmd_aoc_e *aoc_e, struct ast_channel *owner, int passthrough)
{
	struct ast_aoc_decoded *decoded = NULL;
	struct ast_aoc_encoded *encoded = NULL;
	size_t encoded_size = 0;
	enum ast_aoc_charge_type type;

	if (!aoc_e) {
		return;
	}

	switch (aoc_e->charge) {
	case PRI_AOC_DE_CHARGE_CURRENCY:
		type = AST_AOC_CHARGE_CURRENCY;
		break;
	case PRI_AOC_DE_CHARGE_UNITS:
		type = AST_AOC_CHARGE_UNIT;
		break;
	case PRI_AOC_DE_CHARGE_FREE:
		type = AST_AOC_CHARGE_FREE;
		break;
	default:
		type = AST_AOC_CHARGE_NA;
		break;
	}

	if (!(decoded = ast_aoc_create(AST_AOC_E, type, 0))) {
		return;
	}

	switch (aoc_e->associated.charging_type) {
	case PRI_AOC_E_CHARGING_ASSOCIATION_NUMBER:
		if (!aoc_e->associated.charge.number.valid) {
			break;
		}
		ast_aoc_set_association_number(decoded, aoc_e->associated.charge.number.str, aoc_e->associated.charge.number.plan);
		break;
	case PRI_AOC_E_CHARGING_ASSOCIATION_ID:
		ast_aoc_set_association_id(decoded, aoc_e->associated.charge.id);
		break;
	default:
		break;
	}

	switch (aoc_e->billing_id) {
	case PRI_AOC_E_BILLING_ID_NORMAL:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_NORMAL);
		break;
	case PRI_AOC_E_BILLING_ID_REVERSE:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_REVERSE_CHARGE);
		break;
	case PRI_AOC_E_BILLING_ID_CREDIT_CARD:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CREDIT_CARD);
		break;
	case PRI_AOC_E_BILLING_ID_CALL_FORWARDING_UNCONDITIONAL:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CALL_FWD_UNCONDITIONAL);
		break;
	case PRI_AOC_E_BILLING_ID_CALL_FORWARDING_BUSY:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CALL_FWD_BUSY);
		break;
	case PRI_AOC_E_BILLING_ID_CALL_FORWARDING_NO_REPLY:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CALL_FWD_NO_REPLY);
		break;
	case PRI_AOC_E_BILLING_ID_CALL_DEFLECTION:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CALL_DEFLECTION);
		break;
	case PRI_AOC_E_BILLING_ID_CALL_TRANSFER:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_CALL_TRANSFER);
		break;
	case PRI_AOC_E_BILLING_ID_NOT_AVAILABLE:
	default:
		ast_aoc_set_billing_id(decoded, AST_AOC_BILLING_NA);
		break;
	}

	switch (aoc_e->charge) {
	case PRI_AOC_DE_CHARGE_CURRENCY:
		ast_aoc_set_currency_info(decoded,
			aoc_e->recorded.money.amount.cost,
			sig_pri_aoc_multiplier_from_pri(aoc_e->recorded.money.amount.multiplier),
			aoc_e->recorded.money.currency);
		break;
	case PRI_AOC_DE_CHARGE_UNITS:
		{
			int i;
			for (i = 0; i < aoc_e->recorded.unit.num_items; ++i) {
				/* if type or number are negative, then they are not present */
				ast_aoc_add_unit_entry(decoded,
					(aoc_e->recorded.unit.item[i].number >= 0 ? 1 : 0),
					aoc_e->recorded.unit.item[i].number,
					(aoc_e->recorded.unit.item[i].type >= 0 ? 1 : 0),
					aoc_e->recorded.unit.item[i].type);
			}
		}
	}

	if (passthrough && owner && (encoded = ast_aoc_encode(decoded, &encoded_size, owner))) {
		ast_queue_control_data(owner, AST_CONTROL_AOC, encoded, encoded_size);
	}

	ast_aoc_manager_event(decoded, owner);

	ast_aoc_destroy_decoded(decoded);
	ast_aoc_destroy_encoded(encoded);
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief send an AOC-S message on the current call
 *
 * \param pvt sig_pri private channel structure.
 * \param generic decoded ast AOC message
 *
 * \return Nothing
 *
 * \note Assumes that the PRI lock is already obtained.
 */
static void sig_pri_aoc_s_from_ast(struct sig_pri_chan *pvt, struct ast_aoc_decoded *decoded)
{
	struct pri_subcmd_aoc_s aoc_s = { 0, };
	const struct ast_aoc_s_entry *entry;
	int idx;

	for (idx = 0; idx < ast_aoc_s_get_count(decoded); idx++) {
		if (!(entry = ast_aoc_s_get_rate_info(decoded, idx))) {
			break;
		}

		aoc_s.item[idx].chargeable = sig_pri_aoc_charged_item_to_pri(entry->charged_item);

		switch (entry->rate_type) {
		case AST_AOC_RATE_TYPE_DURATION:
			aoc_s.item[idx].rate_type = PRI_AOC_RATE_TYPE_DURATION;
			aoc_s.item[idx].rate.duration.amount.cost = entry->rate.duration.amount;
			aoc_s.item[idx].rate.duration.amount.multiplier =
				sig_pri_aoc_multiplier_from_ast(entry->rate.duration.multiplier);
			aoc_s.item[idx].rate.duration.time.length = entry->rate.duration.time;
			aoc_s.item[idx].rate.duration.time.scale =
				sig_pri_aoc_scale_to_pri(entry->rate.duration.time_scale);
			aoc_s.item[idx].rate.duration.granularity.length = entry->rate.duration.granularity_time;
			aoc_s.item[idx].rate.duration.granularity.scale =
				sig_pri_aoc_scale_to_pri(entry->rate.duration.granularity_time_scale);
			aoc_s.item[idx].rate.duration.charging_type = entry->rate.duration.charging_type;

			if (!ast_strlen_zero(entry->rate.duration.currency_name)) {
				ast_copy_string(aoc_s.item[idx].rate.duration.currency,
					entry->rate.duration.currency_name,
					sizeof(aoc_s.item[idx].rate.duration.currency));
			}
			break;
		case AST_AOC_RATE_TYPE_FLAT:
			aoc_s.item[idx].rate_type = PRI_AOC_RATE_TYPE_FLAT;
			aoc_s.item[idx].rate.flat.amount.cost = entry->rate.flat.amount;
			aoc_s.item[idx].rate.flat.amount.multiplier =
				sig_pri_aoc_multiplier_from_ast(entry->rate.flat.multiplier);

			if (!ast_strlen_zero(entry->rate.flat.currency_name)) {
				ast_copy_string(aoc_s.item[idx].rate.flat.currency,
					entry->rate.flat.currency_name,
					sizeof(aoc_s.item[idx].rate.flat.currency));
			}
			break;
		case AST_AOC_RATE_TYPE_VOLUME:
			aoc_s.item[idx].rate_type = PRI_AOC_RATE_TYPE_VOLUME;
			aoc_s.item[idx].rate.volume.unit = entry->rate.volume.volume_unit;
			aoc_s.item[idx].rate.volume.amount.cost = entry->rate.volume.amount;
			aoc_s.item[idx].rate.volume.amount.multiplier =
				sig_pri_aoc_multiplier_from_ast(entry->rate.volume.multiplier);

			if (!ast_strlen_zero(entry->rate.volume.currency_name)) {
				ast_copy_string(aoc_s.item[idx].rate.volume.currency,
					entry->rate.volume.currency_name,
					sizeof(aoc_s.item[idx].rate.volume.currency));
			}
			break;
		case AST_AOC_RATE_TYPE_SPECIAL_CODE:
			aoc_s.item[idx].rate_type = PRI_AOC_RATE_TYPE_SPECIAL_CODE;
			aoc_s.item[idx].rate.special = entry->rate.special_code;
			break;
		case AST_AOC_RATE_TYPE_FREE:
			aoc_s.item[idx].rate_type = PRI_AOC_RATE_TYPE_FREE;
			break;
		case AST_AOC_RATE_TYPE_FREE_FROM_BEGINNING:
			aoc_s.item[idx].rate_type = PRI_AOC_RATE_TYPE_FREE_FROM_BEGINNING;
			break;
		default:
		case AST_AOC_RATE_TYPE_NA:
			aoc_s.item[idx].rate_type = PRI_AOC_RATE_TYPE_NOT_AVAILABLE;
			break;
		}
	}
	aoc_s.num_items = idx;

	/* if this rate should be sent as a response to an AOC-S request we will
	 * have an aoc_s_request_invoke_id associated with this pvt */
	if (pvt->aoc_s_request_invoke_id_valid) {
		pri_aoc_s_request_response_send(pvt->pri->pri, pvt->call, pvt->aoc_s_request_invoke_id, &aoc_s);
		pvt->aoc_s_request_invoke_id_valid = 0;
	} else {
		pri_aoc_s_send(pvt->pri->pri, pvt->call, &aoc_s);
	}
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief send an AOC-D message on the current call
 *
 * \param pvt sig_pri private channel structure.
 * \param generic decoded ast AOC message
 *
 * \return Nothing
 *
 * \note Assumes that the PRI lock is already obtained.
 */
static void sig_pri_aoc_d_from_ast(struct sig_pri_chan *pvt, struct ast_aoc_decoded *decoded)
{
	struct pri_subcmd_aoc_d aoc_d = { 0, };

	aoc_d.billing_accumulation = (ast_aoc_get_total_type(decoded) == AST_AOC_TOTAL) ? 1 : 0;

	switch (ast_aoc_get_billing_id(decoded)) {
	case AST_AOC_BILLING_NORMAL:
		aoc_d.billing_id = PRI_AOC_D_BILLING_ID_NORMAL;
		break;
	case AST_AOC_BILLING_REVERSE_CHARGE:
		aoc_d.billing_id = PRI_AOC_D_BILLING_ID_REVERSE;
		break;
	case AST_AOC_BILLING_CREDIT_CARD:
		aoc_d.billing_id = PRI_AOC_D_BILLING_ID_CREDIT_CARD;
		break;
	case AST_AOC_BILLING_NA:
	default:
		aoc_d.billing_id = PRI_AOC_D_BILLING_ID_NOT_AVAILABLE;
		break;
	}

	switch (ast_aoc_get_charge_type(decoded)) {
	case AST_AOC_CHARGE_FREE:
		aoc_d.charge = PRI_AOC_DE_CHARGE_FREE;
		break;
	case AST_AOC_CHARGE_CURRENCY:
		{
			const char *currency_name = ast_aoc_get_currency_name(decoded);
			aoc_d.charge = PRI_AOC_DE_CHARGE_CURRENCY;
			aoc_d.recorded.money.amount.cost = ast_aoc_get_currency_amount(decoded);
			aoc_d.recorded.money.amount.multiplier = sig_pri_aoc_multiplier_from_ast(ast_aoc_get_currency_multiplier(decoded));
			if (!ast_strlen_zero(currency_name)) {
				ast_copy_string(aoc_d.recorded.money.currency, currency_name, sizeof(aoc_d.recorded.money.currency));
			}
		}
		break;
	case AST_AOC_CHARGE_UNIT:
		{
			const struct ast_aoc_unit_entry *entry;
			int i;
			aoc_d.charge = PRI_AOC_DE_CHARGE_UNITS;
			for (i = 0; i < ast_aoc_get_unit_count(decoded); i++) {
				if ((entry = ast_aoc_get_unit_info(decoded, i)) && i < ARRAY_LEN(aoc_d.recorded.unit.item)) {
					if (entry->valid_amount) {
						aoc_d.recorded.unit.item[i].number = entry->amount;
					} else {
						aoc_d.recorded.unit.item[i].number = -1;
					}
					if (entry->valid_type) {
						aoc_d.recorded.unit.item[i].type = entry->type;
					} else {
						aoc_d.recorded.unit.item[i].type = -1;
					}
					aoc_d.recorded.unit.num_items++;
				} else {
					break;
				}
			}
		}
		break;
	case AST_AOC_CHARGE_NA:
	default:
		aoc_d.charge = PRI_AOC_DE_CHARGE_NOT_AVAILABLE;
		break;
	}

	pri_aoc_d_send(pvt->pri->pri, pvt->call, &aoc_d);
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief send an AOC-E message on the current call
 *
 * \param pvt sig_pri private channel structure.
 * \param generic decoded ast AOC message
 *
 * \return Nothing
 *
 * \note Assumes that the PRI lock is already obtained.
 */
static void sig_pri_aoc_e_from_ast(struct sig_pri_chan *pvt, struct ast_aoc_decoded *decoded)
{
	struct pri_subcmd_aoc_e *aoc_e = &pvt->aoc_e;
	const struct ast_aoc_charging_association *ca = ast_aoc_get_association_info(decoded);

	memset(aoc_e, 0, sizeof(*aoc_e));
	pvt->holding_aoce = 1;

	switch (ca->charging_type) {
	case AST_AOC_CHARGING_ASSOCIATION_NUMBER:
		aoc_e->associated.charge.number.valid = 1;
		ast_copy_string(aoc_e->associated.charge.number.str,
			ca->charge.number.number,
			sizeof(aoc_e->associated.charge.number.str));
		aoc_e->associated.charge.number.plan = ca->charge.number.plan;
		aoc_e->associated.charging_type = PRI_AOC_E_CHARGING_ASSOCIATION_NUMBER;
		break;
	case AST_AOC_CHARGING_ASSOCIATION_ID:
		aoc_e->associated.charge.id = ca->charge.id;
		aoc_e->associated.charging_type = PRI_AOC_E_CHARGING_ASSOCIATION_ID;
		break;
	case AST_AOC_CHARGING_ASSOCIATION_NA:
	default:
		break;
	}

	switch (ast_aoc_get_billing_id(decoded)) {
	case AST_AOC_BILLING_NORMAL:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_NORMAL;
		break;
	case AST_AOC_BILLING_REVERSE_CHARGE:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_REVERSE;
		break;
	case AST_AOC_BILLING_CREDIT_CARD:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_CREDIT_CARD;
		break;
	case AST_AOC_BILLING_CALL_FWD_UNCONDITIONAL:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_CALL_FORWARDING_UNCONDITIONAL;
		break;
	case AST_AOC_BILLING_CALL_FWD_BUSY:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_CALL_FORWARDING_BUSY;
		break;
	case AST_AOC_BILLING_CALL_FWD_NO_REPLY:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_CALL_FORWARDING_NO_REPLY;
		break;
	case AST_AOC_BILLING_CALL_DEFLECTION:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_CALL_DEFLECTION;
		break;
	case AST_AOC_BILLING_CALL_TRANSFER:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_CALL_TRANSFER;
		break;
	case AST_AOC_BILLING_NA:
	default:
		aoc_e->billing_id = PRI_AOC_E_BILLING_ID_NOT_AVAILABLE;
		break;
	}

	switch (ast_aoc_get_charge_type(decoded)) {
	case AST_AOC_CHARGE_FREE:
		aoc_e->charge = PRI_AOC_DE_CHARGE_FREE;
		break;
	case AST_AOC_CHARGE_CURRENCY:
		{
			const char *currency_name = ast_aoc_get_currency_name(decoded);
			aoc_e->charge = PRI_AOC_DE_CHARGE_CURRENCY;
			aoc_e->recorded.money.amount.cost = ast_aoc_get_currency_amount(decoded);
			aoc_e->recorded.money.amount.multiplier = sig_pri_aoc_multiplier_from_ast(ast_aoc_get_currency_multiplier(decoded));
			if (!ast_strlen_zero(currency_name)) {
				ast_copy_string(aoc_e->recorded.money.currency, currency_name, sizeof(aoc_e->recorded.money.currency));
			}
		}
		break;
	case AST_AOC_CHARGE_UNIT:
		{
			const struct ast_aoc_unit_entry *entry;
			int i;
			aoc_e->charge = PRI_AOC_DE_CHARGE_UNITS;
			for (i = 0; i < ast_aoc_get_unit_count(decoded); i++) {
				if ((entry = ast_aoc_get_unit_info(decoded, i)) && i < ARRAY_LEN(aoc_e->recorded.unit.item)) {
					if (entry->valid_amount) {
						aoc_e->recorded.unit.item[i].number = entry->amount;
					} else {
						aoc_e->recorded.unit.item[i].number = -1;
					}
					if (entry->valid_type) {
						aoc_e->recorded.unit.item[i].type = entry->type;
					} else {
						aoc_e->recorded.unit.item[i].type = -1;
					}
					aoc_e->recorded.unit.num_items++;
				}
			}
		}
		break;
	case AST_AOC_CHARGE_NA:
	default:
		aoc_e->charge = PRI_AOC_DE_CHARGE_NOT_AVAILABLE;
		break;
	}
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

#if defined(HAVE_PRI_AOC_EVENTS)
/*!
 * \internal
 * \brief send an AOC-E termination request on ast_channel and set
 * hangup delay.
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param ms to delay hangup
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_send_aoce_termination_request(struct sig_pri_span *pri, int chanpos, unsigned int ms)
{
	struct sig_pri_chan *pvt;
	struct ast_aoc_decoded *decoded = NULL;
	struct ast_aoc_encoded *encoded = NULL;
	size_t encoded_size;
	struct timeval whentohangup = { 0, };

	sig_pri_lock_owner(pri, chanpos);
	pvt = pri->pvts[chanpos];
	if (!pvt->owner) {
		return;
	}

	if (!(decoded = ast_aoc_create(AST_AOC_REQUEST, 0, AST_AOC_REQUEST_E))) {
		ast_queue_hangup(pvt->owner);
		goto cleanup_termination_request;
	}

	ast_aoc_set_termination_request(decoded);

	if (!(encoded = ast_aoc_encode(decoded, &encoded_size, pvt->owner))) {
		ast_queue_hangup(pvt->owner);
		goto cleanup_termination_request;
	}

	/* convert ms to timeval */
	whentohangup.tv_usec = (ms % 1000) * 1000;
	whentohangup.tv_sec = ms / 1000;

	if (ast_queue_control_data(pvt->owner, AST_CONTROL_AOC, encoded, encoded_size)) {
		ast_queue_hangup(pvt->owner);
		goto cleanup_termination_request;
	}

	pvt->waiting_for_aoce = 1;
	ast_channel_setwhentohangup_tv(pvt->owner, whentohangup);
	ast_debug(1, "Delaying hangup on %s for aoc-e msg\n", ast_channel_name(pvt->owner));

cleanup_termination_request:
	ast_channel_unlock(pvt->owner);
	ast_aoc_destroy_decoded(decoded);
	ast_aoc_destroy_encoded(encoded);
}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

/*!
 * \internal
 * \brief TRUE if PRI event came in on a CIS call.
 * \since 1.8
 *
 * \param channel PRI encoded span/channel
 *
 * \retval non-zero if CIS call.
 */
static int sig_pri_is_cis_call(int channel)
{
	return channel != -1 && (channel & PRI_CIS_CALL);
}

/*!
 * \internal
 * \brief Handle the CIS associated PRI subcommand events.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param event_id PRI event id
 * \param subcmds Subcommands to process if any. (Could be NULL).
 * \param call_rsp libpri opaque call structure to send any responses toward.
 * Could be NULL either because it is not available or the call is for the
 * dummy call reference.  However, this should not be NULL in the cases that
 * need to use the pointer to send a response message back.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_cis_subcmds(struct sig_pri_span *pri, int event_id,
	const struct pri_subcommands *subcmds, q931_call *call_rsp)
{
	int index;
#if defined(HAVE_PRI_CCSS)
	struct ast_cc_agent *agent;
	struct sig_pri_cc_agent_prv *agent_prv;
	struct sig_pri_cc_monitor_instance *monitor;
#endif	/* defined(HAVE_PRI_CCSS) */

	if (!subcmds) {
		return;
	}
	for (index = 0; index < subcmds->counter_subcmd; ++index) {
		const struct pri_subcommand *subcmd = &subcmds->subcmd[index];

		switch (subcmd->cmd) {
#if defined(STATUS_REQUEST_PLACE_HOLDER)
		case PRI_SUBCMD_STATUS_REQ:
		case PRI_SUBCMD_STATUS_REQ_RSP:
			/* Ignore for now. */
			break;
#endif	/* defined(STATUS_REQUEST_PLACE_HOLDER) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_REQ:
			agent = sig_pri_find_cc_agent_by_cc_id(pri, subcmd->u.cc_request.cc_id);
			if (!agent) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_request.cc_id);
				break;
			}
			if (!ast_cc_request_is_within_limits()) {
				if (pri_cc_req_rsp(pri->pri, subcmd->u.cc_request.cc_id,
					5/* queue_full */)) {
					pri_cc_cancel(pri->pri, subcmd->u.cc_request.cc_id);
				}
				ast_cc_failed(agent->core_id, "%s agent system CC queue full",
					sig_pri_cc_type_name);
				ao2_ref(agent, -1);
				break;
			}
			agent_prv = agent->private_data;
			agent_prv->cc_request_response_pending = 1;
			if (ast_cc_agent_accept_request(agent->core_id,
				"%s caller accepted CC offer.", sig_pri_cc_type_name)) {
				agent_prv->cc_request_response_pending = 0;
				if (pri_cc_req_rsp(pri->pri, subcmd->u.cc_request.cc_id,
					2/* short_term_denial */)) {
					pri_cc_cancel(pri->pri, subcmd->u.cc_request.cc_id);
				}
				ast_cc_failed(agent->core_id, "%s agent CC core request accept failed",
					sig_pri_cc_type_name);
			}
			ao2_ref(agent, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_REQ_RSP:
			monitor = sig_pri_find_cc_monitor_by_cc_id(pri,
				subcmd->u.cc_request_rsp.cc_id);
			if (!monitor) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_request_rsp.cc_id);
				break;
			}
			switch (subcmd->u.cc_request_rsp.status) {
			case 0:/* success */
				ast_cc_monitor_request_acked(monitor->core_id,
					"%s far end accepted CC request", sig_pri_cc_type_name);
				break;
			case 1:/* timeout */
				ast_verb(2, "core_id:%d %s CC request timeout\n", monitor->core_id,
					sig_pri_cc_type_name);
				ast_cc_monitor_failed(monitor->core_id, monitor->name,
					"%s CC request timeout", sig_pri_cc_type_name);
				break;
			case 2:/* error */
				ast_verb(2, "core_id:%d %s CC request error: %s\n", monitor->core_id,
					sig_pri_cc_type_name,
					pri_facility_error2str(subcmd->u.cc_request_rsp.fail_code));
				ast_cc_monitor_failed(monitor->core_id, monitor->name,
					"%s CC request error", sig_pri_cc_type_name);
				break;
			case 3:/* reject */
				ast_verb(2, "core_id:%d %s CC request reject: %s\n", monitor->core_id,
					sig_pri_cc_type_name,
					pri_facility_reject2str(subcmd->u.cc_request_rsp.fail_code));
				ast_cc_monitor_failed(monitor->core_id, monitor->name,
					"%s CC request reject", sig_pri_cc_type_name);
				break;
			default:
				ast_verb(2, "core_id:%d %s CC request unknown status %d\n",
					monitor->core_id, sig_pri_cc_type_name,
					subcmd->u.cc_request_rsp.status);
				ast_cc_monitor_failed(monitor->core_id, monitor->name,
					"%s CC request unknown status", sig_pri_cc_type_name);
				break;
			}
			ao2_ref(monitor, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_REMOTE_USER_FREE:
			monitor = sig_pri_find_cc_monitor_by_cc_id(pri,
				subcmd->u.cc_remote_user_free.cc_id);
			if (!monitor) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_remote_user_free.cc_id);
				break;
			}
			ast_cc_monitor_callee_available(monitor->core_id,
				"%s callee has become available", sig_pri_cc_type_name);
			ao2_ref(monitor, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_B_FREE:
			monitor = sig_pri_find_cc_monitor_by_cc_id(pri,
				subcmd->u.cc_b_free.cc_id);
			if (!monitor) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_b_free.cc_id);
				break;
			}
			ast_cc_monitor_party_b_free(monitor->core_id);
			ao2_ref(monitor, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_STATUS_REQ:
			monitor = sig_pri_find_cc_monitor_by_cc_id(pri,
				subcmd->u.cc_status_req.cc_id);
			if (!monitor) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_status_req.cc_id);
				break;
			}
			ast_cc_monitor_status_request(monitor->core_id);
			ao2_ref(monitor, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_STATUS_REQ_RSP:
			agent = sig_pri_find_cc_agent_by_cc_id(pri, subcmd->u.cc_status_req_rsp.cc_id);
			if (!agent) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_status_req_rsp.cc_id);
				break;
			}
			ast_cc_agent_status_response(agent->core_id,
				subcmd->u.cc_status_req_rsp.status ? AST_DEVICE_INUSE
				: AST_DEVICE_NOT_INUSE);
			ao2_ref(agent, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_STATUS:
			agent = sig_pri_find_cc_agent_by_cc_id(pri, subcmd->u.cc_status.cc_id);
			if (!agent) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_status.cc_id);
				break;
			}
			if (subcmd->u.cc_status.status) {
				ast_cc_agent_caller_busy(agent->core_id, "%s agent caller is busy",
					sig_pri_cc_type_name);
			} else {
				ast_cc_agent_caller_available(agent->core_id,
					"%s agent caller is available", sig_pri_cc_type_name);
			}
			ao2_ref(agent, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_CANCEL:
			sig_pri_cc_link_canceled(pri, subcmd->u.cc_cancel.cc_id,
				subcmd->u.cc_cancel.is_agent);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_STOP_ALERTING:
			monitor = sig_pri_find_cc_monitor_by_cc_id(pri,
				subcmd->u.cc_stop_alerting.cc_id);
			if (!monitor) {
				pri_cc_cancel(pri->pri, subcmd->u.cc_stop_alerting.cc_id);
				break;
			}
			ast_cc_monitor_stop_ringing(monitor->core_id);
			ao2_ref(monitor, -1);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_AOC_EVENTS)
		case PRI_SUBCMD_AOC_E:
			/* Queue AST_CONTROL_AOC frame */
			sig_pri_aoc_e_from_pri(&subcmd->u.aoc_e, NULL, 0);
			break;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
		default:
			ast_debug(2, "Span %d: Unknown CIS subcommand(%d) in %s event.\n", pri->span,
				subcmd->cmd, pri_event2str(event_id));
			break;
		}
	}
}

/*!
 * \internal
 * \brief Handle the call associated PRI subcommand events.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param event_id PRI event id
 * \param subcmds Subcommands to process if any. (Could be NULL).
 * \param call_rsp libpri opaque call structure to send any responses toward.
 * Could be NULL either because it is not available or the call is for the
 * dummy call reference.  However, this should not be NULL in the cases that
 * need to use the pointer to send a response message back.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_subcmds(struct sig_pri_span *pri, int chanpos, int event_id,
	const struct pri_subcommands *subcmds, q931_call *call_rsp)
{
	int index;
	struct ast_channel *owner;
	struct ast_party_redirecting ast_redirecting;
#if defined(HAVE_PRI_TRANSFER)
	struct xfer_rsp_data xfer_rsp;
#endif	/* defined(HAVE_PRI_TRANSFER) */

	if (!subcmds) {
		return;
	}
	for (index = 0; index < subcmds->counter_subcmd; ++index) {
		const struct pri_subcommand *subcmd = &subcmds->subcmd[index];

		switch (subcmd->cmd) {
		case PRI_SUBCMD_CONNECTED_LINE:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				struct ast_party_connected_line ast_connected;
				int caller_id_update;

				/* Extract the connected line information */
				ast_party_connected_line_init(&ast_connected);
				sig_pri_party_id_convert(&ast_connected.id, &subcmd->u.connected_line.id,
					pri);
				ast_connected.id.tag = ast_strdup(pri->pvts[chanpos]->user_tag);

				caller_id_update = 0;
				if (ast_connected.id.name.str) {
					/* Save name for Caller-ID update */
					ast_copy_string(pri->pvts[chanpos]->cid_name,
						ast_connected.id.name.str, sizeof(pri->pvts[chanpos]->cid_name));
					caller_id_update = 1;
				}
				if (ast_connected.id.number.str) {
					/* Save number for Caller-ID update */
					ast_copy_string(pri->pvts[chanpos]->cid_num,
						ast_connected.id.number.str, sizeof(pri->pvts[chanpos]->cid_num));
					pri->pvts[chanpos]->cid_ton = ast_connected.id.number.plan;
					caller_id_update = 1;
				}
				ast_connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;

				pri->pvts[chanpos]->cid_subaddr[0] = '\0';
#if defined(HAVE_PRI_SUBADDR)
				if (ast_connected.id.subaddress.str) {
					ast_copy_string(pri->pvts[chanpos]->cid_subaddr,
						ast_connected.id.subaddress.str,
						sizeof(pri->pvts[chanpos]->cid_subaddr));
					caller_id_update = 1;
				}
#endif	/* defined(HAVE_PRI_SUBADDR) */
				if (caller_id_update) {
					struct ast_party_caller ast_caller;

					pri->pvts[chanpos]->callingpres =
						ast_party_id_presentation(&ast_connected.id);
					sig_pri_set_caller_id(pri->pvts[chanpos]);

					ast_party_caller_set_init(&ast_caller, ast_channel_caller(owner));
					ast_caller.id = ast_connected.id;
					ast_caller.ani = ast_connected.id;
					ast_channel_set_caller_event(owner, &ast_caller, NULL);

					/* Update the connected line information on the other channel */
					if (event_id != PRI_EVENT_RING) {
						/* This connected_line update was not from a SETUP message. */
						ast_channel_queue_connected_line_update(owner, &ast_connected,
							NULL);
					}
				}

				ast_party_connected_line_free(&ast_connected);
				ast_channel_unlock(owner);
			}
			break;
		case PRI_SUBCMD_REDIRECTING:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				sig_pri_redirecting_convert(&ast_redirecting, &subcmd->u.redirecting,
					ast_channel_redirecting(owner), pri);
				ast_redirecting.orig.tag = ast_strdup(pri->pvts[chanpos]->user_tag);
				ast_redirecting.from.tag = ast_strdup(pri->pvts[chanpos]->user_tag);
				ast_redirecting.to.tag = ast_strdup(pri->pvts[chanpos]->user_tag);
				ast_channel_set_redirecting(owner, &ast_redirecting, NULL);
				if (event_id != PRI_EVENT_RING) {
					/* This redirection was not from a SETUP message. */

					/* Invalidate any earlier private redirecting id representations */
					ast_party_id_invalidate(&ast_redirecting.priv_orig);
					ast_party_id_invalidate(&ast_redirecting.priv_from);
					ast_party_id_invalidate(&ast_redirecting.priv_to);

					ast_channel_queue_redirecting_update(owner, &ast_redirecting, NULL);
				}
				ast_party_redirecting_free(&ast_redirecting);

				ast_channel_unlock(owner);
			}
			break;
#if defined(HAVE_PRI_CALL_REROUTING)
		case PRI_SUBCMD_REROUTING:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				struct pri_party_redirecting pri_deflection;

				if (!call_rsp) {
					ast_log(LOG_WARNING,
						"Span %d: %s tried CallRerouting/CallDeflection to '%s' without call!\n",
						pri->span, ast_channel_name(owner), subcmd->u.rerouting.deflection.to.number.str);
					ast_channel_unlock(owner);
					break;
				}
				if (ast_strlen_zero(subcmd->u.rerouting.deflection.to.number.str)) {
					ast_log(LOG_WARNING,
						"Span %d: %s tried CallRerouting/CallDeflection to empty number!\n",
						pri->span, ast_channel_name(owner));
					pri_rerouting_rsp(pri->pri, call_rsp, subcmd->u.rerouting.invoke_id,
						PRI_REROUTING_RSP_INVALID_NUMBER);
					ast_channel_unlock(owner);
					break;
				}

				ast_verb(3, "Span %d: %s is CallRerouting/CallDeflection to '%s'.\n",
					pri->span, ast_channel_name(owner), subcmd->u.rerouting.deflection.to.number.str);

				/*
				 * Send back positive ACK to CallRerouting/CallDeflection.
				 *
				 * Note:  This call will be hungup by the core when it processes
				 * the call_forward string.
				 */
				pri_rerouting_rsp(pri->pri, call_rsp, subcmd->u.rerouting.invoke_id,
					PRI_REROUTING_RSP_OK_CLEAR);

				pri_deflection = subcmd->u.rerouting.deflection;

				/* Adjust the deflecting to number based upon the subscription option. */
				switch (subcmd->u.rerouting.subscription_option) {
				case 0:	/* noNotification */
				case 1:	/* notificationWithoutDivertedToNr */
					/* Delete the number because the far end is not supposed to see it. */
					pri_deflection.to.number.presentation =
						PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_UNSCREENED;
					pri_deflection.to.number.plan =
						(PRI_TON_UNKNOWN << 4) | PRI_NPI_E163_E164;
					pri_deflection.to.number.str[0] = '\0';
					break;
				case 2:	/* notificationWithDivertedToNr */
					break;
				case 3:	/* notApplicable */
				default:
					break;
				}
				sig_pri_redirecting_convert(&ast_redirecting, &pri_deflection,
					ast_channel_redirecting(owner), pri);
				ast_redirecting.orig.tag = ast_strdup(pri->pvts[chanpos]->user_tag);
				ast_redirecting.from.tag = ast_strdup(pri->pvts[chanpos]->user_tag);
				ast_redirecting.to.tag = ast_strdup(pri->pvts[chanpos]->user_tag);
				ast_channel_set_redirecting(owner, &ast_redirecting, NULL);
				ast_party_redirecting_free(&ast_redirecting);

				/* Request the core to forward to the new number. */
				ast_channel_call_forward_set(owner, subcmd->u.rerouting.deflection.to.number.str);

				/* Wake up the channel. */
				ast_queue_frame(owner, &ast_null_frame);

				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_CALL_REROUTING) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_AVAILABLE:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				enum ast_cc_service_type service;

				switch (event_id) {
				case PRI_EVENT_RINGING:
					service = AST_CC_CCNR;
					break;
				case PRI_EVENT_HANGUP_REQ:
					/* We will assume that the cause was busy/congestion. */
					service = AST_CC_CCBS;
					break;
				default:
					service = AST_CC_NONE;
					break;
				}
				if (service == AST_CC_NONE
					|| sig_pri_cc_available(pri, chanpos, subcmd->u.cc_available.cc_id,
					service)) {
					pri_cc_cancel(pri->pri, subcmd->u.cc_available.cc_id);
				}
				ast_channel_unlock(owner);
			} else {
				/* No asterisk channel. */
				pri_cc_cancel(pri->pri, subcmd->u.cc_available.cc_id);
			}
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_CALL:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				struct ast_cc_agent *agent;

				agent = sig_pri_find_cc_agent_by_cc_id(pri, subcmd->u.cc_call.cc_id);
				if (agent) {
					ast_setup_cc_recall_datastore(owner, agent->core_id);
					ast_cc_agent_set_interfaces_chanvar(owner);
					ast_cc_agent_recalling(agent->core_id,
						"%s caller is attempting recall", sig_pri_cc_type_name);
					ao2_ref(agent, -1);
				}

				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_CCSS)
		case PRI_SUBCMD_CC_CANCEL:
			sig_pri_cc_link_canceled(pri, subcmd->u.cc_cancel.cc_id,
				subcmd->u.cc_cancel.is_agent);
			break;
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_TRANSFER)
		case PRI_SUBCMD_TRANSFER_CALL:
			if (!call_rsp) {
				/* Should never happen. */
				ast_log(LOG_ERROR,
					"Call transfer subcommand without call to send response!\n");
				break;
			}

			sig_pri_unlock_private(pri->pvts[chanpos]);
			xfer_rsp.pri = pri;
			xfer_rsp.call = call_rsp;
			xfer_rsp.invoke_id = subcmd->u.transfer.invoke_id;
			xfer_rsp.responded = 0;
			sig_pri_attempt_transfer(pri,
				subcmd->u.transfer.call_1, subcmd->u.transfer.is_call_1_held,
				subcmd->u.transfer.call_2, subcmd->u.transfer.is_call_2_held,
				&xfer_rsp);
			sig_pri_lock_private(pri->pvts[chanpos]);
			break;
#endif	/* defined(HAVE_PRI_TRANSFER) */
#if defined(HAVE_PRI_AOC_EVENTS)
		case PRI_SUBCMD_AOC_S:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				sig_pri_aoc_s_from_pri(&subcmd->u.aoc_s, owner,
					(pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_S));
				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
#if defined(HAVE_PRI_AOC_EVENTS)
		case PRI_SUBCMD_AOC_D:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				/* Queue AST_CONTROL_AOC frame on channel */
				sig_pri_aoc_d_from_pri(&subcmd->u.aoc_d, owner,
					(pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_D));
				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
#if defined(HAVE_PRI_AOC_EVENTS)
		case PRI_SUBCMD_AOC_E:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			/* Queue AST_CONTROL_AOC frame */
			sig_pri_aoc_e_from_pri(&subcmd->u.aoc_e, owner,
				(pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_E));
			if (owner) {
				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
#if defined(HAVE_PRI_AOC_EVENTS)
		case PRI_SUBCMD_AOC_CHARGING_REQ:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				sig_pri_aoc_request_from_pri(&subcmd->u.aoc_request, pri->pvts[chanpos],
					call_rsp);
				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
#if defined(HAVE_PRI_AOC_EVENTS)
		case PRI_SUBCMD_AOC_CHARGING_REQ_RSP:
			/*
			 * An AOC request response may contain an AOC-S rate list.
			 * If this is the case handle this just like we
			 * would an incoming AOC-S msg.
			 */
			if (subcmd->u.aoc_request_response.valid_aoc_s) {
				sig_pri_lock_owner(pri, chanpos);
				owner = pri->pvts[chanpos]->owner;
				if (owner) {
					sig_pri_aoc_s_from_pri(&subcmd->u.aoc_request_response.aoc_s, owner,
						(pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_S));
					ast_channel_unlock(owner);
				}
			}
			break;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
#if defined(HAVE_PRI_MCID)
		case PRI_SUBCMD_MCID_REQ:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			sig_pri_mcid_event(pri, &subcmd->u.mcid_req, owner);
			if (owner) {
				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_MCID) */
#if defined(HAVE_PRI_MCID)
		case PRI_SUBCMD_MCID_RSP:
			/* Ignore for now. */
			break;
#endif	/* defined(HAVE_PRI_MCID) */
#if defined(HAVE_PRI_DISPLAY_TEXT)
		case PRI_SUBCMD_DISPLAY_TEXT:
			if (event_id != PRI_EVENT_RING) {
				/*
				 * This display text was not from a SETUP message.  We can do
				 * something with this display text string.
				 */
				sig_pri_lock_owner(pri, chanpos);
				owner = pri->pvts[chanpos]->owner;
				if (owner) {
					struct ast_frame f;

					/* Pass the display text to the peer channel. */
					memset(&f, 0, sizeof(f));
					f.frametype = AST_FRAME_TEXT;
					f.subclass.integer = 0;
					f.offset = 0;
					f.data.ptr = &subcmd->u.display.text;
					f.datalen = subcmd->u.display.length + 1;
					ast_queue_frame(owner, &f);
					ast_channel_unlock(owner);
				}
			}
			break;
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */
		default:
			ast_debug(2, "Span %d: Unknown call subcommand(%d) in %s event.\n",
				pri->span, subcmd->cmd, pri_event2str(event_id));
			break;
		}
	}
}

/*!
 * \internal
 * \brief Convert the MOH state to string.
 * \since 10.0
 *
 * \param state MOH state to process.
 *
 * \return String version of MOH state.
 */
static const char *sig_pri_moh_state_str(enum sig_pri_moh_state state)
{
	const char *str;

	str = "Unknown";
	switch (state) {
	case SIG_PRI_MOH_STATE_IDLE:
		str = "SIG_PRI_MOH_STATE_IDLE";
		break;
	case SIG_PRI_MOH_STATE_NOTIFY:
		str = "SIG_PRI_MOH_STATE_NOTIFY";
		break;
	case SIG_PRI_MOH_STATE_MOH:
		str = "SIG_PRI_MOH_STATE_MOH";
		break;
#if defined(HAVE_PRI_CALL_HOLD)
	case SIG_PRI_MOH_STATE_HOLD_REQ:
		str = "SIG_PRI_MOH_STATE_HOLD_REQ";
		break;
	case SIG_PRI_MOH_STATE_PEND_UNHOLD:
		str = "SIG_PRI_MOH_STATE_PEND_UNHOLD";
		break;
	case SIG_PRI_MOH_STATE_HOLD:
		str = "SIG_PRI_MOH_STATE_HOLD";
		break;
	case SIG_PRI_MOH_STATE_RETRIEVE_REQ:
		str = "SIG_PRI_MOH_STATE_RETRIEVE_REQ";
		break;
	case SIG_PRI_MOH_STATE_PEND_HOLD:
		str = "SIG_PRI_MOH_STATE_PEND_HOLD";
		break;
	case SIG_PRI_MOH_STATE_RETRIEVE_FAIL:
		str = "SIG_PRI_MOH_STATE_RETRIEVE_FAIL";
		break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
	case SIG_PRI_MOH_STATE_NUM:
		/* Not a real state. */
		break;
	}
	return str;
}

/*!
 * \internal
 * \brief Convert the MOH event to string.
 * \since 10.0
 *
 * \param event MOH event to process.
 *
 * \return String version of MOH event.
 */
static const char *sig_pri_moh_event_str(enum sig_pri_moh_event event)
{
	const char *str;

	str = "Unknown";
	switch (event) {
	case SIG_PRI_MOH_EVENT_RESET:
		str = "SIG_PRI_MOH_EVENT_RESET";
		break;
	case SIG_PRI_MOH_EVENT_HOLD:
		str = "SIG_PRI_MOH_EVENT_HOLD";
		break;
	case SIG_PRI_MOH_EVENT_UNHOLD:
		str = "SIG_PRI_MOH_EVENT_UNHOLD";
		break;
#if defined(HAVE_PRI_CALL_HOLD)
	case SIG_PRI_MOH_EVENT_HOLD_ACK:
		str = "SIG_PRI_MOH_EVENT_HOLD_ACK";
		break;
	case SIG_PRI_MOH_EVENT_HOLD_REJ:
		str = "SIG_PRI_MOH_EVENT_HOLD_REJ";
		break;
	case SIG_PRI_MOH_EVENT_RETRIEVE_ACK:
		str = "SIG_PRI_MOH_EVENT_RETRIEVE_ACK";
		break;
	case SIG_PRI_MOH_EVENT_RETRIEVE_REJ:
		str = "SIG_PRI_MOH_EVENT_RETRIEVE_REJ";
		break;
	case SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK:
		str = "SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK";
		break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
	case SIG_PRI_MOH_EVENT_NUM:
		/* Not a real event. */
		break;
	}
	return str;
}

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Retrieve a call that was placed on hold by the HOLD message.
 * \since 10.0
 *
 * \param pvt Channel private control structure.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_retrieve_call(struct sig_pri_chan *pvt)
{
	int chanpos;
	int channel;

	if (pvt->pri->nodetype == PRI_NETWORK) {
		/* Find an available channel to propose */
		chanpos = pri_find_empty_chan(pvt->pri, 1);
		if (chanpos < 0) {
			/* No channels available. */
			return SIG_PRI_MOH_STATE_RETRIEVE_FAIL;
		}
		channel = PVT_TO_CHANNEL(pvt->pri->pvts[chanpos]);

		/*
		 * We cannot occupy or reserve this channel at this time because
		 * the retrieve may fail or we could have a RETRIEVE collision.
		 */
	} else {
		/* Let the network pick the channel. */
		channel = 0;
	}

	if (pri_retrieve(pvt->pri->pri, pvt->call, channel)) {
		return SIG_PRI_MOH_STATE_RETRIEVE_FAIL;
	}
	return SIG_PRI_MOH_STATE_RETRIEVE_REQ;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

/*!
 * \internal
 * \brief MOH FSM state idle.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_idle(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_HOLD:
		if (!strcasecmp(pvt->mohinterpret, "passthrough")) {
			/*
			 * This config setting is deprecated.
			 * The old way did not send MOH just in case the notification was ignored.
			 */
			pri_notify(pvt->pri->pri, pvt->call, pvt->prioffset, PRI_NOTIFY_REMOTE_HOLD);
			next_state = SIG_PRI_MOH_STATE_NOTIFY;
			break;
		}

		switch (pvt->pri->moh_signaling) {
		default:
		case SIG_PRI_MOH_SIGNALING_MOH:
			ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
			next_state = SIG_PRI_MOH_STATE_MOH;
			break;
		case SIG_PRI_MOH_SIGNALING_NOTIFY:
			/* Send MOH anyway in case the far end does not interpret the notification. */
			ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);

			pri_notify(pvt->pri->pri, pvt->call, pvt->prioffset, PRI_NOTIFY_REMOTE_HOLD);
			next_state = SIG_PRI_MOH_STATE_NOTIFY;
			break;
#if defined(HAVE_PRI_CALL_HOLD)
		case SIG_PRI_MOH_SIGNALING_HOLD:
			if (pri_hold(pvt->pri->pri, pvt->call)) {
				/* Fall back to MOH instead */
				ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
				next_state = SIG_PRI_MOH_STATE_MOH;
			} else {
				next_state = SIG_PRI_MOH_STATE_HOLD_REQ;
			}
			break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
		}
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}

/*!
 * \internal
 * \brief MOH FSM state notify remote party.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_notify(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_HOLD:
		if (strcasecmp(pvt->mohinterpret, "passthrough")) {
			/* Restart MOH in case it was stopped by other means. */
			ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
		}
		break;
	case SIG_PRI_MOH_EVENT_UNHOLD:
		pri_notify(pvt->pri->pri, pvt->call, pvt->prioffset, PRI_NOTIFY_REMOTE_RETRIEVAL);
		/* Fall through */
	case SIG_PRI_MOH_EVENT_RESET:
		ast_moh_stop(chan);
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}

/*!
 * \internal
 * \brief MOH FSM state generate moh.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_moh(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_HOLD:
		/* Restart MOH in case it was stopped by other means. */
		ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
		break;
	case SIG_PRI_MOH_EVENT_RESET:
	case SIG_PRI_MOH_EVENT_UNHOLD:
		ast_moh_stop(chan);
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief MOH FSM state hold requested.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_hold_req(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_RESET:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_UNHOLD:
		next_state = SIG_PRI_MOH_STATE_PEND_UNHOLD;
		break;
	case SIG_PRI_MOH_EVENT_HOLD_REJ:
		/* Fall back to MOH */
		if (chan) {
			ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
		}
		next_state = SIG_PRI_MOH_STATE_MOH;
		break;
	case SIG_PRI_MOH_EVENT_HOLD_ACK:
		next_state = SIG_PRI_MOH_STATE_HOLD;
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief MOH FSM state hold requested with pending unhold.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_pend_unhold(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_RESET:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_HOLD:
		next_state = SIG_PRI_MOH_STATE_HOLD_REQ;
		break;
	case SIG_PRI_MOH_EVENT_HOLD_REJ:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_HOLD_ACK:
		next_state = sig_pri_moh_retrieve_call(pvt);
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief MOH FSM state hold.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_hold(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_RESET:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_UNHOLD:
		next_state = sig_pri_moh_retrieve_call(pvt);
		break;
	case SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK:
		/* Fall back to MOH */
		if (chan) {
			ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
		}
		next_state = SIG_PRI_MOH_STATE_MOH;
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief MOH FSM state retrieve requested.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_retrieve_req(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_RESET:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_HOLD:
		next_state = SIG_PRI_MOH_STATE_PEND_HOLD;
		break;
	case SIG_PRI_MOH_EVENT_RETRIEVE_ACK:
	case SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_RETRIEVE_REJ:
		next_state = SIG_PRI_MOH_STATE_RETRIEVE_FAIL;
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief MOH FSM state retrieve requested with pending hold.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_pend_hold(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_RESET:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_UNHOLD:
		next_state = SIG_PRI_MOH_STATE_RETRIEVE_REQ;
		break;
	case SIG_PRI_MOH_EVENT_RETRIEVE_ACK:
	case SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK:
		/*
		 * Successfully came off of hold.  Now we can reinterpret the
		 * MOH signaling option to handle the pending HOLD request.
		 */
		switch (pvt->pri->moh_signaling) {
		default:
		case SIG_PRI_MOH_SIGNALING_MOH:
			if (chan) {
				ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
			}
			next_state = SIG_PRI_MOH_STATE_MOH;
			break;
		case SIG_PRI_MOH_SIGNALING_NOTIFY:
			/* Send MOH anyway in case the far end does not interpret the notification. */
			if (chan) {
				ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
			}

			pri_notify(pvt->pri->pri, pvt->call, pvt->prioffset, PRI_NOTIFY_REMOTE_HOLD);
			next_state = SIG_PRI_MOH_STATE_NOTIFY;
			break;
		case SIG_PRI_MOH_SIGNALING_HOLD:
			if (pri_hold(pvt->pri->pri, pvt->call)) {
				/* Fall back to MOH instead */
				if (chan) {
					ast_moh_start(chan, pvt->moh_suggested, pvt->mohinterpret);
				}
				next_state = SIG_PRI_MOH_STATE_MOH;
			} else {
				next_state = SIG_PRI_MOH_STATE_HOLD_REQ;
			}
			break;
		}
		break;
	case SIG_PRI_MOH_EVENT_RETRIEVE_REJ:
		/*
		 * We cannot reinterpret the MOH signaling option because we
		 * failed to come off of hold.
		 */
		next_state = SIG_PRI_MOH_STATE_HOLD;
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief MOH FSM state retrieve failed.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
static enum sig_pri_moh_state sig_pri_moh_fsm_retrieve_fail(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state next_state;

	next_state = pvt->moh_state;
	switch (event) {
	case SIG_PRI_MOH_EVENT_RESET:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	case SIG_PRI_MOH_EVENT_HOLD:
		next_state = SIG_PRI_MOH_STATE_HOLD;
		break;
	case SIG_PRI_MOH_EVENT_UNHOLD:
		next_state = sig_pri_moh_retrieve_call(pvt);
		break;
	case SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK:
		next_state = SIG_PRI_MOH_STATE_IDLE;
		break;
	default:
		break;
	}
	pvt->moh_state = next_state;
	return next_state;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

/*!
 * \internal
 * \brief MOH FSM state function type.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Next MOH state
 */
typedef enum sig_pri_moh_state (*sig_pri_moh_fsm_state)(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event);

/*! MOH FSM state table. */
static const sig_pri_moh_fsm_state sig_pri_moh_fsm[SIG_PRI_MOH_STATE_NUM] = {
/* *INDENT-OFF* */
	[SIG_PRI_MOH_STATE_IDLE] = sig_pri_moh_fsm_idle,
	[SIG_PRI_MOH_STATE_NOTIFY] = sig_pri_moh_fsm_notify,
	[SIG_PRI_MOH_STATE_MOH] = sig_pri_moh_fsm_moh,
#if defined(HAVE_PRI_CALL_HOLD)
	[SIG_PRI_MOH_STATE_HOLD_REQ] = sig_pri_moh_fsm_hold_req,
	[SIG_PRI_MOH_STATE_PEND_UNHOLD] = sig_pri_moh_fsm_pend_unhold,
	[SIG_PRI_MOH_STATE_HOLD] = sig_pri_moh_fsm_hold,
	[SIG_PRI_MOH_STATE_RETRIEVE_REQ] = sig_pri_moh_fsm_retrieve_req,
	[SIG_PRI_MOH_STATE_PEND_HOLD] = sig_pri_moh_fsm_pend_hold,
	[SIG_PRI_MOH_STATE_RETRIEVE_FAIL] = sig_pri_moh_fsm_retrieve_fail,
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
/* *INDENT-ON* */
};

/*!
 * \internal
 * \brief Send an event to the MOH FSM.
 * \since 10.0
 *
 * \param chan Channel to post event to (Usually pvt->owner)
 * \param pvt Channel private control structure.
 * \param event MOH event to process.
 *
 * \note Assumes the pvt->pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pvt) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_moh_fsm_event(struct ast_channel *chan, struct sig_pri_chan *pvt, enum sig_pri_moh_event event)
{
	enum sig_pri_moh_state orig_state;
	enum sig_pri_moh_state next_state;
	const char *chan_name;

	if (chan) {
		chan_name = ast_strdupa(ast_channel_name(chan));
	} else {
		chan_name = "Unknown";
	}
	orig_state = pvt->moh_state;
	ast_debug(2, "Channel '%s' MOH-Event: %s in state %s\n", chan_name,
		sig_pri_moh_event_str(event), sig_pri_moh_state_str(orig_state));
	if (orig_state < SIG_PRI_MOH_STATE_IDLE || SIG_PRI_MOH_STATE_NUM <= orig_state
		|| !sig_pri_moh_fsm[orig_state]) {
		/* Programming error: State not implemented. */
		ast_log(LOG_ERROR, "MOH state not implemented: %s(%u)\n",
			sig_pri_moh_state_str(orig_state), orig_state);
		return;
	}
	/* Execute the state. */
	next_state = sig_pri_moh_fsm[orig_state](chan, pvt, event);
	ast_debug(2, "Channel '%s'  MOH-Next-State: %s\n", chan_name,
		(orig_state == next_state) ? "$" : sig_pri_moh_state_str(next_state));
}

/*!
 * \internal
 * \brief Set callid threadstorage for the pri_dchannel thread when a new call is created
 *
 * \return A new callid which has been bound to threadstorage. The return must be
 *         unreffed and the threadstorage should be unbound when the pri_dchannel
 *         primary loop wraps.
 */
static struct ast_callid *func_pri_dchannel_new_callid(void)
{
	struct ast_callid *callid = ast_create_callid();

	if (callid) {
		ast_callid_threadassoc_add(callid);
	}

	return callid;
}

/*!
 * \internal
 * \brief Set callid threadstorage for the pri_dchannel thread to that of an existing channel
 *
 * \param pri PRI span control structure.
 * \param chanpos channel position in the span
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return a reference to the callid bound to the channel which has also
 *         been bound to threadstorage if it exists. If this returns non-NULL,
 *         the callid must be unreffed and the threadstorage should be unbound
 *         when the pri_dchannel primary loop wraps.
 */
static struct ast_callid *func_pri_dchannel_chanpos_callid(struct sig_pri_span *pri, int chanpos)
{
	if (chanpos < 0) {
		return NULL;
	}

	sig_pri_lock_owner(pri, chanpos);
	if (pri->pvts[chanpos]->owner) {
		struct ast_callid *callid;
		callid = ast_channel_callid(pri->pvts[chanpos]->owner);
		ast_channel_unlock(pri->pvts[chanpos]->owner);
		if (callid) {
			ast_callid_threadassoc_add(callid);
			return callid;
		}
	}

	return NULL;
}

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the hold event from libpri.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param ev Hold event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int sig_pri_handle_hold(struct sig_pri_span *pri, pri_event *ev)
{
	int retval;
	int chanpos_old;
	int chanpos_new;
	struct ast_channel *owner;
	struct ast_callid *callid = NULL;

	chanpos_old = pri_find_principle_by_call(pri, ev->hold.call);
	if (chanpos_old < 0) {
		ast_log(LOG_WARNING, "Span %d: Received HOLD for unknown call.\n", pri->span);
		return -1;
	}
	if (pri->pvts[chanpos_old]->no_b_channel) {
		/* Call is already on hold or is call waiting call. */
		return -1;
	}

	chanpos_new = -1;

	sig_pri_lock_private(pri->pvts[chanpos_old]);
	sig_pri_lock_owner(pri, chanpos_old);
	owner = pri->pvts[chanpos_old]->owner;
	if (!owner) {
		goto done_with_private;
	}

	callid = ast_channel_callid(owner);

	if (callid) {
		ast_callid_threadassoc_add(callid);
	}

	if (pri->pvts[chanpos_old]->call_level != SIG_PRI_CALL_LEVEL_CONNECT) {
		/*
		 * Make things simple.  Don't allow placing a call on hold that
		 * is not connected.
		 */
		goto done_with_owner;
	}
	chanpos_new = pri_find_empty_nobch(pri);
	if (chanpos_new < 0) {
		/* No hold channel available. */
		goto done_with_owner;
	}
	sig_pri_handle_subcmds(pri, chanpos_old, ev->e, ev->hold.subcmds, ev->hold.call);
	sig_pri_queue_hold(pri, chanpos_old);
	chanpos_new = pri_fixup_principle(pri, chanpos_new, ev->hold.call);
	if (chanpos_new < 0) {
		/* Should never happen. */
		sig_pri_queue_unhold(pri, chanpos_old);
	}

done_with_owner:;
	ast_channel_unlock(owner);
done_with_private:;
	sig_pri_unlock_private(pri->pvts[chanpos_old]);

	if (chanpos_new < 0) {
		retval = -1;
	} else {
		sig_pri_span_devstate_changed(pri);
		retval = 0;
	}

	if (callid) {
		ast_callid_unref(callid);
		ast_callid_threadassoc_remove();
	}

	return retval;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the hold acknowledge event from libpri.
 * \since 10.0
 *
 * \param pri PRI span control structure.
 * \param ev Hold acknowledge event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_hold_ack(struct sig_pri_span *pri, pri_event *ev)
{
	int chanpos;
	struct ast_callid *callid;

	/*
	 * We were successfully put on hold by the remote party
	 * so we just need to switch to a no_b_channel channel.
	 */
	chanpos = pri_find_empty_nobch(pri);
	if (chanpos < 0) {
		/* Very bad news.  No hold channel available. */
		ast_log(LOG_ERROR,
			"Span %d: No hold channel available for held call that is on %d/%d\n",
			pri->span, PRI_SPAN(ev->hold_ack.channel), PRI_CHANNEL(ev->hold_ack.channel));
		sig_pri_kill_call(pri, ev->hold_ack.call, PRI_CAUSE_RESOURCE_UNAVAIL_UNSPECIFIED);
		return;
	}
	chanpos = pri_fixup_principle(pri, chanpos, ev->hold_ack.call);
	if (chanpos < 0) {
		/* Should never happen. */
		sig_pri_kill_call(pri, ev->hold_ack.call, PRI_CAUSE_NORMAL_TEMPORARY_FAILURE);
		return;
	}

	sig_pri_lock_private(pri->pvts[chanpos]);
	callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

	sig_pri_handle_subcmds(pri, chanpos, ev->e, ev->hold_ack.subcmds, ev->hold_ack.call);
	sig_pri_moh_fsm_event(pri->pvts[chanpos]->owner, pri->pvts[chanpos],
		SIG_PRI_MOH_EVENT_HOLD_ACK);
	sig_pri_unlock_private(pri->pvts[chanpos]);
	sig_pri_span_devstate_changed(pri);

	if (callid) {
		ast_callid_unref(callid);
		ast_callid_threadassoc_remove();
	}
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the hold reject event from libpri.
 * \since 10.0
 *
 * \param pri PRI span control structure.
 * \param ev Hold reject event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_hold_rej(struct sig_pri_span *pri, pri_event *ev)
{
	int chanpos;
	struct ast_callid *callid;

	chanpos = pri_find_principle(pri, ev->hold_rej.channel, ev->hold_rej.call);
	if (chanpos < 0) {
		ast_log(LOG_WARNING, "Span %d: Could not find principle for HOLD_REJECT\n",
			pri->span);
		sig_pri_kill_call(pri, ev->hold_rej.call, PRI_CAUSE_NORMAL_TEMPORARY_FAILURE);
		return;
	}
	chanpos = pri_fixup_principle(pri, chanpos, ev->hold_rej.call);
	if (chanpos < 0) {
		/* Should never happen. */
		sig_pri_kill_call(pri, ev->hold_rej.call, PRI_CAUSE_NORMAL_TEMPORARY_FAILURE);
		return;
	}

	ast_debug(1, "Span %d: HOLD_REJECT cause: %d(%s)\n", pri->span,
		ev->hold_rej.cause, pri_cause2str(ev->hold_rej.cause));

	sig_pri_lock_private(pri->pvts[chanpos]);
	callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

	sig_pri_handle_subcmds(pri, chanpos, ev->e, ev->hold_rej.subcmds, ev->hold_rej.call);
	sig_pri_moh_fsm_event(pri->pvts[chanpos]->owner, pri->pvts[chanpos],
		SIG_PRI_MOH_EVENT_HOLD_REJ);
	sig_pri_unlock_private(pri->pvts[chanpos]);

	if (callid) {
		ast_callid_unref(callid);
		ast_callid_threadassoc_remove();
	}
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the retrieve event from libpri.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param ev Retrieve event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_retrieve(struct sig_pri_span *pri, pri_event *ev)
{
	int chanpos;
	struct ast_callid *callid;

	if (!(ev->retrieve.channel & PRI_HELD_CALL)) {
		/* The call is not currently held. */
		pri_retrieve_rej(pri->pri, ev->retrieve.call,
			PRI_CAUSE_RESOURCE_UNAVAIL_UNSPECIFIED);
		return;
	}
	if (pri_find_principle_by_call(pri, ev->retrieve.call) < 0) {
		ast_log(LOG_WARNING, "Span %d: Received RETRIEVE for unknown call.\n", pri->span);
		pri_retrieve_rej(pri->pri, ev->retrieve.call,
			PRI_CAUSE_RESOURCE_UNAVAIL_UNSPECIFIED);
		return;
	}
	if (PRI_CHANNEL(ev->retrieve.channel) == 0xFF) {
		chanpos = pri_find_empty_chan(pri, 1);
	} else {
		chanpos = pri_find_principle(pri,
			ev->retrieve.channel & ~PRI_HELD_CALL, ev->retrieve.call);
		if (ev->retrieve.flexible
			&& (chanpos < 0 || !sig_pri_is_chan_available(pri->pvts[chanpos]))) {
			/*
			 * Channel selection is flexible and the requested channel
			 * is bad or not available.  Pick another channel.
			 */
			chanpos = pri_find_empty_chan(pri, 1);
		}
	}
	if (chanpos < 0) {
		pri_retrieve_rej(pri->pri, ev->retrieve.call,
			ev->retrieve.flexible ? PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION
			: PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
		return;
	}
	chanpos = pri_fixup_principle(pri, chanpos, ev->retrieve.call);
	if (chanpos < 0) {
		/* Channel is already in use. */
		pri_retrieve_rej(pri->pri, ev->retrieve.call,
			PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
		return;
	}
	sig_pri_lock_private(pri->pvts[chanpos]);
	callid = func_pri_dchannel_chanpos_callid(pri, chanpos);
	sig_pri_handle_subcmds(pri, chanpos, ev->e, ev->retrieve.subcmds, ev->retrieve.call);
	sig_pri_queue_unhold(pri, chanpos);
	pri_retrieve_ack(pri->pri, ev->retrieve.call,
		PVT_TO_CHANNEL(pri->pvts[chanpos]));
	sig_pri_moh_fsm_event(pri->pvts[chanpos]->owner, pri->pvts[chanpos],
		SIG_PRI_MOH_EVENT_REMOTE_RETRIEVE_ACK);
	sig_pri_unlock_private(pri->pvts[chanpos]);
	sig_pri_span_devstate_changed(pri);

	if (callid) {
		ast_callid_unref(callid);
		ast_callid_threadassoc_remove();
	}
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the retrieve acknowledge event from libpri.
 * \since 10.0
 *
 * \param pri PRI span control structure.
 * \param ev Retrieve acknowledge event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_retrieve_ack(struct sig_pri_span *pri, pri_event *ev)
{
	int chanpos;
	struct ast_callid *callid;

	chanpos = pri_find_fixup_principle(pri, ev->retrieve_ack.channel,
		ev->retrieve_ack.call);
	if (chanpos < 0) {
		return;
	}

	sig_pri_lock_private(pri->pvts[chanpos]);
	callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

	sig_pri_handle_subcmds(pri, chanpos, ev->e, ev->retrieve_ack.subcmds,
		ev->retrieve_ack.call);
	sig_pri_moh_fsm_event(pri->pvts[chanpos]->owner, pri->pvts[chanpos],
		SIG_PRI_MOH_EVENT_RETRIEVE_ACK);
	sig_pri_unlock_private(pri->pvts[chanpos]);
	sig_pri_span_devstate_changed(pri);

	if (callid) {
		ast_callid_unref(callid);
		ast_callid_threadassoc_remove();
	}
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the retrieve reject event from libpri.
 * \since 10.0
 *
 * \param pri PRI span control structure.
 * \param ev Retrieve reject event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_retrieve_rej(struct sig_pri_span *pri, pri_event *ev)
{
	int chanpos;
	struct ast_callid *callid;

	chanpos = pri_find_principle(pri, ev->retrieve_rej.channel, ev->retrieve_rej.call);
	if (chanpos < 0) {
		ast_log(LOG_WARNING, "Span %d: Could not find principle for RETRIEVE_REJECT\n",
			pri->span);
		sig_pri_kill_call(pri, ev->retrieve_rej.call, PRI_CAUSE_NORMAL_TEMPORARY_FAILURE);
		return;
	}
	chanpos = pri_fixup_principle(pri, chanpos, ev->retrieve_rej.call);
	if (chanpos < 0) {
		/* Should never happen. */
		sig_pri_kill_call(pri, ev->retrieve_rej.call, PRI_CAUSE_NORMAL_TEMPORARY_FAILURE);
		return;
	}

	ast_debug(1, "Span %d: RETRIEVE_REJECT cause: %d(%s)\n", pri->span,
		ev->retrieve_rej.cause, pri_cause2str(ev->retrieve_rej.cause));

	sig_pri_lock_private(pri->pvts[chanpos]);
	callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

	sig_pri_handle_subcmds(pri, chanpos, ev->e, ev->retrieve_rej.subcmds,
		ev->retrieve_rej.call);
	sig_pri_moh_fsm_event(pri->pvts[chanpos]->owner, pri->pvts[chanpos],
		SIG_PRI_MOH_EVENT_RETRIEVE_REJ);
	sig_pri_unlock_private(pri->pvts[chanpos]);

	if (callid) {
		ast_callid_unref(callid);
		ast_callid_threadassoc_remove();
	}
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

/*!
 * \internal
 * \brief Setup channel variables on the owner.
 *
 * \param pri PRI span control structure.
 * \param chanpos Channel position in the span.
 * \param ev SETUP event received.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void setup_incoming_channel(struct sig_pri_span *pri, int chanpos, pri_event *ev)
{
	struct ast_channel *owner;
	char ani2str[6];
	char calledtonstr[10];

	sig_pri_lock_owner(pri, chanpos);
	owner = pri->pvts[chanpos]->owner;
	if (!owner) {
		return;
	}

	ast_channel_stage_snapshot(owner);

#if defined(HAVE_PRI_SUBADDR)
	if (ev->ring.calling.subaddress.valid) {
		/* Set Calling Subaddress */
		sig_pri_set_subaddress(&ast_channel_caller(owner)->id.subaddress,
			&ev->ring.calling.subaddress);
		if (!ev->ring.calling.subaddress.type
			&& !ast_strlen_zero((char *) ev->ring.calling.subaddress.data)) {
			/* NSAP */
			pbx_builtin_setvar_helper(owner, "CALLINGSUBADDR",
				(char *) ev->ring.calling.subaddress.data);
		}
	}
	if (ev->ring.called_subaddress.valid) {
		/* Set Called Subaddress */
		sig_pri_set_subaddress(&ast_channel_dialed(owner)->subaddress,
			&ev->ring.called_subaddress);
		if (!ev->ring.called_subaddress.type
			&& !ast_strlen_zero((char *) ev->ring.called_subaddress.data)) {
			/* NSAP */
			pbx_builtin_setvar_helper(owner, "CALLEDSUBADDR",
				(char *) ev->ring.called_subaddress.data);
		}
	}
#else
	if (!ast_strlen_zero(ev->ring.callingsubaddr)) {
		pbx_builtin_setvar_helper(owner, "CALLINGSUBADDR", ev->ring.callingsubaddr);
	}
#endif /* !defined(HAVE_PRI_SUBADDR) */
	if (ev->ring.ani2 >= 0) {
		ast_channel_caller(owner)->ani2 = ev->ring.ani2;
		snprintf(ani2str, sizeof(ani2str), "%d", ev->ring.ani2);
		pbx_builtin_setvar_helper(owner, "ANI2", ani2str);
	}

#ifdef SUPPORT_USERUSER
	if (!ast_strlen_zero(ev->ring.useruserinfo)) {
		pbx_builtin_setvar_helper(owner, "USERUSERINFO", ev->ring.useruserinfo);
	}
#endif

	snprintf(calledtonstr, sizeof(calledtonstr), "%d", ev->ring.calledplan);
	pbx_builtin_setvar_helper(owner, "CALLEDTON", calledtonstr);
	ast_channel_dialed(owner)->number.plan = ev->ring.calledplan;

	if (ev->ring.redirectingreason >= 0) {
		/* This is now just a status variable.  Use REDIRECTING() dialplan function. */
		pbx_builtin_setvar_helper(owner, "PRIREDIRECTREASON",
			redirectingreason2str(ev->ring.redirectingreason));
	}
#if defined(HAVE_PRI_REVERSE_CHARGE)
	pri->pvts[chanpos]->reverse_charging_indication = ev->ring.reversecharge;
#endif
#if defined(HAVE_PRI_SETUP_KEYPAD)
	ast_copy_string(pri->pvts[chanpos]->keypad_digits,
		ev->ring.keypad_digits, sizeof(pri->pvts[chanpos]->keypad_digits));
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */

	/*
	 * It's ok to call this with the owner already locked here
	 * since it will want to do this anyway if there are any
	 * subcmds.
	 */
	sig_pri_handle_subcmds(pri, chanpos, ev->e, ev->ring.subcmds,
		ev->ring.call);

	ast_channel_stage_snapshot_done(owner);
	ast_channel_unlock(owner);
}

/*!
 * \internal
 * \brief Handle the incoming SETUP event from libpri.
 *
 * \param pri PRI span control structure.
 * \param e SETUP event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_setup(struct sig_pri_span *pri, pri_event *e)
{
	int exten_exists_or_can_exist;
	int could_match_more;
	int need_dialtone;
	enum sig_pri_law law;
	int chanpos = -1;
	struct ast_callid *callid = NULL;
	struct ast_channel *c;
	char plancallingnum[AST_MAX_EXTENSION];
	char plancallingani[AST_MAX_EXTENSION];
	pthread_t threadid;

	if (!ast_strlen_zero(pri->msn_list)
		&& !sig_pri_msn_match(pri->msn_list, e->ring.callednum)) {
		/* The call is not for us so ignore it. */
		ast_verb(3,
			"Ignoring call to '%s' on span %d.  Its not in the MSN list: %s\n",
			e->ring.callednum, pri->span, pri->msn_list);
		pri_destroycall(pri->pri, e->ring.call);
		goto setup_exit;
	}
	if (sig_pri_is_cis_call(e->ring.channel)) {
		sig_pri_handle_cis_subcmds(pri, e->e, e->ring.subcmds, e->ring.call);
		goto setup_exit;
	}
	chanpos = pri_find_principle_by_call(pri, e->ring.call);
	if (-1 < chanpos) {
		/* Libpri has already filtered out duplicate SETUPs. */
		ast_log(LOG_WARNING,
			"Span %d: Got SETUP with duplicate call ptr (%p).  Dropping call.\n",
			pri->span, e->ring.call);
		pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_NORMAL_TEMPORARY_FAILURE);
		goto setup_exit;
	}
	if (e->ring.channel == -1 || PRI_CHANNEL(e->ring.channel) == 0xFF) {
		/* Any channel requested. */
		chanpos = pri_find_empty_chan(pri, 1);
		if (-1 < chanpos) {
			callid = func_pri_dchannel_new_callid();
		}
	} else if (PRI_CHANNEL(e->ring.channel) == 0x00) {
		/* No channel specified. */
#if defined(HAVE_PRI_CALL_WAITING)
		if (!pri->allow_call_waiting_calls)
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
		{
			/* We will not accept incoming call waiting calls. */
			pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
			goto setup_exit;
		}
#if defined(HAVE_PRI_CALL_WAITING)
		chanpos = pri_find_empty_nobch(pri);
		if (chanpos < 0) {
			/* We could not find/create a call interface. */
			pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
			goto setup_exit;
		}

		callid = func_pri_dchannel_new_callid();

		/* Setup the call interface to use. */
		sig_pri_init_config(pri->pvts[chanpos], pri);
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
	} else {
		/* A channel is specified. */
		callid = func_pri_dchannel_new_callid();
		chanpos = pri_find_principle(pri, e->ring.channel, e->ring.call);
		if (chanpos < 0) {
			ast_log(LOG_WARNING,
				"Span %d: SETUP on unconfigured channel %d/%d\n",
				pri->span, PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel));
		} else {
			switch (pri->pvts[chanpos]->resetting) {
			case SIG_PRI_RESET_IDLE:
				break;
			case SIG_PRI_RESET_ACTIVE:
				/*
				 * The peer may have lost the expected ack or not received the
				 * RESTART yet.
				 */
				pri->pvts[chanpos]->resetting = SIG_PRI_RESET_NO_ACK;
				break;
			case SIG_PRI_RESET_NO_ACK:
				/* The peer likely is not going to ack the RESTART. */
				ast_debug(1,
					"Span %d: Second SETUP while waiting for RESTART ACKNOWLEDGE on channel %d/%d\n",
					pri->span, PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel));

				/* Assume we got the ack. */
				pri->pvts[chanpos]->resetting = SIG_PRI_RESET_IDLE;
				if (pri->resetting) {
					/* Go on to the next idle channel to RESTART. */
					pri_check_restart(pri);
				}
				break;
			}
			if (!sig_pri_is_chan_available(pri->pvts[chanpos])) {
				/* This is where we handle initial glare */
				ast_debug(1,
					"Span %d: SETUP requested unavailable channel %d/%d.  Attempting to renegotiate.\n",
					pri->span, PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel));
				chanpos = -1;
			}
		}
#if defined(ALWAYS_PICK_CHANNEL)
		if (e->ring.flexible) {
			chanpos = -1;
		}
#endif	/* defined(ALWAYS_PICK_CHANNEL) */
		if (chanpos < 0 && e->ring.flexible) {
			/* We can try to pick another channel. */
			chanpos = pri_find_empty_chan(pri, 1);
		}
	}
	if (chanpos < 0) {
		if (e->ring.flexible) {
			pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
		} else {
			pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
		}
		goto setup_exit;
	}

	sig_pri_lock_private(pri->pvts[chanpos]);

	/* Mark channel as in use so noone else will steal it. */
	pri->pvts[chanpos]->call = e->ring.call;

	/* Use plancallingnum as a scratch buffer since it is initialized next. */
	apply_plan_to_existing_number(plancallingnum, sizeof(plancallingnum), pri,
		e->ring.redirectingnum, e->ring.callingplanrdnis);
	sig_pri_set_rdnis(pri->pvts[chanpos], plancallingnum);

	/* Setup caller-id info */
	apply_plan_to_existing_number(plancallingnum, sizeof(plancallingnum), pri,
		e->ring.callingnum, e->ring.callingplan);
	pri->pvts[chanpos]->cid_ani2 = 0;
	if (pri->pvts[chanpos]->use_callerid) {
		ast_shrink_phone_number(plancallingnum);
		ast_copy_string(pri->pvts[chanpos]->cid_num, plancallingnum,
			sizeof(pri->pvts[chanpos]->cid_num));
#ifdef PRI_ANI
		apply_plan_to_existing_number(plancallingani, sizeof(plancallingani),
			pri, e->ring.callingani, e->ring.callingplanani);
		ast_shrink_phone_number(plancallingani);
		ast_copy_string(pri->pvts[chanpos]->cid_ani, plancallingani,
			sizeof(pri->pvts[chanpos]->cid_ani));
#endif
		pri->pvts[chanpos]->cid_subaddr[0] = '\0';
#if defined(HAVE_PRI_SUBADDR)
		if (e->ring.calling.subaddress.valid) {
			struct ast_party_subaddress calling_subaddress;

			ast_party_subaddress_init(&calling_subaddress);
			sig_pri_set_subaddress(&calling_subaddress,
				&e->ring.calling.subaddress);
			if (calling_subaddress.str) {
				ast_copy_string(pri->pvts[chanpos]->cid_subaddr,
					calling_subaddress.str,
					sizeof(pri->pvts[chanpos]->cid_subaddr));
			}
			ast_party_subaddress_free(&calling_subaddress);
		}
#endif /* defined(HAVE_PRI_SUBADDR) */
		ast_copy_string(pri->pvts[chanpos]->cid_name, e->ring.callingname,
			sizeof(pri->pvts[chanpos]->cid_name));
		/* this is the callingplan (TON/NPI), e->ring.callingplan>>4 would be the TON */
		pri->pvts[chanpos]->cid_ton = e->ring.callingplan;
		pri->pvts[chanpos]->callingpres = e->ring.callingpres;
		if (e->ring.ani2 >= 0) {
			pri->pvts[chanpos]->cid_ani2 = e->ring.ani2;
		}
	} else {
		pri->pvts[chanpos]->cid_num[0] = '\0';
		pri->pvts[chanpos]->cid_subaddr[0] = '\0';
		pri->pvts[chanpos]->cid_ani[0] = '\0';
		pri->pvts[chanpos]->cid_name[0] = '\0';
		pri->pvts[chanpos]->cid_ton = 0;
		pri->pvts[chanpos]->callingpres = 0;
	}

	/* Setup the user tag for party id's from this device for this call. */
	if (pri->append_msn_to_user_tag) {
		snprintf(pri->pvts[chanpos]->user_tag,
			sizeof(pri->pvts[chanpos]->user_tag), "%s_%s",
			pri->initial_user_tag,
			pri->nodetype == PRI_NETWORK
				? plancallingnum : e->ring.callednum);
	} else {
		ast_copy_string(pri->pvts[chanpos]->user_tag,
			pri->initial_user_tag, sizeof(pri->pvts[chanpos]->user_tag));
	}

	sig_pri_set_caller_id(pri->pvts[chanpos]);

	/* Set DNID on all incoming calls -- even immediate */
	sig_pri_set_dnid(pri->pvts[chanpos], e->ring.callednum);

	if (pri->pvts[chanpos]->immediate) {
		/* immediate=yes go to s|1 */
		ast_verb(3, "Going to extension s|1 because of immediate=yes\n");
		pri->pvts[chanpos]->exten[0] = 's';
		pri->pvts[chanpos]->exten[1] = '\0';
	} else if (!ast_strlen_zero(e->ring.callednum)) {
		/* Get called number */
		ast_copy_string(pri->pvts[chanpos]->exten, e->ring.callednum,
			sizeof(pri->pvts[chanpos]->exten));
	} else if (pri->overlapdial) {
		pri->pvts[chanpos]->exten[0] = '\0';
	} else {
		/* Some PRI circuits are set up to send _no_ digits.  Handle them as 's'. */
		pri->pvts[chanpos]->exten[0] = 's';
		pri->pvts[chanpos]->exten[1] = '\0';
	}
	/* No number yet, but received "sending complete"? */
	if (e->ring.complete && (ast_strlen_zero(e->ring.callednum))) {
		ast_verb(3, "Going to extension s|1 because of Complete received\n");
		pri->pvts[chanpos]->exten[0] = 's';
		pri->pvts[chanpos]->exten[1] = '\0';
	}

	/* Make sure extension exists (or in overlap dial mode, can exist) */
	exten_exists_or_can_exist = ((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
		&& ast_canmatch_extension(NULL, pri->pvts[chanpos]->context,
			pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num))
			|| ast_exists_extension(NULL, pri->pvts[chanpos]->context,
				pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num);
	if (!exten_exists_or_can_exist) {
		ast_verb(3,
			"Span %d: Extension %s@%s does not exist.  Rejecting call from '%s'.\n",
			pri->span, pri->pvts[chanpos]->exten, pri->pvts[chanpos]->context,
			pri->pvts[chanpos]->cid_num);
		pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_UNALLOCATED);
		pri->pvts[chanpos]->call = NULL;
		pri->pvts[chanpos]->exten[0] = '\0';
		sig_pri_unlock_private(pri->pvts[chanpos]);
		sig_pri_span_devstate_changed(pri);
		goto setup_exit;
	}

	/* Select audio companding mode. */
	switch (e->ring.layer1) {
	case PRI_LAYER_1_ALAW:
		law = SIG_PRI_ALAW;
		break;
	case PRI_LAYER_1_ULAW:
		law = SIG_PRI_ULAW;
		break;
	default:
		/* This is a data call to us. */
		law = SIG_PRI_DEFLAW;
		break;
	}

	could_match_more = !e->ring.complete
		&& (pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
		&& ast_matchmore_extension(NULL, pri->pvts[chanpos]->context,
			pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num);

	need_dialtone = could_match_more
		/*
		 * Must explicitly check the digital capability this
		 * way instead of checking the pvt->digital flag
		 * because the flag hasn't been set yet.
		 */
		&& !(e->ring.ctype & AST_TRANS_CAP_DIGITAL)
		&& !pri->pvts[chanpos]->no_b_channel
		&& (!strlen(pri->pvts[chanpos]->exten)
			|| ast_ignore_pattern(pri->pvts[chanpos]->context,
				pri->pvts[chanpos]->exten));

	if (e->ring.complete || !(pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) {
		/* Just announce proceeding */
		pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_PROCEEDING;
		pri_proceeding(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 0);
	} else if (pri->switchtype == PRI_SWITCH_GR303_TMC) {
		pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_CONNECT;
		pri_answer(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
	} else {
		pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_OVERLAP;
#if defined(HAVE_PRI_SETUP_ACK_INBAND)
		pri_setup_ack(pri->pri, e->ring.call,
			PVT_TO_CHANNEL(pri->pvts[chanpos]), 1, need_dialtone);
#else	/* !defined(HAVE_PRI_SETUP_ACK_INBAND) */
		pri_need_more_info(pri->pri, e->ring.call,
			PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
#endif	/* !defined(HAVE_PRI_SETUP_ACK_INBAND) */
	}

	/*
	 * Release the PRI lock while we create the channel so other
	 * threads can send D channel messages.  We must also release
	 * the private lock to prevent deadlock while creating the
	 * channel.
	 */
	sig_pri_unlock_private(pri->pvts[chanpos]);
	ast_mutex_unlock(&pri->lock);
	c = sig_pri_new_ast_channel(pri->pvts[chanpos],
		could_match_more ? AST_STATE_RESERVED : AST_STATE_RING, law, e->ring.ctype,
		pri->pvts[chanpos]->exten, NULL, NULL);
	ast_mutex_lock(&pri->lock);
	sig_pri_lock_private(pri->pvts[chanpos]);

	if (c) {
		setup_incoming_channel(pri, chanpos, e);

		/* Start PBX */
		if (could_match_more) {
#if !defined(HAVE_PRI_SETUP_ACK_INBAND)
			if (need_dialtone) {
				/* Indicate that we are providing dialtone. */
				pri->pvts[chanpos]->progress = 1;/* No need to send plain PROGRESS again. */
#ifdef HAVE_PRI_PROG_W_CAUSE
				pri_progress_with_cause(pri->pri, e->ring.call,
					PVT_TO_CHANNEL(pri->pvts[chanpos]), 1, -1);/* no cause at all */
#else
				pri_progress(pri->pri, e->ring.call,
					PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
#endif
			}
#endif	/* !defined(HAVE_PRI_SETUP_ACK_INBAND) */

			if (!ast_pthread_create_detached(&threadid, NULL, pri_ss_thread,
				pri->pvts[chanpos])) {
				ast_verb(3, "Accepting overlap call from '%s' to '%s' on channel %d/%d, span %d\n",
					plancallingnum, S_OR(pri->pvts[chanpos]->exten, "<unspecified>"),
					pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,
					pri->span);
				sig_pri_unlock_private(pri->pvts[chanpos]);
				goto setup_exit;
			}
		} else {
			if (!ast_pbx_start(c)) {
				ast_verb(3, "Accepting call from '%s' to '%s' on channel %d/%d, span %d\n",
					plancallingnum, pri->pvts[chanpos]->exten,
					pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,
					pri->span);
				sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
				sig_pri_unlock_private(pri->pvts[chanpos]);
				goto setup_exit;
			}
		}
	}
	ast_log(LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n",
		pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
	if (c) {
		/* Avoid deadlock while destroying channel */
		sig_pri_unlock_private(pri->pvts[chanpos]);
		ast_mutex_unlock(&pri->lock);
		ast_hangup(c);
		ast_mutex_lock(&pri->lock);
	} else {
		pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
		pri->pvts[chanpos]->call = NULL;
		sig_pri_unlock_private(pri->pvts[chanpos]);
		sig_pri_span_devstate_changed(pri);
	}

setup_exit:;
	if (callid) {
		ast_callid_unref(callid);
		ast_callid_threadassoc_remove();
	}
}

static void *pri_dchannel(void *vpri)
{
	struct sig_pri_span *pri = vpri;
	pri_event *e;
	struct pollfd fds[SIG_PRI_NUM_DCHANS];
	int res;
	int x;
	struct timeval tv, lowest, *next;
	int doidling=0;
	char *cc;
	time_t t;
	int i, which=-1;
	int numdchans;
	struct timeval lastidle = { 0, 0 };
	pthread_t p;
	struct ast_channel *idle;
	char idlen[80];
	int nextidle = -1;
	int haveidles;
	int activeidles;
	unsigned int len;

	gettimeofday(&lastidle, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	if (!ast_strlen_zero(pri->idledial) && !ast_strlen_zero(pri->idleext)) {
		/* Need to do idle dialing, check to be sure though */
		cc = strchr(pri->idleext, '@');
		if (cc) {
			*cc = '\0';
			cc++;
			ast_copy_string(pri->idlecontext, cc, sizeof(pri->idlecontext));
#if 0
			/* Extensions may not be loaded yet */
			if (!ast_exists_extension(NULL, pri->idlecontext, pri->idleext, 1, NULL))
				ast_log(LOG_WARNING, "Extension '%s @ %s' does not exist\n", pri->idleext, pri->idlecontext);
			else
#endif
				doidling = 1;
		} else
			ast_log(LOG_WARNING, "Idle dial string '%s' lacks '@context'\n", pri->idleext);
	}
	for (;;) {
		struct ast_callid *callid = NULL;

		for (i = 0; i < SIG_PRI_NUM_DCHANS; i++) {
			if (!pri->dchans[i])
				break;
			fds[i].fd = pri->fds[i];
			fds[i].events = POLLIN | POLLPRI;
			fds[i].revents = 0;
		}
		numdchans = i;
		time(&t);
		ast_mutex_lock(&pri->lock);
		if (pri->switchtype != PRI_SWITCH_GR303_TMC && (pri->sig != SIG_BRI_PTMP) && (pri->resetinterval > 0)) {
			if (pri->resetting && pri_is_up(pri)) {
				if (pri->resetpos < 0) {
					pri_check_restart(pri);
					if (pri->resetting) {
						sig_pri_span_devstate_changed(pri);
					}
				}
			} else {
				if (!pri->resetting	&& (t - pri->lastreset) >= pri->resetinterval) {
					pri->resetting = 1;
					pri->resetpos = -1;
				}
			}
		}
		/* Look for any idle channels if appropriate */
		if (doidling && pri_is_up(pri)) {
			nextidle = -1;
			haveidles = 0;
			activeidles = 0;
			for (x = pri->numchans; x >= 0; x--) {
				if (pri->pvts[x] && !pri->pvts[x]->no_b_channel) {
					if (sig_pri_is_chan_available(pri->pvts[x])) {
						if (haveidles < pri->minunused) {
							haveidles++;
						} else {
							nextidle = x;
							break;
						}
					} else if (pri->pvts[x]->owner && pri->pvts[x]->isidlecall) {
						activeidles++;
					}
				}
			}
			if (nextidle > -1) {
				if (ast_tvdiff_ms(ast_tvnow(), lastidle) > 1000) {
					/* Don't create a new idle call more than once per second */
					snprintf(idlen, sizeof(idlen), "%d/%s", pri->pvts[nextidle]->channel, pri->idledial);
					pri->pvts[nextidle]->allocated = 1;
					/*
					 * Release the PRI lock while we create the channel so other
					 * threads can send D channel messages.
					 */
					ast_mutex_unlock(&pri->lock);
					/*
					 * We already have the B channel reserved for this call.  We
					 * just need to make sure that sig_pri_hangup() has completed
					 * cleaning up before continuing.
					 */
					sig_pri_lock_private(pri->pvts[nextidle]);
					sig_pri_unlock_private(pri->pvts[nextidle]);
					idle = sig_pri_request(pri->pvts[nextidle], SIG_PRI_ULAW, NULL, NULL, 0);
					ast_mutex_lock(&pri->lock);
					if (idle) {
						pri->pvts[nextidle]->isidlecall = 1;
						if (ast_pthread_create_background(&p, NULL, do_idle_thread, pri->pvts[nextidle])) {
							ast_log(LOG_WARNING, "Unable to start new thread for idle channel '%s'\n", ast_channel_name(idle));
							ast_mutex_unlock(&pri->lock);
							ast_hangup(idle);
							ast_mutex_lock(&pri->lock);
						}
					} else {
						pri->pvts[nextidle]->allocated = 0;
						ast_log(LOG_WARNING, "Unable to request channel 'DAHDI/%s' for idle call\n", idlen);
					}
					gettimeofday(&lastidle, NULL);
				}
			} else if ((haveidles < pri->minunused) &&
				(activeidles > pri->minidle)) {
				/* Mark something for hangup if there is something
				   that can be hungup */
				for (x = pri->numchans; x >= 0; x--) {
					/* find a candidate channel */
					if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall) {
						ast_channel_softhangup_internal_flag_add(pri->pvts[x]->owner, AST_SOFTHANGUP_DEV);
						haveidles++;
						/* Stop if we have enough idle channels or
						  can't spare any more active idle ones */
						if ((haveidles >= pri->minunused) ||
							(activeidles <= pri->minidle))
							break;
					}
				}
			}
		}
		/* Start with reasonable max */
		if (doidling || pri->resetting) {
			/*
			 * Make sure we stop at least once per second if we're
			 * monitoring idle channels
			 */
			lowest = ast_tv(1, 0);
		} else {
			/* Don't poll for more than 60 seconds */
			lowest = ast_tv(60, 0);
		}
		for (i = 0; i < SIG_PRI_NUM_DCHANS; i++) {
			if (!pri->dchans[i]) {
				/* We scanned all D channels on this span. */
				break;
			}
			next = pri_schedule_next(pri->dchans[i]);
			if (next) {
				/* We need relative time here */
				tv = ast_tvsub(*next, ast_tvnow());
				if (tv.tv_sec < 0) {
					/*
					 * A timer has already expired.
					 * By definition zero time is the lowest so we can quit early.
					 */
					lowest = ast_tv(0, 0);
					break;
				}
				if (ast_tvcmp(tv, lowest) < 0) {
					lowest = tv;
				}
			}
		}
		ast_mutex_unlock(&pri->lock);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
		e = NULL;
		res = poll(fds, numdchans, lowest.tv_sec * 1000 + lowest.tv_usec / 1000);
		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		ast_mutex_lock(&pri->lock);
		if (!res) {
			for (which = 0; which < SIG_PRI_NUM_DCHANS; which++) {
				if (!pri->dchans[which])
					break;
				/* Just a timeout, run the scheduler */
				e = pri_schedule_run(pri->dchans[which]);
				if (e)
					break;
			}
		} else if (res > -1) {
			for (which = 0; which < SIG_PRI_NUM_DCHANS; which++) {
				if (!pri->dchans[which])
					break;
				if (fds[which].revents & POLLPRI) {
					sig_pri_handle_dchan_exception(pri, which);
				} else if (fds[which].revents & POLLIN) {
					e = pri_check_event(pri->dchans[which]);
				}
				if (e)
					break;

				if ((errno != 0) && (errno != EINTR)) {
					ast_log(LOG_NOTICE, "pri_check_event returned error %d (%s)\n",
						errno, strerror(errno));
				}
				if (errno == ENODEV) {
					pri_destroy_later(pri);
				}
			}
		} else if (errno != EINTR)
			ast_log(LOG_WARNING, "pri_event returned error %d (%s)\n", errno, strerror(errno));

		if (e) {
			int chanpos = -1;
			char cause_str[35];

			if (pri->debug) {
				ast_verbose("Span %d: Processing event %s(%d)\n",
					pri->span, pri_event2str(e->e), e->e);
			}

			if (e->e != PRI_EVENT_DCHAN_DOWN) {
				if (!(pri->dchanavail[which] & DCHAN_UP)) {
					ast_verb(2, "%s D-Channel on span %d up\n", pri_order(which), pri->span);
				}
				pri->dchanavail[which] |= DCHAN_UP;
			} else {
				if (pri->dchanavail[which] & DCHAN_UP) {
					ast_verb(2, "%s D-Channel on span %d down\n", pri_order(which), pri->span);
				}
				pri->dchanavail[which] &= ~DCHAN_UP;
			}

			if ((e->e != PRI_EVENT_DCHAN_UP) && (e->e != PRI_EVENT_DCHAN_DOWN) && (pri->pri != pri->dchans[which]))
				/* Must be an NFAS group that has the secondary dchan active */
				pri->pri = pri->dchans[which];

			switch (e->e) {
			case PRI_EVENT_DCHAN_UP:
				pri->no_d_channels = 0;
				if (!pri->pri) {
					pri_find_dchan(pri);
				}

				/* Note presense of D-channel */
				time(&pri->lastreset);

				/* Restart in 5 seconds */
				if (pri->resetinterval > -1) {
					pri->lastreset -= pri->resetinterval;
					pri->lastreset += 5;
				}
				/* Take the channels from inalarm condition */
				pri->resetting = 0;
				for (i = 0; i < pri->numchans; i++) {
					if (pri->pvts[i]) {
						sig_pri_set_alarm(pri->pvts[i], 0);
					}
				}
				sig_pri_span_devstate_changed(pri);
				break;
			case PRI_EVENT_DCHAN_DOWN:
				pri_find_dchan(pri);
				if (!pri_is_up(pri)) {
					if (pri->sig == SIG_BRI_PTMP) {
						/*
						 * For PTMP connections with non-persistent layer 2 we want to
						 * *not* declare inalarm unless there actually is an alarm.
						 */
						break;
					}
					/* Hangup active channels and put them in alarm mode */
					pri->resetting = 0;
					for (i = 0; i < pri->numchans; i++) {
						struct sig_pri_chan *p = pri->pvts[i];

						if (p) {
							if (pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0) {
								/* T309 is not enabled : destroy calls when alarm occurs */
								if (p->call) {
									pri_destroycall(p->pri->pri, p->call);
									p->call = NULL;
								}
								if (p->owner)
									ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
							}
							sig_pri_set_alarm(p, 1);
						}
					}
					sig_pri_span_devstate_changed(pri);
				}
				break;
			case PRI_EVENT_RESTART:
				if (e->restart.channel > -1 && PRI_CHANNEL(e->restart.channel) != 0xFF) {
					chanpos = pri_find_principle(pri, e->restart.channel, NULL);
					if (chanpos < 0)
						ast_log(LOG_WARNING,
							"Span %d: Restart requested on odd/unavailable channel number %d/%d\n",
							pri->span, PRI_SPAN(e->restart.channel),
							PRI_CHANNEL(e->restart.channel));
					else {
						int skipit = 0;
#if defined(HAVE_PRI_SERVICE_MESSAGES)
						unsigned why;

						why = pri->pvts[chanpos]->service_status;
						if (why) {
							ast_log(LOG_NOTICE,
								"Span %d: Channel %d/%d out-of-service (reason: %s), ignoring RESTART\n",
								pri->span, PRI_SPAN(e->restart.channel),
								PRI_CHANNEL(e->restart.channel),
								(why & SRVST_FAREND) ? (why & SRVST_NEAREND) ? "both ends" : "far end" : "near end");
							skipit = 1;
						}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
						sig_pri_lock_private(pri->pvts[chanpos]);
						if (!skipit) {
							ast_verb(3, "Span %d: Channel %d/%d restarted\n", pri->span,
								PRI_SPAN(e->restart.channel),
								PRI_CHANNEL(e->restart.channel));
							if (pri->pvts[chanpos]->call) {
								pri_destroycall(pri->pri, pri->pvts[chanpos]->call);
								pri->pvts[chanpos]->call = NULL;
							}
						}
						/* Force hangup if appropriate */
						sig_pri_queue_hangup(pri, chanpos);
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				} else {
					ast_verb(3, "Restart requested on entire span %d\n", pri->span);
					for (x = 0; x < pri->numchans; x++)
						if (pri->pvts[x]) {
							sig_pri_lock_private(pri->pvts[x]);
							if (pri->pvts[x]->call) {
								pri_destroycall(pri->pri, pri->pvts[x]->call);
								pri->pvts[x]->call = NULL;
							}
							/* Force hangup if appropriate */
							sig_pri_queue_hangup(pri, x);
							sig_pri_unlock_private(pri->pvts[x]);
						}
				}
				sig_pri_span_devstate_changed(pri);
				break;
			case PRI_EVENT_KEYPAD_DIGIT:
				if (sig_pri_is_cis_call(e->digit.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->digit.subcmds,
						e->digit.call);
					break;
				}
				chanpos = pri_find_principle_by_call(pri, e->digit.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING,
						"Span %d: Received keypad digits for unknown call.\n", pri->span);
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->digit.subcmds,
					e->digit.call);
				/* queue DTMF frame if the PBX for this call was already started (we're forwarding KEYPAD_DIGITs further on */
				if ((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
					&& pri->pvts[chanpos]->owner) {
					/* how to do that */
					int digitlen = strlen(e->digit.digits);
					int i;

					for (i = 0; i < digitlen; i++) {
						struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = e->digit.digits[i], };

						pri_queue_frame(pri, chanpos, &f);
					}
				}
				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;

			case PRI_EVENT_INFO_RECEIVED:
				if (sig_pri_is_cis_call(e->ring.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->ring.subcmds,
						e->ring.call);
					break;
				}
				chanpos = pri_find_principle_by_call(pri, e->ring.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING,
						"Span %d: Received INFORMATION for unknown call.\n", pri->span);
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->ring.subcmds, e->ring.call);
				/* queue DTMF frame if the PBX for this call was already started (we're forwarding INFORMATION further on */
				if ((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
					&& pri->pvts[chanpos]->owner) {
					/* how to do that */
					int digitlen = strlen(e->ring.callednum);
					int i;

					for (i = 0; i < digitlen; i++) {
						struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = e->ring.callednum[i], };

						pri_queue_frame(pri, chanpos, &f);
					}
				}
				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
#if defined(HAVE_PRI_SERVICE_MESSAGES)
			case PRI_EVENT_SERVICE:
				chanpos = pri_find_principle(pri, e->service.channel, NULL);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received service change status %d on unconfigured channel %d/%d span %d\n",
						e->service_ack.changestatus, PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span);
				} else {
					char db_chan_name[20];
					char db_answer[5];
					int ch;
					unsigned *why;

					ch = pri->pvts[chanpos]->channel;
					snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, pri->span, ch);
					why = &pri->pvts[chanpos]->service_status;
					switch (e->service.changestatus) {
					case 0: /* in-service */
						/* Far end wants to be in service now. */
						ast_db_del(db_chan_name, SRVST_DBKEY);
						*why &= ~SRVST_FAREND;
						if (*why) {
							snprintf(db_answer, sizeof(db_answer), "%s:%u",
								SRVST_TYPE_OOS, *why);
							ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
						} else {
							sig_pri_span_devstate_changed(pri);
						}
						break;
					case 2: /* out-of-service */
						/* Far end wants to be out-of-service now. */
						ast_db_del(db_chan_name, SRVST_DBKEY);
						*why |= SRVST_FAREND;
						snprintf(db_answer, sizeof(db_answer), "%s:%u", SRVST_TYPE_OOS,
							*why);
						ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
						sig_pri_span_devstate_changed(pri);
						break;
					default:
						ast_log(LOG_ERROR, "Huh?  changestatus is: %d\n", e->service.changestatus);
						break;
					}
					ast_log(LOG_NOTICE, "Channel %d/%d span %d (logical: %d) received a change of service message, status '%d'\n",
						PRI_SPAN(e->service.channel), PRI_CHANNEL(e->service.channel), pri->span, ch, e->service.changestatus);
				}
				break;
			case PRI_EVENT_SERVICE_ACK:
				chanpos = pri_find_principle(pri, e->service_ack.channel, NULL);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received service acknowledge change status '%d' on unconfigured channel %d/%d span %d\n",
						e->service_ack.changestatus, PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span);
				} else {
					ast_debug(2, "Channel %d/%d span %d received a change os service acknowledgement message, status '%d'\n",
						PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span, e->service_ack.changestatus);
				}
				break;
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
			case PRI_EVENT_RING:
				sig_pri_handle_setup(pri, e);
				break;
			case PRI_EVENT_RINGING:
				if (sig_pri_is_cis_call(e->ringing.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->ringing.subcmds,
						e->ringing.call);
					break;
				}
				chanpos = pri_find_fixup_principle(pri, e->ringing.channel,
					e->ringing.call);
				if (chanpos < 0) {
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->ringing.subcmds,
					e->ringing.call);
				sig_pri_cc_generic_check(pri, chanpos, AST_CC_CCNR);
				sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
				sig_pri_lock_owner(pri, chanpos);
				if (pri->pvts[chanpos]->owner) {
					ast_setstate(pri->pvts[chanpos]->owner, AST_STATE_RINGING);
					ast_channel_unlock(pri->pvts[chanpos]->owner);
				}
				pri_queue_control(pri, chanpos, AST_CONTROL_RINGING);
				if (pri->pvts[chanpos]->call_level < SIG_PRI_CALL_LEVEL_ALERTING) {
					pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_ALERTING;
				}

				if (!pri->pvts[chanpos]->progress
					&& !pri->pvts[chanpos]->no_b_channel
#ifdef PRI_PROGRESS_MASK
					&& (e->ringing.progressmask
						& (PRI_PROG_CALL_NOT_E2E_ISDN | PRI_PROG_INBAND_AVAILABLE))
#else
					&& e->ringing.progress == 8
#endif
					) {
					/* Bring voice path up */
					pri_queue_control(pri, chanpos, AST_CONTROL_PROGRESS);
					pri->pvts[chanpos]->progress = 1;
					sig_pri_set_dialing(pri->pvts[chanpos], 0);
					sig_pri_open_media(pri->pvts[chanpos]);
				}

#ifdef SUPPORT_USERUSER
				if (!ast_strlen_zero(e->ringing.useruserinfo)) {
					struct ast_channel *owner;

					sig_pri_lock_owner(pri, chanpos);
					owner = pri->pvts[chanpos]->owner;
					if (owner) {
						pbx_builtin_setvar_helper(owner, "USERUSERINFO",
							e->ringing.useruserinfo);
						ast_channel_unlock(owner);
					}
				}
#endif

				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
			case PRI_EVENT_PROGRESS:
				if (sig_pri_is_cis_call(e->proceeding.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->proceeding.subcmds,
						e->proceeding.call);
					break;
				}
				chanpos = pri_find_fixup_principle(pri, e->proceeding.channel,
					e->proceeding.call);
				if (chanpos < 0) {
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->proceeding.subcmds,
					e->proceeding.call);

				if (e->proceeding.cause > -1) {
					if (pri->pvts[chanpos]->owner) {
						snprintf(cause_str, sizeof(cause_str), "PRI PRI_EVENT_PROGRESS (%d)", e->proceeding.cause);
						pri_queue_pvt_cause_data(pri, chanpos, cause_str, e->proceeding.cause);
					}

					ast_verb(3, "PROGRESS with cause code %d received\n", e->proceeding.cause);

					/* Work around broken, out of spec USER_BUSY cause in a progress message */
					if (e->proceeding.cause == AST_CAUSE_USER_BUSY) {
						if (pri->pvts[chanpos]->owner) {
							ast_verb(3, "PROGRESS with 'user busy' received, signaling AST_CONTROL_BUSY instead of AST_CONTROL_PROGRESS\n");

							ast_channel_hangupcause_set(pri->pvts[chanpos]->owner, e->proceeding.cause);
							pri_queue_control(pri, chanpos, AST_CONTROL_BUSY);
						}
					}
				}

				if (!pri->pvts[chanpos]->progress
					&& !pri->pvts[chanpos]->no_b_channel
#ifdef PRI_PROGRESS_MASK
					&& (e->proceeding.progressmask
						& (PRI_PROG_CALL_NOT_E2E_ISDN | PRI_PROG_INBAND_AVAILABLE))
#else
					&& e->proceeding.progress == 8
#endif
					) {
					/* Bring voice path up */
					ast_debug(1,
						"Queuing frame from PRI_EVENT_PROGRESS on channel %d/%d span %d\n",
						pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,
						pri->span);
					pri_queue_control(pri, chanpos, AST_CONTROL_PROGRESS);
					pri->pvts[chanpos]->progress = 1;
					sig_pri_set_dialing(pri->pvts[chanpos], 0);
					sig_pri_open_media(pri->pvts[chanpos]);
				}
				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
			case PRI_EVENT_PROCEEDING:
				if (sig_pri_is_cis_call(e->proceeding.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->proceeding.subcmds,
						e->proceeding.call);
					break;
				}
				chanpos = pri_find_fixup_principle(pri, e->proceeding.channel,
					e->proceeding.call);
				if (chanpos < 0) {
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->proceeding.subcmds,
					e->proceeding.call);
				if (pri->pvts[chanpos]->call_level < SIG_PRI_CALL_LEVEL_PROCEEDING) {
					pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_PROCEEDING;
					ast_debug(1,
						"Queuing frame from PRI_EVENT_PROCEEDING on channel %d/%d span %d\n",
						pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,
						pri->span);
					pri_queue_control(pri, chanpos, AST_CONTROL_PROCEEDING);
				}
				if (!pri->pvts[chanpos]->progress
					&& !pri->pvts[chanpos]->no_b_channel
#ifdef PRI_PROGRESS_MASK
					/*
					 * We only care about PRI_PROG_INBAND_AVAILABLE to open the
					 * voice path.
					 *
					 * We explicitly DO NOT want to check PRI_PROG_CALL_NOT_E2E_ISDN
					 * because it will mess up ISDN to SIP interoperability for
					 * the ALERTING message.
					 */
					&& (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE)
#else
					&& e->proceeding.progress == 8
#endif
					) {
					/* Bring voice path up */
					pri_queue_control(pri, chanpos, AST_CONTROL_PROGRESS);
					pri->pvts[chanpos]->progress = 1;
					sig_pri_set_dialing(pri->pvts[chanpos], 0);
					sig_pri_open_media(pri->pvts[chanpos]);
				} else if (pri->inband_on_proceeding) {
					/*
					 * XXX This is to accomodate a broken switch that sends a
					 * PROCEEDING without any progress indication ie for
					 * inband audio.  This should be part of the conditional
					 * test above to bring the voice path up.
					 */
					sig_pri_set_dialing(pri->pvts[chanpos], 0);
				}
				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
			case PRI_EVENT_FACILITY:
				if (!e->facility.call || sig_pri_is_cis_call(e->facility.channel)) {
					/* Event came in on the dummy channel or a CIS call. */
#if defined(HAVE_PRI_CALL_REROUTING)
					sig_pri_handle_cis_subcmds(pri, e->e, e->facility.subcmds,
						e->facility.subcall);
#else
					sig_pri_handle_cis_subcmds(pri, e->e, e->facility.subcmds,
						e->facility.call);
#endif	/* !defined(HAVE_PRI_CALL_REROUTING) */
					break;
				}
				chanpos = pri_find_principle_by_call(pri, e->facility.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Span %d: Received facility for unknown call.\n",
						pri->span);
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

#if defined(HAVE_PRI_CALL_REROUTING)
				sig_pri_handle_subcmds(pri, chanpos, e->e, e->facility.subcmds,
					e->facility.subcall);
#else
				sig_pri_handle_subcmds(pri, chanpos, e->e, e->facility.subcmds,
					e->facility.call);
#endif	/* !defined(HAVE_PRI_CALL_REROUTING) */
				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
			case PRI_EVENT_ANSWER:
				if (sig_pri_is_cis_call(e->answer.channel)) {
#if defined(HAVE_PRI_CALL_WAITING)
					/* Call is CIS so do normal CONNECT_ACKNOWLEDGE. */
					pri_connect_ack(pri->pri, e->answer.call, 0);
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
					sig_pri_handle_cis_subcmds(pri, e->e, e->answer.subcmds,
						e->answer.call);
					break;
				}
				chanpos = pri_find_fixup_principle(pri, e->answer.channel, e->answer.call);
				if (chanpos < 0) {
					break;
				}
#if defined(HAVE_PRI_CALL_WAITING)
				if (pri->pvts[chanpos]->is_call_waiting) {
					if (pri->pvts[chanpos]->no_b_channel) {
						int new_chanpos;

						/*
						 * Need to find a free channel now or
						 * kill the call with PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION.
						 */
						new_chanpos = pri_find_empty_chan(pri, 1);
						if (0 <= new_chanpos) {
							new_chanpos = pri_fixup_principle(pri, new_chanpos,
								e->answer.call);
						}
						if (new_chanpos < 0) {
							/*
							 * Either no channel was available or someone stole
							 * the channel!
							 */
							ast_verb(3,
								"Span %d: Channel not available for call waiting call.\n",
								pri->span);
							sig_pri_lock_private(pri->pvts[chanpos]);
							sig_pri_handle_subcmds(pri, chanpos, e->e, e->answer.subcmds,
								e->answer.call);
							sig_pri_cc_generic_check(pri, chanpos, AST_CC_CCBS);
							sig_pri_lock_owner(pri, chanpos);
							if (pri->pvts[chanpos]->owner) {
								ast_channel_hangupcause_set(pri->pvts[chanpos]->owner, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
								switch (ast_channel_state(pri->pvts[chanpos]->owner)) {
								case AST_STATE_BUSY:
								case AST_STATE_UP:
									ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
									break;
								default:
									pri_queue_control(pri, chanpos, AST_CONTROL_CONGESTION);
									break;
								}
								ast_channel_unlock(pri->pvts[chanpos]->owner);
							} else {
								pri->pvts[chanpos]->is_call_waiting = 0;
								ast_atomic_fetchadd_int(&pri->num_call_waiting_calls, -1);
								pri_hangup(pri->pri, e->answer.call, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
								pri->pvts[chanpos]->call = NULL;
							}
							sig_pri_unlock_private(pri->pvts[chanpos]);
							sig_pri_span_devstate_changed(pri);
							break;
						}
						chanpos = new_chanpos;
					}
					pri_connect_ack(pri->pri, e->answer.call, PVT_TO_CHANNEL(pri->pvts[chanpos]));
					sig_pri_span_devstate_changed(pri);
				} else {
					/* Call is normal so do normal CONNECT_ACKNOWLEDGE. */
					pri_connect_ack(pri->pri, e->answer.call, 0);
				}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

#if defined(HAVE_PRI_CALL_WAITING)
				if (pri->pvts[chanpos]->is_call_waiting) {
					pri->pvts[chanpos]->is_call_waiting = 0;
					ast_atomic_fetchadd_int(&pri->num_call_waiting_calls, -1);
				}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
				sig_pri_handle_subcmds(pri, chanpos, e->e, e->answer.subcmds,
					e->answer.call);
				if (!ast_strlen_zero(pri->pvts[chanpos]->deferred_digits)) {
					/* We have some 'w' deferred digits to dial now. */
					ast_verb(3,
						"Span %d: Channel %d/%d dialing deferred digit string: %s\n",
						pri->span, pri->pvts[chanpos]->logicalspan,
						pri->pvts[chanpos]->prioffset,
						pri->pvts[chanpos]->deferred_digits);
					if (pri->pvts[chanpos]->call_level < SIG_PRI_CALL_LEVEL_DEFER_DIAL) {
						pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_DEFER_DIAL;
					}
					sig_pri_dial_digits(pri->pvts[chanpos],
						pri->pvts[chanpos]->deferred_digits);
				} else {
					if (pri->pvts[chanpos]->call_level < SIG_PRI_CALL_LEVEL_CONNECT) {
						pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_CONNECT;
					}
					sig_pri_open_media(pri->pvts[chanpos]);
					pri_queue_control(pri, chanpos, AST_CONTROL_ANSWER);
					sig_pri_set_dialing(pri->pvts[chanpos], 0);
					/* Enable echo cancellation if it's not on already */
					sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
				}

#ifdef SUPPORT_USERUSER
				if (!ast_strlen_zero(e->answer.useruserinfo)) {
					struct ast_channel *owner;

					sig_pri_lock_owner(pri, chanpos);
					owner = pri->pvts[chanpos]->owner;
					if (owner) {
						pbx_builtin_setvar_helper(owner, "USERUSERINFO",
							e->answer.useruserinfo);
						ast_channel_unlock(owner);
					}
				}
#endif

				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
#if defined(HAVE_PRI_CALL_WAITING)
			case PRI_EVENT_CONNECT_ACK:
				if (sig_pri_is_cis_call(e->connect_ack.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->connect_ack.subcmds,
						e->connect_ack.call);
					break;
				}
				chanpos = pri_find_fixup_principle(pri, e->connect_ack.channel,
					e->connect_ack.call);
				if (chanpos < 0) {
					break;
				}

				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->connect_ack.subcmds,
					e->connect_ack.call);
				sig_pri_open_media(pri->pvts[chanpos]);
				sig_pri_unlock_private(pri->pvts[chanpos]);
				sig_pri_span_devstate_changed(pri);
				break;
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
			case PRI_EVENT_HANGUP:
				if (sig_pri_is_cis_call(e->hangup.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->hangup.subcmds,
						e->hangup.call);
					pri_hangup(pri->pri, e->hangup.call, e->hangup.cause);
					break;
				}
				chanpos = pri_find_principle_by_call(pri, e->hangup.call);
				if (chanpos < 0) {
					/*
					 * Continue hanging up the call even though
					 * we do not remember it (if we ever did).
					 */
					pri_hangup(pri->pri, e->hangup.call, e->hangup.cause);
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->hangup.subcmds,
					e->hangup.call);
				switch (e->hangup.cause) {
				case PRI_CAUSE_INVALID_CALL_REFERENCE:
					/*
					 * The peer denies the existence of this call so we must
					 * continue hanging it up and forget about it.
					 */
					pri_hangup(pri->pri, e->hangup.call, e->hangup.cause);
					pri->pvts[chanpos]->call = NULL;
					break;
				default:
					break;
				}
				if (!pri->pvts[chanpos]->alreadyhungup) {
					/* we're calling here dahdi_hangup so once we get there we need to clear p->call after calling pri_hangup */
					pri->pvts[chanpos]->alreadyhungup = 1;
					switch (e->hangup.cause) {
					case PRI_CAUSE_USER_BUSY:
					case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
						sig_pri_cc_generic_check(pri, chanpos, AST_CC_CCBS);
						break;
					default:
						break;
					}
					if (pri->pvts[chanpos]->owner) {
						snprintf(cause_str, sizeof(cause_str), "PRI PRI_EVENT_HANGUP (%d)", e->hangup.cause);
						pri_queue_pvt_cause_data(pri, chanpos, cause_str, e->hangup.cause);
					}
					if (pri->pvts[chanpos]->owner) {
						int do_hangup = 0;

						/* Queue a BUSY instead of a hangup if our cause is appropriate */
						ast_channel_hangupcause_set(pri->pvts[chanpos]->owner, e->hangup.cause);
						switch (ast_channel_state(pri->pvts[chanpos]->owner)) {
						case AST_STATE_BUSY:
						case AST_STATE_UP:
							do_hangup = 1;
							break;
						default:
							if (!pri->pvts[chanpos]->outgoing) {
								/*
								 * The incoming call leg hung up before getting
								 * connected so just hangup the call.
								 */
								do_hangup = 1;
								break;
							}
							switch (e->hangup.cause) {
							case PRI_CAUSE_USER_BUSY:
								pri_queue_control(pri, chanpos, AST_CONTROL_BUSY);
								break;
							case PRI_CAUSE_CALL_REJECTED:
							case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
							case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
							case PRI_CAUSE_SWITCH_CONGESTION:
							case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
							case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
								pri_queue_control(pri, chanpos, AST_CONTROL_CONGESTION);
								break;
							default:
								do_hangup = 1;
								break;
							}
							break;
						}

						if (do_hangup) {
							sig_pri_queue_hangup(pri, chanpos);
						}
					} else {
						/*
						 * Continue hanging up the call even though
						 * we do not have an owner.
						 */
						pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
						pri->pvts[chanpos]->call = NULL;
					}
					ast_verb(3, "Span %d: Channel %d/%d got hangup, cause %d\n",
						pri->span, pri->pvts[chanpos]->logicalspan,
						pri->pvts[chanpos]->prioffset, e->hangup.cause);
				} else {
					/* Continue hanging up the call. */
					pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
					pri->pvts[chanpos]->call = NULL;
				}
				if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL
					&& pri->sig != SIG_BRI_PTMP && !pri->resetting
					&& pri->force_restart_unavailable_chans
					&& pri->pvts[chanpos]->resetting == SIG_PRI_RESET_IDLE) {
					ast_verb(3,
						"Span %d: Forcing restart of channel %d/%d since channel reported in use\n",
						pri->span, pri->pvts[chanpos]->logicalspan,
						pri->pvts[chanpos]->prioffset);
					pri->pvts[chanpos]->resetting = SIG_PRI_RESET_ACTIVE;
					pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
				}
				if (e->hangup.aoc_units > -1)
					ast_verb(3, "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
						pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");

#ifdef SUPPORT_USERUSER
				if (!ast_strlen_zero(e->hangup.useruserinfo)) {
					struct ast_channel *owner;

					sig_pri_lock_owner(pri, chanpos);
					owner = pri->pvts[chanpos]->owner;
					if (owner) {
						pbx_builtin_setvar_helper(owner, "USERUSERINFO",
							e->hangup.useruserinfo);
						ast_channel_unlock(owner);
					}
				}
#endif

				sig_pri_unlock_private(pri->pvts[chanpos]);
				sig_pri_span_devstate_changed(pri);
				break;
			case PRI_EVENT_HANGUP_REQ:
				if (sig_pri_is_cis_call(e->hangup.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->hangup.subcmds,
						e->hangup.call);
					pri_hangup(pri->pri, e->hangup.call, e->hangup.cause);
					break;
				}
				chanpos = pri_find_principle_by_call(pri, e->hangup.call);
				if (chanpos < 0) {
					/*
					 * Continue hanging up the call even though
					 * we do not remember it (if we ever did).
					 */
					pri_hangup(pri->pri, e->hangup.call, e->hangup.cause);
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->hangup.subcmds,
					e->hangup.call);
#if defined(HAVE_PRI_CALL_HOLD)
				if (e->hangup.call_active && e->hangup.call_held
					&& pri->hold_disconnect_transfer) {
					/* We are to transfer the call instead of simply hanging up. */
					sig_pri_unlock_private(pri->pvts[chanpos]);
					if (!sig_pri_attempt_transfer(pri, e->hangup.call_held, 1,
						e->hangup.call_active, 0, NULL)) {
						break;
					}
					sig_pri_lock_private(pri->pvts[chanpos]);
				}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
				switch (e->hangup.cause) {
				case PRI_CAUSE_USER_BUSY:
				case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
					sig_pri_cc_generic_check(pri, chanpos, AST_CC_CCBS);
					break;
				case PRI_CAUSE_INVALID_CALL_REFERENCE:
					/*
					 * The peer denies the existence of this call so we must
					 * continue hanging it up and forget about it.  We should not
					 * get this cause here, but for completeness we will handle it
					 * anyway.
					 */
					pri_hangup(pri->pri, e->hangup.call, e->hangup.cause);
					pri->pvts[chanpos]->call = NULL;
					break;
				default:
					break;
				}
				if (pri->pvts[chanpos]->owner) {
					snprintf(cause_str, sizeof(cause_str), "PRI PRI_EVENT_HANGUP_REQ (%d)", e->hangup.cause);
					pri_queue_pvt_cause_data(pri, chanpos, cause_str, e->hangup.cause);
				}
				if (pri->pvts[chanpos]->owner) {
					int do_hangup = 0;

					ast_channel_hangupcause_set(pri->pvts[chanpos]->owner, e->hangup.cause);
					switch (ast_channel_state(pri->pvts[chanpos]->owner)) {
					case AST_STATE_BUSY:
					case AST_STATE_UP:
						do_hangup = 1;
						break;
					default:
						if (!pri->pvts[chanpos]->outgoing) {
							/*
							 * The incoming call leg hung up before getting
							 * connected so just hangup the call.
							 */
							do_hangup = 1;
							break;
						}
						switch (e->hangup.cause) {
						case PRI_CAUSE_USER_BUSY:
							pri_queue_control(pri, chanpos, AST_CONTROL_BUSY);
							break;
						case PRI_CAUSE_CALL_REJECTED:
						case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
						case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
						case PRI_CAUSE_SWITCH_CONGESTION:
						case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
						case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
							pri_queue_control(pri, chanpos, AST_CONTROL_CONGESTION);
							break;
						default:
							do_hangup = 1;
							break;
						}
						break;
					}

					if (do_hangup) {
#if defined(HAVE_PRI_AOC_EVENTS)
						if (!pri->pvts[chanpos]->holding_aoce
							&& pri->aoce_delayhangup
							&& ast_channel_is_bridged(pri->pvts[chanpos]->owner)) {
							sig_pri_send_aoce_termination_request(pri, chanpos,
								pri_get_timer(pri->pri, PRI_TIMER_T305) / 2);
						} else
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
						{
							sig_pri_queue_hangup(pri, chanpos);
						}
					}
					ast_verb(3, "Span %d: Channel %d/%d got hangup request, cause %d\n",
						pri->span, pri->pvts[chanpos]->logicalspan,
						pri->pvts[chanpos]->prioffset, e->hangup.cause);
				} else {
					/*
					 * Continue hanging up the call even though
					 * we do not have an owner.
					 */
					pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
					pri->pvts[chanpos]->call = NULL;
				}
				if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL
					&& pri->sig != SIG_BRI_PTMP && !pri->resetting
					&& pri->force_restart_unavailable_chans
					&& pri->pvts[chanpos]->resetting == SIG_PRI_RESET_IDLE) {
					ast_verb(3,
						"Span %d: Forcing restart of channel %d/%d since channel reported in use\n",
						pri->span, pri->pvts[chanpos]->logicalspan,
						pri->pvts[chanpos]->prioffset);
					pri->pvts[chanpos]->resetting = SIG_PRI_RESET_ACTIVE;
					pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
				}

#ifdef SUPPORT_USERUSER
				if (!ast_strlen_zero(e->hangup.useruserinfo)) {
					struct ast_channel *owner;

					sig_pri_lock_owner(pri, chanpos);
					owner = pri->pvts[chanpos]->owner;
					if (owner) {
						pbx_builtin_setvar_helper(owner, "USERUSERINFO",
							e->hangup.useruserinfo);
						ast_channel_unlock(owner);
					}
				}
#endif

				sig_pri_unlock_private(pri->pvts[chanpos]);
				sig_pri_span_devstate_changed(pri);
				break;
			case PRI_EVENT_HANGUP_ACK:
				if (sig_pri_is_cis_call(e->hangup.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->hangup.subcmds,
						e->hangup.call);
					break;
				}
				chanpos = pri_find_principle_by_call(pri, e->hangup.call);
				if (chanpos < 0) {
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				pri->pvts[chanpos]->call = NULL;
				if (pri->pvts[chanpos]->owner) {
					ast_verb(3, "Span %d: Channel %d/%d got hangup ACK\n", pri->span,
						pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset);
				}
#ifdef SUPPORT_USERUSER
				if (!ast_strlen_zero(e->hangup.useruserinfo)) {
					struct ast_channel *owner;

					sig_pri_lock_owner(pri, chanpos);
					owner = pri->pvts[chanpos]->owner;
					if (owner) {
						pbx_builtin_setvar_helper(owner, "USERUSERINFO",
							e->hangup.useruserinfo);
						ast_channel_unlock(owner);
					}
				}
#endif
				sig_pri_unlock_private(pri->pvts[chanpos]);
				sig_pri_span_devstate_changed(pri);
				break;
			case PRI_EVENT_CONFIG_ERR:
				ast_log(LOG_WARNING, "PRI Error on span %d: %s\n", pri->span, e->err.err);
				break;
			case PRI_EVENT_RESTART_ACK:
				chanpos = pri_find_principle(pri, e->restartack.channel, NULL);
				if (chanpos < 0) {
					/* Sometime switches (e.g. I421 / British Telecom) don't give us the
					   channel number, so we have to figure it out...  This must be why
					   everybody resets exactly a channel at a time. */
					for (x = 0; x < pri->numchans; x++) {
						if (pri->pvts[x]
							&& pri->pvts[x]->resetting != SIG_PRI_RESET_IDLE) {
							chanpos = x;
							sig_pri_lock_private(pri->pvts[chanpos]);
							ast_debug(1,
								"Span %d: Assuming restart ack is for channel %d/%d\n",
								pri->span, pri->pvts[chanpos]->logicalspan,
								pri->pvts[chanpos]->prioffset);
							if (pri->pvts[chanpos]->owner) {
								ast_log(LOG_WARNING,
									"Span %d: Got restart ack on channel %d/%d with owner\n",
									pri->span, pri->pvts[chanpos]->logicalspan,
									pri->pvts[chanpos]->prioffset);
								ast_channel_softhangup_internal_flag_add(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
							}
							pri->pvts[chanpos]->resetting = SIG_PRI_RESET_IDLE;
							ast_verb(3,
								"Span %d: Channel %d/%d successfully restarted\n",
								pri->span, pri->pvts[chanpos]->logicalspan,
								pri->pvts[chanpos]->prioffset);
							sig_pri_unlock_private(pri->pvts[chanpos]);
							if (pri->resetting)
								pri_check_restart(pri);
							break;
						}
					}
					if (chanpos < 0) {
						ast_log(LOG_WARNING,
							"Span %d: Restart ACK on strange channel %d/%d\n",
							pri->span, PRI_SPAN(e->restartack.channel),
							PRI_CHANNEL(e->restartack.channel));
					}
				} else {
					sig_pri_lock_private(pri->pvts[chanpos]);
					if (pri->pvts[chanpos]->resetting == SIG_PRI_RESET_IDLE) {
						/* The channel is not in the resetting state. */
						ast_debug(1,
							"Span %d: Unexpected or late restart ack on channel %d/%d (Ignoring)\n",
							pri->span, pri->pvts[chanpos]->logicalspan,
							pri->pvts[chanpos]->prioffset);
						sig_pri_unlock_private(pri->pvts[chanpos]);
						break;
					}
					if (pri->pvts[chanpos]->owner) {
						ast_log(LOG_WARNING,
							"Span %d: Got restart ack on channel %d/%d with owner\n",
							pri->span, pri->pvts[chanpos]->logicalspan,
							pri->pvts[chanpos]->prioffset);
						ast_channel_softhangup_internal_flag_add(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
					}
					pri->pvts[chanpos]->resetting = SIG_PRI_RESET_IDLE;
					ast_verb(3,
						"Span %d: Channel %d/%d successfully restarted\n",
						pri->span, pri->pvts[chanpos]->logicalspan,
						pri->pvts[chanpos]->prioffset);
					sig_pri_unlock_private(pri->pvts[chanpos]);
					if (pri->resetting)
						pri_check_restart(pri);
				}
				break;
			case PRI_EVENT_SETUP_ACK:
				if (sig_pri_is_cis_call(e->setup_ack.channel)) {
					sig_pri_handle_cis_subcmds(pri, e->e, e->setup_ack.subcmds,
						e->setup_ack.call);
					break;
				}
				chanpos = pri_find_fixup_principle(pri, e->setup_ack.channel,
					e->setup_ack.call);
				if (chanpos < 0) {
					break;
				}
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

				sig_pri_handle_subcmds(pri, chanpos, e->e, e->setup_ack.subcmds,
					e->setup_ack.call);
				if (pri->pvts[chanpos]->call_level < SIG_PRI_CALL_LEVEL_OVERLAP) {
					pri->pvts[chanpos]->call_level = SIG_PRI_CALL_LEVEL_OVERLAP;
				}

				/* Send any queued digits */
				len = strlen(pri->pvts[chanpos]->dialdest);
				for (x = 0; x < len; ++x) {
					ast_debug(1, "Sending pending digit '%c'\n", pri->pvts[chanpos]->dialdest[x]);
					pri_information(pri->pri, pri->pvts[chanpos]->call,
						pri->pvts[chanpos]->dialdest[x]);
				}

				if (!pri->pvts[chanpos]->progress
					&& (pri->overlapdial & DAHDI_OVERLAPDIAL_OUTGOING)
					&& !pri->pvts[chanpos]->digital
					&& !pri->pvts[chanpos]->no_b_channel
#if defined(HAVE_PRI_SETUP_ACK_INBAND)
					/*
					 * We only care about PRI_PROG_INBAND_AVAILABLE to open the
					 * voice path.
					 *
					 * We explicitly DO NOT want to check PRI_PROG_CALL_NOT_E2E_ISDN
					 * because it will mess up ISDN to SIP interoperability for
					 * the ALERTING message.
					 *
					 * Q.931 Section 5.1.3 says that in scenarios with overlap
					 * dialing where no called digits are received and the tone
					 * option requires dialtone, the switch MAY send an inband
					 * progress indication ie to indicate dialtone presence in
					 * the SETUP ACKNOWLEDGE.  Therefore, if we did not send any
					 * digits with the SETUP then we must assume that dialtone
					 * is present and open the voice path.  Fortunately when
					 * interoperating with SIP, we should be sending digits.
					 */
					&& ((e->setup_ack.progressmask & PRI_PROG_INBAND_AVAILABLE)
						|| pri->inband_on_setup_ack
						|| pri->pvts[chanpos]->no_dialed_digits)
#endif	/* defined(HAVE_PRI_SETUP_ACK_INBAND) */
					) {
					/*
					 * Call has a channel.
					 * Indicate for overlap dialing that dialtone may be present.
					 */
					pri_queue_control(pri, chanpos, AST_CONTROL_PROGRESS);
					pri->pvts[chanpos]->progress = 1;/* Claim to have seen inband-information */
					sig_pri_set_dialing(pri->pvts[chanpos], 0);
					sig_pri_open_media(pri->pvts[chanpos]);
				}
				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
			case PRI_EVENT_NOTIFY:
				if (sig_pri_is_cis_call(e->notify.channel)) {
#if defined(HAVE_PRI_CALL_HOLD)
					sig_pri_handle_cis_subcmds(pri, e->e, e->notify.subcmds,
						e->notify.call);
#else
					sig_pri_handle_cis_subcmds(pri, e->e, e->notify.subcmds, NULL);
#endif	/* !defined(HAVE_PRI_CALL_HOLD) */
					break;
				}
#if defined(HAVE_PRI_CALL_HOLD)
				chanpos = pri_find_principle_by_call(pri, e->notify.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Span %d: Received NOTIFY for unknown call.\n",
						pri->span);
					break;
				}
#else
				/*
				 * This version of libpri does not supply a call pointer for
				 * this message.  We are just going to have to trust that the
				 * correct principle is found.
				 */
				chanpos = pri_find_principle(pri, e->notify.channel, NULL);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received NOTIFY on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->notify.channel), PRI_CHANNEL(e->notify.channel), pri->span);
					break;
				}
#endif	/* !defined(HAVE_PRI_CALL_HOLD) */
				sig_pri_lock_private(pri->pvts[chanpos]);

				callid = func_pri_dchannel_chanpos_callid(pri, chanpos);

#if defined(HAVE_PRI_CALL_HOLD)
				sig_pri_handle_subcmds(pri, chanpos, e->e, e->notify.subcmds,
					e->notify.call);
#else
				sig_pri_handle_subcmds(pri, chanpos, e->e, e->notify.subcmds, NULL);
#endif	/* !defined(HAVE_PRI_CALL_HOLD) */
				switch (e->notify.info) {
				case PRI_NOTIFY_REMOTE_HOLD:
					if (!pri->discardremoteholdretrieval) {
						sig_pri_queue_hold(pri, chanpos);
					}
					break;
				case PRI_NOTIFY_REMOTE_RETRIEVAL:
					if (!pri->discardremoteholdretrieval) {
						sig_pri_queue_unhold(pri, chanpos);
					}
					break;
				}
				sig_pri_unlock_private(pri->pvts[chanpos]);
				break;
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_HOLD:
				/* We should not be getting any CIS calls with this message type. */
				if (sig_pri_handle_hold(pri, e)) {
					pri_hold_rej(pri->pri, e->hold.call,
						PRI_CAUSE_RESOURCE_UNAVAIL_UNSPECIFIED);
				} else {
					pri_hold_ack(pri->pri, e->hold.call);
				}
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_HOLD_ACK:
				/* We should not be getting any CIS calls with this message type. */
				sig_pri_handle_hold_ack(pri, e);
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_HOLD_REJ:
				/* We should not be getting any CIS calls with this message type. */
				sig_pri_handle_hold_rej(pri, e);
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_RETRIEVE:
				/* We should not be getting any CIS calls with this message type. */
				sig_pri_handle_retrieve(pri, e);
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_RETRIEVE_ACK:
				/* We should not be getting any CIS calls with this message type. */
				sig_pri_handle_retrieve_ack(pri, e);
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_RETRIEVE_REJ:
				/* We should not be getting any CIS calls with this message type. */
				sig_pri_handle_retrieve_rej(pri, e);
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
			default:
				ast_debug(1, "Span: %d Unhandled event: %s(%d)\n",
					pri->span, pri_event2str(e->e), e->e);
				break;
			}

			/* If a callid was set, we need to deref it and remove it from thread storage. */
			if (callid) {
				callid = ast_callid_unref(callid);
				ast_callid_threadassoc_remove();
			}
		}
		ast_mutex_unlock(&pri->lock);
	}
	/* Never reached */
	return NULL;
}

/*!
 * \brief Output AMI show spans response events for the given PRI span.
 * \since 10.0
 *
 * \param show_cmd AMI command name
 * \param s AMI session to output span information.
 * \param pri PRI span control structure.
 * \param dchannels Array of D channel channel numbers.
 * \param action_id Action ID line to use.
 *
 * \return Number of D channels on this span.
 */
int sig_pri_ami_show_spans(struct mansession *s, const char *show_cmd, struct sig_pri_span *pri, const int *dchannels, const char *action_id)
{
	int count;
	int x;

	count = 0;
	for (x = 0; x < ARRAY_LEN(pri->dchans); ++x) {
		if (pri->dchans[x]) {
			++count;

			astman_append(s,
				"Event: %s\r\n"
				"Span: %d\r\n"
				"DChannel: %d\r\n"
				"Order: %s\r\n"
				"Active: %s\r\n"
				"Alarm: %s\r\n"
				"Up: %s\r\n"
				"%s"
				"\r\n",
				show_cmd,
				pri->span,
				dchannels[x],
				pri_order(x),
				(pri->dchans[x] == pri->pri) ? "Yes" : "No",
				(pri->dchanavail[x] & DCHAN_NOTINALARM) ? "No" : "Yes",
				(pri->dchanavail[x] & DCHAN_UP) ? "Yes" : "No",
				action_id
				);
		}
	}
	return count;
}

void sig_pri_init_pri(struct sig_pri_span *pri)
{
	int i;

	memset(pri, 0, sizeof(*pri));

	ast_mutex_init(&pri->lock);

	pri->master = AST_PTHREADT_NULL;
	for (i = 0; i < SIG_PRI_NUM_DCHANS; i++)
		pri->fds[i] = -1;
}

int sig_pri_hangup(struct sig_pri_chan *p, struct ast_channel *ast)
{
	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);
	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	sig_pri_set_outgoing(p, 0);
	sig_pri_set_digital(p, 0);	/* push up to parent for EC*/
#if defined(HAVE_PRI_CALL_WAITING)
	if (p->is_call_waiting) {
		p->is_call_waiting = 0;
		ast_atomic_fetchadd_int(&p->pri->num_call_waiting_calls, -1);
	}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
	p->call_level = SIG_PRI_CALL_LEVEL_IDLE;
	p->progress = 0;
	p->cid_num[0] = '\0';
	p->cid_subaddr[0] = '\0';
	p->cid_name[0] = '\0';
	p->user_tag[0] = '\0';
	p->exten[0] = '\0';
	sig_pri_set_dialing(p, 0);

	/* Make sure we really have a call */
	pri_grab(p, p->pri);
	sig_pri_moh_fsm_event(ast, p, SIG_PRI_MOH_EVENT_RESET);
	if (p->call) {
#if defined(SUPPORT_USERUSER)
		const char *useruser = pbx_builtin_getvar_helper(ast, "USERUSERINFO");

		if (!ast_strlen_zero(useruser)) {
			pri_call_set_useruser(p->call, useruser);
		}
#endif	/* defined(SUPPORT_USERUSER) */

#if defined(HAVE_PRI_TRANSFER)
		if (p->xfer_data) {
			/*
			 * The transferrer call leg is disconnecting.  It must mean that
			 * the transfer was successful and the core is disconnecting the
			 * call legs involved.
			 *
			 * The transfer protocol response message must go out before the
			 * call leg is disconnected.
			 */
			sig_pri_transfer_rsp(p->xfer_data, 1);
		}
#endif	/* defined(HAVE_PRI_TRANSFER) */

#if defined(HAVE_PRI_AOC_EVENTS)
		if (p->holding_aoce) {
			pri_aoc_e_send(p->pri->pri, p->call, &p->aoc_e);
		}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

		if (p->alreadyhungup) {
			ast_debug(1, "Already hungup...  Calling hangup once, and clearing call\n");

			pri_hangup(p->pri->pri, p->call, -1);
			p->call = NULL;
		} else {
			const char *cause = pbx_builtin_getvar_helper(ast,"PRI_CAUSE");
			int icause = ast_channel_hangupcause(ast) ? ast_channel_hangupcause(ast) : -1;

			p->alreadyhungup = 1;
			if (!ast_strlen_zero(cause)) {
				if (atoi(cause)) {
					icause = atoi(cause);
				}
			}
			ast_debug(1,
				"Not yet hungup...  Calling hangup with cause %d, and clearing call\n",
				icause);

			pri_hangup(p->pri->pri, p->call, icause);
		}
	}
#if defined(HAVE_PRI_TRANSFER)
	p->xfer_data = NULL;
#endif	/* defined(HAVE_PRI_TRANSFER) */
#if defined(HAVE_PRI_AOC_EVENTS)
	p->aoc_s_request_invoke_id_valid = 0;
	p->holding_aoce = 0;
	p->waiting_for_aoce = 0;
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

	p->allocated = 0;
	p->owner = NULL;

	sig_pri_span_devstate_changed(p->pri);
	pri_rel(p->pri);
	return 0;
}

/*!
 * \brief Extract the called number and subaddress from the dial string.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 * \param rdest Dial string buffer to extract called number and subaddress.
 * \param called Buffer to fill with extracted <number>[:<subaddress>]
 * \param called_buff_size Size of buffer to fill.
 *
 * \note Parsing must remain in sync with sig_pri_call().
 *
 * \return Nothing
 */
void sig_pri_extract_called_num_subaddr(struct sig_pri_chan *p, const char *rdest, char *called, size_t called_buff_size)
{
	char *dial;
	char *number;
	char *subaddr;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);	/* channel/group token */
		AST_APP_ARG(ext);	/* extension token */
		//AST_APP_ARG(opts);	/* options token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	/* Get private copy of dial string and break it up. */
	dial = ast_strdupa(rdest);
	AST_NONSTANDARD_APP_ARGS(args, dial, '/');

	number = args.ext;
	if (!number) {
		number = "";
	}

	/* Find and extract dialed_subaddress */
	subaddr = strchr(number, ':');
	if (subaddr) {
		*subaddr++ = '\0';

		/* Skip subaddress type prefix. */
		switch (*subaddr) {
		case 'U':
		case 'u':
		case 'N':
		case 'n':
			++subaddr;
			break;
		default:
			break;
		}
	}

	/* Skip type-of-number/dial-plan prefix characters. */
	if (strlen(number) < p->stripmsd) {
		number = "";
	} else {
		char *deferred;

		number += p->stripmsd;
		deferred = strchr(number, 'w');
		if (deferred) {
			/* Remove any 'w' deferred digits. */
			*deferred = '\0';
		}
		while (isalpha(*number)) {
			++number;
		}
	}

	/* Fill buffer with extracted number and subaddress. */
	if (ast_strlen_zero(subaddr)) {
		/* Put in called number only since there is no subaddress. */
		snprintf(called, called_buff_size, "%s", number);
	} else {
		/* Put in called number and subaddress. */
		snprintf(called, called_buff_size, "%s:%s", number, subaddr);
	}
}

enum SIG_PRI_CALL_OPT_FLAGS {
	OPT_KEYPAD =         (1 << 0),
	OPT_REVERSE_CHARGE = (1 << 1),	/* Collect call */
	OPT_AOC_REQUEST =    (1 << 2),	/* AOC Request */
};
enum SIG_PRI_CALL_OPT_ARGS {
	OPT_ARG_KEYPAD = 0,
	OPT_ARG_AOC_REQUEST,

	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(sig_pri_call_opts, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('K', OPT_KEYPAD, OPT_ARG_KEYPAD),
	AST_APP_OPTION('R', OPT_REVERSE_CHARGE),
	AST_APP_OPTION_ARG('A', OPT_AOC_REQUEST, OPT_ARG_AOC_REQUEST),
END_OPTIONS);

/*! \note Parsing must remain in sync with sig_pri_extract_called_num_subaddr(). */
int sig_pri_call(struct sig_pri_chan *p, struct ast_channel *ast, const char *rdest, int timeout, int layer1)
{
	char dest[256]; /* must be same length as p->dialdest */
	struct ast_party_subaddress dialed_subaddress; /* Called subaddress */
	struct pri_sr *sr;
	char *c, *l, *n, *s;
#ifdef SUPPORT_USERUSER
	const char *useruser;
#endif
	int core_id;
	int pridialplan;
	int dp_strip;
	int prilocaldialplan;
	int ldp_strip;
	int exclusive;
#if defined(HAVE_PRI_SETUP_KEYPAD)
	const char *keypad;
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);	/* channel/group token */
		AST_APP_ARG(ext);	/* extension token */
		AST_APP_ARG(opts);	/* options token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);
	struct ast_flags opts;
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct ast_party_id connected_id = ast_channel_connected_effective_id(ast);

	ast_debug(1, "CALLER NAME: %s NUM: %s\n",
		S_COR(connected_id.name.valid, connected_id.name.str, ""),
		S_COR(connected_id.number.valid, connected_id.number.str, ""));

	if (!p->pri) {
		ast_log(LOG_ERROR, "Could not find pri on channel %d\n", p->channel);
		return -1;
	}

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "sig_pri_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}

	p->dialdest[0] = '\0';
	sig_pri_set_outgoing(p, 1);

	ast_copy_string(dest, rdest, sizeof(dest));
	AST_NONSTANDARD_APP_ARGS(args, dest, '/');
	if (ast_app_parse_options(sig_pri_call_opts, &opts, opt_args, args.opts)) {
		/* General invalid option syntax. */
		return -1;
	}

	c = args.ext;
	if (!c) {
		c = "";
	}

	/* setup dialed_subaddress if found */
	ast_party_subaddress_init(&dialed_subaddress);
	s = strchr(c, ':');
	if (s) {
		*s = '\0';
		s++;
		/* prefix */
		/* 'n' = NSAP */
		/* 'u' = User Specified */
		/* Default = NSAP */
		switch (*s) {
		case 'U':
		case 'u':
			s++;
			dialed_subaddress.type = 2;
			break;
		case 'N':
		case 'n':
			s++;
			/* default already covered with ast_party_subaddress_init */
			break;
		}
		dialed_subaddress.str = s;
		dialed_subaddress.valid = 1;
	}

	l = NULL;
	n = NULL;
	if (!p->hidecallerid) {
		if (connected_id.number.valid) {
			/* If we get to the end of this loop without breaking, there's no
			 * calleridnum.  This is done instead of testing for "unknown" or
			 * the thousands of other ways that the calleridnum could be
			 * invalid. */
			for (l = connected_id.number.str; l && *l; l++) {
				if (strchr("0123456789", *l)) {
					l = connected_id.number.str;
					break;
				}
			}
		} else {
			l = NULL;
		}
		if (!p->hidecalleridname) {
			n = connected_id.name.valid ? connected_id.name.str : NULL;
		}
	}

	if (strlen(c) < p->stripmsd) {
		ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
		return -1;
	}

	/* Extract any 'w' deferred digits. */
	s = strchr(c + p->stripmsd, 'w');
	if (s) {
		*s++ = '\0';
		ast_copy_string(p->deferred_digits, s, sizeof(p->deferred_digits));
		/*
		 * Since we have a 'w', this means that there will not be any
		 * more normal dialed digits.  Therefore, the sending complete
		 * ie needs to be sent with any normal digits.
		 */
	} else {
		p->deferred_digits[0] = '\0';
	}

	pri_grab(p, p->pri);
	if (!(p->call = pri_new_call(p->pri->pri))) {
		ast_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
		pri_rel(p->pri);
		return -1;
	}
	if (!(sr = pri_sr_new())) {
		ast_log(LOG_WARNING, "Failed to allocate setup request on channel %d\n",
			p->channel);
		pri_destroycall(p->pri->pri, p->call);
		p->call = NULL;
		pri_rel(p->pri);
		return -1;
	}

	sig_pri_set_digital(p, IS_DIGITAL(ast_channel_transfercapability(ast)));	/* push up to parent for EC */

#if defined(HAVE_PRI_CALL_WAITING)
	if (p->is_call_waiting) {
		/*
		 * Indicate that this is a call waiting call.
		 * i.e., Normal call but with no B channel.
		 */
		pri_sr_set_channel(sr, 0, 0, 1);
	} else
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
	{
		/* Should the picked channel be used exclusively? */
		if (p->priexclusive || p->pri->nodetype == PRI_NETWORK) {
			exclusive = 1;
		} else {
			exclusive = 0;
		}
		pri_sr_set_channel(sr, PVT_TO_CHANNEL(p), exclusive, 1);
	}

	pri_sr_set_bearer(sr, p->digital ? PRI_TRANS_CAP_DIGITAL : ast_channel_transfercapability(ast),
		(p->digital ? -1 : layer1));

	if (p->pri->facilityenable)
		pri_facility_enable(p->pri->pri);

	ast_verb(3, "Requested transfer capability: 0x%02hx - %s\n", ast_channel_transfercapability(ast), ast_transfercapability2str(ast_channel_transfercapability(ast)));
	dp_strip = 0;
	pridialplan = p->pri->dialplan - 1;
	if (pridialplan == -2 || pridialplan == -3) { /* compute dynamically */
		if (strncmp(c + p->stripmsd, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
			if (pridialplan == -2) {
				dp_strip = strlen(p->pri->internationalprefix);
			}
			pridialplan = PRI_INTERNATIONAL_ISDN;
		} else if (strncmp(c + p->stripmsd, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
			if (pridialplan == -2) {
				dp_strip = strlen(p->pri->nationalprefix);
			}
			pridialplan = PRI_NATIONAL_ISDN;
		} else {
			pridialplan = PRI_LOCAL_ISDN;
		}
	}
	while (c[p->stripmsd] > '9' && c[p->stripmsd] != '*' && c[p->stripmsd] != '#') {
		switch (c[p->stripmsd]) {
		case 'U':
			pridialplan = (PRI_TON_UNKNOWN << 4) | (pridialplan & 0xf);
			break;
		case 'I':
			pridialplan = (PRI_TON_INTERNATIONAL << 4) | (pridialplan & 0xf);
			break;
		case 'N':
			pridialplan = (PRI_TON_NATIONAL << 4) | (pridialplan & 0xf);
			break;
		case 'L':
			pridialplan = (PRI_TON_NET_SPECIFIC << 4) | (pridialplan & 0xf);
			break;
		case 'S':
			pridialplan = (PRI_TON_SUBSCRIBER << 4) | (pridialplan & 0xf);
			break;
		case 'V':
			pridialplan = (PRI_TON_ABBREVIATED << 4) | (pridialplan & 0xf);
			break;
		case 'R':
			pridialplan = (PRI_TON_RESERVED << 4) | (pridialplan & 0xf);
			break;
		case 'u':
			pridialplan = PRI_NPI_UNKNOWN | (pridialplan & 0xf0);
			break;
		case 'e':
			pridialplan = PRI_NPI_E163_E164 | (pridialplan & 0xf0);
			break;
		case 'x':
			pridialplan = PRI_NPI_X121 | (pridialplan & 0xf0);
			break;
		case 'f':
			pridialplan = PRI_NPI_F69 | (pridialplan & 0xf0);
			break;
		case 'n':
			pridialplan = PRI_NPI_NATIONAL | (pridialplan & 0xf0);
			break;
		case 'p':
			pridialplan = PRI_NPI_PRIVATE | (pridialplan & 0xf0);
			break;
		case 'r':
			pridialplan = PRI_NPI_RESERVED | (pridialplan & 0xf0);
			break;
		default:
			if (isalpha(c[p->stripmsd])) {
				ast_log(LOG_WARNING, "Unrecognized pridialplan %s modifier: %c\n",
					c[p->stripmsd] > 'Z' ? "NPI" : "TON", c[p->stripmsd]);
			}
			break;
		}
		c++;
	}
#if defined(HAVE_PRI_SETUP_KEYPAD)
	if (ast_test_flag(&opts, OPT_KEYPAD)
		&& !ast_strlen_zero(opt_args[OPT_ARG_KEYPAD])) {
		/* We have a keypad facility digits option with digits. */
		keypad = opt_args[OPT_ARG_KEYPAD];
		pri_sr_set_keypad_digits(sr, keypad);
	} else {
		keypad = NULL;
	}
	if (!keypad || !ast_strlen_zero(c + p->stripmsd + dp_strip))
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */
	{
		char *called = c + p->stripmsd + dp_strip;

		pri_sr_set_called(sr, called, pridialplan, s ? 1 : 0);
#if defined(HAVE_PRI_SETUP_ACK_INBAND)
		p->no_dialed_digits = !called[0];
#endif	/* defined(HAVE_PRI_SETUP_ACK_INBAND) */
	}

#if defined(HAVE_PRI_SUBADDR)
	if (dialed_subaddress.valid) {
		struct pri_party_subaddress subaddress;

		memset(&subaddress, 0, sizeof(subaddress));
		sig_pri_party_subaddress_from_ast(&subaddress, &dialed_subaddress);
		pri_sr_set_called_subaddress(sr, &subaddress);
	}
#endif	/* defined(HAVE_PRI_SUBADDR) */
#if defined(HAVE_PRI_REVERSE_CHARGE)
	if (ast_test_flag(&opts, OPT_REVERSE_CHARGE)) {
		pri_sr_set_reversecharge(sr, PRI_REVERSECHARGE_REQUESTED);
	}
#endif	/* defined(HAVE_PRI_REVERSE_CHARGE) */
#if defined(HAVE_PRI_AOC_EVENTS)
	if (ast_test_flag(&opts, OPT_AOC_REQUEST)
		&& !ast_strlen_zero(opt_args[OPT_ARG_AOC_REQUEST])) {
		if (strchr(opt_args[OPT_ARG_AOC_REQUEST], 's')) {
			pri_sr_set_aoc_charging_request(sr, PRI_AOC_REQUEST_S);
		}
		if (strchr(opt_args[OPT_ARG_AOC_REQUEST], 'd')) {
			pri_sr_set_aoc_charging_request(sr, PRI_AOC_REQUEST_D);
		}
		if (strchr(opt_args[OPT_ARG_AOC_REQUEST], 'e')) {
			pri_sr_set_aoc_charging_request(sr, PRI_AOC_REQUEST_E);
		}
	}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */

	/* Setup the user tag for party id's from this device for this call. */
	if (p->pri->append_msn_to_user_tag) {
		snprintf(p->user_tag, sizeof(p->user_tag), "%s_%s", p->pri->initial_user_tag,
			p->pri->nodetype == PRI_NETWORK
				? c + p->stripmsd + dp_strip
				: S_COR(ast_channel_connected(ast)->id.number.valid,
					ast_channel_connected(ast)->id.number.str, ""));
	} else {
		ast_copy_string(p->user_tag, p->pri->initial_user_tag, sizeof(p->user_tag));
	}

	/*
	 * Replace the caller id tag from the channel creation
	 * with the actual tag value.
	 */
	ast_free(ast_channel_caller(ast)->id.tag);
	ast_channel_caller(ast)->id.tag = ast_strdup(p->user_tag);

	ldp_strip = 0;
	prilocaldialplan = p->pri->localdialplan - 1;
	if ((l != NULL) && (prilocaldialplan == -2 || prilocaldialplan == -3)) { /* compute dynamically */
		if (strncmp(l, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
			if (prilocaldialplan == -2) {
				ldp_strip = strlen(p->pri->internationalprefix);
			}
			prilocaldialplan = PRI_INTERNATIONAL_ISDN;
		} else if (strncmp(l, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
			if (prilocaldialplan == -2) {
				ldp_strip = strlen(p->pri->nationalprefix);
			}
			prilocaldialplan = PRI_NATIONAL_ISDN;
		} else {
			prilocaldialplan = PRI_LOCAL_ISDN;
		}
	} else if (prilocaldialplan == -1) {
		/* Use the numbering plan passed in. */
		prilocaldialplan = connected_id.number.plan;
	}
	if (l != NULL) {
		while (*l > '9' && *l != '*' && *l != '#') {
			switch (*l) {
			case 'U':
				prilocaldialplan = (PRI_TON_UNKNOWN << 4) | (prilocaldialplan & 0xf);
				break;
			case 'I':
				prilocaldialplan = (PRI_TON_INTERNATIONAL << 4) | (prilocaldialplan & 0xf);
				break;
			case 'N':
				prilocaldialplan = (PRI_TON_NATIONAL << 4) | (prilocaldialplan & 0xf);
				break;
			case 'L':
				prilocaldialplan = (PRI_TON_NET_SPECIFIC << 4) | (prilocaldialplan & 0xf);
				break;
			case 'S':
				prilocaldialplan = (PRI_TON_SUBSCRIBER << 4) | (prilocaldialplan & 0xf);
				break;
			case 'V':
				prilocaldialplan = (PRI_TON_ABBREVIATED << 4) | (prilocaldialplan & 0xf);
				break;
			case 'R':
				prilocaldialplan = (PRI_TON_RESERVED << 4) | (prilocaldialplan & 0xf);
				break;
			case 'u':
				prilocaldialplan = PRI_NPI_UNKNOWN | (prilocaldialplan & 0xf0);
				break;
			case 'e':
				prilocaldialplan = PRI_NPI_E163_E164 | (prilocaldialplan & 0xf0);
				break;
			case 'x':
				prilocaldialplan = PRI_NPI_X121 | (prilocaldialplan & 0xf0);
				break;
			case 'f':
				prilocaldialplan = PRI_NPI_F69 | (prilocaldialplan & 0xf0);
				break;
			case 'n':
				prilocaldialplan = PRI_NPI_NATIONAL | (prilocaldialplan & 0xf0);
				break;
			case 'p':
				prilocaldialplan = PRI_NPI_PRIVATE | (prilocaldialplan & 0xf0);
				break;
			case 'r':
				prilocaldialplan = PRI_NPI_RESERVED | (prilocaldialplan & 0xf0);
				break;
			default:
				if (isalpha(*l)) {
					ast_log(LOG_WARNING,
						"Unrecognized prilocaldialplan %s modifier: %c\n",
						*l > 'Z' ? "NPI" : "TON", *l);
				}
				break;
			}
			l++;
		}
	}
	pri_sr_set_caller(sr, l ? (l + ldp_strip) : NULL, n, prilocaldialplan,
		p->use_callingpres ? connected_id.number.presentation : (l ? PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN : PRES_NUMBER_NOT_AVAILABLE));

#if defined(HAVE_PRI_SUBADDR)
	if (connected_id.subaddress.valid) {
		struct pri_party_subaddress subaddress;

		memset(&subaddress, 0, sizeof(subaddress));
		sig_pri_party_subaddress_from_ast(&subaddress, &connected_id.subaddress);
		pri_sr_set_caller_subaddress(sr, &subaddress);
	}
#endif	/* defined(HAVE_PRI_SUBADDR) */

	sig_pri_redirecting_update(p, ast);

#ifdef SUPPORT_USERUSER
	/* User-user info */
	useruser = pbx_builtin_getvar_helper(p->owner, "USERUSERINFO");
	if (useruser)
		pri_sr_set_useruser(sr, useruser);
#endif

#if defined(HAVE_PRI_CCSS)
	if (ast_cc_is_recall(ast, &core_id, sig_pri_cc_type_name)) {
		struct ast_cc_monitor *monitor;
		char device_name[AST_CHANNEL_NAME];

		/* This is a CC recall call. */
		ast_channel_get_device_name(ast, device_name, sizeof(device_name));
		monitor = ast_cc_get_monitor_by_recall_core_id(core_id, device_name);
		if (monitor) {
			struct sig_pri_cc_monitor_instance *instance;

			instance = monitor->private_data;

			/* If this fails then we have monitor instance ambiguity. */
			ast_assert(p->pri == instance->pri);

			if (pri_cc_call(p->pri->pri, instance->cc_id, p->call, sr)) {
				/* The CC recall call failed for some reason. */
				ast_log(LOG_WARNING, "Unable to setup CC recall call to device %s\n",
					device_name);
				ao2_ref(monitor, -1);
				pri_destroycall(p->pri->pri, p->call);
				p->call = NULL;
				pri_rel(p->pri);
				pri_sr_free(sr);
				return -1;
			}
			ao2_ref(monitor, -1);
		} else {
			core_id = -1;
		}
	} else
#endif	/* defined(HAVE_PRI_CCSS) */
	{
		core_id = -1;
	}
	if (core_id == -1 && pri_setup(p->pri->pri, p->call, sr)) {
		ast_log(LOG_WARNING, "Unable to setup call to %s (using %s)\n",
			c + p->stripmsd + dp_strip, dialplan2str(p->pri->dialplan));
		pri_destroycall(p->pri->pri, p->call);
		p->call = NULL;
		pri_rel(p->pri);
		pri_sr_free(sr);
		return -1;
	}
	p->call_level = SIG_PRI_CALL_LEVEL_SETUP;
	pri_sr_free(sr);
	ast_setstate(ast, AST_STATE_DIALING);
	sig_pri_set_dialing(p, 1);
	pri_rel(p->pri);
	return 0;
}

int sig_pri_indicate(struct sig_pri_chan *p, struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
	int res = -1;

	switch (condition) {
	case AST_CONTROL_BUSY:
		if (p->priindication_oob || p->no_b_channel) {
			ast_channel_hangupcause_set(chan, AST_CAUSE_USER_BUSY);
			ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
			res = 0;
			break;
		}
		res = sig_pri_play_tone(p, SIG_PRI_TONE_BUSY);
		if (p->call_level < SIG_PRI_CALL_LEVEL_ALERTING && !p->outgoing) {
			ast_channel_hangupcause_set(chan, AST_CAUSE_USER_BUSY);
			p->progress = 1;/* No need to send plain PROGRESS after this. */
			if (p->pri && p->pri->pri) {
				pri_grab(p, p->pri);
#ifdef HAVE_PRI_PROG_W_CAUSE
				pri_progress_with_cause(p->pri->pri, p->call, PVT_TO_CHANNEL(p), 1, ast_channel_hangupcause(chan));
#else
				pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
				pri_rel(p->pri);
			}
		}
		break;
	case AST_CONTROL_RINGING:
		if (p->call_level < SIG_PRI_CALL_LEVEL_ALERTING && !p->outgoing) {
			p->call_level = SIG_PRI_CALL_LEVEL_ALERTING;
			if (p->pri && p->pri->pri) {
				pri_grab(p, p->pri);
				pri_acknowledge(p->pri->pri,p->call, PVT_TO_CHANNEL(p),
					p->no_b_channel || p->digital ? 0 : 1);
				pri_rel(p->pri);
			}
		}
		res = sig_pri_play_tone(p, SIG_PRI_TONE_RINGTONE);
		if (ast_channel_state(chan) != AST_STATE_UP) {
			if (ast_channel_state(chan) != AST_STATE_RING)
				ast_setstate(chan, AST_STATE_RINGING);
		}
		break;
	case AST_CONTROL_PROCEEDING:
		ast_debug(1, "Received AST_CONTROL_PROCEEDING on %s\n",ast_channel_name(chan));
		if (p->call_level < SIG_PRI_CALL_LEVEL_PROCEEDING && !p->outgoing) {
			p->call_level = SIG_PRI_CALL_LEVEL_PROCEEDING;
			if (p->pri && p->pri->pri) {
				pri_grab(p, p->pri);
				pri_proceeding(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 0);
				pri_rel(p->pri);
			}
		}
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_PROGRESS:
		ast_debug(1, "Received AST_CONTROL_PROGRESS on %s\n",ast_channel_name(chan));
		sig_pri_set_digital(p, 0);	/* Digital-only calls isn't allowing any inband progress messages */
		if (!p->progress && p->call_level < SIG_PRI_CALL_LEVEL_ALERTING && !p->outgoing
			&& !p->no_b_channel) {
			p->progress = 1;/* No need to send plain PROGRESS again. */
			if (p->pri && p->pri->pri) {
				pri_grab(p, p->pri);
#ifdef HAVE_PRI_PROG_W_CAUSE
				pri_progress_with_cause(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1, -1);  /* no cause at all */
#else
				pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
				pri_rel(p->pri);
			}
		}
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_INCOMPLETE:
		/* If we are connected or if we support overlap dialing, wait for additional digits */
		if (p->call_level == SIG_PRI_CALL_LEVEL_CONNECT || (p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) {
			res = 0;
			break;
		}
		/* Otherwise, treat as congestion */
		ast_channel_hangupcause_set(chan, AST_CAUSE_INVALID_NUMBER_FORMAT);
		/* Falls through */
	case AST_CONTROL_CONGESTION:
		if (p->priindication_oob || p->no_b_channel) {
			/* There are many cause codes that generate an AST_CONTROL_CONGESTION. */
			switch (ast_channel_hangupcause(chan)) {
			case AST_CAUSE_USER_BUSY:
			case AST_CAUSE_NORMAL_CLEARING:
			case 0:/* Cause has not been set. */
				/* Supply a more appropriate cause. */
				ast_channel_hangupcause_set(chan, AST_CAUSE_SWITCH_CONGESTION);
				break;
			default:
				break;
			}
			ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
			res = 0;
			break;
		}
		res = sig_pri_play_tone(p, SIG_PRI_TONE_CONGESTION);
		if (p->call_level < SIG_PRI_CALL_LEVEL_ALERTING && !p->outgoing) {
			/* There are many cause codes that generate an AST_CONTROL_CONGESTION. */
			switch (ast_channel_hangupcause(chan)) {
			case AST_CAUSE_USER_BUSY:
			case AST_CAUSE_NORMAL_CLEARING:
			case 0:/* Cause has not been set. */
				/* Supply a more appropriate cause. */
				ast_channel_hangupcause_set(chan, AST_CAUSE_SWITCH_CONGESTION);
				break;
			default:
				break;
			}
			p->progress = 1;/* No need to send plain PROGRESS after this. */
			if (p->pri && p->pri->pri) {
				pri_grab(p, p->pri);
#ifdef HAVE_PRI_PROG_W_CAUSE
				pri_progress_with_cause(p->pri->pri, p->call, PVT_TO_CHANNEL(p), 1, ast_channel_hangupcause(chan));
#else
				pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
				pri_rel(p->pri);
			}
		}
		break;
	case AST_CONTROL_HOLD:
		ast_copy_string(p->moh_suggested, S_OR(data, ""), sizeof(p->moh_suggested));
		if (p->pri) {
			pri_grab(p, p->pri);
			sig_pri_moh_fsm_event(chan, p, SIG_PRI_MOH_EVENT_HOLD);
			pri_rel(p->pri);
		} else {
			/* Something is wrong here.  A PRI channel without the pri pointer? */
			ast_moh_start(chan, data, p->mohinterpret);
		}
		break;
	case AST_CONTROL_UNHOLD:
		if (p->pri) {
			pri_grab(p, p->pri);
			sig_pri_moh_fsm_event(chan, p, SIG_PRI_MOH_EVENT_UNHOLD);
			pri_rel(p->pri);
		} else {
			/* Something is wrong here.  A PRI channel without the pri pointer? */
			ast_moh_stop(chan);
		}
		break;
	case AST_CONTROL_SRCUPDATE:
		res = 0;
		break;
	case -1:
		res = sig_pri_play_tone(p, -1);
		break;
	case AST_CONTROL_CONNECTED_LINE:
		ast_debug(1, "Received AST_CONTROL_CONNECTED_LINE on %s\n", ast_channel_name(chan));
		if (p->pri) {
			struct pri_party_connected_line connected;
			int dialplan;
			int prefix_strip;
			int colp_allowed = 0;
			struct ast_party_id connected_id = ast_channel_connected_effective_id(chan);

			pri_grab(p, p->pri);

			/* Check if a connected line update is allowed at this time. */
			switch (p->pri->colp_send) {
			case SIG_PRI_COLP_BLOCK:
				break;
			case SIG_PRI_COLP_CONNECT:
				/*
				 * Outgoing calls receive CONNECT and act like an update before
				 * the call is connected.
				 */
				if (p->call_level <= SIG_PRI_CALL_LEVEL_ALERTING && !p->outgoing) {
					colp_allowed = 1;
				}
				break;
			case SIG_PRI_COLP_UPDATE:
				colp_allowed = 1;
				break;
			}
			if (!colp_allowed) {
				pri_rel(p->pri);
				ast_debug(1, "Blocked AST_CONTROL_CONNECTED_LINE on %s\n",
					ast_channel_name(chan));
				break;
			}

			memset(&connected, 0, sizeof(connected));
			sig_pri_party_id_from_ast(&connected.id, &connected_id);

			/* Determine the connected line numbering plan to actually use. */
			switch (p->pri->cpndialplan) {
			case -2:/* redundant */
			case -1:/* dynamic */
				/* compute dynamically */
				prefix_strip = 0;
				if (!strncmp(connected.id.number.str, p->pri->internationalprefix,
					strlen(p->pri->internationalprefix))) {
					prefix_strip = strlen(p->pri->internationalprefix);
					dialplan = PRI_INTERNATIONAL_ISDN;
				} else if (!strncmp(connected.id.number.str, p->pri->nationalprefix,
					strlen(p->pri->nationalprefix))) {
					prefix_strip = strlen(p->pri->nationalprefix);
					dialplan = PRI_NATIONAL_ISDN;
				} else {
					dialplan = PRI_LOCAL_ISDN;
				}
				connected.id.number.plan = dialplan;

				if (prefix_strip && p->pri->cpndialplan != -2) {
					/* Strip the prefix from the connected line number. */
					memmove(connected.id.number.str,
						connected.id.number.str + prefix_strip,
						strlen(connected.id.number.str + prefix_strip) + 1);
				}
				break;
			case 0:/* from_channel */
				/* Use the numbering plan passed in. */
				break;
			default:
				connected.id.number.plan = p->pri->cpndialplan - 1;
				break;
			}

			pri_connected_line_update(p->pri->pri, p->call, &connected);
			pri_rel(p->pri);
		}
		break;
	case AST_CONTROL_REDIRECTING:
		ast_debug(1, "Received AST_CONTROL_REDIRECTING on %s\n", ast_channel_name(chan));
		if (p->pri) {
			pri_grab(p, p->pri);
			sig_pri_redirecting_update(p, chan);
			pri_rel(p->pri);
		}
		break;
	case AST_CONTROL_AOC:
#if defined(HAVE_PRI_AOC_EVENTS)
		{
			struct ast_aoc_decoded *decoded
				= ast_aoc_decode((struct ast_aoc_encoded *) data, datalen, chan);
			ast_debug(1, "Received AST_CONTROL_AOC on %s\n", ast_channel_name(chan));
			if (decoded && p->pri) {
				pri_grab(p, p->pri);
				switch (ast_aoc_get_msg_type(decoded)) {
				case AST_AOC_S:
					if (p->pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_S) {
						sig_pri_aoc_s_from_ast(p, decoded);
					}
					break;
				case AST_AOC_D:
					if (p->pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_D) {
						sig_pri_aoc_d_from_ast(p, decoded);
					}
					break;
				case AST_AOC_E:
					if (p->pri->aoc_passthrough_flag & SIG_PRI_AOC_GRANT_E) {
						sig_pri_aoc_e_from_ast(p, decoded);
					}
					/*
					 * If hangup was delayed for this AOC-E msg, waiting_for_aoc
					 * will be set.  A hangup is already occuring via a timeout during
					 * this delay.  Instead of waiting for that timeout to occur, go ahead
					 * and initiate the hangup since the delay is no longer necessary.
					 */
					if (p->waiting_for_aoce) {
						p->waiting_for_aoce = 0;
						ast_debug(1,
							"Received final AOC-E msg, continue with hangup on %s\n",
							ast_channel_name(chan));
						ast_queue_hangup(chan);
					}
					break;
				case AST_AOC_REQUEST:
					/* We do not pass through AOC requests, So unless this
					 * is an AOC termination request it will be ignored */
					if (ast_aoc_get_termination_request(decoded)) {
						pri_hangup(p->pri->pri, p->call, -1);
					}
					break;
				default:
					break;
				}
				pri_rel(p->pri);
			}
			ast_aoc_destroy_decoded(decoded);
		}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
		break;
#if defined(HAVE_PRI_MCID)
	case AST_CONTROL_MCID:
		if (p->pri && p->pri->pri && p->pri->mcid_send) {
			pri_grab(p, p->pri);
			pri_mcid_req_send(p->pri->pri, p->call);
			pri_rel(p->pri);
		}
		break;
#endif	/* defined(HAVE_PRI_MCID) */
	}

	return res;
}

int sig_pri_answer(struct sig_pri_chan *p, struct ast_channel *ast)
{
	int res;

	/* Send a pri acknowledge */
	pri_grab(p, p->pri);
#if defined(HAVE_PRI_AOC_EVENTS)
	if (p->aoc_s_request_invoke_id_valid) {
		/* if AOC-S was requested and the invoke id is still present on answer.  That means
		 * no AOC-S rate list was provided, so send a NULL response which will indicate that
		 * AOC-S is not available */
		pri_aoc_s_request_response_send(p->pri->pri, p->call,
			p->aoc_s_request_invoke_id, NULL);
		p->aoc_s_request_invoke_id_valid = 0;
	}
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
	if (p->call_level < SIG_PRI_CALL_LEVEL_CONNECT) {
		p->call_level = SIG_PRI_CALL_LEVEL_CONNECT;
	}
	sig_pri_set_dialing(p, 0);
	sig_pri_open_media(p);
	res = pri_answer(p->pri->pri, p->call, 0, !p->digital);
	pri_rel(p->pri);
	ast_setstate(ast, AST_STATE_UP);
	return res;
}

/*!
 * \internal
 * \brief Simple check if the channel is available to use.
 * \since 1.8
 *
 * \param pvt Private channel control structure.
 *
 * \retval 0 Interface not available.
 * \retval 1 Interface is available.
 */
static int sig_pri_available_check(struct sig_pri_chan *pvt)
{
	/*
	 * If interface has a B channel and is available for use
	 * then the channel is available.
	 */
	if (!pvt->no_b_channel && sig_pri_is_chan_available(pvt)) {
		return 1;
	}
	return 0;
}

#if defined(HAVE_PRI_CALL_WAITING)
/*!
 * \internal
 * \brief Get an available call waiting interface.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval cw Call waiting interface to use.
 * \retval NULL if no call waiting interface available.
 */
static struct sig_pri_chan *sig_pri_cw_available(struct sig_pri_span *pri)
{
	struct sig_pri_chan *cw;
	int idx;

	cw = NULL;
	if (pri->num_call_waiting_calls < pri->max_call_waiting_calls) {
		if (!pri->num_call_waiting_calls) {
			/*
			 * There are no outstanding call waiting calls.  Check to see
			 * if the span is in a congested state for the first call
			 * waiting call.
			 */
			for (idx = 0; idx < pri->numchans; ++idx) {
				if (pri->pvts[idx] && sig_pri_available_check(pri->pvts[idx])) {
					/* There is another channel that is available on this span. */
					return cw;
				}
			}
		}
		idx = pri_find_empty_nobch(pri);
		if (0 <= idx) {
			/* Setup the call waiting interface to use. */
			cw = pri->pvts[idx];
			cw->is_call_waiting = 1;
			sig_pri_init_config(cw, pri);
			ast_atomic_fetchadd_int(&pri->num_call_waiting_calls, 1);
		}
	}
	return cw;
}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */

int sig_pri_available(struct sig_pri_chan **pvt, int is_specific_channel)
{
	struct sig_pri_chan *p = *pvt;
	struct sig_pri_span *pri;

	if (!p->pri) {
		/* Something is wrong here.  A PRI channel without the pri pointer? */
		return 0;
	}
	pri = p->pri;

	ast_mutex_lock(&pri->lock);
	if (
#if defined(HAVE_PRI_CALL_WAITING)
		/*
		 * Only do call waiting calls if we have any
		 * call waiting call outstanding.  We do not
		 * want new calls to steal a B channel
		 * freed for an earlier call waiting call.
		 */
		!pri->num_call_waiting_calls &&
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
		sig_pri_available_check(p)) {
		p->allocated = 1;
		ast_mutex_unlock(&pri->lock);
		return 1;
	}

#if defined(HAVE_PRI_CALL_WAITING)
	if (!is_specific_channel) {
		struct sig_pri_chan *cw;

		cw = sig_pri_cw_available(pri);
		if (cw) {
			/* We have a call waiting interface to use instead. */
			cw->allocated = 1;
			*pvt = cw;
			ast_mutex_unlock(&pri->lock);
			return 1;
		}
	}
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
	ast_mutex_unlock(&pri->lock);
	return 0;
}

/* If return 0, it means this function was able to handle it (pre setup digits).  If non zero, the user of this
 * functions should handle it normally (generate inband DTMF) */
int sig_pri_digit_begin(struct sig_pri_chan *pvt, struct ast_channel *ast, char digit)
{
	if (ast_channel_state(ast) == AST_STATE_DIALING) {
		if (pvt->call_level < SIG_PRI_CALL_LEVEL_OVERLAP) {
			unsigned int len;

			len = strlen(pvt->dialdest);
			if (len < sizeof(pvt->dialdest) - 1) {
				ast_debug(1, "Queueing digit '%c' since setup_ack not yet received\n",
					digit);
				pvt->dialdest[len++] = digit;
				pvt->dialdest[len] = '\0';
			} else {
				ast_log(LOG_WARNING,
					"Span %d: Deferred digit buffer overflow for digit '%c'.\n",
					pvt->pri->span, digit);
			}
			return 0;
		}
		if (pvt->call_level < SIG_PRI_CALL_LEVEL_PROCEEDING) {
			pri_grab(pvt, pvt->pri);
			pri_information(pvt->pri->pri, pvt->call, digit);
			pri_rel(pvt->pri);
			return 0;
		}
		if (pvt->call_level < SIG_PRI_CALL_LEVEL_CONNECT) {
			ast_log(LOG_WARNING,
				"Span %d: Digit '%c' may be ignored by peer. (Call level:%u(%s))\n",
				pvt->pri->span, digit, pvt->call_level,
				sig_pri_call_level2str(pvt->call_level));
		}
	}
	return 1;
}

/*!
 * \brief DTMF dial string complete.
 * \since 1.8.11
 *
 * \param pvt sig_pri private channel structure.
 * \param ast Asterisk channel
 *
 * \note Channel and private lock are already held.
 *
 * \return Nothing
 */
void sig_pri_dial_complete(struct sig_pri_chan *pvt, struct ast_channel *ast)
{
	/* If we just completed 'w' deferred dialing digits, we need to answer now. */
	if (pvt->call_level == SIG_PRI_CALL_LEVEL_DEFER_DIAL) {
		pvt->call_level = SIG_PRI_CALL_LEVEL_CONNECT;

		sig_pri_open_media(pvt);
		{
			struct ast_frame f = {AST_FRAME_CONTROL, };

			if (sig_pri_callbacks.queue_control) {
				sig_pri_callbacks.queue_control(pvt->chan_pvt, AST_CONTROL_ANSWER);
			}

			f.subclass.integer = AST_CONTROL_ANSWER;
			ast_queue_frame(ast, &f);
		}
		sig_pri_set_dialing(pvt, 0);
		/* Enable echo cancellation if it's not on already */
		sig_pri_set_echocanceller(pvt, 1);
	}
}

#if defined(HAVE_PRI_MWI)
/*!
 * \internal
 * \brief Send a MWI indication to the given span.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 * \param vm_number Voicemail controlling number (NULL if not present).
 * \param vm_box Voicemail mailbox number
 * \param mbox_id Mailbox id
 * \param num_messages Number of messages waiting.
 *
 * \return Nothing
 */
static void sig_pri_send_mwi_indication(struct sig_pri_span *pri, const char *vm_number, const char *vm_box, const char *mbox_id, int num_messages)
{
	struct pri_party_id voicemail;
	struct pri_party_id mailbox;

	ast_debug(1, "Send MWI indication for %s(%s) vm_number:%s num_messages:%d\n",
		vm_box, mbox_id, S_OR(vm_number, "<not-present>"), num_messages);

	memset(&mailbox, 0, sizeof(mailbox));
	mailbox.number.valid = 1;
	mailbox.number.presentation = PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
	mailbox.number.plan = (PRI_TON_UNKNOWN << 4) | PRI_NPI_UNKNOWN;
	ast_copy_string(mailbox.number.str, vm_box, sizeof(mailbox.number.str));

	memset(&voicemail, 0, sizeof(voicemail));
	voicemail.number.valid = 1;
	voicemail.number.presentation = PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
	voicemail.number.plan = (PRI_TON_UNKNOWN << 4) | PRI_NPI_UNKNOWN;
	if (vm_number) {
		ast_copy_string(voicemail.number.str, vm_number, sizeof(voicemail.number.str));
	}

	ast_mutex_lock(&pri->lock);
#if defined(HAVE_PRI_MWI_V2)
	pri_mwi_indicate_v2(pri->pri, &mailbox, &voicemail, 1 /* speech */, num_messages,
		NULL, NULL, -1, 0);
#else	/* !defined(HAVE_PRI_MWI_V2) */
	pri_mwi_indicate(pri->pri, &mailbox, 1 /* speech */, num_messages, NULL, NULL, -1, 0);
#endif	/* !defined(HAVE_PRI_MWI_V2) */
	ast_mutex_unlock(&pri->lock);
}
#endif	/* defined(HAVE_PRI_MWI) */

#if defined(HAVE_PRI_MWI)
/*!
 * \internal
 * \brief MWI subscription event callback.
 * \since 1.8
 *
 * \param userdata the data provider in the call to stasis_subscribe()
 * \param sub the subscription to which the message was delivered for this callback
 * \param topic the topic on which the message was published
 * \param msg the message being passed to the subscriber
 *
 * \return Nothing
 */
static void sig_pri_mwi_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct sig_pri_span *pri = userdata;
	int idx;
	struct ast_mwi_state *mwi_state;

	if (ast_mwi_state_type() != stasis_message_type(msg)) {
		return;
	}

	mwi_state = stasis_message_data(msg);

	for (idx = 0; idx < ARRAY_LEN(pri->mbox); ++idx) {
		if (!pri->mbox[idx].sub) {
			/* Mailbox slot is empty */
			continue;
		}

		if (!strcmp(pri->mbox[idx].uniqueid, mwi_state->uniqueid)) {
			/* Found the mailbox. */
			sig_pri_send_mwi_indication(pri, pri->mbox[idx].vm_number,
				pri->mbox[idx].vm_box, pri->mbox[idx].uniqueid, mwi_state->new_msgs);
			break;
		}
	}
}
#endif	/* defined(HAVE_PRI_MWI) */

#if defined(HAVE_PRI_MWI)
/*!
 * \internal
 * \brief Send update MWI indications from the event cache.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 *
 * \return Nothing
 */
static void sig_pri_mwi_cache_update(struct sig_pri_span *pri)
{
	int idx;
	struct ast_mwi_state *mwi_state;

	for (idx = 0; idx < ARRAY_LEN(pri->mbox); ++idx) {
		RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
		if (!pri->mbox[idx].sub) {
			/* Mailbox slot is empty */
			continue;
		}

		msg = stasis_cache_get(ast_mwi_state_cache(), ast_mwi_state_type(),
			pri->mbox[idx].uniqueid);
		if (!msg) {
			/* No cached event for this mailbox. */
			continue;
		}

		mwi_state = stasis_message_data(msg);
		sig_pri_send_mwi_indication(pri, pri->mbox[idx].vm_number, pri->mbox[idx].vm_box,
			pri->mbox[idx].uniqueid, mwi_state->new_msgs);
	}
}
#endif	/* defined(HAVE_PRI_MWI) */

/*!
 * \brief Stop PRI span.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 *
 * \return Nothing
 */
void sig_pri_stop_pri(struct sig_pri_span *pri)
{
#if defined(HAVE_PRI_MWI)
	int idx;
#endif	/* defined(HAVE_PRI_MWI) */

#if defined(HAVE_PRI_MWI)
	for (idx = 0; idx < ARRAY_LEN(pri->mbox); ++idx) {
		if (pri->mbox[idx].sub) {
			pri->mbox[idx].sub = stasis_unsubscribe(pri->mbox[idx].sub);
		}
	}
#endif	/* defined(HAVE_PRI_MWI) */
}

/*!
 * \internal
 * \brief qsort comparison function.
 * \since 1.8
 *
 * \param left Ptr to sig_pri_chan ptr to compare.
 * \param right Ptr to sig_pri_chan ptr to compare.
 *
 * \retval <0 if left < right.
 * \retval =0 if left == right.
 * \retval >0 if left > right.
 */
static int sig_pri_cmp_pri_chans(const void *left, const void *right)
{
	const struct sig_pri_chan *pvt_left;
	const struct sig_pri_chan *pvt_right;

	pvt_left = *(struct sig_pri_chan **) left;
	pvt_right = *(struct sig_pri_chan **) right;
	if (!pvt_left) {
		if (!pvt_right) {
			return 0;
		}
		return 1;
	}
	if (!pvt_right) {
		return -1;
	}

	return pvt_left->channel - pvt_right->channel;
}

/*!
 * \internal
 * \brief Sort the PRI B channel private pointer array.
 * \since 1.8
 *
 * \param pri PRI span control structure.
 *
 * \details
 * Since the chan_dahdi.conf file can declare channels in any order, we need to sort
 * the private channel pointer array.
 *
 * \return Nothing
 */
static void sig_pri_sort_pri_chans(struct sig_pri_span *pri)
{
	qsort(&pri->pvts, pri->numchans, sizeof(pri->pvts[0]), sig_pri_cmp_pri_chans);
}

int sig_pri_start_pri(struct sig_pri_span *pri)
{
	int x;
	int i;
#if defined(HAVE_PRI_MWI)
	char *saveptr;
	char *prev_vm_number;
#endif	/* defined(HAVE_PRI_MWI) */

#if defined(HAVE_PRI_MWI)
	/* Prepare the mbox[] for use. */
	for (i = 0; i < ARRAY_LEN(pri->mbox); ++i) {
		if (pri->mbox[i].sub) {
			pri->mbox[i].sub = stasis_unsubscribe(pri->mbox[i].sub);
		}
	}
#endif	/* defined(HAVE_PRI_MWI) */

	ast_mutex_init(&pri->lock);
	sig_pri_sort_pri_chans(pri);

#if defined(HAVE_PRI_MWI)
	/*
	 * Split the mwi_vm_numbers configuration string into the mbox[].vm_number:
	 * vm_number{,vm_number}
	 */
	prev_vm_number = NULL;
	saveptr = pri->mwi_vm_numbers;
	for (i = 0; i < ARRAY_LEN(pri->mbox); ++i) {
		char *vm_number;

		vm_number = strsep(&saveptr, ",");
		if (vm_number) {
			vm_number = ast_strip(vm_number);
		}
		if (ast_strlen_zero(vm_number)) {
			/* There was no number so reuse the previous number. */
			vm_number = prev_vm_number;
		} else {
			/* We have a new number. */
			prev_vm_number = vm_number;
		}
		pri->mbox[i].vm_number = vm_number;
	}

	/*
	 * Split the mwi_vm_boxes configuration string into the mbox[].vm_box:
	 * vm_box{,vm_box}
	 */
	saveptr = pri->mwi_vm_boxes;
	for (i = 0; i < ARRAY_LEN(pri->mbox); ++i) {
		char *vm_box;

		vm_box = strsep(&saveptr, ",");
		if (vm_box) {
			vm_box = ast_strip(vm_box);
			if (ast_strlen_zero(vm_box)) {
				vm_box = NULL;
			}
		}
		pri->mbox[i].vm_box = vm_box;
	}

	/*
	 * Split the mwi_mailboxes configuration string into the mbox[]:
	 * vm_mailbox{,vm_mailbox}
	 */
	saveptr = pri->mwi_mailboxes;
	for (i = 0; i < ARRAY_LEN(pri->mbox); ++i) {
		char *mbox_id;
		struct stasis_topic *mailbox_specific_topic;

		mbox_id = strsep(&saveptr, ",");
		if (mbox_id) {
			mbox_id = ast_strip(mbox_id);
			if (ast_strlen_zero(mbox_id)) {
				mbox_id = NULL;
			}
		}
		pri->mbox[i].uniqueid = mbox_id;
		if (!pri->mbox[i].vm_box || !mbox_id) {
			/* The mailbox position is disabled. */
			ast_debug(1, "%s span %d MWI position %d disabled.  vm_box:%s mbox_id:%s.\n",
				sig_pri_cc_type_name, pri->span, i,
				pri->mbox[i].vm_box ?: "<missing>",
				mbox_id ?: "<missing>");
			continue;
		}

		mailbox_specific_topic = ast_mwi_topic(mbox_id);
		if (mailbox_specific_topic) {
			pri->mbox[i].sub = stasis_subscribe_pool(mailbox_specific_topic, sig_pri_mwi_event_cb, pri);
		}
		if (!pri->mbox[i].sub) {
			ast_log(LOG_ERROR, "%s span %d could not subscribe to MWI events for %s(%s).\n",
				sig_pri_cc_type_name, pri->span, pri->mbox[i].vm_box, mbox_id);
		}
#if defined(HAVE_PRI_MWI_V2)
		if (ast_strlen_zero(pri->mbox[i].vm_number)) {
			ast_log(LOG_WARNING, "%s span %d MWI voicemail number for %s(%s) is empty.\n",
				sig_pri_cc_type_name, pri->span, pri->mbox[i].vm_box, mbox_id);
		}
#endif	/* defined(HAVE_PRI_MWI_V2) */
	}
#endif	/* defined(HAVE_PRI_MWI) */

	for (i = 0; i < SIG_PRI_NUM_DCHANS; i++) {
		if (pri->fds[i] == -1) {
			break;
		}

		switch (pri->sig) {
		case SIG_BRI:
			pri->dchans[i] = pri_new_bri(pri->fds[i], 1, pri->nodetype, pri->switchtype);
			break;
		case SIG_BRI_PTMP:
			pri->dchans[i] = pri_new_bri(pri->fds[i], 0, pri->nodetype, pri->switchtype);
			break;
		default:
			pri->dchans[i] = pri_new(pri->fds[i], pri->nodetype, pri->switchtype);
#if defined(HAVE_PRI_SERVICE_MESSAGES)
			if (pri->enable_service_message_support) {
				pri_set_service_message_support(pri->dchans[i], 1);
			}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
			break;
		}

		pri_set_overlapdial(pri->dchans[i], (pri->overlapdial & DAHDI_OVERLAPDIAL_OUTGOING) ? 1 : 0);
#ifdef HAVE_PRI_PROG_W_CAUSE
		pri_set_chan_mapping_logical(pri->dchans[i], pri->qsigchannelmapping == DAHDI_CHAN_MAPPING_LOGICAL);
#endif
#ifdef HAVE_PRI_INBANDDISCONNECT
		pri_set_inbanddisconnect(pri->dchans[i], pri->inbanddisconnect);
#endif
		/* Enslave to master if appropriate */
		if (i)
			pri_enslave(pri->dchans[0], pri->dchans[i]);
		if (!pri->dchans[i]) {
			if (pri->fds[i] > 0)
				close(pri->fds[i]);
			pri->fds[i] = -1;
			ast_log(LOG_ERROR, "Unable to create PRI structure\n");
			return -1;
		}
		pri_set_debug(pri->dchans[i], SIG_PRI_DEBUG_DEFAULT);
		pri_set_nsf(pri->dchans[i], pri->nsf);
#ifdef PRI_GETSET_TIMERS
		for (x = 0; x < PRI_MAX_TIMERS; x++) {
			if (pri->pritimers[x] != 0)
				pri_set_timer(pri->dchans[i], x, pri->pritimers[x]);
		}
#endif
	}

	/* Assume primary is the one we use */
	pri->pri = pri->dchans[0];

#if defined(HAVE_PRI_CALL_HOLD)
	pri_hold_enable(pri->pri, 1);
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_REROUTING)
	pri_reroute_enable(pri->pri, 1);
#endif	/* defined(HAVE_PRI_CALL_REROUTING) */
#if defined(HAVE_PRI_HANGUP_FIX)
	pri_hangup_fix_enable(pri->pri, 1);
#endif	/* defined(HAVE_PRI_HANGUP_FIX) */
#if defined(HAVE_PRI_CCSS)
	pri_cc_enable(pri->pri, 1);
	pri_cc_recall_mode(pri->pri, pri->cc_ptmp_recall_mode);
	pri_cc_retain_signaling_req(pri->pri, pri->cc_qsig_signaling_link_req);
	pri_cc_retain_signaling_rsp(pri->pri, pri->cc_qsig_signaling_link_rsp);
#endif	/* defined(HAVE_PRI_CCSS) */
#if defined(HAVE_PRI_TRANSFER)
	pri_transfer_enable(pri->pri, 1);
#endif	/* defined(HAVE_PRI_TRANSFER) */
#if defined(HAVE_PRI_AOC_EVENTS)
	pri_aoc_events_enable(pri->pri, 1);
#endif	/* defined(HAVE_PRI_AOC_EVENTS) */
#if defined(HAVE_PRI_CALL_WAITING)
	pri_connect_ack_enable(pri->pri, 1);
#endif	/* defined(HAVE_PRI_CALL_WAITING) */
#if defined(HAVE_PRI_MCID)
	pri_mcid_enable(pri->pri, 1);
#endif	/* defined(HAVE_PRI_MCID) */
#if defined(HAVE_PRI_DISPLAY_TEXT)
	pri_display_options_send(pri->pri, pri->display_flags_send);
	pri_display_options_receive(pri->pri, pri->display_flags_receive);
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */
#if defined(HAVE_PRI_DATETIME_SEND)
	pri_date_time_send_option(pri->pri, pri->datetime_send);
#endif	/* defined(HAVE_PRI_DATETIME_SEND) */
#if defined(HAVE_PRI_L2_PERSISTENCE)
	pri_persistent_layer2_option(pri->pri, pri->l2_persistence);
#endif	/* defined(HAVE_PRI_L2_PERSISTENCE) */

	pri->resetpos = -1;
	if (ast_pthread_create_background(&pri->master, NULL, pri_dchannel, pri)) {
		for (i = 0; i < SIG_PRI_NUM_DCHANS; i++) {
			if (!pri->dchans[i])
				break;
			if (pri->fds[i] > 0)
				close(pri->fds[i]);
			pri->fds[i] = -1;
		}
		ast_log(LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
	}

#if defined(HAVE_PRI_MWI)
	/*
	 * Send the initial MWI indications from the event cache for this span.
	 *
	 * If we were loaded after app_voicemail the event would already be in
	 * the cache.  If we were loaded before app_voicemail the event would not
	 * be in the cache yet and app_voicemail will send the event when it
	 * gets loaded.
	 */
	sig_pri_mwi_cache_update(pri);
#endif	/* defined(HAVE_PRI_MWI) */

	return 0;
}

/*!
 * \brief Notify new alarm status.
 *
 * \param p Channel private pointer.
 * \param noalarm Non-zero if not in alarm mode.
 *
 * \note Assumes the sig_pri_lock_private(p) is already obtained.
 *
 * \return Nothing
 */
void sig_pri_chan_alarm_notify(struct sig_pri_chan *p, int noalarm)
{
	pri_grab(p, p->pri);
	sig_pri_set_alarm(p, !noalarm);
	if (!noalarm) {
		if (pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0) {
			/* T309 is not enabled : destroy calls when alarm occurs */
			if (p->call) {
				pri_destroycall(p->pri->pri, p->call);
				p->call = NULL;
			}
			if (p->owner)
				ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
		}
	}
	sig_pri_span_devstate_changed(p->pri);
	pri_rel(p->pri);
}

/*!
 * \brief Determine if layer 1 alarms are ignored.
 *
 * \param p Channel private pointer.
 *
 * \return TRUE if the alarm is ignored.
 */
int sig_pri_is_alarm_ignored(struct sig_pri_span *pri)
{
	return pri->layer1_ignored;
}

struct sig_pri_chan *sig_pri_chan_new(void *pvt_data, struct sig_pri_span *pri, int logicalspan, int channo, int trunkgroup)
{
	struct sig_pri_chan *p;

	p = ast_calloc(1, sizeof(*p));
	if (!p)
		return p;

	p->logicalspan = logicalspan;
	p->prioffset = channo;
	p->mastertrunkgroup = trunkgroup;

	p->chan_pvt = pvt_data;

	p->pri = pri;

	return p;
}

/*!
 * \brief Delete the sig_pri private channel structure.
 * \since 1.8
 *
 * \param doomed sig_pri private channel structure to delete.
 *
 * \return Nothing
 */
void sig_pri_chan_delete(struct sig_pri_chan *doomed)
{
	ast_free(doomed);
}

#define SIG_PRI_SC_HEADER	"%-4s %4s %-4s %-4s %-10s %-4s %s\n"
#define SIG_PRI_SC_LINE		 "%4d %4d %-4s %-4s %-10s %-4s %s"
void sig_pri_cli_show_channels_header(int fd)
{
	ast_cli(fd, SIG_PRI_SC_HEADER, "PRI",  "",     "B",    "Chan", "Call",  "PRI",  "Channel");
	ast_cli(fd, SIG_PRI_SC_HEADER, "Span", "Chan", "Chan", "Idle", "Level", "Call", "Name");
}

void sig_pri_cli_show_channels(int fd, struct sig_pri_span *pri)
{
	char line[256];
	int idx;
	struct sig_pri_chan *pvt;

	ast_mutex_lock(&pri->lock);
	for (idx = 0; idx < pri->numchans; ++idx) {
		if (!pri->pvts[idx]) {
			continue;
		}
		pvt = pri->pvts[idx];
		sig_pri_lock_private(pvt);
		sig_pri_lock_owner(pri, idx);
		if (pvt->no_b_channel && sig_pri_is_chan_available(pvt)) {
			/* Don't show held/call-waiting channels if they are not in use. */
			sig_pri_unlock_private(pvt);
			continue;
		}

		snprintf(line, sizeof(line), SIG_PRI_SC_LINE,
			pri->span,
			pvt->channel,
			pvt->no_b_channel ? "No" : "Yes",/* Has media */
			sig_pri_is_chan_available(pvt) ? "Yes" : "No",
			sig_pri_call_level2str(pvt->call_level),
			pvt->call ? "Yes" : "No",
			pvt->owner ? ast_channel_name(pvt->owner) : "");

		if (pvt->owner) {
			ast_channel_unlock(pvt->owner);
		}
		sig_pri_unlock_private(pvt);

		ast_mutex_unlock(&pri->lock);
		ast_cli(fd, "%s\n", line);
		ast_mutex_lock(&pri->lock);
	}
	ast_mutex_unlock(&pri->lock);
}

static void build_status(char *s, size_t len, int status, int active)
{
	if (!s || len < 1) {
		return;
	}
	snprintf(s, len, "%s%s, %s",
		(status & DCHAN_NOTINALARM) ? "" : "In Alarm, ",
		(status & DCHAN_UP) ? "Up" : "Down",
		(active) ? "Active" : "Standby");
}

void sig_pri_cli_show_spans(int fd, int span, struct sig_pri_span *pri)
{
	char status[256];
	int x;
	for (x = 0; x < SIG_PRI_NUM_DCHANS; x++) {
		if (pri->dchans[x]) {
			build_status(status, sizeof(status), pri->dchanavail[x], pri->dchans[x] == pri->pri);
			ast_cli(fd, "PRI span %d/%d: %s\n", span, x, status);
		}
	}
}

void sig_pri_cli_show_span(int fd, int *dchannels, struct sig_pri_span *pri)
{
	int x;
	char status[256];

	for (x = 0; x < SIG_PRI_NUM_DCHANS; x++) {
		if (pri->dchans[x]) {
#ifdef PRI_DUMP_INFO_STR
			char *info_str = NULL;
#endif
			ast_cli(fd, "%s D-channel: %d\n", pri_order(x), dchannels[x]);
			build_status(status, sizeof(status), pri->dchanavail[x], pri->dchans[x] == pri->pri);
			ast_cli(fd, "Status: %s\n", status);
			ast_mutex_lock(&pri->lock);
#ifdef PRI_DUMP_INFO_STR
			info_str = pri_dump_info_str(pri->pri);
			if (info_str) {
				ast_cli(fd, "%s", info_str);
				ast_std_free(info_str);
			}
#else
			pri_dump_info(pri->pri);
#endif
			ast_mutex_unlock(&pri->lock);
			ast_cli(fd, "Overlap Recv: %s\n\n", (pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)?"Yes":"No");
			ast_cli(fd, "\n");
		}
	}
}

int pri_send_keypad_facility_exec(struct sig_pri_chan *p, const char *digits)
{
	sig_pri_lock_private(p);

	if (!p->pri || !p->call) {
		ast_debug(1, "Unable to find pri or call on channel!\n");
		sig_pri_unlock_private(p);
		return -1;
	}

	pri_grab(p, p->pri);
	pri_keypad_facility(p->pri->pri, p->call, digits);
	pri_rel(p->pri);

	sig_pri_unlock_private(p);

	return 0;
}

int pri_send_callrerouting_facility_exec(struct sig_pri_chan *p, enum ast_channel_state chanstate, const char *destination, const char *original, const char *reason)
{
	int res;

	sig_pri_lock_private(p);

	if (!p->pri || !p->call) {
		ast_debug(1, "Unable to find pri or call on channel!\n");
		sig_pri_unlock_private(p);
		return -1;
	}

	pri_grab(p, p->pri);
	res = pri_callrerouting_facility(p->pri->pri, p->call, destination, original, reason);
	pri_rel(p->pri);

	sig_pri_unlock_private(p);

	return res;
}

#if defined(HAVE_PRI_SERVICE_MESSAGES)
int pri_maintenance_bservice(struct pri *pri, struct sig_pri_chan *p, int changestatus)
{
	int channel = PVT_TO_CHANNEL(p);
	int span = PRI_SPAN(channel);

	return pri_maintenance_service(pri, span, channel, changestatus);
}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */

void sig_pri_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, struct sig_pri_chan *pchan)
{
	if (pchan->owner == oldchan) {
		pchan->owner = newchan;
	}
}

#if defined(HAVE_PRI_DISPLAY_TEXT)
/*!
 * \brief Send display text.
 * \since 10.0
 *
 * \param p Channel to send text over
 * \param text Text to send.
 *
 * \return Nothing
 */
void sig_pri_sendtext(struct sig_pri_chan *p, const char *text)
{
	struct pri_subcmd_display_txt display;

	if (p->pri && p->pri->pri) {
		ast_copy_string(display.text, text, sizeof(display.text));
		display.length = strlen(display.text);
		display.char_set = 0;/* unknown(0) */
		pri_grab(p, p->pri);
		pri_display_text(p->pri->pri, p->call, &display);
		pri_rel(p->pri);
	}
}
#endif	/* defined(HAVE_PRI_DISPLAY_TEXT) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief PRI CC agent initialization.
 * \since 1.8
 *
 * \param agent CC core agent control.
 * \param pvt_chan Original channel the agent will attempt to recall.
 *
 * \details
 * This callback is called when the CC core is initialized.  Agents should allocate
 * any private data necessary for the call and assign it to the private_data
 * on the agent.  Additionally, if any ast_cc_agent_flags are pertinent to the
 * specific agent type, they should be set in this function as well.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_init(struct ast_cc_agent *agent, struct sig_pri_chan *pvt_chan)
{
	struct sig_pri_cc_agent_prv *cc_pvt;

	cc_pvt = ast_calloc(1, sizeof(*cc_pvt));
	if (!cc_pvt) {
		return -1;
	}

	ast_mutex_lock(&pvt_chan->pri->lock);
	cc_pvt->pri = pvt_chan->pri;
	cc_pvt->cc_id = pri_cc_available(pvt_chan->pri->pri, pvt_chan->call);
	ast_mutex_unlock(&pvt_chan->pri->lock);
	if (cc_pvt->cc_id == -1) {
		ast_free(cc_pvt);
		return -1;
	}
	agent->private_data = cc_pvt;
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Start the offer timer.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * This is called by the core when the caller hangs up after
 * a call for which CC may be requested. The agent should
 * begin the timer as configured.
 *
 * The primary reason why this functionality is left to
 * the specific agent implementations is due to the differing
 * use of schedulers throughout the code. Some channel drivers
 * may already have a scheduler context they wish to use, and
 * amongst those, some may use the ast_sched API while others
 * may use the ast_sched_thread API, which are incompatible.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_start_offer_timer(struct ast_cc_agent *agent)
{
	/* libpri maintains it's own offer timer in the form of T_RETENTION. */
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Stop the offer timer.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * This callback is called by the CC core when the caller
 * has requested CC.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_stop_offer_timer(struct ast_cc_agent *agent)
{
	/* libpri maintains it's own offer timer in the form of T_RETENTION. */
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Response to a CC request.
 * \since 1.8
 *
 * \param agent CC core agent control.
 * \param reason CC request response status.
 *
 * \details
 * When the core receives knowledge that a called
 * party has accepted a CC request, it will call
 * this callback.  The core may also call this
 * if there is some error when attempting to process
 * the incoming CC request.
 *
 * The duty of this is to issue a propper response to a
 * CC request from the caller by acknowledging receipt
 * of that request or rejecting it.
 *
 * \return Nothing
 */
void sig_pri_cc_agent_req_rsp(struct ast_cc_agent *agent, enum ast_cc_agent_response_reason reason)
{
	struct sig_pri_cc_agent_prv *cc_pvt;
	int res;
	int status;
	const char *failed_msg;
	static const char *failed_to_send = "Failed to send the CC request response.";
	static const char *not_accepted = "The core declined the CC request.";

	cc_pvt = agent->private_data;
	ast_mutex_lock(&cc_pvt->pri->lock);
	if (cc_pvt->cc_request_response_pending) {
		cc_pvt->cc_request_response_pending = 0;

		/* Convert core response reason to ISDN response status. */
		status = 2;/* short_term_denial */
		switch (reason) {
		case AST_CC_AGENT_RESPONSE_SUCCESS:
			status = 0;/* success */
			break;
		case AST_CC_AGENT_RESPONSE_FAILURE_INVALID:
			status = 2;/* short_term_denial */
			break;
		case AST_CC_AGENT_RESPONSE_FAILURE_TOO_MANY:
			status = 5;/* queue_full */
			break;
		}

		res = pri_cc_req_rsp(cc_pvt->pri->pri, cc_pvt->cc_id, status);
		if (!status) {
			/* CC core request was accepted. */
			if (res) {
				failed_msg = failed_to_send;
			} else {
				failed_msg = NULL;
			}
		} else {
			/* CC core request was declined. */
			if (res) {
				failed_msg = failed_to_send;
			} else {
				failed_msg = not_accepted;
			}
		}
	} else {
		failed_msg = NULL;
	}
	ast_mutex_unlock(&cc_pvt->pri->lock);
	if (failed_msg) {
		ast_cc_failed(agent->core_id, "%s agent: %s", sig_pri_cc_type_name, failed_msg);
	}
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Request the status of the agent's device.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * Asynchronous request for the status of any caller
 * which may be a valid caller for the CC transaction.
 * Status responses should be made using the
 * ast_cc_status_response function.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_status_req(struct ast_cc_agent *agent)
{
	struct sig_pri_cc_agent_prv *cc_pvt;

	cc_pvt = agent->private_data;
	ast_mutex_lock(&cc_pvt->pri->lock);
	pri_cc_status_req(cc_pvt->pri->pri, cc_pvt->cc_id);
	ast_mutex_unlock(&cc_pvt->pri->lock);
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Request for an agent's phone to stop ringing.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * The usefulness of this is quite limited. The only specific
 * known case for this is if Asterisk requests CC over an ISDN
 * PTMP link as the TE side. If other phones are in the same
 * recall group as the Asterisk server, and one of those phones
 * picks up the recall notice, then Asterisk will receive a
 * "stop ringing" notification from the NT side of the PTMP
 * link. This indication needs to be passed to the phone
 * on the other side of the Asterisk server which originally
 * placed the call so that it will stop ringing. Since the
 * phone may be of any type, it is necessary to have a callback
 * that the core can know about.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_stop_ringing(struct ast_cc_agent *agent)
{
	struct sig_pri_cc_agent_prv *cc_pvt;

	cc_pvt = agent->private_data;
	ast_mutex_lock(&cc_pvt->pri->lock);
	pri_cc_stop_alerting(cc_pvt->pri->pri, cc_pvt->cc_id);
	ast_mutex_unlock(&cc_pvt->pri->lock);
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Let the caller know that the callee has become free
 * but that the caller cannot attempt to call back because
 * he is either busy or there is congestion on his line.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * This is something that really only affects a scenario where
 * a phone places a call over ISDN PTMP to Asterisk, who then
 * connects over PTMP again to the ISDN network. For most agent
 * types, there is no need to implement this callback at all
 * because they don't really need to actually do anything in
 * this situation. If you're having trouble understanding what
 * the purpose of this callback is, then you can be safe simply
 * not implementing it.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_party_b_free(struct ast_cc_agent *agent)
{
	struct sig_pri_cc_agent_prv *cc_pvt;

	cc_pvt = agent->private_data;
	ast_mutex_lock(&cc_pvt->pri->lock);
	pri_cc_b_free(cc_pvt->pri->pri, cc_pvt->cc_id);
	ast_mutex_unlock(&cc_pvt->pri->lock);
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Begin monitoring a busy device.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * The core will call this callback if the callee becomes
 * available but the caller has reported that he is busy.
 * The agent should begin monitoring the caller's device.
 * When the caller becomes available again, the agent should
 * call ast_cc_agent_caller_available.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_start_monitoring(struct ast_cc_agent *agent)
{
	/* libpri already knows when and how it needs to monitor Party A. */
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Alert the caller that it is time to try recalling.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * The core will call this function when it receives notice
 * that a monitored party has become available.
 *
 * The agent's job is to send a message to the caller to
 * notify it of such a change. If the agent is able to
 * discern that the caller is currently unavailable, then
 * the agent should react by calling the ast_cc_caller_unavailable
 * function.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_cc_agent_callee_available(struct ast_cc_agent *agent)
{
	struct sig_pri_cc_agent_prv *cc_pvt;

	cc_pvt = agent->private_data;
	ast_mutex_lock(&cc_pvt->pri->lock);
	pri_cc_remote_user_free(cc_pvt->pri->pri, cc_pvt->cc_id);
	ast_mutex_unlock(&cc_pvt->pri->lock);
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Destroy private data on the agent.
 * \since 1.8
 *
 * \param agent CC core agent control.
 *
 * \details
 * The core will call this function upon completion
 * or failure of CC.
 *
 * \note
 * The agent private_data pointer may be NULL if the agent
 * constructor failed.
 *
 * \return Nothing
 */
void sig_pri_cc_agent_destructor(struct ast_cc_agent *agent)
{
	struct sig_pri_cc_agent_prv *cc_pvt;
	int res;

	cc_pvt = agent->private_data;
	if (!cc_pvt) {
		/* The agent constructor probably failed. */
		return;
	}
	ast_mutex_lock(&cc_pvt->pri->lock);
	res = -1;
	if (cc_pvt->cc_request_response_pending) {
		res = pri_cc_req_rsp(cc_pvt->pri->pri, cc_pvt->cc_id, 2/* short_term_denial */);
	}
	if (res) {
		pri_cc_cancel(cc_pvt->pri->pri, cc_pvt->cc_id);
	}
	ast_mutex_unlock(&cc_pvt->pri->lock);
	ast_free(cc_pvt);
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Return the hash value of the given CC monitor instance object.
 * \since 1.8
 *
 * \param obj pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback().  Ignored at the moment.
 *
 * \retval core_id
 */
static int sig_pri_cc_monitor_instance_hash_fn(const void *obj, const int flags)
{
	const struct sig_pri_cc_monitor_instance *monitor_instance = obj;

	return monitor_instance->core_id;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \internal
 * \brief Compere the monitor instance core_id key value.
 * \since 1.8
 *
 * \param obj pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param flags flags from ao2_callback()
 *
 * \return values are a combination of enum _cb_results.
 */
static int sig_pri_cc_monitor_instance_cmp_fn(void *obj, void *arg, int flags)
{
	struct sig_pri_cc_monitor_instance *monitor_1 = obj;
	struct sig_pri_cc_monitor_instance *monitor_2 = arg;

	return monitor_1->core_id == monitor_2->core_id ? CMP_MATCH | CMP_STOP : 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Request CCSS.
 * \since 1.8
 *
 * \param monitor CC core monitor control.
 * \param available_timer_id Where to put the available timer scheduler id.
 * Will never be NULL for a device monitor.
 *
 * \details
 * Perform whatever steps are necessary in order to request CC.
 * In addition, the monitor implementation is responsible for
 * starting the available timer in this callback. The scheduler
 * ID for the callback must be stored in the parent_link's child_avail_id
 * field.
 *
 * \retval 0 on success
 * \retval -1 on failure.
 */
int sig_pri_cc_monitor_req_cc(struct ast_cc_monitor *monitor, int *available_timer_id)
{
	struct sig_pri_cc_monitor_instance *instance;
	int cc_mode;
	int res;

	switch (monitor->service_offered) {
	case AST_CC_CCBS:
		cc_mode = 0;/* CCBS */
		break;
	case AST_CC_CCNR:
		cc_mode = 1;/* CCNR */
		break;
	default:
		/* CC service not supported by ISDN. */
		return -1;
	}

	instance = monitor->private_data;

	/* libpri handles it's own available timer. */
	ast_mutex_lock(&instance->pri->lock);
	res = pri_cc_req(instance->pri->pri, instance->cc_id, cc_mode);
	ast_mutex_unlock(&instance->pri->lock);

	return res;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Suspend monitoring.
 * \since 1.8
 *
 * \param monitor CC core monitor control.
 *
 * \details
 * Implementers must perform the necessary steps to suspend
 * monitoring.
 *
 * \retval 0 on success
 * \retval -1 on failure.
 */
int sig_pri_cc_monitor_suspend(struct ast_cc_monitor *monitor)
{
	struct sig_pri_cc_monitor_instance *instance;

	instance = monitor->private_data;
	ast_mutex_lock(&instance->pri->lock);
	pri_cc_status(instance->pri->pri, instance->cc_id, 1/* busy */);
	ast_mutex_unlock(&instance->pri->lock);

	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Unsuspend monitoring.
 * \since 1.8
 *
 * \param monitor CC core monitor control.
 *
 * \details
 * Perform the necessary steps to unsuspend monitoring.
 *
 * \retval 0 on success
 * \retval -1 on failure.
 */
int sig_pri_cc_monitor_unsuspend(struct ast_cc_monitor *monitor)
{
	struct sig_pri_cc_monitor_instance *instance;

	instance = monitor->private_data;
	ast_mutex_lock(&instance->pri->lock);
	pri_cc_status(instance->pri->pri, instance->cc_id, 0/* free */);
	ast_mutex_unlock(&instance->pri->lock);

	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Status response to an ast_cc_monitor_status_request().
 * \since 1.8
 *
 * \param monitor CC core monitor control.
 * \param devstate Current status of a Party A device.
 *
 * \details
 * Alert a monitor as to the status of the agent for which
 * the monitor had previously requested a status request.
 *
 * \note Zero or more responses may come as a result.
 *
 * \retval 0 on success
 * \retval -1 on failure.
 */
int sig_pri_cc_monitor_status_rsp(struct ast_cc_monitor *monitor, enum ast_device_state devstate)
{
	struct sig_pri_cc_monitor_instance *instance;
	int cc_status;

	switch (devstate) {
	case AST_DEVICE_UNKNOWN:
	case AST_DEVICE_NOT_INUSE:
		cc_status = 0;/* free */
		break;
	case AST_DEVICE_BUSY:
	case AST_DEVICE_INUSE:
		cc_status = 1;/* busy */
		break;
	default:
		/* Don't know how to interpret this device state into free/busy status. */
		return 0;
	}
	instance = monitor->private_data;
	ast_mutex_lock(&instance->pri->lock);
	pri_cc_status_req_rsp(instance->pri->pri, instance->cc_id, cc_status);
	ast_mutex_unlock(&instance->pri->lock);

	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Cancel the running available timer.
 * \since 1.8
 *
 * \param monitor CC core monitor control.
 * \param sched_id Available timer scheduler id to cancel.
 * Will never be NULL for a device monitor.
 *
 * \details
 * In most cases, this function will likely consist of just a
 * call to AST_SCHED_DEL. It might have been possible to do this
 * within the core, but unfortunately the mixture of sched_thread
 * and sched usage in Asterisk prevents such usage.
 *
 * \retval 0 on success
 * \retval -1 on failure.
 */
int sig_pri_cc_monitor_cancel_available_timer(struct ast_cc_monitor *monitor, int *sched_id)
{
	/*
	 * libpri maintains it's own available timer as one of:
	 * T_CCBS2/T_CCBS5/T_CCBS6/QSIG_CCBS_T2
	 * T_CCNR2/T_CCNR5/T_CCNR6/QSIG_CCNR_T2
	 */
	return 0;
}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_CCSS)
/*!
 * \brief Destroy PRI private data on the monitor.
 * \since 1.8
 *
 * \param monitor_pvt CC device monitor private data pointer.
 *
 * \details
 * Implementers of this callback are responsible for destroying
 * all heap-allocated data in the monitor's private_data pointer, including
 * the private_data itself.
 */
void sig_pri_cc_monitor_destructor(void *monitor_pvt)
{
	struct sig_pri_cc_monitor_instance *instance;

	instance = monitor_pvt;
	if (!instance) {
		return;
	}
	ao2_unlink(sig_pri_cc_monitors, instance);
	ao2_ref(instance, -1);
}
#endif	/* defined(HAVE_PRI_CCSS) */

/*!
 * \brief Load the sig_pri submodule.
 * \since 1.8
 *
 * \param cc_type_name CC type name to use when looking up agent/monitor.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_pri_load(const char *cc_type_name)
{
#if defined(HAVE_PRI_MCID)
	if (STASIS_MESSAGE_TYPE_INIT(mcid_type)) {
		return -1;
	}
#endif	/* defined(HAVE_PRI_MCID) */

#if defined(HAVE_PRI_CCSS)
	sig_pri_cc_type_name = cc_type_name;
	sig_pri_cc_monitors = ao2_container_alloc(37, sig_pri_cc_monitor_instance_hash_fn,
		sig_pri_cc_monitor_instance_cmp_fn);
	if (!sig_pri_cc_monitors) {
		return -1;
	}
#endif	/* defined(HAVE_PRI_CCSS) */
	return 0;
}

/*!
 * \brief Unload the sig_pri submodule.
 * \since 1.8
 *
 * \return Nothing
 */
void sig_pri_unload(void)
{
#if defined(HAVE_PRI_CCSS)
	if (sig_pri_cc_monitors) {
		ao2_ref(sig_pri_cc_monitors, -1);
		sig_pri_cc_monitors = NULL;
	}
#endif	/* defined(HAVE_PRI_CCSS) */

#if defined(HAVE_PRI_MCID)
	STASIS_MESSAGE_TYPE_CLEANUP(mcid_type);
#endif	/* defined(HAVE_PRI_MCID) */
}

#endif /* HAVE_PRI */
