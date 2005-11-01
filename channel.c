/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>			/* For PI */

#ifdef ZAPTEL_OPTIMIZATIONS
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/zaptel.h>
#else
#include <zaptel.h>
#endif /* __linux__ */
#ifndef ZT_TIMERPING
#error "You need newer zaptel!  Please cvs update zaptel"
#endif
#endif

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/musiconhold.h"
#include "asterisk/logger.h"
#include "asterisk/say.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/translate.h"
#include "asterisk/manager.h"
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

struct channel_spy_trans {
	int last_format;
	struct ast_trans_pvt *path;
};

struct ast_channel_spy_list {
	struct channel_spy_trans read_translator;
	struct channel_spy_trans write_translator;
	AST_LIST_HEAD_NOLOCK(, ast_channel_spy) list;
};

/* uncomment if you have problems with 'monitoring' synchronized files */
#if 0
#define MONITOR_CONSTANT_DELAY
#define MONITOR_DELAY	150 * 8		/* 150 ms of MONITORING DELAY */
#endif

/*
 * Prevent new channel allocation if shutting down.
 */
static int shutting_down = 0;

static int uniqueint = 0;

unsigned long global_fin = 0, global_fout = 0;

/* XXX Lock appropriately in more functions XXX */

struct chanlist {
	const struct ast_channel_tech *tech;
	struct chanlist *next;
};

static struct chanlist *backends = NULL;

/*
 * the list of channels we have
 */
static struct ast_channel *channels = NULL;

/* Protect the channel list, both backends and channels.
 */
AST_MUTEX_DEFINE_STATIC(chlock);

const struct ast_cause {
	int cause;
	const char *desc;
} causes[] = {
	{ AST_CAUSE_UNALLOCATED, "Unallocated (unassigned) number" },
	{ AST_CAUSE_NO_ROUTE_TRANSIT_NET, "No route to specified transmit network" },
	{ AST_CAUSE_NO_ROUTE_DESTINATION, "No route to destination" },
	{ AST_CAUSE_CHANNEL_UNACCEPTABLE, "Channel unacceptable" },
	{ AST_CAUSE_CALL_AWARDED_DELIVERED, "Call awarded and being delivered in an established channel" },
	{ AST_CAUSE_NORMAL_CLEARING, "Normal Clearing" },
	{ AST_CAUSE_USER_BUSY, "User busy" },
	{ AST_CAUSE_NO_USER_RESPONSE, "No user responding" },
	{ AST_CAUSE_NO_ANSWER, "User alerting, no answer" },
	{ AST_CAUSE_CALL_REJECTED, "Call Rejected" },
	{ AST_CAUSE_NUMBER_CHANGED, "Number changed" },
	{ AST_CAUSE_DESTINATION_OUT_OF_ORDER, "Destination out of order" },
	{ AST_CAUSE_INVALID_NUMBER_FORMAT, "Invalid number format" },
	{ AST_CAUSE_FACILITY_REJECTED, "Facility rejected" },
	{ AST_CAUSE_RESPONSE_TO_STATUS_ENQUIRY, "Response to STATus ENQuiry" },
	{ AST_CAUSE_NORMAL_UNSPECIFIED, "Normal, unspecified" },
	{ AST_CAUSE_NORMAL_CIRCUIT_CONGESTION, "Circuit/channel congestion" },
	{ AST_CAUSE_NETWORK_OUT_OF_ORDER, "Network out of order" },
	{ AST_CAUSE_NORMAL_TEMPORARY_FAILURE, "Temporary failure" },
	{ AST_CAUSE_SWITCH_CONGESTION, "Switching equipment congestion" },
	{ AST_CAUSE_ACCESS_INFO_DISCARDED, "Access information discarded" },
	{ AST_CAUSE_REQUESTED_CHAN_UNAVAIL, "Requested channel not available" },
	{ AST_CAUSE_PRE_EMPTED, "Pre-empted" },
	{ AST_CAUSE_FACILITY_NOT_SUBSCRIBED, "Facility not subscribed" },
	{ AST_CAUSE_OUTGOING_CALL_BARRED, "Outgoing call barred" },
	{ AST_CAUSE_INCOMING_CALL_BARRED, "Incoming call barred" },
	{ AST_CAUSE_BEARERCAPABILITY_NOTAUTH, "Bearer capability not authorized" },
	{ AST_CAUSE_BEARERCAPABILITY_NOTAVAIL, "Bearer capability not available" },
	{ AST_CAUSE_BEARERCAPABILITY_NOTIMPL, "Bearer capability not implemented" },
	{ AST_CAUSE_CHAN_NOT_IMPLEMENTED, "Channel not implemented" },
	{ AST_CAUSE_FACILITY_NOT_IMPLEMENTED, "Facility not implemented" },
	{ AST_CAUSE_INVALID_CALL_REFERENCE, "Invalid call reference value" },
	{ AST_CAUSE_INCOMPATIBLE_DESTINATION, "Incompatible destination" },
	{ AST_CAUSE_INVALID_MSG_UNSPECIFIED, "Invalid message unspecified" },
	{ AST_CAUSE_MANDATORY_IE_MISSING, "Mandatory information element is missing" },
	{ AST_CAUSE_MESSAGE_TYPE_NONEXIST, "Message type nonexist." },
	{ AST_CAUSE_WRONG_MESSAGE, "Wrong message" },
	{ AST_CAUSE_IE_NONEXIST, "Info. element nonexist or not implemented" },
	{ AST_CAUSE_INVALID_IE_CONTENTS, "Invalid information element contents" },
	{ AST_CAUSE_WRONG_CALL_STATE, "Message not compatible with call state" },
	{ AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE, "Recover on timer expiry" },
	{ AST_CAUSE_MANDATORY_IE_LENGTH_ERROR, "Mandatory IE length error" },
	{ AST_CAUSE_PROTOCOL_ERROR, "Protocol error, unspecified" },
	{ AST_CAUSE_INTERWORKING, "Interworking, unspecified" },
};


static int show_channeltypes(int fd, int argc, char *argv[])
{
#define FORMAT  "%-10.10s  %-30.30s %-12.12s %-12.12s %-12.12s\n"
	struct chanlist *cl = backends;
	ast_cli(fd, FORMAT, "Type", "Description",       "Devicestate", "Indications", "Transfer");
	ast_cli(fd, FORMAT, "----------", "-----------", "-----------", "-----------", "--------");
	if (ast_mutex_lock(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return -1;
	}
	while (cl) {
		ast_cli(fd, FORMAT, cl->tech->type, cl->tech->description, 
			(cl->tech->devicestate) ? "yes" : "no", 
			(cl->tech->indicate) ? "yes" : "no",
			(cl->tech->transfer) ? "yes" : "no");
		cl = cl->next;
	}
	ast_mutex_unlock(&chlock);
	return RESULT_SUCCESS;

#undef FORMAT

}

static char show_channeltypes_usage[] = 
"Usage: show channeltypes\n"
"       Shows available channel types registered in your Asterisk server.\n";

static struct ast_cli_entry cli_show_channeltypes = 
	{ { "show", "channeltypes", NULL }, show_channeltypes, "Show available channel types", show_channeltypes_usage };

/*--- ast_check_hangup: Checks to see if a channel is needing hang up */
int ast_check_hangup(struct ast_channel *chan)
{
	time_t	myt;

	/* if soft hangup flag, return true */
	if (chan->_softhangup) 
		return 1;
	/* if no technology private data, return true */
	if (!chan->tech_pvt) 
		return 1;
	/* if no hangup scheduled, just return here */
	if (!chan->whentohangup) 
		return 0;
	time(&myt); /* get current time */
	/* return, if not yet */
	if (chan->whentohangup > myt) 
		return 0;
	chan->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
	return 1;
}

static int ast_check_hangup_locked(struct ast_channel *chan)
{
	int res;
	ast_mutex_lock(&chan->lock);
	res = ast_check_hangup(chan);
	ast_mutex_unlock(&chan->lock);
	return res;
}

/*--- ast_begin_shutdown: Initiate system shutdown */
void ast_begin_shutdown(int hangup)
{
	struct ast_channel *c;
	shutting_down = 1;
	if (hangup) {
		ast_mutex_lock(&chlock);
		c = channels;
		while(c) {
			ast_softhangup(c, AST_SOFTHANGUP_SHUTDOWN);
			c = c->next;
		}
		ast_mutex_unlock(&chlock);
	}
}

/*--- ast_active_channels: returns number of active/allocated channels */
int ast_active_channels(void)
{
	struct ast_channel *c;
	int cnt = 0;
	ast_mutex_lock(&chlock);
	c = channels;
	while(c) {
		cnt++;
		c = c->next;
	}
	ast_mutex_unlock(&chlock);
	return cnt;
}

/*--- ast_cancel_shutdown: Cancel a shutdown in progress */
void ast_cancel_shutdown(void)
{
	shutting_down = 0;
}

/*--- ast_shutting_down: Returns non-zero if Asterisk is being shut down */
int ast_shutting_down(void)
{
	return shutting_down;
}

/*--- ast_channel_setwhentohangup: Set when to hangup channel */
void ast_channel_setwhentohangup(struct ast_channel *chan, time_t offset)
{
	time_t	myt;
	struct ast_frame fr = { AST_FRAME_NULL, };

	time(&myt);
	if (offset)
		chan->whentohangup = myt + offset;
	else
		chan->whentohangup = 0;
	ast_queue_frame(chan, &fr);
	return;
}

/*--- ast_channel_cmpwhentohangup: Compare a offset with when to hangup channel */
int ast_channel_cmpwhentohangup(struct ast_channel *chan, time_t offset)
{
	time_t whentohangup;

	if (chan->whentohangup == 0) {
		if (offset == 0)
			return (0);
		else
			return (-1);
	} else { 
		if (offset == 0)
			return (1);
		else {
			whentohangup = offset + time (NULL);
			if (chan->whentohangup < whentohangup)
				return (1);
			else if (chan->whentohangup == whentohangup)
				return (0);
			else
				return (-1);
		}
	}
}

/*--- ast_channel_register: Register a new telephony channel in Asterisk */
int ast_channel_register(const struct ast_channel_tech *tech)
{
	struct chanlist *chan;

	ast_mutex_lock(&chlock);

	chan = backends;
	while (chan) {
		if (!strcasecmp(tech->type, chan->tech->type)) {
			ast_log(LOG_WARNING, "Already have a handler for type '%s'\n", tech->type);
			ast_mutex_unlock(&chlock);
			return -1;
		}
		chan = chan->next;
	}

	chan = malloc(sizeof(*chan));
	if (!chan) {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_mutex_unlock(&chlock);
		return -1;
	}
	chan->tech = tech;
	chan->next = backends;
	backends = chan;

	if (option_debug)
		ast_log(LOG_DEBUG, "Registered handler for '%s' (%s)\n", chan->tech->type, chan->tech->description);

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered channel type '%s' (%s)\n", chan->tech->type,
			    chan->tech->description);

	ast_mutex_unlock(&chlock);
	return 0;
}

void ast_channel_unregister(const struct ast_channel_tech *tech)
{
	struct chanlist *chan, *last=NULL;

	if (option_debug)
		ast_log(LOG_DEBUG, "Unregistering channel type '%s'\n", tech->type);

	ast_mutex_lock(&chlock);

	chan = backends;
	while (chan) {
		if (chan->tech == tech) {
			if (last)
				last->next = chan->next;
			else
				backends = backends->next;
			free(chan);
			ast_mutex_unlock(&chlock);

			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Unregistered channel type '%s'\n", tech->type);

			return;
		}
		last = chan;
		chan = chan->next;
	}

	ast_mutex_unlock(&chlock);
}

