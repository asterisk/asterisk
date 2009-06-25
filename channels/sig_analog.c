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

#include "sig_analog.h"

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
	if (p->calls->start_cid_detect)
		return p->calls->start_cid_detect(p->chan_pvt, cid_signalling);
	else
		return -1;
}

static int analog_stop_cid_detect(struct analog_pvt *p)
{
	if (p->calls->stop_cid_detect)
		return p->calls->stop_cid_detect(p->chan_pvt);
	else
		return -1;
}

static int analog_get_callerid(struct analog_pvt *p, char *name, char *number, enum analog_event *ev, size_t timeout)
{
	if (p->calls->get_callerid)
		return p->calls->get_callerid(p->chan_pvt, name, number, ev, timeout);
	else
		return -1;
}

static int analog_get_event(struct analog_pvt *p)
{
	if (p->calls->get_event)
		return p->calls->get_event(p->chan_pvt);
	else
		return -1;
}

static int analog_wait_event(struct analog_pvt *p)
{
	if (p->calls->wait_event)
		return p->calls->wait_event(p->chan_pvt);
	else
		return -1;
}

enum analog_cid_start analog_str_to_cidstart(const char *value)
{
	if (!strcasecmp(value, "ring")) {
		return ANALOG_CID_START_RING;
	} else if (!strcasecmp(value, "polarity")) {
		return ANALOG_CID_START_POLARITY;
	} else if (!strcasecmp(value, "polarity_in")) {
		return ANALOG_CID_START_POLARITY_IN;
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
	}

	return "Unknown";
}

static char *analog_event2str(enum analog_event event)
{
	char *res;
	switch (event) {
	case ANALOG_EVENT_DIALCOMPLETE:
		res = "ANALOG_EVENT_DIALCOMPLETE";
		break;
	case ANALOG_EVENT_WINKFLASH:
		res = "ANALOG_EVENT_WINKFLASH";
		break;
	case ANALOG_EVENT_ONHOOK:
		res = "ANALOG_EVENT_ONHOOK";
		break;
	case ANALOG_EVENT_RINGOFFHOOK:
		res = "ANALOG_EVENT_RINGOFFHOOK";
		break;
	case ANALOG_EVENT_ALARM:
		res = "ANALOG_EVENT_ALARM";
		break;
	case ANALOG_EVENT_NOALARM:
		res = "ANALOG_EVENT_NOALARM";
		break;
	case ANALOG_EVENT_HOOKCOMPLETE:
		res = "ANALOG_EVENT_HOOKCOMPLETE";
		break;
	case ANALOG_EVENT_POLARITY:
		res = "ANALOG_EVENT_POLARITY";
		break;
	case ANALOG_EVENT_RINGERON:
		res = "ANALOG_EVENT_RINGERON";
		break;
	case ANALOG_EVENT_RINGEROFF:
		res = "ANALOG_EVENT_RINGEROFF";
		break;
	case ANALOG_EVENT_RINGBEGIN:
		res = "ANALOG_EVENT_RINGBEGIN";
		break;
	case ANALOG_EVENT_PULSE_START:
		res = "ANALOG_EVENT_PULSE_START";
		break;
	case ANALOG_EVENT_NEONMWI_ACTIVE:
		res = "ANALOG_EVENT_NEONMWI_ACTIVE";
		break;
	case ANALOG_EVENT_NEONMWI_INACTIVE:
		res = "ANALOG_EVENT_NEONMWI_INACTIVE";
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

	ast_debug(1, "Swapping %d and %d\n", a, b);

	towner = p->subs[a].owner;
	tinthreeway = p->subs[a].inthreeway;

	p->subs[a].owner = p->subs[b].owner;
	p->subs[a].inthreeway = p->subs[b].inthreeway;

	p->subs[b].owner = towner;
	p->subs[b].inthreeway = tinthreeway;

	if (p->calls->swap_subs)
		p->calls->swap_subs(p->chan_pvt, a, p->subs[a].owner, b, p->subs[b].owner);

}

static int analog_alloc_sub(struct analog_pvt *p, enum analog_sub x)
{
	p->subs[x].allocd = 1;
	if (p->calls->allocate_sub)
		return p->calls->allocate_sub(p->chan_pvt, x);

	return 0;
}

static int analog_unalloc_sub(struct analog_pvt *p, enum analog_sub x)
{
	p->subs[x].allocd = 0;
	p->subs[x].owner = NULL;
	if (p->calls->unallocate_sub)
		return p->calls->unallocate_sub(p->chan_pvt, x);

	return 0;
}

static int analog_send_callerid(struct analog_pvt *p, int cwcid, struct ast_callerid *cid)
{
	ast_debug(1, "Sending callerid.  CID_NAME: '%s' CID_NUM: '%s'\n", cid->cid_name, cid->cid_num);

	if (cwcid) {
		p->callwaitcas = 0;
	}

	if (p->calls->send_callerid)
		return p->calls->send_callerid(p->chan_pvt, cwcid, cid);
	else
		return 0;
}

static int analog_get_index(struct ast_channel *ast, struct analog_pvt *p, int nullok)
{
	int res;
	if (p->subs[ANALOG_SUB_REAL].owner == ast)
		res = ANALOG_SUB_REAL;
	else if (p->subs[ANALOG_SUB_CALLWAIT].owner == ast)
		res = ANALOG_SUB_CALLWAIT;
	else if (p->subs[ANALOG_SUB_THREEWAY].owner == ast)
		res = ANALOG_SUB_THREEWAY;
	else {
		res = -1;
		if (!nullok)
			ast_log(LOG_WARNING, "Unable to get index, and nullok is not asserted\n");
	}
	return res;
}

static int analog_dsp_reset_and_flush_digits(struct analog_pvt *p)
{
	if (p->calls->dsp_reset_and_flush_digits)
		return p->calls->dsp_reset_and_flush_digits(p->chan_pvt);
	else
		/* Return 0 since I think this is unnecessary to do in most cases it is used.  Mostly only for ast_dsp */
		return 0;
}

static int analog_play_tone(struct analog_pvt *p, enum analog_sub sub, enum analog_tone tone)
{
	if (p->calls->play_tone)
		return p->calls->play_tone(p->chan_pvt, sub, tone);
	else
		return -1;
}

static struct ast_channel * analog_new_ast_channel(struct analog_pvt *p, int state, int startpbx, enum analog_sub sub)
{
	struct ast_channel *c;

	if (p->calls->new_ast_channel)
		c = p->calls->new_ast_channel(p->chan_pvt, state, startpbx, sub);
	else
		return NULL;

	p->subs[sub].owner = c;
	if (!p->owner)
		p->owner = c;
	return c;
}

static int analog_set_echocanceller(struct analog_pvt *p, int enable)
{
	if (p->calls->set_echocanceller)
		return p->calls->set_echocanceller(p->chan_pvt, enable);
	else
		return -1;
}

static int analog_train_echocanceller(struct analog_pvt *p)
{
	if (p->calls->train_echocanceller)
		return p->calls->train_echocanceller(p->chan_pvt);
	else
		return -1;
}

static int analog_is_off_hook(struct analog_pvt *p)
{
	if (p->calls->is_off_hook)
		return p->calls->is_off_hook(p->chan_pvt);
	else
		return -1;
}

static int analog_ring(struct analog_pvt *p)
{
	if (p->calls->ring)
		return p->calls->ring(p->chan_pvt);
	else
		return -1;
}

static int analog_start(struct analog_pvt *p)
{
	if (p->calls->start)
		return p->calls->start(p->chan_pvt);
	else
		return -1;
}

static int analog_dial_digits(struct analog_pvt *p, enum analog_sub sub, struct analog_dialoperation *dop)
{
	if (p->calls->dial_digits)
		return p->calls->dial_digits(p->chan_pvt, sub, dop);
	else
		return -1;
}

static int analog_on_hook(struct analog_pvt *p)
{
	if (p->calls->on_hook)
		return p->calls->on_hook(p->chan_pvt);
	else
		return -1;
}

static int analog_check_for_conference(struct analog_pvt *p)
{
	if (p->calls->check_for_conference)
		return p->calls->check_for_conference(p->chan_pvt);
	else
		return -1;
}

static void analog_all_subchannels_hungup(struct analog_pvt *p)
{
	if (p->calls->all_subchannels_hungup)
		p->calls->all_subchannels_hungup(p->chan_pvt);
}

static void analog_unlock_private(struct analog_pvt *p)
{
	if (p->calls->unlock_private)
		p->calls->unlock_private(p->chan_pvt);
}

static void analog_lock_private(struct analog_pvt *p)
{
	if (p->calls->lock_private)
		p->calls->lock_private(p->chan_pvt);
}

static int analog_off_hook(struct analog_pvt *p)
{
	if (p->calls->off_hook)
		return p->calls->off_hook(p->chan_pvt);
	else
		return -1;
}

static int analog_dsp_set_digitmode(struct analog_pvt *p, enum analog_dsp_digitmode mode)
{
	if (p->calls->dsp_set_digitmode)
		return p->calls->dsp_set_digitmode(p->chan_pvt, mode);
	else
		return -1;
}

static void analog_cb_handle_dtmfup(struct analog_pvt *p, struct ast_channel *ast, enum analog_sub analog_index, struct ast_frame **dest)
{
	if (p->calls->handle_dtmfup)
		p->calls->handle_dtmfup(p->chan_pvt, ast, analog_index, dest);
}

static int analog_wink(struct analog_pvt *p, enum analog_sub index)
{
	if (p->calls->wink)
		return p->calls->wink(p->chan_pvt, index);
	else
		return -1;
}

static int analog_has_voicemail(struct analog_pvt *p)
{
	if (p->calls->has_voicemail)
		return p->calls->has_voicemail(p->chan_pvt);
	else
		return -1;
}

static int analog_is_dialing(struct analog_pvt *p, enum analog_sub index)
{
	if (p->calls->is_dialing)
		return p->calls->is_dialing(p->chan_pvt, index);
	else
		return -1;
}

static int analog_attempt_transfer(struct analog_pvt *p)
{
	/* In order to transfer, we need at least one of the channels to
	   actually be in a call bridge.  We can't conference two applications
	   together (but then, why would we want to?) */
	if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner)) {
		/* The three-way person we're about to transfer to could still be in MOH, so
		   stop if now if appropriate */
		if (ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner))
			ast_queue_control(p->subs[ANALOG_SUB_THREEWAY].owner, AST_CONTROL_UNHOLD);
		if (p->subs[ANALOG_SUB_REAL].owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner), AST_CONTROL_RINGING);
		}
		if (p->subs[ANALOG_SUB_THREEWAY].owner->_state == AST_STATE_RING) {
			analog_play_tone(p, ANALOG_SUB_THREEWAY, ANALOG_TONE_RINGTONE);
		}
		 if (ast_channel_masquerade(p->subs[ANALOG_SUB_THREEWAY].owner, ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner)->name, p->subs[ANALOG_SUB_THREEWAY].owner->name);
			return -1;
		}
		/* Orphan the channel after releasing the lock */
		ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
		analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
	} else if (ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)) {
		ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
		if (p->subs[ANALOG_SUB_THREEWAY].owner->_state == AST_STATE_RINGING) {
			ast_indicate(ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner), AST_CONTROL_RINGING);
		}
		if (p->subs[ANALOG_SUB_REAL].owner->_state == AST_STATE_RING) {
			analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_RINGTONE);
		}
		if (ast_channel_masquerade(p->subs[ANALOG_SUB_REAL].owner, ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner))) {
			ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)->name, p->subs[ANALOG_SUB_REAL].owner->name);
			return -1;
		}
		/* Three-way is now the REAL */
		analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
		ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
		analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
		/* Tell the caller not to hangup */
		return 1;
	} else {
		ast_debug(1, "Neither %s nor %s are in a bridge, nothing to transfer\n",
					p->subs[ANALOG_SUB_REAL].owner->name, p->subs[ANALOG_SUB_THREEWAY].owner->name);
		ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
		return -1;
	}
	return 0;
}

