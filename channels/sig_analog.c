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
 * \brief Analog signaling module
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <errno.h>
#include <ctype.h>

#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/say.h"
#include "asterisk/manager.h"
#include "asterisk/astdb.h"
#include "asterisk/features.h"
#include "asterisk/cel.h"
#include "asterisk/causes.h"

#include "sig_analog.h"

/*** DOCUMENTATION
 ***/

/*! \note
 * Define if you want to check the hook state for an FXO (FXS signalled) interface
 * before dialing on it.  Certain FXO interfaces always think they're out of
 * service with this method however.
 */
/* #define DAHDI_CHECK_HOOKSTATE */

#define POLARITY_IDLE 0
#define POLARITY_REV    1
#define MIN_MS_SINCE_FLASH			( (2000) )	/*!< 2000 ms */
static int analog_matchdigittimeout = 3000;
static int analog_gendigittimeout = 8000;
static int analog_firstdigittimeout = 16000;
static char analog_defaultcic[64] = "";
static char analog_defaultozz[64] = "";

static const struct {
	enum analog_sigtype sigtype;
	const char const *name;
} sigtypes[] = {
	{ ANALOG_SIG_FXOLS, "fxo_ls" },
	{ ANALOG_SIG_FXOKS, "fxo_ks" },
	{ ANALOG_SIG_FXOGS, "fxo_gs" },
	{ ANALOG_SIG_FXSLS, "fxs_ls" },
	{ ANALOG_SIG_FXSKS, "fxs_ks" },
	{ ANALOG_SIG_FXSGS, "fxs_gs" },
	{ ANALOG_SIG_EMWINK, "em_w" },
	{ ANALOG_SIG_EM, "em" },
	{ ANALOG_SIG_EM_E1, "em_e1" },
	{ ANALOG_SIG_FEATD, "featd" },
	{ ANALOG_SIG_FEATDMF, "featdmf" },
	{ ANALOG_SIG_FEATDMF_TA, "featdmf_ta" },
	{ ANALOG_SIG_FEATB, "featb" },
	{ ANALOG_SIG_FGC_CAMA, "fgccama" },
	{ ANALOG_SIG_FGC_CAMAMF, "fgccamamf" },
	{ ANALOG_SIG_SF, "sf" },
	{ ANALOG_SIG_SFWINK, "sf_w" },
	{ ANALOG_SIG_SF_FEATD, "sf_featd" },
	{ ANALOG_SIG_SF_FEATDMF, "sf_featdmf" },
	{ ANALOG_SIG_SF_FEATB, "sf_featb" },
	{ ANALOG_SIG_E911, "e911" },
};

static const struct {
	unsigned int cid_type;
	const char const *name;
} cidtypes[] = {
	{ CID_SIG_BELL,   "bell" },
	{ CID_SIG_V23,    "v23" },
	{ CID_SIG_V23_JP, "v23_jp" },
	{ CID_SIG_DTMF,   "dtmf" },
	/* "smdi" is intentionally not supported here, as there is a much better
	 * way to do this in the dialplan now. */
};

#define ISTRUNK(p) ((p->sig == ANALOG_SIG_FXSLS) || (p->sig == ANALOG_SIG_FXSKS) || \
					(p->sig == ANALOG_SIG_FXSGS))

enum analog_sigtype analog_str_to_sigtype(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_LEN(sigtypes); i++) {
		if (!strcasecmp(sigtypes[i].name, name)) {
			return sigtypes[i].sigtype;
		}
	}

	return 0;
}

const char *analog_sigtype_to_str(enum analog_sigtype sigtype)
{
	int i;

	for (i = 0; i < ARRAY_LEN(sigtypes); i++) {
		if (sigtype == sigtypes[i].sigtype) {
			return sigtypes[i].name;
		}
	}

	return "Unknown";
}

unsigned int analog_str_to_cidtype(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_LEN(cidtypes); i++) {
		if (!strcasecmp(cidtypes[i].name, name)) {
			return cidtypes[i].cid_type;
		}
	}

	return 0;
}

const char *analog_cidtype_to_str(unsigned int cid_type)
{
	int i;

	for (i = 0; i < ARRAY_LEN(cidtypes); i++) {
		if (cid_type == cidtypes[i].cid_type) {
			return cidtypes[i].name;
		}
	}

	return "Unknown";
}

static int analog_start_cid_detect(struct analog_pvt *p, int cid_signalling)
{
	if (analog_callbacks.start_cid_detect) {
		return analog_callbacks.start_cid_detect(p->chan_pvt, cid_signalling);
	}
	return -1;
}

static int analog_stop_cid_detect(struct analog_pvt *p)
{
	if (analog_callbacks.stop_cid_detect) {
		return analog_callbacks.stop_cid_detect(p->chan_pvt);
	}
	return -1;
}

static int analog_get_callerid(struct analog_pvt *p, char *name, char *number, enum analog_event *ev, size_t timeout)
{
	if (analog_callbacks.get_callerid) {
		return analog_callbacks.get_callerid(p->chan_pvt, name, number, ev, timeout);
	}
	return -1;
}

static const char *analog_get_orig_dialstring(struct analog_pvt *p)
{
	if (analog_callbacks.get_orig_dialstring) {
		return analog_callbacks.get_orig_dialstring(p->chan_pvt);
	}
	return "";
}

static int analog_get_event(struct analog_pvt *p)
{
	if (analog_callbacks.get_event) {
		return analog_callbacks.get_event(p->chan_pvt);
	}
	return -1;
}

static int analog_wait_event(struct analog_pvt *p)
{
	if (analog_callbacks.wait_event) {
		return analog_callbacks.wait_event(p->chan_pvt);
	}
	return -1;
}

static int analog_have_progressdetect(struct analog_pvt *p)
{
	if (analog_callbacks.have_progressdetect) {
		return analog_callbacks.have_progressdetect(p->chan_pvt);
	}
	/* Don't have progress detection. */
	return 0;
}

enum analog_cid_start analog_str_to_cidstart(const char *value)
{
	if (!strcasecmp(value, "ring")) {
		return ANALOG_CID_START_RING;
	} else if (!strcasecmp(value, "polarity")) {
		return ANALOG_CID_START_POLARITY;
	} else if (!strcasecmp(value, "polarity_in")) {
		return ANALOG_CID_START_POLARITY_IN;
	} else if (!strcasecmp(value, "dtmf")) {
		return ANALOG_CID_START_DTMF_NOALERT;
	}

	return 0;
}

const char *analog_cidstart_to_str(enum analog_cid_start cid_start)
{
	switch (cid_start) {
	case ANALOG_CID_START_RING:
		return "Ring";
	case ANALOG_CID_START_POLARITY:
		return "Polarity";
	case ANALOG_CID_START_POLARITY_IN:
		return "Polarity_In";
	case ANALOG_CID_START_DTMF_NOALERT:
		return "DTMF";
	}

	return "Unknown";
}

static char *analog_event2str(enum analog_event event)
{
	char *res;
	switch (event) {
	case ANALOG_EVENT_ONHOOK:
		res = "ANALOG_EVENT_ONHOOK";
		break;
	case ANALOG_EVENT_RINGOFFHOOK:
		res = "ANALOG_EVENT_RINGOFFHOOK";
		break;
	case ANALOG_EVENT_WINKFLASH:
		res = "ANALOG_EVENT_WINKFLASH";
		break;
	case ANALOG_EVENT_ALARM:
		res = "ANALOG_EVENT_ALARM";
		break;
	case ANALOG_EVENT_NOALARM:
		res = "ANALOG_EVENT_NOALARM";
		break;
	case ANALOG_EVENT_DIALCOMPLETE:
		res = "ANALOG_EVENT_DIALCOMPLETE";
		break;
	case ANALOG_EVENT_HOOKCOMPLETE:
		res = "ANALOG_EVENT_HOOKCOMPLETE";
		break;
	case ANALOG_EVENT_PULSE_START:
		res = "ANALOG_EVENT_PULSE_START";
		break;
	case ANALOG_EVENT_POLARITY:
		res = "ANALOG_EVENT_POLARITY";
		break;
	case ANALOG_EVENT_RINGBEGIN:
		res = "ANALOG_EVENT_RINGBEGIN";
		break;
	case ANALOG_EVENT_EC_DISABLED:
		res = "ANALOG_EVENT_EC_DISABLED";
		break;
	case ANALOG_EVENT_RINGERON:
		res = "ANALOG_EVENT_RINGERON";
		break;
	case ANALOG_EVENT_RINGEROFF:
		res = "ANALOG_EVENT_RINGEROFF";
		break;
	case ANALOG_EVENT_REMOVED:
		res = "ANALOG_EVENT_REMOVED";
		break;
	case ANALOG_EVENT_NEONMWI_ACTIVE:
		res = "ANALOG_EVENT_NEONMWI_ACTIVE";
		break;
	case ANALOG_EVENT_NEONMWI_INACTIVE:
		res = "ANALOG_EVENT_NEONMWI_INACTIVE";
		break;
#ifdef HAVE_DAHDI_ECHOCANCEL_FAX_MODE
	case ANALOG_EVENT_TX_CED_DETECTED:
		res = "ANALOG_EVENT_TX_CED_DETECTED";
		break;
	case ANALOG_EVENT_RX_CED_DETECTED:
		res = "ANALOG_EVENT_RX_CED_DETECTED";
		break;
	case ANALOG_EVENT_EC_NLP_DISABLED:
		res = "ANALOG_EVENT_EC_NLP_DISABLED";
		break;
	case ANALOG_EVENT_EC_NLP_ENABLED:
		res = "ANALOG_EVENT_EC_NLP_ENABLED";
		break;
#endif
	case ANALOG_EVENT_PULSEDIGIT:
		res = "ANALOG_EVENT_PULSEDIGIT";
		break;
	case ANALOG_EVENT_DTMFDOWN:
		res = "ANALOG_EVENT_DTMFDOWN";
		break;
	case ANALOG_EVENT_DTMFUP:
		res = "ANALOG_EVENT_DTMFUP";
		break;
	default:
		res = "UNKNOWN/OTHER";
		break;
	}

	return res;
}

static void analog_swap_subs(struct analog_pvt *p, enum analog_sub a, enum analog_sub b)
{
	int tinthreeway;
	struct ast_channel *towner;

	ast_debug(1, "Swapping %u and %u\n", a, b);

	towner = p->subs[a].owner;
	p->subs[a].owner = p->subs[b].owner;
	p->subs[b].owner = towner;

	tinthreeway = p->subs[a].inthreeway;
	p->subs[a].inthreeway = p->subs[b].inthreeway;
	p->subs[b].inthreeway = tinthreeway;

	if (analog_callbacks.swap_subs) {
		analog_callbacks.swap_subs(p->chan_pvt, a, p->subs[a].owner, b, p->subs[b].owner);
	}
}

static int analog_alloc_sub(struct analog_pvt *p, enum analog_sub x)
{
	if (analog_callbacks.allocate_sub) {
		int res;
		res = analog_callbacks.allocate_sub(p->chan_pvt, x);
		if (!res) {
			p->subs[x].allocd = 1;
		}
		return res;
	}
	return 0;
}

static int analog_unalloc_sub(struct analog_pvt *p, enum analog_sub x)
{
	p->subs[x].allocd = 0;
	p->subs[x].owner = NULL;
	if (analog_callbacks.unallocate_sub) {
		return analog_callbacks.unallocate_sub(p->chan_pvt, x);
	}
	return 0;
}

static int analog_send_callerid(struct analog_pvt *p, int cwcid, struct ast_party_caller *caller)
{
	ast_debug(1, "Sending callerid.  CID_NAME: '%s' CID_NUM: '%s'\n",
		caller->id.name.str,
		caller->id.number.str);

	if (cwcid) {
		p->callwaitcas = 0;
	}

	if (analog_callbacks.send_callerid) {
		return analog_callbacks.send_callerid(p->chan_pvt, cwcid, caller);
	}
	return 0;
}

#define analog_get_index(ast, p, nullok)	_analog_get_index(ast, p, nullok, __PRETTY_FUNCTION__, __LINE__)
static int _analog_get_index(struct ast_channel *ast, struct analog_pvt *p, int nullok, const char *fname, unsigned long line)
{
	int res;
	if (p->subs[ANALOG_SUB_REAL].owner == ast) {
		res = ANALOG_SUB_REAL;
	} else if (p->subs[ANALOG_SUB_CALLWAIT].owner == ast) {
		res = ANALOG_SUB_CALLWAIT;
	} else if (p->subs[ANALOG_SUB_THREEWAY].owner == ast) {
		res = ANALOG_SUB_THREEWAY;
	} else {
		res = -1;
		if (!nullok) {
			ast_log(LOG_WARNING,
				"Unable to get index for '%s' on channel %d (%s(), line %lu)\n",
				ast ? ast_channel_name(ast) : "", p->channel, fname, line);
		}
	}
	return res;
}

static int analog_dsp_reset_and_flush_digits(struct analog_pvt *p)
{
	if (analog_callbacks.dsp_reset_and_flush_digits) {
		return analog_callbacks.dsp_reset_and_flush_digits(p->chan_pvt);
	}

	/* Return 0 since I think this is unnecessary to do in most cases it is used.  Mostly only for ast_dsp */
	return 0;
}

static int analog_play_tone(struct analog_pvt *p, enum analog_sub sub, enum analog_tone tone)
{
	if (analog_callbacks.play_tone) {
		return analog_callbacks.play_tone(p->chan_pvt, sub, tone);
	}
	return -1;
}

static void analog_set_new_owner(struct analog_pvt *p, struct ast_channel *new_owner)
{
	p->owner = new_owner;
	if (analog_callbacks.set_new_owner) {
		analog_callbacks.set_new_owner(p->chan_pvt, new_owner);
	}
}

static struct ast_channel * analog_new_ast_channel(struct analog_pvt *p, int state, int startpbx, enum analog_sub sub, const struct ast_channel *requestor)
{
	struct ast_channel *c;

	if (!analog_callbacks.new_ast_channel) {
		return NULL;
	}

	c = analog_callbacks.new_ast_channel(p->chan_pvt, state, startpbx, sub, requestor);
	if (c) {
		ast_channel_call_forward_set(c, p->call_forward);
	}
	p->subs[sub].owner = c;
	if (!p->owner) {
		analog_set_new_owner(p, c);
	}
	return c;
}

static int analog_set_echocanceller(struct analog_pvt *p, int enable)
{
	if (analog_callbacks.set_echocanceller) {
		return analog_callbacks.set_echocanceller(p->chan_pvt, enable);
	}
	return -1;
}

static int analog_train_echocanceller(struct analog_pvt *p)
{
	if (analog_callbacks.train_echocanceller) {
		return analog_callbacks.train_echocanceller(p->chan_pvt);
	}
	return -1;
}

static int analog_is_off_hook(struct analog_pvt *p)
{
	if (analog_callbacks.is_off_hook) {
		return analog_callbacks.is_off_hook(p->chan_pvt);
	}
	return -1;
}

static int analog_ring(struct analog_pvt *p)
{
	if (analog_callbacks.ring) {
		return analog_callbacks.ring(p->chan_pvt);
	}
	return -1;
}

static int analog_flash(struct analog_pvt *p)
{
	if (analog_callbacks.flash) {
		return analog_callbacks.flash(p->chan_pvt);
	}
	return -1;
}

static int analog_start(struct analog_pvt *p)
{
	if (analog_callbacks.start) {
		return analog_callbacks.start(p->chan_pvt);
	}
	return -1;
}

static int analog_dial_digits(struct analog_pvt *p, enum analog_sub sub, struct analog_dialoperation *dop)
{
	if (analog_callbacks.dial_digits) {
		return analog_callbacks.dial_digits(p->chan_pvt, sub, dop);
	}
	return -1;
}

static int analog_on_hook(struct analog_pvt *p)
{
	if (analog_callbacks.on_hook) {
		return analog_callbacks.on_hook(p->chan_pvt);
	}
	return -1;
}

static void analog_set_outgoing(struct analog_pvt *p, int is_outgoing)
{
	p->outgoing = is_outgoing;
	if (analog_callbacks.set_outgoing) {
		analog_callbacks.set_outgoing(p->chan_pvt, is_outgoing);
	}
}

static int analog_check_for_conference(struct analog_pvt *p)
{
	if (analog_callbacks.check_for_conference) {
		return analog_callbacks.check_for_conference(p->chan_pvt);
	}
	return -1;
}

static void analog_all_subchannels_hungup(struct analog_pvt *p)
{
	if (analog_callbacks.all_subchannels_hungup) {
		analog_callbacks.all_subchannels_hungup(p->chan_pvt);
	}
}

static void analog_unlock_private(struct analog_pvt *p)
{
	if (analog_callbacks.unlock_private) {
		analog_callbacks.unlock_private(p->chan_pvt);
	}
}

static void analog_lock_private(struct analog_pvt *p)
{
	if (analog_callbacks.lock_private) {
		analog_callbacks.lock_private(p->chan_pvt);
	}
}

static void analog_deadlock_avoidance_private(struct analog_pvt *p)
{
	if (analog_callbacks.deadlock_avoidance_private) {
		analog_callbacks.deadlock_avoidance_private(p->chan_pvt);
	} else {
		/* Fallback to manual avoidance if callback not present. */
		analog_unlock_private(p);
		usleep(1);
		analog_lock_private(p);
	}
}