const struct ast_channel_tech *ast_get_channel_tech(const char *name)
{
	struct chanlist *chanls;

	if (ast_mutex_lock(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel tech list\n");
		return NULL;
	}

	for (chanls = backends; chanls; chanls = chanls->next) {
		if (strcasecmp(name, chanls->tech->type))
			continue;

		ast_mutex_unlock(&chlock);
		return chanls->tech;
	}

	ast_mutex_unlock(&chlock);
	return NULL;
}

/*--- ast_cause2str: Gives the string form of a given hangup cause */
const char *ast_cause2str(int cause)
{
	int x;

	for (x=0; x < sizeof(causes) / sizeof(causes[0]); x++) 
		if (causes[x].cause == cause)
			return causes[x].desc;

	return "Unknown";
}

/*--- ast_state2str: Gives the string form of a given channel state */
char *ast_state2str(int state)
{
	/* XXX Not reentrant XXX */
	static char localtmp[256];
	switch(state) {
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
	default:
		snprintf(localtmp, sizeof(localtmp), "Unknown (%d)\n", state);
		return localtmp;
	}
}

/*--- ast_transfercapability2str: Gives the string form of a given transfer capability */
char *ast_transfercapability2str(int transfercapability)
{
	switch(transfercapability) {
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

/*--- ast_best_codec: Pick the best codec */
int ast_best_codec(int fmts)
{
	/* This just our opinion, expressed in code.  We are asked to choose
	   the best codec to use, given no information */
	int x;
	static int prefs[] = 
	{
		/* Okay, ulaw is used by all telephony equipment, so start with it */
		AST_FORMAT_ULAW,
		/* Unless of course, you're a silly European, so then prefer ALAW */
		AST_FORMAT_ALAW,
		/* Okay, well, signed linear is easy to translate into other stuff */
		AST_FORMAT_SLINEAR,
		/* G.726 is standard ADPCM */
		AST_FORMAT_G726,
		/* ADPCM has great sound quality and is still pretty easy to translate */
		AST_FORMAT_ADPCM,
		/* Okay, we're down to vocoders now, so pick GSM because it's small and easier to
		   translate and sounds pretty good */
		AST_FORMAT_GSM,
		/* iLBC is not too bad */
		AST_FORMAT_ILBC,
		/* Speex is free, but computationally more expensive than GSM */
		AST_FORMAT_SPEEX,
		/* Ick, LPC10 sounds terrible, but at least we have code for it, if you're tacky enough
		   to use it */
		AST_FORMAT_LPC10,
		/* G.729a is faster than 723 and slightly less expensive */
		AST_FORMAT_G729A,
		/* Down to G.723.1 which is proprietary but at least designed for voice */
		AST_FORMAT_G723_1,
	};
	
	
	/* Find the first prefered codec in the format given */
	for (x=0; x < (sizeof(prefs) / sizeof(prefs[0]) ); x++)
		if (fmts & prefs[x])
			return prefs[x];
	ast_log(LOG_WARNING, "Don't know any of 0x%x formats\n", fmts);
	return 0;
}

static const struct ast_channel_tech null_tech = {
	.type = "NULL",
	.description = "Null channel (should not see this)",
};

/*--- ast_channel_alloc: Create a new channel structure */
struct ast_channel *ast_channel_alloc(int needqueue)
{
	struct ast_channel *tmp;
	int x;
	int flags;
	struct varshead *headp;        
	        

	/* If shutting down, don't allocate any new channels */
	if (shutting_down) {
		ast_log(LOG_WARNING, "Channel allocation failed: Refusing due to active shutdown\n");
		return NULL;
	}

	tmp = malloc(sizeof(struct ast_channel));
	if (!tmp) {
		ast_log(LOG_WARNING, "Channel allocation failed: Out of memory\n");
		return NULL;
	}

	memset(tmp, 0, sizeof(struct ast_channel));
	tmp->sched = sched_context_create();
	if (!tmp->sched) {
		ast_log(LOG_WARNING, "Channel allocation failed: Unable to create schedule context\n");
		free(tmp);
		return NULL;
	}
	
	for (x=0; x<AST_MAX_FDS - 1; x++)
		tmp->fds[x] = -1;

#ifdef ZAPTEL_OPTIMIZATIONS
	tmp->timingfd = open("/dev/zap/timer", O_RDWR);
	if (tmp->timingfd > -1) {
		/* Check if timing interface supports new
		   ping/pong scheme */
		flags = 1;
		if (!ioctl(tmp->timingfd, ZT_TIMERPONG, &flags))
			needqueue = 0;
	}
#else
	tmp->timingfd = -1;					
#endif					

	if (needqueue) {
		if (pipe(tmp->alertpipe)) {
			ast_log(LOG_WARNING, "Channel allocation failed: Can't create alert pipe!\n");
			free(tmp);
			return NULL;
		} else {
			flags = fcntl(tmp->alertpipe[0], F_GETFL);
			fcntl(tmp->alertpipe[0], F_SETFL, flags | O_NONBLOCK);
			flags = fcntl(tmp->alertpipe[1], F_GETFL);
			fcntl(tmp->alertpipe[1], F_SETFL, flags | O_NONBLOCK);
		}
	} else 
		/* Make sure we've got it done right if they don't */
		tmp->alertpipe[0] = tmp->alertpipe[1] = -1;

	/* Always watch the alertpipe */
	tmp->fds[AST_MAX_FDS-1] = tmp->alertpipe[0];
	/* And timing pipe */
	tmp->fds[AST_MAX_FDS-2] = tmp->timingfd;
	strcpy(tmp->name, "**Unkown**");
	/* Initial state */
	tmp->_state = AST_STATE_DOWN;
	tmp->streamid = -1;
	tmp->appl = NULL;
	tmp->data = NULL;
	tmp->fin = global_fin;
	tmp->fout = global_fout;
	snprintf(tmp->uniqueid, sizeof(tmp->uniqueid), "%li.%d", (long) time(NULL), uniqueint++);
	headp = &tmp->varshead;
	ast_mutex_init(&tmp->lock);
	AST_LIST_HEAD_INIT_NOLOCK(headp);
	strcpy(tmp->context, "default");
	ast_copy_string(tmp->language, defaultlanguage, sizeof(tmp->language));
	strcpy(tmp->exten, "s");
	tmp->priority = 1;
	tmp->amaflags = ast_default_amaflags;
	ast_copy_string(tmp->accountcode, ast_default_accountcode, sizeof(tmp->accountcode));

	tmp->tech = &null_tech;

	ast_mutex_lock(&chlock);
	tmp->next = channels;
	channels = tmp;

	ast_mutex_unlock(&chlock);
	return tmp;
}

/*--- ast_queue_frame: Queue an outgoing media frame */
int ast_queue_frame(struct ast_channel *chan, struct ast_frame *fin)
{
	struct ast_frame *f;
	struct ast_frame *prev, *cur;
	int blah = 1;
	int qlen = 0;

	/* Build us a copy and free the original one */
	f = ast_frdup(fin);
	if (!f) {
		ast_log(LOG_WARNING, "Unable to duplicate frame\n");
		return -1;
	}
	ast_mutex_lock(&chan->lock);
	prev = NULL;
	cur = chan->readq;
	while(cur) {
		if ((cur->frametype == AST_FRAME_CONTROL) && (cur->subclass == AST_CONTROL_HANGUP)) {
			/* Don't bother actually queueing anything after a hangup */
			ast_frfree(f);
			ast_mutex_unlock(&chan->lock);
			return 0;
		}
		prev = cur;
		cur = cur->next;
		qlen++;
	}
	/* Allow up to 96 voice frames outstanding, and up to 128 total frames */
	if (((fin->frametype == AST_FRAME_VOICE) && (qlen > 96)) || (qlen  > 128)) {
		if (fin->frametype != AST_FRAME_VOICE) {
			ast_log(LOG_WARNING, "Exceptionally long queue length queuing to %s\n", chan->name);
			CRASH;
		} else {
			ast_log(LOG_DEBUG, "Dropping voice to exceptionally long queue on %s\n", chan->name);
			ast_frfree(f);
			ast_mutex_unlock(&chan->lock);
			return 0;
		}
	}
	if (prev)
		prev->next = f;
	else
		chan->readq = f;
	if (chan->alertpipe[1] > -1) {
		if (write(chan->alertpipe[1], &blah, sizeof(blah)) != sizeof(blah))
			ast_log(LOG_WARNING, "Unable to write to alert pipe on %s, frametype/subclass %d/%d (qlen = %d): %s!\n",
				chan->name, f->frametype, f->subclass, qlen, strerror(errno));
#ifdef ZAPTEL_OPTIMIZATIONS
	} else if (chan->timingfd > -1) {
		ioctl(chan->timingfd, ZT_TIMERPING, &blah);
#endif				
	} else if (ast_test_flag(chan, AST_FLAG_BLOCKING)) {
		pthread_kill(chan->blocker, SIGURG);
	}
	ast_mutex_unlock(&chan->lock);
	return 0;
}

/*--- ast_queue_hangup: Queue a hangup frame for channel */
int ast_queue_hangup(struct ast_channel *chan)
{
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_HANGUP };
	chan->_softhangup |= AST_SOFTHANGUP_DEV;
	return ast_queue_frame(chan, &f);
}

/*--- ast_queue_control: Queue a control frame */
int ast_queue_control(struct ast_channel *chan, int control)
{
	struct ast_frame f = { AST_FRAME_CONTROL, };
	f.subclass = control;
	return ast_queue_frame(chan, &f);
}

/*--- ast_channel_defer_dtmf: Set defer DTMF flag on channel */
int ast_channel_defer_dtmf(struct ast_channel *chan)
{
	int pre = 0;

	if (chan) {
		pre = ast_test_flag(chan, AST_FLAG_DEFER_DTMF);
		ast_set_flag(chan, AST_FLAG_DEFER_DTMF);
	}
	return pre;
}

/*--- ast_channel_undefer_dtmf: Unset defer DTMF flag on channel */
void ast_channel_undefer_dtmf(struct ast_channel *chan)
{
	if (chan)
		ast_clear_flag(chan, AST_FLAG_DEFER_DTMF);
}

/*
 * Helper function to find channels. It supports these modes:
 *
 * prev != NULL : get channel next in list after prev
 * name != NULL : get channel with matching name
 * name != NULL && namelen != 0 : get channel whose name starts with prefix
 * exten != NULL : get channel whose exten or macroexten matches
 * context != NULL && exten != NULL : get channel whose context or macrocontext
 *                                    
 * It returns with the channel's lock held. If getting the individual lock fails,
 * unlock and retry quickly up to 10 times, then give up.
 * 
 * XXX Note that this code has cost O(N) because of the need to verify
 * that the object is still on the global list.
 *
 * XXX also note that accessing fields (e.g. c->name in ast_log())
 * can only be done with the lock held or someone could delete the
 * object while we work on it. This causes some ugliness in the code.
 * Note that removing the first ast_log() may be harmful, as it would
 * shorten the retry period and possibly cause failures.
 * We should definitely go for a better scheme that is deadlock-free.
 */
static struct ast_channel *channel_find_locked(const struct ast_channel *prev,
					       const char *name, const int namelen,
					       const char *context, const char *exten)
{
	const char *msg = prev ? "deadlock" : "initial deadlock";
	int retries, done;
	struct ast_channel *c;

	for (retries = 0; retries < 10; retries++) {
		ast_mutex_lock(&chlock);
		for (c = channels; c; c = c->next) {
			if (!prev) {
				/* want head of list */
				if (!name && !exten)
					break;
				if (name) {
					/* want match by full name */
					if (!namelen) {
						if (!strcasecmp(c->name, name))
							break;
						else
							continue;
					}
					/* want match by name prefix */
					if (!strncasecmp(c->name, name, namelen))
						break;
				} else if (exten) {
					/* want match by context and exten */
					if (context && (strcasecmp(c->context, context) &&
							strcasecmp(c->macrocontext, context)))
						continue;
					/* match by exten */
					if (strcasecmp(c->exten, exten) &&
					    strcasecmp(c->macroexten, exten))
						continue;
					else
						break;
				}
			} else if (c == prev) { /* found, return c->next */
				c = c->next;
				break;
			}
		}
		/* exit if chan not found or mutex acquired successfully */
		done = (c == NULL) || (ast_mutex_trylock(&c->lock) == 0);
		/* this is slightly unsafe, as we _should_ hold the lock to access c->name */
		if (!done && c)
			ast_log(LOG_DEBUG, "Avoiding %s for '%s'\n", msg, c->name);
		ast_mutex_unlock(&chlock);
		if (done)
			return c;
		usleep(1);
	}
	/*
 	 * c is surely not null, but we don't have the lock so cannot
	 * access c->name
	 */
	ast_log(LOG_WARNING, "Avoided %s for '%p', %d retries!\n",
		msg, c, retries);

	return NULL;
}

/*--- ast_channel_walk_locked: Browse channels in use */
struct ast_channel *ast_channel_walk_locked(const struct ast_channel *prev)
{
	return channel_find_locked(prev, NULL, 0, NULL, NULL);
}

/*--- ast_get_channel_by_name_locked: Get channel by name and lock it */
struct ast_channel *ast_get_channel_by_name_locked(const char *name)
{
	return channel_find_locked(NULL, name, 0, NULL, NULL);
}

/*--- ast_get_channel_by_name_prefix_locked: Get channel by name prefix and lock it */
struct ast_channel *ast_get_channel_by_name_prefix_locked(const char *name, const int namelen)
{
	return channel_find_locked(NULL, name, namelen, NULL, NULL);
}

/*--- ast_walk_channel_by_name_prefix_locked: Get next channel by name prefix and lock it */
struct ast_channel *ast_walk_channel_by_name_prefix_locked(struct ast_channel *chan, const char *name, const int namelen)
{
	return channel_find_locked(chan, name, namelen, NULL, NULL);
}

/*--- ast_get_channel_by_exten_locked: Get channel by exten (and optionally context) and lock it */
struct ast_channel *ast_get_channel_by_exten_locked(const char *exten, const char *context)
{
	return channel_find_locked(NULL, NULL, 0, context, exten);
}

/*--- ast_safe_sleep_conditional: Wait, look for hangups and condition arg */
int ast_safe_sleep_conditional(	struct ast_channel *chan, int ms,
	int (*cond)(void*), void *data )
{
	struct ast_frame *f;

	while(ms > 0) {
		if( cond && ((*cond)(data) == 0 ) )
			return 0;
		ms = ast_waitfor(chan, ms);
		if (ms <0)
			return -1;
		if (ms > 0) {
			f = ast_read(chan);
			if (!f)
				return -1;
			ast_frfree(f);
		}
	}
	return 0;
}

/*--- ast_safe_sleep: Wait, look for hangups */
int ast_safe_sleep(struct ast_channel *chan, int ms)
{
	struct ast_frame *f;
	while(ms > 0) {
		ms = ast_waitfor(chan, ms);
		if (ms <0)
			return -1;
		if (ms > 0) {
			f = ast_read(chan);
			if (!f)
				return -1;
			ast_frfree(f);
		}
	}
	return 0;
}

static void free_cid(struct ast_callerid *cid)
{
	if (cid->cid_dnid)
		free(cid->cid_dnid);
	if (cid->cid_num)
		free(cid->cid_num);	
	if (cid->cid_name)
		free(cid->cid_name);	
	if (cid->cid_ani)
		free(cid->cid_ani);
	if (cid->cid_rdnis)
		free(cid->cid_rdnis);
}

/*--- ast_channel_free: Free a channel structure */
void ast_channel_free(struct ast_channel *chan)
{
	struct ast_channel *last=NULL, *cur;
	int fd;
	struct ast_var_t *vardata;
	struct ast_frame *f, *fp;
	struct varshead *headp;
	char name[AST_CHANNEL_NAME];
	
	headp=&chan->varshead;
	
	ast_mutex_lock(&chlock);
	cur = channels;
	while(cur) {
		if (cur == chan) {
			if (last)
				last->next = cur->next;
			else
				channels = cur->next;
			break;
		}
		last = cur;
		cur = cur->next;
	}
	if (!cur)
		ast_log(LOG_WARNING, "Unable to find channel in list\n");
	else {
		/* Lock and unlock the channel just to be sure nobody
		   has it locked still */
		ast_mutex_lock(&cur->lock);
		ast_mutex_unlock(&cur->lock);
	}
	if (chan->tech_pvt) {
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n", chan->name);
		free(chan->tech_pvt);
	}

	ast_copy_string(name, chan->name, sizeof(name));
	
	/* Stop monitoring */
	if (chan->monitor) {
		chan->monitor->stop( chan, 0 );
	}

	/* If there is native format music-on-hold state, free it */
	if(chan->music_state)
		ast_moh_cleanup(chan);

	/* Free translatosr */
	if (chan->readtrans)
		ast_translator_free_path(chan->readtrans);
	if (chan->writetrans)
		ast_translator_free_path(chan->writetrans);
	if (chan->pbx) 
		ast_log(LOG_WARNING, "PBX may not have been terminated properly on '%s'\n", chan->name);
	free_cid(&chan->cid);
	ast_mutex_destroy(&chan->lock);
	/* Close pipes if appropriate */
	if ((fd = chan->alertpipe[0]) > -1)
		close(fd);
	if ((fd = chan->alertpipe[1]) > -1)
		close(fd);
	if ((fd = chan->timingfd) > -1)
		close(fd);
	f = chan->readq;
	chan->readq = NULL;
	while(f) {
		fp = f;
		f = f->next;
		ast_frfree(fp);
	}
	
	/* loop over the variables list, freeing all data and deleting list items */
	/* no need to lock the list, as the channel is already locked */
	
	while ((vardata = AST_LIST_REMOVE_HEAD(headp, entries)))
		ast_var_delete(vardata);

	free(chan);
	ast_mutex_unlock(&chlock);

	ast_device_state_changed_literal(name);
}

int ast_channel_spy_add(struct ast_channel *chan, struct ast_channel_spy *spy)
{
	if (!ast_test_flag(spy, CHANSPY_FORMAT_AUDIO)) {
		ast_log(LOG_WARNING, "Could not add channel spy '%s' to channel '%s', only audio format spies are supported.\n",
			spy->type, chan->name);
		return -1;
	}

	if (ast_test_flag(spy, CHANSPY_READ_VOLADJUST) && (spy->read_queue.format != AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Cannot provide volume adjustment on '%s' format spies\n",
			ast_getformatname(spy->read_queue.format));
		return -1;
	}

	if (ast_test_flag(spy, CHANSPY_WRITE_VOLADJUST) && (spy->write_queue.format != AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Cannot provide volume adjustment on '%s' format spies\n",
			ast_getformatname(spy->write_queue.format));
		return -1;
	}

	if (ast_test_flag(spy, CHANSPY_MIXAUDIO) &&
	    ((spy->read_queue.format != AST_FORMAT_SLINEAR) ||
	     (spy->write_queue.format != AST_FORMAT_SLINEAR))) {
		ast_log(LOG_WARNING, "Cannot provide audio mixing on '%s'-'%s' format spies\n",
			ast_getformatname(spy->read_queue.format), ast_getformatname(spy->write_queue.format));
		return -1;
	}

	if (!chan->spies) {
		if (!(chan->spies = calloc(1, sizeof(*chan->spies)))) {
			ast_log(LOG_WARNING, "Memory allocation failure\n");
			return -1;
		}

		AST_LIST_HEAD_INIT_NOLOCK(&chan->spies->list);
		AST_LIST_INSERT_HEAD(&chan->spies->list, spy, list);
	} else {
		AST_LIST_INSERT_TAIL(&chan->spies->list, spy, list);
	}

	if (ast_test_flag(spy, CHANSPY_TRIGGER_MODE) != CHANSPY_TRIGGER_NONE) {
		ast_cond_init(&spy->trigger, NULL);
		ast_set_flag(spy, CHANSPY_TRIGGER_READ);
		ast_clear_flag(spy, CHANSPY_TRIGGER_WRITE);
	}

	ast_log(LOG_DEBUG, "Spy %s added to channel %s\n",
		spy->type, chan->name);

	return 0;
}

void ast_channel_spy_stop_by_type(struct ast_channel *chan, const char *type)
{
	struct ast_channel_spy *spy;
	
	if (!chan->spies)
		return;

	AST_LIST_TRAVERSE(&chan->spies->list, spy, list) {
		if ((spy->type == type) && (spy->status == CHANSPY_RUNNING))
			spy->status = CHANSPY_DONE;
	}
}

void ast_channel_spy_trigger_wait(struct ast_channel_spy *spy)
{
	ast_cond_wait(&spy->trigger, &spy->lock);
}

void ast_channel_spy_remove(struct ast_channel *chan, struct ast_channel_spy *spy)
{
	struct ast_frame *f;

	if (!chan->spies)
		return;

	AST_LIST_REMOVE(&chan->spies->list, spy, list);

	ast_mutex_lock(&spy->lock);

	for (f = spy->read_queue.head; f; f = spy->read_queue.head) {
		spy->read_queue.head = f->next;
		ast_frfree(f);
	}
	for (f = spy->write_queue.head; f; f = spy->write_queue.head) {
		spy->write_queue.head = f->next;
		ast_frfree(f);
	}

	if (ast_test_flag(spy, CHANSPY_TRIGGER_MODE) != CHANSPY_TRIGGER_NONE)
		ast_cond_destroy(&spy->trigger);

	ast_mutex_unlock(&spy->lock);

	ast_log(LOG_DEBUG, "Spy %s removed from channel %s\n",
		spy->type, chan->name);

	if (AST_LIST_EMPTY(&chan->spies->list)) {
		if (chan->spies->read_translator.path)
			ast_translator_free_path(chan->spies->read_translator.path);
		if (chan->spies->write_translator.path)
			ast_translator_free_path(chan->spies->write_translator.path);
		free(chan->spies);
		chan->spies = NULL;
	}
}

static void detach_spies(struct ast_channel *chan) 
{
	struct ast_channel_spy *spy;

	if (!chan->spies)
		return;

	/* Marking the spies as done is sufficient.  Chanspy or spy users will get the picture. */
	AST_LIST_TRAVERSE(&chan->spies->list, spy, list) {
		ast_mutex_lock(&spy->lock);
		if (spy->status == CHANSPY_RUNNING)
			spy->status = CHANSPY_DONE;
		if (ast_test_flag(spy, CHANSPY_TRIGGER_MODE) != CHANSPY_TRIGGER_NONE)
			ast_cond_signal(&spy->trigger);
		ast_mutex_unlock(&spy->lock);
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(&chan->spies->list, spy, list)
		ast_channel_spy_remove(chan, spy);
	AST_LIST_TRAVERSE_SAFE_END;
}

/*--- ast_softhangup_nolock: Softly hangup a channel, don't lock */
int ast_softhangup_nolock(struct ast_channel *chan, int cause)
{
	int res = 0;
	struct ast_frame f = { AST_FRAME_NULL };
	if (option_debug)
		ast_log(LOG_DEBUG, "Soft-Hanging up channel '%s'\n", chan->name);
	/* Inform channel driver that we need to be hung up, if it cares */
	chan->_softhangup |= cause;
	ast_queue_frame(chan, &f);
	/* Interrupt any poll call or such */
	if (ast_test_flag(chan, AST_FLAG_BLOCKING))
		pthread_kill(chan->blocker, SIGURG);
	return res;
}

/*--- ast_softhangup_nolock: Softly hangup a channel, lock */
int ast_softhangup(struct ast_channel *chan, int cause)
{
	int res;
	ast_mutex_lock(&chan->lock);
	res = ast_softhangup_nolock(chan, cause);
	ast_mutex_unlock(&chan->lock);
	return res;
}

enum spy_direction {
	SPY_READ,
	SPY_WRITE,
};

#define SPY_QUEUE_SAMPLE_LIMIT 4000			/* half of one second */

static void queue_frame_to_spies(struct ast_channel *chan, struct ast_frame *f, enum spy_direction dir)
{
	struct ast_frame *translated_frame = NULL;
	struct ast_channel_spy *spy;
	struct ast_channel_spy_queue *queue;
	struct ast_channel_spy_queue *other_queue;
	struct channel_spy_trans *trans;
	struct ast_frame *last;

	trans = (dir == SPY_READ) ? &chan->spies->read_translator : &chan->spies->write_translator;

	AST_LIST_TRAVERSE(&chan->spies->list, spy, list) {
		ast_mutex_lock(&spy->lock);

		queue = (dir == SPY_READ) ? &spy->read_queue : &spy->write_queue;

		if ((queue->format == AST_FORMAT_SLINEAR) && (f->subclass != AST_FORMAT_SLINEAR)) {
			if (!translated_frame) {
				if (trans->path && (trans->last_format != f->subclass)) {
					ast_translator_free_path(trans->path);
					trans->path = NULL;
				}
				if (!trans->path) {
					ast_log(LOG_DEBUG, "Building translator from %s to SLINEAR for spies on channel %s\n",
						ast_getformatname(f->subclass), chan->name);
					if ((trans->path = ast_translator_build_path(AST_FORMAT_SLINEAR, f->subclass)) == NULL) {
						ast_log(LOG_WARNING, "Cannot build a path from %s to %s\n",
							ast_getformatname(f->subclass), ast_getformatname(AST_FORMAT_SLINEAR));
						ast_mutex_unlock(&spy->lock);
						continue;
					} else {
						trans->last_format = f->subclass;
					}
				}
				translated_frame = ast_translate(trans->path, f, 0);
			}

			for (last = queue->head; last && last->next; last = last->next);
			if (last)
				last->next = ast_frdup(translated_frame);
			else
				queue->head = ast_frdup(translated_frame);
		} else {
			if (f->subclass != queue->format) {
				ast_log(LOG_WARNING, "Spy '%s' on channel '%s' wants format '%s', but frame is '%s', dropping\n",
					spy->type, chan->name,
					ast_getformatname(queue->format), ast_getformatname(f->subclass));
				ast_mutex_unlock(&spy->lock);
				continue;
			}

			for (last = queue->head; last && last->next; last = last->next);
			if (last)
				last->next = ast_frdup(f);
			else
				queue->head = ast_frdup(f);
		}

		queue->samples += f->samples;

		if (queue->samples > SPY_QUEUE_SAMPLE_LIMIT) {
			if (ast_test_flag(spy, CHANSPY_TRIGGER_MODE) != CHANSPY_TRIGGER_NONE) {
				other_queue = (dir == SPY_WRITE) ? &spy->read_queue : &spy->write_queue;

				if (other_queue->samples == 0) {
					switch (ast_test_flag(spy, CHANSPY_TRIGGER_MODE)) {
					case CHANSPY_TRIGGER_READ:
						if (dir == SPY_WRITE) {
							ast_set_flag(spy, CHANSPY_TRIGGER_WRITE);
							ast_clear_flag(spy, CHANSPY_TRIGGER_READ);
							if (option_debug)
								ast_log(LOG_DEBUG, "Switching spy '%s' on '%s' to write-trigger mode\n",
									spy->type, chan->name);
						}
						break;
					case CHANSPY_TRIGGER_WRITE:
						if (dir == SPY_READ) {
							ast_set_flag(spy, CHANSPY_TRIGGER_READ);
							ast_clear_flag(spy, CHANSPY_TRIGGER_WRITE);
							if (option_debug)
								ast_log(LOG_DEBUG, "Switching spy '%s' on '%s' to read-trigger mode\n",
									spy->type, chan->name);
						}
						break;
					}
					if (option_debug)
						ast_log(LOG_DEBUG, "Triggering queue flush for spy '%s' on '%s'\n",
							spy->type, chan->name);
					ast_set_flag(spy, CHANSPY_TRIGGER_FLUSH);
					ast_cond_signal(&spy->trigger);
					ast_mutex_unlock(&spy->lock);
					continue;
				}
			}

			if (option_debug)
				ast_log(LOG_DEBUG, "Spy '%s' on channel '%s' %s queue too long, dropping frames\n",
					spy->type, chan->name, (dir == SPY_READ) ? "read" : "write");
			while (queue->samples > SPY_QUEUE_SAMPLE_LIMIT) {
				struct ast_frame *drop = queue->head;

				queue->samples -= drop->samples;
				queue->head = drop->next;
				ast_frfree(drop);
			}
		} else {
			switch (ast_test_flag(spy, CHANSPY_TRIGGER_MODE)) {
			case CHANSPY_TRIGGER_READ:
				if (dir == SPY_READ)
					ast_cond_signal(&spy->trigger);
				break;
			case CHANSPY_TRIGGER_WRITE:
				if (dir == SPY_WRITE)
					ast_cond_signal(&spy->trigger);
				break;
			}
		}

		ast_mutex_unlock(&spy->lock);
	}

	if (translated_frame)
		ast_frfree(translated_frame);
}

static void free_translation(struct ast_channel *clone)
{
	if (clone->writetrans)
		ast_translator_free_path(clone->writetrans);
	if (clone->readtrans)
		ast_translator_free_path(clone->readtrans);
	clone->writetrans = NULL;
	clone->readtrans = NULL;
	clone->rawwriteformat = clone->nativeformats;
	clone->rawreadformat = clone->nativeformats;
}

/*--- ast_hangup: Hangup a channel */
int ast_hangup(struct ast_channel *chan)
{
	int res = 0;

	/* Don't actually hang up a channel that will masquerade as someone else, or
	   if someone is going to masquerade as us */
	ast_mutex_lock(&chan->lock);

	detach_spies(chan);		/* get rid of spies */

	if (chan->masq) {
		if (ast_do_masquerade(chan)) 
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
	}

	if (chan->masq) {
		ast_log(LOG_WARNING, "%s getting hung up, but someone is trying to masq into us?!?\n", chan->name);
		ast_mutex_unlock(&chan->lock);
		return 0;
	}
	/* If this channel is one which will be masqueraded into something, 
	   mark it as a zombie already, so we know to free it later */
	if (chan->masqr) {
		ast_set_flag(chan, AST_FLAG_ZOMBIE);
		ast_mutex_unlock(&chan->lock);
		return 0;
	}
	free_translation(chan);
	if (chan->stream) 		/* Close audio stream */
		ast_closestream(chan->stream);
	if (chan->vstream)		/* Close video stream */
		ast_closestream(chan->vstream);
	if (chan->sched)
		sched_context_destroy(chan->sched);
	
	if (chan->generatordata)	/* Clear any tone stuff remaining */ 
		chan->generator->release(chan, chan->generatordata);
	chan->generatordata = NULL;
	chan->generator = NULL;
	if (chan->cdr) {		/* End the CDR if it hasn't already */ 
		ast_cdr_end(chan->cdr);
		ast_cdr_detach(chan->cdr);	/* Post and Free the CDR */ 
		chan->cdr = NULL;
	}
	if (ast_test_flag(chan, AST_FLAG_BLOCKING)) {
		ast_log(LOG_WARNING, "Hard hangup called by thread %ld on %s, while fd "
					"is blocked by thread %ld in procedure %s!  Expect a failure\n",
					(long)pthread_self(), chan->name, (long)chan->blocker, chan->blockproc);
		CRASH;
	}
	if (!ast_test_flag(chan, AST_FLAG_ZOMBIE)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Hanging up channel '%s'\n", chan->name);
		if (chan->tech->hangup)
			res = chan->tech->hangup(chan);
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Hanging up zombie '%s'\n", chan->name);
	}
			
	ast_mutex_unlock(&chan->lock);
	manager_event(EVENT_FLAG_CALL, "Hangup", 
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Cause: %d\r\n"
			"Cause-txt: %s\r\n",
			chan->name, 
			chan->uniqueid, 
			chan->hangupcause,
			ast_cause2str(chan->hangupcause)
			);
	ast_channel_free(chan);
	return res;
}

int ast_answer(struct ast_channel *chan)
{
	int res = 0;
	ast_mutex_lock(&chan->lock);
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		ast_mutex_unlock(&chan->lock);
		return -1;
	}
	switch(chan->_state) {
	case AST_STATE_RINGING:
	case AST_STATE_RING:
		if (chan->tech->answer)
			res = chan->tech->answer(chan);
		ast_setstate(chan, AST_STATE_UP);
		if (chan->cdr)
			ast_cdr_answer(chan->cdr);
		ast_mutex_unlock(&chan->lock);
		return res;
		break;
	case AST_STATE_UP:
		if (chan->cdr)
			ast_cdr_answer(chan->cdr);
		break;
	}
	ast_mutex_unlock(&chan->lock);
	return 0;
}



void ast_deactivate_generator(struct ast_channel *chan)
{
	ast_mutex_lock(&chan->lock);
	if (chan->generatordata) {
		if (chan->generator && chan->generator->release) 
			chan->generator->release(chan, chan->generatordata);
		chan->generatordata = NULL;
		chan->generator = NULL;
		ast_clear_flag(chan, AST_FLAG_WRITE_INT);
		ast_settimeout(chan, 0, NULL, NULL);
	}
	ast_mutex_unlock(&chan->lock);
}

static int generator_force(void *data)
{
	/* Called if generator doesn't have data */
	void *tmp;
	int res;
	int (*generate)(struct ast_channel *chan, void *tmp, int datalen, int samples);
	struct ast_channel *chan = data;
	tmp = chan->generatordata;
	chan->generatordata = NULL;
	generate = chan->generator->generate;
	res = generate(chan, tmp, 0, 160);
	chan->generatordata = tmp;
	if (res) {
		ast_log(LOG_DEBUG, "Auto-deactivating generator\n");
		ast_deactivate_generator(chan);
	}
	return 0;
}

int ast_activate_generator(struct ast_channel *chan, struct ast_generator *gen, void *params)
{
	int res = 0;

	ast_mutex_lock(&chan->lock);

	if (chan->generatordata) {
		if (chan->generator && chan->generator->release)
			chan->generator->release(chan, chan->generatordata);
		chan->generatordata = NULL;
	}

	ast_prod(chan);
	if (gen->alloc) {
		if (!(chan->generatordata = gen->alloc(chan, params)))
			res = -1;
	}
	
	if (!res) {
		ast_settimeout(chan, 160, generator_force, chan);
		chan->generator = gen;
	}

	ast_mutex_unlock(&chan->lock);

	return res;
}

/*--- ast_waitfor_n_fd: Wait for x amount of time on a file descriptor to have input.  */
int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception)
{
	struct timeval start = { 0 , 0 };
	int res;
	int x, y;
	int winner = -1;
	int spoint;
	struct pollfd *pfds;
	
	pfds = alloca(sizeof(struct pollfd) * n);
	if (!pfds) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	if (*ms > 0)
		start = ast_tvnow();
	y = 0;
	for (x=0; x < n; x++) {
		if (fds[x] > -1) {
			pfds[y].fd = fds[x];
			pfds[y].events = POLLIN | POLLPRI;
			y++;
		}
	}
	res = poll(pfds, y, *ms);
	if (res < 0) {
		/* Simulate a timeout if we were interrupted */
		if (errno != EINTR)
			*ms = -1;
		else
			*ms = 0;
		return -1;
	}
	spoint = 0;
	for (x=0; x < n; x++) {
		if (fds[x] > -1) {
			if ((res = ast_fdisset(pfds, fds[x], y, &spoint))) {
				winner = fds[x];
				if (exception) {
					if (res & POLLPRI)
						*exception = -1;
					else
						*exception = 0;
				}
			}
		}
	}
	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0)
			*ms = 0;
	}
	return winner;
}

