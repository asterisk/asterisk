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


#include "asterisk.h"

#ifdef HAVE_PRI

#include <errno.h>
#include <ctype.h>
#include <signal.h>

#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
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

#include "sig_pri.h"

/* define this to send PRI user-user information elements */
#undef SUPPORT_USERUSER

#if 0
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE)
#else
#define DEFAULT_PRI_DEBUG 0
#endif

static int pri_matchdigittimeout = 3000;

static int pri_gendigittimeout = 8000;

#define DCHAN_NOTINALARM  (1 << 0)
#define DCHAN_UP          (1 << 1)

#define PRI_CHANNEL(p) ((p) & 0xff)
#define PRI_SPAN(p) (((p) >> 8) & 0xff)
#define PRI_EXPLICIT(p) (((p) >> 16) & 0x01)


#define DCHAN_AVAILABLE	(DCHAN_NOTINALARM | DCHAN_UP)

#define PRI_DEADLOCK_AVOIDANCE(p) \
	do { \
		sig_pri_unlock_private(p); \
		usleep(1); \
		sig_pri_lock_private(p); \
	} while (0)

static int pri_active_dchan_index(struct sig_pri_pri *pri);

static inline void pri_rel(struct sig_pri_pri *pri)
{
	ast_mutex_unlock(&pri->lock);
}

static unsigned int PVT_TO_CHANNEL(struct sig_pri_chan *p)
{
	int res = (((p)->prioffset) | ((p)->logicalspan << 8) | (p->mastertrunkgroup ? 0x10000 : 0));
	ast_debug(5, "prioffset: %d mastertrunkgroup: %d logicalspan: %d result: %d\n",
		p->prioffset, p->mastertrunkgroup, p->logicalspan, res);

	return res;
}

static void sig_pri_handle_dchan_exception(struct sig_pri_pri *pri, int index)
{
	if (pri->calls->handle_dchan_exception)
		pri->calls->handle_dchan_exception(pri, index);
}

static void sig_pri_set_dialing(struct sig_pri_chan *p, int flag)
{
	if (p->calls->set_dialing)
		p->calls->set_dialing(p->chan_pvt, flag);
}

/*!
 * \internal
 * \brief Set the caller id information in the parent module.
 * \since 1.6.3
 *
 * \param p sig_pri channel structure.
 *
 * \return Nothing
 */
static void sig_pri_set_caller_id(struct sig_pri_chan *p)
{
	struct ast_party_caller caller;

	if (p->calls->set_callerid) {
		ast_party_caller_init(&caller);
		caller.id.number = p->cid_num;
		caller.id.name = p->cid_name;
		caller.id.number_type = p->cid_ton;
		caller.id.number_presentation = p->callingpres;
		caller.ani = p->cid_ani;
		caller.ani2 = p->cid_ani2;
		p->calls->set_callerid(p->chan_pvt, &caller);
	}
}

/*!
 * \internal
 * \brief Set the Dialed Number Identifier.
 * \since 1.6.3
 *
 * \param p sig_pri channel structure.
 * \param dnid Dialed Number Identifier string.
 *
 * \return Nothing
 */
static void sig_pri_set_dnid(struct sig_pri_chan *p, const char *dnid)
{
	if (p->calls->set_dnid) {
		p->calls->set_dnid(p->chan_pvt, dnid);
	}
}

/*!
 * \internal
 * \brief Set the Redirecting Directory Number Information Service (RDNIS).
 * \since 1.6.3
 *
 * \param p sig_pri channel structure.
 * \param rdnis Redirecting Directory Number Information Service (RDNIS) string.
 *
 * \return Nothing
 */
static void sig_pri_set_rdnis(struct sig_pri_chan *p, const char *rdnis)
{
	if (p->calls->set_rdnis) {
		p->calls->set_rdnis(p->chan_pvt, rdnis);
	}
}

static void sig_pri_unlock_private(struct sig_pri_chan *p)
{
	if (p->calls->unlock_private)
		p->calls->unlock_private(p->chan_pvt);
}

static void sig_pri_lock_private(struct sig_pri_chan *p)
{
	if (p->calls->lock_private)
		p->calls->lock_private(p->chan_pvt);
}

static inline int pri_grab(struct sig_pri_chan *p, struct sig_pri_pri *pri)
{
	int res;
	/* Grab the lock first */
	do {
		res = ast_mutex_trylock(&pri->lock);
		if (res) {
			PRI_DEADLOCK_AVOIDANCE(p);
		}
	} while (res);
	/* Then break the poll */
	pthread_kill(pri->master, SIGURG);
	return 0;
}

static int sig_pri_set_echocanceller(struct sig_pri_chan *p, int enable)
{
	if (p->calls->set_echocanceller)
		return p->calls->set_echocanceller(p->chan_pvt, enable);
	else
		return -1;
}

static void sig_pri_fixup_chans(struct sig_pri_chan *old, struct sig_pri_chan *new)
{
	if (old->calls->fixup_chans)
		old->calls->fixup_chans(old->chan_pvt, new->chan_pvt);
}

static int sig_pri_play_tone(struct sig_pri_chan *p, enum sig_pri_tone tone)
{
	if (p->calls->play_tone)
		return p->calls->play_tone(p->chan_pvt, tone);
	else
		return -1;
}

static struct ast_channel *sig_pri_new_ast_channel(struct sig_pri_chan *p, int state, int startpbx, int ulaw, int transfercapability, char *exten, const struct ast_channel *requestor)
{
	struct ast_channel *c;

	if (p->calls->new_ast_channel)
		c = p->calls->new_ast_channel(p->chan_pvt, state, startpbx, ulaw, transfercapability, exten, requestor);
	else
		return NULL;

	if (!p->owner)
		p->owner = c;
	p->isidlecall = 0;
	p->alreadyhungup = 0;

	return c;
}

struct ast_channel *sig_pri_request(struct sig_pri_chan *p, enum sig_pri_law law, const struct ast_channel *requestor)
{
	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);

	return sig_pri_new_ast_channel(p, AST_STATE_RESERVED, 0, law, 0, p->exten, requestor);
}

int pri_is_up(struct sig_pri_pri *pri)
{
	int x;
	for (x = 0; x < NUM_DCHANS; x++) {
		if (pri->dchanavail[x] == DCHAN_AVAILABLE)
			return 1;
	}
	return 0;
}

static char *pri_order(int level)
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
static int pri_active_dchan_index(struct sig_pri_pri *pri)
{
	int x;

	for (x = 0; x < NUM_DCHANS; x++) {
		if ((pri->dchans[x] == pri->pri))
			return x;
	}

	ast_log(LOG_WARNING, "No active dchan found!\n");
	return -1;
}

static int pri_find_dchan(struct sig_pri_pri *pri)
{
	int oldslot = -1;
	struct pri *old;
	int newslot = -1;
	int x;
	old = pri->pri;
	for (x = 0; x < NUM_DCHANS; x++) {
		if ((pri->dchanavail[x] == DCHAN_AVAILABLE) && (newslot < 0))
			newslot = x;
		if (pri->dchans[x] == old) {
			oldslot = x;
		}
	}
	if (newslot < 0) {
		newslot = 0;
		/* This is annoying to see on non persistent layer 2 connections.  Let's not complain in that case */
		if (pri->sig != SIG_BRI_PTMP) {
			ast_log(LOG_WARNING, "No D-channels available!  Using Primary channel as D-channel anyway!\n");
		}
	}
	if (old && (oldslot != newslot))
		ast_log(LOG_NOTICE, "Switching from d-channel fd %d to fd %d!\n",
			pri->fds[oldslot], pri->fds[newslot]);
	pri->pri = pri->dchans[newslot];
	return 0;
}
static void pri_update_cid(struct sig_pri_chan *p, struct sig_pri_pri *pri)
{
	/* We must unlock the PRI to avoid the possibility of a deadlock */
	if (pri)
		ast_mutex_unlock(&pri->lock);
	for (;;) {
		if (p->owner) {
			if (ast_channel_trylock(p->owner)) {
				PRI_DEADLOCK_AVOIDANCE(p);
			} else {
				ast_set_callerid(p->owner, S_OR(p->lastcid_num, NULL),
							S_OR(p->lastcid_name, NULL),
							S_OR(p->lastcid_num, NULL)
							);
				ast_channel_unlock(p->owner);
				break;
			}
		} else
			break;
	}
	if (pri)
		ast_mutex_lock(&pri->lock);
}

static void pri_queue_frame(struct sig_pri_chan *p, struct ast_frame *f, struct sig_pri_pri *pri)
{
	/* We must unlock the PRI to avoid the possibility of a deadlock */
	if (pri)
		ast_mutex_unlock(&pri->lock);
	for (;;) {
		if (p->owner) {
			if (ast_channel_trylock(p->owner)) {
				PRI_DEADLOCK_AVOIDANCE(p);
			} else {
				ast_queue_frame(p->owner, f);
				ast_channel_unlock(p->owner);
				break;
			}
		} else
			break;
	}
	if (pri)
		ast_mutex_lock(&pri->lock);
}