/*!
 * \internal
 * \brief Obtain the specified subchannel owner lock if the owner exists.
 *
 * \param pvt Analog private struct.
 * \param sub_idx Subchannel owner to lock.
 *
 * \note Assumes the analog_lock_private(pvt->chan_pvt) is already obtained.
 *
 * \note
 * Because deadlock avoidance may have been necessary, you need to confirm
 * the state of things before continuing.
 *
 * \return Nothing
 */
static void analog_lock_sub_owner(struct analog_pvt *pvt, enum analog_sub sub_idx)
{
	for (;;) {
		if (!pvt->subs[sub_idx].owner) {
			/* No subchannel owner pointer */
			break;
		}
		if (!ast_channel_trylock(pvt->subs[sub_idx].owner)) {
			/* Got subchannel owner lock */
			break;
		}
		/* We must unlock the private to avoid the possibility of a deadlock */
		analog_deadlock_avoidance_private(pvt);
	}
}

static int analog_off_hook(struct analog_pvt *p)
{
	if (analog_callbacks.off_hook) {
		return analog_callbacks.off_hook(p->chan_pvt);
	}
	return -1;
}

static void analog_set_needringing(struct analog_pvt *p, int value)
{
	if (analog_callbacks.set_needringing) {
		analog_callbacks.set_needringing(p->chan_pvt, value);
	}
}

#if 0
static void analog_set_polarity(struct analog_pvt *p, int value)
{
	if (analog_callbacks.set_polarity) {
		analog_callbacks.set_polarity(p->chan_pvt, value);
	}
}
#endif

static void analog_start_polarityswitch(struct analog_pvt *p)
{
	if (analog_callbacks.start_polarityswitch) {
		analog_callbacks.start_polarityswitch(p->chan_pvt);
	}
}
static void analog_answer_polarityswitch(struct analog_pvt *p)
{
	if (analog_callbacks.answer_polarityswitch) {
		analog_callbacks.answer_polarityswitch(p->chan_pvt);
	}
}

static void analog_hangup_polarityswitch(struct analog_pvt *p)
{
	if (analog_callbacks.hangup_polarityswitch) {
		analog_callbacks.hangup_polarityswitch(p->chan_pvt);
	}
}

static int analog_dsp_set_digitmode(struct analog_pvt *p, enum analog_dsp_digitmode mode)
{
	if (analog_callbacks.dsp_set_digitmode) {
		return analog_callbacks.dsp_set_digitmode(p->chan_pvt, mode);
	}
	return -1;
}

static void analog_cb_handle_dtmf(struct analog_pvt *p, struct ast_channel *ast, enum analog_sub analog_index, struct ast_frame **dest)
{
	if (analog_callbacks.handle_dtmf) {
		analog_callbacks.handle_dtmf(p->chan_pvt, ast, analog_index, dest);
	}
}

static int analog_wink(struct analog_pvt *p, enum analog_sub index)
{
	if (analog_callbacks.wink) {
		return analog_callbacks.wink(p->chan_pvt, index);
	}
	return -1;
}

static int analog_has_voicemail(struct analog_pvt *p)
{
	if (analog_callbacks.has_voicemail) {
		return analog_callbacks.has_voicemail(p->chan_pvt);
	}
	return -1;
}

static int analog_is_dialing(struct analog_pvt *p, enum analog_sub index)
{
	if (analog_callbacks.is_dialing) {
		return analog_callbacks.is_dialing(p->chan_pvt, index);
	}
	return -1;
}

/*!
 * \internal
 * \brief Attempt to transfer 3-way call.
 *
 * \param p Analog private structure.
 * \param inthreeway TRUE if the 3-way call is conferenced.
 *
 * \note On entry these locks are held: real-call, private, 3-way call.
 * \note On exit these locks are held: real-call, private.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int analog_attempt_transfer(struct analog_pvt *p, int inthreeway)
{
	struct ast_channel *owner_real;
	struct ast_channel *owner_3way;
	struct ast_channel *bridge_real;
	struct ast_channel *bridge_3way;
	int ret = 0;

	owner_real = p->subs[ANALOG_SUB_REAL].owner;
	owner_3way = p->subs[ANALOG_SUB_THREEWAY].owner;
	bridge_real = ast_bridged_channel(owner_real);
	bridge_3way = ast_bridged_channel(owner_3way);

	/*
	 * In order to transfer, we need at least one of the channels to
	 * actually be in a call bridge.  We can't conference two
	 * applications together.  Why would we want to?
	 */
	if (bridge_3way) {
		ast_verb(3, "TRANSFERRING %s to %s\n", ast_channel_name(owner_3way), ast_channel_name(owner_real));
		ast_cel_report_event(owner_3way,
			(ast_channel_state(owner_real) == AST_STATE_RINGING
				|| ast_channel_state(owner_3way) == AST_STATE_RINGING)
				? AST_CEL_BLINDTRANSFER : AST_CEL_ATTENDEDTRANSFER,
			NULL, ast_channel_linkedid(owner_3way), NULL);

		/*
		 * The three-way party we're about to transfer is on hold if he
		 * is not in a three way conference.
		 */
		if (ast_channel_transfer_masquerade(owner_real, ast_channel_connected(owner_real), 0,
			bridge_3way, ast_channel_connected(owner_3way), !inthreeway)) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
				ast_channel_name(bridge_3way), ast_channel_name(owner_real));
			ret = -1;
		}
	} else if (bridge_real) {
		/* Try transferring the other way. */
		ast_verb(3, "TRANSFERRING %s to %s\n", ast_channel_name(owner_real), ast_channel_name(owner_3way));
		ast_cel_report_event(owner_3way,
			(ast_channel_state(owner_real) == AST_STATE_RINGING
				|| ast_channel_state(owner_3way) == AST_STATE_RINGING)
				? AST_CEL_BLINDTRANSFER : AST_CEL_ATTENDEDTRANSFER,
			NULL, ast_channel_linkedid(owner_3way), NULL);

		/*
		 * The three-way party we're about to transfer is on hold if he
		 * is not in a three way conference.
		 */
		if (ast_channel_transfer_masquerade(owner_3way, ast_channel_connected(owner_3way),
			!inthreeway, bridge_real, ast_channel_connected(owner_real), 0)) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
				ast_channel_name(bridge_real), ast_channel_name(owner_3way));
			ret = -1;
		}
	} else {
		ast_debug(1, "Neither %s nor %s are in a bridge, nothing to transfer\n",
			ast_channel_name(owner_real), ast_channel_name(owner_3way));
		ret = -1;
	}

	if (ret) {
		ast_softhangup_nolock(owner_3way, AST_SOFTHANGUP_DEV);
	}
	ast_channel_unlock(owner_3way);
	return ret;
}

static int analog_update_conf(struct analog_pvt *p)
{
	int x;
	int needconf = 0;

	/* Start with the obvious, general stuff */
	for (x = 0; x < 3; x++) {
		/* Look for three way calls */
		if ((p->subs[x].allocd) && p->subs[x].inthreeway) {
			if (analog_callbacks.conf_add) {
				analog_callbacks.conf_add(p->chan_pvt, x);
			}
			needconf++;
		} else {
			if (analog_callbacks.conf_del) {
				analog_callbacks.conf_del(p->chan_pvt, x);
			}
		}
	}
	ast_debug(1, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);

	if (analog_callbacks.complete_conference_update) {
		analog_callbacks.complete_conference_update(p->chan_pvt, needconf);
	}
	return 0;
}

struct ast_channel * analog_request(struct analog_pvt *p, int *callwait, const struct ast_channel *requestor)
{
	struct ast_channel *ast;

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);
	*callwait = (p->owner != NULL);

	if (p->owner) {
		if (analog_alloc_sub(p, ANALOG_SUB_CALLWAIT)) {
			ast_log(LOG_ERROR, "Unable to alloc subchannel\n");
			return NULL;
		}
	}

	analog_set_outgoing(p, 1);
	ast = analog_new_ast_channel(p, AST_STATE_RESERVED, 0,
		p->owner ? ANALOG_SUB_CALLWAIT : ANALOG_SUB_REAL, requestor);
	if (!ast) {
		analog_set_outgoing(p, 0);
	}
	return ast;
}

int analog_available(struct analog_pvt *p)
{
	int offhook;

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);

	/* If do not disturb, definitely not */
	if (p->dnd) {
		return 0;
	}
	/* If guard time, definitely not */
	if (p->guardtime && (time(NULL) < p->guardtime)) {
		return 0;
	}

	/* If no owner definitely available */
	if (!p->owner) {
		offhook = analog_is_off_hook(p);

		/* TDM FXO card, "onhook" means out of service (no battery on the line) */
		if ((p->sig == ANALOG_SIG_FXSLS) || (p->sig == ANALOG_SIG_FXSKS) || (p->sig == ANALOG_SIG_FXSGS)) {
#ifdef DAHDI_CHECK_HOOKSTATE
			if (offhook) {
				return 1;
			}
			return 0;
#endif
		/* TDM FXS card, "offhook" means someone took the hook off so it's unavailable! */
		} else if (offhook) {
			ast_debug(1, "Channel %d off hook, can't use\n", p->channel);
			/* Not available when the other end is off hook */
			return 0;
		}
		return 1;
	}

	/* If it's not an FXO, forget about call wait */
	if ((p->sig != ANALOG_SIG_FXOKS) && (p->sig != ANALOG_SIG_FXOLS) && (p->sig != ANALOG_SIG_FXOGS)) {
		return 0;
	}

	if (!p->callwaiting) {
		/* If they don't have call waiting enabled, then for sure they're unavailable at this point */
		return 0;
	}

	if (p->subs[ANALOG_SUB_CALLWAIT].allocd) {
		/* If there is already a call waiting call, then we can't take a second one */
		return 0;
	}

	if ((ast_channel_state(p->owner) != AST_STATE_UP) &&
	    ((ast_channel_state(p->owner) != AST_STATE_RINGING) || p->outgoing)) {
		/* If the current call is not up, then don't allow the call */
		return 0;
	}
	if ((p->subs[ANALOG_SUB_THREEWAY].owner) && (!p->subs[ANALOG_SUB_THREEWAY].inthreeway)) {
		/* Can't take a call wait when the three way calling hasn't been merged yet. */
		return 0;
	}
	/* We're cool */
	return 1;
}

static int analog_stop_callwait(struct analog_pvt *p)
{
	p->callwaitcas = 0;
	if (analog_callbacks.stop_callwait) {
		return analog_callbacks.stop_callwait(p->chan_pvt);
	}
	return 0;
}

static int analog_callwait(struct analog_pvt *p)
{
	p->callwaitcas = p->callwaitingcallerid;
	if (analog_callbacks.callwait) {
		return analog_callbacks.callwait(p->chan_pvt);
	}
	return 0;
}

static void analog_set_callwaiting(struct analog_pvt *p, int callwaiting_enable)
{
	p->callwaiting = callwaiting_enable;
	if (analog_callbacks.set_callwaiting) {
		analog_callbacks.set_callwaiting(p->chan_pvt, callwaiting_enable);
	}
}

static void analog_set_cadence(struct analog_pvt *p, struct ast_channel *chan)
{
	if (analog_callbacks.set_cadence) {
		analog_callbacks.set_cadence(p->chan_pvt, &p->cidrings, chan);
	}
}

static void analog_set_dialing(struct analog_pvt *p, int is_dialing)
{
	p->dialing = is_dialing;
	if (analog_callbacks.set_dialing) {
		analog_callbacks.set_dialing(p->chan_pvt, is_dialing);
	}
}

static void analog_set_alarm(struct analog_pvt *p, int in_alarm)
{
	p->inalarm = in_alarm;
	if (analog_callbacks.set_alarm) {
		analog_callbacks.set_alarm(p->chan_pvt, in_alarm);
	}
}

static void analog_set_ringtimeout(struct analog_pvt *p, int ringt)
{
	p->ringt = ringt;
	if (analog_callbacks.set_ringtimeout) {
		analog_callbacks.set_ringtimeout(p->chan_pvt, ringt);
	}
}

static void analog_set_waitingfordt(struct analog_pvt *p, struct ast_channel *ast)
{
	if (analog_callbacks.set_waitingfordt) {
		analog_callbacks.set_waitingfordt(p->chan_pvt, ast);
	}
}

static int analog_check_waitingfordt(struct analog_pvt *p)
{
	if (analog_callbacks.check_waitingfordt) {
		return analog_callbacks.check_waitingfordt(p->chan_pvt);
	}

	return 0;
}

static void analog_set_confirmanswer(struct analog_pvt *p, int flag)
{
	if (analog_callbacks.set_confirmanswer) {
		analog_callbacks.set_confirmanswer(p->chan_pvt, flag);
	}
}

static int analog_check_confirmanswer(struct analog_pvt *p)
{
	if (analog_callbacks.check_confirmanswer) {
		return analog_callbacks.check_confirmanswer(p->chan_pvt);
	}

	return 0;
}

static void analog_cancel_cidspill(struct analog_pvt *p)
{
	if (analog_callbacks.cancel_cidspill) {
		analog_callbacks.cancel_cidspill(p->chan_pvt);
	}
}

static int analog_confmute(struct analog_pvt *p, int mute)
{
	if (analog_callbacks.confmute) {
		return analog_callbacks.confmute(p->chan_pvt, mute);
	}
	return 0;
}

static void analog_set_pulsedial(struct analog_pvt *p, int flag)
{
	if (analog_callbacks.set_pulsedial) {
		analog_callbacks.set_pulsedial(p->chan_pvt, flag);
	}
}

static int analog_set_linear_mode(struct analog_pvt *p, enum analog_sub sub, int linear_mode)
{
	if (analog_callbacks.set_linear_mode) {
		/* Return provides old linear_mode setting or error indication */
		return analog_callbacks.set_linear_mode(p->chan_pvt, sub, linear_mode);
	}
	return -1;
}

static void analog_set_inthreeway(struct analog_pvt *p, enum analog_sub sub, int inthreeway)
{
	p->subs[sub].inthreeway = inthreeway;
	if (analog_callbacks.set_inthreeway) {
		analog_callbacks.set_inthreeway(p->chan_pvt, sub, inthreeway);
	}
}