static int analog_update_conf(struct analog_pvt *p)
{
	int x;
	int needconf = 0;

	/* Start with the obvious, general stuff */
	for (x = 0; x < 3; x++) {
		/* Look for three way calls */
		if ((p->subs[x].allocd) && p->subs[x].inthreeway) {
			if (p->calls->conf_add)
				p->calls->conf_add(p->chan_pvt, x);
			needconf++;
		} else {
			if (p->calls->conf_del)
				p->calls->conf_del(p->chan_pvt, x);
		}
	}
	ast_debug(1, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);

	if (p->calls->complete_conference_update)
		p->calls->complete_conference_update(p->chan_pvt, needconf);
	return 0;
}

struct ast_channel * analog_request(struct analog_pvt *p, int *callwait)
{
	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);
	*callwait = (p->owner != NULL);

	if (p->owner) {
		if (analog_alloc_sub(p, ANALOG_SUB_CALLWAIT)) {
			ast_log(LOG_ERROR, "Unable to alloc subchannel\n");
			return NULL;
		}
	}

	return analog_new_ast_channel(p, AST_STATE_RESERVED, 0, p->owner ? ANALOG_SUB_CALLWAIT : ANALOG_SUB_REAL);
}

int analog_available(struct analog_pvt *p, int channelmatch, ast_group_t groupmatch, int *busy, int *channelmatched, int *groupmatched)
{
	int offhook;

	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);
	/* We're at least busy at this point */
	if (busy) {
		if ((p->sig == ANALOG_SIG_FXOKS) || (p->sig == ANALOG_SIG_FXOLS) || (p->sig == ANALOG_SIG_FXOGS))
			*busy = 1;
	}
	/* If do not disturb, definitely not */
	if (p->dnd)
		return 0;
	/* If guard time, definitely not */
	if (p->guardtime && (time(NULL) < p->guardtime)) 
		return 0;

	/* If no owner definitely available */
	if (!p->owner) {
		if (p->sig == ANALOG_SIG_FXSLS)
			return 1;

		offhook = analog_is_off_hook(p);

		if ((p->sig == ANALOG_SIG_FXSKS) || (p->sig == ANALOG_SIG_FXSGS)) {
			/* When "onhook" that means no battery on the line, and thus
			  it is out of service..., if it's on a TDM card... If it's a channel
			  bank, there is no telling... */
			if (offhook)
				return 1;
			else
				return 0;
		} else if (offhook) {
			ast_debug(1, "Channel %d off hook, can't use\n", p->channel);
			/* Not available when the other end is off hook */
			return 0;
		}
		return 1;
	}

	/* If it's not an FXO, forget about call wait */
	if ((p->sig != ANALOG_SIG_FXOKS) && (p->sig != ANALOG_SIG_FXOLS) && (p->sig != ANALOG_SIG_FXOGS)) 
		return 0;

	if (!p->callwaiting) {
		/* If they don't have call waiting enabled, then for sure they're unavailable at this point */
		return 0;
	}

	if (p->subs[ANALOG_SUB_CALLWAIT].allocd) {
		/* If there is already a call waiting call, then we can't take a second one */
		return 0;
	}

	if ((p->owner->_state != AST_STATE_UP) &&
	    ((p->owner->_state != AST_STATE_RINGING) || p->outgoing)) {
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
	if (p->callwaitingcallerid)
		p->callwaitcas = 0;

	if (p->calls->stop_callwait)
		return p->calls->stop_callwait(p->chan_pvt);
	else
		return 0;
}

static int analog_callwait(struct analog_pvt *p)
{
	if (p->callwaitingcallerid)
		p->callwaitcas = 1;
	if (p->calls->callwait)
		return p->calls->callwait(p->chan_pvt);
	else
		return 0;
}

int analog_call(struct analog_pvt *p, struct ast_channel *ast, char *rdest, int timeout)
{
	int res, index,mysig;
	char *c, *n, *l;
	char dest[256]; /* must be same length as p->dialdest */

	ast_log(LOG_DEBUG, "CALLING CID_NAME: %s CID_NUM:: %s\n", ast->connected.id.name, ast->connected.id.number);

	ast_copy_string(dest, rdest, sizeof(dest));
	ast_copy_string(p->dialdest, rdest, sizeof(p->dialdest));

	if ((ast->_state == AST_STATE_BUSY)) {
		ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_BUSY);
		return 0;
	}

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "analog_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	p->dialednone = 0;

	mysig = p->sig;
	if (p->outsigmod > -1)
		mysig = p->outsigmod;

	switch (mysig) {
	case ANALOG_SIG_FXOLS:
	case ANALOG_SIG_FXOGS:
	case ANALOG_SIG_FXOKS:
		if (p->owner == ast) {
			/* Normal ring, on hook */

			/* Don't send audio while on hook, until the call is answered */
			p->dialing = 1;
			/* XXX */
#if 0
			/* Choose proper cadence */
			if ((p->distinctivering > 0) && (p->distinctivering <= num_cadence)) {
				if (ioctl(p->subs[ANALOG_SUB_REAL].dfd, DAHDI_SETCADENCE, &cadences[p->distinctivering - 1]))
					ast_log(LOG_WARNING, "Unable to set distinctive ring cadence %d on '%s': %s\n", p->distinctivering, ast->name, strerror(errno));
				p->cidrings = cidrings[p->distinctivering - 1];
			} else {
				if (ioctl(p->subs[ANALOG_SUB_REAL].dfd, DAHDI_SETCADENCE, NULL))
					ast_log(LOG_WARNING, "Unable to reset default ring on '%s': %s\n", ast->name, strerror(errno));
				p->cidrings = p->sendcalleridafter;
			}
#endif
			p->cidrings = p->sendcalleridafter;

			/* nick@dccinc.com 4/3/03 mods to allow for deferred dialing */
			c = strchr(dest, '/');
			if (c)
				c++;
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
			p->dialing = 1;
		} else {
			if (ast->connected.id.number)
				ast_copy_string(p->callwait_num, ast->connected.id.number, sizeof(p->callwait_num));
			else
				p->callwait_num[0] = '\0';
			if (ast->connected.id.name)
				ast_copy_string(p->callwait_name, ast->connected.id.name, sizeof(p->callwait_name));
			else
				p->callwait_name[0] = '\0';

			/* Call waiting tone instead */
			if (analog_callwait(p)) {
				return -1;
			}
			/* Make ring-back */
			if (analog_play_tone(p, ANALOG_SUB_CALLWAIT, ANALOG_TONE_RINGTONE))
				ast_log(LOG_WARNING, "Unable to generate call-wait ring-back on channel %s\n", ast->name);

		}
		n = ast->connected.id.name;
		l = ast->connected.id.number;
		if (l)
			ast_copy_string(p->lastcid_num, l, sizeof(p->lastcid_num));
		else
			p->lastcid_num[0] = '\0';
		if (n)
			ast_copy_string(p->lastcid_name, n, sizeof(p->lastcid_name));
		else
			p->lastcid_name[0] = '\0';

		if (p->use_callerid) {
			p->callwaitcas = 0;
			p->cid.cid_name = p->lastcid_name;
			p->cid.cid_num = p->lastcid_num;
		}

		ast_setstate(ast, AST_STATE_RINGING);
		index = analog_get_index(ast, p, 0);
		if (index > -1) {
			ast_queue_control(p->subs[index].owner, AST_CONTROL_RINGING);
		}
		break;
	case ANALOG_SIG_FXSLS:
	case ANALOG_SIG_FXSGS:
	case ANALOG_SIG_FXSKS:
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
		if (c)
			c++;
		else
			c = "";
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
		ast_log(LOG_DEBUG, "Dialing '%s'\n", c);
		p->dop.op = ANALOG_DIAL_OP_REPLACE;

		c += p->stripmsd;

		switch (mysig) {
		case ANALOG_SIG_FEATD:
			l = ast->connected.id.number;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T*%s*%s*", l, c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T**%s*", c);
			break;
		case ANALOG_SIG_FEATDMF:
			l = ast->connected.id.number;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*00%s#*%s#", l, c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*02#*%s#", c);
			break;
		case ANALOG_SIG_FEATDMF_TA:
		{
			const char *cic = "", *ozz = "";

			/* If you have to go through a Tandem Access point you need to use this */
#ifndef STANDALONE
			ozz = pbx_builtin_getvar_helper(p->owner, "FEATDMF_OZZ");
			if (!ozz)
				ozz = analog_defaultozz;
			cic = pbx_builtin_getvar_helper(p->owner, "FEATDMF_CIC");
			if (!cic)
				cic = analog_defaultcic;
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
			if (p->pulse)
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "P%sw", c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%sw", c);
			break;
		}

		if (p->echotraining && (strlen(p->dop.dialstr) > 4)) {
			memset(p->echorest, 'w', sizeof(p->echorest) - 1);
			strcpy(p->echorest + (p->echotraining / 400) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
			p->echorest[sizeof(p->echorest) - 1] = '\0';
			p->echobreak = 1;
			p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
		} else
			p->echobreak = 0;
		if (!res) {
			if (analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop)) {
				int saveerr = errno;

				analog_on_hook(p);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(saveerr));
				return -1;
			}
		} else
			ast_debug(1, "Deferring dialing...\n");
		p->dialing = 1;
		if (ast_strlen_zero(c))
			p->dialednone = 1;
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
	int index, x;

	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	index = analog_get_index(ast, p, 1);

	x = 0;
	if (p->origcid_num) {
		ast_copy_string(p->cid_num, p->origcid_num, sizeof(p->cid_num));
		free(p->origcid_num);
		p->origcid_num = NULL;
	}
	if (p->origcid_name) {
		ast_copy_string(p->cid_name, p->origcid_name, sizeof(p->cid_name));
		free(p->origcid_name);
		p->origcid_name = NULL;
	}

	analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);

	ast_debug(1, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		p->channel, index, p->subs[ANALOG_SUB_REAL].allocd, p->subs[ANALOG_SUB_CALLWAIT].allocd, p->subs[ANALOG_SUB_THREEWAY].allocd);
	if (index > -1) {
		/* Real channel, do some fixup */
		p->subs[index].owner = NULL;
		p->subs[index].needcallerid = 0;
		p->polarity = POLARITY_IDLE;
		if (index == ANALOG_SUB_REAL) {
			if (p->subs[ANALOG_SUB_CALLWAIT].allocd && p->subs[ANALOG_SUB_THREEWAY].allocd) {
				ast_debug(1, "Normal call hung up with both three way call and a call waiting call in place?\n");
				if (p->subs[ANALOG_SUB_CALLWAIT].inthreeway) {
					/* We had flipped over to answer a callwait and now it's gone */
					ast_debug(1, "We were flipped over to the callwait, moving back and unowning.\n");
					/* Move to the call-wait, but un-own us until they flip back. */
					analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_REAL);
					analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
					p->owner = NULL;
				} else {
					/* The three way hung up, but we still have a call wait */
					ast_debug(1, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
					analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
					analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
					if (p->subs[ANALOG_SUB_REAL].inthreeway) {
						/* This was part of a three way call.  Immediately make way for
						   another call */
						ast_debug(1, "Call was complete, setting owner to former third call\n");
						p->owner = p->subs[ANALOG_SUB_REAL].owner;
					} else {
						/* This call hasn't been completed yet...  Set owner to NULL */
						ast_debug(1, "Call was incomplete, setting owner to NULL\n");
						p->owner = NULL;
					}
					p->subs[ANALOG_SUB_REAL].inthreeway = 0;
				}
			} else if (p->subs[ANALOG_SUB_CALLWAIT].allocd) {
				/* Move to the call-wait and switch back to them. */
				analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_REAL);
				analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
				p->owner = p->subs[ANALOG_SUB_REAL].owner;
				if (p->owner->_state != AST_STATE_UP)
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_ANSWER);
				if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner))
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
			} else if (p->subs[ANALOG_SUB_THREEWAY].allocd) {
				analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
				analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
				if (p->subs[ANALOG_SUB_REAL].inthreeway) {
					/* This was part of a three way call.  Immediately make way for
					   another call */
					ast_debug(1, "Call was complete, setting owner to former third call\n");
					p->owner = p->subs[ANALOG_SUB_REAL].owner;
				} else {
					/* This call hasn't been completed yet...  Set owner to NULL */
					ast_debug(1, "Call was incomplete, setting owner to NULL\n");
					p->owner = NULL;
				}
				p->subs[ANALOG_SUB_REAL].inthreeway = 0;
			}
		} else if (index == ANALOG_SUB_CALLWAIT) {
			/* Ditch the holding callwait call, and immediately make it availabe */
			if (p->subs[ANALOG_SUB_CALLWAIT].inthreeway) {
				/* This is actually part of a three way, placed on hold.  Place the third part
				   on music on hold now */
				if (p->subs[ANALOG_SUB_THREEWAY].owner && ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_THREEWAY].owner, AST_CONTROL_HOLD, 
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				p->subs[ANALOG_SUB_THREEWAY].inthreeway = 0;
				/* Make it the call wait now */
				analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_THREEWAY);
				analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
			} else
				analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
		} else if (index == ANALOG_SUB_THREEWAY) {
			if (p->subs[ANALOG_SUB_CALLWAIT].inthreeway) {
				/* The other party of the three way call is currently in a call-wait state.
				   Start music on hold for them, and take the main guy out of the third call */
				if (p->subs[ANALOG_SUB_CALLWAIT].owner && ast_bridged_channel(p->subs[ANALOG_SUB_CALLWAIT].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_CALLWAIT].owner, AST_CONTROL_HOLD, 
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				p->subs[ANALOG_SUB_CALLWAIT].inthreeway = 0;
			}
			p->subs[ANALOG_SUB_REAL].inthreeway = 0;
			/* If this was part of a three way call index, let us make
			   another three way call */
			analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
		} else {
			/* This wasn't any sort of call, but how are we an index? */
			ast_log(LOG_WARNING, "Index found but not any type of call?\n");
		}
	}

	if (!p->subs[ANALOG_SUB_REAL].owner && !p->subs[ANALOG_SUB_CALLWAIT].owner && !p->subs[ANALOG_SUB_THREEWAY].owner) {
		p->owner = NULL;
#if 0
		p->ringt = 0;
#endif
#if 0 /* Since we set it in _call */
		p->cidrings = 1;
#endif
		p->outgoing = 0;

		/* Perform low level hangup if no owner left */
		res = analog_on_hook(p);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to hangup line %s\n", ast->name);
		}
		switch (p->sig) {
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOKS:
			/* If they're off hook, try playing congestion */
			if (analog_is_off_hook(p))
				analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
			else
				analog_play_tone(p, ANALOG_SUB_REAL, -1);
			break;
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSKS:
			/* Make sure we're not made available for at least two seconds assuming
			   we were actually used for an inbound or outbound call. */
			if (ast->_state != AST_STATE_RESERVED) {
				time(&p->guardtime);
				p->guardtime += 2;
			}
			break;
		default:
			analog_play_tone(p, ANALOG_SUB_REAL, -1);
		}


		analog_set_echocanceller(p, 0);

		x = 0;
		ast_channel_setoption(ast,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		ast_channel_setoption(ast,AST_OPTION_TDD,&x,sizeof(char),0);
		p->callwaitcas = 0;
		p->callwaiting = p->permcallwaiting;
		p->hidecallerid = p->permhidecallerid;
		p->dialing = 0;
		analog_update_conf(p);
		analog_all_subchannels_hungup(p);
	}

	analog_stop_callwait(p);
	ast->tech_pvt = NULL;

	ast_verb(3, "Hanging up on '%s'\n", ast->name);

	return 0;
}