/*--- ast_waitfor_nanfds: Wait for x amount of time on a file descriptor to have input.  */
struct ast_channel *ast_waitfor_nandfds(struct ast_channel **c, int n, int *fds, int nfds, 
	int *exception, int *outfd, int *ms)
{
	struct timeval start = { 0 , 0 };
	struct pollfd *pfds;
	int res;
	long rms;
	int x, y, max;
	int spoint;
	time_t now = 0;
	long whentohangup = 0, havewhen = 0, diff;
	struct ast_channel *winner = NULL;

	pfds = alloca(sizeof(struct pollfd) * (n * AST_MAX_FDS + nfds));
	if (!pfds) {
		ast_log(LOG_ERROR, "Out of memory\n");
		*outfd = -1;
		return NULL;
	}

	if (outfd)
		*outfd = -99999;
	if (exception)
		*exception = 0;
	
	/* Perform any pending masquerades */
	for (x=0; x < n; x++) {
		ast_mutex_lock(&c[x]->lock);
		if (c[x]->whentohangup) {
			if (!havewhen)
				time(&now);
			diff = c[x]->whentohangup - now;
			if (!havewhen || (diff < whentohangup)) {
				havewhen++;
				whentohangup = diff;
			}
		}
		if (c[x]->masq) {
			if (ast_do_masquerade(c[x])) {
				ast_log(LOG_WARNING, "Masquerade failed\n");
				*ms = -1;
				ast_mutex_unlock(&c[x]->lock);
				return NULL;
			}
		}
		ast_mutex_unlock(&c[x]->lock);
	}

	rms = *ms;
	
	if (havewhen) {
		if ((*ms < 0) || (whentohangup * 1000 < *ms)) {
			rms =  whentohangup * 1000;
		}
	}
	max = 0;
	for (x=0; x < n; x++) {
		for (y=0; y< AST_MAX_FDS; y++) {
			if (c[x]->fds[y] > -1) {
				pfds[max].fd = c[x]->fds[y];
				pfds[max].events = POLLIN | POLLPRI;
				pfds[max].revents = 0;
				max++;
			}
		}
		CHECK_BLOCKING(c[x]);
	}
	for (x=0; x < nfds; x++) {
		if (fds[x] > -1) {
			pfds[max].fd = fds[x];
			pfds[max].events = POLLIN | POLLPRI;
			pfds[max].revents = 0;
			max++;
		}
	}
	if (*ms > 0) 
		start = ast_tvnow();
	
	if (sizeof(int) == 4) {
		do {
			int kbrms = rms;
			if (kbrms > 600000)
				kbrms = 600000;
			res = poll(pfds, max, kbrms);
			if (!res)
				rms -= kbrms;
		} while (!res && (rms > 0));
	} else {
		res = poll(pfds, max, rms);
	}
	
	if (res < 0) {
		for (x=0; x < n; x++) 
			ast_clear_flag(c[x], AST_FLAG_BLOCKING);
		/* Simulate a timeout if we were interrupted */
		if (errno != EINTR)
			*ms = -1;
		else {
			/* Just an interrupt */
#if 0
			*ms = 0;
#endif			
		}
		return NULL;
        } else {
        	/* If no fds signalled, then timeout. So set ms = 0
		   since we may not have an exact timeout.
		*/
		if (res == 0)
			*ms = 0;
	}

	if (havewhen)
		time(&now);
	spoint = 0;
	for (x=0; x < n; x++) {
		ast_clear_flag(c[x], AST_FLAG_BLOCKING);
		if (havewhen && c[x]->whentohangup && (now > c[x]->whentohangup)) {
			c[x]->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
			if (!winner)
				winner = c[x];
		}
		for (y=0; y < AST_MAX_FDS; y++) {
			if (c[x]->fds[y] > -1) {
				if ((res = ast_fdisset(pfds, c[x]->fds[y], max, &spoint))) {
					if (res & POLLPRI)
						ast_set_flag(c[x], AST_FLAG_EXCEPTION);
					else
						ast_clear_flag(c[x], AST_FLAG_EXCEPTION);
					c[x]->fdno = y;
					winner = c[x];
				}
			}
		}
	}
	for (x=0; x < nfds; x++) {
		if (fds[x] > -1) {
			if ((res = ast_fdisset(pfds, fds[x], max, &spoint))) {
				if (outfd)
					*outfd = fds[x];
				if (exception) {	
					if (res & POLLPRI) 
						*exception = -1;
					else
						*exception = 0;
				}
				winner = NULL;
			}
		}	
	}
	if (*ms > 0) {
		*ms -= ast_tvdiff_ms(ast_tvnow(), start);
		if (*ms < 0)
			*ms = 0;
	}
	return winner;
}