static void pri_queue_control(struct sig_pri_chan *p, int subclass, struct sig_pri_pri *pri)
{
	struct ast_frame f = {AST_FRAME_CONTROL, };

	f.subclass = subclass;
	pri_queue_frame(p, &f, pri);
}

static int pri_find_principle(struct sig_pri_pri *pri, int channel)
{
	int x;
	int span = PRI_SPAN(channel);
	int principle = -1;
	int explicit = PRI_EXPLICIT(channel);
	channel = PRI_CHANNEL(channel);

	if (!explicit) {
		int index = pri_active_dchan_index(pri);
		if (index == -1)
			return -1;
		span = pri->dchan_logical_span[index];
	}

	for (x = 0; x < pri->numchans; x++) {
		if (pri->pvts[x] && (pri->pvts[x]->prioffset == channel) && (pri->pvts[x]->logicalspan == span)) {
			principle = x;
			break;
		}
	}

	return principle;
}

static int pri_fixup_principle(struct sig_pri_pri *pri, int principle, q931_call *c)
{
	int x;
	if (!c) {
		if (principle < 0)
			return -1;
		return principle;
	}
	if ((principle > -1) &&
		(principle < pri->numchans) &&
		(pri->pvts[principle]) &&
		(pri->pvts[principle]->call == c))
		return principle;
	/* First, check for other bearers */
	for (x = 0; x < pri->numchans; x++) {
		if (!pri->pvts[x])
			continue;
		if (pri->pvts[x]->call == c) {
			/* Found our call */
			if (principle != x) {
				struct sig_pri_chan *new = pri->pvts[principle], *old = pri->pvts[x];

				ast_verb(3, "Moving call from channel %d to channel %d\n",
					old->channel, new->channel);
				if (new->owner) {
					ast_log(LOG_WARNING, "Can't fix up channel from %d to %d because %d is already in use\n",
						old->channel, new->channel, new->channel);
					return -1;
				}

				sig_pri_fixup_chans(old, new);
				/* Fix it all up now */
				new->owner = old->owner;
				old->owner = NULL;

				new->call = old->call;
				old->call = NULL;

			}
			return principle;
		}
	}
	ast_log(LOG_WARNING, "Call specified, but not found?\n");
	return -1;
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

static void apply_plan_to_number(char *buf, size_t size, const struct sig_pri_pri *pri, const char *number, const int plan)
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

static int pri_check_restart(struct sig_pri_pri *pri)
{
#ifdef HAVE_PRI_SERVICE_MESSAGES
tryanotherpos:
#endif
	do {
		pri->resetpos++;
	} while ((pri->resetpos < pri->numchans) &&
		(!pri->pvts[pri->resetpos] ||
		pri->pvts[pri->resetpos]->call ||
		pri->pvts[pri->resetpos]->resetting));
	if (pri->resetpos < pri->numchans) {
#ifdef HAVE_PRI_SERVICE_MESSAGES
		char db_chan_name[20], db_answer[5], state;
		int why;

		/* check if the channel is out of service */
		ast_mutex_lock(&pri->pvts[pri->resetpos]->service_lock);
		snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, pri->pvts[pri->resetpos]->pri->span, pri->pvts[pri->resetpos]->channel);
		ast_mutex_unlock(&pri->pvts[pri->resetpos]->service_lock);

		/* if so, try next channel */
		if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
			sscanf(db_answer, "%c:%d", &state, &why);
			if (why) {
				ast_log(LOG_NOTICE, "span '%d' channel '%d' out-of-service (reason: %s), not sending RESTART\n", pri->span,
				pri->pvts[pri->resetpos]->channel, (why & SRVST_FAREND) ? (why & SRVST_NEAREND) ? "both ends" : "far end" : "near end");
				goto tryanotherpos;
			}
		}
#endif

		/* Mark the channel as resetting and restart it */
		pri->pvts[pri->resetpos]->resetting = 1;
		pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[pri->resetpos]));
	} else {
		pri->resetting = 0;
		time(&pri->lastreset);
	}
	return 0;
}

static int pri_find_empty_chan(struct sig_pri_pri *pri, int backwards)
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
		if (pri->pvts[x] && !pri->pvts[x]->inalarm && !pri->pvts[x]->owner) {
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

static void *do_idle_thread(void *vchan)
{
	struct ast_channel *chan = vchan;
	struct sig_pri_chan *pvt = chan->tech_pvt;
	struct ast_frame *f;
	char ex[80];
	/* Wait up to 30 seconds for an answer */
	int newms, ms = 30000;
	ast_verb(3, "Initiating idle call on channel %s\n", chan->name);
	snprintf(ex, sizeof(ex), "%d/%s", pvt->channel, pvt->pri->idledial);
	if (ast_call(chan, ex, 0)) {
		ast_log(LOG_WARNING, "Idle dial failed on '%s' to '%s'\n", chan->name, ex);
		ast_hangup(chan);
		return NULL;
	}
	while ((newms = ast_waitfor(chan, ms)) > 0) {
		f = ast_read(chan);
		if (!f) {
			/* Got hangup */
			break;
		}
		if (f->frametype == AST_FRAME_CONTROL) {
			switch (f->subclass) {
			case AST_CONTROL_ANSWER:
				/* Launch the PBX */
				ast_copy_string(chan->exten, pvt->pri->idleext, sizeof(chan->exten));
				ast_copy_string(chan->context, pvt->pri->idlecontext, sizeof(chan->context));
				chan->priority = 1;
				ast_verb(4, "Idle channel '%s' answered, sending to %s@%s\n", chan->name, chan->exten, chan->context);
				ast_pbx_run(chan);
				/* It's already hungup, return immediately */
				return NULL;
			case AST_CONTROL_BUSY:
				ast_verb(4, "Idle channel '%s' busy, waiting...\n", chan->name);
				break;
			case AST_CONTROL_CONGESTION:
				ast_verb(4, "Idle channel '%s' congested, waiting...\n", chan->name);
				break;
			};
		}
		ast_frfree(f);
		ms = newms;
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

	if (!chan) {
		/* We lost the owner before we could get started. */
		return NULL;
	}

	/*
	 * In the bizarre case where the channel has become a zombie before we
	 * even get started here, abort safely.
	 */
	if (!chan->tech_pvt) {
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", chan->name);
		ast_hangup(chan);
		return NULL;
	}

	ast_verb(3, "Starting simple switch on '%s'\n", chan->name);

	/* Now loop looking for an extension */
	ast_copy_string(exten, p->exten, sizeof(exten));
	len = strlen(exten);
	res = 0;
	while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
		if (len && !ast_ignore_pattern(chan->context, exten))
			sig_pri_play_tone(p, -1);
		else
			sig_pri_play_tone(p, SIG_PRI_TONE_DIALTONE);
		if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num))
			timeout = pri_matchdigittimeout;
		else
			timeout = pri_gendigittimeout;
		res = ast_waitfordigit(chan, timeout);
		if (res < 0) {
			ast_log(LOG_DEBUG, "waitfordigit returned < 0...\n");
			ast_hangup(chan);
			return NULL;
		} else if (res) {
			exten[len++] = res;
			exten[len] = '\0';
		} else
			goto exit;
	}
	/* if no extension was received ('unspecified') on overlap call, use the 's' extension */
	if (ast_strlen_zero(exten)) {
		ast_verb(3, "Going to extension s|1 because of empty extension received on overlap call\n");
		exten[0] = 's';
		exten[1] = '\0';
	}
	sig_pri_play_tone(p, -1);
	if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num)) {
		/* Start the real PBX */
		ast_copy_string(chan->exten, exten, sizeof(chan->exten));
		sig_pri_set_echocanceller(p, 1);
		ast_setstate(chan, AST_STATE_RING);
		res = ast_pbx_run(chan);
		if (res) {
			ast_log(LOG_WARNING, "PBX exited non-zero!\n");
		}
	} else {
		ast_log(LOG_DEBUG, "No such possible extension '%s' in context '%s'\n", exten, chan->context);
		chan->hangupcause = AST_CAUSE_UNALLOCATED;
		ast_hangup(chan);
		p->exten[0] = '\0';
		/* Since we send release complete here, we won't get one */
		p->call = NULL;
	}
	return NULL;

exit:
	res = sig_pri_play_tone(p, SIG_PRI_TONE_CONGESTION);
	if (res < 0)
		ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
	return NULL;
}