int analog_answer(struct analog_pvt *p, struct ast_channel *ast)
{
	int res = 0;
	int index;
	int oldstate = ast->_state;
	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);
	ast_setstate(ast, AST_STATE_UP);
	index = analog_get_index(ast, p, 1);
	if (index < 0)
		index = ANALOG_SUB_REAL;
	switch (p->sig) {
	case ANALOG_SIG_FXSLS:
	case ANALOG_SIG_FXSGS:
	case ANALOG_SIG_FXSKS:
#if 0
		p->ringt = 0;
#endif
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
		ast_debug(1, "Took %s off hook\n", ast->name);
		if (p->hanguponpolarityswitch) {
			gettimeofday(&p->polaritydelaytv, NULL);
		}
		res = analog_off_hook(p);
		analog_play_tone(p, index, -1);
		p->dialing = 0;
		if ((index == ANALOG_SUB_REAL) && p->subs[ANALOG_SUB_THREEWAY].inthreeway) {
			if (oldstate == AST_STATE_RINGING) {
				ast_debug(1, "Finally swapping real and threeway\n");
				analog_play_tone(p, ANALOG_SUB_THREEWAY, -1);
				analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
				p->owner = p->subs[ANALOG_SUB_REAL].owner;
			}
		}
		if ((p->sig == ANALOG_SIG_FXSLS) || (p->sig == ANALOG_SIG_FXSKS) || (p->sig == ANALOG_SIG_FXSGS)) {
			analog_set_echocanceller(p, 1);
			analog_train_echocanceller(p);
		}
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		res = -1;
	}
	ast_setstate(ast, AST_STATE_UP);
	return res;
}