struct ast_channel *ast_waitfor_n(struct ast_channel **c, int n, int *ms)
{
	return ast_waitfor_nandfds(c, n, NULL, 0, NULL, NULL, ms);
}

int ast_waitfor(struct ast_channel *c, int ms)
{
	struct ast_channel *chan;
	int oldms = ms;

	chan = ast_waitfor_n(&c, 1, &ms);
	if (ms < 0) {
		if (oldms < 0)
			return 0;
		else
			return -1;
	}
	return ms;
}

int ast_waitfordigit(struct ast_channel *c, int ms)
{
	/* XXX Should I be merged with waitfordigit_full XXX */
	struct ast_frame *f;
	int result = 0;

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c, AST_FLAG_ZOMBIE) || ast_check_hangup(c)) 
		return -1;

	/* Wait for a digit, no more than ms milliseconds total. */
	while(ms && !result) {
		ms = ast_waitfor(c, ms);
		if (ms < 0) /* Error */
			result = -1; 
		else if (ms > 0) {
			/* Read something */
			f = ast_read(c);
			if (f) {
				if (f->frametype == AST_FRAME_DTMF) 
					result = f->subclass;
				ast_frfree(f);
			} else
				result = -1;
		}
	}
	return result;
}

int ast_settimeout(struct ast_channel *c, int samples, int (*func)(void *data), void *data)
{
	int res = -1;
#ifdef ZAPTEL_OPTIMIZATIONS
	if (c->timingfd > -1) {
		if (!func) {
			samples = 0;
			data = 0;
		}
		ast_log(LOG_DEBUG, "Scheduling timer at %d sample intervals\n", samples);
		res = ioctl(c->timingfd, ZT_TIMERCONFIG, &samples);
		c->timingfunc = func;
		c->timingdata = data;
	}
#endif	
	return res;
}

int ast_waitfordigit_full(struct ast_channel *c, int ms, int audiofd, int cmdfd)
{
	struct ast_frame *f;
	struct ast_channel *rchan;
	int outfd;
	int res;

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c, AST_FLAG_ZOMBIE) || ast_check_hangup(c)) 
		return -1;
	/* Wait for a digit, no more than ms milliseconds total. */
	while(ms) {
		errno = 0;
		rchan = ast_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);
		if ((!rchan) && (outfd < 0) && (ms)) { 
			if (errno == 0 || errno == EINTR)
				continue;
			ast_log(LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
			return -1;
		} else if (outfd > -1) {
			/* The FD we were watching has something waiting */
			return 1;
		} else if (rchan) {
			f = ast_read(c);
			if(!f) {
				return -1;
			}

			switch(f->frametype) {
			case AST_FRAME_DTMF:
				res = f->subclass;
				ast_frfree(f);
				return res;
			case AST_FRAME_CONTROL:
				switch(f->subclass) {
				case AST_CONTROL_HANGUP:
					ast_frfree(f);
					return -1;
				case AST_CONTROL_RINGING:
				case AST_CONTROL_ANSWER:
					/* Unimportant */
					break;
				default:
					ast_log(LOG_WARNING, "Unexpected control subclass '%d'\n", f->subclass);
				}
			case AST_FRAME_VOICE:
				/* Write audio if appropriate */
				if (audiofd > -1)
					write(audiofd, f->data, f->datalen);
			}
			/* Ignore */
			ast_frfree(f);
		}
	}
	return 0; /* Time is up */
}

struct ast_frame *ast_read(struct ast_channel *chan)
{
	struct ast_frame *f = NULL;
	int blah;
	int prestate;
#ifdef ZAPTEL_OPTIMIZATIONS
	int (*func)(void *);
	void *data;
	int res;
#endif
	static struct ast_frame null_frame = {
		AST_FRAME_NULL,
	};
	
	ast_mutex_lock(&chan->lock);
	if (chan->masq) {
		if (ast_do_masquerade(chan)) {
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
			f = NULL;
		} else
			f =  &null_frame;
		ast_mutex_unlock(&chan->lock);
		return f;
	}

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) {
		if (chan->generator)
			ast_deactivate_generator(chan);
		ast_mutex_unlock(&chan->lock);
		return NULL;
	}
	prestate = chan->_state;

	if (!ast_test_flag(chan, AST_FLAG_DEFER_DTMF) && !ast_strlen_zero(chan->dtmfq)) {
		/* We have DTMF that has been deferred.  Return it now */
		chan->dtmff.frametype = AST_FRAME_DTMF;
		chan->dtmff.subclass = chan->dtmfq[0];
		/* Drop first digit */
		memmove(chan->dtmfq, chan->dtmfq + 1, sizeof(chan->dtmfq) - 1);
		ast_mutex_unlock(&chan->lock);
		return &chan->dtmff;
	}
	
	/* Read and ignore anything on the alertpipe, but read only
	   one sizeof(blah) per frame that we send from it */
	if (chan->alertpipe[0] > -1) {
		read(chan->alertpipe[0], &blah, sizeof(blah));
	}
#ifdef ZAPTEL_OPTIMIZATIONS
	if ((chan->timingfd > -1) && (chan->fdno == AST_MAX_FDS - 2) && ast_test_flag(chan, AST_FLAG_EXCEPTION)) {
		ast_clear_flag(chan, AST_FLAG_EXCEPTION);
		blah = -1;
		/* IF we can't get event, assume it's an expired as-per the old interface */
		res = ioctl(chan->timingfd, ZT_GETEVENT, &blah);
		if (res) 
			blah = ZT_EVENT_TIMER_EXPIRED;

		if (blah == ZT_EVENT_TIMER_PING) {
#if 0
			ast_log(LOG_NOTICE, "Oooh, there's a PING!\n");
#endif			
			if (!chan->readq || !chan->readq->next) {
				/* Acknowledge PONG unless we need it again */
#if 0
				ast_log(LOG_NOTICE, "Sending a PONG!\n");
#endif				
				if (ioctl(chan->timingfd, ZT_TIMERPONG, &blah)) {
					ast_log(LOG_WARNING, "Failed to pong timer on '%s': %s\n", chan->name, strerror(errno));
				}
			}
		} else if (blah == ZT_EVENT_TIMER_EXPIRED) {
			ioctl(chan->timingfd, ZT_TIMERACK, &blah);
			func = chan->timingfunc;
			data = chan->timingdata;
			ast_mutex_unlock(&chan->lock);
			if (func) {
#if 0
				ast_log(LOG_DEBUG, "Calling private function\n");
#endif			
				func(data);
			} else {
				blah = 0;
				ast_mutex_lock(&chan->lock);
				ioctl(chan->timingfd, ZT_TIMERCONFIG, &blah);
				chan->timingdata = NULL;
				ast_mutex_unlock(&chan->lock);
			}
			f =  &null_frame;
			return f;
		} else
			ast_log(LOG_NOTICE, "No/unknown event '%d' on timer for '%s'?\n", blah, chan->name);
	}
#endif
	/* Check for pending read queue */
	if (chan->readq) {
		f = chan->readq;
		chan->readq = f->next;
		/* Interpret hangup and return NULL */
		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
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
				f = &null_frame;
			}
			/* Clear the exception flag */
			ast_clear_flag(chan, AST_FLAG_EXCEPTION);
		} else {
			if (chan->tech->read)
				f = chan->tech->read(chan);
			else
				ast_log(LOG_WARNING, "No read routine on channel %s\n", chan->name);
		}
	}


	if (f && (f->frametype == AST_FRAME_VOICE)) {
		if (!(f->subclass & chan->nativeformats)) {
			/* This frame can't be from the current native formats -- drop it on the
			   floor */
			ast_log(LOG_NOTICE, "Dropping incompatible voice frame on %s of format %s since our native format has changed to %s\n", chan->name, ast_getformatname(f->subclass), ast_getformatname(chan->nativeformats));
			ast_frfree(f);
			f = &null_frame;
		} else {
			if (chan->spies)
				queue_frame_to_spies(chan, f, SPY_READ);

			if (chan->monitor && chan->monitor->read_stream ) {
#ifndef MONITOR_CONSTANT_DELAY
				int jump = chan->outsmpl - chan->insmpl - 2 * f->samples;
				if (jump >= 0) {
					if (ast_seekstream(chan->monitor->read_stream, jump + f->samples, SEEK_FORCECUR) == -1)
						ast_log(LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
					chan->insmpl += jump + 2 * f->samples;
				} else
					chan->insmpl+= f->samples;
#else
				int jump = chan->outsmpl - chan->insmpl;
				if (jump - MONITOR_DELAY >= 0) {
					if (ast_seekstream(chan->monitor->read_stream, jump - f->samples, SEEK_FORCECUR) == -1)
						ast_log(LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
					chan->insmpl += jump;
				} else
					chan->insmpl += f->samples;
#endif
				if (ast_writestream(chan->monitor->read_stream, f) < 0)
					ast_log(LOG_WARNING, "Failed to write data to channel monitor read stream\n");
			}
			if (chan->readtrans) {
				f = ast_translate(chan->readtrans, f, 1);
				if (!f)
					f = &null_frame;
			}
		}
	}

	/* Make sure we always return NULL in the future */
	if (!f) {
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
		if (chan->generator)
			ast_deactivate_generator(chan);
		/* End the CDR if appropriate */
		if (chan->cdr)
			ast_cdr_end(chan->cdr);
	} else if (ast_test_flag(chan, AST_FLAG_DEFER_DTMF) && f->frametype == AST_FRAME_DTMF) {
		if (strlen(chan->dtmfq) < sizeof(chan->dtmfq) - 2)
			chan->dtmfq[strlen(chan->dtmfq)] = f->subclass;
		else
			ast_log(LOG_WARNING, "Dropping deferred DTMF digits on %s\n", chan->name);
		f = &null_frame;
	} else if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_ANSWER)) {
		if (prestate == AST_STATE_UP) {
			ast_log(LOG_DEBUG, "Dropping duplicate answer!\n");
			f = &null_frame;
		}
		/* Answer the CDR */
		ast_setstate(chan, AST_STATE_UP);
		ast_cdr_answer(chan->cdr);
	} 

	/* Run any generator sitting on the line */
	if (f && (f->frametype == AST_FRAME_VOICE) && chan->generatordata) {
		/* Mask generator data temporarily and apply.  If there is a timing function, it
		   will be calling the generator instead */
		void *tmp;
		int res;
		int (*generate)(struct ast_channel *chan, void *tmp, int datalen, int samples);

		if (chan->timingfunc) {
			ast_log(LOG_DEBUG, "Generator got voice, switching to phase locked mode\n");
			ast_settimeout(chan, 0, NULL, NULL);
		}
		tmp = chan->generatordata;
		chan->generatordata = NULL;
		generate = chan->generator->generate;
		res = generate(chan, tmp, f->datalen, f->samples);
		chan->generatordata = tmp;
		if (res) {
			ast_log(LOG_DEBUG, "Auto-deactivating generator\n");
			ast_deactivate_generator(chan);
		}
	} else if (f && (f->frametype == AST_FRAME_CNG)) {
		if (chan->generator && !chan->timingfunc && (chan->timingfd > -1)) {
			ast_log(LOG_DEBUG, "Generator got CNG, switching to zap timed mode\n");
			ast_settimeout(chan, 160, generator_force, chan);
		}
	}
	/* High bit prints debugging */
	if (chan->fin & 0x80000000)
		ast_frame_dump(chan->name, f, "<<");
	if ((chan->fin & 0x7fffffff) == 0x7fffffff)
		chan->fin &= 0x80000000;
	else
		chan->fin++;
	ast_mutex_unlock(&chan->lock);
	return f;
}