void pri_event_alarm(struct sig_pri_pri *pri, int index, int before_start_pri)
{
	pri->dchanavail[index] &= ~(DCHAN_NOTINALARM | DCHAN_UP);
	if (!before_start_pri)
		pri_find_dchan(pri);
}

void pri_event_noalarm(struct sig_pri_pri *pri, int index, int before_start_pri)
{
	pri->dchanavail[index] |= DCHAN_NOTINALARM;
	if (!before_start_pri)
		pri_restart(pri->dchans[index]);
}

#if defined(SUPPORT_USERUSER)
/*!
 * \internal
 * \brief Obtain the sig_pri owner channel lock if the owner exists.
 * \since 1.6.3
 *
 * \param pri sig_pri PRI control structure.
 * \param chanpos Channel position in the span.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_lock_owner(struct sig_pri_pri *pri, int chanpos)
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
		/* We must unlock the PRI to avoid the possibility of a deadlock */
		ast_mutex_unlock(&pri->lock);
		PRI_DEADLOCK_AVOIDANCE(pri->pvts[chanpos]);
		ast_mutex_lock(&pri->lock);
	}
}
#endif	/* defined(SUPPORT_USERUSER) */

static void *pri_dchannel(void *vpri)
{
	struct sig_pri_pri *pri = vpri;
	pri_event *e;
	struct pollfd fds[NUM_DCHANS];
	int res;
	int chanpos = 0;
	int x;
	struct ast_channel *c;
	struct timeval tv, lowest, *next;
	int doidling=0;
	char *cc;
	time_t t;
	int i, which=-1;
	int numdchans;
	pthread_t threadid;
	char ani2str[6];
	char plancallingnum[AST_MAX_EXTENSION];
	char plancallingani[AST_MAX_EXTENSION];
	char calledtonstr[10];
	struct timeval lastidle = { 0, 0 };
	pthread_t p;
	struct ast_channel *idle;
	char idlen[80];
	int nextidle = -1;
	int haveidles;
	int activeidles;

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
		for (i = 0; i < NUM_DCHANS; i++) {
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
				if (pri->resetpos < 0)
					pri_check_restart(pri);
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
				if (pri->pvts[x] && !pri->pvts[x]->owner &&
					!pri->pvts[x]->call) {
					if (haveidles < pri->minunused) {
						haveidles++;
					} else if (!pri->pvts[x]->resetting) {
						nextidle = x;
						break;
					}
				} else if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall)
					activeidles++;
			}
			if (nextidle > -1) {
				if (ast_tvdiff_ms(ast_tvnow(), lastidle) > 1000) {
					/* Don't create a new idle call more than once per second */
					snprintf(idlen, sizeof(idlen), "%d/%s", pri->pvts[nextidle]->channel, pri->idledial);
					idle = sig_pri_request(pri->pvts[nextidle], AST_FORMAT_ULAW, NULL);
					if (idle) {
						pri->pvts[nextidle]->isidlecall = 1;
						if (ast_pthread_create_background(&p, NULL, do_idle_thread, idle)) {
							ast_log(LOG_WARNING, "Unable to start new thread for idle channel '%s'\n", idle->name);
							ast_hangup(idle);
						}
					} else
						ast_log(LOG_WARNING, "Unable to request channel 'DAHDI/%s' for idle call\n", idlen);
					gettimeofday(&lastidle, NULL);
				}
			} else if ((haveidles < pri->minunused) &&
				(activeidles > pri->minidle)) {
				/* Mark something for hangup if there is something
				   that can be hungup */
				for (x = pri->numchans; x >= 0; x--) {
					/* find a candidate channel */
					if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall) {
						pri->pvts[x]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
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
		lowest = ast_tv(60, 0);
		for (i = 0; i < NUM_DCHANS; i++) {
			/* Find lowest available d-channel */
			if (!pri->dchans[i])
				break;
			if ((next = pri_schedule_next(pri->dchans[i]))) {
				/* We need relative time here */
				tv = ast_tvsub(*next, ast_tvnow());
				if (tv.tv_sec < 0) {
					tv = ast_tv(0,0);
				}
				if (doidling || pri->resetting) {
					if (tv.tv_sec > 1) {
						tv = ast_tv(1, 0);
					}
				} else {
					if (tv.tv_sec > 60) {
						tv = ast_tv(60, 0);
					}
				}
			} else if (doidling || pri->resetting) {
				/* Make sure we stop at least once per second if we're
				   monitoring idle channels */
				tv = ast_tv(1,0);
			} else {
				/* Don't poll for more than 60 seconds */
				tv = ast_tv(60, 0);
			}
			if (!i || ast_tvcmp(tv, lowest) < 0) {
				lowest = tv;
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
			for (which = 0; which < NUM_DCHANS; which++) {
				if (!pri->dchans[which])
					break;
				/* Just a timeout, run the scheduler */
				e = pri_schedule_run(pri->dchans[which]);
				if (e)
					break;
			}
		} else if (res > -1) {
			for (which = 0; which < NUM_DCHANS; which++) {
				if (!pri->dchans[which])
					break;
				if (fds[which].revents & POLLPRI) {
					sig_pri_handle_dchan_exception(pri, which);
				} else if (fds[which].revents & POLLIN) {
					e = pri_check_event(pri->dchans[which]);
				}
				if (e)
					break;
			}
		} else if (errno != EINTR)
			ast_log(LOG_WARNING, "pri_event returned error %d (%s)\n", errno, strerror(errno));

		if (e) {
			if (pri->debug)
				pri_dump_event(pri->dchans[which], e);

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
				if (!pri->pri) pri_find_dchan(pri);

				/* Note presense of D-channel */
				time(&pri->lastreset);

				/* Restart in 5 seconds */
				if (pri->resetinterval > -1) {
					pri->lastreset -= pri->resetinterval;
					pri->lastreset += 5;
				}
				pri->resetting = 0;
				/* Take the channels from inalarm condition */
				for (i = 0; i < pri->numchans; i++)
					if (pri->pvts[i]) {
						pri->pvts[i]->inalarm = 0;
					}
				break;
			case PRI_EVENT_DCHAN_DOWN:
				pri_find_dchan(pri);
				if (!pri_is_up(pri)) {
					pri->resetting = 0;
					/* Hangup active channels and put them in alarm mode */
					for (i = 0; i < pri->numchans; i++) {
						struct sig_pri_chan *p = pri->pvts[i];
						if (p) {
							if (!p->pri || !p->pri->pri || pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0) {
								/* T309 is not enabled : hangup calls when alarm occurs */
								if (p->call) {
									if (p->pri && p->pri->pri) {
										pri_hangup(p->pri->pri, p->call, -1);
										pri_destroycall(p->pri->pri, p->call);
										p->call = NULL;
									} else
										ast_log(LOG_WARNING, "The PRI Call have not been destroyed\n");
								}
								if (p->owner)
									ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
							}
							/* For PTMP connections with non persistent layer 2 we want
							 * to *not* declare inalarm unless there actually is an alarm */
							if (p->pri->sig != SIG_BRI_PTMP) {
								p->inalarm = 1;
							}
						}
					}
				}
				break;
			case PRI_EVENT_RESTART:
				if (e->restart.channel > -1) {
					chanpos = pri_find_principle(pri, e->restart.channel);
					if (chanpos < 0)
						ast_log(LOG_WARNING, "Restart requested on odd/unavailable channel number %d/%d on span %d\n",
							PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
					else {
#ifdef HAVE_PRI_SERVICE_MESSAGES
						char db_chan_name[20], db_answer[5], state;
						int why, skipit = 0;

						ast_mutex_lock(&pri->pvts[chanpos]->service_lock);
						snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, pri->pvts[chanpos]->pri->span, pri->pvts[chanpos]->channel);
						ast_mutex_unlock(&pri->pvts[chanpos]->service_lock);

						if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
							sscanf(db_answer, "%c:%d", &state, &why);
							if (why) {
								ast_log(LOG_NOTICE, "span '%d' channel '%d' out-of-service (reason: %s), ignoring RESTART\n", pri->span,
									e->restart.channel, (why & SRVST_FAREND) ? (why & SRVST_NEAREND) ? "both ends" : "far end" : "near end");
								skipit = 1;
							} else {
								ast_db_del(db_chan_name, SRVST_DBKEY);
							}
						}
						if (!skipit) {
#endif
							ast_verb(3, "B-channel %d/%d restarted on span %d\n",
								PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
							sig_pri_lock_private(pri->pvts[chanpos]);
							if (pri->pvts[chanpos]->call) {
								pri_destroycall(pri->pri, pri->pvts[chanpos]->call);
								pri->pvts[chanpos]->call = NULL;
							}
#ifdef HAVE_PRI_SERVICE_MESSAGES
						}
#endif
						/* Force soft hangup if appropriate */
						if (pri->pvts[chanpos]->owner)
							ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				} else {
					ast_verb(3, "Restart on requested on entire span %d\n", pri->span);
					for (x = 0; x < pri->numchans; x++)
						if (pri->pvts[x]) {
							sig_pri_lock_private(pri->pvts[x]);
							if (pri->pvts[x]->call) {
								pri_destroycall(pri->pri, pri->pvts[x]->call);
								pri->pvts[x]->call = NULL;
							}
 							if (pri->pvts[x]->owner)
								ast_softhangup_nolock(pri->pvts[x]->owner, AST_SOFTHANGUP_DEV);
							sig_pri_unlock_private(pri->pvts[x]);
						}
				}
				break;
			case PRI_EVENT_KEYPAD_DIGIT:
				chanpos = pri_find_principle(pri, e->digit.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "KEYPAD_DIGITs received on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->digit.channel), PRI_CHANNEL(e->digit.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->digit.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding KEYPAD_DIGITs further on */
						if ((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
							&& pri->pvts[chanpos]->call == e->digit.call
							&& pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->digit.digits);
							int i;

							for (i = 0; i < digitlen; i++) {
								struct ast_frame f = { AST_FRAME_DTMF, e->digit.digits[i], };

								pri_queue_frame(pri->pvts[chanpos], &f, pri);
							}
						}
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;

			case PRI_EVENT_INFO_RECEIVED:
				chanpos = pri_find_principle(pri, e->ring.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "INFO received on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->ring.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding INFORMATION further on */
						if ((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
							&& pri->pvts[chanpos]->call == e->ring.call
							&& pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->ring.callednum);
							int i;

							for (i = 0; i < digitlen; i++) {
								struct ast_frame f = { AST_FRAME_DTMF, e->ring.callednum[i], };

								pri_queue_frame(pri->pvts[chanpos], &f, pri);
							}
						}
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
#ifdef HAVE_PRI_SERVICE_MESSAGES
			case PRI_EVENT_SERVICE:
				chanpos = pri_find_principle(pri, e->service.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received service change status %d on unconfigured channel %d/%d span %d\n",
						e->service_ack.changestatus, PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span);
				} else {
					char db_chan_name[20], db_answer[5], state;
					int ch, why = -1;

					ast_mutex_lock(&pri->pvts[chanpos]->service_lock);
					ch = pri->pvts[chanpos]->channel;
					ast_mutex_unlock(&pri->pvts[chanpos]->service_lock);

					snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, pri->pvts[chanpos]->pri->span, ch);
					if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
						sscanf(db_answer, "%c:%d", &state, &why);
						ast_db_del(db_chan_name, SRVST_DBKEY);
					}
					switch (e->service.changestatus) {
					case 0: /* in-service */
						if (why > -1) {
							if (why & SRVST_NEAREND) {
								snprintf(db_answer, sizeof(db_answer), "%s:%d", SRVST_TYPE_OOS, SRVST_NEAREND);
								ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
								ast_debug(2, "channel '%d' service state { near: out-of-service,  far: in-service }\n", ch);
							}
						}
						break;
					case 2: /* out-of-service */
						if (why == -1) {
							why = SRVST_FAREND;
						} else {
							why |= SRVST_FAREND;
						}
						snprintf(db_answer, sizeof(db_answer), "%s:%d", SRVST_TYPE_OOS, why);
						ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
						break;
					default:
						ast_log(LOG_ERROR, "Huh?  changestatus is: %d\n", e->service.changestatus);
					}
					ast_log(LOG_NOTICE, "Channel %d/%d span %d (logical: %d) received a change of service message, status '%d'\n",
						PRI_SPAN(e->service.channel), PRI_CHANNEL(e->service.channel), pri->span, ch, e->service.changestatus);
				}
				break;
			case PRI_EVENT_SERVICE_ACK:
				chanpos = pri_find_principle(pri, e->service_ack.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received service acknowledge change status '%d' on unconfigured channel %d/%d span %d\n",
						e->service_ack.changestatus, PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span);
				} else {
					ast_debug(2, "Channel %d/%d span %d received a change os service acknowledgement message, status '%d'\n",
						PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span, e->service_ack.changestatus);
				}
				break;
#endif
			case PRI_EVENT_RING:
				if (e->ring.channel == -1)
					chanpos = pri_find_empty_chan(pri, 1);
				else
					chanpos = pri_find_principle(pri, e->ring.channel);
				/* if no channel specified find one empty */
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ring requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					sig_pri_lock_private(pri->pvts[chanpos]);
					if (pri->pvts[chanpos]->owner) {
						if (pri->pvts[chanpos]->call == e->ring.call) {
							ast_log(LOG_WARNING, "Duplicate setup requested on channel %d/%d already in use on span %d\n",
								PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							sig_pri_unlock_private(pri->pvts[chanpos]);
							break;
						} else {
							/* This is where we handle initial glare */
							ast_debug(1, "Ring requested on channel %d/%d already in use or previously requested on span %d.  Attempting to renegotiating channel.\n",
							PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							sig_pri_unlock_private(pri->pvts[chanpos]);
							chanpos = -1;
						}
					}
					if (chanpos > -1)
						sig_pri_unlock_private(pri->pvts[chanpos]);
				}
				if ((chanpos < 0) && (e->ring.flexible))
					chanpos = pri_find_empty_chan(pri, 1);
				if (chanpos > -1) {
					sig_pri_lock_private(pri->pvts[chanpos]);
					pri->pvts[chanpos]->call = e->ring.call;

					/* Use plancallingnum as a scratch buffer since it is initialized next. */
					apply_plan_to_number(plancallingnum, sizeof(plancallingnum), pri,
						e->ring.redirectingnum, e->ring.callingplanrdnis);
					sig_pri_set_rdnis(pri->pvts[chanpos], plancallingnum);

					/* Setup caller-id info */
					apply_plan_to_number(plancallingnum, sizeof(plancallingnum), pri, e->ring.callingnum, e->ring.callingplan);
					pri->pvts[chanpos]->cid_ani2 = 0;
					if (pri->pvts[chanpos]->use_callerid) {
						ast_shrink_phone_number(plancallingnum);
						ast_copy_string(pri->pvts[chanpos]->cid_num, plancallingnum, sizeof(pri->pvts[chanpos]->cid_num));
#ifdef PRI_ANI
						if (!ast_strlen_zero(e->ring.callingani)) {
							apply_plan_to_number(plancallingani, sizeof(plancallingani), pri, e->ring.callingani, e->ring.callingplanani);
							ast_shrink_phone_number(plancallingani);
							ast_copy_string(pri->pvts[chanpos]->cid_ani, plancallingani, sizeof(pri->pvts[chanpos]->cid_ani));
						} else {
							pri->pvts[chanpos]->cid_ani[0] = '\0';
						}
#endif
						ast_copy_string(pri->pvts[chanpos]->cid_name, e->ring.callingname, sizeof(pri->pvts[chanpos]->cid_name));
						pri->pvts[chanpos]->cid_ton = e->ring.callingplan; /* this is the callingplan (TON/NPI), e->ring.callingplan>>4 would be the TON */
						pri->pvts[chanpos]->callingpres = e->ring.callingpres;
						if (e->ring.ani2 >= 0) {
							pri->pvts[chanpos]->cid_ani2 = e->ring.ani2;
						}
					} else {
						pri->pvts[chanpos]->cid_num[0] = '\0';
						pri->pvts[chanpos]->cid_ani[0] = '\0';
						pri->pvts[chanpos]->cid_name[0] = '\0';
						pri->pvts[chanpos]->cid_ton = 0;
						pri->pvts[chanpos]->callingpres = 0;
					}
					sig_pri_set_caller_id(pri->pvts[chanpos]);

					/* Set DNID on all incoming calls -- even immediate */
					sig_pri_set_dnid(pri->pvts[chanpos], e->ring.callednum);

					/* If immediate=yes go to s|1 */
					if (pri->pvts[chanpos]->immediate) {
						ast_verb(3, "Going to extension s|1 because of immediate=yes\n");
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}
					/* Get called number */
					else if (!ast_strlen_zero(e->ring.callednum)) {
						ast_copy_string(pri->pvts[chanpos]->exten, e->ring.callednum, sizeof(pri->pvts[chanpos]->exten));
					} else if (pri->overlapdial)
						pri->pvts[chanpos]->exten[0] = '\0';
					else {
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
					if (((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING) && ast_canmatch_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) ||
						ast_exists_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
						/* Setup law */
						if (e->ring.complete || !(pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) {
							/* Just announce proceeding */
							pri->pvts[chanpos]->proceeding = 1;
							pri_proceeding(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 0);
						} else {
							if (pri->switchtype != PRI_SWITCH_GR303_TMC)
								pri_need_more_info(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
							else
								pri_answer(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
						}

						/* Start PBX */
						if (!e->ring.complete
							&& (pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
							&& ast_matchmore_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
							/*
							 * Release the PRI lock while we create the channel
							 * so other threads can send D channel messages.
							 */
							ast_mutex_unlock(&pri->lock);
							c = sig_pri_new_ast_channel(pri->pvts[chanpos], AST_STATE_RESERVED, 0, (e->ring.layer1 = PRI_LAYER_1_ALAW) ? SIG_PRI_ALAW : SIG_PRI_ULAW, e->ring.ctype, pri->pvts[chanpos]->exten, NULL);
							ast_mutex_lock(&pri->lock);
							if (c) {
								if (!ast_strlen_zero(e->ring.callingsubaddr)) {
									pbx_builtin_setvar_helper(c, "CALLINGSUBADDR", e->ring.callingsubaddr);
								}
								if (e->ring.ani2 >= 0) {
									snprintf(ani2str, sizeof(ani2str), "%d", e->ring.ani2);
									pbx_builtin_setvar_helper(c, "ANI2", ani2str);
								}

#ifdef SUPPORT_USERUSER
								if (!ast_strlen_zero(e->ring.useruserinfo)) {
									pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
								}
#endif

								snprintf(calledtonstr, sizeof(calledtonstr), "%d", e->ring.calledplan);
								pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);
								if (e->ring.redirectingreason >= 0)
									pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
#if defined(HAVE_PRI_REVERSE_CHARGE)
								pri->pvts[chanpos]->reverse_charging_indication = e->ring.reversecharge;
#endif
							}
							if (c && !ast_pthread_create_detached(&threadid, NULL, pri_ss_thread, pri->pvts[chanpos])) {
								ast_verb(3, "Accepting overlap call from '%s' to '%s' on channel %d/%d, span %d\n",
									plancallingnum, S_OR(pri->pvts[chanpos]->exten, "<unspecified>"),
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								if (c)
									ast_hangup(c);
								else {
									pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
									pri->pvts[chanpos]->call = NULL;
								}
							}
						} else {
							/*
							 * Release the PRI lock while we create the channel
							 * so other threads can send D channel messages.
							 */
							ast_mutex_unlock(&pri->lock);
							c = sig_pri_new_ast_channel(pri->pvts[chanpos], AST_STATE_RING, 0, (e->ring.layer1 == PRI_LAYER_1_ALAW) ? SIG_PRI_ALAW : SIG_PRI_ULAW, e->ring.ctype, pri->pvts[chanpos]->exten, NULL);
							ast_mutex_lock(&pri->lock);
							if (c) {
								/*
								 * It is reasonably safe to set the following
								 * channel variables while the PRI and DAHDI private
								 * structures are locked.  The PBX has not been
								 * started yet and it is unlikely that any other task
								 * will do anything with the channel we have just
								 * created.
								 */
								if (!ast_strlen_zero(e->ring.callingsubaddr)) {
									pbx_builtin_setvar_helper(c, "CALLINGSUBADDR", e->ring.callingsubaddr);
								}
								if (e->ring.ani2 >= 0) {
									snprintf(ani2str, sizeof(ani2str), "%d", e->ring.ani2);
									pbx_builtin_setvar_helper(c, "ANI2", ani2str);
								}

#ifdef SUPPORT_USERUSER
								if (!ast_strlen_zero(e->ring.useruserinfo)) {
									pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
								}
#endif

								if (e->ring.redirectingreason >= 0)
									pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
#if defined(HAVE_PRI_REVERSE_CHARGE)
								pri->pvts[chanpos]->reverse_charging_indication = e->ring.reversecharge;
#endif

								snprintf(calledtonstr, sizeof(calledtonstr), "%d", e->ring.calledplan);
								pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);
							}
							if (c && !ast_pbx_start(c)) {
								ast_verb(3, "Accepting call from '%s' to '%s' on channel %d/%d, span %d\n",
									plancallingnum, pri->pvts[chanpos]->exten,
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								if (c) {
									ast_hangup(c);
								} else {
									pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
									pri->pvts[chanpos]->call = NULL;
								}
							}
						}
					} else {
						ast_verb(3, "Extension '%s' in context '%s' from '%s' does not exist.  Rejecting call on channel %d/%d, span %d\n",
							pri->pvts[chanpos]->exten, pri->pvts[chanpos]->context, pri->pvts[chanpos]->cid_num, pri->pvts[chanpos]->logicalspan,
							pri->pvts[chanpos]->prioffset, pri->span);
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_UNALLOCATED);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->exten[0] = '\0';
					}
					sig_pri_unlock_private(pri->pvts[chanpos]);
				} else {
					if (e->ring.flexible)
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
					else
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
				}
				break;
			case PRI_EVENT_RINGING:
				chanpos = pri_find_principle(pri, e->ringing.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ringing requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->ringing.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Ringing requested on channel %d/%d not in use on span %d\n",
							PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
					} else {
						sig_pri_lock_private(pri->pvts[chanpos]);
						sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
						pri_queue_control(pri->pvts[chanpos], AST_CONTROL_RINGING, pri);
						pri->pvts[chanpos]->alerting = 1;

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
					}
				}
				break;
			case PRI_EVENT_PROGRESS:
				/* Get chan value if e->e is not PRI_EVNT_RINGING */
				chanpos = pri_find_principle(pri, e->proceeding.channel);
				if (chanpos > -1) {
					if ((!pri->pvts[chanpos]->progress)
#ifdef PRI_PROGRESS_MASK
						|| (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE)
#else
						|| (e->proceeding.progress == 8)
#endif
						) {
						struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROGRESS, };

						if (e->proceeding.cause > -1) {
							ast_verb(3, "PROGRESS with cause code %d received\n", e->proceeding.cause);

							/* Work around broken, out of spec USER_BUSY cause in a progress message */
							if (e->proceeding.cause == AST_CAUSE_USER_BUSY) {
								if (pri->pvts[chanpos]->owner) {
									ast_verb(3, "PROGRESS with 'user busy' received, signaling AST_CONTROL_BUSY instead of AST_CONTROL_PROGRESS\n");

									pri->pvts[chanpos]->owner->hangupcause = e->proceeding.cause;
									f.subclass = AST_CONTROL_BUSY;
								}
							}
						}

						sig_pri_lock_private(pri->pvts[chanpos]);
						ast_debug(1, "Queuing frame from PRI_EVENT_PROGRESS on channel %d/%d span %d\n",
							pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						pri_queue_frame(pri->pvts[chanpos], &f, pri);
						if (
#ifdef PRI_PROGRESS_MASK
							e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE
#else
							e->proceeding.progress == 8
#endif
							) {
							/* Bring voice path up */
							f.subclass = AST_CONTROL_PROGRESS;
							pri_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						pri->pvts[chanpos]->progress = 1;
						sig_pri_set_dialing(pri->pvts[chanpos], 0);
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
			case PRI_EVENT_PROCEEDING:
				chanpos = pri_find_principle(pri, e->proceeding.channel);
				if (chanpos > -1) {
					if (!pri->pvts[chanpos]->proceeding) {
						struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROCEEDING, };

						sig_pri_lock_private(pri->pvts[chanpos]);
						ast_debug(1, "Queuing frame from PRI_EVENT_PROCEEDING on channel %d/%d span %d\n",
							pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						pri_queue_frame(pri->pvts[chanpos], &f, pri);
						if (
#ifdef PRI_PROGRESS_MASK
							e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE
#else
							e->proceeding.progress == 8
#endif
							) {
							/* Bring voice path up */
							f.subclass = AST_CONTROL_PROGRESS;
							pri_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						pri->pvts[chanpos]->proceeding = 1;
						sig_pri_set_dialing(pri->pvts[chanpos], 0);
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
			case PRI_EVENT_FACNAME:
				chanpos = pri_find_principle(pri, e->facname.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Facility Name requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->facname.channel), PRI_CHANNEL(e->facname.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->facname.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Facility Name requested on channel %d/%d not in use on span %d\n",
							PRI_SPAN(e->facname.channel), PRI_CHANNEL(e->facname.channel), pri->span);
					} else {
						/* Re-use *69 field for PRI */
						sig_pri_lock_private(pri->pvts[chanpos]);
						ast_copy_string(pri->pvts[chanpos]->lastcid_num, e->facname.callingnum, sizeof(pri->pvts[chanpos]->lastcid_num));
						ast_copy_string(pri->pvts[chanpos]->lastcid_name, e->facname.callingname, sizeof(pri->pvts[chanpos]->lastcid_name));
						pri_update_cid(pri->pvts[chanpos], pri);
						sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
			case PRI_EVENT_ANSWER:
				chanpos = pri_find_principle(pri, e->answer.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Answer on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->answer.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Answer requested on channel %d/%d not in use on span %d\n",
							PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
					} else {
						sig_pri_lock_private(pri->pvts[chanpos]);
						pri_queue_control(pri->pvts[chanpos], AST_CONTROL_ANSWER, pri);
						/* Enable echo cancellation if it's not on already */
						sig_pri_set_dialing(pri->pvts[chanpos], 0);
						sig_pri_set_echocanceller(pri->pvts[chanpos], 1);

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
					}
				}
				break;
			case PRI_EVENT_HANGUP:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						if (!pri->pvts[chanpos]->alreadyhungup) {
							/* we're calling here dahdi_hangup so once we get there we need to clear p->call after calling pri_hangup */
							pri->pvts[chanpos]->alreadyhungup = 1;
							if (pri->pvts[chanpos]->owner) {
								/* Queue a BUSY instead of a hangup if our cause is appropriate */
								pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
								if (pri->pvts[chanpos]->owner->_state == AST_STATE_UP)
									ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
								else {
									switch (e->hangup.cause) {
									case PRI_CAUSE_USER_BUSY:
										pri_queue_control(pri->pvts[chanpos], AST_CONTROL_BUSY, pri);
										break;
									case PRI_CAUSE_CALL_REJECTED:
									case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
									case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
									case PRI_CAUSE_SWITCH_CONGESTION:
									case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
									case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
										pri_queue_control(pri->pvts[chanpos], AST_CONTROL_CONGESTION, pri);
										break;
									default:
										ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
										break;
									}
								}
							}
							ast_verb(3, "Channel %d/%d, span %d got hangup, cause %d\n",
								pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, e->hangup.cause);
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							ast_verb(3, "Forcing restart of channel %d/%d on span %d since channel reported in use\n",
								PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
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
					} else {
						ast_log(LOG_WARNING, "Hangup on bad channel %d/%d on span %d\n",
							PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				}
				break;
#ifndef PRI_EVENT_HANGUP_REQ
#error please update libpri
#endif
			case PRI_EVENT_HANGUP_REQ:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup REQ requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						if (pri->pvts[chanpos]->owner) {
							pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
							if (pri->pvts[chanpos]->owner->_state == AST_STATE_UP)
								ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
							else {
								switch (e->hangup.cause) {
								case PRI_CAUSE_USER_BUSY:
									pri_queue_control(pri->pvts[chanpos], AST_CONTROL_BUSY, pri);
									break;
								case PRI_CAUSE_CALL_REJECTED:
								case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
								case PRI_CAUSE_SWITCH_CONGESTION:
								case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
									pri_queue_control(pri->pvts[chanpos], AST_CONTROL_CONGESTION, pri);
									break;
								default:
									ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
									break;
								}
							}
							ast_verb(3, "Channel %d/%d, span %d got hangup request, cause %d\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span, e->hangup.cause);
							if (e->hangup.aoc_units > -1)
								ast_verb(3, "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							ast_verb(3, "Forcing restart of channel %d/%d span %d since channel reported in use\n",
								PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
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
					} else {
						ast_log(LOG_WARNING, "Hangup REQ on bad channel %d/%d on span %d\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				}
				break;
			case PRI_EVENT_HANGUP_ACK:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup ACK requested on unconfigured channel number %d/%d span %d\n",
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->resetting = 0;
						if (pri->pvts[chanpos]->owner) {
							ast_verb(3, "Channel %d/%d, span %d got hangup ACK\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
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
					}
				}
				break;
			case PRI_EVENT_CONFIG_ERR:
				ast_log(LOG_WARNING, "PRI Error on span %d: %s\n", pri->trunkgroup, e->err.err);
				break;
			case PRI_EVENT_RESTART_ACK:
				chanpos = pri_find_principle(pri, e->restartack.channel);
				if (chanpos < 0) {
					/* Sometime switches (e.g. I421 / British Telecom) don't give us the
					   channel number, so we have to figure it out...  This must be why
					   everybody resets exactly a channel at a time. */
					for (x = 0; x < pri->numchans; x++) {
						if (pri->pvts[x] && pri->pvts[x]->resetting) {
							chanpos = x;
							sig_pri_lock_private(pri->pvts[chanpos]);
							ast_debug(1, "Assuming restart ack is really for channel %d/%d span %d\n", pri->pvts[chanpos]->logicalspan,
								pri->pvts[chanpos]->prioffset, pri->span);
							if (pri->pvts[chanpos]->owner) {
								ast_log(LOG_WARNING, "Got restart ack on channel %d/%d with owner on span %d\n", pri->pvts[chanpos]->logicalspan,
									pri->pvts[chanpos]->prioffset, pri->span);
								ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
							}
							pri->pvts[chanpos]->resetting = 0;
							ast_verb(3, "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan,
								pri->pvts[chanpos]->prioffset, pri->span);
							sig_pri_unlock_private(pri->pvts[chanpos]);
							if (pri->resetting)
								pri_check_restart(pri);
							break;
						}
					}
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Restart ACK requested on strange channel %d/%d span %d\n",
							PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
					}
				} else {
					if (pri->pvts[chanpos]) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						if (pri->pvts[chanpos]->owner) {
							ast_log(LOG_WARNING, "Got restart ack on channel %d/%d span %d with owner\n",
								PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
							ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
						}
						pri->pvts[chanpos]->resetting = 0;
						ast_verb(3, "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan,
							pri->pvts[chanpos]->prioffset, pri->span);
						sig_pri_unlock_private(pri->pvts[chanpos]);
						if (pri->resetting)
							pri_check_restart(pri);
					}
				}
				break;
			case PRI_EVENT_SETUP_ACK:
				chanpos = pri_find_principle(pri, e->setup_ack.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received SETUP_ACKNOWLEDGE on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->setup_ack.channel), PRI_CHANNEL(e->setup_ack.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->setup_ack.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						pri->pvts[chanpos]->setup_ack = 1;
						/* Send any queued digits */
						for (x = 0;x < strlen(pri->pvts[chanpos]->dialdest); x++) {
							ast_debug(1, "Sending pending digit '%c'\n", pri->pvts[chanpos]->dialdest[x]);
							pri_information(pri->pri, pri->pvts[chanpos]->call,
								pri->pvts[chanpos]->dialdest[x]);
						}
						sig_pri_unlock_private(pri->pvts[chanpos]);
					} else
						ast_log(LOG_WARNING, "Unable to move channel %d!\n", e->setup_ack.channel);
				}
				break;
			case PRI_EVENT_NOTIFY:
				chanpos = pri_find_principle(pri, e->notify.channel);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received NOTIFY on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->notify.channel), PRI_CHANNEL(e->notify.channel), pri->span);
				} else if (!pri->discardremoteholdretrieval) {
					struct ast_frame f = { AST_FRAME_CONTROL, };

					sig_pri_lock_private(pri->pvts[chanpos]);
					switch (e->notify.info) {
					case PRI_NOTIFY_REMOTE_HOLD:
						f.subclass = AST_CONTROL_HOLD;
						pri_queue_frame(pri->pvts[chanpos], &f, pri);
						break;
					case PRI_NOTIFY_REMOTE_RETRIEVAL:
						f.subclass = AST_CONTROL_UNHOLD;
						pri_queue_frame(pri->pvts[chanpos], &f, pri);
						break;
					}
					sig_pri_unlock_private(pri->pvts[chanpos]);
				}
				break;
			default:
				ast_debug(1, "Event: %d\n", e->e);
			}
		}
		ast_mutex_unlock(&pri->lock);
	}
	/* Never reached */
	return NULL;
}

void sig_pri_init_pri(struct sig_pri_pri *pri)
{
	int i;

	memset(pri, 0, sizeof(*pri));

	ast_mutex_init(&pri->lock);

	pri->master = AST_PTHREADT_NULL;
	for (i = 0; i < NUM_DCHANS; i++)
		pri->fds[i] = -1;
}

int sig_pri_hangup(struct sig_pri_chan *p, struct ast_channel *ast)
{
	int res = 0;
#ifdef SUPPORT_USERUSER
	const char *useruser = pbx_builtin_getvar_helper(ast, "USERUSERINFO");
#endif

	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	p->owner = NULL;
	p->outgoing = 0;
	p->digital = 0;
	p->proceeding = 0;
	p->progress = 0;
	p->alerting = 0;
	p->setup_ack = 0;
	p->exten[0] = '\0';
	sig_pri_set_dialing(p, 0);

	if (!p->call) {
		res = 0;
		goto exit;
	}

	/* Make sure we have a call (or REALLY have a call in the case of a PRI) */
	if (!pri_grab(p, p->pri)) {
		if (p->alreadyhungup) {
			ast_log(LOG_DEBUG, "Already hungup...  Calling hangup once, and clearing call\n");

#ifdef SUPPORT_USERUSER
			pri_call_set_useruser(p->call, useruser);
#endif

			pri_hangup(p->pri->pri, p->call, -1);
			p->call = NULL;
		} else {
			const char *cause = pbx_builtin_getvar_helper(ast,"PRI_CAUSE");
			int icause = ast->hangupcause ? ast->hangupcause : -1;
			ast_log(LOG_DEBUG, "Not yet hungup...  Calling hangup once with icause, and clearing call\n");

#ifdef SUPPORT_USERUSER
			pri_call_set_useruser(p->call, useruser);
#endif

			p->alreadyhungup = 1;
			if (cause) {
				if (atoi(cause))
					icause = atoi(cause);
			}
			pri_hangup(p->pri->pri, p->call, icause);
		}
		if (res < 0)
			ast_log(LOG_WARNING, "pri_disconnect failed\n");
		pri_rel(p->pri);
	} else {
		ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
		res = -1;
	}

exit:
	ast->tech_pvt = NULL;
	return res;
}

int sig_pri_call(struct sig_pri_chan *p, struct ast_channel *ast, char *rdest, int timeout, int layer1)
{
	char dest[256]; /* must be same length as p->dialdest */
	struct pri_sr *sr;
	char *c, *l, *n, *s = NULL;
#ifdef SUPPORT_USERUSER
	const char *useruser;
#endif
	int pridialplan;
	int dp_strip;
	int prilocaldialplan;
	int ldp_strip;
	int exclusive;
	const char *rr_str;
	int redirect_reason;

	ast_log(LOG_DEBUG, "CALLING CID_NAME: %s CID_NUM:: %s\n", ast->cid.cid_name, ast->cid.cid_num);

	if (!p->pri) {
		ast_log(LOG_ERROR, "Could not find pri on channel %d\n", p->channel);
		return -1;
	}


	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "sig_pri_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	ast_copy_string(dest, rdest, sizeof(dest));

	p->dialdest[0] = '\0';
	p->outgoing = 1;

	c = strchr(dest, '/');
	if (c) {
		c++;
	} else {
		c = "";
	}

	l = NULL;
	n = NULL;
	if (!p->hidecallerid) {
		l = ast->connected.id.number;
		if (!p->hidecalleridname) {
			n = ast->connected.id.name;
		}
	}

	if (strlen(c) < p->stripmsd) {
		ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
		return -1;
	}
	if (pri_grab(p, p->pri)) {
		ast_log(LOG_WARNING, "Failed to grab PRI!\n");
		return -1;
	}
	if (!(p->call = pri_new_call(p->pri->pri))) {
		ast_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
		pri_rel(p->pri);
		return -1;
	}
	if (!(sr = pri_sr_new())) {
		ast_log(LOG_WARNING, "Failed to allocate setup request channel %d\n", p->channel);
		pri_destroycall(p->pri->pri, p->call);
		p->call = NULL;
		pri_rel(p->pri);
		return -1;
	}

	p->digital = IS_DIGITAL(ast->transfercapability);

	/* Should the picked channel be used exclusively? */
	if (p->priexclusive || p->pri->nodetype == PRI_NETWORK) {
		exclusive = 1;
	} else {
		exclusive = 0;
	}

	pri_sr_set_channel(sr, PVT_TO_CHANNEL(p), exclusive, 1);
	pri_sr_set_bearer(sr, p->digital ? PRI_TRANS_CAP_DIGITAL : ast->transfercapability,
		(p->digital ? -1 : layer1));

	if (p->pri->facilityenable)
		pri_facility_enable(p->pri->pri);

	ast_verb(3, "Requested transfer capability: 0x%.2x - %s\n", ast->transfercapability, ast_transfercapability2str(ast->transfercapability));
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
#if defined(HAVE_PRI_REVERSE_CHARGE)
		case 'C':
			pri_sr_set_reversecharge(sr, PRI_REVERSECHARGE_REQUESTED);
			break;
#endif
		default:
			if (isalpha(c[p->stripmsd])) {
				ast_log(LOG_WARNING, "Unrecognized pridialplan %s modifier: %c\n",
					c[p->stripmsd] > 'Z' ? "NPI" : "TON", c[p->stripmsd]);
			}
			break;
		}
		c++;
	}
	pri_sr_set_called(sr, c + p->stripmsd + dp_strip, pridialplan, s ? 1 : 0);

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
		p->use_callingpres ? ast->cid.cid_pres : (l ? PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN : PRES_NUMBER_NOT_AVAILABLE));
	if ((rr_str = pbx_builtin_getvar_helper(ast, "PRIREDIRECTREASON"))) {
		if (!strcasecmp(rr_str, "UNKNOWN"))
			redirect_reason = 0;
		else if (!strcasecmp(rr_str, "BUSY"))
			redirect_reason = 1;
		else if (!strcasecmp(rr_str, "NO_REPLY"))
			redirect_reason = 2;
		else if (!strcasecmp(rr_str, "UNCONDITIONAL"))
			redirect_reason = 15;
		else
			redirect_reason = PRI_REDIR_UNCONDITIONAL;
	} else
		redirect_reason = PRI_REDIR_UNCONDITIONAL;
	pri_sr_set_redirecting(sr, ast->cid.cid_rdnis, p->pri->localdialplan - 1, PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN, redirect_reason);

#ifdef SUPPORT_USERUSER
	/* User-user info */
	useruser = pbx_builtin_getvar_helper(p->owner, "USERUSERINFO");
	if (useruser)
		pri_sr_set_useruser(sr, useruser);
#endif

	if (pri_setup(p->pri->pri, p->call, sr)) {
		ast_log(LOG_WARNING, "Unable to setup call to %s (using %s)\n",
			c + p->stripmsd + dp_strip, dialplan2str(p->pri->dialplan));
		pri_rel(p->pri);
		pri_sr_free(sr);
		return -1;
	}
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
		if (p->priindication_oob) {
			chan->hangupcause = AST_CAUSE_USER_BUSY;
			chan->_softhangup |= AST_SOFTHANGUP_DEV;
			res = 0;
		} else if (!p->progress && p->pri && !p->outgoing) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
#ifdef HAVE_PRI_PROG_W_CAUSE
					pri_progress_with_cause(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1, PRI_CAUSE_USER_BUSY); /* cause = 17 */
#else
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->progress = 1;
			res = sig_pri_play_tone(p, SIG_PRI_TONE_BUSY);
		}
		break;
	case AST_CONTROL_RINGING:
		if ((!p->alerting) && p->pri && !p->outgoing && (chan->_state != AST_STATE_UP)) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
					pri_acknowledge(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->alerting = 1;
		}
		res = sig_pri_play_tone(p, SIG_PRI_TONE_RINGTONE);
		if (chan->_state != AST_STATE_UP) {
			if (chan->_state != AST_STATE_RING)
				ast_setstate(chan, AST_STATE_RINGING);
		}
		break;
	case AST_CONTROL_PROCEEDING:
		ast_debug(1,"Received AST_CONTROL_PROCEEDING on %s\n",chan->name);
		if (!p->proceeding && p->pri && !p->outgoing) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
					pri_proceeding(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->proceeding = 1;
			sig_pri_set_dialing(p, 0);
		}
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_PROGRESS:
		ast_debug(1,"Received AST_CONTROL_PROGRESS on %s\n",chan->name);
		p->digital = 0;	/* Digital-only calls isn't allowing any inband progress messages */
		if (!p->progress && p->pri && !p->outgoing) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
#ifdef HAVE_PRI_PROG_W_CAUSE
					pri_progress_with_cause(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1, -1);  /* no cause at all */
#else
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->progress = 1;
		}
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_CONGESTION:
		chan->hangupcause = AST_CAUSE_CONGESTION;
		if (p->priindication_oob) {
			chan->hangupcause = AST_CAUSE_SWITCH_CONGESTION;
			chan->_softhangup |= AST_SOFTHANGUP_DEV;
			res = 0;
		} else if (!p->progress && p->pri && !p->outgoing) {
			if (p->pri) {
				if (!pri_grab(p, p->pri)) {
#ifdef HAVE_PRI_PROG_W_CAUSE
					pri_progress_with_cause(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1, PRI_CAUSE_SWITCH_CONGESTION); /* cause = 42 */
#else
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->progress = 1;
			res = sig_pri_play_tone(p, SIG_PRI_TONE_CONGESTION);
		}
		break;
	case AST_CONTROL_HOLD:
		if (p->pri && !strcasecmp(p->mohinterpret, "passthrough")) {
			if (!pri_grab(p, p->pri)) {
				res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_HOLD);
				pri_rel(p->pri);
			} else {
				ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
			}
		} else
			ast_moh_start(chan, data, p->mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		if (p->pri && !strcasecmp(p->mohinterpret, "passthrough")) {
			if (!pri_grab(p, p->pri)) {
				res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_RETRIEVAL);
				pri_rel(p->pri);
			}
		} else
			ast_moh_stop(chan);
		break;
	case AST_CONTROL_SRCUPDATE:
		res = 0;
		break;
	case -1:
		res = sig_pri_play_tone(p, -1);
		break;
	}

	return res;
}

int sig_pri_answer(struct sig_pri_chan *p, struct ast_channel *ast)
{
	int res = 0;
	/* Send a pri acknowledge */
	if (!pri_grab(p, p->pri)) {
		p->proceeding = 1;
		sig_pri_set_dialing(p, 0);
		res = pri_answer(p->pri->pri, p->call, 0, !p->digital);
		pri_rel(p->pri);
	} else {
		res = -1;
	}
	ast_setstate(ast, AST_STATE_UP);
	return res;
}

int sig_pri_available(struct sig_pri_chan *p, int channelmatch, ast_group_t groupmatch, int *reason, int *channelmatched, int *groupmatched)
{
	/* If no owner definitely available */
	if (!p->owner) {
		/* Trust PRI */
		if (p->pri) {
#ifdef HAVE_PRI_SERVICE_MESSAGES
			char db_chan_name[20], db_answer[5], state;
			int why = 0;

			snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, p->pri->span, p->channel);
			if (!ast_db_get(db_chan_name, SRVST_DBKEY, db_answer, sizeof(db_answer))) {
				sscanf(db_answer, "%c:%d", &state, &why);
			}
			if ((p->resetting || p->call) || (why)) {
				if (why) {
					*reason = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
				}
#else
			if (p->resetting || p->call) {
#endif
				return 0;
			} else {
				return 1;
			}
		}
	}

	return 0;
}

/* If return 0, it means this function was able to handle it (pre setup digits).  If non zero, the user of this
 * functions should handle it normally (generate inband DTMF) */
int sig_pri_digit_begin(struct sig_pri_chan *pvt, struct ast_channel *ast, char digit)
{
	if ((ast->_state == AST_STATE_DIALING) && !pvt->proceeding) {
		if (pvt->setup_ack) {
			if (!pri_grab(pvt, pvt->pri)) {
				pri_information(pvt->pri->pri, pvt->call, digit);
				pri_rel(pvt->pri);
			} else {
				ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", pvt->pri->span);
			}
		} else if (strlen(pvt->dialdest) < sizeof(pvt->dialdest) - 1) {
			int res;
			ast_debug(1, "Queueing digit '%c' since setup_ack not yet received\n", digit);
			res = strlen(pvt->dialdest);
			pvt->dialdest[res++] = digit;
			pvt->dialdest[res] = '\0';
		}
		return 0;
	}
	return 1;
}

int sig_pri_start_pri(struct sig_pri_pri *pri)
{
	int x;
	int i;

	ast_mutex_init(&pri->lock);

	for (i = 0; i < NUM_DCHANS; i++) {
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
#ifdef HAVE_PRI_SERVICE_MESSAGES
			if (pri->enable_service_message_support) {
				pri_set_service_message_support(pri->dchans[i], 1);
			}
#endif
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
		pri_set_debug(pri->dchans[i], DEFAULT_PRI_DEBUG);
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
	pri->resetpos = -1;
	if (ast_pthread_create_background(&pri->master, NULL, pri_dchannel, pri)) {
		for (i = 0; i < NUM_DCHANS; i++) {
			if (!pri->dchans[i])
				break;
			if (pri->fds[i] > 0)
				close(pri->fds[i]);
			pri->fds[i] = -1;
		}
		ast_log(LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

void sig_pri_chan_alarm_notify(struct sig_pri_chan *p, int noalarm)
{
	if (!noalarm) {
		p->inalarm = 1;
		if (!p->pri || !p->pri->pri || (pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0)) {
			/* T309 is not enabled : hangup calls when alarm occurs */
			if (p->call) {
				if (p->pri && p->pri->pri) {
					if (!pri_grab(p, p->pri)) {
						pri_hangup(p->pri->pri, p->call, -1);
						pri_destroycall(p->pri->pri, p->call);
						p->call = NULL;
						pri_rel(p->pri);
					} else
						ast_log(LOG_WARNING, "Failed to grab PRI!\n");
				} else
					ast_log(LOG_WARNING, "Failed to grab PRI!\n");
			} else
				ast_log(LOG_WARNING, "The PRI Call has not been destroyed\n");
		}
		if (p->owner)
			ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
	} else {
		p->inalarm = 0;
	}
}

struct sig_pri_chan *sig_pri_chan_new(void *pvt_data, struct sig_pri_callback *callback, struct sig_pri_pri *pri, int logicalspan, int channo, int trunkgroup)
{
	struct sig_pri_chan *p;

	p = ast_calloc(1, sizeof(*p));

	if (!p)
		return p;

	p->logicalspan = logicalspan;
	p->prioffset = channo;
	p->mastertrunkgroup = trunkgroup;

	p->calls = callback;
	p->chan_pvt = pvt_data;

	pri->pvts[pri->numchans++] = p;
	p->pri = pri;

	return p;
}

static void build_status(char *s, size_t len, int status, int active)
{
	if (!s || len < 1) {
		return;
	}
	s[0] = '\0';
	if (!(status & DCHAN_NOTINALARM))
		strncat(s, "In Alarm, ", len - strlen(s) - 1);
	if (status & DCHAN_UP)
		strncat(s, "Up", len - strlen(s) - 1);
	else
		strncat(s, "Down", len - strlen(s) - 1);
	if (active)
		strncat(s, ", Active", len - strlen(s) - 1);
	else
		strncat(s, ", Standby", len - strlen(s) - 1);
	s[len - 1] = '\0';
}

void sig_pri_cli_show_spans(int fd, int span, struct sig_pri_pri *pri)
{
	char status[256];
	int x;
	for (x = 0; x < NUM_DCHANS; x++) {
		if (pri->dchans[x]) {
			build_status(status, sizeof(status), pri->dchanavail[x], pri->dchans[x] == pri->pri);
			ast_cli(fd, "PRI span %d/%d: %s\n", span, x, status);
		}
	}
}

void sig_pri_cli_show_span(int fd, int *dchannels, struct sig_pri_pri *pri)
{
	int x;
	char status[256];

	for (x = 0; x < NUM_DCHANS; x++) {
		if (pri->dchans[x]) {
#ifdef PRI_DUMP_INFO_STR
			char *info_str = NULL;
#endif
			ast_cli(fd, "%s D-channel: %d\n", pri_order(x), dchannels[x]);
			build_status(status, sizeof(status), pri->dchanavail[x], pri->dchans[x] == pri->pri);
			ast_cli(fd, "Status: %s\n", status);
#ifdef PRI_DUMP_INFO_STR
			info_str = pri_dump_info_str(pri->pri);
			if (info_str) {
				ast_cli(fd, "%s", info_str);
				free(info_str);
			}
#else
			pri_dump_info(pri->pri);
#endif
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

	if (!pri_grab(p, p->pri)) {
		pri_keypad_facility(p->pri->pri, p->call, digits);
		pri_rel(p->pri);
	} else {
		ast_debug(1, "Unable to grab pri to send keypad facility!\n");
		sig_pri_unlock_private(p);
		return -1;
	}

	sig_pri_unlock_private(p);

	return 0;
}

int pri_send_callrerouting_facility_exec(struct sig_pri_chan *p, enum ast_channel_state chanstate, const char *destination, const char *original, const char *reason)
{
	int res = -1;

	sig_pri_lock_private(p);

	if (!p->pri || !p->call) {
		ast_log(LOG_DEBUG, "Unable to find pri or call on channel!\n");
		sig_pri_unlock_private(p);
		return -1;
	}

	switch (p->pri->sig) {
	case SIG_PRI:
		if (!pri_grab(p, p->pri)) {
			if (chanstate == AST_STATE_RING) {
				res = pri_callrerouting_facility(p->pri->pri, p->call, destination, original, reason);
			}
			pri_rel(p->pri);
		} else {
			ast_log(LOG_DEBUG, "Unable to grab pri to send callrerouting facility on span %d!\n", p->pri->span);
			sig_pri_unlock_private(p);
			return -1;
		}
		break;
	}

	sig_pri_unlock_private(p);

	return res;
}

#ifdef HAVE_PRI_SERVICE_MESSAGES
int pri_maintenance_bservice(struct pri *pri, struct sig_pri_chan *p, int changestatus)
{
	int channel = PVT_TO_CHANNEL(p);
	int span = PRI_SPAN(channel);

	return pri_maintenance_service(pri, span, channel, changestatus);
}
#endif

#endif /* HAVE_PRI */
