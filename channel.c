/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Management
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>			/* For PI */
#include <asterisk/pbx.h>
#include <asterisk/frame.h>
#include <asterisk/sched.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/musiconhold.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/say.h>
#include <asterisk/file.h>
#include <asterisk/cli.h>
#include <asterisk/translate.h>
#include <asterisk/manager.h>
#include <asterisk/chanvars.h>
#include <asterisk/linkedlists.h>
#include <asterisk/indications.h>
#include <asterisk/monitor.h>
#include <asterisk/causes.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <asterisk/app.h>
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

/* uncomment if you have problems with 'monitoring' synchronized files */
#if 0
#define MONITOR_CONSTANT_DELAY
#define MONITOR_DELAY	150 * 8		/* 150 ms of MONITORING DELAY */
#endif

static int shutting_down = 0;
static int uniqueint = 0;

unsigned long global_fin = 0, global_fout = 0;

/* XXX Lock appropriately in more functions XXX */

struct chanlist {
	char type[80];
	char description[80];
	int capabilities;
	struct ast_channel * (*requester)(const char *type, int format, void *data, int *cause);
	int (*devicestate)(void *data);
	struct chanlist *next;
} *backends = NULL;
struct ast_channel *channels = NULL;

/* Protect the channel list (highly unlikely that two things would change
   it at the same time, but still! */
   
AST_MUTEX_DEFINE_STATIC(chlock);