int analog_call(struct analog_pvt *p, struct ast_channel *ast, const char *rdest, int timeout)
{
	int res, idx, mysig;
	char *c, *n, *l;
	char dest[256]; /* must be same length as p->dialdest */

	ast_debug(1, "CALLING CID_NAME: %s CID_NUM:: %s\n",
		S_COR(ast_channel_connected(ast)->id.name.valid, ast_channel_connected(ast)->id.name.str, ""),
		S_COR(ast_channel_connected(ast)->id.number.valid, ast_channel_connected(ast)->id.number.str, ""));

	ast_copy_string(dest, rdest, sizeof(dest));
	ast_copy_string(p->dialdest, rdest, sizeof(p->dialdest));

	if ((ast_channel_state(ast) == AST_STATE_BUSY)) {
		ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_BUSY);
		return 0;
	}

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "analog_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}

	p->dialednone = 0;
	analog_set_outgoing(p, 1);

	mysig = p->sig;
	if (p->outsigmod > -1) {
		mysig = p->outsigmod;
	}

	switch (mysig) {
	case ANALOG_SIG_FXOLS:
	case ANALOG_SIG_FXOGS:
	case ANALOG_SIG_FXOKS:
		if (p->owner == ast) {
			/* Normal ring, on hook */

			/* Don't send audio while on hook, until the call is answered */
			analog_set_dialing(p, 1);
			analog_set_cadence(p, ast); /* and set p->cidrings */

			/* nick@dccinc.com 4/3/03 mods to allow for deferred dialing */
			c = strchr(dest, '/');
			if (c) {
				c++;
			}
			if (c && (strlen(c) < p->stripmsd)) {
				ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
				c = NULL;
			}
			if (c) {
				p->dop.op = ANALOG_DIAL_OP_REPLACE;
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "Tw%s", c);
				ast_debug(1, "FXO: setup deferred dialstring: %s\n", c);
			} else {
				p->dop.dialstr[0] = '\0';
			}

			if (analog_ring(p)) {
				ast_log(LOG_WARNING, "Unable to ring phone: %s\n", strerror(errno));
				return -1;
			}
			analog_set_dialing(p, 1);
		} else {
			/* Call waiting call */
			if (ast_channel_connected(ast)->id.number.valid && ast_channel_connected(ast)->id.number.str) {
				ast_copy_string(p->callwait_num, ast_channel_connected(ast)->id.number.str, sizeof(p->callwait_num));
			} else {
				p->callwait_num[0] = '\0';
			}
			if (ast_channel_connected(ast)->id.name.valid && ast_channel_connected(ast)->id.name.str) {
				ast_copy_string(p->callwait_name, ast_channel_connected(ast)->id.name.str, sizeof(p->callwait_name));
			} else {
				p->callwait_name[0] = '\0';
			}

			/* Call waiting tone instead */
			if (analog_callwait(p)) {
				return -1;
			}
			/* Make ring-back */
			if (analog_play_tone(p, ANALOG_SUB_CALLWAIT, ANALOG_TONE_RINGTONE)) {
				ast_log(LOG_WARNING, "Unable to generate call-wait ring-back on channel %s\n", ast_channel_name(ast));
			}

		}
		n = ast_channel_connected(ast)->id.name.valid ? ast_channel_connected(ast)->id.name.str : NULL;
		l = ast_channel_connected(ast)->id.number.valid ? ast_channel_connected(ast)->id.number.str : NULL;
		if (l) {
			ast_copy_string(p->lastcid_num, l, sizeof(p->lastcid_num));
		} else {
			p->lastcid_num[0] = '\0';
		}
		if (n) {
			ast_copy_string(p->lastcid_name, n, sizeof(p->lastcid_name));
		} else {
			p->lastcid_name[0] = '\0';
		}

		if (p->use_callerid) {
			p->caller.id.name.str = p->lastcid_name;
			p->caller.id.number.str = p->lastcid_num;
		}

		ast_setstate(ast, AST_STATE_RINGING);
		idx = analog_get_index(ast, p, 0);
		if (idx > -1) {
			struct ast_cc_config_params *cc_params;

			/* This is where the initial ringing frame is queued for an analog call.
			 * As such, this is a great time to offer CCNR to the caller if it's available.
			 */
			cc_params = ast_channel_get_cc_config_params(p->subs[idx].owner);
			if (cc_params) {
				switch (ast_get_cc_monitor_policy(cc_params)) {
				case AST_CC_MONITOR_NEVER:
					break;
				case AST_CC_MONITOR_NATIVE:
				case AST_CC_MONITOR_ALWAYS:
				case AST_CC_MONITOR_GENERIC:
					ast_queue_cc_frame(p->subs[idx].owner, AST_CC_GENERIC_MONITOR_TYPE,
						analog_get_orig_dialstring(p), AST_CC_CCNR, NULL);
					break;
				}
			}
			ast_queue_control(p->subs[idx].owner, AST_CONTROL_RINGING);
		}
		break;
	case ANALOG_SIG_FXSLS:
	case ANALOG_SIG_FXSGS:
	case ANALOG_SIG_FXSKS:
		if (p->answeronpolarityswitch || p->hanguponpolarityswitch) {
			ast_debug(1, "Ignore possible polarity reversal on line seizure\n");
			p->polaritydelaytv = ast_tvnow();
		}
		/* fall through */
	case ANALOG_SIG_EMWINK:
	case ANALOG_SIG_EM:
	case ANALOG_SIG_EM_E1:
	case ANALOG_SIG_FEATD:
	case ANALOG_SIG_FEATDMF:
	case ANALOG_SIG_E911:
	case ANALOG_SIG_FGC_CAMA:
	case ANALOG_SIG_FGC_CAMAMF:
	case ANALOG_SIG_FEATB:
	case ANALOG_SIG_SFWINK:
	case ANALOG_SIG_SF:
	case ANALOG_SIG_SF_FEATD:
	case ANALOG_SIG_SF_FEATDMF:
	case ANALOG_SIG_FEATDMF_TA:
	case ANALOG_SIG_SF_FEATB:
		c = strchr(dest, '/');
		if (c) {
			c++;
		} else {
			c = "";
		}
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			return -1;
		}
		res = analog_start(p);
		if (res < 0) {
			if (errno != EINPROGRESS) {
				return -1;
			}
		}
		ast_debug(1, "Dialing '%s'\n", c);
		p->dop.op = ANALOG_DIAL_OP_REPLACE;

		c += p->stripmsd;

		switch (mysig) {
		case ANALOG_SIG_FEATD:
			l = ast_channel_connected(ast)->id.number.valid ? ast_channel_connected(ast)->id.number.str : NULL;
			if (l) {
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T*%s*%s*", l, c);
			} else {
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T**%s*", c);
			}
			break;
		case ANALOG_SIG_FEATDMF:
			l = ast_channel_connected(ast)->id.number.valid ? ast_channel_connected(ast)->id.number.str : NULL;
			if (l) {
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*00%s#*%s#", l, c);
			} else {
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*02#*%s#", c);
			}
			break;
		case ANALOG_SIG_FEATDMF_TA:
		{
			const char *cic = "", *ozz = "";

			/* If you have to go through a Tandem Access point you need to use this */
#ifndef STANDALONE
			ozz = pbx_builtin_getvar_helper(p->owner, "FEATDMF_OZZ");
			if (!ozz) {
				ozz = analog_defaultozz;
			}
			cic = pbx_builtin_getvar_helper(p->owner, "FEATDMF_CIC");
			if (!cic) {
				cic = analog_defaultcic;
			}
#endif
			if (!ozz || !cic) {
				ast_log(LOG_WARNING, "Unable to dial channel of type feature group D MF tandem access without CIC or OZZ set\n");
				return -1;
			}
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%s%s#", ozz, cic);
			snprintf(p->finaldial, sizeof(p->finaldial), "M*%s#", c);
			p->whichwink = 0;
		}
			break;
		case ANALOG_SIG_E911:
			ast_copy_string(p->dop.dialstr, "M*911#", sizeof(p->dop.dialstr));
			break;
		case ANALOG_SIG_FGC_CAMA:
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "P%s", c);
			break;
		case ANALOG_SIG_FGC_CAMAMF:
		case ANALOG_SIG_FEATB:
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%s#", c);
			break;
		default:
			if (p->pulse) {
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "P%sw", c);
			} else {
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%sw", c);
			}
			break;
		}

		if (p->echotraining && (strlen(p->dop.dialstr) > 4)) {
			memset(p->echorest, 'w', sizeof(p->echorest) - 1);
			strcpy(p->echorest + (p->echotraining / 400) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
			p->echorest[sizeof(p->echorest) - 1] = '\0';
			p->echobreak = 1;
			p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
		} else {
			p->echobreak = 0;
		}
		analog_set_waitingfordt(p, ast);
		if (!res) {
			if (analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop)) {
				int saveerr = errno;

				analog_on_hook(p);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(saveerr));
				return -1;
			}
		} else {
			ast_debug(1, "Deferring dialing...\n");
		}
		analog_set_dialing(p, 1);
		if (ast_strlen_zero(c)) {
			p->dialednone = 1;
		}
		ast_setstate(ast, AST_STATE_DIALING);
		break;
	default:
		ast_debug(1, "not yet implemented\n");
		return -1;
	}
	return 0;
}

int analog_hangup(struct analog_pvt *p, struct ast_channel *ast)
{
	int res;
	int idx, x;

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);
	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	idx = analog_get_index(ast, p, 1);

	x = 0;
	if (p->origcid_num) {
		ast_copy_string(p->cid_num, p->origcid_num, sizeof(p->cid_num));
		ast_free(p->origcid_num);
		p->origcid_num = NULL;
	}
	if (p->origcid_name) {
		ast_copy_string(p->cid_name, p->origcid_name, sizeof(p->cid_name));
		ast_free(p->origcid_name);
		p->origcid_name = NULL;
	}

	analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);

	ast_debug(1, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		p->channel, idx, p->subs[ANALOG_SUB_REAL].allocd, p->subs[ANALOG_SUB_CALLWAIT].allocd, p->subs[ANALOG_SUB_THREEWAY].allocd);
	if (idx > -1) {
		/* Real channel, do some fixup */
		p->subs[idx].owner = NULL;
		p->polarity = POLARITY_IDLE;
		analog_set_linear_mode(p, idx, 0);
		switch (idx) {
		case ANALOG_SUB_REAL:
			if (p->subs[ANALOG_SUB_CALLWAIT].allocd && p->subs[ANALOG_SUB_THREEWAY].allocd) {
				ast_debug(1, "Normal call hung up with both three way call and a call waiting call in place?\n");
				if (p->subs[ANALOG_SUB_CALLWAIT].inthreeway) {
					/* We had flipped over to answer a callwait and now it's gone */
					ast_debug(1, "We were flipped over to the callwait, moving back and unowning.\n");
					/* Move to the call-wait, but un-own us until they flip back. */
					analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_REAL);
					analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
					analog_set_new_owner(p, NULL);
				} else {
					/* The three way hung up, but we still have a call wait */
					ast_debug(1, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
					analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
					analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
					if (p->subs[ANALOG_SUB_REAL].inthreeway) {
						/* This was part of a three way call.  Immediately make way for
						   another call */
						ast_debug(1, "Call was complete, setting owner to former third call\n");
						analog_set_inthreeway(p, ANALOG_SUB_REAL, 0);
						analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
					} else {
						/* This call hasn't been completed yet...  Set owner to NULL */
						ast_debug(1, "Call was incomplete, setting owner to NULL\n");
						analog_set_new_owner(p, NULL);
					}
				}
			} else if (p->subs[ANALOG_SUB_CALLWAIT].allocd) {
				/* Need to hold the lock for real-call, private, and call-waiting call */
				analog_lock_sub_owner(p, ANALOG_SUB_CALLWAIT);
				if (!p->subs[ANALOG_SUB_CALLWAIT].owner) {
					/* The call waiting call dissappeared. */
					analog_set_new_owner(p, NULL);
					break;
				}

				/* Move to the call-wait and switch back to them. */
				analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_REAL);
				analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
				analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
				if (ast_channel_state(p->owner) != AST_STATE_UP) {
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_ANSWER);
				}
				if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner)) {
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
				}
				/* Unlock the call-waiting call that we swapped to real-call. */
				ast_channel_unlock(p->subs[ANALOG_SUB_REAL].owner);
			} else if (p->subs[ANALOG_SUB_THREEWAY].allocd) {
				analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
				analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
				if (p->subs[ANALOG_SUB_REAL].inthreeway) {
					/* This was part of a three way call.  Immediately make way for
					   another call */
					ast_debug(1, "Call was complete, setting owner to former third call\n");
					analog_set_inthreeway(p, ANALOG_SUB_REAL, 0);
					analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
				} else {
					/* This call hasn't been completed yet...  Set owner to NULL */
					ast_debug(1, "Call was incomplete, setting owner to NULL\n");
					analog_set_new_owner(p, NULL);
				}
			}
			break;
		case ANALOG_SUB_CALLWAIT:
			/* Ditch the holding callwait call, and immediately make it available */
			if (p->subs[ANALOG_SUB_CALLWAIT].inthreeway) {
				/* Need to hold the lock for call-waiting call, private, and 3-way call */
				analog_lock_sub_owner(p, ANALOG_SUB_THREEWAY);

				/* This is actually part of a three way, placed on hold.  Place the third part
				   on music on hold now */
				if (p->subs[ANALOG_SUB_THREEWAY].owner && ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_THREEWAY].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				analog_set_inthreeway(p, ANALOG_SUB_THREEWAY, 0);
				/* Make it the call wait now */
				analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_THREEWAY);
				analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
				if (p->subs[ANALOG_SUB_CALLWAIT].owner) {
					/* Unlock the 3-way call that we swapped to call-waiting call. */
					ast_channel_unlock(p->subs[ANALOG_SUB_CALLWAIT].owner);
				}
			} else {
				analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
			}
			break;
		case ANALOG_SUB_THREEWAY:
			/* Need to hold the lock for 3-way call, private, and call-waiting call */
			analog_lock_sub_owner(p, ANALOG_SUB_CALLWAIT);
			if (p->subs[ANALOG_SUB_CALLWAIT].inthreeway) {
				/* The other party of the three way call is currently in a call-wait state.
				   Start music on hold for them, and take the main guy out of the third call */
				analog_set_inthreeway(p, ANALOG_SUB_CALLWAIT, 0);
				if (p->subs[ANALOG_SUB_CALLWAIT].owner && ast_bridged_channel(p->subs[ANALOG_SUB_CALLWAIT].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_CALLWAIT].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
			}
			if (p->subs[ANALOG_SUB_CALLWAIT].owner) {
				ast_channel_unlock(p->subs[ANALOG_SUB_CALLWAIT].owner);
			}
			analog_set_inthreeway(p, ANALOG_SUB_REAL, 0);
			/* If this was part of a three way call index, let us make
			   another three way call */
			analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
			break;
		default:
			/*
			 * Should never happen.
			 * This wasn't any sort of call, so how are we an index?
			 */
			ast_log(LOG_ERROR, "Index found but not any type of call?\n");
			break;
		}
	}

	if (!p->subs[ANALOG_SUB_REAL].owner && !p->subs[ANALOG_SUB_CALLWAIT].owner && !p->subs[ANALOG_SUB_THREEWAY].owner) {
		analog_set_new_owner(p, NULL);
		analog_set_ringtimeout(p, 0);
		analog_set_confirmanswer(p, 0);
		analog_set_pulsedial(p, 0);
		analog_set_outgoing(p, 0);
		p->onhooktime = time(NULL);
		p->cidrings = 1;

		/* Perform low level hangup if no owner left */
		res = analog_on_hook(p);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast_channel_name(ast));
		}
		switch (p->sig) {
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOKS:
			/* If they're off hook, try playing congestion */
			if (analog_is_off_hook(p)) {
				analog_hangup_polarityswitch(p);
				analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
			} else {
				analog_play_tone(p, ANALOG_SUB_REAL, -1);
			}
			break;
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSKS:
			/* Make sure we're not made available for at least two seconds assuming
			   we were actually used for an inbound or outbound call. */
			if (ast_channel_state(ast) != AST_STATE_RESERVED) {
				time(&p->guardtime);
				p->guardtime += 2;
			}
			break;
		default:
			analog_play_tone(p, ANALOG_SUB_REAL, -1);
			break;
		}

		analog_set_echocanceller(p, 0);

		x = 0;
		ast_channel_setoption(ast,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		ast_channel_setoption(ast,AST_OPTION_TDD,&x,sizeof(char),0);
		p->callwaitcas = 0;
		analog_set_callwaiting(p, p->permcallwaiting);
		p->hidecallerid = p->permhidecallerid;
		analog_set_dialing(p, 0);
		analog_update_conf(p);
		analog_all_subchannels_hungup(p);
	}

	analog_stop_callwait(p);

	ast_verb(3, "Hanging up on '%s'\n", ast_channel_name(ast));

	return 0;
}

int analog_answer(struct analog_pvt *p, struct ast_channel *ast)
{
	int res = 0;
	int idx;
	int oldstate = ast_channel_state(ast);

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);
	ast_setstate(ast, AST_STATE_UP);
	idx = analog_get_index(ast, p, 1);
	if (idx < 0) {
		idx = ANALOG_SUB_REAL;
	}
	switch (p->sig) {
	case ANALOG_SIG_FXSLS:
	case ANALOG_SIG_FXSGS:
	case ANALOG_SIG_FXSKS:
		analog_set_ringtimeout(p, 0);
		/* Fall through */
	case ANALOG_SIG_EM:
	case ANALOG_SIG_EM_E1:
	case ANALOG_SIG_EMWINK:
	case ANALOG_SIG_FEATD:
	case ANALOG_SIG_FEATDMF:
	case ANALOG_SIG_FEATDMF_TA:
	case ANALOG_SIG_E911:
	case ANALOG_SIG_FGC_CAMA:
	case ANALOG_SIG_FGC_CAMAMF:
	case ANALOG_SIG_FEATB:
	case ANALOG_SIG_SF:
	case ANALOG_SIG_SFWINK:
	case ANALOG_SIG_SF_FEATD:
	case ANALOG_SIG_SF_FEATDMF:
	case ANALOG_SIG_SF_FEATB:
	case ANALOG_SIG_FXOLS:
	case ANALOG_SIG_FXOGS:
	case ANALOG_SIG_FXOKS:
		/* Pick up the line */
		ast_debug(1, "Took %s off hook\n", ast_channel_name(ast));
		if (p->hanguponpolarityswitch) {
			gettimeofday(&p->polaritydelaytv, NULL);
		}
		res = analog_off_hook(p);
		analog_play_tone(p, idx, -1);
		analog_set_dialing(p, 0);
		if ((idx == ANALOG_SUB_REAL) && p->subs[ANALOG_SUB_THREEWAY].inthreeway) {
			if (oldstate == AST_STATE_RINGING) {
				ast_debug(1, "Finally swapping real and threeway\n");
				analog_play_tone(p, ANALOG_SUB_THREEWAY, -1);
				analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
				analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
			}
		}

		switch (p->sig) {
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSKS:
		case ANALOG_SIG_FXSGS:
			analog_set_echocanceller(p, 1);
			analog_train_echocanceller(p);
			break;
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOKS:
		case ANALOG_SIG_FXOGS:
			analog_answer_polarityswitch(p);
			break;
		default:
			break;
		}
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		res = -1;
		break;
	}
	ast_setstate(ast, AST_STATE_UP);
	return res;
}

static int analog_handles_digit(struct ast_frame *f)
{
	char subclass = toupper(f->subclass.integer);

	switch (subclass) {
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '9':
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F':
		return 1;
	default:
		return 0;
	}
}