int ast_indicate(struct ast_channel *chan, int condition)
{
	int res = -1;

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) 
		return -1;
	ast_mutex_lock(&chan->lock);
	if (chan->tech->indicate)
		res = chan->tech->indicate(chan, condition);
	ast_mutex_unlock(&chan->lock);
	if (!chan->tech->indicate || res) {
		/*
		 * Device does not support (that) indication, lets fake
		 * it by doing our own tone generation. (PM2002)
		 */
		if (condition >= 0) {
			const struct tone_zone_sound *ts = NULL;
			switch (condition) {
			case AST_CONTROL_RINGING:
				ts = ast_get_indication_tone(chan->zone, "ring");
				break;
			case AST_CONTROL_BUSY:
				ts = ast_get_indication_tone(chan->zone, "busy");
				break;
			case AST_CONTROL_CONGESTION:
				ts = ast_get_indication_tone(chan->zone, "congestion");
				break;
			}
			if (ts && ts->data[0]) {
				ast_log(LOG_DEBUG, "Driver for channel '%s' does not support indication %d, emulating it\n", chan->name, condition);
				ast_playtones_start(chan,0,ts->data, 1);
				res = 0;
			} else if (condition == AST_CONTROL_PROGRESS) {
				/* ast_playtones_stop(chan); */
			} else if (condition == AST_CONTROL_PROCEEDING) {
				/* Do nothing, really */
			} else if (condition == AST_CONTROL_HOLD) {
				/* Do nothing.... */
			} else if (condition == AST_CONTROL_UNHOLD) {
				/* Do nothing.... */
			} else if (condition == AST_CONTROL_VIDUPDATE) {
				/* Do nothing.... */
			} else {
				/* not handled */
				ast_log(LOG_WARNING, "Unable to handle indication %d for '%s'\n", condition, chan->name);
				res = -1;
			}
		}
		else ast_playtones_stop(chan);
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
	free(buf);
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
		if (f->frametype == AST_FRAME_CONTROL && f->subclass == AST_CONTROL_HANGUP)
			done = 1;	/* force a break */
		else if (f->frametype == AST_FRAME_TEXT) {		/* what we want */
			buf = strndup((char *) f->data, f->datalen);	/* dup and break */
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

static int do_senddigit(struct ast_channel *chan, char digit)
{
	int res = -1;

	if (chan->tech->send_digit)
		res = chan->tech->send_digit(chan, digit);
	if (!chan->tech->send_digit || res) {
		/*
		 * Device does not support DTMF tones, lets fake
		 * it by doing our own generation. (PM2002)
		 */
		static const char* dtmf_tones[] = {
			"!941+1336/100,!0/100",	/* 0 */
			"!697+1209/100,!0/100",	/* 1 */
			"!697+1336/100,!0/100",	/* 2 */
			"!697+1477/100,!0/100",	/* 3 */
			"!770+1209/100,!0/100",	/* 4 */
			"!770+1336/100,!0/100",	/* 5 */
			"!770+1477/100,!0/100",	/* 6 */
			"!852+1209/100,!0/100",	/* 7 */
			"!852+1336/100,!0/100",	/* 8 */
			"!852+1477/100,!0/100",	/* 9 */
			"!697+1633/100,!0/100",	/* A */
			"!770+1633/100,!0/100",	/* B */
			"!852+1633/100,!0/100",	/* C */
			"!941+1633/100,!0/100",	/* D */
			"!941+1209/100,!0/100",	/* * */
			"!941+1477/100,!0/100" };	/* # */
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
			ast_log(LOG_DEBUG, "Unable to generate DTMF tone '%c' for '%s'\n", digit, chan->name);
		}
	}
	return 0;
}

int ast_senddigit(struct ast_channel *chan, char digit)
{
	return do_senddigit(chan, digit);
}

int ast_prod(struct ast_channel *chan)
{
	struct ast_frame a = { AST_FRAME_VOICE };
	char nothing[128];

	/* Send an empty audio frame to get things moving */
	if (chan->_state != AST_STATE_UP) {
		ast_log(LOG_DEBUG, "Prodding channel '%s'\n", chan->name);
		a.subclass = chan->rawwriteformat;
		a.data = nothing + AST_FRIENDLY_OFFSET;
		a.src = "ast_prod";
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

int ast_write(struct ast_channel *chan, struct ast_frame *fr)
{
	int res = -1;
	struct ast_frame *f = NULL;
	/* Stop if we're a zombie or need a soft hangup */
	ast_mutex_lock(&chan->lock);
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan))  {
		ast_mutex_unlock(&chan->lock);
		return -1;
	}
	/* Handle any pending masquerades */
	if (chan->masq) {
		if (ast_do_masquerade(chan)) {
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
			ast_mutex_unlock(&chan->lock);
			return -1;
		}
	}
	if (chan->masqr) {
		ast_mutex_unlock(&chan->lock);
		return 0;
	}
	if (chan->generatordata) {
		if (ast_test_flag(chan, AST_FLAG_WRITE_INT))
			ast_deactivate_generator(chan);
		else {
			ast_mutex_unlock(&chan->lock);
			return 0;
		}
	}
	/* High bit prints debugging */
	if (chan->fout & 0x80000000)
		ast_frame_dump(chan->name, fr, ">>");
	CHECK_BLOCKING(chan);
	switch(fr->frametype) {
	case AST_FRAME_CONTROL:
		/* XXX Interpret control frames XXX */
		ast_log(LOG_WARNING, "Don't know how to handle control frames yet\n");
		break;
	case AST_FRAME_DTMF:
		ast_clear_flag(chan, AST_FLAG_BLOCKING);
		ast_mutex_unlock(&chan->lock);
		res = do_senddigit(chan,fr->subclass);
		ast_mutex_lock(&chan->lock);
		CHECK_BLOCKING(chan);
		break;
	case AST_FRAME_TEXT:
		if (chan->tech->send_text)
			res = chan->tech->send_text(chan, (char *) fr->data);
		else
			res = 0;
		break;
	case AST_FRAME_HTML:
		if (chan->tech->send_html)
			res = chan->tech->send_html(chan, fr->subclass, (char *) fr->data, fr->datalen);
		else
			res = 0;
		break;
	case AST_FRAME_VIDEO:
		/* XXX Handle translation of video codecs one day XXX */
		if (chan->tech->write_video)
			res = chan->tech->write_video(chan, fr);
		else
			res = 0;
		break;
	default:
		if (chan->tech->write) {
			f = (chan->writetrans) ? ast_translate(chan->writetrans, fr, 0) : fr;
			if (f) {
				if (f->frametype == AST_FRAME_VOICE && chan->spies)
					queue_frame_to_spies(chan, f, SPY_WRITE);

				if( chan->monitor && chan->monitor->write_stream &&
						f && ( f->frametype == AST_FRAME_VOICE ) ) {
#ifndef MONITOR_CONSTANT_DELAY
					int jump = chan->insmpl - chan->outsmpl - 2 * f->samples;
					if (jump >= 0) {
						if (ast_seekstream(chan->monitor->write_stream, jump + f->samples, SEEK_FORCECUR) == -1)
							ast_log(LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
						chan->outsmpl += jump + 2 * f->samples;
					} else
						chan->outsmpl += f->samples;
#else
					int jump = chan->insmpl - chan->outsmpl;
					if (jump - MONITOR_DELAY >= 0) {
						if (ast_seekstream(chan->monitor->write_stream, jump - f->samples, SEEK_FORCECUR) == -1)
							ast_log(LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
						chan->outsmpl += jump;
					} else
						chan->outsmpl += f->samples;
#endif
					if (ast_writestream(chan->monitor->write_stream, f) < 0)
						ast_log(LOG_WARNING, "Failed to write data to channel monitor write stream\n");
				}

				res = chan->tech->write(chan, f);
			} else
				res = 0;
		}
	}

	/* It's possible this is a translated frame */
	if (f && f->frametype == AST_FRAME_DTMF) {
		ast_log(LOG_DTMF, "%s : %c\n", chan->name, f->subclass);
	} else if (fr->frametype == AST_FRAME_DTMF) {
		ast_log(LOG_DTMF, "%s : %c\n", chan->name, fr->subclass);
	}

	if (f && (f != fr))
		ast_frfree(f);
	ast_clear_flag(chan, AST_FLAG_BLOCKING);
	/* Consider a write failure to force a soft hangup */
	if (res < 0)
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
	else {
		if ((chan->fout & 0x7fffffff) == 0x7fffffff)
			chan->fout &= 0x80000000;
		else
			chan->fout++;
	}
	ast_mutex_unlock(&chan->lock);
	return res;
}

static int set_format(struct ast_channel *chan, int fmt, int *rawformat, int *format,
		      struct ast_trans_pvt **trans, const int direction)
{
	int native;
	int res;
	
	native = chan->nativeformats;
	/* Find a translation path from the native format to one of the desired formats */
	if (!direction)
		/* reading */
		res = ast_translator_best_choice(&fmt, &native);
	else
		/* writing */
		res = ast_translator_best_choice(&native, &fmt);

	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to find a codec translation path from %s to %s\n",
			ast_getformatname(native), ast_getformatname(fmt));
		return -1;
	}
	
	/* Now we have a good choice for both. */
	ast_mutex_lock(&chan->lock);
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
	ast_mutex_unlock(&chan->lock);
	if (option_debug)
		ast_log(LOG_DEBUG, "Set channel %s to %s format %s\n", chan->name,
			direction ? "write" : "read", ast_getformatname(fmt));
	return 0;
}

int ast_set_read_format(struct ast_channel *chan, int fmt)
{
	return set_format(chan, fmt, &chan->rawreadformat, &chan->readformat,
			  &chan->readtrans, 0);
}

int ast_set_write_format(struct ast_channel *chan, int fmt)
{
	return set_format(chan, fmt, &chan->rawwriteformat, &chan->writeformat,
			  &chan->writetrans, 1);
}

struct ast_channel *__ast_request_and_dial(const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name, struct outgoing_helper *oh)
{
	int state = 0;
	int cause = 0;
	struct ast_channel *chan;
	struct ast_frame *f;
	int res = 0;
	
	chan = ast_request(type, format, data, &cause);
	if (chan) {
		if (oh) {
			if (oh->vars)	
				ast_set_variables(chan, oh->vars);
			if (oh->cid_num && *oh->cid_num && oh->cid_name && *oh->cid_name)
				ast_set_callerid(chan, oh->cid_num, oh->cid_name, oh->cid_num);
			if (oh->parent_channel)
				ast_channel_inherit_variables(oh->parent_channel, chan);
		}
		ast_set_callerid(chan, cid_num, cid_name, cid_num);

		if (!ast_call(chan, data, 0)) {
			while(timeout && (chan->_state != AST_STATE_UP)) {
				res = ast_waitfor(chan, timeout);
				if (res < 0) {
					/* Something not cool, or timed out */
					break;
				}
				/* If done, break out */
				if (!res)
					break;
				if (timeout > -1)
					timeout = res;
				f = ast_read(chan);
				if (!f) {
					state = AST_CONTROL_HANGUP;
					res = 0;
					break;
				}
				if (f->frametype == AST_FRAME_CONTROL) {
					if (f->subclass == AST_CONTROL_RINGING)
						state = AST_CONTROL_RINGING;
					else if ((f->subclass == AST_CONTROL_BUSY) || (f->subclass == AST_CONTROL_CONGESTION)) {
						state = f->subclass;
						ast_frfree(f);
						break;
					} else if (f->subclass == AST_CONTROL_ANSWER) {
						state = f->subclass;
						ast_frfree(f);
						break;
					} else if (f->subclass == AST_CONTROL_PROGRESS) {
						/* Ignore */
					} else if (f->subclass == -1) {
						/* Ignore -- just stopping indications */
					} else {
						ast_log(LOG_NOTICE, "Don't know what to do with control frame %d\n", f->subclass);
					}
				}
				ast_frfree(f);
			}
		} else
			ast_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, (char *)data);
	} else {
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		switch(cause) {
		case AST_CAUSE_BUSY:
			state = AST_CONTROL_BUSY;
			break;
		case AST_CAUSE_CONGESTION:
			state = AST_CONTROL_CONGESTION;
			break;
		}
	}
	if (chan) {
		/* Final fixups */
		if (oh) {
			if (oh->context && *oh->context)
				ast_copy_string(chan->context, oh->context, sizeof(chan->context));
			if (oh->exten && *oh->exten)
				ast_copy_string(chan->exten, oh->exten, sizeof(chan->exten));
			if (oh->priority)	
				chan->priority = oh->priority;
		}
		if (chan->_state == AST_STATE_UP) 
			state = AST_CONTROL_ANSWER;
	}
	if (outstate)
		*outstate = state;
	if (chan && res <= 0) {
		if (!chan->cdr) {
			chan->cdr = ast_cdr_alloc();
			if (chan->cdr)
				ast_cdr_init(chan->cdr, chan);
		}
		if (chan->cdr) {
			char tmp[256];
			snprintf(tmp, 256, "%s/%s", type, (char *)data);
			ast_cdr_setapp(chan->cdr,"Dial",tmp);
			ast_cdr_update(chan);
			ast_cdr_start(chan->cdr);
			ast_cdr_end(chan->cdr);
			/* If the cause wasn't handled properly */
			if (ast_cdr_disposition(chan->cdr,chan->hangupcause))
				ast_cdr_failed(chan->cdr);
		} else 
			ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
		ast_hangup(chan);
		chan = NULL;
	}
	return chan;
}

struct ast_channel *ast_request_and_dial(const char *type, int format, void *data, int timeout, int *outstate, const char *cidnum, const char *cidname)
{
	return __ast_request_and_dial(type, format, data, timeout, outstate, cidnum, cidname, NULL);
}