static int analog_handles_digit(struct ast_frame *f)
{
	char subclass = toupper(f->subclass);

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

void analog_handle_dtmfup(struct analog_pvt *p, struct ast_channel *ast, enum analog_sub index, struct ast_frame **dest)
{
	struct ast_frame *f = *dest;

	if (p->callwaitcas) {
		if ((f->subclass == 'A') || (f->subclass == 'D')) {
			ast_log(LOG_ERROR, "Got some DTMF, but it's for the CAS\n");
			p->cid.cid_name = p->callwait_name;
			p->cid.cid_num = p->callwait_num;
			analog_send_callerid(p, 1, &p->cid);
		}
		if (analog_handles_digit(f))
			p->callwaitcas = 0;
		p->subs[index].f.frametype = AST_FRAME_NULL;
		p->subs[index].f.subclass = 0;
		*dest = &p->subs[index].f;
	} else {
		analog_cb_handle_dtmfup(p, ast, index, dest);
	}
}

static int analog_my_getsigstr(struct ast_channel *chan, char *str, const char *term, int ms)
{
	char c;

	*str = 0; /* start with empty output buffer */
	for (;;)
	{
		/* Wait for the first digit (up to specified ms). */
		c = ast_waitfordigit(chan, ms);
		/* if timeout, hangup or error, return as such */
		if (c < 1)
			return c;
		*str++ = c;
		*str = 0;
		if (strchr(term, c))
			return 1;
	}
}

static int analog_handle_notify_message(struct ast_channel *chan, struct analog_pvt *p, int cid_flags, int neon_mwievent)
{
	if (p->calls->handle_notify_message) {
		p->calls->handle_notify_message(chan, p->chan_pvt, cid_flags, neon_mwievent);
		return 0;
	}
	else
		return -1;
}

static int analog_increase_ss_count(struct analog_pvt *p)
{
	if (p->calls->increase_ss_count) {
		p->calls->increase_ss_count();
		return 0;
	} else
		return -1;
}

static int analog_decrease_ss_count(struct analog_pvt *p)
{
	if (p->calls->decrease_ss_count) {
		p->calls->decrease_ss_count();
		return 0;
	} else
		return -1;
}

#define ANALOG_NEED_MFDETECT(p) (((p)->sig == ANALOG_SIG_FEATDMF) || ((p)->sig == ANALOG_SIG_FEATDMF_TA) || ((p)->sig == ANALOG_SIG_E911) || ((p)->sig == ANALOG_SIG_FGC_CAMA) || ((p)->sig == ANALOG_SIG_FGC_CAMAMF) || ((p)->sig == ANALOG_SIG_FEATB))

/* Note by jpeeler: This function has a rather large section of code ifdefed
 * away. I'd like to leave the code there until more testing is done and I
 * know for sure that nothing got left out. The plan is at the latest for this
 * comment and code below to be removed shortly after the merging of sig_pri.
 */
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
	struct callerid_state *cs = NULL;
	char *name = NULL, *number = NULL;
	int flags;
#if 0
	unsigned char buf[256];
	int distMatches;
	int curRingData[3];
	int receivedRingT;
	int samples = 0;
	int counter1;
	int counter;
	int i;
#endif
	int timeout;
	int getforward = 0;
	char *s1, *s2;
	int len = 0;
	int res;
	int index;

	analog_increase_ss_count(p);

	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);

	/* in the bizarre case where the channel has become a zombie before we
	   even get started here, abort safely
	*/
	if (!p) {
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", chan->name);
		ast_hangup(chan);
		goto quit;
	}

	ast_verb(3, "Starting simple switch on '%s'\n", chan->name);
	index = analog_get_index(chan, p, 1);
	if (index < 0) {
		ast_log(LOG_WARNING, "Huh?\n");
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
		if (analog_wink(p, index))
			goto quit;
		/* Fall through */
	case ANALOG_SIG_EM:
	case ANALOG_SIG_EM_E1:
	case ANALOG_SIG_SF:
	case ANALOG_SIG_FGC_CAMA:
		res = analog_play_tone(p, index, -1);

		analog_dsp_reset_and_flush_digits(p);

		if (ANALOG_NEED_MFDETECT(p)) {
			analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_MF);
		} else
			analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);

		memset(dtmfbuf, 0, sizeof(dtmfbuf));
		/* Wait for the first digit only if immediate=no */
		if (!p->immediate)
			/* Wait for the first digit (up to 5 seconds). */
			res = ast_waitfordigit(chan, 5000);
		else
			res = 0;
		if (res > 0) {
			/* save first char */
			dtmfbuf[0] = res;
			switch (p->sig) {
			case ANALOG_SIG_FEATD:
			case ANALOG_SIG_SF_FEATD:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
				if (res > 0)
					res = analog_my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
				if (res < 1)
					analog_dsp_reset_and_flush_digits(p);
				break;
			case ANALOG_SIG_FEATDMF_TA:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if (res < 1)
					analog_dsp_reset_and_flush_digits(p);
				if (analog_wink(p, index)) goto quit;
				dtmfbuf[0] = 0;
				/* Wait for the first digit (up to 5 seconds). */
				res = ast_waitfordigit(chan, 5000);
				if (res <= 0) break;
				dtmfbuf[0] = res;
				/* fall through intentionally */
			case ANALOG_SIG_FEATDMF:
			case ANALOG_SIG_E911:
			case ANALOG_SIG_FGC_CAMAMF:
			case ANALOG_SIG_SF_FEATDMF:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				/* if international caca, do it again to get real ANO */
				if ((p->sig == ANALOG_SIG_FEATDMF) && (dtmfbuf[1] != '0') && (strlen(dtmfbuf) != 14))
				{
					if (analog_wink(p, index)) goto quit;
					dtmfbuf[0] = 0;
					/* Wait for the first digit (up to 5 seconds). */
					res = ast_waitfordigit(chan, 5000);
					if (res <= 0) break;
					dtmfbuf[0] = res;
					res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				}
				if (res > 0) {
					/* if E911, take off hook */
					if (p->sig == ANALOG_SIG_E911)
						analog_off_hook(p);
					res = analog_my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "#", 3000);
				}
				if (res < 1)
					analog_dsp_reset_and_flush_digits(p);
				break;
			case ANALOG_SIG_FEATB:
			case ANALOG_SIG_SF_FEATB:
				res = analog_my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if (res < 1)
					analog_dsp_reset_and_flush_digits(p);
				break;
			case ANALOG_SIG_EMWINK:
				/* if we received a '*', we are actually receiving Feature Group D
				   dial syntax, so use that mode; otherwise, fall through to normal
				   mode
				*/
				if (res == '*') {
					res = analog_my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
					if (res > 0)
						res = analog_my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
					if (res < 1)
						analog_dsp_reset_and_flush_digits(p);
					break;
				}
			default:
				/* If we got the first digit, get the rest */
				len = 1;
				dtmfbuf[len] = '\0';
				while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
					if (ast_exists_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
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
				if (anibuf[strlen(anibuf) - 1] == '#')
					anibuf[strlen(anibuf) - 1] = 0;
				ast_set_callerid(chan, anibuf + 2, NULL, anibuf + 2);
			}
			analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);
		}

		ast_copy_string(exten, dtmfbuf, sizeof(exten));
		if (ast_strlen_zero(exten))
			ast_copy_string(exten, "s", sizeof(exten));
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
					if (!ast_strlen_zero(p->cid_num))
						ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					else
						ast_set_callerid(chan, s1, NULL, s1);
					ast_copy_string(exten, s2, sizeof(exten));
				} else
					ast_copy_string(exten, s1, sizeof(exten));
			} else if (p->sig == ANALOG_SIG_FEATD)
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if ((p->sig == ANALOG_SIG_FEATDMF) || (p->sig == ANALOG_SIG_FEATDMF_TA)) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2) {
					if (!ast_strlen_zero(p->cid_num))
						ast_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					else
						if (*(s1 + 2))
							ast_set_callerid(chan, s1 + 2, NULL, s1 + 2);
					ast_copy_string(exten, s2 + 1, sizeof(exten));
				} else
					ast_copy_string(exten, s1 + 2, sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
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
					if (*(s2 + 2))
						ast_set_callerid(chan, s2 + 2, NULL, s2 + 2);
				}
				if (s1)	ast_copy_string(exten, s1, sizeof(exten));
				else ast_copy_string(exten, "911", sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-E911/FGC CAMA input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == ANALOG_SIG_FEATB) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				ast_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				ast_copy_string(exten, exten2 + 1, sizeof(exten));
			} else
				ast_log(LOG_WARNING, "Got a non-Feature Group B input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if ((p->sig == ANALOG_SIG_FEATDMF) || (p->sig == ANALOG_SIG_FEATDMF_TA)) {
			analog_wink(p, index);
			/* some switches require a minimum guard time between
			the last FGD wink and something that answers
			immediately. This ensures it */
			if (ast_safe_sleep(chan,100)) goto quit;
		}
		analog_set_echocanceller(p, 1);

		analog_dsp_set_digitmode(p, ANALOG_DIGITMODE_DTMF);

		if (ast_exists_extension(chan, chan->context, exten, 1, chan->cid.cid_num)) {
			ast_copy_string(chan->exten, exten, sizeof(chan->exten));
			analog_dsp_reset_and_flush_digits(p);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero\n");
				res = analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
			}
			goto quit;
		} else {
			ast_verb(3, "Unknown extension '%s' in context '%s' requested\n", exten, chan->context);
			sleep(2);
			res = analog_play_tone(p, index, ANALOG_TONE_INFO);
			if (res < 0)
				ast_log(LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			else
				sleep(1);
			res = ast_streamfile(chan, "ss-noservice", chan->language);
			if (res >= 0)
				ast_waitstream(chan, "");
			res = analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
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
		if (p->subs[ANALOG_SUB_THREEWAY].owner) 
			timeout = 999999;
		while (len < AST_MAX_EXTENSION-1) {
			/* Read digit unless it's supposed to be immediate, in which case the
			   only answer is 's' */
			if (p->immediate) 
				res = 's';
			else
				res = ast_waitfordigit(chan, timeout);
			timeout = 0;
			if (res < 0) {
				ast_debug(1, "waitfordigit returned < 0...\n");
				res = analog_play_tone(p, index, -1);
				ast_hangup(chan);
				goto quit;
			} else if (res)  {
				exten[len++]=res;
				exten[len] = '\0';
			}
			if (!ast_ignore_pattern(chan->context, exten))
				analog_play_tone(p, index, -1);
			else
				analog_play_tone(p, index, ANALOG_TONE_DIALTONE);
			if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num) && strcmp(exten, ast_parking_ext())) {
				if (!res || !ast_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
					if (getforward) {
						/* Record this as the forwarding extension */
						ast_copy_string(p->call_forward, exten, sizeof(p->call_forward)); 
						ast_verb(3, "Setting call forward to '%s' on channel %d\n", p->call_forward, p->channel);
						res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
						if (res)
							break;
						usleep(500000);
						res = analog_play_tone(p, index, -1);
						sleep(1);
						memset(exten, 0, sizeof(exten));
						res = analog_play_tone(p, index, ANALOG_TONE_DIALTONE);
						len = 0;
						getforward = 0;
					} else  {
						res = analog_play_tone(p, index, -1);
						ast_copy_string(chan->exten, exten, sizeof(chan->exten));
						if (!ast_strlen_zero(p->cid_num)) {
							if (!p->hidecallerid)
								ast_set_callerid(chan, p->cid_num, NULL, p->cid_num); 
							else
								ast_set_callerid(chan, NULL, NULL, p->cid_num); 
						}
						if (!ast_strlen_zero(p->cid_name)) {
							if (!p->hidecallerid)
								ast_set_callerid(chan, NULL, p->cid_name, NULL);
						}
						ast_setstate(chan, AST_STATE_RING);
						analog_set_echocanceller(p, 1);
						res = ast_pbx_run(chan);
						if (res) {
							ast_log(LOG_WARNING, "PBX exited non-zero\n");
							res = analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
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
				res = analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
				analog_wait_event(p);
				ast_hangup(chan);
				goto quit;
			} else if (p->callwaiting && !strcmp(exten, "*70")) {
				ast_verb(3, "Disabling call waiting on %s\n", chan->name);
				/* Disable call waiting if enabled */
				p->callwaiting = 0;
				res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = analog_firstdigittimeout;

			} else if (!strcmp(exten,ast_pickup_ext())) {
				/* Scan all channels and see if there are any
				 * ringing channels that have call groups
				 * that equal this channels pickup group
				 */
				if (index == ANALOG_SUB_REAL) {
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
						res = analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
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
				ast_verb(3, "Disabling Caller*ID on %s\n", chan->name);
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				if (chan->cid.cid_num)
					free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				if (chan->cid.cid_name)
					free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = analog_firstdigittimeout;
			} else if (p->callreturn && !strcmp(exten, "*69")) {
				res = 0;
				if (!ast_strlen_zero(p->lastcid_num)) {
					res = ast_say_digit_str(chan, p->lastcid_num, "", chan->language);
				}
				if (!res)
					res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				break;
			} else if (!strcmp(exten, "*78")) {
				/* Do not disturb */
				ast_verb(3, "Enabled DND on channel %d\n", p->channel);
				manager_event(EVENT_FLAG_SYSTEM, "DNDState",
					      "Channel: DAHDI/%d\r\n"
					      "Status: enabled\r\n", p->channel);
				res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				p->dnd = 1;
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (!strcmp(exten, "*79")) {
				/* Do not disturb */
				ast_verb(3, "Disabled DND on channel %d\n", p->channel);
				manager_event(EVENT_FLAG_SYSTEM, "DNDState",
					      "Channel: DAHDI/%d\r\n"
					      "Status: disabled\r\n", p->channel);
				res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				p->dnd = 0;
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*72")) {
				res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				getforward = 1;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*73")) {
				ast_verb(3, "Cancelling call forwarding on channel %d\n", p->channel);
				res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				memset(p->call_forward, 0, sizeof(p->call_forward));
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if ((p->transfer || p->canpark) && !strcmp(exten, ast_parking_ext()) &&
						p->subs[ANALOG_SUB_THREEWAY].owner &&
						ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)) {
				/* This is a three way call, the main call being a real channel,
					and we're parking the first call. */
				ast_masq_park_call(ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner), chan, 0, NULL);
				ast_verb(3, "Parking call to '%s'\n", chan->name);
				break;
			} else if (!ast_strlen_zero(p->lastcid_num) && !strcmp(exten, "*60")) {
				ast_verb(3, "Blacklisting number %s\n", p->lastcid_num);
				res = ast_db_put("blacklist", p->lastcid_num, "1");
				if (!res) {
					res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
					memset(exten, 0, sizeof(exten));
					len = 0;
				}
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				ast_verb(3, "Enabling Caller*ID on %s\n", chan->name);
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				if (chan->cid.cid_num)
					free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				if (chan->cid.cid_name)
					free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				ast_set_callerid(chan, p->cid_num, p->cid_name, NULL);
				res = analog_play_tone(p, index, ANALOG_TONE_DIALRECALL);
				if (res) {
					ast_log(LOG_WARNING, "Unable to do dial recall on channel %s: %s\n",
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = analog_firstdigittimeout;
			} else if (!strcmp(exten, "*0")) {
#ifdef XXX
				struct ast_channel *nbridge = p->subs[ANALOG_SUB_THREEWAY].owner;
				struct dahdi_pvt *pbridge = NULL;
				  /* set up the private struct of the bridged one, if any */
				if (nbridge && ast_bridged_channel(nbridge))
					pbridge = ast_bridged_channel(nbridge)->tech_pvt;
				if (nbridge && pbridge &&
				    (nbridge->tech == chan_tech) &&
				    (ast_bridged_channel(nbridge)->tech == chan_tech) &&
				    ISTRUNK(pbridge)) {
					int func = DAHDI_FLASH;
					/* Clear out the dial buffer */
					p->dop.dialstr[0] = '\0';
					/* flash hookswitch */
					if ((ioctl(pbridge->subs[ANALOG_SUB_REAL].dfd,DAHDI_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
						ast_log(LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n",
							nbridge->name, strerror(errno));
					}
					analog_swap_subs(p, ANALOG_SUB_REAL, ANALOG_SUB_THREEWAY);
					analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
					p->owner = p->subs[ANALOG_SUB_REAL].owner;
					if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner))
						ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
					ast_hangup(chan);
					goto quit;
				} else {
					analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
					analog_wait_event(p);
					analog_play_tone(p, index, -1);
					analog_swap_subs(p, ANALOG_SUB_REAL, ANALOG_SUB_THREEWAY);
					analog_unalloc_sub(p, ANALOG_SUB_THREEWAY);
					p->owner = p->subs[ANALOG_SUB_REAL].owner;
					ast_hangup(chan);
					goto quit;
				}
#endif
			} else if (!ast_canmatch_extension(chan, chan->context, exten, 1, chan->cid.cid_num) &&
							((exten[0] != '*') || (strlen(exten) > 2))) {
				ast_debug(1, "Can't match %s from '%s' in context %s\n", exten, chan->cid.cid_num ? chan->cid.cid_num : "<Unknown Caller>", chan->context);
				break;
			}
			if (!timeout)
				timeout = analog_gendigittimeout;
			if (len && !ast_ignore_pattern(chan->context, exten))
				analog_play_tone(p, index, -1);
		}
		break;
	case ANALOG_SIG_FXSLS:
	case ANALOG_SIG_FXSGS:
	case ANALOG_SIG_FXSKS:

		/* If we want caller id, we're in a prering state due to a polarity reversal
		 * and we're set to use a polarity reversal to trigger the start of caller id,
		 * grab the caller id and wait for ringing to start... */
		if (p->use_callerid && (chan->_state == AST_STATE_PRERING && (p->cid_start == ANALOG_CID_START_POLARITY || p->cid_start == ANALOG_CID_START_POLARITY_IN))) {
			/* If set to use DTMF CID signalling, listen for DTMF */
			if (p->cid_signalling == CID_SIG_DTMF) {
				int i = 0;
				cs = NULL;
				ast_debug(1, "Receiving DTMF cid on "
					"channel %s\n", chan->name);
#if 0
				dahdi_setlinear(p->subs[index].dfd, 0);
#endif
				res = 2000;
				for (;;) {
					struct ast_frame *f;
					res = ast_waitfor(chan, res);
					if (res <= 0) {
						ast_log(LOG_WARNING, "DTMFCID timed out waiting for ring. "
							"Exiting simple switch\n");
						ast_hangup(chan);
						goto quit;
					}
					f = ast_read(chan);
					if (!f)
						break;
					if (f->frametype == AST_FRAME_DTMF) {
						dtmfbuf[i++] = f->subclass;
						ast_debug(1, "CID got digit '%c'\n", f->subclass);
						res = 2000;
					}
					ast_frfree(f);
					if (chan->_state == AST_STATE_RING ||
					    chan->_state == AST_STATE_RINGING)
						break; /* Got ring */
				}
				dtmfbuf[i] = '\0';
#if 0
				dahdi_setlinear(p->subs[index].dfd, p->subs[index].linear);
#endif
				/* Got cid and ring. */
				ast_debug(1, "CID got string '%s'\n", dtmfbuf);
				callerid_get_dtmf(dtmfbuf, dtmfcid, &flags);
				ast_debug(1, "CID is '%s', flags %d\n",
					dtmfcid, flags);
				/* If first byte is NULL, we have no cid */
				if (!ast_strlen_zero(dtmfcid))
					number = dtmfcid;
				else
					number = NULL;
#if 0
			/* If set to use V23 Signalling, launch our FSK gubbins and listen for it */
			} else if ((p->cid_signalling == CID_SIG_V23) || (p->cid_signalling == CID_SIG_V23_JP)) {
				cs = callerid_new(p->cid_signalling);
				if (cs) {
					samples = 0;
#if 1
					bump_gains(p);
#endif				
					/* Take out of linear mode for Caller*ID processing */
					dahdi_setlinear(p->subs[index].dfd, 0);
					
					/* First we wait and listen for the Caller*ID */
					for (;;) {	
						i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
						if ((res = ioctl(p->subs[index].dfd, DAHDI_IOMUX, &i)))	{
							ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
							callerid_free(cs);
							ast_hangup(chan);
							goto quit;
						}
						if (i & DAHDI_IOMUX_SIGEVENT) {
							res = dahdi_get_event(p->subs[index].dfd);
							ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));

							if (p->cid_signalling == CID_SIG_V23_JP) {
#ifdef DAHDI_EVENT_RINGBEGIN
								if (res == ANALOG_EVENT_RINGBEGIN) {
									res = analog_off_hook(p);
									usleep(1);
								}
#endif
							} else {
								res = 0;
								break;
							}
						} else if (i & DAHDI_IOMUX_READ) {
							res = read(p->subs[index].dfd, buf, sizeof(buf));
							if (res < 0) {
								if (errno != ELAST) {
									ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
									callerid_free(cs);
									ast_hangup(chan);
									goto quit;
								}
								break;
							}
							samples += res;

							if  (p->cid_signalling == CID_SIG_V23_JP) {
								res = callerid_feed_jp(cs, buf, res, AST_LAW(p));
							} else {
								res = callerid_feed(cs, buf, res, AST_LAW(p));
							}

							if (res < 0) {
								ast_log(LOG_WARNING, "CallerID feed failed on channel '%s'\n", chan->name);
								break;
							} else if (res)
								break;
							else if (samples > (8000 * 10))
								break;
						}
					}
					if (res == 1) {
						callerid_get(cs, &name, &number, &flags);
						ast_log(LOG_NOTICE, "CallerID number: %s, name: %s, flags=%d\n", number, name, flags);
					}

					if (p->cid_signalling == CID_SIG_V23_JP) {
						res = analog_on_hook(p);
						usleep(1);
						res = 4000;
					} else {

						/* Finished with Caller*ID, now wait for a ring to make sure there really is a call coming */ 
						res = 2000;
					}

					for (;;) {
						struct ast_frame *f;
						res = ast_waitfor(chan, res);
						if (res <= 0) {
							ast_log(LOG_WARNING, "CID timed out waiting for ring. "
								"Exiting simple switch\n");
							ast_hangup(chan);
							goto quit;
						} 
						if (!(f = ast_read(chan))) {
							ast_log(LOG_WARNING, "Hangup received waiting for ring. Exiting simple switch\n");
							ast_hangup(chan);
							goto quit;
						}
						ast_frfree(f);
						if (chan->_state == AST_STATE_RING ||
						    chan->_state == AST_STATE_RINGING) 
							break; /* Got ring */
					}
	
					/* Restore linear mode (if appropriate) for Caller*ID processing */
					dahdi_setlinear(p->subs[index].dfd, p->subs[index].linear);
#if 1
					restore_gains(p);
#endif				
				} else
					ast_log(LOG_WARNING, "Unable to get caller ID space\n");			
#endif
			} else {
				ast_log(LOG_WARNING, "Channel %s in prering "
					"state, but I have nothing to do. "
					"Terminating simple switch, should be "
					"restarted by the actual ring.\n", 
					chan->name);
				ast_hangup(chan);
				goto quit;
			}
		} else if (p->use_callerid && p->cid_start == ANALOG_CID_START_RING) {
			int timeout = 10000;  /* Ten seconds */
			struct timeval start = ast_tvnow();
			enum analog_event ev;

			namebuf[0] = 0;
			numbuf[0] = 0;

			if (!analog_start_cid_detect(p, p->cid_signalling)) {
				while (1) {
					res = analog_get_callerid(p, namebuf, numbuf, &ev, timeout - ast_tvdiff_ms(ast_tvnow(), start));

					if (res == 0) {
						break;
					}

					if (res == 1) {
						if (ev == ANALOG_EVENT_POLARITY && p->hanguponpolarityswitch && p->polarity == POLARITY_REV) {
							ast_debug(1, "Hanging up due to polarity reversal on channel %d while detecting callerid\n", p->channel);
							p->polarity = POLARITY_IDLE;
							ast_hangup(chan);
							goto quit;
						} else if (ev != ANALOG_EVENT_NONE) {
							break;
						}
					}

					if (ast_tvdiff_ms(ast_tvnow(), start) > timeout)
						break;

				}
				name = namebuf;
				number = numbuf;

				analog_stop_cid_detect(p);

#if 0
			/* XXX */
			if (strcmp(p->context,p->defcontext) != 0) {
				ast_copy_string(p->context, p->defcontext, sizeof(p->context));
				ast_copy_string(chan->context,p->defcontext,sizeof(chan->context));
			}

			analog_get_callerid(p, name, number);
			/* FSK Bell202 callerID */
			cs = callerid_new(p->cid_signalling);
			if (cs) {
#if 1
				bump_gains(p);
#endif				
				samples = 0;
				len = 0;
				distMatches = 0;
				/* Clear the current ring data array so we dont have old data in it. */
				for (receivedRingT = 0; receivedRingT < (sizeof(curRingData) / sizeof(curRingData[0])); receivedRingT++)
					curRingData[receivedRingT] = 0;
				receivedRingT = 0;
				counter = 0;
				counter1 = 0;
				/* Check to see if context is what it should be, if not set to be. */

				/* Take out of linear mode for Caller*ID processing */
				dahdi_setlinear(p->subs[index].dfd, 0);
				for (;;) {	
					i = DAHDI_IOMUX_READ | DAHDI_IOMUX_SIGEVENT;
					if ((res = ioctl(p->subs[index].dfd, DAHDI_IOMUX, &i)))	{
						ast_log(LOG_WARNING, "I/O MUX failed: %s\n", strerror(errno));
						callerid_free(cs);
						ast_hangup(chan);
						goto quit;
					}
					if (i & DAHDI_IOMUX_SIGEVENT) {
						res = dahdi_get_event(p->subs[index].dfd);
						ast_log(LOG_NOTICE, "Got event %d (%s)...\n", res, event2str(res));
						/* If we get a PR event, they hung up while processing calerid */
						if ( res == ANALOG_EVENT_POLARITY && p->hanguponpolarityswitch && p->polarity == POLARITY_REV) {
							ast_debug(1, "Hanging up due to polarity reversal on channel %d while detecting callerid\n", p->channel);
							p->polarity = POLARITY_IDLE;
							callerid_free(cs);
							ast_hangup(chan);
							goto quit;
						}
						res = 0;
						/* Let us detect callerid when the telco uses distinctive ring */

						curRingData[receivedRingT] = p->ringt;

						if (p->ringt < p->ringt_base/2)
							break;
						/* Increment the ringT counter so we can match it against
						   values in chan_dahdi.conf for distinctive ring */
						if (++receivedRingT == (sizeof(curRingData) / sizeof(curRingData[0])))
							break;
					} else if (i & DAHDI_IOMUX_READ) {
						res = read(p->subs[index].dfd, buf, sizeof(buf));
						if (res < 0) {
							if (errno != ELAST) {
								ast_log(LOG_WARNING, "read returned error: %s\n", strerror(errno));
								callerid_free(cs);
								ast_hangup(chan);
								goto quit;
							}
							break;
						}
						if (p->ringt) 
							p->ringt--;
						if (p->ringt == 1) {
							res = -1;
							break;
						}
						samples += res;
						res = callerid_feed(cs, buf, res, AST_LAW(p));
						if (res < 0) {
							ast_log(LOG_WARNING, "CallerID feed failed: %s\n", strerror(errno));
							break;
						} else if (res)
							break;
						else if (samples > (8000 * 10))
							break;
					}
				}
				if (res == 1) {
					callerid_get(cs, &name, &number, &flags);
					ast_debug(1, "CallerID number: %s, name: %s, flags=%d\n", number, name, flags);
				}
				/* Restore linear mode (if appropriate) for Caller*ID processing */
				dahdi_setlinear(p->subs[index].dfd, p->subs[index].linear);
#if 1
				restore_gains(p);
#endif				
				if (res < 0) {
					ast_log(LOG_WARNING, "CallerID returned with error on channel '%s'\n", chan->name);
				}
			} else
				ast_log(LOG_WARNING, "Unable to get caller ID space\n");
#endif
			} else
				ast_log(LOG_WARNING, "Unable to get caller ID space\n");
		}
		else
			cs = NULL;

		if (number)
			ast_shrink_phone_number(number);
		ast_set_callerid(chan, number, name, number);

		if (cs)
			callerid_free(cs);

		analog_handle_notify_message(chan, p, flags, -1);

		ast_setstate(chan, AST_STATE_RING);
		chan->rings = 1;
#if 0
		p->ringt = p->ringt_base;
#endif
		res = ast_pbx_run(chan);
		if (res) {
			ast_hangup(chan);
			ast_log(LOG_WARNING, "PBX exited non-zero\n");
		}
		goto quit;
	default:
		ast_log(LOG_WARNING, "Don't know how to handle simple switch with signalling %s on channel %d\n", analog_sigtype_to_str(p->sig), p->channel);
		res = analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
		if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	res = analog_play_tone(p, index, ANALOG_TONE_CONGESTION);
	if (res < 0)
			ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
quit:
	analog_decrease_ss_count(p);
	return NULL;
}

int analog_ss_thread_start(struct analog_pvt *p, struct ast_channel *chan)
{
	pthread_t threadid;
	return ast_pthread_create_detached(&threadid, NULL, __analog_ss_thread, chan);
}

static struct ast_frame *__analog_handle_event(struct analog_pvt *p, struct ast_channel *ast)
{
	int res, x;
	int mysig;
	enum analog_sub index;
	char *c;
	pthread_t threadid;
	pthread_attr_t attr;
	struct ast_channel *chan;
	struct ast_frame *f;
	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);

	index = analog_get_index(ast, p, 0);
	mysig = p->sig;
	if (p->outsigmod > -1)
		mysig = p->outsigmod;
	p->subs[index].f.frametype = AST_FRAME_NULL;
	p->subs[index].f.subclass = 0;
	p->subs[index].f.datalen = 0;
	p->subs[index].f.samples = 0;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = 0;
	p->subs[index].f.src = "dahdi_handle_event";
	p->subs[index].f.data.ptr = NULL;
	f = &p->subs[index].f;

	if (index < 0)
		return &p->subs[index].f;

	if (index != ANALOG_SUB_REAL) {
		ast_log(LOG_ERROR, "We got an event on a non real sub.  Fix it!\n");
	}

	res = analog_get_event(p);

	ast_debug(1, "Got event %s(%d) on channel %d (index %d)\n", analog_event2str(res), res, p->channel, index);

	switch (res) {
#ifdef ANALOG_EVENT_EC_DISABLED
	case ANALOG_EVENT_EC_DISABLED:
		ast_verb(3, "Channel %d echo canceler disabled due to CED detection\n", p->channel);
		p->echocanon = 0;
		break;
#endif
	case ANALOG_EVENT_PULSE_START:
		/* Stop tone if there's a pulse start and the PBX isn't started */
		if (!ast->pbx)
			analog_play_tone(p, ANALOG_SUB_REAL, -1);
		break;
	case ANALOG_EVENT_DIALCOMPLETE:
		if (p->inalarm) break;
		x = analog_is_dialing(p, index);
		if (!x) { /* if not still dialing in driver */
			analog_set_echocanceller(p, 1);
			if (p->echobreak) {
				analog_train_echocanceller(p);
				ast_copy_string(p->dop.dialstr, p->echorest, sizeof(p->dop.dialstr));
				p->dop.op = ANALOG_DIAL_OP_REPLACE;
				analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop);
				p->echobreak = 0;
			} else {
				p->dialing = 0;
				if ((mysig == ANALOG_SIG_E911) || (mysig == ANALOG_SIG_FGC_CAMA) || (mysig == ANALOG_SIG_FGC_CAMAMF)) {
					/* if thru with dialing after offhook */
					if (ast->_state == AST_STATE_DIALING_OFFHOOK) {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[index].f.frametype = AST_FRAME_CONTROL;
						p->subs[index].f.subclass = AST_CONTROL_ANSWER;
						break;
					} else { /* if to state wait for offhook to dial rest */
						/* we now wait for off hook */
						ast_setstate(ast,AST_STATE_DIALING_OFFHOOK);
					}
				}
				if (ast->_state == AST_STATE_DIALING) {
					if ((!p->dialednone && ((mysig == ANALOG_SIG_EM) || (mysig == ANALOG_SIG_EM_E1) ||  (mysig == ANALOG_SIG_EMWINK) || (mysig == ANALOG_SIG_FEATD) || (mysig == ANALOG_SIG_FEATDMF_TA) || (mysig == ANALOG_SIG_FEATDMF) || (mysig == ANALOG_SIG_E911) || (mysig == ANALOG_SIG_FGC_CAMA) || (mysig == ANALOG_SIG_FGC_CAMAMF) || (mysig == ANALOG_SIG_FEATB) || (mysig == ANALOG_SIG_SF) || (mysig == ANALOG_SIG_SFWINK) || (mysig == ANALOG_SIG_SF_FEATD) || (mysig == ANALOG_SIG_SF_FEATDMF) || (mysig == ANALOG_SIG_SF_FEATB)))) {
						ast_setstate(ast, AST_STATE_RINGING);
					} else if (!p->answeronpolarityswitch) {
						ast_setstate(ast, AST_STATE_UP);
						p->subs[index].f.frametype = AST_FRAME_CONTROL;
						p->subs[index].f.subclass = AST_CONTROL_ANSWER;
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
		p->inalarm = 1;
#if 0
		res = get_alarms(p);
		handle_alarms(p, res);	
#endif
	case ANALOG_EVENT_ONHOOK:
		switch (p->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
			/* Check for some special conditions regarding call waiting */
			if (index == ANALOG_SUB_REAL) {
				/* The normal line was hung up */
				if (p->subs[ANALOG_SUB_CALLWAIT].owner) {
					/* There's a call waiting call, so ring the phone, but make it unowned in the mean time */
					analog_swap_subs(p, ANALOG_SUB_CALLWAIT, ANALOG_SUB_REAL);
					ast_verb(3, "Channel %d still has (callwait) call, ringing phone\n", p->channel);
					analog_unalloc_sub(p, ANALOG_SUB_CALLWAIT);
					analog_stop_callwait(p);
					p->owner = NULL;
					/* Don't start streaming audio yet if the incoming call isn't up yet */
					if (p->subs[ANALOG_SUB_REAL].owner->_state != AST_STATE_UP)
						p->dialing = 1;
					analog_ring(p);
				} else if (p->subs[ANALOG_SUB_THREEWAY].owner) {
					unsigned int mssinceflash;
					/* Here we have to retain the lock on both the main channel, the 3-way channel, and
					   the private structure -- not especially easy or clean */
					while (p->subs[ANALOG_SUB_THREEWAY].owner && ast_channel_trylock(p->subs[ANALOG_SUB_THREEWAY].owner)) {
						/* Yuck, didn't get the lock on the 3-way, gotta release everything and re-grab! */
						analog_unlock_private(p);
						CHANNEL_DEADLOCK_AVOIDANCE(ast);
						/* We can grab ast and p in that order, without worry.  We should make sure
						   nothing seriously bad has happened though like some sort of bizarre double
						   masquerade! */
						analog_lock_private(p);
						if (p->owner != ast) {
							ast_log(LOG_WARNING, "This isn't good...\n");
							return NULL;
						}
					}
					if (!p->subs[ANALOG_SUB_THREEWAY].owner) {
						ast_log(LOG_NOTICE, "Whoa, threeway disappeared kinda randomly.\n");
						return NULL;
					}
					mssinceflash = ast_tvdiff_ms(ast_tvnow(), p->flashtime);
					ast_debug(1, "Last flash was %d ms ago\n", mssinceflash);
					if (mssinceflash < MIN_MS_SINCE_FLASH) {
						/* It hasn't been long enough since the last flashook.  This is probably a bounce on
						   hanging up.  Hangup both channels now */
						if (p->subs[ANALOG_SUB_THREEWAY].owner)
							ast_queue_hangup(p->subs[ANALOG_SUB_THREEWAY].owner);
						ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
						ast_debug(1, "Looks like a bounced flash, hanging up both calls on %d\n", p->channel);
						ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
					} else if ((ast->pbx) || (ast->_state == AST_STATE_UP)) {
						if (p->transfer) {
							/* In any case this isn't a threeway call anymore */
							p->subs[ANALOG_SUB_REAL].inthreeway = 0;
							p->subs[ANALOG_SUB_THREEWAY].inthreeway = 0;
							/* Only attempt transfer if the phone is ringing; why transfer to busy tone eh? */
							if (!p->transfertobusy && ast->_state == AST_STATE_BUSY) {
								ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
								/* Swap subs and dis-own channel */
								analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
								p->owner = NULL;
								/* Ring the phone */
								analog_ring(p);
							} else {
								if ((res = analog_attempt_transfer(p)) < 0) {
									ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
									if (p->subs[ANALOG_SUB_THREEWAY].owner)
										ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
								} else if (res) {
									/* Don't actually hang up at this point */
									if (p->subs[ANALOG_SUB_THREEWAY].owner)
										ast_channel_unlock(&p->subs[ANALOG_SUB_THREEWAY].owner);
									break;
								}
							}
						} else {
							ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
							if (p->subs[ANALOG_SUB_THREEWAY].owner)
								ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
						}
					} else {
						ast_channel_unlock(p->subs[ANALOG_SUB_THREEWAY].owner);
						/* Swap subs and dis-own channel */
						analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
						p->owner = NULL;
						/* Ring the phone */
						analog_ring(p);
					}
				}
			} else {
				ast_log(LOG_WARNING, "Got a hangup and my index is %d?\n", index);
			}
			/* Fall through */
		default:
			analog_set_echocanceller(p, 0);
			return NULL;
		}
		break;
	case ANALOG_EVENT_RINGOFFHOOK:
		if (p->inalarm) break;
		/* for E911, its supposed to wait for offhook then dial
		   the second half of the dial string */
		if (((mysig == ANALOG_SIG_E911) || (mysig == ANALOG_SIG_FGC_CAMA) || (mysig == ANALOG_SIG_FGC_CAMAMF)) && (ast->_state == AST_STATE_DIALING_OFFHOOK)) {
			c = strchr(p->dialdest, '/');
			if (c)
				c++;
			else
				c = p->dialdest;
			if (*c) snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*0%s#", c);
			else ast_copy_string(p->dop.dialstr,"M*2#", sizeof(p->dop.dialstr));
			if (strlen(p->dop.dialstr) > 4) {
				memset(p->echorest, 'w', sizeof(p->echorest) - 1);
				strcpy(p->echorest + (p->echotraining / 401) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
				p->echorest[sizeof(p->echorest) - 1] = '\0';
				p->echobreak = 1;
				p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
			} else
				p->echobreak = 0;
			if (analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop)) {
				int saveerr = errno;
				analog_on_hook(p);
				ast_log(LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(saveerr));
				return NULL;
			}
			p->dialing = 1;
			return &p->subs[index].f;
		}
		switch (p->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
			switch (ast->_state) {
			case AST_STATE_RINGING:
				analog_set_echocanceller(p, 1);
				analog_train_echocanceller(p);
				p->subs[index].f.frametype = AST_FRAME_CONTROL;
				p->subs[index].f.subclass = AST_CONTROL_ANSWER;
				/* Make sure it stops ringing */
				analog_off_hook(p);
				ast_debug(1, "channel %d answered\n", p->channel);
				p->dialing = 0;
				p->callwaitcas = 0;
				if (!ast_strlen_zero(p->dop.dialstr)) {
					/* nick@dccinc.com 4/3/03 - fxo should be able to do deferred dialing */
					res = analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop);
					if (res < 0) {
						ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
						p->dop.dialstr[0] = '\0';
						return NULL;
					} else {
						ast_debug(1, "Sent FXO deferred digit string: %s\n", p->dop.dialstr);
						p->subs[index].f.frametype = AST_FRAME_NULL;
						p->subs[index].f.subclass = 0;
						p->dialing = 1;
					}
					p->dop.dialstr[0] = '\0';
					ast_setstate(ast, AST_STATE_DIALING);
				} else
					ast_setstate(ast, AST_STATE_UP);
				return &p->subs[index].f;
			case AST_STATE_DOWN:
				ast_setstate(ast, AST_STATE_RING);
				ast->rings = 1;
				p->subs[index].f.frametype = AST_FRAME_CONTROL;
				p->subs[index].f.subclass = AST_CONTROL_OFFHOOK;
				ast_debug(1, "channel %d picked up\n", p->channel);
				return &p->subs[index].f;
			case AST_STATE_UP:
				/* Make sure it stops ringing */
				analog_off_hook(p);
				/* Okay -- probably call waiting*/
				if (ast_bridged_channel(p->owner))
					ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
				break;
			case AST_STATE_RESERVED:
				/* Start up dialtone */
				if (analog_has_voicemail(p))
					res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_STUTTER);
				else
					res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_DIALTONE);
				break;
			default:
				ast_log(LOG_WARNING, "FXO phone off hook in weird state %d??\n", ast->_state);
			}
			break;
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
#if 0
			if (ast->_state == AST_STATE_RING) {
				p->ringt = p->ringt_base;
			}
#endif

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
			if (ast->_state == AST_STATE_PRERING)
				ast_setstate(ast, AST_STATE_RING);
			if ((ast->_state == AST_STATE_DOWN) || (ast->_state == AST_STATE_RING)) {
				ast_debug(1, "Ring detected\n");
				p->subs[index].f.frametype = AST_FRAME_CONTROL;
				p->subs[index].f.subclass = AST_CONTROL_RING;
			} else if (p->outgoing && ((ast->_state == AST_STATE_RINGING) || (ast->_state == AST_STATE_DIALING))) {
				ast_debug(1, "Line answered\n");
				p->subs[index].f.frametype = AST_FRAME_CONTROL;
				p->subs[index].f.subclass = AST_CONTROL_ANSWER;
				ast_setstate(ast, AST_STATE_UP);
			} else if (ast->_state != AST_STATE_RING)
				ast_log(LOG_WARNING, "Ring/Off-hook in strange state %d on channel %d\n", ast->_state, p->channel);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/off hook for signalling %d\n", p->sig);
		}
		break;
#ifdef ANALOG_EVENT_RINGBEGIN
	case ANALOG_EVENT_RINGBEGIN:
		switch (p->sig) {
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
#if 0
			if (ast->_state == AST_STATE_RING) {
				p->ringt = p->ringt_base;
			}
#endif
			break;
		}
		break;
#endif
	case ANALOG_EVENT_RINGEROFF:
		if (p->inalarm) break;
		ast->rings++;
		if (ast->rings == p->cidrings) {
			analog_send_callerid(p, 0, &p->cid);
		}

		if (ast->rings > p->cidrings) {
			p->callwaitcas = 0;
		}
		p->subs[index].f.frametype = AST_FRAME_CONTROL;
		p->subs[index].f.subclass = AST_CONTROL_RINGING;
		break;
	case ANALOG_EVENT_RINGERON:
		break;
	case ANALOG_EVENT_NOALARM:
		p->inalarm = 0;
		if (!p->unknown_alarm) {
			ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
			manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
				"Channel: %d\r\n", p->channel);
		} else {
			p->unknown_alarm = 0;
		}
		break;
	case ANALOG_EVENT_WINKFLASH:
		if (p->inalarm) break;
		/* Remember last time we got a flash-hook */
		gettimeofday(&p->flashtime, NULL);
		switch (mysig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
#if 0
			ast_debug(1, "Winkflash, index: %d, normal: %d, callwait: %d, thirdcall: %d\n",
				index, p->subs[ANALOG_SUB_REAL].dfd, p->subs[ANALOG_SUB_CALLWAIT].dfd, p->subs[ANALOG_SUB_THREEWAY].dfd);
#endif
			p->callwaitcas = 0;

			if (index != ANALOG_SUB_REAL) {
				ast_log(LOG_WARNING, "Got flash hook with index %d on channel %d?!?\n", index, p->channel);
				goto winkflashdone;
			}

			if (p->subs[ANALOG_SUB_CALLWAIT].owner) {
				/* Swap to call-wait */
				int previous_state = p->subs[ANALOG_SUB_CALLWAIT].owner->_state;
				if (p->subs[ANALOG_SUB_CALLWAIT].owner->_state == AST_STATE_RINGING) {
					ast_setstate(p->subs[ANALOG_SUB_CALLWAIT].owner, AST_STATE_UP);
				}
				analog_swap_subs(p, ANALOG_SUB_REAL, ANALOG_SUB_CALLWAIT);
				analog_play_tone(p, ANALOG_SUB_REAL, -1);
				p->owner = p->subs[ANALOG_SUB_REAL].owner;
				ast_debug(1, "Making %s the new owner\n", p->owner->name);
				if (previous_state == AST_STATE_RINGING) {
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_ANSWER);
				}
				analog_stop_callwait(p);
				/* Start music on hold if appropriate */
				if (!p->subs[ANALOG_SUB_CALLWAIT].inthreeway && ast_bridged_channel(p->subs[ANALOG_SUB_CALLWAIT].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_CALLWAIT].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_ANSWER);
				if (ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner)) {
					ast_queue_control_data(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_HOLD,
						S_OR(p->mohsuggest, NULL),
						!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				}
				ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
			} else if (!p->subs[ANALOG_SUB_THREEWAY].owner) {
				char cid_num[256];
				char cid_name[256];

				if (!p->threewaycalling) {
					/* Just send a flash if no 3-way calling */
					ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_FLASH);
					goto winkflashdone;
				} else if (!analog_check_for_conference(p)) {
					if (p->dahditrcallerid && p->owner) {
						if (p->owner->cid.cid_num)
							ast_copy_string(cid_num, p->owner->cid.cid_num, sizeof(cid_num));
						if (p->owner->cid.cid_name)
							ast_copy_string(cid_name, p->owner->cid.cid_name, sizeof(cid_name));
					}
					/* XXX This section needs much more error checking!!! XXX */
					/* Start a 3-way call if feasible */
					if (!((ast->pbx) ||
					      (ast->_state == AST_STATE_UP) ||
					      (ast->_state == AST_STATE_RING))) {
						ast_debug(1, "Flash when call not up or ringing\n");
							goto winkflashdone;
					}
					if (analog_alloc_sub(p, ANALOG_SUB_THREEWAY)) {
						ast_log(LOG_WARNING, "Unable to allocate three-way subchannel\n");
						goto winkflashdone;
					}
					/* Make new channel */
					chan = analog_new_ast_channel(p, AST_STATE_RESERVED, 0, ANALOG_SUB_THREEWAY);
					if (p->dahditrcallerid) {
						if (!p->origcid_num)
							p->origcid_num = ast_strdup(p->cid_num);
						if (!p->origcid_name)
							p->origcid_name = ast_strdup(p->cid_name);
						ast_copy_string(p->cid_num, cid_num, sizeof(p->cid_num));
						ast_copy_string(p->cid_name, cid_name, sizeof(p->cid_name));
					}
					/* Swap things around between the three-way and real call */
					analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
					/* Disable echo canceller for better dialing */
					analog_set_echocanceller(p, 0);
					res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_DIALRECALL);
					if (res)
						ast_log(LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
					p->ss_astchan = p->owner = chan;
					pthread_attr_init(&attr);
					pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
					if (!chan) {
						ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", p->channel);
					} else if (ast_pthread_create(&threadid, &attr, __analog_ss_thread, p)) {
						ast_log(LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
						res = analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
						analog_set_echocanceller(p, 1);
						ast_hangup(chan);
					} else {
 						struct ast_channel *other = ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner);
 						int way3bridge = 0, cdr3way = 0;

						if (!other) {
							other = ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner);
						} else
							way3bridge = 1;

						if (p->subs[ANALOG_SUB_THREEWAY].owner->cdr)
							cdr3way = 1;
 						
						ast_verb(3, "Started three way call on channel %d\n", p->channel);
						/* Start music on hold if appropriate */
						if (ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner)) {
							ast_queue_control_data(p->subs[ANALOG_SUB_THREEWAY].owner, AST_CONTROL_HOLD,
								S_OR(p->mohsuggest, NULL),
								!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
						}
					}
					pthread_attr_destroy(&attr);
				}
			} else {
				/* Already have a 3 way call */
				if (p->subs[ANALOG_SUB_THREEWAY].inthreeway) {
					/* Call is already up, drop the last person */
					ast_debug(1, "Got flash with three way call up, dropping last call on %d\n", p->channel);
					/* If the primary call isn't answered yet, use it */
					if ((p->subs[ANALOG_SUB_REAL].owner->_state != AST_STATE_UP) && (p->subs[ANALOG_SUB_THREEWAY].owner->_state == AST_STATE_UP)) {
						/* Swap back -- we're dropping the real 3-way that isn't finished yet*/
						analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
						p->owner = p->subs[ANALOG_SUB_REAL].owner;
					}
					/* Drop the last call and stop the conference */
					ast_verb(3, "Dropping three-way call on %s\n", p->subs[ANALOG_SUB_THREEWAY].owner->name);
					ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
					p->subs[ANALOG_SUB_REAL].inthreeway = 0;
					p->subs[ANALOG_SUB_THREEWAY].inthreeway = 0;
				} else {
					/* Lets see what we're up to */
					if (((ast->pbx) || (ast->_state == AST_STATE_UP)) && 
					    (p->transfertobusy || (ast->_state != AST_STATE_BUSY))) {
						int otherindex = ANALOG_SUB_THREEWAY;
						struct ast_channel *other = ast_bridged_channel(p->subs[ANALOG_SUB_THREEWAY].owner);
						int way3bridge = 0, cdr3way = 0;

						if (!other) {
							other = ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner);
						} else
							way3bridge = 1;

						if (p->subs[ANALOG_SUB_THREEWAY].owner->cdr)
							cdr3way = 1;

						ast_verb(3, "Building conference on call on %s and %s\n", p->subs[ANALOG_SUB_THREEWAY].owner->name, p->subs[ANALOG_SUB_REAL].owner->name);
						/* Put them in the threeway, and flip */
						p->subs[ANALOG_SUB_THREEWAY].inthreeway = 1;
						p->subs[ANALOG_SUB_REAL].inthreeway = 1;
						if (ast->_state == AST_STATE_UP) {
							analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
							otherindex = ANALOG_SUB_REAL;
						}
						if (p->subs[otherindex].owner && ast_bridged_channel(p->subs[otherindex].owner))
							ast_queue_control(p->subs[otherindex].owner, AST_CONTROL_UNHOLD);
						p->owner = p->subs[ANALOG_SUB_REAL].owner;
						if (ast->_state == AST_STATE_RINGING) {
							ast_debug(1, "Enabling ringtone on real and threeway\n");
							analog_play_tone(p, ANALOG_SUB_REAL, ANALOG_TONE_RINGTONE);
							analog_play_tone(p, ANALOG_SUB_THREEWAY, ANALOG_TONE_RINGTONE);
						}
					} else {
						ast_verb(3, "Dumping incomplete call on on %s\n", p->subs[ANALOG_SUB_THREEWAY].owner->name);
						analog_swap_subs(p, ANALOG_SUB_THREEWAY, ANALOG_SUB_REAL);
						ast_softhangup_nolock(p->subs[ANALOG_SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
						p->owner = p->subs[ANALOG_SUB_REAL].owner;
						if (p->subs[ANALOG_SUB_REAL].owner && ast_bridged_channel(p->subs[ANALOG_SUB_REAL].owner))
							ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_UNHOLD);
						analog_set_echocanceller(p, 1);
					}
				}
			}
		winkflashdone:
			analog_update_conf(p);
			break;
		case ANALOG_SIG_EM:
		case ANALOG_SIG_EM_E1:
		case ANALOG_SIG_EMWINK:
		case ANALOG_SIG_FEATD:
		case ANALOG_SIG_SF:
		case ANALOG_SIG_SFWINK:
		case ANALOG_SIG_SF_FEATD:
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
			if (p->dialing)
				ast_debug(1, "Ignoring wink on channel %d\n", p->channel);
			else
				ast_debug(1, "Got wink in weird state %d on channel %d\n", ast->_state, p->channel);
			break;
		case ANALOG_SIG_FEATDMF_TA:
			switch (p->whichwink) {
			case 0:
				ast_debug(1, "ANI2 set to '%d' and ANI is '%s'\n", p->owner->cid.cid_ani2, p->owner->cid.cid_ani);
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%d%s#", p->owner->cid.cid_ani2, p->owner->cid.cid_ani);
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
			/* FGD MF *Must* wait for wink */
			if (!ast_strlen_zero(p->dop.dialstr)) {
				res = analog_dial_digits(p, ANALOG_SUB_REAL, &p->dop);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else
					ast_debug(1, "Sent deferred digit string on channel %d: %s\n", p->channel, p->dop.dialstr);
			}
			p->dop.dialstr[0] = '\0';
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
		}
		break;
	case ANALOG_EVENT_HOOKCOMPLETE:
		if (p->inalarm) break;
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
				} else 
					ast_debug(1, "Sent deferred digit string on channel %d: %s\n", p->channel, p->dop.dialstr);
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
		 * If we get a Polarity Switch event, check to see
		 * if we should change the polarity state and
		 * mark the channel as UP or if this is an indication
		 * of remote end disconnect.
		 */
		if (p->polarity == POLARITY_IDLE) {
			p->polarity = POLARITY_REV;
			if (p->answeronpolarityswitch &&
			    ((ast->_state == AST_STATE_DIALING) ||
				 (ast->_state == AST_STATE_RINGING))) {
				ast_debug(1, "Answering on polarity switch!\n");
				ast_setstate(p->owner, AST_STATE_UP);
				if (p->hanguponpolarityswitch) {
					gettimeofday(&p->polaritydelaytv, NULL);
				}
			} else
				ast_debug(1, "Ignore switch to REVERSED Polarity on channel %d, state %d\n", p->channel, ast->_state);
		}
		/* Removed else statement from here as it was preventing hangups from ever happening*/
		/* Added AST_STATE_RING in if statement below to deal with calling party hangups that take place when ringing */
		if (p->hanguponpolarityswitch &&
			(p->polarityonanswerdelay > 0) &&
		       (p->polarity == POLARITY_REV) &&
			((ast->_state == AST_STATE_UP) || (ast->_state == AST_STATE_RING)) ) {
                               /* Added log_debug information below to provide a better indication of what is going on */
			ast_debug(1, "Polarity Reversal event occured - DEBUG 1: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %d\n", p->channel, ast->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );

			if (ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) > p->polarityonanswerdelay) {
				ast_debug(1, "Polarity Reversal detected and now Hanging up on channel %d\n", p->channel);
				ast_softhangup(p->owner, AST_SOFTHANGUP_EXPLICIT);
				p->polarity = POLARITY_IDLE;
			} else {
				ast_debug(1, "Polarity Reversal detected but NOT hanging up (too close to answer event) on channel %d, state %d\n", p->channel, ast->_state);
			}
		} else {
			p->polarity = POLARITY_IDLE;
			ast_debug(1, "Ignoring Polarity switch to IDLE on channel %d, state %d\n", p->channel, ast->_state);
		}
		/* Added more log_debug information below to provide a better indication of what is going on */
		ast_debug(1, "Polarity Reversal event occured - DEBUG 2: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %d\n", p->channel, ast->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
		break;
	default:
		ast_debug(1, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[index].f;
}

struct ast_frame *analog_exception(struct analog_pvt *p, struct ast_channel *ast)
{
	int res;
	int usedindex=-1;
	int index;
	struct ast_frame *f;

	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);

	index = analog_get_index(ast, p, 1);

	p->subs[index].f.frametype = AST_FRAME_NULL;
	p->subs[index].f.datalen = 0;
	p->subs[index].f.samples = 0;
	p->subs[index].f.mallocd = 0;
	p->subs[index].f.offset = 0;
	p->subs[index].f.subclass = 0;
	p->subs[index].f.delivery = ast_tv(0,0);
	p->subs[index].f.src = "dahdi_exception";
	p->subs[index].f.data.ptr = NULL;


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
			p->owner = p->subs[ANALOG_SUB_REAL].owner;
			if (p->owner && ast_bridged_channel(p->owner))
				ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
		}
		switch (res) {
		case ANALOG_EVENT_ONHOOK:
			analog_set_echocanceller(p, 0);
			if (p->owner) {
				ast_verb(3, "Channel %s still has call, ringing phone\n", p->owner->name);
				analog_ring(p);
				analog_stop_callwait(p);
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			analog_update_conf(p);
			break;
		case ANALOG_EVENT_RINGOFFHOOK:
			analog_set_echocanceller(p, 1);
			analog_off_hook(p);
			if (p->owner && (p->owner->_state == AST_STATE_RINGING)) {
				ast_queue_control(p->subs[ANALOG_SUB_REAL].owner, AST_CONTROL_ANSWER);
				p->dialing = 0;
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
				ast_verb(3, "Channel %d flashed to other channel %s\n", p->channel, p->owner->name);
				if (p->owner->_state != AST_STATE_UP) {
					/* Answer if necessary */
					usedindex = analog_get_index(p->owner, p, 0);
					if (usedindex > -1) {
						ast_queue_control(p->subs[usedindex].owner, AST_CONTROL_ANSWER);
					}
					ast_setstate(p->owner, AST_STATE_UP);
				}
				analog_stop_callwait(p);
				if (ast_bridged_channel(p->owner))
					ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			analog_update_conf(p);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", analog_event2str(res));
		}
		f = &p->subs[index].f;
		return f;
	}
	ast_debug(1, "Exception on %d, channel %d\n", ast->fds[0],p->channel);
	/* If it's not us, return NULL immediately */
	if (ast != p->owner) {
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast->name, p->owner->name);
		f = &p->subs[index].f;
		return f;
	}
	f = __analog_handle_event(p, ast);
	return f;
}