void analog_handle_dtmf(struct analog_pvt *p, struct ast_channel *ast, enum analog_sub idx, struct ast_frame **dest)
{
	struct ast_frame *f = *dest;

	ast_debug(1, "%s DTMF digit: 0x%02X '%c' on %s\n",
		f->frametype == AST_FRAME_DTMF_BEGIN ? "Begin" : "End",
		(unsigned)f->subclass.integer, f->subclass.integer, ast_channel_name(ast));

	if (analog_check_confirmanswer(p)) {
		if (f->frametype == AST_FRAME_DTMF_END) {
			ast_debug(1, "Confirm answer on %s!\n", ast_channel_name(ast));
			/* Upon receiving a DTMF digit, consider this an answer confirmation instead
			of a DTMF digit */
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
			/* Reset confirmanswer so DTMF's will behave properly for the duration of the call */
			analog_set_confirmanswer(p, 0);
		} else {
			p->subs[idx].f.frametype = AST_FRAME_NULL;
			p->subs[idx].f.subclass.integer = 0;
		}
		*dest = &p->subs[idx].f;
	} else if (p->callwaitcas) {
		if (f->frametype == AST_FRAME_DTMF_END) {
			if ((f->subclass.integer == 'A') || (f->subclass.integer == 'D')) {
				ast_debug(1, "Got some DTMF, but it's for the CAS\n");
				p->caller.id.name.str = p->callwait_name;
				p->caller.id.number.str = p->callwait_num;
				analog_send_callerid(p, 1, &p->caller);
			}
			if (analog_handles_digit(f)) {
				p->callwaitcas = 0;
			}
		}
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
		*dest = &p->subs[idx].f;
	} else {
		analog_cb_handle_dtmf(p, ast, idx, dest);
	}
}

static int analog_my_getsigstr(struct ast_channel *chan, char *str, const char *term, int ms)
{
	char c;

	*str = 0; /* start with empty output buffer */
	for (;;) {
		/* Wait for the first digit (up to specified ms). */
		c = ast_waitfordigit(chan, ms);
		/* if timeout, hangup or error, return as such */
		if (c < 1) {
			return c;
		}
		*str++ = c;
		*str = 0;
		if (strchr(term, c)) {
			return 1;
		}
	}
}

static int analog_handle_notify_message(struct ast_channel *chan, struct analog_pvt *p, int cid_flags, int neon_mwievent)
{
	if (analog_callbacks.handle_notify_message) {
		analog_callbacks.handle_notify_message(chan, p->chan_pvt, cid_flags, neon_mwievent);
		return 0;
	}
	return -1;
}

static void analog_increase_ss_count(void)
{
	if (analog_callbacks.increase_ss_count) {
		analog_callbacks.increase_ss_count();
	}
}

static void analog_decrease_ss_count(void)
{
	if (analog_callbacks.decrease_ss_count) {
		analog_callbacks.decrease_ss_count();
	}
}

static int analog_distinctive_ring(struct ast_channel *chan, struct analog_pvt *p, int idx, int *ringdata)
{
	if (analog_callbacks.distinctive_ring) {
		return analog_callbacks.distinctive_ring(chan, p->chan_pvt, idx, ringdata);
	}
	return -1;

}

static void analog_get_and_handle_alarms(struct analog_pvt *p)
{
	if (analog_callbacks.get_and_handle_alarms) {
		analog_callbacks.get_and_handle_alarms(p->chan_pvt);
	}
}

static void *analog_get_bridged_channel(struct ast_channel *chan)
{
	if (analog_callbacks.get_sigpvt_bridged_channel) {
		return analog_callbacks.get_sigpvt_bridged_channel(chan);
	}
	return NULL;
}

static int analog_get_sub_fd(struct analog_pvt *p, enum analog_sub sub)
{
	if (analog_callbacks.get_sub_fd) {
		return analog_callbacks.get_sub_fd(p->chan_pvt, sub);
	}
	return -1;
}

#define ANALOG_NEED_MFDETECT(p) (((p)->sig == ANALOG_SIG_FEATDMF) || ((p)->sig == ANALOG_SIG_FEATDMF_TA) || ((p)->sig == ANALOG_SIG_E911) || ((p)->sig == ANALOG_SIG_FGC_CAMA) || ((p)->sig == ANALOG_SIG_FGC_CAMAMF) || ((p)->sig == ANALOG_SIG_FEATB))

static int analog_canmatch_featurecode(const char *exten)
{
	int extlen = strlen(exten);
	const char *pickup_ext;
	if (!extlen) {
		return 1;
	}
	pickup_ext = ast_pickup_ext();
	if (extlen < strlen(pickup_ext) && !strncmp(pickup_ext, exten, extlen)) {
		return 1;
	}
	/* hardcoded features are *60, *67, *69, *70, *72, *73, *78, *79, *82, *0 */
	if (exten[0] == '*' && extlen < 3) {
		if (extlen == 1) {
			return 1;
		}
		/* "*0" should be processed before it gets here */
		switch (exten[1]) {
		case '6':
		case '7':
		case '8':
			return 1;
		}
	}
	return 0;
}