struct ast_channel *ast_request(const char *type, int format, void *data, int *cause)
{
	struct chanlist *chan;
	struct ast_channel *c;
	int capabilities;
	int fmt;
	int res;
	int foo;

	if (!cause)
		cause = &foo;
	*cause = AST_CAUSE_NOTDEFINED;

	if (ast_mutex_lock(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return NULL;
	}

	for (chan = backends; chan; chan = chan->next) {
		if (strcasecmp(type, chan->tech->type))
			continue;

		capabilities = chan->tech->capabilities;
		fmt = format;
		res = ast_translator_best_choice(&fmt, &capabilities);
		if (res < 0) {
			ast_log(LOG_WARNING, "No translator path exists for channel type %s (native %d) to %d\n", type, chan->tech->capabilities, format);
			ast_mutex_unlock(&chlock);
			return NULL;
		}
		ast_mutex_unlock(&chlock);
		if (!chan->tech->requester)
			return NULL;
		
		if (!(c = chan->tech->requester(type, capabilities, data, cause)))
			return NULL;

		if (c->_state == AST_STATE_DOWN) {
			manager_event(EVENT_FLAG_CALL, "Newchannel",
				      "Channel: %s\r\n"
				      "State: %s\r\n"
				      "CallerID: %s\r\n"
				      "CallerIDName: %s\r\n"
				      "Uniqueid: %s\r\n",
				      c->name, ast_state2str(c->_state),
				      c->cid.cid_num ? c->cid.cid_num : "<unknown>",
				      c->cid.cid_name ? c->cid.cid_name : "<unknown>",
				      c->uniqueid);
		}
		return c;
	}

	ast_log(LOG_WARNING, "No channel type registered for '%s'\n", type);
	*cause = AST_CAUSE_NOSUCHDRIVER;
	ast_mutex_unlock(&chlock);

	return NULL;
}

int ast_call(struct ast_channel *chan, char *addr, int timeout) 
{
	/* Place an outgoing call, but don't wait any longer than timeout ms before returning. 
	   If the remote end does not answer within the timeout, then do NOT hang up, but 
	   return anyway.  */
	int res = -1;
	/* Stop if we're a zombie or need a soft hangup */
	ast_mutex_lock(&chan->lock);
	if (!ast_test_flag(chan, AST_FLAG_ZOMBIE) && !ast_check_hangup(chan)) 
		if (chan->tech->call)
			res = chan->tech->call(chan, addr, timeout);
	ast_mutex_unlock(&chan->lock);
	return res;
}

/*--- ast_transfer: Transfer a call to dest, if the channel supports transfer */
/*	called by app_transfer or the manager interface */
int ast_transfer(struct ast_channel *chan, char *dest) 
{
	int res = -1;

	/* Stop if we're a zombie or need a soft hangup */
	ast_mutex_lock(&chan->lock);
	if (!ast_test_flag(chan, AST_FLAG_ZOMBIE) && !ast_check_hangup(chan)) {
		if (chan->tech->transfer) {
			res = chan->tech->transfer(chan, dest);
			if (!res)
				res = 1;
		} else
			res = 0;
	}
	ast_mutex_unlock(&chan->lock);
	return res;
}

int ast_readstring(struct ast_channel *c, char *s, int len, int timeout, int ftimeout, char *enders)
{
	int pos=0;
	int to = ftimeout;
	int d;

	/* XXX Merge with full version? XXX */
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c, AST_FLAG_ZOMBIE) || ast_check_hangup(c)) 
		return -1;
	if (!len)
		return -1;
	do {
		if (c->stream) {
			d = ast_waitstream(c, AST_DIGIT_ANY);
			ast_stopstream(c);
			usleep(1000);
			if (!d)
				d = ast_waitfordigit(c, to);
		} else {
			d = ast_waitfordigit(c, to);
		}
		if (d < 0)
			return -1;
		if (d == 0) {
			s[pos]='\0';
			return 1;
		}
		if (!strchr(enders, d))
			s[pos++] = d;
		if (strchr(enders, d) || (pos >= len)) {
			s[pos]='\0';
			return 0;
		}
		to = timeout;
	} while(1);
	/* Never reached */
	return 0;
}

int ast_readstring_full(struct ast_channel *c, char *s, int len, int timeout, int ftimeout, char *enders, int audiofd, int ctrlfd)
{
	int pos=0;
	int to = ftimeout;
	int d;

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c, AST_FLAG_ZOMBIE) || ast_check_hangup(c)) 
		return -1;
	if (!len)
		return -1;
	do {
		if (c->stream) {
			d = ast_waitstream_full(c, AST_DIGIT_ANY, audiofd, ctrlfd);
			ast_stopstream(c);
			usleep(1000);
			if (!d)
				d = ast_waitfordigit_full(c, to, audiofd, ctrlfd);
		} else {
			d = ast_waitfordigit_full(c, to, audiofd, ctrlfd);
		}
		if (d < 0)
			return -1;
		if (d == 0) {
			s[pos]='\0';
			return 1;
		}
		if (d == 1) {
			s[pos]='\0';
			return 2;
		}
		if (!strchr(enders, d))
			s[pos++] = d;
		if (strchr(enders, d) || (pos >= len)) {
			s[pos]='\0';
			return 0;
		}
		to = timeout;
	} while(1);
	/* Never reached */
	return 0;
}

int ast_channel_supports_html(struct ast_channel *chan)
{
	if (chan->tech->send_html)
		return 1;
	return 0;
}

int ast_channel_sendhtml(struct ast_channel *chan, int subclass, const char *data, int datalen)
{
	if (chan->tech->send_html)
		return chan->tech->send_html(chan, subclass, data, datalen);
	return -1;
}

int ast_channel_sendurl(struct ast_channel *chan, const char *url)
{
	if (chan->tech->send_html)
		return chan->tech->send_html(chan, AST_HTML_URL, url, strlen(url) + 1);
	return -1;
}

int ast_channel_make_compatible(struct ast_channel *chan, struct ast_channel *peer)
{
	int src;
	int dst;

	/* Set up translation from the chan to the peer */
	src = chan->nativeformats;
	dst = peer->nativeformats;
	if (ast_translator_best_choice(&dst, &src) < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", chan->name, src, peer->name, dst);
		return -1;
	}

	/* if the best path is not 'pass through', then
	   transcoding is needed; if desired, force transcode path
	   to use SLINEAR between channels */
	if ((src != dst) && option_transcode_slin)
		dst = AST_FORMAT_SLINEAR;
	if (ast_set_read_format(chan, dst) < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %d\n", chan->name, dst);
		return -1;
	}
	if (ast_set_write_format(peer, dst) < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %d\n", peer->name, dst);
		return -1;
	}

	/* Set up translation from the peer to the chan */
	src = peer->nativeformats;
	dst = chan->nativeformats;
	if (ast_translator_best_choice(&dst, &src) < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", peer->name, src, chan->name, dst);
		return -1;
	}
	/* if the best path is not 'pass through', then
	   transcoding is needed; if desired, force transcode path
	   to use SLINEAR between channels */
	if ((src != dst) && option_transcode_slin)
		dst = AST_FORMAT_SLINEAR;
	if (ast_set_read_format(peer, dst) < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %d\n", peer->name, dst);
		return -1;
	}
	if (ast_set_write_format(chan, dst) < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %d\n", chan->name, dst);
		return -1;
	}
	return 0;
}

int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clone)
{
	struct ast_frame null = { AST_FRAME_NULL, };
	int res = -1;

	if (original == clone) {
		ast_log(LOG_WARNING, "Can't masquerade channel '%s' into itself!\n", original->name);
		return -1;
	}
	ast_mutex_lock(&original->lock);
	while(ast_mutex_trylock(&clone->lock)) {
		ast_mutex_unlock(&original->lock);
		usleep(1);
		ast_mutex_lock(&original->lock);
	}
	ast_log(LOG_DEBUG, "Planning to masquerade channel %s into the structure of %s\n",
		clone->name, original->name);
	if (original->masq) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n", 
			original->masq->name, original->name);
	} else if (clone->masqr) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n", 
			clone->name, clone->masqr->name);
	} else {
		original->masq = clone;
		clone->masqr = original;
		ast_queue_frame(original, &null);
		ast_queue_frame(clone, &null);
		ast_log(LOG_DEBUG, "Done planning to masquerade channel %s into the structure of %s\n", clone->name, original->name);
		res = 0;
	}
	ast_mutex_unlock(&clone->lock);
	ast_mutex_unlock(&original->lock);
	return res;
}

void ast_change_name(struct ast_channel *chan, char *newname)
{
	char tmp[256];
	ast_copy_string(tmp, chan->name, sizeof(tmp));
	ast_copy_string(chan->name, newname, sizeof(chan->name));
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", tmp, chan->name, chan->uniqueid);
}

void ast_channel_inherit_variables(const struct ast_channel *parent, struct ast_channel *child)
{
	struct ast_var_t *current, *newvar;
	char *varname;

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
				if (option_debug)
					ast_log(LOG_DEBUG, "Copying soft-transferable variable %s.\n", ast_var_name(newvar));
			}
			break;
		case 2:
			newvar = ast_var_assign(ast_var_full_name(current), ast_var_value(current));
			if (newvar) {
				AST_LIST_INSERT_TAIL(&child->varshead, newvar, entries);
				if (option_debug)
					ast_log(LOG_DEBUG, "Copying hard-transferable variable %s.\n", ast_var_name(newvar));
			}
			break;
		default:
			if (option_debug)
				ast_log(LOG_DEBUG, "Not copying variable %s.\n", ast_var_name(current));
			break;
		}
	}
}

/* Clone channel variables from 'clone' channel into 'original' channel
   All variables except those related to app_groupcount are cloned
   Variables are actually _removed_ from 'clone' channel, presumably
   because it will subsequently be destroyed.
   Assumes locks will be in place on both channels when called.
*/
   