int analog_handle_init_event(struct analog_pvt *i, int event)
{
	int res;
	pthread_t threadid;
	pthread_attr_t attr;
	struct ast_channel *chan;

	ast_debug(1, "channel (%d) - signaling (%d) - event (%s)\n",
				i->channel, i->sig, analog_event2str(event));

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	/* Handle an event on a given channel for the monitor thread. */
	switch (event) {
	case ANALOG_EVENT_WINKFLASH:
	case ANALOG_EVENT_RINGOFFHOOK:
		if (i->inalarm) break;
		/* Got a ring/answer.  What kind of channel are we? */
		switch (i->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
		case ANALOG_SIG_FXOKS:
			res = analog_off_hook(i);
			if (res && (errno == EBUSY))
				break;
			if (i->immediate) {
				analog_set_echocanceller(i, 1);
				/* The channel is immediately up.  Start right away */
				res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_RINGTONE);
				chan = analog_new_ast_channel(i, AST_STATE_RING, 1, ANALOG_SUB_REAL);
				if (!chan) {
					ast_log(LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = analog_new_ast_channel(i, AST_STATE_RESERVED, 0, ANALOG_SUB_REAL);
				i->ss_astchan = chan;
				if (chan) {
					if (analog_has_voicemail(i))
						res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_STUTTER);
					else
						res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_DIALTONE);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play dialtone on channel %d, do you have defaultzone and loadzone defined?\n", i->channel);
					if (ast_pthread_create(&threadid, &attr, __analog_ss_thread, i)) {
						ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
						res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
						if (res < 0)
							ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
						ast_hangup(chan);
					}
				} else
					ast_log(LOG_WARNING, "Unable to create channel\n");
			}
			break;
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSGS:
		case ANALOG_SIG_FXSKS:
#if 0
				i->ringt = i->ringt_base;
#endif
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
				/* Check for callerid, digits, etc */
				if (i->cid_start == ANALOG_CID_START_POLARITY_IN) {
					chan = analog_new_ast_channel(i, AST_STATE_PRERING, 0, ANALOG_SUB_REAL);
				} else {
					chan = analog_new_ast_channel(i, AST_STATE_RING, 0, ANALOG_SUB_REAL);
				}
				i->ss_astchan = chan;
				if (chan && ast_pthread_create(&threadid, &attr, __analog_ss_thread, i)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
					if (res < 0)
						ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					ast_hangup(chan);
				} else if (!chan) {
					ast_log(LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
				}
				break;
		default:
			ast_log(LOG_WARNING, "Don't know how to handle ring/answer with signalling %s on channel %d\n", analog_sigtype_to_str(i->sig), i->channel);
			res = analog_play_tone(i, ANALOG_SUB_REAL, ANALOG_TONE_CONGESTION);
			if (res < 0)
					ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
			return -1;
		}
		break;
	case ANALOG_EVENT_NOALARM:
		i->inalarm = 0;
		if (!i->unknown_alarm) {
			ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", i->channel);
			manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
				      "Channel: %d\r\n", i->channel);
		} else {
			i->unknown_alarm = 0;
		}
		break;
	case ANALOG_EVENT_ALARM:
		i->inalarm = 1;
#if 0
		res = get_alarms(i);
		handle_alarms(i, res);	
#endif
		/* fall thru intentionally */
	case ANALOG_EVENT_ONHOOK:
		/* Back on hook.  Hang up. */
		switch (i->sig) {
		case ANALOG_SIG_FXOLS:
		case ANALOG_SIG_FXOGS:
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
			return -1;
		}
		break;
	case ANALOG_EVENT_POLARITY:
		switch (i->sig) {
		case ANALOG_SIG_FXSLS:
		case ANALOG_SIG_FXSKS:
		case ANALOG_SIG_FXSGS:
			/* We have already got a PR before the channel was
			   created, but it wasn't handled. We need polarity
			   to be REV for remote hangup detection to work.
			   At least in Spain */
			if (i->hanguponpolarityswitch)
				i->polarity = POLARITY_REV;

			if (i->cid_start == ANALOG_CID_START_POLARITY || i->cid_start == ANALOG_CID_START_POLARITY_IN) {
				i->polarity = POLARITY_REV;
				ast_verbose(VERBOSE_PREFIX_2 "Starting post polarity "
					    "CID detection on channel %d\n",
					    i->channel);
				chan = analog_new_ast_channel(i, AST_STATE_PRERING, 0, ANALOG_SUB_REAL);
				i->ss_astchan = chan;
				if (chan && ast_pthread_create(&threadid, &attr, __analog_ss_thread, i)) {
					ast_log(LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
				}
			}
			break;
		default:
			ast_log(LOG_WARNING, "handle_init_event detected "
				"polarity reversal on non-FXO (ANALOG_SIG_FXS) "
				"interface %d\n", i->channel);
		}
		break;
	case ANALOG_EVENT_NEONMWI_ACTIVE:
		analog_handle_notify_message(NULL, i, -1, ANALOG_EVENT_NEONMWI_ACTIVE);
		break;
	case ANALOG_EVENT_NEONMWI_INACTIVE:
		analog_handle_notify_message(NULL, i, -1, ANALOG_EVENT_NEONMWI_INACTIVE);
		break;
	}
	pthread_attr_destroy(&attr);
	return 0;
}