static int show_channeltypes(int fd, int argc, char *argv[])
{
#define FORMAT  "%-7.7s  %-50.50s\n"
	struct chanlist *cl = backends;
	ast_cli(fd, FORMAT, "Type", "Description");
	ast_cli(fd, FORMAT, "------", "-----------");
	if (ast_mutex_lock(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return -1;
	}
	while (cl) {
		ast_cli(fd, FORMAT, cl->type, cl->description);
		cl = cl->next;
	}
	ast_mutex_unlock(&chlock);
	return RESULT_SUCCESS;

}

static char show_channeltypes_usage[] = 
"Usage: show channeltypes\n"
"       Shows available channel types registred in your Asterisk server.";

static struct ast_cli_entry cli_show_channeltypes = 
	{ { "show", "channeltypes", NULL }, show_channeltypes, "Show available channel types", show_channeltypes_usage };

int ast_check_hangup(struct ast_channel *chan)
{
time_t	myt;

	  /* if soft hangup flag, return true */
	if (chan->_softhangup) return 1;
	  /* if no private structure, return true */
	if (!chan->pvt->pvt) return 1;
	  /* if no hangup scheduled, just return here */
	if (!chan->whentohangup) return 0;
	time(&myt); /* get current time */
	  /* return, if not yet */
	if (chan->whentohangup > myt) return 0;
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

void ast_cancel_shutdown(void)
{
	shutting_down = 0;
}

int ast_shutting_down(void)
{
	return shutting_down;
}

void ast_channel_setwhentohangup(struct ast_channel *chan, time_t offset)
{
	time_t	myt;

	time(&myt);
	if (offset)
		chan->whentohangup = myt + offset;
	else
		chan->whentohangup = 0;
	return;
}

int ast_channel_register(const char *type, const char *description, int capabilities,
		struct ast_channel *(*requester)(const char *type, int format, void *data, int *cause))
{
	return ast_channel_register_ex(type, description, capabilities, requester, NULL);
}

int ast_channel_register_ex(const char *type, const char *description, int capabilities,
		struct ast_channel *(*requester)(const char *type, int format, void *data, int *cause),
		int (*devicestate)(void *data))
{
	struct chanlist *chan, *last=NULL;
	if (ast_mutex_lock(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return -1;
	}
	chan = backends;
	while (chan) {
		if (!strcasecmp(type, chan->type)) {
			ast_log(LOG_WARNING, "Already have a handler for type '%s'\n", type);
			ast_mutex_unlock(&chlock);
			return -1;
		}
		last = chan;
		chan = chan->next;
	}
	chan = malloc(sizeof(struct chanlist));
	if (!chan) {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_mutex_unlock(&chlock);
		return -1;
	}
	strncpy(chan->type, type, sizeof(chan->type)-1);
	strncpy(chan->description, description, sizeof(chan->description)-1);
	chan->capabilities = capabilities;
	chan->requester = requester;
	chan->devicestate = devicestate;
	chan->next = NULL;
	if (last)
		last->next = chan;
	else
		backends = chan;
	if (option_debug)
		ast_log(LOG_DEBUG, "Registered handler for '%s' (%s)\n", chan->type, chan->description);
	else if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered channel type '%s' (%s)\n", chan->type, chan->description);
	ast_mutex_unlock(&chlock);
	return 0;
}

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
	
	
	for (x=0;x<sizeof(prefs) / sizeof(prefs[0]); x++)
		if (fmts & prefs[x])
			return prefs[x];
	ast_log(LOG_WARNING, "Don't know any of 0x%x formats\n", fmts);
	return 0;
}

struct ast_channel *ast_channel_alloc(int needqueue)
{
	struct ast_channel *tmp;
	struct ast_channel_pvt *pvt;
	int x;
	int flags;
	struct varshead *headp;        
	        
	
	/* If shutting down, don't allocate any new channels */
	if (shutting_down)
		return NULL;
	ast_mutex_lock(&chlock);
	tmp = malloc(sizeof(struct ast_channel));
	if (tmp) {
		memset(tmp, 0, sizeof(struct ast_channel));
		pvt = malloc(sizeof(struct ast_channel_pvt));
		if (pvt) {
			memset(pvt, 0, sizeof(struct ast_channel_pvt));
			tmp->sched = sched_context_create();
			if (tmp->sched) {
				for (x=0;x<AST_MAX_FDS - 1;x++)
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
				if (needqueue &&  
					pipe(pvt->alertpipe)) {
					ast_log(LOG_WARNING, "Alert pipe creation failed!\n");
					free(pvt);
					free(tmp);
					tmp = NULL;
					pvt = NULL;
				} else {
					if (needqueue) {
						flags = fcntl(pvt->alertpipe[0], F_GETFL);
						fcntl(pvt->alertpipe[0], F_SETFL, flags | O_NONBLOCK);
						flags = fcntl(pvt->alertpipe[1], F_GETFL);
						fcntl(pvt->alertpipe[1], F_SETFL, flags | O_NONBLOCK);
					} else 
					/* Make sure we've got it done right if they don't */
						pvt->alertpipe[0] = pvt->alertpipe[1] = -1;
					/* Always watch the alertpipe */
					tmp->fds[AST_MAX_FDS-1] = pvt->alertpipe[0];
					/* And timing pipe */
					tmp->fds[AST_MAX_FDS-2] = tmp->timingfd;
					strncpy(tmp->name, "**Unknown**", sizeof(tmp->name)-1);
					tmp->pvt = pvt;
					/* Initial state */
					tmp->_state = AST_STATE_DOWN;
					tmp->streamid = -1;
					tmp->appl = NULL;
					tmp->data = NULL;
					tmp->fin = global_fin;
					tmp->fout = global_fout;
					snprintf(tmp->uniqueid, sizeof(tmp->uniqueid), "%li.%d", (long)time(NULL), uniqueint++);
					headp=&tmp->varshead;
					ast_mutex_init(&tmp->lock);
				        AST_LIST_HEAD_INIT(headp);
					strncpy(tmp->context, "default", sizeof(tmp->context)-1);
					strncpy(tmp->language, defaultlanguage, sizeof(tmp->language)-1);
					strncpy(tmp->exten, "s", sizeof(tmp->exten)-1);
					tmp->priority=1;
					tmp->amaflags = ast_default_amaflags;
					strncpy(tmp->accountcode, ast_default_accountcode, sizeof(tmp->accountcode)-1);
					tmp->next = channels;
					channels= tmp;
				}
			} else {
				ast_log(LOG_WARNING, "Unable to create schedule context\n");
				free(tmp);
				tmp = NULL;
			}
		} else {
			ast_log(LOG_WARNING, "Out of memory\n");
			free(tmp);
			tmp = NULL;
		}
	} else 
		ast_log(LOG_WARNING, "Out of memory\n");
	ast_mutex_unlock(&chlock);
	return tmp;
}

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
	cur = chan->pvt->readq;
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
		chan->pvt->readq = f;
	if (chan->pvt->alertpipe[1] > -1) {
		if (write(chan->pvt->alertpipe[1], &blah, sizeof(blah)) != sizeof(blah))
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

int ast_queue_hangup(struct ast_channel *chan)
{
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_HANGUP };
	chan->_softhangup |= AST_SOFTHANGUP_DEV;
	return ast_queue_frame(chan, &f);
}

int ast_queue_control(struct ast_channel *chan, int control)
{
	struct ast_frame f = { AST_FRAME_CONTROL, };
	f.subclass = control;
	return ast_queue_frame(chan, &f);
}

int ast_channel_defer_dtmf(struct ast_channel *chan)
{
	int pre = 0;
	if (chan) {
		pre = ast_test_flag(chan, AST_FLAG_DEFER_DTMF);
		ast_set_flag(chan, AST_FLAG_DEFER_DTMF);
	}
	return pre;
}

void ast_channel_undefer_dtmf(struct ast_channel *chan)
{
	if (chan)
		ast_clear_flag(chan, AST_FLAG_DEFER_DTMF);
}

struct ast_channel *ast_channel_walk_locked(struct ast_channel *prev)
{
	/* Returns next channel (locked) */
	struct ast_channel *l, *ret;
	int retries = 0;	
retry:
	ret=NULL;
	ast_mutex_lock(&chlock);
	l = channels;
	if (!prev) {
		if (l) {
			if (ast_mutex_trylock(&l->lock)) {
				if (retries < 10)
					ast_log(LOG_DEBUG, "Avoiding initial deadlock for '%s'\n", l->name);
				else
					ast_log(LOG_WARNING, "Avoided initial deadlock for '%s', %d retries!\n", l->name, retries);
				ast_mutex_unlock(&chlock);
				if (retries < 10) {
					usleep(1);
					retries++;
					goto retry;
				} else
					return NULL;
			}
		}
		ast_mutex_unlock(&chlock);
		return l;
	}
	while(l) {
		if (l == prev)
			ret = l->next;
		l = l->next;
	}
	if (ret) {
		if (ast_mutex_trylock(&ret->lock)) {
			if (retries < 10)
				ast_log(LOG_DEBUG, "Avoiding deadlock for '%s'\n", ret->name);
			else
				ast_log(LOG_WARNING, "Avoided deadlock for '%s', %d retries!\n", ret->name, retries);
			ast_mutex_unlock(&chlock);
			if (retries < 10) {
				usleep(1);
				retries++;
				goto retry;
			} else
				return NULL;
		}
	}
	ast_mutex_unlock(&chlock);
	return ret;
	
}

struct ast_channel *ast_get_channel_by_name_locked(char *channame)
{
	struct ast_channel *chan;
	chan = ast_channel_walk_locked(NULL);
	while(chan) {
		if (!strcasecmp(chan->name, channame))
			return chan;
		ast_mutex_unlock(&chan->lock);
		chan = ast_channel_walk_locked(chan);
	}
	return NULL;
}

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
	if (chan->pvt->pvt)
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n", chan->name);

	strncpy(name, chan->name, sizeof(name)-1);
	
	/* Stop monitoring */
	if (chan->monitor) {
		chan->monitor->stop( chan, 0 );
	}

	/* If there is native format music-on-hold state, free it */
	if(chan->music_state)
		ast_moh_cleanup(chan);

	/* Free translatosr */
	if (chan->pvt->readtrans)
		ast_translator_free_path(chan->pvt->readtrans);
	if (chan->pvt->writetrans)
		ast_translator_free_path(chan->pvt->writetrans);
	if (chan->pbx) 
		ast_log(LOG_WARNING, "PBX may not have been terminated properly on '%s'\n", chan->name);
	free_cid(&chan->cid);
	ast_mutex_destroy(&chan->lock);
	/* Close pipes if appropriate */
	if ((fd = chan->pvt->alertpipe[0]) > -1)
		close(fd);
	if ((fd = chan->pvt->alertpipe[1]) > -1)
		close(fd);
	if ((fd = chan->timingfd) > -1)
		close(fd);
	f = chan->pvt->readq;
	chan->pvt->readq = NULL;
	while(f) {
		fp = f;
		f = f->next;
		ast_frfree(fp);
	}
	
	/* loop over the variables list, freeing all data and deleting list items */
	/* no need to lock the list, as the channel is already locked */
	
	while (!AST_LIST_EMPTY(headp)) {           /* List Deletion. */
	            vardata = AST_LIST_REMOVE_HEAD(headp, entries);
/*	            printf("deleting var %s=%s\n",ast_var_name(vardata),ast_var_value(vardata)); */
	            ast_var_delete(vardata);
	}
	                                                 

	free(chan->pvt);
	chan->pvt = NULL;
	free(chan);
	ast_mutex_unlock(&chlock);

	ast_device_state_changed(name);
}

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

int ast_softhangup(struct ast_channel *chan, int cause)
{
	int res;
	ast_mutex_lock(&chan->lock);
	res = ast_softhangup_nolock(chan, cause);
	ast_mutex_unlock(&chan->lock);
	return res;
}

static void free_translation(struct ast_channel *clone)
{
	if (clone->pvt->writetrans)
		ast_translator_free_path(clone->pvt->writetrans);
	if (clone->pvt->readtrans)
		ast_translator_free_path(clone->pvt->readtrans);
	clone->pvt->writetrans = NULL;
	clone->pvt->readtrans = NULL;
	clone->pvt->rawwriteformat = clone->nativeformats;
	clone->pvt->rawreadformat = clone->nativeformats;
}

int ast_hangup(struct ast_channel *chan)
{
	int res = 0;
	/* Don't actually hang up a channel that will masquerade as someone else, or
	   if someone is going to masquerade as us */
	ast_mutex_lock(&chan->lock);
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
	if (chan->stream) 
		ast_closestream(chan->stream);
	if (chan->vstream)
		ast_closestream(chan->vstream);
	if (chan->sched)
		sched_context_destroy(chan->sched);
	/* Clear any tone stuff remaining */
	if (chan->generatordata)
		chan->generator->release(chan, chan->generatordata);
	chan->generatordata = NULL;
	chan->generator = NULL;
	if (chan->cdr) {
		/* End the CDR if it hasn't already */
		ast_cdr_end(chan->cdr);
		/* Post and Free the CDR */
		ast_cdr_post(chan->cdr);
		ast_cdr_free(chan->cdr);
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
		if (chan->pvt->hangup)
			res = chan->pvt->hangup(chan);
	} else
		if (option_debug)
			ast_log(LOG_DEBUG, "Hanging up zombie '%s'\n", chan->name);
			
	ast_mutex_unlock(&chan->lock);
	manager_event(EVENT_FLAG_CALL, "Hangup", 
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Cause: %i\r\n",
			chan->name, chan->uniqueid, chan->hangupcause);
	ast_channel_free(chan);
	return res;
}

void ast_channel_unregister(const char *type)
{
	struct chanlist *chan, *last=NULL;
	if (option_debug)
		ast_log(LOG_DEBUG, "Unregistering channel type '%s'\n", type);
	if (ast_mutex_lock(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return;
	}
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Unregistered channel type '%s'\n", type);

	chan = backends;
	while(chan) {
		if (!strcasecmp(chan->type, type)) {
			if (last)
				last->next = chan->next;
			else
				backends = backends->next;
			free(chan);
			ast_mutex_unlock(&chlock);
			return;
		}
		last = chan;
		chan = chan->next;
	}
	ast_mutex_unlock(&chlock);
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
		if (chan->pvt->answer)
			res = chan->pvt->answer(chan);
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
	if ((chan->generatordata = gen->alloc(chan, params))) {
		ast_settimeout(chan, 160, generator_force, chan);
		chan->generator = gen;
	} else {
		res = -1;
	}
	ast_mutex_unlock(&chan->lock);
	return res;
}

int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception)
{
	/* Wait for x amount of time on a file descriptor to have input.  */
	struct timeval start, now;
	int res;
	int x, y;
	int winner = -1;
	int spoint;
	struct pollfd *pfds;
	
	pfds = alloca(sizeof(struct pollfd) * n);
	if (!pfds) {
		ast_log(LOG_WARNING, "alloca failed!  bad things will happen.\n");
		return -1;
	}
	if (*ms > 0)
		gettimeofday(&start, NULL);
	y = 0;
	for (x=0;x<n;x++) {
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
	for (x=0;x<n;x++) {
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
		long passed;
		gettimeofday(&now, NULL);
		passed = (now.tv_sec - start.tv_sec) * 1000;
		passed += (now.tv_usec - start.tv_usec) / 1000;
		if (passed <= *ms)
			*ms -= passed;
		else
			*ms = 0;
	}
	return winner;
}

struct ast_channel *ast_waitfor_nandfds(struct ast_channel **c, int n, int *fds, int nfds, 
	int *exception, int *outfd, int *ms)
{
	/* Wait for x amount of time on a file descriptor to have input.  */
	struct timeval start, end;
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
		ast_log(LOG_WARNING, "alloca failed!  bad things will happen.\n");
		*outfd = -1;
		return NULL;
	}

	if (outfd)
		*outfd = -99999;
	if (exception)
		*exception = 0;
	
	/* Perform any pending masquerades */
	for (x=0;x<n;x++) {
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
	for (x=0;x<n;x++) {
		for (y=0;y<AST_MAX_FDS;y++) {
			if (c[x]->fds[y] > -1) {
				pfds[max].fd = c[x]->fds[y];
				pfds[max].events = POLLIN | POLLPRI;
				max++;
			}
		}
		CHECK_BLOCKING(c[x]);
	}
	for (x=0;x<nfds; x++) {
		if (fds[x] > -1) {
			pfds[max].fd = fds[x];
			pfds[max].events = POLLIN | POLLPRI;
			max++;
		}
	}
	if (*ms > 0) 
		gettimeofday(&start, NULL);
	res = poll(pfds, max, rms);
	if (res < 0) {
		for (x=0;x<n;x++) 
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
	}

	if (havewhen)
		time(&now);
	spoint = 0;
	for (x=0;x<n;x++) {
		ast_clear_flag(c[x], AST_FLAG_BLOCKING);
		if (havewhen && c[x]->whentohangup && (now > c[x]->whentohangup)) {
			c[x]->_softhangup |= AST_SOFTHANGUP_TIMEOUT;
			if (!winner)
				winner = c[x];
		}
		for (y=0;y<AST_MAX_FDS;y++) {
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
	for (x=0;x<nfds;x++) {
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
		long diff;
		gettimeofday(&end, NULL);
		diff = (end.tv_sec - start.tv_sec) * 1000;
		diff += (end.tv_usec - start.tv_usec) / 1000;
		if (diff < *ms)
			*ms -= diff;
		else
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

char ast_waitfordigit(struct ast_channel *c, int ms)
{
	/* XXX Should I be merged with waitfordigit_full XXX */
	struct ast_frame *f;
	char result = 0;
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
char ast_waitfordigit_full(struct ast_channel *c, int ms, int audiofd, int cmdfd)
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
		rchan = ast_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);
		if ((!rchan) && (outfd < 0) && (ms)) { 
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
	return 0; // Time is up
}

struct ast_frame *ast_read(struct ast_channel *chan)
{
	struct ast_frame *f = NULL;
	int blah;
#ifdef ZAPTEL_OPTIMIZATIONS
	int (*func)(void *);
	void *data;
	int res;
#endif
	static struct ast_frame null_frame = 
	{
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
	if (chan->pvt->alertpipe[0] > -1) {
		read(chan->pvt->alertpipe[0], &blah, sizeof(blah));
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
			if (!chan->pvt->readq || !chan->pvt->readq->next) {
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
	if (chan->pvt->readq) {
		f = chan->pvt->readq;
		chan->pvt->readq = f->next;
		/* Interpret hangup and return NULL */
		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
			ast_frfree(f);
			f = NULL;
		}
	} else {
		chan->blocker = pthread_self();
		if (ast_test_flag(chan, AST_FLAG_EXCEPTION)) {
			if (chan->pvt->exception) 
				f = chan->pvt->exception(chan);
			else {
				ast_log(LOG_WARNING, "Exception flag set on '%s', but no exception handler\n", chan->name);
				f = &null_frame;
			}
			/* Clear the exception flag */
			ast_clear_flag(chan, AST_FLAG_EXCEPTION);
		} else
		if (chan->pvt->read)
			f = chan->pvt->read(chan);
		else
			ast_log(LOG_WARNING, "No read routine on channel %s\n", chan->name);
	}


	if (f && (f->frametype == AST_FRAME_VOICE)) {
		if (!(f->subclass & chan->nativeformats)) {
			/* This frame can't be from the current native formats -- drop it on the
			   floor */
			ast_log(LOG_NOTICE, "Dropping incompatible voice frame on %s of format %s since our native format has changed to %s\n", chan->name, ast_getformatname(f->subclass), ast_getformatname(chan->nativeformats));
			ast_frfree(f);
			f = &null_frame;
		} else {
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
			if (chan->pvt->readtrans) {
				f = ast_translate(chan->pvt->readtrans, f, 1);
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
		if (chan->_state == AST_STATE_UP) {
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
	if (chan->pvt->indicate)
		res = chan->pvt->indicate(chan, condition);
	ast_mutex_unlock(&chan->lock);
	if (!chan->pvt->indicate || res) {
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
	int res,ourto,c;
	struct ast_frame *f;
	
	ourto = timeout;
	for(;;)
	   {
		if (ast_check_hangup(chan)) return -1;
		res = ast_waitfor(chan,ourto);
		if (res <= 0) /* if timeout */
		   {
			return 0;
		   }
		ourto = res;
		f = ast_read(chan);
		if (f == NULL) return -1; /* if hangup */
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_HANGUP)) return -1; /* if hangup */
		if (f->frametype == AST_FRAME_TEXT)  /* if a text frame */
		   {
			c = *((char *)f->data);  /* get the data */
			ast_frfree(f);
			return(c);
		   }
		ast_frfree(f);
	}
}

int ast_sendtext(struct ast_channel *chan, char *text)
{
	int res = 0;
	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(chan, AST_FLAG_ZOMBIE) || ast_check_hangup(chan)) 
		return -1;
	CHECK_BLOCKING(chan);
	if (chan->pvt->send_text)
		res = chan->pvt->send_text(chan, text);
	ast_clear_flag(chan, AST_FLAG_BLOCKING);
	return res;
}

static int do_senddigit(struct ast_channel *chan, char digit)
{
	int res = -1;

	if (chan->pvt->send_digit)
		res = chan->pvt->send_digit(chan, digit);
	if (!chan->pvt->send_digit || res) {
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
			ast_playtones_start(chan,0,dtmf_tones[digit-'0'], 0);
		else if (digit >= 'A' && digit <= 'D')
			ast_playtones_start(chan,0,dtmf_tones[digit-'A'+10], 0);
		else if (digit == '*')
			ast_playtones_start(chan,0,dtmf_tones[14], 0);
		else if (digit == '#')
			ast_playtones_start(chan,0,dtmf_tones[15], 0);
		else {
			/* not handled */
			ast_log(LOG_DEBUG, "Unable to handle DTMF tone '%c' for '%s'\n", digit, chan->name);
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
		a.subclass = chan->pvt->rawwriteformat;
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
	if (!chan->pvt->write_video)
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
		if (chan->pvt->send_text)
			res = chan->pvt->send_text(chan, (char *) fr->data);
		break;
	case AST_FRAME_VIDEO:
		/* XXX Handle translation of video codecs one day XXX */
		if (chan->pvt->write_video)
			res = chan->pvt->write_video(chan, fr);
		else
			res = 0;
		break;
	default:
		if (chan->pvt->write) {
			if (chan->pvt->writetrans) {
				f = ast_translate(chan->pvt->writetrans, fr, 0);
			} else
				f = fr;
			if (f) {
				res = chan->pvt->write(chan, f);
				if( chan->monitor &&
						chan->monitor->write_stream &&
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
			} else
				res = 0;
		}
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
		chan->fout++;
	}
	ast_mutex_unlock(&chan->lock);
	return res;
}

int ast_set_write_format(struct ast_channel *chan, int fmts)
{
	int fmt;
	int native;
	int res;
	
	ast_mutex_lock(&chan->lock);
	native = chan->nativeformats;
	fmt = fmts;
	
	res = ast_translator_best_choice(&native, &fmt);
	if (res < 0) {
		ast_log(LOG_NOTICE, "Unable to find a path from %s to %s\n",
			ast_getformatname(fmts), ast_getformatname(chan->nativeformats));
		ast_mutex_unlock(&chan->lock);
		return -1;
	}
	
	/* Now we have a good choice for both.  We'll write using our native format. */
	chan->pvt->rawwriteformat = native;
	/* User perspective is fmt */
	chan->writeformat = fmt;
	/* Free any write translation we have right now */
	if (chan->pvt->writetrans)
		ast_translator_free_path(chan->pvt->writetrans);
	/* Build a translation path from the user write format to the raw writing format */
	chan->pvt->writetrans = ast_translator_build_path(chan->pvt->rawwriteformat, chan->writeformat);
	if (option_debug)
		ast_log(LOG_DEBUG, "Set channel %s to write format %s\n", chan->name, ast_getformatname(chan->writeformat));
	ast_mutex_unlock(&chan->lock);
	return 0;
}

int ast_set_read_format(struct ast_channel *chan, int fmts)
{
	int fmt;
	int native;
	int res;
	
	ast_mutex_lock(&chan->lock);
	native = chan->nativeformats;
	fmt = fmts;
	/* Find a translation path from the native read format to one of the user's read formats */
	res = ast_translator_best_choice(&fmt, &native);
	if (res < 0) {
		ast_log(LOG_NOTICE, "Unable to find a path from %s to %s\n",
			ast_getformatname(chan->nativeformats), ast_getformatname(fmts));
		ast_mutex_unlock(&chan->lock);
		return -1;
	}
	
	/* Now we have a good choice for both.  We'll write using our native format. */
	chan->pvt->rawreadformat = native;
	/* User perspective is fmt */
	chan->readformat = fmt;
	/* Free any read translation we have right now */
	if (chan->pvt->readtrans)
		ast_translator_free_path(chan->pvt->readtrans);
	/* Build a translation path from the raw read format to the user reading format */
	chan->pvt->readtrans = ast_translator_build_path(chan->readformat, chan->pvt->rawreadformat);
	if (option_debug)
		ast_log(LOG_DEBUG, "Set channel %s to read format %s\n", 
			chan->name, ast_getformatname(chan->readformat));
	ast_mutex_unlock(&chan->lock);
	return 0;
}

struct ast_channel *__ast_request_and_dial(const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name, struct outgoing_helper *oh)
{
	int state = 0;
	int cause = 0;
	struct ast_channel *chan;
	struct ast_frame *f;
	int res = 0;
	char *variable;
	chan = ast_request(type, format, data, &cause);
	if (chan) {
		if (oh) {
			char *tmp, *var;
			/* JDG chanvar */
			if (oh->variable)
				variable = ast_strdupa(oh->variable);
			else
				variable = NULL;
			tmp = variable;
			/* FIXME replace this call with strsep  NOT*/
			while( (var = strtok_r(NULL, "|", &tmp)) ) {
				pbx_builtin_setvar( chan, var );
			} /* /JDG */
			ast_set_callerid(chan, oh->cid_num, oh->cid_name, oh->cid_num);
			if (oh->account && *oh->account)
				ast_cdr_setaccount(chan, oh->account);
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
				strncpy(chan->context, oh->context, sizeof(chan->context) - 1);
			if (oh->exten && *oh->exten)
				strncpy(chan->exten, oh->exten, sizeof(chan->exten) - 1);
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
	struct ast_channel *c = NULL;
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
	chan = backends;
	while(chan) {
		if (!strcasecmp(type, chan->type)) {
			capabilities = chan->capabilities;
			fmt = format;
			res = ast_translator_best_choice(&fmt, &capabilities);
			if (res < 0) {
				ast_log(LOG_WARNING, "No translator path exists for channel type %s (native %d) to %d\n", type, chan->capabilities, format);
				ast_mutex_unlock(&chlock);
				return NULL;
			}
			ast_mutex_unlock(&chlock);
			if (chan->requester)
				c = chan->requester(type, capabilities, data, cause);
			if (c) {
				if (c->_state == AST_STATE_DOWN) {
					manager_event(EVENT_FLAG_CALL, "Newchannel",
					"Channel: %s\r\n"
					"State: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n"
					"Uniqueid: %s\r\n",
					c->name, ast_state2str(c->_state), c->cid.cid_num ? c->cid.cid_num : "<unknown>", c->cid.cid_name ? c->cid.cid_name : "<unknown>",c->uniqueid);
				}
			}
			return c;
		}
		chan = chan->next;
	}
	if (!chan) {
		ast_log(LOG_WARNING, "No channel type registered for '%s'\n", type);
		*cause = AST_CAUSE_NOSUCHDRIVER;
	}
	ast_mutex_unlock(&chlock);
	return c;
}

int ast_parse_device_state(char *device)
{
	char name[AST_CHANNEL_NAME] = "";
	char *cut;
	struct ast_channel *chan;

	chan = ast_channel_walk_locked(NULL);
	while (chan) {
		strncpy(name, chan->name, sizeof(name)-1);
		ast_mutex_unlock(&chan->lock);
		cut = strchr(name,'-');
		if (cut)
		        *cut = 0;
		if (!strcmp(name, device))
		        return AST_DEVICE_INUSE;
		chan = ast_channel_walk_locked(chan);
	}
	return AST_DEVICE_UNKNOWN;
}

int ast_device_state(char *device)
{
	char tech[AST_MAX_EXTENSION] = "";
	char *number;
	struct chanlist *chanls;
	int res = 0;
	
	strncpy(tech, device, sizeof(tech)-1);
	number = strchr(tech, '/');
	if (!number) {
	    return AST_DEVICE_INVALID;
	}
	*number = 0;
	number++;
		
	if (ast_mutex_lock(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return -1;
	}
	chanls = backends;
	while(chanls) {
		if (!strcasecmp(tech, chanls->type)) {
			ast_mutex_unlock(&chlock);
			if (!chanls->devicestate) 
				return ast_parse_device_state(device);
			else {
				res = chanls->devicestate(number);
				if (res == AST_DEVICE_UNKNOWN)
					return ast_parse_device_state(device);
				else
					return res;
			}
		}
		chanls = chanls->next;
	}
	ast_mutex_unlock(&chlock);
	return AST_DEVICE_INVALID;
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
		if (chan->pvt->call)
			res = chan->pvt->call(chan, addr, timeout);
	ast_mutex_unlock(&chan->lock);
	return res;
}

int ast_transfer(struct ast_channel *chan, char *dest) 
{
	/* Place an outgoing call, but don't wait any longer than timeout ms before returning. 
	   If the remote end does not answer within the timeout, then do NOT hang up, but 
	   return anyway.  */
	int res = -1;
	/* Stop if we're a zombie or need a soft hangup */
	ast_mutex_lock(&chan->lock);
	if (!ast_test_flag(chan, AST_FLAG_ZOMBIE) && !ast_check_hangup(chan)) {
		if (chan->pvt->transfer) {
			res = chan->pvt->transfer(chan, dest);
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
	char d;
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
	char d;
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
	if (chan->pvt->send_html)
		return 1;
	return 0;
}

int ast_channel_sendhtml(struct ast_channel *chan, int subclass, char *data, int datalen)
{
	if (chan->pvt->send_html)
		return chan->pvt->send_html(chan, subclass, data, datalen);
	return -1;
}

int ast_channel_sendurl(struct ast_channel *chan, char *url)
{
	if (chan->pvt->send_html)
		return chan->pvt->send_html(chan, AST_HTML_URL, url, strlen(url) + 1);
	return -1;
}

int ast_channel_make_compatible(struct ast_channel *chan, struct ast_channel *peer)
{
	int peerf;
	int chanf;
	int res;
	ast_mutex_lock(&peer->lock);
	peerf = peer->nativeformats;
	ast_mutex_unlock(&peer->lock);
	ast_mutex_lock(&chan->lock);
	chanf = chan->nativeformats;
	ast_mutex_unlock(&chan->lock);
	res = ast_translator_best_choice(&peerf, &chanf);
	if (res < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", chan->name, chan->nativeformats, peer->name, peer->nativeformats);
		return -1;
	}
	/* Set read format on channel */
	res = ast_set_read_format(chan, peerf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %d\n", chan->name, chanf);
		return -1;
	}
	/* Set write format on peer channel */
	res = ast_set_write_format(peer, peerf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %d\n", peer->name, peerf);
		return -1;
	}
	/* Now we go the other way */
	peerf = peer->nativeformats;
	chanf = chan->nativeformats;
	res = ast_translator_best_choice(&chanf, &peerf);
	if (res < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", peer->name, peer->nativeformats, chan->name, chan->nativeformats);
		return -1;
	}
	/* Set writeformat on channel */
	res = ast_set_write_format(chan, chanf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %d\n", chan->name, chanf);
		return -1;
	}
	/* Set read format on peer channel */
	res = ast_set_read_format(peer, chanf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %d\n", peer->name, peerf);
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
	ast_log(LOG_DEBUG, "Planning to masquerade %s into the structure of %s\n",
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
		ast_log(LOG_DEBUG, "Done planning to masquerade %s into the structure of %s\n", original->name, clone->name);
		res = 0;
	}
	ast_mutex_unlock(&clone->lock);
	ast_mutex_unlock(&original->lock);
	return res;
}

void ast_change_name(struct ast_channel *chan, char *newname)
{
	char tmp[256];
	strncpy(tmp, chan->name, sizeof(tmp) - 1);
	strncpy(chan->name, newname, sizeof(chan->name) - 1);
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
				AST_LIST_INSERT_HEAD(&child->varshead, newvar, entries);
				if (option_debug)
					ast_log(LOG_DEBUG, "Copying soft-transferable variable %s.\n", ast_var_name(newvar));
			}
			break;
		case 2:
			newvar = ast_var_assign(ast_var_full_name(current), ast_var_value(current));
			if (newvar) {
				AST_LIST_INSERT_HEAD(&child->varshead, newvar, entries);
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
	AST_LIST_INSERT_TAIL(&original->varshead, AST_LIST_FIRST(&clone->varshead), entries);
}

/* Assumes channel will be locked when called */
int ast_do_masquerade(struct ast_channel *original)
{
	int x,i;
	int res=0;
	int origstate;
	struct ast_frame *cur, *prev;
	struct ast_channel_pvt *p;
	struct ast_callerid tmpcid;
	struct ast_channel *clone = original->masq;
	int rformat = original->readformat;
	int wformat = original->writeformat;
	char newn[100];
	char orig[100];
	char masqn[100];
	char zombn[100];

#if 1
	ast_log(LOG_DEBUG, "Actually Masquerading %s(%d) into the structure of %s(%d)\n",
		clone->name, clone->_state, original->name, original->_state);
#endif
	/* XXX This is a seriously wacked out operation.  We're essentially putting the guts of
	   the clone channel into the original channel.  Start by killing off the original
	   channel's backend.   I'm not sure we're going to keep this function, because 
	   while the features are nice, the cost is very high in terms of pure nastiness. XXX */

	/* We need the clone's lock, too */
	ast_mutex_lock(&clone->lock);

	ast_log(LOG_DEBUG, "Got clone lock on '%s' at %p\n", clone->name, &clone->lock);

	/* Having remembered the original read/write formats, we turn off any translation on either
	   one */
	free_translation(clone);
	free_translation(original);


	/* Unlink the masquerade */
	original->masq = NULL;
	clone->masqr = NULL;
	
	/* Save the original name */
	strncpy(orig, original->name, sizeof(orig) - 1);
	/* Save the new name */
	strncpy(newn, clone->name, sizeof(newn) - 1);
	/* Create the masq name */
	snprintf(masqn, sizeof(masqn), "%s<MASQ>", newn);
		
	/* Copy the name from the clone channel */
	strncpy(original->name, newn, sizeof(original->name)-1);

	/* Mangle the name of the clone channel */
	strncpy(clone->name, masqn, sizeof(clone->name) - 1);
	
	/* Notify any managers of the change, first the masq then the other */
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", newn, masqn, clone->uniqueid);
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", orig, newn, original->uniqueid);

	/* Swap the guts */	
	p = original->pvt;
	original->pvt = clone->pvt;
	clone->pvt = p;

	/* Save any pending frames on both sides.  Start by counting
	 * how many we're going to need... */
	prev = NULL;
	cur = clone->pvt->readq;
	x = 0;
	while(cur) {
		x++;
		prev = cur;
		cur = cur->next;
	}
	/* If we had any, prepend them to the ones already in the queue, and 
	 * load up the alertpipe */
	if (prev) {
		prev->next = original->pvt->readq;
		original->pvt->readq = clone->pvt->readq;
		clone->pvt->readq = NULL;
		if (original->pvt->alertpipe[1] > -1) {
			for (i=0;i<x;i++)
				write(original->pvt->alertpipe[1], &x, sizeof(x));
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

	if (clone->pvt->fixup){
		res = clone->pvt->fixup(original, clone);
		if (res) 
			ast_log(LOG_WARNING, "Fixup failed on channel %s, strange things may happen.\n", clone->name);
	}

	/* Start by disconnecting the original's physical side */
	if (clone->pvt->hangup)
		res = clone->pvt->hangup(clone);
	if (res) {
		ast_log(LOG_WARNING, "Hangup failed!  Strange things may happen!\n");
		ast_mutex_unlock(&clone->lock);
		return -1;
	}
	
	snprintf(zombn, sizeof(zombn), "%s<ZOMBIE>", orig);
	/* Mangle the name of the clone channel */
	strncpy(clone->name, zombn, sizeof(clone->name) - 1);
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\nUniqueid: %s\r\n", masqn, zombn, clone->uniqueid);

	/* Keep the same language.  */
	/* Update the type. */
	original->type = clone->type;
	/* Copy the FD's */
	for (x=0;x<AST_MAX_FDS;x++) {
		original->fds[x] = clone->fds[x];
	}
	clone_variables(original, clone);
	clone->varshead.first = NULL;
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
	strncpy(original->musicclass, clone->musicclass, sizeof(original->musicclass) - 1);

	ast_log(LOG_DEBUG, "Putting channel %s in %d/%d formats\n", original->name, wformat, rformat);

	/* Okay.  Last thing is to let the channel driver know about all this mess, so he
	   can fix up everything as best as possible */
	if (original->pvt->fixup) {
		res = original->pvt->fixup(clone, original);
		if (res) {
			ast_log(LOG_WARNING, "Driver for '%s' could not fixup channel %s\n",
				original->type, original->name);
			ast_mutex_unlock(&clone->lock);
			return -1;
		}
	} else
		ast_log(LOG_WARNING, "Driver '%s' does not have a fixup routine (for %s)!  Bad things may happen.\n",
			original->type, original->name);
	
	/* Now, at this point, the "clone" channel is totally F'd up.  We mark it as
	   a zombie so nothing tries to touch it.  If it's already been marked as a
	   zombie, then free it now (since it already is considered invalid). */
	if (ast_test_flag(clone, AST_FLAG_ZOMBIE)) {
		ast_log(LOG_DEBUG, "Destroying clone '%s'\n", clone->name);
		ast_mutex_unlock(&clone->lock);
		ast_channel_free(clone);
		manager_event(EVENT_FLAG_CALL, "Hangup", "Channel: %s\r\n", zombn);
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
	ast_log(LOG_DEBUG, "Done Masquerading %s (%d)\n",
		original->name, original->_state);
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
				"Uniqueid: %s\r\n",
				chan->name, chan->cid.cid_num ? 
				chan->cid.cid_num : "<Unknown>",
				chan->cid.cid_name ? 
				chan->cid.cid_name : "<Unknown>",
				chan->uniqueid);
}

int ast_setstate(struct ast_channel *chan, int state)
{
	if (chan->_state != state) {
		int oldstate = chan->_state;
		chan->_state = state;
		if (oldstate == AST_STATE_DOWN) {
			ast_device_state_changed(chan->name);
			manager_event(EVENT_FLAG_CALL, "Newchannel",
			"Channel: %s\r\n"
			"State: %s\r\n"
			"CallerID: %s\r\n"
			"CallerIDName: %s\r\n"
			"Uniqueid: %s\r\n",
			chan->name, ast_state2str(chan->_state), 
			chan->cid.cid_num ? chan->cid.cid_num : "<unknown>", 
			chan->cid.cid_name ? chan->cid.cid_name : "<unknown>", 
			chan->uniqueid);
		} else {
			manager_event(EVENT_FLAG_CALL, "Newstate", 
				"Channel: %s\r\n"
				"State: %s\r\n"
				"CallerID: %s\r\n"
				"CallerIDName: %s\r\n"
				"Uniqueid: %s\r\n",
				chan->name, ast_state2str(chan->_state), 
				chan->cid.cid_num ? chan->cid.cid_num : "<unknown>", 
				chan->cid.cid_name ? chan->cid.cid_name : "<unknown>", 
				chan->uniqueid);
		}
	}
	return 0;
}

static long tvdiff(struct timeval *now, struct timeval *then) 
{
#if 0
	return (((now->tv_sec * 1000) + now->tv_usec / 1000) - ((then->tv_sec * 1000) + then->tv_usec / 1000));
#else
	return (now->tv_sec - then->tv_sec) * 1000 + (now->tv_usec - then->tv_usec) / 1000;	
#endif
}

struct ast_channel *ast_bridged_channel(struct ast_channel *chan)
{
	struct ast_channel *bridged;
	bridged = chan->_bridge;
	if (bridged && bridged->pvt->bridged_channel) 
		bridged = bridged->pvt->bridged_channel(chan, bridged);
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
	
	if (!strcmp(sound,"timeleft")) {
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

int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, struct ast_bridge_config *config, struct ast_frame **fo, struct ast_channel **rc) 
{
	/* Copy voice back and forth between the two channels.	Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_channel *cs[3];
	int to = -1;
	struct ast_frame *f;
	struct ast_channel *who = NULL;
	int res=0;
	int nativefailed=0;
	int firstpass;
	int o0nativeformats;
	int o1nativeformats;
	struct timeval start_time,precise_now;
	long elapsed_ms=0, time_left_ms=0;
	int playit=0, playitagain=1, first_time=1;

	*fo = NULL;
	firstpass = config->firstpass;
	config->firstpass = 0;

	/* timestamp */
	gettimeofday(&start_time,NULL);
	time_left_ms = config->timelimit;

	if ((ast_test_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING)) && config->start_sound && firstpass)
		bridge_playfile(c0,c1,config->start_sound,time_left_ms / 1000);
	if ((ast_test_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING)) && config->start_sound && firstpass)
		bridge_playfile(c1,c0,config->start_sound,time_left_ms / 1000);

	/* Stop if we're a zombie or need a soft hangup */
	if (ast_test_flag(c0, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c0) || ast_test_flag(c1, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c1)) 
		return -1;
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
	
	/* Keep track of bridge */
	c0->_bridge = c1;
	c1->_bridge = c0;
	cs[0] = c0;
	cs[1] = c1;
	
	manager_event(EVENT_FLAG_CALL, "Link", 
			"Channel1: %s\r\n"
			"Channel2: %s\r\n"
			"Uniqueid1: %s\r\n"
			"Uniqueid2: %s\r\n",
			c0->name, c1->name, c0->uniqueid, c1->uniqueid);
	o1nativeformats = c1->nativeformats;
	o0nativeformats = c0->nativeformats;
	for (/* ever */;;) {
		/* timestamp */
		if (config->timelimit) {
			gettimeofday(&precise_now,NULL);
			elapsed_ms = tvdiff(&precise_now,&start_time);
			time_left_ms = config->timelimit - elapsed_ms;

			if (playitagain && ((ast_test_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING)) || (ast_test_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING))) && (config->play_warning && time_left_ms <= config->play_warning)) { 
				/* narrowing down to the end */
				if (config->warning_freq == 0) {
					playit = 1;
					first_time=0;
					playitagain=0;
				} else if (first_time) {
					playit = 1;
					first_time=0;
				} else {
					if ((time_left_ms % config->warning_freq) <= 50) {
						playit = 1;
					}
				}
			}
			if (time_left_ms <= 0) {
				if ((ast_test_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING)) && config->end_sound)
					bridge_playfile(c0,c1,config->end_sound,0);
				if ((ast_test_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING)) && config->end_sound)
					bridge_playfile(c1,c0,config->end_sound,0);
				*fo = NULL;
				if (who) *rc = who;
				res = 0;
				break;
			}
			if (time_left_ms >= 5000 && playit) {
				if ((ast_test_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING)) && config->warning_sound && config->play_warning)
					bridge_playfile(c0,c1,config->warning_sound,time_left_ms / 1000);
				if ((ast_test_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING)) && config->warning_sound && config->play_warning)
					bridge_playfile(c1,c0,config->warning_sound,time_left_ms / 1000);
				playit = 0;
			}
			
		}
		/* Stop if we're a zombie or need a soft hangup */
		if (ast_test_flag(c0, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c0) || ast_test_flag(c1, AST_FLAG_ZOMBIE) || ast_check_hangup_locked(c1)) {
			*fo = NULL;
			if (who) *rc = who;
			res = 0;
			ast_log(LOG_DEBUG, "Bridge stops because we're zombie or need a soft hangup: c0=%s, c1=%s, flags: %s,%s,%s,%s\n",c0->name,c1->name,ast_test_flag(c0, AST_FLAG_ZOMBIE)?"Yes":"No",ast_check_hangup(c0)?"Yes":"No",ast_test_flag(c1, AST_FLAG_ZOMBIE)?"Yes":"No",ast_check_hangup(c1)?"Yes":"No");
			break;
		}
		if (c0->pvt->bridge && config->timelimit==0 &&
			(c0->pvt->bridge == c1->pvt->bridge) && !nativefailed && !c0->monitor && !c1->monitor) {
				/* Looks like they share a bridge code */
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "Attempting native bridge of %s and %s\n", c0->name, c1->name);
			if (!(res = c0->pvt->bridge(c0, c1, config->flags, fo, rc))) {
				c0->_bridge = NULL;
				c1->_bridge = NULL;
				manager_event(EVENT_FLAG_CALL, "Unlink", 
					"Channel1: %s\r\n"
					"Channel2: %s\r\n"
					"Uniqueid1: %s\r\n"
					"Uniqueid2: %s\r\n",
					c0->name, c1->name, c0->uniqueid, c1->uniqueid);
				ast_log(LOG_DEBUG, "Returning from native bridge, channels: %s, %s\n",c0->name ,c1->name);
				return 0;
			}
			/* If they return non-zero then continue on normally.  Let "-2" mean don't worry about
			   my not wanting to bridge */
			if ((res != -2) && (res != -3))
				ast_log(LOG_WARNING, "Private bridge between %s and %s failed\n", c0->name, c1->name);
			if (res != -3) nativefailed++;
		}
	
		if (((c0->writeformat != c1->readformat) || (c0->readformat != c1->writeformat) || (c0->nativeformats != o0nativeformats) || (c1->nativeformats != o1nativeformats)) &&
			!(c0->generator || c1->generator))  {
			if (ast_channel_make_compatible(c0, c1)) {
				ast_log(LOG_WARNING, "Can't make %s and %s compatible\n", c0->name, c1->name);
				manager_event(EVENT_FLAG_CALL, "Unlink", 
					"Channel1: %s\r\n"
					"Channel2: %s\r\n"
					"Uniqueid1: %s\r\n"
					"Uniqueid2: %s\r\n",
					c0->name, c1->name, c0->uniqueid, c1->uniqueid);
				return -1;
			}
			o0nativeformats = c0->nativeformats;
			o1nativeformats = c1->nativeformats;
		}
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_DEBUG, "Nobody there, continuing...\n"); 
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			res = 0;
			ast_log(LOG_DEBUG, "Didn't get a frame from channel: %s\n",who->name);
			break;
		}

		if ((f->frametype == AST_FRAME_CONTROL) && !(config->flags & AST_BRIDGE_IGNORE_SIGS)) {
			*fo = f;
			*rc = who;
			res =  0;
			ast_log(LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass, who->name);
			break;
		}
		if ((f->frametype == AST_FRAME_VOICE) ||
			(f->frametype == AST_FRAME_TEXT) ||
			(f->frametype == AST_FRAME_VIDEO) || 
			(f->frametype == AST_FRAME_IMAGE) ||
			(f->frametype == AST_FRAME_DTMF)) {
			if ((f->frametype == AST_FRAME_DTMF) && 
				(config->flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))) {
				if ((who == c0)) {
					if  ((config->flags & AST_BRIDGE_DTMF_CHANNEL_0)) {
						*rc = c0;
						*fo = f;
						/* Take out of conference mode */
						res = 0;
						ast_log(LOG_DEBUG, "Got AST_BRIDGE_DTMF_CHANNEL_0 on c0 (%s)\n",c0->name);
						break;
					} else 
						goto tackygoto;
				} else
				if ((who == c1)) {
					if (config->flags & AST_BRIDGE_DTMF_CHANNEL_1) {
						*rc = c1;
						*fo = f;
						res =  0;
						ast_log(LOG_DEBUG, "Got AST_BRIDGE_DTMF_CHANNEL_1 on c1 (%s)\n",c1->name);
						break;
					} else
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
				/* Don't copy packets if there is a generator on either one, since they're
				   not supposed to be listening anyway */
				if (who == c0) 
					ast_write(c1, f);
				else 
					ast_write(c0, f);
			}
			ast_frfree(f);
		} else
			ast_frfree(f);
		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
	c0->_bridge = NULL;
	c1->_bridge = NULL;
	manager_event(EVENT_FLAG_CALL, "Unlink", 
					"Channel1: %s\r\n"
					"Channel2: %s\r\n"
					"Uniqueid1: %s\r\n"
					"Uniqueid2: %s\r\n",
					c0->name, c1->name, c0->uniqueid, c1->uniqueid);
	ast_log(LOG_DEBUG, "Bridge stops bridging channels %s and %s\n",c0->name,c1->name);
	return res;
}

int ast_channel_setoption(struct ast_channel *chan, int option, void *data, int datalen, int block)
{
	int res;
	if (chan->pvt->setoption) {
		res = chan->pvt->setoption(chan, option, data, datalen);
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

static void * tonepair_alloc(struct ast_channel *chan, void *params)
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
	for (x=0;x<len/2;x++) {
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

unsigned int ast_get_group(char *s)
{
	char *copy;
	char *piece;
	char *c=NULL;
	int start=0, finish=0,x;
	unsigned int group = 0;
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
			ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'.  Using '0'\n", s,piece);
			return 0;
		}
		for (x=start;x<=finish;x++) {
			if ((x > 31) || (x < 0)) {
				ast_log(LOG_WARNING, "Ignoring invalid group %d (maximum group is 31)\n", x);
			} else
				group |= (1 << x);
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

/*! Turn on/off music on hold on a given channel */

int ast_moh_start(struct ast_channel *chan, char *mclass) 
{
	if(ast_moh_start_ptr)
		return ast_moh_start_ptr(chan, mclass);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Music class %s requested but no musiconhold loaded.\n", mclass ? mclass : "default");
	
	return 0;
}

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