static void clone_variables(struct ast_channel *original, struct ast_channel *clone)
{
	struct ast_var_t *varptr;

	/* we need to remove all app_groupcount related variables from the original
	   channel before merging in the clone's variables; any groups assigned to the
	   original channel should be released, only those assigned to the clone
	   should remain
	*/

	AST_LIST_TRAVERSE_SAFE_BEGIN(&original->varshead, varptr, entries) {
		if (!strncmp(ast_var_name(varptr), GROUP_CATEGORY_PREFIX, strlen(GROUP_CATEGORY_PREFIX))) {
			AST_LIST_REMOVE(&original->varshead, varptr, entries);
			ast_var_delete(varptr);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* Append variables from clone channel into original channel */
	/* XXX Is this always correct?  We have to in order to keep MACROS working XXX */
	if (AST_LIST_FIRST(&clone->varshead))
		AST_LIST_INSERT_TAIL(&original->varshead, AST_LIST_FIRST(&clone->varshead), entries);
}

/*--- ast_do_masquerade: Masquerade a channel */
/* Assumes channel will be locked when called */
int ast_do_masquerade(struct ast_channel *original)
{
	int x,i;
	int res=0;
	int origstate;
	struct ast_frame *cur, *prev;
	const struct ast_channel_tech *t;
	void *t_pvt;
	struct ast_callerid tmpcid;
	struct ast_channel *clone = original->masq;
	int rformat = original->readformat;
	int wformat = original->writeformat;
	char newn[100];
	char orig[100];
	char masqn[100];
	char zombn[100];

	if (option_debug > 3)
		ast_log(LOG_DEBUG, "Actually Masquerading %s(%d) into the structure of %s(%d)\n",
			clone->name, clone->_state, original->name, original->_state);

	/* XXX This is a seriously wacked out operation.  We're essentially putting the guts of
	   the clone channel into the original channel.  Start by killing off the original
	   channel's backend.   I'm not sure we're going to keep this function, because 
	   while the features are nice, the cost is very high in terms of pure nastiness. XXX */

	/* We need the clone's lock, too */
	ast_mutex_lock(&clone->lock);

	ast_log(LOG_DEBUG, "Got clone lock for masquerade on '%s' at %p\n", clone->name, &clone->lock);

	/* Having remembered the original read/write formats, we turn off any translation on either
	   one */
	free_translation(clone);
	free_translation(original);


	/* Unlink the masquerade */
	original->masq = NULL;
	clone->masqr = NULL;
	
	/* Save the original name */
	ast_copy_string(orig, original->name, sizeof(orig));
	/* Save the new name */
	ast_copy_string(newn, clone->name, sizeof(newn));
	/* Create the masq name */
	snprintf(masqn, sizeof(masqn), "%s<MASQ>", newn);
		
	/* Copy the name from the clone channel */
	ast_copy_string(original->name, newn, sizeof(original->name));

	/* Mangle the name of the clone channel */
	ast_copy_string(clone->name, masqn, sizeof(clone->name));
	
	/* Notify any managers of the change, first the masq then the other */
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", newn, masqn, clone->uniqueid);
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", orig, newn, original->uniqueid);

	/* Swap the technlogies */	
	t = original->tech;
	original->tech = clone->tech;
	clone->tech = t;

	t_pvt = original->tech_pvt;
	original->tech_pvt = clone->tech_pvt;
	clone->tech_pvt = t_pvt;

	/* Swap the readq's */
	cur = original->readq;
	original->readq = clone->readq;
	clone->readq = cur;

	/* Swap the alertpipes */
	for (i = 0; i < 2; i++) {
		x = original->alertpipe[i];
		original->alertpipe[i] = clone->alertpipe[i];
		clone->alertpipe[i] = x;
	}

	/* Swap the raw formats */
	x = original->rawreadformat;
	original->rawreadformat = clone->rawreadformat;
	clone->rawreadformat = x;
	x = original->rawwriteformat;
	original->rawwriteformat = clone->rawwriteformat;
	clone->rawwriteformat = x;

	/* Save any pending frames on both sides.  Start by counting
	 * how many we're going to need... */
	prev = NULL;
	cur = clone->readq;
	x = 0;
	while(cur) {
		x++;
		prev = cur;
		cur = cur->next;
	}
	/* If we had any, prepend them to the ones already in the queue, and 
	 * load up the alertpipe */
	if (prev) {
		prev->next = original->readq;
		original->readq = clone->readq;
		clone->readq = NULL;
		if (original->alertpipe[1] > -1) {
			for (i = 0; i < x; i++)
				write(original->alertpipe[1], &x, sizeof(x));
		}
	}
	clone->_softhangup = AST_SOFTHANGUP_DEV;


	/* And of course, so does our current state.  Note we need not
	   call ast_setstate since the event manager doesn't really consider
	   these separate.  We do this early so that the clone has the proper
	   state of the original channel. */
	origstate = original->_state;
	original->_state = clone->_state;
	clone->_state = origstate;

	if (clone->tech->fixup){
		res = clone->tech->fixup(original, clone);
		if (res) 
			ast_log(LOG_WARNING, "Fixup failed on channel %s, strange things may happen.\n", clone->name);
	}

	/* Start by disconnecting the original's physical side */
	if (clone->tech->hangup)
		res = clone->tech->hangup(clone);
	if (res) {
		ast_log(LOG_WARNING, "Hangup failed!  Strange things may happen!\n");
		ast_mutex_unlock(&clone->lock);
		return -1;
	}
	
	snprintf(zombn, sizeof(zombn), "%s<ZOMBIE>", orig);
	/* Mangle the name of the clone channel */
	ast_copy_string(clone->name, zombn, sizeof(clone->name));
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", masqn, zombn, clone->uniqueid);

	/* Update the type. */
	original->type = clone->type;
	t_pvt = original->monitor;
	original->monitor = clone->monitor;
	clone->monitor = t_pvt;
	
	/* Keep the same language.  */
	ast_copy_string(original->language, clone->language, sizeof(original->language));
	/* Copy the FD's */
	for (x = 0; x < AST_MAX_FDS; x++) {
		original->fds[x] = clone->fds[x];
	}
	clone_variables(original, clone);
	AST_LIST_HEAD_INIT_NOLOCK(&clone->varshead);
	/* Presense of ADSI capable CPE follows clone */
	original->adsicpe = clone->adsicpe;
	/* Bridge remains the same */
	/* CDR fields remain the same */
	/* XXX What about blocking, softhangup, blocker, and lock and blockproc? XXX */
	/* Application and data remain the same */
	/* Clone exception  becomes real one, as with fdno */
	ast_copy_flags(original, clone, AST_FLAG_EXCEPTION);
	original->fdno = clone->fdno;
	/* Schedule context remains the same */
	/* Stream stuff stays the same */
	/* Keep the original state.  The fixup code will need to work with it most likely */

	/* Just swap the whole structures, nevermind the allocations, they'll work themselves
	   out. */
	tmpcid = original->cid;
	original->cid = clone->cid;
	clone->cid = tmpcid;
	
	/* Restore original timing file descriptor */
	original->fds[AST_MAX_FDS - 2] = original->timingfd;
	
	/* Our native formats are different now */
	original->nativeformats = clone->nativeformats;
	
	/* Context, extension, priority, app data, jump table,  remain the same */
	/* pvt switches.  pbx stays the same, as does next */
	
	/* Set the write format */
	ast_set_write_format(original, wformat);

	/* Set the read format */
	ast_set_read_format(original, rformat);

	/* Copy the music class */
	ast_copy_string(original->musicclass, clone->musicclass, sizeof(original->musicclass));

	ast_log(LOG_DEBUG, "Putting channel %s in %d/%d formats\n", original->name, wformat, rformat);

	/* Okay.  Last thing is to let the channel driver know about all this mess, so he
	   can fix up everything as best as possible */
	if (original->tech->fixup) {
		res = original->tech->fixup(clone, original);
		if (res) {
			ast_log(LOG_WARNING, "Channel for type '%s' could not fixup channel %s\n",
				original->type, original->name);
			ast_mutex_unlock(&clone->lock);
			return -1;
		}
	} else
		ast_log(LOG_WARNING, "Channel type '%s' does not have a fixup routine (for %s)!  Bad things may happen.\n",
			original->type, original->name);
	
	/* Now, at this point, the "clone" channel is totally F'd up.  We mark it as
	   a zombie so nothing tries to touch it.  If it's already been marked as a
	   zombie, then free it now (since it already is considered invalid). */
	if (ast_test_flag(clone, AST_FLAG_ZOMBIE)) {
		ast_log(LOG_DEBUG, "Destroying channel clone '%s'\n", clone->name);
		ast_mutex_unlock(&clone->lock);
		ast_channel_free(clone);
		manager_event(EVENT_FLAG_CALL, "Hangup", 
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Cause: %d\r\n"
			"Cause-txt: %s\r\n",
			clone->name, 
			clone->uniqueid, 
			clone->hangupcause,
			ast_cause2str(clone->hangupcause)
			);
	} else {
		struct ast_frame null_frame = { AST_FRAME_NULL, };
		ast_log(LOG_DEBUG, "Released clone lock on '%s'\n", clone->name);
		ast_set_flag(clone, AST_FLAG_ZOMBIE);
		ast_queue_frame(clone, &null_frame);
		ast_mutex_unlock(&clone->lock);
	}
	
	/* Signal any blocker */
	if (ast_test_flag(original, AST_FLAG_BLOCKING))
		pthread_kill(original->blocker, SIGURG);
	ast_log(LOG_DEBUG, "Done Masquerading %s (%d)\n", original->name, original->_state);
	return 0;
}

void ast_set_callerid(struct ast_channel *chan, const char *callerid, const char *calleridname, const char *ani)
{
	if (callerid) {
		if (chan->cid.cid_num)
			free(chan->cid.cid_num);
		if (ast_strlen_zero(callerid))
			chan->cid.cid_num = NULL;
		else
			chan->cid.cid_num = strdup(callerid);
	}
	if (calleridname) {
		if (chan->cid.cid_name)
			free(chan->cid.cid_name);
		if (ast_strlen_zero(calleridname))
			chan->cid.cid_name = NULL;
		else
			chan->cid.cid_name = strdup(calleridname);
	}
	if (ani) {
		if (chan->cid.cid_ani)
			free(chan->cid.cid_ani);
		if (ast_strlen_zero(ani))
			chan->cid.cid_ani = NULL;
		else
			chan->cid.cid_ani = strdup(ani);
	}
	if (chan->cdr)
		ast_cdr_setcid(chan->cdr, chan);
	manager_event(EVENT_FLAG_CALL, "Newcallerid", 
				"Channel: %s\r\n"
				"CallerID: %s\r\n"
				"CallerIDName: %s\r\n"
				"Uniqueid: %s\r\n"
				"CID-CallingPres: %d (%s)\r\n",
				chan->name, chan->cid.cid_num ? 
				chan->cid.cid_num : "<Unknown>",
				chan->cid.cid_name ? 
				chan->cid.cid_name : "<Unknown>",
				chan->uniqueid,
				chan->cid.cid_pres,
				ast_describe_caller_presentation(chan->cid.cid_pres)
				);
}

int ast_setstate(struct ast_channel *chan, int state)
{
	int oldstate = chan->_state;

	if (oldstate == state)
		return 0;

	chan->_state = state;
	ast_device_state_changed_literal(chan->name);
	manager_event(EVENT_FLAG_CALL,
		      (oldstate == AST_STATE_DOWN) ? "Newchannel" : "Newstate",
		      "Channel: %s\r\n"
		      "State: %s\r\n"
		      "CallerID: %s\r\n"
		      "CallerIDName: %s\r\n"
		      "Uniqueid: %s\r\n",
		      chan->name, ast_state2str(chan->_state), 
		      chan->cid.cid_num ? chan->cid.cid_num : "<unknown>", 
		      chan->cid.cid_name ? chan->cid.cid_name : "<unknown>", 
		      chan->uniqueid);

	return 0;
}

/*--- Find bridged channel */
struct ast_channel *ast_bridged_channel(struct ast_channel *chan)
{
	struct ast_channel *bridged;
	bridged = chan->_bridge;
	if (bridged && bridged->tech->bridged_channel) 
		bridged = bridged->tech->bridged_channel(chan, bridged);
	return bridged;
}

static void bridge_playfile(struct ast_channel *chan, struct ast_channel *peer, char *sound, int remain) 
{
	int res=0, min=0, sec=0,check=0;

	check = ast_autoservice_start(peer);
	if(check) 
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
		res = ast_streamfile(chan, "vm-youhave", chan->language);
		res = ast_waitstream(chan, "");
		if (min) {
			res = ast_say_number(chan, min, AST_DIGIT_ANY, chan->language, (char *) NULL);
			res = ast_streamfile(chan, "queue-minutes", chan->language);
			res = ast_waitstream(chan, "");
		}
		if (sec) {
			res = ast_say_number(chan, sec, AST_DIGIT_ANY, chan->language, (char *) NULL);
			res = ast_streamfile(chan, "queue-seconds", chan->language);
			res = ast_waitstream(chan, "");
		}
	} else {
		res = ast_streamfile(chan, sound, chan->language);
		res = ast_waitstream(chan, "");
	}

	check = ast_autoservice_stop(peer);
}

static enum ast_bridge_result ast_generic_bridge(struct ast_channel *c0, struct ast_channel *c1,
			      struct ast_bridge_config *config, struct ast_frame **fo, struct ast_channel **rc, int toms)
{
	/* Copy voice back and forth between the two channels. */
	struct ast_channel *cs[3];
	struct ast_frame *f;
	struct ast_channel *who = NULL;
	enum ast_bridge_result res = AST_BRIDGE_COMPLETE;
	int o0nativeformats;
	int o1nativeformats;
	int watch_c0_dtmf;
	int watch_c1_dtmf;
	void *pvt0, *pvt1;
	
	cs[0] = c0;
	cs[1] = c1;
	pvt0 = c0->tech_pvt;
	pvt1 = c1->tech_pvt;
	o0nativeformats = c0->nativeformats;
	o1nativeformats = c1->nativeformats;
	watch_c0_dtmf = config->flags & AST_BRIDGE_DTMF_CHANNEL_0;
	watch_c1_dtmf = config->flags & AST_BRIDGE_DTMF_CHANNEL_1;

	for (;;) {
		if ((c0->tech_pvt != pvt0) || (c1->tech_pvt != pvt1) ||
		    (o0nativeformats != c0->nativeformats) ||
		    (o1nativeformats != c1->nativeformats)) {
			/* Check for Masquerade, codec changes, etc */
			res = AST_BRIDGE_RETRY;
			break;
		}
		who = ast_waitfor_n(cs, 2, &toms);
		if (!who) {
			if (!toms) {
				res = AST_BRIDGE_RETRY;
				break;
			}
			ast_log(LOG_DEBUG, "Nobody there, continuing...\n"); 
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
			res = AST_BRIDGE_COMPLETE;
			ast_log(LOG_DEBUG, "Didn't get a frame from channel: %s\n",who->name);
			break;
		}

		if ((f->frametype == AST_FRAME_CONTROL) && !(config->flags & AST_BRIDGE_IGNORE_SIGS)) {
			if ((f->subclass == AST_CONTROL_HOLD) || (f->subclass == AST_CONTROL_UNHOLD) ||
			    (f->subclass == AST_CONTROL_VIDUPDATE)) {
				ast_indicate(who == c0 ? c1 : c0, f->subclass);
			} else {
				*fo = f;
				*rc = who;
				res =  AST_BRIDGE_COMPLETE;
				ast_log(LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass, who->name);
				break;
			}
		}
		if ((f->frametype == AST_FRAME_VOICE) ||
		    (f->frametype == AST_FRAME_DTMF) ||
		    (f->frametype == AST_FRAME_VIDEO) || 
		    (f->frametype == AST_FRAME_IMAGE) ||
		    (f->frametype == AST_FRAME_HTML) ||
		    (f->frametype == AST_FRAME_TEXT)) {
			if (f->frametype == AST_FRAME_DTMF) {
				if (((who == c0) && watch_c0_dtmf) ||
				    ((who == c1) && watch_c1_dtmf)) {
					*rc = who;
					*fo = f;
					res = AST_BRIDGE_COMPLETE;
					ast_log(LOG_DEBUG, "Got DTMF on channel (%s)\n", who->name);
					break;
				} else {
					goto tackygoto;
				}
			} else {
#if 0
				ast_log(LOG_DEBUG, "Read from %s\n", who->name);
				if (who == last) 
					ast_log(LOG_DEBUG, "Servicing channel %s twice in a row?\n", last->name);
				last = who;
#endif
tackygoto:
				ast_write((who == c0) ? c1 : c0, f);
			}
		}
		ast_frfree(f);

		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
	return res;
}

/*--- ast_channel_bridge: Bridge two channels together */
enum ast_bridge_result ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1,
					  struct ast_bridge_config *config, struct ast_frame **fo, struct ast_channel **rc) 
{
	struct ast_channel *who = NULL;
	enum ast_bridge_result res = AST_BRIDGE_COMPLETE;
	int nativefailed=0;
	int firstpass;
	int o0nativeformats;
	int o1nativeformats;
	long time_left_ms=0;
	struct timeval nexteventts = { 0, };
	char caller_warning = 0;
	char callee_warning = 0;
	int to;

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
	firstpass = config->firstpass;
	config->firstpass = 0;

	if (ast_tvzero(config->start_time))
		config->start_time = ast_tvnow();
	time_left_ms = config->timelimit;

	caller_warning = ast_test_flag(&config->features_caller, AST_FEATURE_PLAY_WARNING);
	callee_warning = ast_test_flag(&config->features_callee, AST_FEATURE_PLAY_WARNING);

	if (config->start_sound && firstpass) {
		if (caller_warning)
			bridge_playfile(c0, c1, config->start_sound, time_left_ms / 1000);
		if (callee_warning)
			bridge_playfile(c1, c0, config->start_sound, time_left_ms / 1000);
	}

	/* Keep track of bridge */
	c0->_bridge = c1;
	c1->_bridge = c0;
	
	manager_event(EVENT_FLAG_CALL, "Link", 
		      "Channel1: %s\r\n"
		      "Channel2: %s\r\n"
		      "Uniqueid1: %s\r\n"
		      "Uniqueid2: %s\r\n"
		      "CallerID1: %s\r\n"
		      "CallerID2: %s\r\n",
		      c0->name, c1->name, c0->uniqueid, c1->uniqueid, c0->cid.cid_num, c1->cid.cid_num);
                                                                        
	o0nativeformats = c0->nativeformats;
	o1nativeformats = c1->nativeformats;

	if (config->timelimit) {
		nexteventts = ast_tvadd(config->start_time, ast_samp2tv(config->timelimit, 1000));
		if (caller_warning || callee_warning)
			nexteventts = ast_tvsub(nexteventts, ast_samp2tv(config->play_warning, 1000));
	}

	for (/* ever */;;) {
		to = -1;
		if (config->timelimit) {
			struct timeval now;
			now = ast_tvnow();
			to = ast_tvdiff_ms(nexteventts, now);
			if (to < 0)
				to = 0;
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
				if (time_left_ms >= 5000) {
					if (caller_warning && config->warning_sound && config->play_warning)
						bridge_playfile(c0, c1, config->warning_sound, time_left_ms / 1000);
					if (callee_warning && config->warning_sound && config->play_warning)
						bridge_playfile(c1, c0, config->warning_sound, time_left_ms / 1000);
				}
				if (config->warning_freq) {
					nexteventts = ast_tvadd(nexteventts, ast_samp2tv(config->warning_freq, 1000));
				} else
					nexteventts = ast_tvadd(config->start_time, ast_samp2tv(config->timelimit, 1000));
			}
		}

		if (c0->_softhangup == AST_SOFTHANGUP_UNBRIDGE || c1->_softhangup == AST_SOFTHANGUP_UNBRIDGE) {
			if (c0->_softhangup == AST_SOFTHANGUP_UNBRIDGE)
				c0->_softhangup = 0;
			if (c1->_softhangup == AST_SOFTHANGUP_UNBRIDGE)
				c1->_softhangup = 0;
			c0->_bridge = c1;
			c1->_bridge = c0;
			ast_log(LOG_DEBUG, "Unbridge signal received. Ending native bridge.\n");
			continue;
		}
		
		/* Stop if we're a zombie or need a soft hangup */
		if (ast_test_flag(c0, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c0) ||
		    ast_test_flag(c1, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c1)) {
			*fo = NULL;
			if (who)
				*rc = who;
			res = 0;
			ast_log(LOG_DEBUG, "Bridge stops because we're zombie or need a soft hangup: c0=%s, c1=%s, flags: %s,%s,%s,%s\n",
				c0->name, c1->name,
				ast_test_flag(c0, AST_FLAG_ZOMBIE) ? "Yes" : "No",
				ast_check_hangup(c0) ? "Yes" : "No",
				ast_test_flag(c1, AST_FLAG_ZOMBIE) ? "Yes" : "No",
				ast_check_hangup(c1) ? "Yes" : "No");
			break;
		}

		if (c0->tech->bridge &&
		    (config->timelimit == 0) &&
		    (c0->tech->bridge == c1->tech->bridge) &&
		    !nativefailed && !c0->monitor && !c1->monitor &&
		    !c0->spies && !c1->spies) {
			/* Looks like they share a bridge method and nothing else is in the way */
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "Attempting native bridge of %s and %s\n", c0->name, c1->name);
			ast_set_flag(c0, AST_FLAG_NBRIDGE);
			ast_set_flag(c1, AST_FLAG_NBRIDGE);
			if ((res = c0->tech->bridge(c0, c1, config->flags, fo, rc, to)) == AST_BRIDGE_COMPLETE) {
				manager_event(EVENT_FLAG_CALL, "Unlink", 
					      "Channel1: %s\r\n"
					      "Channel2: %s\r\n"
					      "Uniqueid1: %s\r\n"
					      "Uniqueid2: %s\r\n"
					      "CallerID1: %s\r\n"
					      "CallerID2: %s\r\n",
					      c0->name, c1->name, c0->uniqueid, c1->uniqueid, c0->cid.cid_num, c1->cid.cid_num);
				ast_log(LOG_DEBUG, "Returning from native bridge, channels: %s, %s\n", c0->name, c1->name);

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
				ast_verbose(VERBOSE_PREFIX_3 "Native bridge of %s and %s was unsuccessful\n", c0->name, c1->name);
			}
			if (res == AST_BRIDGE_RETRY)
				continue;
			switch (res) {
			case AST_BRIDGE_RETRY:
/*				continue; */
				break;
			default:
				ast_log(LOG_WARNING, "Private bridge between %s and %s failed\n", c0->name, c1->name);
				/* fallthrough */
			case AST_BRIDGE_FAILED_NOWARN:
				nativefailed++;
				break;
			}
		}
	
		if (((c0->writeformat != c1->readformat) || (c0->readformat != c1->writeformat) ||
		    (c0->nativeformats != o0nativeformats) || (c1->nativeformats != o1nativeformats)) &&
		    !(c0->generator || c1->generator)) {
			if (ast_channel_make_compatible(c0, c1)) {
				ast_log(LOG_WARNING, "Can't make %s and %s compatible\n", c0->name, c1->name);
                                manager_event(EVENT_FLAG_CALL, "Unlink",
					      "Channel1: %s\r\n"
					      "Channel2: %s\r\n"
					      "Uniqueid1: %s\r\n"
					      "Uniqueid2: %s\r\n"
					      "CallerID1: %s\r\n"
					      "CallerID2: %s\r\n",
					      c0->name, c1->name, c0->uniqueid, c1->uniqueid, c0->cid.cid_num, c1->cid.cid_num);
				return AST_BRIDGE_FAILED;
			}
			o0nativeformats = c0->nativeformats;
			o1nativeformats = c1->nativeformats;
		}
		res = ast_generic_bridge(c0, c1, config, fo, rc, to);
		if (res != AST_BRIDGE_RETRY)
			break;
	}

	c0->_bridge = NULL;
	c1->_bridge = NULL;

	manager_event(EVENT_FLAG_CALL, "Unlink",
		      "Channel1: %s\r\n"
		      "Channel2: %s\r\n"
		      "Uniqueid1: %s\r\n"
		      "Uniqueid2: %s\r\n"
		      "CallerID1: %s\r\n"
		      "CallerID2: %s\r\n",
		      c0->name, c1->name, c0->uniqueid, c1->uniqueid, c0->cid.cid_num, c1->cid.cid_num);
	ast_log(LOG_DEBUG, "Bridge stops bridging channels %s and %s\n", c0->name, c1->name);

	return res;
}

