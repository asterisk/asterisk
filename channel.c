/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Management
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <asterisk/sched.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/file.h>
#include <asterisk/translate.h>



#ifdef DEBUG_MUTEX
/* Convenient mutex debugging functions */
#define PTHREAD_MUTEX_LOCK(a) __PTHREAD_MUTEX_LOCK(__FUNCTION__, a)
#define PTHREAD_MUTEX_UNLOCK(a) __PTHREAD_MUTEX_UNLOCK(__FUNCTION__, a)

static int __PTHREAD_MUTEX_LOCK(char *f, pthread_mutex_t *a) {
	ast_log(LOG_DEBUG, "Locking %p (%s)\n", a, f); 
	return pthread_mutex_lock(a);
}

static int __PTHREAD_MUTEX_UNLOCK(char *f, pthread_mutex_t *a) {
	ast_log(LOG_DEBUG, "Unlocking %p (%s)\n", a, f); 
	return pthread_mutex_unlock(a);
}
#else
#define PTHREAD_MUTEX_LOCK(a) pthread_mutex_lock(a)
#define PTHREAD_MUTEX_UNLOCK(a) pthread_mutex_unlock(a)
#endif

struct chanlist {
	char type[80];
	char description[80];
	int capabilities;
	struct ast_channel * (*requester)(char *type, int format, void *data);
	struct chanlist *next;
} *backends = NULL;
struct ast_channel *channels = NULL;

/* Protect the channel list (highly unlikely that two things would change
   it at the same time, but still! */
   
static pthread_mutex_t chlock = PTHREAD_MUTEX_INITIALIZER;