struct analog_pvt * analog_new(enum analog_sigtype signallingtype, struct analog_callback *c, void *private_data)
{
	struct analog_pvt *p;

	p = ast_calloc(1, sizeof(*p));

	if (!p)
		return p;

	p->calls = c;
	p->outsigmod = ANALOG_SIG_NONE;
	p->sig = signallingtype;
	p->chan_pvt = private_data;

	/* Some defaults for values */
	p->sendcalleridafter = 1;
	p->cid_start = ANALOG_CID_START_RING;
	p->cid_signalling = CID_SIG_BELL;
	/* Sub real is assumed to always be alloc'd */
	p->subs[ANALOG_SUB_REAL].allocd = 1;

	return p;
}

int analog_config_complete(struct analog_pvt *p)
{
	/* No call waiting on non FXS channels */
	if ((p->sig != ANALOG_SIG_FXOKS) && (p->sig != ANALOG_SIG_FXOLS) && (p->sig != ANALOG_SIG_FXOGS))
		p->permcallwaiting = 0;

	p->callwaiting = p->permcallwaiting;

	return 0;
}

void analog_free(struct analog_pvt *p)
{
	free(p);
}

/* called while dahdi_pvt is locked in dahdi_fixup */
int analog_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, void *newp)
{
	struct analog_pvt *new_pvt = newp;
	int x;
	ast_debug(1, "New owner for channel %d is %s\n", new_pvt->channel, newchan->name);
	if (new_pvt->owner == oldchan) {
		new_pvt->owner = newchan;
	}
	for (x = 0; x < 3; x++)
		if (new_pvt->subs[x].owner == oldchan) {
			new_pvt->subs[x].owner = newchan;
		}

	analog_update_conf(new_pvt);
	return 0;
}