static void *__analog_ss_thread(void *data)
{
	struct analog_pvt *p = data;
	struct ast_channel *chan = p->ss_astchan;
	char exten[AST_MAX_EXTENSION] = "";
	char exten2[AST_MAX_EXTENSION] = "";
	char dtmfcid[300];
	char dtmfbuf[300];
	char namebuf[ANALOG_MAX_CID];
	char numbuf[ANALOG_MAX_CID];
	char *name = NULL, *number = NULL;
	int flags = 0;
	struct ast_smdi_md_message *smdi_msg = NULL;
	int timeout;
	int getforward = 0;
	char *s1, *s2;
	int len = 0;
	int res;
	int idx;
	struct ast_callid *callid;

	analog_increase_ss_count();

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);

	if (!chan) {
		/* What happened to the channel? */
		goto quit;
	}

	if ((callid = ast_channel_callid(chan))) {
		ast_callid_threadassoc_add(callid);
		ast_callid_unref(callid);
	}

	/* in the bizarre case where the channel has become a zombie before we
	   even get started here, abort safely
	*/
	if (!ast_channel_tech_pvt(chan)) {
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", ast_channel_name(chan));
		ast_hangup(chan);
		goto quit;
	}

	ast_verb(3, "Starting simple switch on '%s'\n", ast_channel_name(chan));
	idx = analog_get_index(chan, p, 0);
	if (idx < 0) {
		ast_hangup(chan);
		goto quit;
	}
	analog_dsp_reset_and_flush_digits(p);
	switch (p->sig) {
	case ANALOG_SIG_FEATD:
	case ANALOG_SIG_FEATDMF:
	case ANALOG_SIG_FEATDMF_TA:
	case ANALOG_SIG_E911:
	case ANALOG_SIG_FGC_CAMAMF:
	case ANALOG_SIG_FEATB:
	case ANALOG_SIG_EMWINK:
	case ANALOG_SIG_SF_FEATD:
	case ANALOG_SIG_SF_FEATDMF:
	case ANALOG_SIG_SF_FEATB:
	case ANALOG_SIG_SFWINK:
		if (analog_wink(p, idx))
			goto quit;
		/* Fall through */
	case ANALOG_SIG_EM:
	case ANALOG_SIG_EM_E1:
	case ANALOG_SIG_SF:
	case ANALOG_SIG_FGC_CAMA:
		res = analog_play_tone(p, idx, -1);

		analog_dsp_reset_and_flush_digits(p);

		/* set digit mode appropriately */
		if (ANALOG_NEED_MFDETECT(p)) {
			analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_MF);
		} else {
			analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);
		}

		memset(dtmfbuf, 0, sizeof(dtmfbuf));
		/* Wait for the first digit only if immediate=no */
		if (!p->immediate) {
			/* Wait for the first digit (up to 5 seconds). */
			res = ast_waitfordigit(chan, 5000);
		} else {
			res = 0;
		}
		if (res > 0) {
			/* save first char */
			dtmfbuf[0] = res;
			switch (p->sig) {
			case ANALOG_SIG_FEATD:
			case ANALOG_SIG_SF_FEATD:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
				if (res > 0) {
					res = analog_my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
				}
				if (res < 1) {
					analog_dsp_reset_and_flush_digits(p);
				}
				break;
			case ANALOG_SIG_FEATDMF_TA:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if (res < 1) {
					analog_dsp_reset_and_flush_digits(p);
				}
				if (analog_wink(p, idx)) {
					goto quit;
				}
				dtmfbuf[0] = 0;
				/* Wait for the first digit (up to 5 seconds). */
				res = ast_waitfordigit(chan, 5000);
				if (res <= 0) {
					break;
				}
				dtmfbuf[0] = res;
				/* fall through intentionally */
			case ANALOG_SIG_FEATDMF:
			case ANALOG_SIG_E911:
			case ANALOG_SIG_FGC_CAMAMF:
			case ANALOG_SIG_SF_FEATDMF:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				/* if international caca, do it again to get real ANO */
				if ((p->sig == ANALOG_SIG_FEATDMF) && (dtmfbuf[1] != '0') 
					&& (strlen(dtmfbuf) != 14)) {
					if (analog_wink(p, idx)) {
						goto quit;
					}
					dtmfbuf[0] = 0;
					/* Wait for the first digit (up to 5 seconds). */
					res = ast_waitfordigit(chan, 5000);
					if (res <= 0) {
						break;
					}
					dtmfbuf[0] = res;
					res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				}
				if (res > 0) {
					/* if E911, take off hook */
					if (p->sig == ANALOG_SIG_E911) {
						analog_off_hook(p);
					}
					res = analog_my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "#", 3000);
				}
				if (res < 1) {
					analog_dsp_reset_and_flush_digits(p);
				}
				break;
			case ANALOG_SIG_FEATB:
			case ANALOG_SIG_SF_FEATB:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if (res < 1) {
					analog_dsp_reset_and_flush_digits(p);
				}
				break;
			case ANALOG_SIG_EMWINK:
				/* if we received a '*', we are actually receiving Feature Group D
				   dial syntax, so use that mode; otherwise, fall through to normal
				   mode
				*/
				if (res == '*') {
					res = analog_my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
					if (res > 0) {
						res = analog_my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
					}
					if (res < 1) {
						analog_dsp_reset_and_flush_digits(p);
					}
					break;
				}
			default:
				/* If we got the first digit, get the rest */
				len = 1;
				dtmfbuf[len] = '\0';
				while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, ast_channel_context(chan), dtmfbuf, 1, p->cid_num)) {
					if (ast_exists_extension(chan, ast_channel_context(chan), dtmfbuf, 1, p->cid_num)) {
						timeout = analog_matchdigittimeout;
					} else {
						timeout = analog_gendigittimeout;
					}
					res = ast_waitfordigit(chan, timeout);
					if (res < 0) {
						ast_debug(1, "waitfordigit returned < 0...\n");
						ast_hangup(chan);
						goto quit;
					} else if (res) {
						dtmfbuf[len++] = res;
						dtmfbuf[len] = '\0';
					} else {
						break;
					}
				}
				break;
			}
		}
		if (res == -1) {
			ast_log(LOG_WARNING, "getdtmf on channel %d: %s\n", p->channel, strerror(errno));
			ast_hangup(chan);
			goto quit;
		} else if (res < 0) {
			ast_debug(1, "Got hung up before digits finished\n");
			ast_hangup(chan);
			goto quit;
		}

		if (p->sig == ANALOG_SIG_FGC_CAMA) {
			char anibuf[100];

			if (ast_safe_sleep(chan,1000) == -1) {
				ast_hangup(chan);
				goto quit;
			}
			analog_off_hook(p);
			analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_MF);
			res = analog_my_getsigstr(chan, anibuf, "#", 10000);
			if ((res > 0) && (strlen(anibuf) > 2)) {
				if (anibuf[strlen(anibuf) - 1] == '#') {
					anibuf[strlen(anibuf) - 1] = 0;
				}
				ast_set_callerid(chan, anibuf + 2, NULL, anibuf + 2);
			}
			analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);
		}

		ast_copy_string(exten, dtmfbuf, sizeof(exten));
		if (ast_strlen_zero(exten)) {
			ast_copy_string(exten, "s", sizeof(exten));
		}
		if (p->sig == ANALOG_SIG_FEATD || p->sig == ANALOG_SIG_EMWINK) {
			/* Look for Feature Group D on all E&M Wink and Feature Group D trunks */
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "*");
				s2 = strsep(&stringp, "*");
				if (s2) {
					if (!ast_strlen_zero(p->cid_num)) {
						ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					} else {
						ast_set_callerid(chan, s1, NULL, s1);
					}
					ast_copy_string(exten, s2, sizeof(exten));
				} else {
					ast_copy_string(exten, s1, sizeof(exten));
				}
			} else if (p->sig == ANALOG_SIG_FEATD) {
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
			}
		}
		if ((p->sig == ANALOG_SIG_FEATDMF) || (p->sig == ANALOG_SIG_FEATDMF_TA)) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				struct ast_party_caller *caller;

				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2) {
					if (!ast_strlen_zero(p->cid_num)) {
						ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					} else {
						if (*(s1 + 2)) {
							ast_set_callerid(chan, s1 + 2, NULL, s1 + 2);
						}
					}
					ast_copy_string(exten, s2 + 1, sizeof(exten));
				} else {
					ast_copy_string(exten, s1 + 2, sizeof(exten));
				}

				/* The first two digits are ani2 information. */
				caller = ast_channel_caller(chan);
				s1[2] = '\0';
				caller->ani2 = atoi(s1);
			} else {
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
			}
		}
		if ((p->sig == ANALOG_SIG_E911) || (p->sig == ANALOG_SIG_FGC_CAMAMF)) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2 && (*(s2 + 1) == '0')) {
					if (*(s2 + 2)) {
						ast_set_callerid(chan, s2 + 2, NULL, s2 + 2);
					}
				}
				if (s1) {
					ast_copy_string(exten, s1, sizeof(exten));
				} else {
					ast_copy_string(exten, "911", sizeof(exten));
				}
			} else {
				ast_log(LOG_WARNING, "Got a non-E911/FGC CAMA input on channel %d.  Assuming E&M Wink instead\n", p->channel);
			}
		}
		if (p->sig == ANALOG_SIG_FEATB) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				ast_copy_string(exten, exten2 + 1, sizeof(exten));
			} else {
				ast_log(LOG_WARNING, "Got a non-Feature Group B input on channel %d.  Assuming E&M Wink instead\n", p->channel);
			}
		}
		if ((p->sig == ANALOG_SIG_FEATDMF) || (p->sig == ANALOG_SIG_FEATDMF_TA)) {
			analog_wink(p, idx);
			/*
			 * Some switches require a minimum guard time between the last
			 * FGD wink and something that answers immediately.  This
			 * ensures it.
			 */
			if (ast_safe_sleep(chan, 100)) {
				ast_hangup(chan);
				goto quit;
			}
		}
		analog_set_echocanceller(p, 1);

		analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);

		if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1,
			ast_channel_caller(chan)->id.number.valid ? ast_channel_caller(chan)->id.number.str : NULL)) {
			ast_channel_exten_set(chan, exten);
			analog_dsp_reset_and_flush_digits(p);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero\n");
				res = analog_play_tone(p, idx, ANALOG_TONE_CONGESTION);
			}
			goto quit;
		} else {
			ast_verb(3, "Unknown extension '%s' in context '%s' requested\n", exten, ast_channel_context(chan));
			sleep(2);
			res = analog_play_tone(p, idx, ANALOG_TONE_INFO);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			} else {
				sleep(1);
			}
			res = ast_streamfile(chan, "ss-noservice", ast_channel_language(chan));
			if (res >= 0) {
				ast_waitstream(chan, "");
			}
			res = analog_play_tone(p, idx, ANALOG_TONE_CONGESTION);
			ast_hangup(chan);
			goto quit;
		}
		break;
	case ANALOG_SIG_FXOLS:
	case ANALOG_SIG_FXOGS:
	case ANALOG_SIG_FXOKS:
		/* Read the first digit */
		timeout = analog_firstdigittimeout;
		/* If starting a threeway call, never timeout on the first digit so someone
		   can use flash-hook as a "hold" feature */
		if (p->subs[ANALOG_SUB_THREEWAY].owner) {
			timeout = 999999;
		}
		while (len < AST_MAX_EXTENSION-1) {
			/* Read digit unless it's supposed to be immediate, in which case the
			   only answer is 's' */
			if (p->immediate) {
				res = 's';
			} else {
				res = ast_waitfordigit(chan, timeout);
			}
			timeout = 0;
			if (res < 0) {
				ast_debug(1, "waitfordigit returned < 0...\n");
				res = analog_play_tone(p, idx, -1);
				ast_hangup(chan);
				goto quit;
			} else if (res) {
				ast_debug(1,"waitfordigit returned '%c' (%d), timeout = %d\n", res, res, timeout);
				exten[len++]=res;
				exten[len] = '\0';
			}
			if (!ast_ignore_pattern(ast_channel_context(chan), exten)) {
				analog_play_tone(p, idx, -1);
			} else {
				analog_play_tone(p, idx, ANALOG_TONE_DIALTONE);
			}
			if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num) && !ast_parking_ext_valid(exten, chan, ast_channel_context(chan))) {
				if (!res || !ast_matchmore_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num)) {
					if (getforward) {
						/* Record this as the forwarding extension */
						ast_copy_string(p->call_forward, exten, sizeof(p->call_forward));
						ast_verb(3, "Setting call forward to '%s' on channel %d\n", p->call_forward, p->channel);
						res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
						if (res) {
							break;
						}
						usleep(500000);
						res = analog_play_tone(p, idx, -1);
						sleep(1);
						memset(exten, 0, sizeof(exten));
						res = analog_play_tone(p, idx, ANALOG_TONE_DIALTONE);
						len = 0;
						getforward = 0;
					} else {
						res = analog_play_tone(p, idx, -1);
						ast_channel_exten_set(chan, exten);
						if (!ast_strlen_zero(p->cid_num)) {
							if (!p->hidecallerid) {
								ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
							} else {
								ast_set_callerid(chan, NULL, NULL, p->cid_num);
							}
						}
						if (!ast_strlen_zero(p->cid_name)) {
							if (!p->hidecallerid) {
								ast_set_callerid(chan, NULL, p->cid_name, NULL);
							}
						}
						ast_setstate(chan, AST_STATE_RING);
						analog_set_echocanceller(p, 1);
						res = ast_pbx_run(chan);
						if (res) {
							ast_log(LOG_WARNING, "PBX exited non-zero\n");
							res = analog_play_tone(p, idx, ANALOG_TONE_CONGESTION);
						}
						goto quit;
					}
				} else {
					/* It's a match, but they just typed a digit, and there is an ambiguous match,
					   so just set the timeout to analog_matchdigittimeout and wait some more */
					timeout = analog_matchdigittimeout;
				}
			} else if (res == 0) {
				ast_debug(1, "not enough digits (and no ambiguous match)...\n");
				res = analog_play_tone(p, idx, ANALOG_TONE_CONGESTION);
				analog_wait_event(p);
				ast_hangup(chan);
				goto quit;
			} else if (p->callwaiting && !strcmp(exten, "*70")) {
				ast_verb(3, "Disabling call waiting on %s\n", ast_channel_name(chan));
				/* Disable call waiting if enabled */
				analog_set_callwaiting(p, 0);
				res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						ast_channel_name(chan), strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = analog_firstdigittimeout;

			} else if (!strcmp(exten,ast_pickup_ext())) {
				/* Scan all channels and see if there are any
				 * ringing channels that have call groups
				 * that equal this channels pickup group
				 */
				if (idx == ANALOG_SUB_REAL) {
					/* Switch us from Third call to Call Wait */
					if (p->subs[ANALOG_SUB_THREEWAY].owner) {
						/* If you make a threeway call and the *8# a call, it should actually
						   look like a callwait */
						analog_alloc_sub(p, ANALOG_SUB_CALLWAIT);
						analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_THREEWAY);
						analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
					}
					analog_set_echocanceller(p, 1);
					if (ast_pickup_call(chan)) {
						ast_debug(1, "No call pickup possible...\n");
						res = analog_play_tone(p, idx, ANALOG_TONE_CONGESTION);
						analog_wait_event(p);
					}
					ast_hangup(chan);
					goto quit;
				} else {
					ast_log(LOG_WARNING, "Huh?  Got *8# on call not on real\n");
					ast_hangup(chan);
					goto quit;
				}

			} else if (!p->hidecallerid && !strcmp(exten, "*67")) {
				ast_verb(3, "Disabling Caller*ID on %s\n", ast_channel_name(chan));
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				ast_party_number_free(&ast_channel_caller(chan)->id.number);
				ast_party_number_init(&ast_channel_caller(chan)->id.number);
				ast_party_name_free(&ast_channel_caller(chan)->id.name);
				ast_party_name_init(&ast_channel_caller(chan)->id.name);
				res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						ast_channel_name(chan), strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = analog_firstdigittimeout;
			} else if (p->callreturn && !strcmp(exten, "*69")) {
				res = 0;
				if (!ast_strlen_zero(p->lastcid_num)) {
					res = ast_say_digit_str(chan, p->lastcid_num, "", ast_channel_language(chan));
				}
				if (!res) {
					res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				}
				break;
			} else if (!strcmp(exten, "*78")) {
				/* Do not disturb enabled */
				analog_dnd(p, 1);
				res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (!strcmp(exten, "*79")) {
				/* Do not disturb disabled */
				analog_dnd(p, 0);
				res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*72")) {
				res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				getforward = 1;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*73")) {
				ast_verb(3, "Cancelling call forwarding on channel %d\n", p->channel);
				res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				memset(p->call_forward, 0, sizeof(p->call_forward));
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if ((p->transfer || p->canpark) && ast_parking_ext_valid(exten, chan, ast_channel_context(chan)) &&
						p->subs[ANALOG_SUB_THREEWAY].owner &&
						ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)) {
				/* This is a three way call, the main call being a real channel,
					and we're parking the first call. */
				ast_masq_park_call_exten(
					ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner), chan, exten,
					ast_channel_context(chan), 0, NULL);
				ast_verb(3, "Parking call to '%s'\n", ast_channel_name(chan));
				break;
			} else if (!ast_strlen_zero(p->lastcid_num) && !strcmp(exten, "*60")) {
				ast_verb(3, "Blacklisting number %s\n", p->lastcid_num);
				res = ast_db_put("blacklist", p->lastcid_num, "1");
				if (!res) {
					res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
					memset(exten, 0, sizeof(exten));
					len = 0;
				}
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				ast_verb(3, "Enabling Caller*ID on %s\n", ast_channel_name(chan));
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				ast_set_callerid(chan, p->cid_num, p->cid_name, NULL);
				res = analog_play_tone(p, idx, ANALOG_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						ast_channel_name(chan), strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = analog_firstdigittimeout;
			} else if (!strcmp(exten, "*0")) {
				struct ast_channel *nbridge = p->subs[ANALOG_SUB_THREEWAY].owner;
				struct analog_pvt *pbridge = NULL;
				/* set up the private struct of the bridged one, if any */
				if (nbridge) {
					pbridge = analog_get_bridged_channel(nbridge);
				}
				if (pbridge && ISTRUNK(pbridge)) {
					/* Clear out the dial buffer */
					p->dop.dialstr[0] = '\0';
					/* flash hookswitch */
					if ((analog_flash(pbridge) == -1) && (errno != EINPROGRESS)) {
						ast_log(LOG_WARNING,
							"Unable to flash-hook bridged trunk from channel %s: %s\n",
							ast_channel_name(nbridge), strerror(errno));
					}
					analog_swap_subs(p, ANALOG_SUB_REAL, ANALOG_SUB_THREEWAY);
					analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
					analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
					if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner)) {
						ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
					}
					ast_hangup(chan);
					goto quit;
				} else {
					analog_play_tone(p, idx, ANALOG_TONE_CONGESTION);
					analog_wait_event(p);
					analog_play_tone(p, idx, -1);
					analog_swap_subs(p, ANALOG_SUB_REAL, ANALOG_SUB_THREEWAY);
					analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
					analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
					ast_hangup(chan);
					goto quit;
				}
			} else if (!ast_canmatch_extension(chan, ast_channel_context(chan), exten, 1,
				ast_channel_caller(chan)->id.number.valid ? ast_channel_caller(chan)->id.number.str : NULL)
				&& !analog_canmatch_featurecode(exten)) {
				ast_debug(1, "Can't match %s from '%s' in context %s\n", exten,
					ast_channel_caller(chan)->id.number.valid && ast_channel_caller(chan)->id.number.str
						? ast_channel_caller(chan)->id.number.str : "<Unknown Caller>",
					ast_channel_context(chan));
				break;
			}
			if (!timeout) {
				timeout = analog_gendigittimeout;
			}
			if (len && !ast_ignore_pattern(ast_channel_context(chan), exten)) {
				analog_play_tone(p, idx, -1);
			}
		}
		break;
	case ANALOG_SIG_FXSLS:
	case ANALOG_SIG_FXSGS:
	case ANALOG_SIG_FXSKS:
		/* check for SMDI messages */
		if (p->use_smdi && p->smdi_iface) {
			smdi_msg = ast_smdi_md_message_wait(p->smdi_iface, ANALOG_SMDI_MD_WAIT_TIMEOUT);
			if (smdi_msg != NULL) {
				ast_channel_exten_set(chan, smdi_msg->fwd_st);

				if (smdi_msg->type == 'B')
					pbx_builtin_setvar_helper(chan, "_SMDI_VM_TYPE", "b");
				else if (smdi_msg->type == 'N')
					pbx_builtin_setvar_helper(chan, "_SMDI_VM_TYPE", "u");

				ast_debug(1, "Received SMDI message on %s\n", ast_channel_name(chan));
			} else {
				ast_log(LOG_WARNING, "SMDI enabled but no SMDI message present\n");
			}
		}

		if (p->use_callerid && (p->cid_signalling == CID_SIG_SMDI && smdi_msg)) {
			number = smdi_msg->calling_st;

		/* If we want caller id, we're in a prering state due to a polarity reversal
		 * and we're set to use a polarity reversal to trigger the start of caller id,
		 * grab the caller id and wait for ringing to start... */
		} else if (p->use_callerid && (ast_channel_state(chan) == AST_STATE_PRERING
			&& (p->cid_start == ANALOG_CID_START_POLARITY
				|| p->cid_start == ANALOG_CID_START_POLARITY_IN
				|| p->cid_start == ANALOG_CID_START_DTMF_NOALERT))) {
			/* If set to use DTMF CID signalling, listen for DTMF */
			if (p->cid_signalling == CID_SIG_DTMF) {
				int k = 0;
				int oldlinearity; 
				int timeout_ms;
				int ms;
				struct timeval start = ast_tvnow();
				ast_debug(1, "Receiving DTMF cid on channel %s\n", ast_channel_name(chan));

				oldlinearity = analog_set_linear_mode(p, idx, 0);

				/*
				 * We are the only party interested in the Rx stream since
				 * we have not answered yet.  We don't need or even want DTMF
				 * emulation.  The DTMF digits can come so fast that emulation
				 * can drop some of them.
				 */
				ast_set_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
				timeout_ms = 4000;/* This is a typical OFF time between rings. */
				for (;;) {
					struct ast_frame *f;

					ms = ast_remaining_ms(start, timeout_ms);
					res = ast_waitfor(chan, ms);
					if (res <= 0) {
						/*
						 * We do not need to restore the analog_set_linear_mode()
						 * or AST_FLAG_END_DTMF_ONLY flag settings since we
						 * are hanging up the channel.
						 */
						ast_log(LOG_WARNING,
							"DTMFCID timed out waiting for ring. Exiting simple switch\n");
						ast_hangup(chan);
						goto quit;
					}
					f = ast_read(chan);
					if (!f) {
						break;
					}
					if (f->frametype == AST_FRAME_DTMF) {
						if (k < ARRAY_LEN(dtmfbuf) - 1) {
							dtmfbuf[k++] = f->subclass.integer;
						}
						ast_debug(1, "CID got digit '%c'\n", f->subclass.integer);
						start = ast_tvnow();
					}
					ast_frfree(f);
					if (ast_channel_state(chan) == AST_STATE_RING ||
						ast_channel_state(chan) == AST_STATE_RINGING) {
						break; /* Got ring */
					}
				}
				ast_clear_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
				dtmfbuf[k] = '\0';

				analog_set_linear_mode(p, idx, oldlinearity);

				/* Got cid and ring. */
				ast_debug(1, "CID got string '%s'\n", dtmfbuf);
				callerid_get_dtmf(dtmfbuf, dtmfcid, &flags);
				ast_debug(1, "CID is '%s', flags %d\n", dtmfcid, flags);
				/* If first byte is NULL, we have no cid */
				if (!ast_strlen_zero(dtmfcid)) {
					number = dtmfcid;
				} else {
					number = NULL;
				}

			/* If set to use V23 Signalling, launch our FSK gubbins and listen for it */
			} else if ((p->cid_signalling == CID_SIG_V23) || (p->cid_signalling == CID_SIG_V23_JP)) {
				int timeout = 10000;  /* Ten seconds */
				struct timeval start = ast_tvnow();
				enum analog_event ev;

				namebuf[0] = 0;
				numbuf[0] = 0;

				if (!analog_start_cid_detect(p, p->cid_signalling)) {
					int off_ms;
					int ms;
					struct timeval off_start;
					while (1) {
						res = analog_get_callerid(p, namebuf, numbuf, &ev, timeout - ast_tvdiff_ms(ast_tvnow(), start));

						if (res == 0) {
							break;
						}

						if (res == 1) {
							if (ev == ANALOG_EVENT_NOALARM) {
								analog_set_alarm(p, 0);
							}
							if (p->cid_signalling == CID_SIG_V23_JP) {
								if (ev == ANALOG_EVENT_RINGBEGIN) {
									analog_off_hook(p);
									usleep(1);
								}
							} else {
								ev = ANALOG_EVENT_NONE;
								break;
							}
						}

						if (ast_tvdiff_ms(ast_tvnow(), start) > timeout)
							break;

					}
					name = namebuf;
					number = numbuf;

					analog_stop_cid_detect(p);

					if (p->cid_signalling == CID_SIG_V23_JP) {
						res = analog_on_hook(p);
						usleep(1);
					}

					/* Finished with Caller*ID, now wait for a ring to make sure there really is a call coming */
					off_start = ast_tvnow();
					off_ms = 4000;/* This is a typical OFF time between rings. */
					while ((ms = ast_remaining_ms(off_start, off_ms))) {
						struct ast_frame *f;

						res = ast_waitfor(chan, ms);
						if (res <= 0) {
							ast_log(LOG_WARNING,
								"CID timed out waiting for ring. Exiting simple switch\n");
							ast_hangup(chan);
							goto quit;
						}
						if (!(f = ast_read(chan))) {
							ast_log(LOG_WARNING, "Hangup received waiting for ring. Exiting simple switch\n");
							ast_hangup(chan);
							goto quit;
						}
						ast_frfree(f);
						if (ast_channel_state(chan) == AST_STATE_RING ||
							ast_channel_state(chan) == AST_STATE_RINGING)
							break; /* Got ring */
					}

					if (analog_distinctive_ring(chan, p, idx, NULL)) {
						goto quit;
					}

					if (res < 0) {
						ast_log(LOG_WARNING, "CallerID returned with error on channel '%s'\n", ast_channel_name(chan));
					}
				} else {
					ast_log(LOG_WARNING, "Unable to get caller ID space\n");
				}
			} else {
				ast_log(LOG_WARNING,
					"Channel %s in prering state, but I have nothing to do. Terminating simple switch, should be restarted by the actual ring.\n",
					ast_channel_name(chan));
				ast_hangup(chan);
				goto quit;
			}
		} else if (p->use_callerid && p->cid_start == ANALOG_CID_START_RING) {
			int timeout = 10000;  /* Ten seconds */
			struct timeval start = ast_tvnow();
			enum analog_event ev;
			int curRingData[RING_PATTERNS] = { 0 };
			int receivedRingT = 0;

			namebuf[0] = 0;
			numbuf[0] = 0;

			if (!analog_start_cid_detect(p, p->cid_signalling)) {
				while (1) {
					res = analog_get_callerid(p, namebuf, numbuf, &ev, timeout - ast_tvdiff_ms(ast_tvnow(), start));

					if (res == 0) {
						break;
					}

					if (res == 1 || res == 2) {
						if (ev == ANALOG_EVENT_NOALARM) {
							analog_set_alarm(p, 0);
						} else if (ev == ANALOG_EVENT_POLARITY && p->hanguponpolarityswitch && p->polarity == POLARITY_REV) {
							ast_debug(1, "Hanging up due to polarity reversal on channel %d while detecting callerid\n", p->channel);
							p->polarity = POLARITY_IDLE;
							ast_hangup(chan);
							goto quit;
						} else if (ev != ANALOG_EVENT_NONE && ev != ANALOG_EVENT_RINGBEGIN && ev != ANALOG_EVENT_RINGOFFHOOK) {
							break;
						}
						if (res != 2) {
							/* Let us detect callerid when the telco uses distinctive ring */
							curRingData[receivedRingT] = p->ringt;

							if (p->ringt < p->ringt_base/2) {
								break;
							}
							/* Increment the ringT counter so we can match it against
							   values in chan_dahdi.conf for distinctive ring */
							if (++receivedRingT == RING_PATTERNS) {
								break;
							}
						}
					}

					if (ast_tvdiff_ms(ast_tvnow(), start) > timeout) {
						break;
					}

				}
				name = namebuf;
				number = numbuf;

				analog_stop_cid_detect(p);

				if (analog_distinctive_ring(chan, p, idx, curRingData)) {
					goto quit;
				}

				if (res < 0) {
					ast_log(LOG_WARNING, "CallerID returned with error on channel '%s'\n", ast_channel_name(chan));
				}
			} else {
				ast_log(LOG_WARNING, "Unable to get caller ID space\n");
			}
		}

		if (number) {
			ast_shrink_phone_number(number);
		}
		ast_set_callerid(chan, number, name, number);

		analog_handle_notify_message(chan, p, flags, -1);

		ast_setstate(chan, AST_STATE_RING);
		ast_channel_rings_set(chan, 1);
		analog_set_ringtimeout(p, p->ringt_base);
		res = ast_pbx_run(chan);
		if (res) {
			ast_hangup(chan);
			ast_log(LOG_WARNING, "PBX exited non-zero\n");
		}
		goto quit;
	default:
		ast_log(LOG_WARNING, "Don't know how to handle simple switch with signalling %s on channel %d\n", analog_sigtype_to_str(p->sig), p->channel);
		break;
	}
	res = analog_play_tone(p, idx, ANALOG_TONE_CONGESTION);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	ast_hangup(chan);