/*--- ast_channel_setoption: Sets an option on a channel */
int ast_channel_setoption(struct ast_channel *chan, int option, void *data, int datalen, int block)
{
	int res;

	if (chan->tech->setoption) {
		res = chan->tech->setoption(chan, option, data, datalen);
		if (res < 0)
			return res;
	} else {
		errno = ENOSYS;
		return -1;
	}
	if (block) {
		/* XXX Implement blocking -- just wait for our option frame reply, discarding
		   intermediate packets. XXX */
		ast_log(LOG_ERROR, "XXX Blocking not implemented yet XXX\n");
		return -1;
	}
	return 0;
}

struct tonepair_def {
	int freq1;
	int freq2;
	int duration;
	int vol;
};

struct tonepair_state {
	float freq1;
	float freq2;
	float vol;
	int duration;
	int pos;
	int origwfmt;
	struct ast_frame f;
	unsigned char offset[AST_FRIENDLY_OFFSET];
	short data[4000];
};

static void tonepair_release(struct ast_channel *chan, void *params)
{
	struct tonepair_state *ts = params;

	if (chan) {
		ast_set_write_format(chan, ts->origwfmt);
	}
	free(ts);
}

static void *tonepair_alloc(struct ast_channel *chan, void *params)
{
	struct tonepair_state *ts;
	struct tonepair_def *td = params;

	ts = malloc(sizeof(struct tonepair_state));
	if (!ts)
		return NULL;
	memset(ts, 0, sizeof(struct tonepair_state));
	ts->origwfmt = chan->writeformat;
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set '%s' to signed linear format (write)\n", chan->name);
		tonepair_release(NULL, ts);
		ts = NULL;
	} else {
		ts->freq1 = td->freq1;
		ts->freq2 = td->freq2;
		ts->duration = td->duration;
		ts->vol = td->vol;
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
	for (x = 0; x < (len / 2); x++) {
		ts->data[x] = ts->vol * (
				sin((ts->freq1 * 2.0 * M_PI / 8000.0) * (ts->pos + x)) +
				sin((ts->freq2 * 2.0 * M_PI / 8000.0) * (ts->pos + x))
			);
	}
	ts->f.frametype = AST_FRAME_VOICE;
	ts->f.subclass = AST_FORMAT_SLINEAR;
	ts->f.datalen = len;
	ts->f.samples = samples;
	ts->f.offset = AST_FRIENDLY_OFFSET;
	ts->f.data = ts->data;
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
	if (vol < 1)
		d.vol = 8192;
	else
		d.vol = vol;
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
	struct ast_frame *f;
	int res;

	if ((res = ast_tonepair_start(chan, freq1, freq2, duration, vol)))
		return res;

	/* Give us some wiggle room */
	while(chan->generatordata && (ast_waitfor(chan, 100) >= 0)) {
		f = ast_read(chan);
		if (f)
			ast_frfree(f);
		else
			return -1;
	}
	return 0;
}

ast_group_t ast_get_group(char *s)
{
	char *copy;
	char *piece;
	char *c=NULL;
	int start=0, finish=0, x;
	ast_group_t group = 0;

	copy = ast_strdupa(s);
	if (!copy) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return 0;
	}
	c = copy;
	
	while((piece = strsep(&c, ","))) {
		if (sscanf(piece, "%d-%d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(piece, "%d", &start)) {
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

static int (*ast_moh_start_ptr)(struct ast_channel *, char *) = NULL;
static void (*ast_moh_stop_ptr)(struct ast_channel *) = NULL;
static void (*ast_moh_cleanup_ptr)(struct ast_channel *) = NULL;


void ast_install_music_functions(int (*start_ptr)(struct ast_channel *, char *),
								 void (*stop_ptr)(struct ast_channel *),
								 void (*cleanup_ptr)(struct ast_channel *)
								 ) 
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

/*! Turn on music on hold on a given channel */
int ast_moh_start(struct ast_channel *chan, char *mclass) 
{
	if (ast_moh_start_ptr)
		return ast_moh_start_ptr(chan, mclass);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Music class %s requested but no musiconhold loaded.\n", mclass ? mclass : "default");
	
	return 0;
}

/*! Turn off music on hold on a given channel */
void ast_moh_stop(struct ast_channel *chan) 
{
	if(ast_moh_stop_ptr)
		ast_moh_stop_ptr(chan);
}

void ast_moh_cleanup(struct ast_channel *chan) 
{
	if(ast_moh_cleanup_ptr)
        ast_moh_cleanup_ptr(chan);
}

void ast_channels_init(void)
{
	ast_cli_register(&cli_show_channeltypes);
}

/*--- ast_print_group: Print call group and pickup group ---*/
char *ast_print_group(char *buf, int buflen, ast_group_t group) 
{
	unsigned int i;
	int first=1;
	char num[3];

	buf[0] = '\0';
	
	if (!group)	/* Return empty string if no group */
		return(buf);

	for (i=0; i<=63; i++) {	/* Max group is 63 */
		if (group & ((ast_group_t) 1 << i)) {
	   		if (!first) {
				strncat(buf, ", ", buflen);
			} else {
				first=0;
	  		}
			snprintf(num, sizeof(num), "%u", i);
			strncat(buf, num, buflen);
		}
	}
	return(buf);
}

void ast_set_variables(struct ast_channel *chan, struct ast_variable *vars)
{
	struct ast_variable *cur;

	for (cur = vars; cur; cur = cur->next)
		pbx_builtin_setvar_helper(chan, cur->name, cur->value);	
}

static void copy_data_from_queue(struct ast_channel_spy_queue *queue, short *buf, unsigned int samples)
{
	struct ast_frame *f;
	int tocopy;
	int bytestocopy;

	while (samples) {
		f = queue->head;

		if (!f) {
			ast_log(LOG_ERROR, "Ran out of frames before buffer filled!\n");
			break;
		}

		tocopy = (f->samples > samples) ? samples : f->samples;
		bytestocopy = ast_codec_get_len(queue->format, samples);
		memcpy(buf, f->data, bytestocopy);
		samples -= tocopy;
		buf += tocopy;
		f->samples -= tocopy;
		f->data += bytestocopy;
		f->datalen -= bytestocopy;
		f->offset += bytestocopy;
		queue->samples -= tocopy;
		if (!f->samples) {
			queue->head = f->next;
			ast_frfree(f);
		}
	}
}

struct ast_frame *ast_channel_spy_read_frame(struct ast_channel_spy *spy, unsigned int samples)
{
	struct ast_frame *result;
	/* buffers are allocated to hold SLINEAR, which is the largest format */
        short read_buf[samples];
        short write_buf[samples];
	struct ast_frame *read_frame;
	struct ast_frame *write_frame;
	int need_dup;
	struct ast_frame stack_read_frame = { .frametype = AST_FRAME_VOICE,
					      .subclass = spy->read_queue.format,
					      .data = read_buf,
					      .samples = samples,
					      .datalen = ast_codec_get_len(spy->read_queue.format, samples),
	};
	struct ast_frame stack_write_frame = { .frametype = AST_FRAME_VOICE,
					       .subclass = spy->write_queue.format,
					       .data = write_buf,
					       .samples = samples,
					       .datalen = ast_codec_get_len(spy->write_queue.format, samples),
	};

	/* if a flush has been requested, dump everything in whichever queue is larger */
	if (ast_test_flag(spy, CHANSPY_TRIGGER_FLUSH)) {
		if (spy->read_queue.samples > spy->write_queue.samples) {
			if (ast_test_flag(spy, CHANSPY_READ_VOLADJUST)) {
				for (result = spy->read_queue.head; result; result = result->next)
					ast_frame_adjust_volume(result, spy->read_vol_adjustment);
			}
			result = spy->read_queue.head;
			spy->read_queue.head = NULL;
			spy->read_queue.samples = 0;
			ast_clear_flag(spy, CHANSPY_TRIGGER_FLUSH);
			return result;
		} else {
			if (ast_test_flag(spy, CHANSPY_WRITE_VOLADJUST)) {
				for (result = spy->write_queue.head; result; result = result->next)
					ast_frame_adjust_volume(result, spy->write_vol_adjustment);
			}
			result = spy->write_queue.head;
			spy->write_queue.head = NULL;
			spy->write_queue.samples = 0;
			ast_clear_flag(spy, CHANSPY_TRIGGER_FLUSH);
			return result;
		}
	}

	if ((spy->read_queue.samples < samples) || (spy->write_queue.samples < samples))
		return NULL;

	/* short-circuit if both head frames have exactly what we want */
	if ((spy->read_queue.head->samples == samples) &&
	    (spy->write_queue.head->samples == samples)) {
		read_frame = spy->read_queue.head;
		spy->read_queue.head = read_frame->next;
		read_frame->next = NULL;

		write_frame = spy->write_queue.head;
		spy->write_queue.head = write_frame->next;
		write_frame->next = NULL;

		spy->read_queue.samples -= samples;
		spy->write_queue.samples -= samples;

		need_dup = 0;
	} else {
		copy_data_from_queue(&spy->read_queue, read_buf, samples);
		copy_data_from_queue(&spy->write_queue, write_buf, samples);

		read_frame = &stack_read_frame;
		write_frame = &stack_write_frame;
		need_dup = 1;
	}
	
	if (ast_test_flag(spy, CHANSPY_READ_VOLADJUST))
		ast_frame_adjust_volume(read_frame, spy->read_vol_adjustment);

	if (ast_test_flag(spy, CHANSPY_WRITE_VOLADJUST))
		ast_frame_adjust_volume(write_frame, spy->write_vol_adjustment);

	if (ast_test_flag(spy, CHANSPY_MIXAUDIO)) {
		ast_frame_slinear_sum(read_frame, write_frame);

		if (need_dup)
			result = ast_frdup(read_frame);
		else
			result = read_frame;
	} else {
		if (need_dup) {
			result = ast_frdup(read_frame);
			result->next = ast_frdup(write_frame);
		} else {
			result = read_frame;
			result->next = write_frame;
		}
	}

	return result;
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
	if (samples == 160) {
		short buf[160] = { 0, };
		struct ast_frame frame = {
			.frametype = AST_FRAME_VOICE,
			.subclass = AST_FORMAT_SLINEAR,
			.data = buf,
			.samples = 160,
			.datalen = sizeof(buf),
		};

		if (ast_write(chan, &frame))
			return -1;
	} else {
		short buf[samples];
		int x;
		struct ast_frame frame = {
			.frametype = AST_FRAME_VOICE,
			.subclass = AST_FORMAT_SLINEAR,
			.data = buf,
			.samples = samples,
			.datalen = sizeof(buf),
		};

		for (x = 0; x < samples; x++)
			buf[x] = 0;

		if (ast_write(chan, &frame))
			return -1;
	}

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

	if (!(state = calloc(1, sizeof(*state)))) {
		ast_log(LOG_WARNING, "Could not allocate state structure\n");
		return NULL;
	}

	state->old_write_format = chan->writeformat;

	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could not set write format to SLINEAR\n");
		free(state);
		return NULL;
	}

	ast_activate_generator(chan, &silence_generator, state);

	if (option_debug)
		ast_log(LOG_DEBUG, "Started silence generator on '%s'\n", chan->name);

	return state;
}

void ast_channel_stop_silence_generator(struct ast_channel *chan, struct ast_silence_generator *state)
{
	if (!state)
		return;

	ast_deactivate_generator(chan);

	if (option_debug)
		ast_log(LOG_DEBUG, "Stopped silence generator on '%s'\n", chan->name);

	if (ast_set_write_format(chan, state->old_write_format) < 0)
		ast_log(LOG_ERROR, "Could not return write format to its original state\n");

	free(state);
}