int ast_channel_register(char *type, char *description, int capabilities,
		struct ast_channel *(*requester)(char *type, int format, void *data))
{
	struct chanlist *chan, *last=NULL;
	if (PTHREAD_MUTEX_LOCK(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return -1;
	}
	chan = backends;
	while(chan) {
		if (!strcasecmp(type, chan->type)) {
			ast_log(LOG_WARNING, "Already have a handler for type '%s'\n", type);
			PTHREAD_MUTEX_UNLOCK(&chlock);
			return -1;
		}
		last = chan;
		chan = chan->next;
	}
	chan = malloc(sizeof(struct chanlist));
	if (!chan) {
		ast_log(LOG_WARNING, "Out of memory\n");
		PTHREAD_MUTEX_UNLOCK(&chlock);
		return -1;
	}
	strncpy(chan->type, type, sizeof(chan->type));
	strncpy(chan->description, description, sizeof(chan->description));
	chan->capabilities = capabilities;
	chan->requester = requester;
	chan->next = NULL;
	if (last)
		last->next = chan;
	else
		backends = chan;
	if (option_debug)
		ast_log(LOG_DEBUG, "Registered handler for '%s' (%s)\n", chan->type, chan->description);
	else if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered channel type '%s' (%s)\n", chan->type, chan->description);
	PTHREAD_MUTEX_UNLOCK(&chlock);
	return 0;
}

struct ast_channel *ast_channel_alloc(void)
{
	struct ast_channel *tmp;
	struct ast_channel_pvt *pvt;
	PTHREAD_MUTEX_LOCK(&chlock);
	tmp = malloc(sizeof(struct ast_channel));
	memset(tmp, 0, sizeof(struct ast_channel));
	if (tmp) {
		pvt = malloc(sizeof(struct ast_channel_pvt));
		if (pvt) {
			memset(pvt, 0, sizeof(struct ast_channel_pvt));
			tmp->sched = sched_context_create();
			if (tmp->sched) {
				tmp->fd = -1;
				strncpy(tmp->name, "**Unknown**", sizeof(tmp->name));
				tmp->pvt = pvt;
				tmp->state = AST_STATE_DOWN;
				tmp->stack = -1;
				tmp->streamid = -1;
				tmp->appl = NULL;
				tmp->data = NULL;
				pthread_mutex_init(&tmp->lock, NULL);
				strncpy(tmp->context, "default", sizeof(tmp->context));
				strncpy(tmp->language, defaultlanguage, sizeof(tmp->language));
				strncpy(tmp->exten, "s", sizeof(tmp->exten));
				tmp->priority=1;
				tmp->next = channels;
				channels= tmp;
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
	PTHREAD_MUTEX_UNLOCK(&chlock);
	return tmp;
}

struct ast_channel *ast_channel_walk(struct ast_channel *prev)
{
	struct ast_channel *l, *ret=NULL;
	PTHREAD_MUTEX_LOCK(&chlock);
	l = channels;
	if (!prev) {
		PTHREAD_MUTEX_UNLOCK(&chlock);
		return l;
	}
	while(l) {
		if (l == prev)
			ret = l->next;
		l = l->next;
	}
	PTHREAD_MUTEX_UNLOCK(&chlock);
	return ret;
	
}

void ast_channel_free(struct ast_channel *chan)
{
	struct ast_channel *last=NULL, *cur;
	PTHREAD_MUTEX_LOCK(&chlock);
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
	if (chan->pvt->pvt)
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n", chan->name);
	/* Free translatosr */
	if (chan->pvt->readtrans)
		ast_translator_free_path(chan->pvt->readtrans);
	if (chan->pvt->writetrans)
		ast_translator_free_path(chan->pvt->writetrans);
	if (chan->pbx) 
		ast_log(LOG_WARNING, "PBX may not have been terminated properly on '%s'\n", chan->name);
	if (chan->dnid)
		free(chan->dnid);
	if (chan->callerid)
		free(chan->callerid);	
	pthread_mutex_destroy(&chan->lock);
	free(chan->pvt);
	free(chan);
	PTHREAD_MUTEX_UNLOCK(&chlock);
}

int ast_softhangup(struct ast_channel *chan)
{
	int res = 0;
	if (chan->stream)
		ast_stopstream(chan);
	if (option_debug)
		ast_log(LOG_DEBUG, "Soft-Hanging up channel '%s'\n", chan->name);
	if (chan->pvt->hangup)
		res = chan->pvt->hangup(chan);
	if (chan->pvt->pvt)
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n", chan->name);
	if (chan->pbx) 
		ast_log(LOG_WARNING, "PBX may not have been terminated properly on '%s'\n", chan->name);	
	/* Interrupt any select call or such */
	if (chan->blocking)
		pthread_kill(chan->blocker, SIGURG);
	return res;
}

int ast_hangup(struct ast_channel *chan)
{
	int res = 0;
	if (chan->stream)
		ast_stopstream(chan);
	if (chan->sched)
		sched_context_destroy(chan->sched);
	if (chan->blocking) {
		ast_log(LOG_WARNING, "Hard hangup called by thread %ld on %s, while fd "
					"is blocked by thread %ld in procedure %s!  Expect a failure\n",
					pthread_self(), chan->name, chan->blocker, chan->blockproc);
		CRASH;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Hanging up channel '%s'\n", chan->name);
	if (chan->pvt->hangup)
		res = chan->pvt->hangup(chan);
	ast_channel_free(chan);
	return res;
}

void ast_channel_unregister(char *type)
{
	struct chanlist *chan, *last=NULL;
	if (option_debug)
		ast_log(LOG_DEBUG, "Unregistering channel type '%s'\n", type);
	if (PTHREAD_MUTEX_LOCK(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return;
	}
	chan = backends;
	while(chan) {
		if (!strcasecmp(chan->type, type)) {
			if (last)
				last->next = chan->next;
			else
				backends = backends->next;
			free(chan);
			PTHREAD_MUTEX_UNLOCK(&chlock);
			return;
		}
		last = chan;
		chan = chan->next;
	}
	PTHREAD_MUTEX_UNLOCK(&chlock);
}

int ast_answer(struct ast_channel *chan)
{
	/* Answer the line, if possible */
	if (chan->state == AST_STATE_RING) {
		if (chan->pvt->answer)
			return chan->pvt->answer(chan);
	}
	return 0;
}

int ast_waitfor_n_fd(int *fds, int n, int *ms, int *exception)
{
	/* Wait for x amount of time on a file descriptor to have input.  */
	struct timeval tv;
	fd_set rfds, efds;
	int res;
	int x, max=-1;
	int winner = -1;
	
	tv.tv_sec = *ms / 1000;
	tv.tv_usec = (*ms % 1000) * 1000;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);
	for (x=0;x<n;x++) {
		FD_SET(fds[x], &rfds);
		FD_SET(fds[x], &efds);
		if (fds[x] > max)
			max = fds[x];
	}
	if (*ms >= 0) 
		res = select(max + 1, &rfds, NULL, &efds, &tv);
	else
		res = select(max + 1, &rfds, NULL, &efds, NULL);
	for (x=0;x<n;x++) {
		if ((FD_ISSET(fds[x], &rfds) || FD_ISSET(fds[x], &efds)) && (winner < 0)) {
			if (exception)
				*exception = FD_ISSET(fds[x], &efds);
			winner = fds[x];
		}
	}
	*ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (res < 0)
		*ms = -10;
	return winner;
}

struct ast_channel *ast_waitfor_n(struct ast_channel **c, int n, int *ms)
{
	/* Wait for x amount of time on a file descriptor to have input.  */
	struct timeval tv;
	fd_set rfds, efds;
	int res;
	int x, max=-1;
	struct ast_channel *winner = NULL;
	
	tv.tv_sec = *ms / 1000;
	tv.tv_usec = (*ms % 1000) * 1000;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);
	for (x=0;x<n;x++) {
		FD_SET(c[x]->fd, &rfds);
		FD_SET(c[x]->fd, &efds);
		CHECK_BLOCKING(c[x]);
		if (c[x]->fd > max)
			max = c[x]->fd;
	}
	if (*ms >= 0) 
		res = select(max + 1, &rfds, NULL, &efds, &tv);
	else
		res = select(max + 1, &rfds, NULL, &efds, NULL);
	for (x=0;x<n;x++) {
		c[x]->blocking = 0;
		if ((FD_ISSET(c[x]->fd, &rfds) || FD_ISSET(c[x]->fd, &efds)) && !winner) {
			/* Set exception flag if appropriate */
			if (FD_ISSET(c[x]->fd, &efds))
				c[x]->exception = 1;
			winner = c[x];
		}
	}
	*ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (res < 0)
		*ms = -10;
	return winner;
}

int ast_waitfor(struct ast_channel *c, int ms)
{
	if (ast_waitfor_n(&c, 1, &ms)) {
		if (ms < 0)
			return -ms;
		return ms;
	}
	/* Error if ms < 0 */
	if (ms < 0) 
		return -1;
	return 0;
}

char ast_waitfordigit(struct ast_channel *c, int ms)
{
	struct ast_frame *f;
	char result = 0;
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

struct ast_frame *ast_read(struct ast_channel *chan)
{
	struct ast_frame *f = NULL;
	static struct ast_frame null_frame = 
	{
		AST_FRAME_NULL,
	};
	chan->blocker = pthread_self();
	if (chan->exception) {
		if (chan->pvt->exception) 
			f = chan->pvt->exception(chan);
		else
			ast_log(LOG_WARNING, "Exception flag set, but no exception handler\n");
		/* Clear the exception flag */
		chan->exception = 0;
	} else
	if (chan->pvt->read)
		f = chan->pvt->read(chan);
	else
		ast_log(LOG_WARNING, "No read routine on channel %s\n", chan);
	if (f && (f->frametype == AST_FRAME_VOICE)) {
		if (chan->pvt->readtrans) {
			f = ast_translate(chan->pvt->readtrans, f, 1);
			if (!f)
				f = &null_frame;
		}
	}
	return f;
}

int ast_sendtext(struct ast_channel *chan, char *text)
{
	int res = 0;
	CHECK_BLOCKING(chan);
	if (chan->pvt->send_text)
		res = chan->pvt->send_text(chan, text);
	chan->blocking = 0;
	return res;
}

int ast_write(struct ast_channel *chan, struct ast_frame *fr)
{
	int res = -1;
	struct ast_frame *f;
	CHECK_BLOCKING(chan);
	switch(fr->frametype) {
	case AST_FRAME_CONTROL:
		/* XXX Interpret control frames XXX */
		ast_log(LOG_WARNING, "Don't know how to handle control frames yet\n");
		break;
	case AST_FRAME_DTMF:
		
		if (chan->pvt->send_digit)
			res = chan->pvt->send_digit(chan, fr->subclass);
		break;
	default:
		if (chan->pvt->write) {
			if (chan->pvt->writetrans) {
				f = ast_translate(chan->pvt->writetrans, fr, 1);
			} else
				f = fr;
			if (f)	
				res = chan->pvt->write(chan, f);
		}
	}
	chan->blocking = 0;
	return res;
}

int ast_set_write_format(struct ast_channel *chan, int fmts)
{
	int fmt;
	int native;
	int res;
	
	native = chan->nativeformats;
	fmt = fmts;
	
	res = ast_translator_best_choice(&native, &fmt);
	if (res < 0) {
		ast_log(LOG_NOTICE, "Unable to find a path from %d to %d\n", fmts, chan->nativeformats);
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
	ast_log(LOG_DEBUG, "Set channel %s to format %d\n", chan->name, chan->writeformat);
	return 0;
}

int ast_set_read_format(struct ast_channel *chan, int fmts)
{
	int fmt;
	int native;
	int res;
	
	native = chan->nativeformats;
	fmt = fmts;
	/* Find a translation path from the native read format to one of the user's read formats */
	res = ast_translator_best_choice(&fmt, &native);
	if (res < 0) {
		ast_log(LOG_NOTICE, "Unable to find a path from %d to %d\n", chan->nativeformats, fmts);
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
	return 0;
}

struct ast_channel *ast_request(char *type, int format, void *data)
{
	struct chanlist *chan;
	struct ast_channel *c = NULL;
	int capabilities;
	int fmt;
	int res;
	if (PTHREAD_MUTEX_LOCK(&chlock)) {
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
				PTHREAD_MUTEX_UNLOCK(&chlock);
				return NULL;
			}
			PTHREAD_MUTEX_UNLOCK(&chlock);
			if (chan->requester)
				c = chan->requester(type, capabilities, data);
			return c;
		}
		chan = chan->next;
	}
	if (!chan)
		ast_log(LOG_WARNING, "No channel type registered for '%s'\n", type);
	PTHREAD_MUTEX_UNLOCK(&chlock);
	return c;
}

int ast_call(struct ast_channel *chan, char *addr, int timeout) 
{
	/* Place an outgoing call, but don't wait any longer than timeout ms before returning. 
	   If the remote end does not answer within the timeout, then do NOT hang up, but 
	   return anyway.  */
	int res = -1;
	if (chan->pvt->call)
		res = chan->pvt->call(chan, addr, timeout);
	return res;
}

int ast_readstring(struct ast_channel *c, char *s, int len, int timeout, int ftimeout, char *enders)
{
	int pos=0;
	int to = ftimeout;
	char d;
	if (!len)
		return -1;
	do {
		if (c->streamid > -1) {
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
		if (!strchr(enders, d))
			s[pos++] = d;
		if ((d == 0) || strchr(enders, d) || (pos >= len - 1)) {
			s[pos]='\0';
			return 0;
		}
		to = timeout;
	} while(1);
	/* Never reached */
	return 0;
}

int ast_channel_make_compatible(struct ast_channel *chan, struct ast_channel *peer)
{
	int peerf;
	int chanf;
	int res;
	peerf = peer->nativeformats;
	chanf = chan->nativeformats;
	res = ast_translator_best_choice(&peerf, &chanf);
	if (res < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", chan, chan->nativeformats, peer, peer->nativeformats);
		return -1;
	}
	/* Set read format on channel */
	res = ast_set_read_format(chan, peerf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %d\n", chan, chanf);
		return -1;
	}
	/* Set write format on peer channel */
	res = ast_set_write_format(peer, peerf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %d\n", peer, peerf);
		return -1;
	}
	/* Now we go the other way */
	peerf = peer->nativeformats;
	chanf = chan->nativeformats;
	res = ast_translator_best_choice(&chanf, &peerf);
	if (res < 0) {
		ast_log(LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", peer, peer->nativeformats, chan, chan->nativeformats);
		return -1;
	}
	/* Set writeformat on channel */
	res = ast_set_write_format(chan, chanf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format on channel %s to %d\n", chan, chanf);
		return -1;
	}
	/* Set read format on peer channel */
	res = ast_set_read_format(peer, chanf);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set read format on channel %s to %d\n", peer, peerf);
		return -1;
	}
	return 0;
}



int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_channel *cs[3];
	int to = -1;
	struct ast_frame *f;
	struct ast_channel *who;
	if (c0->pvt->bridge && 
		(c0->pvt->bridge == c1->pvt->bridge)) {
			/* Looks like they share a bridge code */
		if (!c0->pvt->bridge(c0, c1, flags, fo, rc))
			return 0;
		ast_log(LOG_WARNING, "Private bridge between %s and %s failed\n", c0->name, c1->name);
		/* If they return non-zero then continue on normally */
	}
	
	
	
	cs[0] = c0;
	cs[1] = c1;
	for (/* ever */;;) {
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_WARNING, "Nobody there??\n");
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			return 0;
		}
		if ((f->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			*fo = f;
			*rc = who;
			return 0;
		}
		if ((f->frametype == AST_FRAME_VOICE) ||
			(f->frametype == AST_FRAME_TEXT) ||
			(f->frametype == AST_FRAME_VIDEO) || 
			(f->frametype == AST_FRAME_IMAGE) ||
			(f->frametype == AST_FRAME_DTMF)) {
			if ((f->frametype == AST_FRAME_DTMF) && (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))) {
				if ((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) {
					*rc = c0;
					*fo = f;
					/* Take out of conference mode */
					return 0;
				} else
				if ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1)) {
					*rc = c1;
					*fo = f;
					return 0;
				}
			} else {
#if 0
				ast_log(LOG_DEBUG, "Read from %s\n", who->name);
				if (who == last) 
					ast_log(LOG_DEBUG, "Servicing channel %s twice in a row?\n", last->name);
				last = who;
#endif
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
	return 0;
}