quit:
	if (smdi_msg) {
		ASTOBJ_UNREF(smdi_msg, ast_smdi_md_message_destroy);
	}
	analog_decrease_ss_count();
	return NULL;
}

int analog_ss_thread_start(struct analog_pvt *p, struct ast_channel *chan)
{
	pthread_t threadid;

	return ast_pthread_create_detached(&threadid, NULL, __analog_ss_thread, p);
}

static struct ast_frame *__analog_handle_event(struct analog_pvt *p, struct ast_channel *ast)
{
	int res, x;
	int mysig;
	enum analog_sub idx;
	char *c;
	pthread_t threadid;
	struct ast_channel *chan;
	struct ast_frame *f;
	struct ast_control_pvt_cause_code *cause_code = NULL;
	int data_size = sizeof(*cause_code);
	char *subclass = NULL;

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);

	idx = analog_get_index(ast, p, 0);
	if (idx < 0) {
		return &ast_null_frame;
	}
	if (idx != ANALOG_SUB_REAL) {
		ast_log(LOG_ERROR, "We got an event on a non real sub.  Fix it!\n");
	}

	mysig = p->sig;
	if (p->outsigmod > -1) {
		mysig = p->outsigmod;
	}

	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.subclass.integer = 0;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.src = "dahdi_handle_event";
	p->subs[idx].f.data.ptr = NULL;
	f = &p->subs[idx].f;

	res = analog_get_event(p);

	ast_debug(1, "Got event %s(%d) on channel %d (index %u)\n", analog_event2str(res), res, p->channel, idx);

	if (res & (ANALOG_EVENT_PULSEDIGIT | ANALOG_EVENT_DTMFUP)) {
		analog_set_pulsedial(p, (res & ANALOG_EVENT_PULSEDIGIT) ? 1 : 0);
		ast_debug(1, "Detected %sdigit '%c'\n", (res & ANALOG_EVENT_PULSEDIGIT) ? "pulse ": "", res & 0xff);
		analog_confmute(p, 0);
		p->subs[idx].f.frametype = AST_FRAME_DTMF_END;
		p->subs[idx].f.subclass.integer = res & 0xff;
		analog_handle_dtmf(p, ast, idx, &f);
		return f;
	}

	if (res & ANALOG_EVENT_DTMFDOWN) {
		ast_debug(1, "DTMF Down '%c'\n", res & 0xff);
		/* Mute conference */
		analog_confmute(p, 1);
		p->subs[idx].f.frametype = AST_FRAME_DTMF_BEGIN;
		p->subs[idx].f.subclass.integer = res & 0xff;
		analog_handle_dtmf(p, ast, idx, &f);
		return f;
	}

	switch (res) {
	case ANALOG_EVENT_ALARM:
	case ANALOG_EVENT_POLARITY:
	case ANALOG_EVENT_ONHOOK:
		/* add length of "ANALOG " */
		data_size += 7;
		subclass = analog_event2str(res);
		data_size += strlen(subclass);
		cause_code = ast_alloca(data_size);
		memset(cause_code, 0, data_size);
		cause_code->ast_cause = AST_CAUSE_NORMAL_CLEARING;
		ast_copy_string(cause_code->chan_name, ast_channel_name(ast), AST_CHANNEL_NAME);
		snprintf(cause_code->code, data_size - sizeof(*cause_code) + 1, "ANALOG %s", subclass);
		break;
	default:
		break;
	}

	switch (res) {
	case ANALOG_EVENT_EC_DISABLED:
		ast_verb(3, "Channel %d echo canceler disabled due to CED detection\n", p->channel);
		analog_set_echocanceller(p, 0);
		break;
#ifdef HAVE_DAHDI_ECHOCANCEL_FAX_MODE
	case ANALOG_EVENT_TX_CED_DETECTED:
		ast_verb(3, "Channel %d detected a CED tone towards the network.\n", p->channel);
		break;
	case ANALOG_EVENT_RX_CED_DETECTED:
		ast_verb(3, "Channel %d detected a CED tone from the network.\n", p->channel);
		break;
	case ANALOG_EVENT_EC_NLP_DISABLED:
		ast_verb(3, "Channel %d echo canceler disabled its NLP.\n", p->channel);
		break;
	case ANALOG_EVENT_EC_NLP_ENABLED:
		ast_verb(3, "Channel %d echo canceler enabled its NLP.\n", p->channel);
		break;
#endif
	case ANALOG_EVENT_PULSE_START:
		/* Stop tone if there's a pulse start and the PBX isn't started */
		if (!ast_channel_pbx(ast))
			analog_play_tone(p, ANALOG_SUB_REAL, -1);
		break;
	case ANALOG_EVENT_DIALCOMPLETE:
		if (p->inalarm) {
			break;
		}
		x = analog_is_dialing(p, idx);
		if (!x) { /* if not still dialing in driver */
			analog_set_echocanceller(p, 1);
			if (p->echobreak) {
				analog_train_echocanceller(p);
				ast_copy_string(p->dop.dialstr, p->echorest, sizeof(p->dop.dialstr));
				p->dop.op = ANALOG_DIAL_OP_REPLACE;
				if (analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop)) {
					int dial_err = errno;
					ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(dial_err));
				}
				p->echobreak = 0;
			} else {
				analog_set_dialing(p, 0);
				if ((mysig == ANALOG_SIG_E911) || (mysig == ANALOG_SIG_FGC_CAMA) || (mysig == ANALOG_SIG_FGC_CAMAMF)) {
					/* if thru with dialing after offhook */
					if (ast_channel_state(ast) == AST_STATE_DIALING_OFFHOOK) {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[idx].f.frametype = AST_FRAME_CONTROL;
						p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
						break;
					} else { /* if to state wait for offhook to dial rest */
						/* we now wait for off hook */
						ast_setstate(ast,AST_STATE_DIALING_OFFHOOK);
					}
				}
				if (ast_channel_state(ast) == AST_STATE_DIALING) {
					if (analog_have_progressdetect(p)) {
						ast_debug(1, "Done dialing, but waiting for progress detection before doing more...\n");
					} else if (analog_check_confirmanswer(p) || (!p->dialednone
						&& ((mysig == ANALOG_SIG_EM) || (mysig == ANALOG_SIG_EM_E1)
							|| (mysig == ANALOG_SIG_EMWINK) || (mysig == ANALOG_SIG_FEATD)
							|| (mysig == ANALOG_SIG_FEATDMF_TA) || (mysig == ANALOG_SIG_FEATDMF)
							|| (mysig == ANALOG_SIG_E911) || (mysig == ANALOG_SIG_FGC_CAMA)
							|| (mysig == ANALOG_SIG_FGC_CAMAMF) || (mysig == ANALOG_SIG_FEATB)
							|| (mysig == ANALOG_SIG_SF) || (mysig == ANALOG_SIG_SFWINK)
							|| (mysig == ANALOG_SIG_SF_FEATD) || (mysig == ANALOG_SIG_SF_FEATDMF)
							|| (mysig == ANALOG_SIG_SF_FEATB)))) {
						ast_setstate(ast, AST_STATE_RINGING);
					} else if (!p->answeronpolarityswitch) {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[idx].f.frametype = AST_FRAME_CONTROL;
						p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
						/* If aops=0 and hops=1, this is necessary */
						p->polarity = POLARITY_REV;
					} else {
						/* Start clean, so we can catch the change to REV polarity when party answers */
						p->polarity = POLARITY_IDLE;
					}
				}
			}
		}
		break;
	case ANALOG_EVENT_ALARM:
		analog_set_alarm(p, 1);
		analog_get_and_handle_alarms(p);
		cause_code->ast_cause = AST_CAUSE_NETWORK_OUT_OF_ORDER;
	case ANALOG_EVENT_ONHOOK:
		ast_queue_control_data(ast, AST_CONTROL_PVT_CAUSE_CODE, cause_code, data_size);
		ast_channel_hangupcause_hash_set(ast, cause_code, data_size);
		switch (p->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
			analog_start_polarityswitch(p);
			p->fxsoffhookstate = 0;
			p->onhooktime = time(NULL);
			p->msgstate = -1;
			/* Check for some special conditions regarding call waiting */
			if (idx == ANALOG_SUB_REAL) {
				/* The normal line was hung up */
				if (p->subs[ANALOG_SUB_CALLWAIT].owner) {
					/* Need to hold the lock for real-call, private, and call-waiting call */
					analog_lock_sub_owner(p, ANALOG_SUB_CALLWAIT);
					if (!p->subs[ANALOG_SUB_CALLWAIT].owner) {
						/*
						 * The call waiting call dissappeared.
						 * This is now a normal hangup.
						 */
						analog_set_echocanceller(p, 0);
						return NULL;
					}

					/* There's a call waiting call, so ring the phone, but make it unowned in the mean time */
					analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_REAL);
					ast_verb(3, "Channel %d still has (callwait) call, ringing phone\n", p->channel);
					analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
					analog_stop_callwait(p);
					analog_set_new_owner(p, NULL);
					/* Don't start streaming audio yet if the incoming call isn't up yet */
					if (ast_channel_state(p->subs[ANALOG_SUB_REAL].owner) != AST_STATE_UP) {
						analog_set_dialing(p, 1);
					}
					/* Unlock the call-waiting call that we swapped to real-call. */
					ast_channel_unlock(p->subs[ANALOG_SUB_REAL].owner);
					analog_ring(p);
				} else if (p->subs[ANALOG_SUB_THREEWAY].owner) {
					unsigned int mssinceflash;

					/* Need to hold the lock for real-call, private, and 3-way call */
					analog_lock_sub_owner(p, ANALOG_SUB_THREEWAY);
					if (!p->subs[ANALOG_SUB_THREEWAY].owner) {
						ast_log(LOG_NOTICE, "Whoa, threeway disappeared kinda randomly.\n");
						/* Just hangup */
						return NULL;
					}
					if (p->owner != ast) {
						ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
						ast_log(LOG_WARNING, "This isn't good...\n");
						/* Just hangup */
						return NULL;
					}

					mssinceflash = ast_tvdiff_ms(ast_tvnow(), p->flashtime);
					ast_debug(1, "Last flash was %u ms ago\n", mssinceflash);
					if (mssinceflash < MIN_MS_SINCE_FLASH) {
						/* It hasn't been long enough since the last flashook.  This is probably a bounce on
						   hanging up.  Hangup both channels now */
						ast_debug(1, "Looks like a bounced flash, hanging up both calls on %d\n", p->channel);
						ast_queue_hangup_with_cause(p->subs[ANALOG_SUB_THREEWAY].owner, AST_CAUSE_NO_ANSWER);
						ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
						ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
					} else if ((ast_channel_pbx(ast)) || (ast_channel_state(ast) == AST_STATE_UP)) {
						if (p->transfer) {
							int inthreeway;

							inthreeway = p->subs[ANALOG_SUB_THREEWAY].inthreeway;

							/* In any case this isn't a threeway call anymore */
							analog_set_inthreeway(p, ANALOG_SUB_REAL, 0);
							analog_set_inthreeway(p, ANALOG_SUB_THREEWAY, 0);

							/* Only attempt transfer if the phone is ringing; why transfer to busy tone eh? */
							if (!p->transfertobusy && ast_channel_state(ast) == AST_STATE_BUSY) {
								/* Swap subs and dis-own channel */
								analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
								/* Unlock the 3-way call that we swapped to real-call. */
								ast_channel_unlock(p->subs[ANALOG_SUB_REAL].owner);
								analog_set_new_owner(p, NULL);
								/* Ring the phone */
								analog_ring(p);
							} else if (!analog_attempt_transfer(p, inthreeway)) {
								/*
								 * Transfer successful.  Don't actually hang up at this point.
								 * Let our channel legs of the calls die off as the transfer
								 * percolates through the core.
								 */
								break;
							}
						} else {
							ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
							ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
						}
					} else {
						/* Swap subs and dis-own channel */
						analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
						/* Unlock the 3-way call that we swapped to real-call. */
						ast_channel_unlock(p->subs[ANALOG_SUB_REAL].owner);
						analog_set_new_owner(p, NULL);
						/* Ring the phone */
						analog_ring(p);
					}
				}
			} else {
				ast_log(LOG_WARNING, "Got a hangup and my index is %u?\n", idx);
			}
			/* Fall through */
		default:
			analog_set_echocanceller(p, 0);
			return NULL;
		}
		break;
	case ANALOG_EVENT_RINGOFFHOOK:
		if (p->inalarm) {
			break;
		}
		/* for E911, its supposed to wait for offhook then dial
		   the second half of the dial string */
		if (((mysig == ANALOG_SIG_E911) || (mysig == ANALOG_SIG_FGC_CAMA) || (mysig == ANALOG_SIG_FGC_CAMAMF)) && (ast_channel_state(ast) == AST_STATE_DIALING_OFFHOOK)) {
			c = strchr(p->dialdest, '/');
			if (c) {
				c++;
			} else {
				c = p->dialdest;
			}
			if (*c) {
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*0%s#", c);
			} else {
				ast_copy_string(p->dop.dialstr,"M*2#", sizeof(p->dop.dialstr));
			}
			if (strlen(p->dop.dialstr) > 4) {
				memset(p->echorest, 'w', sizeof(p->echorest) - 1);
				strcpy(p->echorest + (p->echotraining / 401) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
				p->echorest[sizeof(p->echorest) - 1] = '\0';
				p->echobreak = 1;
				p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
			} else {
				p->echobreak = 0;
			}
			if (analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop)) {
				int saveerr = errno;
				analog_on_hook(p);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(saveerr));
				return NULL;
			}
			analog_set_dialing(p, 1);
			return &p->subs[idx].f;
		}
		switch (p->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
			p->fxsoffhookstate = 1;
			switch (ast_channel_state(ast)) {
			case AST_STATE_RINGING:
				analog_set_echocanceller(p, 1);
				analog_train_echocanceller(p);
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
				/* Make sure it stops ringing */
				analog_set_needringing(p, 0);
				analog_off_hook(p);
				ast_debug(1, "channel %d answered\n", p->channel);

				/* Cancel any running CallerID spill */
				analog_cancel_cidspill(p);

				analog_set_dialing(p, 0);
				p->callwaitcas = 0;
				if (analog_check_confirmanswer(p)) {
					/* Ignore answer if "confirm answer" is enabled */
					p->subs[idx].f.frametype = AST_FRAME_NULL;
					p->subs[idx].f.subclass.integer = 0;
				} else if (!ast_strlen_zero(p->dop.dialstr)) {
					/* nick@dccinc.com 4/3/03 - fxo should be able to do deferred dialing */
					res = analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop);
					if (res < 0) {
						ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
						p->dop.dialstr[0] = '\0';
						return NULL;
					} else {
						ast_debug(1, "Sent FXO deferred digit string: %s\n", p->dop.dialstr);
						p->subs[idx].f.frametype = AST_FRAME_NULL;
						p->subs[idx].f.subclass.integer = 0;
						analog_set_dialing(p, 1);
					}
					p->dop.dialstr[0] = '\0';
					ast_setstate(ast, AST_STATE_DIALING);
				} else {
					ast_setstate(ast, AST_STATE_UP);
					analog_answer_polarityswitch(p);
				}
				return &p->subs[idx].f;
			case AST_STATE_DOWN:
				ast_setstate(ast, AST_STATE_RING);
				ast_channel_rings_set(ast, 1);
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass.integer = AST_CONTROL_OFFHOOK;
				ast_debug(1, "channel %d picked up\n", p->channel);
				return &p->subs[idx].f;
			case AST_STATE_UP:
				/* Make sure it stops ringing */
				analog_off_hook(p);
				/* Okay -- probably call waiting*/
				if (ast_bridged_channel(p->owner)) {
					ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
				}
				break;
			case AST_STATE_RESERVED:
				/* Start up dialtone */
				if (analog_has_voicemail(p)) {
					res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_STUTTER);
				} else {
					res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_DIALTONE);
				}
				break;
			default:
				ast_log(LOG_WARNING, "FXO phone off hook in weird state %u??\n", ast_channel_state(ast));
			}
			break;
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
			if (ast_channel_state(ast) == AST_STATE_RING) {
				analog_set_ringtimeout(p, p->ringt_base);
			}

			/* Fall through */
		case ANALOG_SIG_EM:
		case ANALOG_SIG_EM_E1:
		case ANALOG_SIG_EMWINK:
		case ANALOG_SIG_FEATD:
		case ANALOG_SIG_FEATDMF:
		case ANALOG_SIG_FEATDMF_TA:
		case ANALOG_SIG_E911:
		case ANALOG_SIG_FGC_CAMA:
		case ANALOG_SIG_FGC_CAMAMF:
		case ANALOG_SIG_FEATB:
		case ANALOG_SIG_SF:
		case ANALOG_SIG_SFWINK:
		case ANALOG_SIG_SF_FEATD:
		case ANALOG_SIG_SF_FEATDMF:
		case ANALOG_SIG_SF_FEATB:
			switch (ast_channel_state(ast)) {
			case AST_STATE_PRERING:
				ast_setstate(ast, AST_STATE_RING);
				/* Fall through */
			case AST_STATE_DOWN:
			case AST_STATE_RING:
				ast_debug(1, "Ring detected\n");
				p->subs[idx].f.frametype = AST_FRAME_CONTROL;
				p->subs[idx].f.subclass.integer = AST_CONTROL_RING;
				break;
			case AST_STATE_RINGING:
			case AST_STATE_DIALING:
				if (p->outgoing) {
					ast_debug(1, "Line answered\n");
					if (analog_check_confirmanswer(p)) {
						p->subs[idx].f.frametype = AST_FRAME_NULL;
						p->subs[idx].f.subclass.integer = 0;
					} else {
						p->subs[idx].f.frametype = AST_FRAME_CONTROL;
						p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
						ast_setstate(ast, AST_STATE_UP);
					}
					break;
				}
				/* Fall through */
			default:
				ast_log(LOG_WARNING, "Ring/Off-hook in strange state %u on channel %d\n", ast_channel_state(ast), p->channel);
				break;
			}
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/off hook for signalling %d\n", p->sig);
			break;
		}
		break;
	case ANALOG_EVENT_RINGBEGIN:
		switch (p->sig) {
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
			if (ast_channel_state(ast) == AST_STATE_RING) {
				analog_set_ringtimeout(p, p->ringt_base);
			}
			break;
		default:
			break;
		}
		break;
	case ANALOG_EVENT_RINGEROFF:
		if (p->inalarm) break;
		ast_channel_rings_set(ast, ast_channel_rings(ast) + 1);
		if (ast_channel_rings(ast) == p->cidrings) {
			analog_send_callerid(p, 0, &p->caller);
		}

		if (ast_channel_rings(ast) > p->cidrings) {
			analog_cancel_cidspill(p);
			p->callwaitcas = 0;
		}
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_RINGING;
		break;
	case ANALOG_EVENT_RINGERON:
		break;
	case ANALOG_EVENT_NOALARM:
		analog_set_alarm(p, 0);
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
		/*** DOCUMENTATION
			<managerEventInstance>
				<synopsis>Raised when an Alarm is cleared on an Analog channel.</synopsis>
			</managerEventInstance>
		***/
		manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
			"Channel: %d\r\n", p->channel);
		break;
	case ANALOG_EVENT_WINKFLASH:
		if (p->inalarm) {
			break;
		}
		/* Remember last time we got a flash-hook */
		gettimeofday(&p->flashtime, NULL);
		switch (mysig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
			ast_debug(1, "Winkflash, index: %u, normal: %d, callwait: %d, thirdcall: %d\n",
				idx, analog_get_sub_fd(p, ANALOG_SUB_REAL), analog_get_sub_fd(p, ANALOG_SUB_CALLWAIT), analog_get_sub_fd(p, ANALOG_SUB_THREEWAY));

			/* Cancel any running CallerID spill */
			analog_cancel_cidspill(p);
			p->callwaitcas = 0;

			if (idx != ANALOG_SUB_REAL) {
				ast_log(LOG_WARNING, "Got flash hook with index %u on channel %d?!?\n", idx, p->channel);
				goto winkflashdone;
			}

			if (p->subs[ANALOG_SUB_CALLWAIT].owner) {
				/* Need to hold the lock for real-call, private, and call-waiting call */
				analog_lock_sub_owner(p, ANALOG_SUB_CALLWAIT);
				if (!p->subs[ANALOG_SUB_CALLWAIT].owner) {
					/*
					 * The call waiting call dissappeared.
					 * Let's just ignore this flash-hook.
					 */
					ast_log(LOG_NOTICE, "Whoa, the call-waiting call disappeared.\n");
					goto winkflashdone;
				}

				/* Swap to call-wait */
				analog_swap_subs(p, ANALOG_SUB_REAL, ANALOG_SUB_CALLWAIT);
				analog_play_tone(p, ANALOG_SUB_REAL, -1);
				analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
				ast_debug(1, "Making %s the new owner\n", ast_channel_name(p->owner));
				if (ast_channel_state(p->subs[ANALOG_SUB_REAL].owner) == AST_STATE_RINGING) {
					ast_setstate(p->subs[ANALOG_SUB_REAL].owner, AST_STATE_UP);
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_ANSWER);
				}
				analog_stop_callwait(p);

				/* Start music on hold if appropriate */
				if (!p->subs[ANALOG_SUB_CALLWAIT].inthreeway && ast_bridged_channel(p->subs[ANALOG_SUB_CALLWAIT].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_CALLWAIT].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);

				/* Unlock the call-waiting call that we swapped to real-call. */
				ast_channel_unlock(p->subs[ANALOG_SUB_REAL].owner);
			} else if (!p->subs[ANALOG_SUB_THREEWAY].owner) {
				if (!p->threewaycalling) {
					/* Just send a flash if no 3-way calling */
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_FLASH);
					goto winkflashdone;
				} else if (!analog_check_for_conference(p)) {
					struct ast_callid *callid = NULL;
					int callid_created;
					char cid_num[256];
					char cid_name[256];

					cid_num[0] = '\0';
					cid_name[0] = '\0';
					if (p->dahditrcallerid && p->owner) {
						if (ast_channel_caller(p->owner)->id.number.valid
							&& ast_channel_caller(p->owner)->id.number.str) {
							ast_copy_string(cid_num, ast_channel_caller(p->owner)->id.number.str,
								sizeof(cid_num));
						}
						if (ast_channel_caller(p->owner)->id.name.valid
							&& ast_channel_caller(p->owner)->id.name.str) {
							ast_copy_string(cid_name, ast_channel_caller(p->owner)->id.name.str,
								sizeof(cid_name));
						}
					}
					/* XXX This section needs much more error checking!!! XXX */
					/* Start a 3-way call if feasible */
					if (!((ast_channel_pbx(ast)) ||
						(ast_channel_state(ast) == AST_STATE_UP) ||
						(ast_channel_state(ast) == AST_STATE_RING))) {
						ast_debug(1, "Flash when call not up or ringing\n");
						goto winkflashdone;
					}
					if (analog_alloc_sub(p, ANALOG_SUB_THREEWAY)) {
						ast_log(LOG_WARNING, "Unable to allocate three-way subchannel\n");
						goto winkflashdone;
					}

					callid_created = ast_callid_threadstorage_auto(&callid);

					/*
					 * Make new channel
					 *
					 * We cannot hold the p or ast locks while creating a new
					 * channel.
					 */
					analog_unlock_private(p);
					ast_channel_unlock(ast);
					chan = analog_new_ast_channel(p, AST_STATE_RESERVED, 0, ANALOG_SUB_THREEWAY, NULL);
					ast_channel_lock(ast);
					analog_lock_private(p);
					if (!chan) {
						ast_log(LOG_WARNING,
							"Cannot allocate new call structure on channel %d\n",
							p->channel);
						analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
						ast_callid_threadstorage_auto_clean(callid, callid_created);
						goto winkflashdone;
					}
					if (p->dahditrcallerid) {
						if (!p->origcid_num) {
							p->origcid_num = ast_strdup(p->cid_num);
						}
						if (!p->origcid_name) {
							p->origcid_name = ast_strdup(p->cid_name);
						}
						ast_copy_string(p->cid_num, cid_num, sizeof(p->cid_num));
						ast_copy_string(p->cid_name, cid_name, sizeof(p->cid_name));
					}
					/* Swap things around between the three-way and real call */
					analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
					/* Disable echo canceller for better dialing */
					analog_set_echocanceller(p, 0);
					res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_DIALRECALL);
					if (res) {
						ast_log(LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
					}
					analog_set_new_owner(p, chan);
					p->ss_astchan = chan;
					if (ast_pthread_create_detached(&threadid, NULL, __analog_ss_thread, p)) {
						ast_log(LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
						res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
						analog_set_echocanceller(p, 1);
						ast_hangup(chan);
					} else {
						ast_verb(3, "Started three way call on channel %d\n", p->channel);

						/* Start music on hold if appropriate */
						if (ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)) {
							ast_queue_control_data(p->subs[ANALOG_SUB_THREEWAY].owner, AST_CONTROL_HOLD,
								S_OR(p->mohsuggest, NULL),
								!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
						}
					}
					ast_callid_threadstorage_auto_clean(callid, callid_created);
				}
			} else {
				/* Already have a 3 way call */
				enum analog_sub orig_3way_sub;

				/* Need to hold the lock for real-call, private, and 3-way call */
				analog_lock_sub_owner(p, ANALOG_SUB_THREEWAY);
				if (!p->subs[ANALOG_SUB_THREEWAY].owner) {
					/*
					 * The 3-way call dissappeared.
					 * Let's just ignore this flash-hook.
					 */
					ast_log(LOG_NOTICE, "Whoa, the 3-way call disappeared.\n");
					goto winkflashdone;
				}
				orig_3way_sub = ANALOG_SUB_THREEWAY;

				if (p->subs[ANALOG_SUB_THREEWAY].inthreeway) {
					/* Call is already up, drop the last person */
					ast_debug(1, "Got flash with three way call up, dropping last call on %d\n", p->channel);
					/* If the primary call isn't answered yet, use it */
					if ((ast_channel_state(p->subs[ANALOG_SUB_REAL].owner) != AST_STATE_UP) &&
						(ast_channel_state(p->subs[ANALOG_SUB_THREEWAY].owner) == AST_STATE_UP)) {
						/* Swap back -- we're dropping the real 3-way that isn't finished yet*/
						analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
						orig_3way_sub = ANALOG_SUB_REAL;
						analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
					}
					/* Drop the last call and stop the conference */
					ast_verb(3, "Dropping three-way call on %s\n", ast_channel_name(p->subs[ANALOG_SUB_THREEWAY].owner));
					ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
					analog_set_inthreeway(p, ANALOG_SUB_REAL, 0);
					analog_set_inthreeway(p, ANALOG_SUB_THREEWAY, 0);
				} else {
					/* Lets see what we're up to */
					if (((ast_channel_pbx(ast)) || (ast_channel_state(ast) == AST_STATE_UP)) &&
						(p->transfertobusy || (ast_channel_state(ast) != AST_STATE_BUSY))) {
						ast_verb(3, "Building conference call with %s and %s\n",
							ast_channel_name(p->subs[ANALOG_SUB_THREEWAY].owner),
							ast_channel_name(p->subs[ANALOG_SUB_REAL].owner));
						/* Put them in the threeway, and flip */
						analog_set_inthreeway(p, ANALOG_SUB_THREEWAY, 1);
						analog_set_inthreeway(p, ANALOG_SUB_REAL, 1);
						if (ast_channel_state(ast) == AST_STATE_UP) {
							analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
							orig_3way_sub = ANALOG_SUB_REAL;
						}
						if (ast_bridged_channel(p->subs[orig_3way_sub].owner)) {
							ast_queue_control(p->subs[orig_3way_sub].owner, AST_CONTROL_UNHOLD);
						}
						analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
					} else {
						ast_verb(3, "Dumping incomplete call on %s\n", ast_channel_name(p->subs[ANALOG_SUB_THREEWAY].owner));
						analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
						orig_3way_sub = ANALOG_SUB_REAL;
						ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
						analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
						if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner)) {
							ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
						}
						analog_set_echocanceller(p, 1);
					}
				}
				ast_channel_unlock(p->subs[orig_3way_sub].owner);
			}
winkflashdone:
			analog_update_conf(p);
			break;
		case ANALOG_SIG_EM:
		case ANALOG_SIG_EM_E1:
		case ANALOG_SIG_FEATD:
		case ANALOG_SIG_SF:
		case ANALOG_SIG_SFWINK:
		case ANALOG_SIG_SF_FEATD:
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
			if (p->dialing) {
				ast_debug(1, "Ignoring wink on channel %d\n", p->channel);
			} else {
				ast_debug(1, "Got wink in weird state %u on channel %d\n", ast_channel_state(ast), p->channel);
			}
			break;
		case ANALOG_SIG_FEATDMF_TA:
			switch (p->whichwink) {
			case 0:
				ast_debug(1, "ANI2 set to '%d' and ANI is '%s'\n", ast_channel_caller(p->owner)->ani2,
					S_COR(ast_channel_caller(p->owner)->ani.number.valid,
						ast_channel_caller(p->owner)->ani.number.str, ""));
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%d%s#",
					ast_channel_caller(p->owner)->ani2,
					S_COR(ast_channel_caller(p->owner)->ani.number.valid,
						ast_channel_caller(p->owner)->ani.number.str, ""));
				break;
			case 1:
				ast_copy_string(p->dop.dialstr, p->finaldial, sizeof(p->dop.dialstr));
				break;
			case 2:
				ast_log(LOG_WARNING, "Received unexpected wink on channel of type ANALOG_SIG_FEATDMF_TA\n");
				return NULL;
			}
			p->whichwink++;
			/* Fall through */
		case ANALOG_SIG_FEATDMF:
		case ANALOG_SIG_E911:
		case ANALOG_SIG_FGC_CAMAMF:
		case ANALOG_SIG_FGC_CAMA:
		case ANALOG_SIG_FEATB:
		case ANALOG_SIG_SF_FEATDMF:
		case ANALOG_SIG_SF_FEATB:
		case ANALOG_SIG_EMWINK:
			/* FGD MF and EMWINK *Must* wait for wink */
			if (!ast_strlen_zero(p->dop.dialstr)) {
				res = analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else {
					ast_debug(1, "Sent deferred digit string on channel %d: %s\n", p->channel, p->dop.dialstr);
				}
			}
			p->dop.dialstr[0] = '\0';
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/off hook for signalling %d\n", p->sig);
		}
		break;
	case ANALOG_EVENT_HOOKCOMPLETE:
		if (p->inalarm) break;
		if (analog_check_waitingfordt(p)) {
			break;
		}
		switch (mysig) {
		case ANALOG_SIG_FXSLS:  /* only interesting for FXS */
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
		case ANALOG_SIG_EM:
		case ANALOG_SIG_EM_E1:
		case ANALOG_SIG_EMWINK:
		case ANALOG_SIG_FEATD:
		case ANALOG_SIG_SF:
		case ANALOG_SIG_SFWINK:
		case ANALOG_SIG_SF_FEATD:
			if (!ast_strlen_zero(p->dop.dialstr)) {
				res = analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else {
					ast_debug(1, "Sent deferred digit string on channel %d: %s\n", p->channel, p->dop.dialstr);
				}
			}
			p->dop.dialstr[0] = '\0';
			p->dop.op = ANALOG_DIAL_OP_REPLACE;
			break;
		case ANALOG_SIG_FEATDMF:
		case ANALOG_SIG_FEATDMF_TA:
		case ANALOG_SIG_E911:
		case ANALOG_SIG_FGC_CAMA:
		case ANALOG_SIG_FGC_CAMAMF:
		case ANALOG_SIG_FEATB:
		case ANALOG_SIG_SF_FEATDMF:
		case ANALOG_SIG_SF_FEATB:
			ast_debug(1, "Got hook complete in MF FGD, waiting for wink now on channel %d\n",p->channel);
			break;
		default:
			break;
		}
		break;
	case ANALOG_EVENT_POLARITY:
		/*
		 * If we get a Polarity Switch event, this could be
		 * due to line seizure, remote end connect or remote end disconnect.
		 *
		 * Check to see if we should change the polarity state and
		 * mark the channel as UP or if this is an indication
		 * of remote end disconnect.
		 */

		if (p->polarityonanswerdelay > 0) {
			/* check if event is not too soon after OffHook or Answer */
			if (ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) > p->polarityonanswerdelay) {
				switch (ast_channel_state(ast)) {
				case AST_STATE_DIALING:			/*!< Digits (or equivalent) have been dialed */
				case AST_STATE_RINGING:			/*!< Remote end is ringing */
					if (p->answeronpolarityswitch) {
						ast_debug(1, "Answering on polarity switch! channel %d\n", p->channel);
						ast_setstate(p->owner, AST_STATE_UP);
						p->polarity = POLARITY_REV;
						if (p->hanguponpolarityswitch) {
							p->polaritydelaytv = ast_tvnow();
						}
					} else {
						ast_debug(1, "Ignore Answer on polarity switch, channel %d\n", p->channel);
					}
					break;

				case AST_STATE_UP:				/*!< Line is up */
				case AST_STATE_RING:			/*!< Line is ringing */
					if (p->hanguponpolarityswitch) {
						ast_debug(1, "HangingUp on polarity switch! channel %d\n", p->channel);
						ast_queue_control_data(ast, AST_CONTROL_PVT_CAUSE_CODE, cause_code, data_size);
						ast_channel_hangupcause_hash_set(ast, cause_code, data_size);
						ast_softhangup(p->owner, AST_SOFTHANGUP_EXPLICIT);
						p->polarity = POLARITY_IDLE;
					} else {
						ast_debug(1, "Ignore Hangup on polarity switch, channel %d\n", p->channel);
					}
					break;

				case AST_STATE_DOWN:				/*!< Channel is down and available */
				case AST_STATE_RESERVED:			/*!< Channel is down, but reserved */
				case AST_STATE_OFFHOOK:				/*!< Channel is off hook */
				case AST_STATE_BUSY:				/*!< Line is busy */
				case AST_STATE_DIALING_OFFHOOK:		/*!< Digits (or equivalent) have been dialed while offhook */
				case AST_STATE_PRERING:				/*!< Channel has detected an incoming call and is waiting for ring */
				default:
					if (p->answeronpolarityswitch || p->hanguponpolarityswitch) {
						ast_debug(1, "Ignoring Polarity switch on channel %d, state %u\n", p->channel, ast_channel_state(ast));
					}
					break;
				}

			} else {
				/* event is too soon after OffHook or Answer */
				switch (ast_channel_state(ast)) {
				case AST_STATE_DIALING:		/*!< Digits (or equivalent) have been dialed */
				case AST_STATE_RINGING:		/*!< Remote end is ringing */
					if (p->answeronpolarityswitch) {
						ast_debug(1, "Polarity switch detected but NOT answering (too close to OffHook event) on channel %d, state %u\n", p->channel, ast_channel_state(ast));
					}
					break;

				case AST_STATE_UP:			/*!< Line is up */
				case AST_STATE_RING:		/*!< Line is ringing */
					if (p->hanguponpolarityswitch) {
						ast_debug(1, "Polarity switch detected but NOT hanging up (too close to Answer event) on channel %d, state %u\n", p->channel, ast_channel_state(ast));
					}
					break;

				default:
					if (p->answeronpolarityswitch || p->hanguponpolarityswitch) {
						ast_debug(1, "Polarity switch detected (too close to previous event) on channel %d, state %u\n", p->channel, ast_channel_state(ast));
					}
					break;
				}
			}
		}

		/* Added more log_debug information below to provide a better indication of what is going on */
		ast_debug(1, "Polarity Reversal event occured - DEBUG 2: channel %d, state %u, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %" PRIi64 "\n", p->channel, ast_channel_state(ast), p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
		break;
	default:
		ast_debug(1, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[idx].f;
}

struct ast_frame *analog_exception(struct analog_pvt *p, struct ast_channel *ast)
{
	int res;
	int idx;
	struct ast_frame *f;

	ast_debug(1, "%s %d\n", __FUNCTION__, p->channel);

	idx = analog_get_index(ast, p, 1);
	if (idx < 0) {
		idx = ANALOG_SUB_REAL;
	}

	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.subclass.integer = 0;
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "dahdi_exception";
	p->subs[idx].f.data.ptr = NULL;

	if (!p->owner) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		res = analog_get_event(p);

		/* Switch to real if there is one and this isn't something really silly... */
		if ((res != ANALOG_EVENT_RINGEROFF) && (res != ANALOG_EVENT_RINGERON) &&
			(res != ANALOG_EVENT_HOOKCOMPLETE)) {
			ast_debug(1, "Restoring owner of channel %d on event %d\n", p->channel, res);
			analog_set_new_owner(p, p->subs[ANALOG_SUB_REAL].owner);
			if (p->owner && ast != p->owner) {
				/*
				 * Could this even happen?
				 * Possible deadlock because we do not have the real-call lock.
				 */
				ast_log(LOG_WARNING, "Event %s on %s is not restored owner %s\n",
					analog_event2str(res), ast_channel_name(ast), ast_channel_name(p->owner));
			}
			if (p->owner && ast_bridged_channel(p->owner)) {
				ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
			}
		}
		switch (res) {
		case ANALOG_EVENT_ONHOOK:
			analog_set_echocanceller(p, 0);
			if (p->owner) {
				ast_verb(3, "Channel %s still has call, ringing phone\n", ast_channel_name(p->owner));
				analog_ring(p);
				analog_stop_callwait(p);
			} else {
				ast_log(LOG_WARNING, "Absorbed %s, but nobody is left!?!?\n",
					analog_event2str(res));
			}
			analog_update_conf(p);
			break;
		case ANALOG_EVENT_RINGOFFHOOK:
			analog_set_echocanceller(p, 1);
			analog_off_hook(p);
			if (p->owner && (ast_channel_state(p->owner) == AST_STATE_RINGING)) {
				ast_queue_control(p->owner, AST_CONTROL_ANSWER);
				analog_set_dialing(p, 0);
			}
			break;
		case ANALOG_EVENT_HOOKCOMPLETE:
		case ANALOG_EVENT_RINGERON:
		case ANALOG_EVENT_RINGEROFF:
			/* Do nothing */
			break;
		case ANALOG_EVENT_WINKFLASH:
			gettimeofday(&p->flashtime, NULL);
			if (p->owner) {
				ast_verb(3, "Channel %d flashed to other channel %s\n", p->channel, ast_channel_name(p->owner));
				if (ast_channel_state(p->owner) != AST_STATE_UP) {
					/* Answer if necessary */
					ast_queue_control(p->owner, AST_CONTROL_ANSWER);
					ast_setstate(p->owner, AST_STATE_UP);
				}
				analog_stop_callwait(p);
				if (ast_bridged_channel(p->owner)) {
					ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
				}
			} else {
				ast_log(LOG_WARNING, "Absorbed %s, but nobody is left!?!?\n",
					analog_event2str(res));
			}
			analog_update_conf(p);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", analog_event2str(res));
			break;
		}
		f = &p->subs[idx].f;
		return f;
	}
	ast_debug(1, "Exception on %d, channel %d\n", ast_channel_fd(ast, 0), p->channel);
	/* If it's not us, return NULL immediately */
	if (ast != p->owner) {
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast_channel_name(ast), ast_channel_name(p->owner));
		f = &p->subs[idx].f;
		return f;
	}

	f = __analog_handle_event(p, ast);
	if (!f) {
		const char *name = ast_strdupa(ast_channel_name(ast));

		/* Tell the CDR this DAHDI device hung up */
		analog_unlock_private(p);
		ast_channel_unlock(ast);
		ast_set_hangupsource(ast, name, 0);
		ast_channel_lock(ast);
		analog_lock_private(p);
	}
	return f;
}

void *analog_handle_init_event(struct analog_pvt *i, int event)
{
	int res;
	pthread_t threadid;
	struct ast_channel *chan;
	struct ast_callid *callid = NULL;
	int callid_created;

	ast_debug(1, "channel (%d) - signaling (%d) - event (%s)\n",
				i->channel, i->sig, analog_event2str(event));

	/* Handle an event on a given channel for the monitor thread. */
	switch (event) {
	case ANALOG_EVENT_WINKFLASH:
	case ANALOG_EVENT_RINGOFFHOOK:
		if (i->inalarm) {
			break;
		}
		/* Got a ring/answer.  What kind of channel are we? */
		switch (i->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
			res = analog_off_hook(i);
			i->fxsoffhookstate = 1;
			if (res && (errno == EBUSY)) {
				break;
			}
			callid_created = ast_callid_threadstorage_auto(&callid);

			/* Cancel VMWI spill */
			analog_cancel_cidspill(i);

			if (i->immediate) {
				analog_set_echocanceller(i, 1);
				/* The channel is immediately up.  Start right away */
				res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_RINGTONE);
				chan = analog_new_ast_channel(i, AST_STATE_RING, 1, ANALOG_SUB_REAL, NULL);
				if (!chan) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
					if (res < 0) {
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					}
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = analog_new_ast_channel(i, AST_STATE_RESERVED, 0, ANALOG_SUB_REAL, NULL);
				i->ss_astchan = chan;
				if (chan) {
					if (analog_has_voicemail(i)) {
						res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_STUTTER);
					} else {
						res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_DIALTONE);
					}
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play dialtone on channel %d, do you have defaultzone and loadzone defined?\n", i->channel);

					if (ast_pthread_create_detached(&threadid, NULL, __analog_ss_thread, i)) {
						ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
						res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
						if (res < 0) {
							ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
						}
						ast_hangup(chan);
					}
				} else
					ast_log(LOG_WARNING, "Unable to create channel\n");
			}
			ast_callid_threadstorage_auto_clean(callid, callid_created);
			break;
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
			analog_set_ringtimeout(i, i->ringt_base);
			/* Fall through */
		case ANALOG_SIG_EMWINK:
		case ANALOG_SIG_FEATD:
		case ANALOG_SIG_FEATDMF:
		case ANALOG_SIG_FEATDMF_TA:
		case ANALOG_SIG_E911:
		case ANALOG_SIG_FGC_CAMA:
		case ANALOG_SIG_FGC_CAMAMF:
		case ANALOG_SIG_FEATB:
		case ANALOG_SIG_EM:
		case ANALOG_SIG_EM_E1:
		case ANALOG_SIG_SFWINK:
		case ANALOG_SIG_SF_FEATD:
		case ANALOG_SIG_SF_FEATDMF:
		case ANALOG_SIG_SF_FEATB:
		case ANALOG_SIG_SF:
			callid_created = ast_callid_threadstorage_auto(&callid);
			/* Check for callerid, digits, etc */
			if (i->cid_start == ANALOG_CID_START_POLARITY_IN || i->cid_start == ANALOG_CID_START_DTMF_NOALERT) {
				chan = analog_new_ast_channel(i, AST_STATE_PRERING, 0, ANALOG_SUB_REAL, NULL);
			} else {
				chan = analog_new_ast_channel(i, AST_STATE_RING, 0, ANALOG_SUB_REAL, NULL);
			}
			i->ss_astchan = chan;
			if (!chan) {
				ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
			} else if (ast_pthread_create_detached(&threadid, NULL, __analog_ss_thread, i)) {
				ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
				res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
				ast_hangup(chan);
			}
			ast_callid_threadstorage_auto_clean(callid, callid_created);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/answer with signalling %s on channel %d\n", analog_sigtype_to_str(i->sig), i->channel);
			res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
			}
			return NULL;
		}
		break;
	case ANALOG_EVENT_NOALARM:
		analog_set_alarm(i, 0);
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", i->channel);
		manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
			"Channel: %d\r\n", i->channel);
		break;
	case ANALOG_EVENT_ALARM:
		analog_set_alarm(i, 1);
		analog_get_and_handle_alarms(i);
		/* fall thru intentionally */
	case ANALOG_EVENT_ONHOOK:
		/* Back on hook.  Hang up. */
		switch (i->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
			i->fxsoffhookstate = 0;
			analog_start_polarityswitch(i);
			/* Fall through */
		case ANALOG_SIG_FEATD:
		case ANALOG_SIG_FEATDMF:
		case ANALOG_SIG_FEATDMF_TA:
		case ANALOG_SIG_E911:
		case ANALOG_SIG_FGC_CAMA:
		case ANALOG_SIG_FGC_CAMAMF:
		case ANALOG_SIG_FEATB:
		case ANALOG_SIG_EM:
		case ANALOG_SIG_EM_E1:
		case ANALOG_SIG_EMWINK:
		case ANALOG_SIG_SF_FEATD:
		case ANALOG_SIG_SF_FEATDMF:
		case ANALOG_SIG_SF_FEATB:
		case ANALOG_SIG_SF:
		case ANALOG_SIG_SFWINK:
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
			analog_set_echocanceller(i, 0);
			res = analog_play_tone(i, ANALOG_SUB_REAL, -1);
			analog_on_hook(i);
			break;
		case ANALOG_SIG_FXOKS:
			i->fxsoffhookstate = 0;
			analog_start_polarityswitch(i);
			analog_set_echocanceller(i, 0);
			/* Diddle the battery for the zhone */
#ifdef ZHONE_HACK
			analog_off_hook(i);
			usleep(1);
#endif
			res = analog_play_tone(i, ANALOG_SUB_REAL, -1);
			analog_on_hook(i);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle on hook with signalling %s on channel %d\n", analog_sigtype_to_str(i->sig), i->channel);
			res = analog_play_tone(i, ANALOG_SUB_REAL, -1);
			return NULL;
		}
		break;
	case ANALOG_EVENT_POLARITY:
		switch (i->sig) {
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSKS:
		case ANALOG_SIG_FXSGS:
			callid_created = ast_callid_threadstorage_auto(&callid);
			/* We have already got a PR before the channel was
			   created, but it wasn't handled. We need polarity
			   to be REV for remote hangup detection to work.
			   At least in Spain */
			if (i->hanguponpolarityswitch) {
				i->polarity = POLARITY_REV;
			}
			if (i->cid_start == ANALOG_CID_START_POLARITY || i->cid_start == ANALOG_CID_START_POLARITY_IN) {
				i->polarity = POLARITY_REV;
				ast_verb(2, "Starting post polarity CID detection on channel %d\n",
					i->channel);
				chan = analog_new_ast_channel(i, AST_STATE_PRERING, 0, ANALOG_SUB_REAL, NULL);
				i->ss_astchan = chan;
				if (!chan) {
					ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
				} else if (ast_pthread_create_detached(&threadid, NULL, __analog_ss_thread, i)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					ast_hangup(chan);
				}
			}
			ast_callid_threadstorage_auto_clean(callid, callid_created);
			break;
		default:
			ast_log(LOG_WARNING,
				"handle_init_event detected polarity reversal on non-FXO (ANALOG_SIG_FXS) interface %d\n",
				i->channel);
			break;
		}
		break;
	case ANALOG_EVENT_DTMFCID:
		switch (i->sig) {
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSKS:
		case ANALOG_SIG_FXSGS:
			callid_created = ast_callid_threadstorage_auto(&callid);
			if (i->cid_start == ANALOG_CID_START_DTMF_NOALERT) {
				ast_verb(2, "Starting DTMF CID detection on channel %d\n",
					i->channel);
				chan = analog_new_ast_channel(i, AST_STATE_PRERING, 0, ANALOG_SUB_REAL, NULL);
				i->ss_astchan = chan;
				if (!chan) {
					ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
				} else if (ast_pthread_create_detached(&threadid, NULL, __analog_ss_thread, i)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					ast_hangup(chan);
				}
			}
			ast_callid_threadstorage_auto_clean(callid, callid_created);
			break;
		default:
			ast_log(LOG_WARNING,
				"handle_init_event detected dtmfcid generation event on non-FXO (ANALOG_SIG_FXS) interface %d\n",
				i->channel);
			break;
		}
		break;
	case ANALOG_EVENT_REMOVED: /* destroy channel, will actually do so in do_monitor */
		ast_log(LOG_NOTICE, "Got ANALOG_EVENT_REMOVED. Destroying channel %d\n",
			i->channel);
		return i->chan_pvt;
	case ANALOG_EVENT_NEONMWI_ACTIVE:
		analog_handle_notify_message(NULL, i, -1, ANALOG_EVENT_NEONMWI_ACTIVE);
		break;
	case ANALOG_EVENT_NEONMWI_INACTIVE:
		analog_handle_notify_message(NULL, i, -1, ANALOG_EVENT_NEONMWI_INACTIVE);
		break;
	}
	return NULL;
}


struct analog_pvt *analog_new(enum analog_sigtype signallingtype, void *private_data)
{
	struct analog_pvt *p;

	p = ast_calloc(1, sizeof(*p));
	if (!p) {
		return p;
	}

	p->outsigmod = ANALOG_SIG_NONE;
	p->sig = signallingtype;
	p->chan_pvt = private_data;

	/* Some defaults for values */
	p->cid_start = ANALOG_CID_START_RING;
	p->cid_signalling = CID_SIG_BELL;
	/* Sub real is assumed to always be alloc'd */
	p->subs[ANALOG_SUB_REAL].allocd = 1;

	return p;
}

/*!
 * \brief Delete the analog private structure.
 * \since 1.8
 *
 * \param doomed Analog private structure to delete.
 *
 * \return Nothing
 */
void analog_delete(struct analog_pvt *doomed)
{
	ast_free(doomed);
}

int analog_config_complete(struct analog_pvt *p)
{
	/* No call waiting on non FXS channels */
	if ((p->sig != ANALOG_SIG_FXOKS) && (p->sig != ANALOG_SIG_FXOLS) && (p->sig != ANALOG_SIG_FXOGS)) {
		p->permcallwaiting = 0;
	}

	analog_set_callwaiting(p, p->permcallwaiting);

	return 0;
}

void analog_free(struct analog_pvt *p)
{
	ast_free(p);
}

/* called while dahdi_pvt is locked in dahdi_fixup */
int analog_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, void *newp)
{
	struct analog_pvt *new_pvt = newp;
	int x;
	ast_debug(1, "New owner for channel %d is %s\n", new_pvt->channel, ast_channel_name(newchan));
	if (new_pvt->owner == oldchan) {
		analog_set_new_owner(new_pvt, newchan);
	}
	for (x = 0; x < 3; x++) {
		if (new_pvt->subs[x].owner == oldchan) {
			new_pvt->subs[x].owner = newchan;
		}
	}

	analog_update_conf(new_pvt);
	return 0;
}

int analog_dnd(struct analog_pvt *p, int flag)
{
	if (flag == -1) {
		return p->dnd;
	}

	p->dnd = flag;

	ast_verb(3, "%s DND on channel %d\n",
			flag ? "Enabled" : "Disabled",
			p->channel);
	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when the Do Not Disturb state is changed on an Analog channel.</synopsis>
			<syntax>
				<parameter name="Status">
					<enumlist>
						<enum name="enabled"/>
						<enum name="disabled"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_SYSTEM, "DNDState",
			"Channel: DAHDI/%d\r\n"
			"Status: %s\r\n", p->channel,
			flag ? "enabled" : "disabled");

	return 0;
}
