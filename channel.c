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


/* XXX Lock appropriately in more functions XXX */

#ifdef DEBUG_MUTEX
/* Convenient mutex debugging functions */
#define PTHREAD_MUTEX_LOCK(a) __PTHREAD_MUTEX_LOCK(__FUNCTION__, a)
#define PTHREAD_MUTEX_UNLOCK(a) __PTHREAD_MUTEX_UNLOCK(__FUNCTION__, a)

static int __PTHREAD_MUTEX_LOCK(char *f, pthread_mutex_t *a) {
	ast_log(LOG_DEBUG, "Locking %p (%s)\n", a, f); 
	return ast_pthread_mutex_lock(a);
}

static int __PTHREAD_MUTEX_UNLOCK(char *f, pthread_mutex_t *a) {
	ast_log(LOG_DEBUG, "Unlocking %p (%s)\n", a, f); 
	return ast_pthread_mutex_unlock(a);
}
#else
#define PTHREAD_MUTEX_LOCK(a) ast_pthread_mutex_lock(a)
#define PTHREAD_MUTEX_UNLOCK(a) ast_pthread_mutex_unlock(a)
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

char *ast_state2str(int state)
{
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
		return "Unknown";
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
		/* ADPCM has great sound quality and is still pretty easy to translate */
		AST_FORMAT_ADPCM,
		/* Okay, we're down to vocoders now, so pick GSM because it's small and easier to
		   translate and sounds pretty good */
		AST_FORMAT_GSM,
		/* Ick, LPC10 sounds terrible, but at least we have code for it, if you're tacky enough
		   to use it */
		AST_FORMAT_LPC10,
		/* Down to G.723.1 which is proprietary but at least designed for voice */
		AST_FORMAT_G723_1,
		/* Last and least, MP3 which was of course never designed for real-time voice */
		AST_FORMAT_MP3,
	};
	
	
	for (x=0;x<sizeof(prefs) / sizeof(prefs[0]); x++)
		if (fmts & prefs[x])
			return prefs[x];
	ast_log(LOG_WARNING, "Don't know any of 0x%x formats\n", fmts);
	return 0;
}

struct ast_channel *ast_channel_alloc(void)
{
	struct ast_channel *tmp;
	struct ast_channel_pvt *pvt;
	int x;
	PTHREAD_MUTEX_LOCK(&chlock);
	tmp = malloc(sizeof(struct ast_channel));
	memset(tmp, 0, sizeof(struct ast_channel));
	if (tmp) {
		pvt = malloc(sizeof(struct ast_channel_pvt));
		if (pvt) {
			memset(pvt, 0, sizeof(struct ast_channel_pvt));
			tmp->sched = sched_context_create();
			if (tmp->sched) {
				for (x=0;x<AST_MAX_FDS;x++)
					tmp->fds[x] = -1;
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

int ast_channel_defer_dtmf(struct ast_channel *chan)
{
	int pre = 0;
	if (chan) {
		pre = chan->deferdtmf;
		chan->deferdtmf = 1;
	}
	return pre;
}

void ast_channel_undefer_dtmf(struct ast_channel *chan)
{
	if (chan)
		chan->deferdtmf = 0;
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
	if (option_debug)
		ast_log(LOG_DEBUG, "Soft-Hanging up channel '%s'\n", chan->name);
	/* Inform channel driver that we need to be hung up, if it cares */
	chan->softhangup = 1;		
	/* Interrupt any select call or such */
	if (chan->blocking)
		pthread_kill(chan->blocker, SIGURG);
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
	ast_pthread_mutex_lock(&chan->lock);
	if (chan->masq) {
		ast_log(LOG_WARNING, "We're getting hung up, but someone is trying to masq into us?!?\n");
		ast_pthread_mutex_unlock(&chan->lock);
		return 0;
	}
	/* If this channel is one which will be masqueraded into something, 
	   mark it as a zombie already, so we know to free it later */
	if (chan->masqr) {
		ast_pthread_mutex_unlock(&chan->lock);
		chan->zombie=1;
		return 0;
	}
	free_translation(chan);
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
	if (!chan->zombie) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Hanging up channel '%s'\n", chan->name);
		if (chan->pvt->hangup)
			res = chan->pvt->hangup(chan);
	} else
		if (option_debug)
			ast_log(LOG_DEBUG, "Hanging up zombie '%s'\n", chan->name);
			
	ast_pthread_mutex_unlock(&chan->lock);
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
	int res = 0;
	/* Stop if we're a zombie or need a soft hangup */
	if (chan->zombie || chan->softhangup) 
		return -1;
	switch(chan->state) {
	case AST_STATE_RINGING:
	case AST_STATE_RING:
		if (chan->pvt->answer)
			res = chan->pvt->answer(chan);
		chan->state = AST_STATE_UP;
		return res;
		break;
	case AST_STATE_UP:
		break;
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
		if (fds[x] > -1) {
			FD_SET(fds[x], &rfds);
			FD_SET(fds[x], &efds);
			if (fds[x] > max)
				max = fds[x];
		}
	}
	if (*ms >= 0) 
		res = select(max + 1, &rfds, NULL, &efds, &tv);
	else
		res = select(max + 1, &rfds, NULL, &efds, NULL);

	if (res < 0) {
		/* Simulate a timeout if we were interrupted */
		if (errno != EINTR)
			*ms = -1;
		else
			*ms = 0;
		return -1;
	}

	for (x=0;x<n;x++) {
		if ((fds[x] > -1) && (FD_ISSET(fds[x], &rfds) || FD_ISSET(fds[x], &efds)) && (winner < 0)) {
			if (exception)
				*exception = FD_ISSET(fds[x], &efds);
			winner = fds[x];
		}
	}
	*ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	return winner;
}

static int ast_do_masquerade(struct ast_channel *original);

struct ast_channel *ast_waitfor_nandfds(struct ast_channel **c, int n, int *fds, int nfds, 
	int *exception, int *outfd, int *ms)
{
	/* Wait for x amount of time on a file descriptor to have input.  */
	struct timeval tv;
	fd_set rfds, efds;
	int res;
	int x, y, max=-1;
	struct ast_channel *winner = NULL;
	if (outfd)
		*outfd = -1;
	if (exception)
		*exception = 0;
	
	/* Perform any pending masquerades */
	for (x=0;x<n;x++) {
		if (c[x]->masq) {
			if (ast_do_masquerade(c[x])) {
				ast_log(LOG_WARNING, "Masquerade failed\n");
				*ms = -1;
				return NULL;
			}
		}
	}
	
	tv.tv_sec = *ms / 1000;
	tv.tv_usec = (*ms % 1000) * 1000;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);
	for (x=0;x<n;x++) {
		for (y=0;y<AST_MAX_FDS;y++) {
			if (c[x]->fds[y] > 0) {
				FD_SET(c[x]->fds[y], &rfds);
				FD_SET(c[x]->fds[y], &efds);
				if (c[x]->fds[y] > max)
					max = c[x]->fds[y];
			}
		}
		CHECK_BLOCKING(c[x]);
	}
	for (x=0;x<nfds; x++) {
		FD_SET(fds[x], &rfds);
		FD_SET(fds[x], &efds);
		if (fds[x] > max)
			max = fds[x];
	}
	if (*ms >= 0) 
		res = select(max + 1, &rfds, NULL, &efds, &tv);
	else
		res = select(max + 1, &rfds, NULL, &efds, NULL);

	if (res < 0) {
		for (x=0;x<n;x++) 
			c[x]->blocking = 0;
		/* Simulate a timeout if we were interrupted */
		if (errno != EINTR)
			*ms = -1;
		else
			*ms = 0;
		return NULL;
	}

	for (x=0;x<n;x++) {
		c[x]->blocking = 0;
		for (y=0;y<AST_MAX_FDS;y++) {
			if (c[x]->fds[y] > -1) {
				if ((FD_ISSET(c[x]->fds[y], &rfds) || FD_ISSET(c[x]->fds[y], &efds)) && !winner) {
					/* Set exception flag if appropriate */
					if (FD_ISSET(c[x]->fds[y], &efds))
						c[x]->exception = 1;
					c[x]->fdno = y;
					winner = c[x];
				}
			}
		}
	}
	for (x=0;x<nfds;x++) {
		if ((FD_ISSET(fds[x], &rfds) || FD_ISSET(fds[x], &efds)) && !winner) {
			if (outfd)
				*outfd = fds[x];
			if (FD_ISSET(fds[x], &efds) && exception)
				*exception = 1;
			winner = NULL;
		}
	}
	*ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
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
	struct ast_frame *f;
	char result = 0;
	/* Stop if we're a zombie or need a soft hangup */
	if (c->zombie || c->softhangup) 
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

struct ast_frame *ast_read(struct ast_channel *chan)
{
	struct ast_frame *f = NULL;
	static struct ast_frame null_frame = 
	{
		AST_FRAME_NULL,
	};
	
	pthread_mutex_lock(&chan->lock);
	if (chan->masq) {
		if (ast_do_masquerade(chan)) {
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
			f = NULL;
		} else
			f =  &null_frame;
		pthread_mutex_unlock(&chan->lock);
		return f;
	}

	/* Stop if we're a zombie or need a soft hangup */
	if (chan->zombie || chan->softhangup) {
		pthread_mutex_unlock(&chan->lock);
		return NULL;
	}
	
	if (!chan->deferdtmf && strlen(chan->dtmfq)) {
		/* We have DTMF that has been deferred.  Return it now */
		chan->dtmff.frametype = AST_FRAME_DTMF;
		chan->dtmff.subclass = chan->dtmfq[0];
		/* Drop first digit */
		memmove(chan->dtmfq, chan->dtmfq + 1, sizeof(chan->dtmfq) - 1);
		pthread_mutex_unlock(&chan->lock);
		return &chan->dtmff;
	}
	
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
	/* Make sure we always return NULL in the future */
	if (!f)
		chan->softhangup = 1;
	else if (chan->deferdtmf && f->frametype == AST_FRAME_DTMF) {
		if (strlen(chan->dtmfq) < sizeof(chan->dtmfq) - 2)
			chan->dtmfq[strlen(chan->dtmfq)] = f->subclass;
		else
			ast_log(LOG_WARNING, "Dropping deferred DTMF digits on %s\n", chan->name);
		f = &null_frame;
	}
	pthread_mutex_unlock(&chan->lock);

	return f;
}

int ast_indicate(struct ast_channel *chan, int condition)
{
	int res = -1;
	/* Stop if we're a zombie or need a soft hangup */
	if (chan->zombie || chan->softhangup) 
		return -1;
	if (chan->pvt->indicate) {
		res = chan->pvt->indicate(chan, condition);
		if (res)
			ast_log(LOG_WARNING, "Driver for channel '%s' failed to indicate condition %d\n", chan->name, condition);
	} else
		ast_log(LOG_WARNING, "Driver for channel '%s' does not support indication\n", chan->name);
	return res;
}

int ast_sendtext(struct ast_channel *chan, char *text)
{
	int res = 0;
	/* Stop if we're a zombie or need a soft hangup */
	if (chan->zombie || chan->softhangup) 
		return -1;
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
	/* Stop if we're a zombie or need a soft hangup */
	if (chan->zombie || chan->softhangup) 
		return -1;
	/* Handle any pending masquerades */
	if (chan->masq) {
		if (ast_do_masquerade(chan)) {
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
			return -1;
		}
	}
	if (chan->masqr)
		return 0;
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
			else
				res = 0;
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
	if (option_debug)
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
	/* Stop if we're a zombie or need a soft hangup */
	pthread_mutex_lock(&chan->lock);
	if (!chan->zombie && !chan->softhangup) 
		if (chan->pvt->call)
			res = chan->pvt->call(chan, addr, timeout);
	pthread_mutex_unlock(&chan->lock);
	return res;
}

int ast_readstring(struct ast_channel *c, char *s, int len, int timeout, int ftimeout, char *enders)
{
	int pos=0;
	int to = ftimeout;
	char d;
	/* Stop if we're a zombie or need a soft hangup */
	if (c->zombie || c->softhangup) 
		return -1;
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
		if (d == 0) {
			s[pos]='\0';
			return 1;
		}
		if (!strchr(enders, d))
			s[pos++] = d;
		if (strchr(enders, d) || (pos >= len - 1)) {
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

int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clone)
{
	ast_log(LOG_DEBUG, "Planning to masquerade %s into the structure of %s\n",
		clone->name, original->name);
	if (original->masq) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n", 
			original->masq->name, original->name);
		return -1;
	}
	if (clone->masqr) {
		ast_log(LOG_WARNING, "%s is already going to masquerade as %s\n", 
			clone->name, clone->masqr->name);
		return -1;
	}
	original->masq = clone;
	clone->masqr = original;
	return 0;
}

static int ast_do_masquerade(struct ast_channel *original)
{
	int x;
	int res=0;
	char *tmp;
	struct ast_channel_pvt *p;
	struct ast_channel *clone = original->masq;
	int rformat = original->readformat;
	int wformat = original->writeformat;
	
#if 0
	ast_log(LOG_DEBUG, "Actually Masquerading %s(%d) into the structure of %s(%d)\n",
		clone->name, clone->state, original->name, original->state);
#endif
	/* XXX This is a seriously wacked out operation.  We're essentially putting the guts of
	   the clone channel into the original channel.  Start by killing off the original
	   channel's backend.   I'm not sure we're going to keep this function, because 
	   while the features are nice, the cost is very high in terms of pure nastiness. XXX */

	/* Having remembered the original read/write formats, we turn off any translation on either
	   one */
	free_translation(clone);
	free_translation(original);

	/* We need the clone's lock, too */
	pthread_mutex_lock(&clone->lock);

	/* Unlink the masquerade */
	original->masq = NULL;
	clone->masqr = NULL;
		
	/* Copy the name from the clone channel */
	strncpy(original->name, clone->name, sizeof(original->name));

	/* Mangle the name of the clone channel */
	strncat(clone->name, "<MASQ>", sizeof(clone->name));

	/* Swap the guts */	
	p = original->pvt;
	original->pvt = clone->pvt;
	clone->pvt = p;
	
	clone->softhangup = 1;


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
		pthread_mutex_unlock(&clone->lock);
		return -1;
	}
	
	/* Mangle the name of the clone channel */
	snprintf(clone->name, sizeof(clone->name), "%s<ZOMBIE>", original->name);

	/* Keep the same language.  */
	/* Update the type. */
	original->type = clone->type;
	/* Copy the FD's */
	for (x=0;x<AST_MAX_FDS;x++)
		original->fds[x] = clone->fds[x];
	/* Bridge remains the same */
	/* CDR fields remain the same */
	/* XXX What about blocking, softhangup, blocker, and lock and blockproc? XXX */
	/* Application and data remain the same */
	/* Clone exception  becomes real one, as with fdno */
	original->exception = clone->exception;
	original->fdno = clone->fdno;
	/* Schedule context remains the same */
	/* Stream stuff stays the same */
	/* Keep the original state.  The fixup code will need to work with it most likely */

	/* dnid and callerid change to become the new, HOWEVER, we also link the original's
	   fields back into the defunct 'clone' so that they will be freed when
	   ast_frfree is eventually called */
	tmp = original->dnid;
	original->dnid = clone->dnid;
	clone->dnid = tmp;
	
	tmp = original->callerid;
	original->callerid = clone->callerid;
	clone->callerid = tmp;
	
	/* Our native formats are different now */
	original->nativeformats = clone->nativeformats;
	
	/* Context, extension, priority, app data, jump table,  remain the same */
	/* pvt switches.  pbx stays the same, as does next */
	
	/* Now, at this point, the "clone" channel is totally F'd up.  We mark it as
	   a zombie so nothing tries to touch it.  If it's already been marked as a
	   zombie, then free it now (since it already is considered invalid). */
	if (clone->zombie) {
		pthread_mutex_unlock(&clone->lock);
		ast_channel_free(clone);
	} else {
		clone->zombie=1;
		pthread_mutex_unlock(&clone->lock);
	}
	/* Set the write format */
	ast_set_write_format(original, wformat);

	/* Set the read format */
	ast_set_read_format(original, rformat);

	ast_log(LOG_DEBUG, "Putting channel %s in %d/%d formats\n", original->name, wformat, rformat);

	/* Okay.  Last thing is to let the channel driver know about all this mess, so he
	   can fix up everything as best as possible */
	if (original->pvt->fixup) {
		res = original->pvt->fixup(clone, original);
		if (res) {
			ast_log(LOG_WARNING, "Driver for '%s' could not fixup channel %s\n",
				original->type, original->name);
			return -1;
		}
	} else
		ast_log(LOG_WARNING, "Driver '%s' does not have a fixup routine (for %s)!  Bad things may happen.\n",
			original->type, original->name);
	/* Signal any blocker */
	if (original->blocking)
		pthread_kill(original->blocker, SIGURG);
	ast_log(LOG_DEBUG, "Done Masquerading %s(%d) into the structure of %s(%d)\n",
		clone->name, clone->state, original->name, original->state);
	return 0;
}

int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_channel *cs[3];
	int to = -1;
	struct ast_frame *f;
	struct ast_channel *who = NULL;
	int res;
	int nativefailed=0;
	/* Stop if we're a zombie or need a soft hangup */
	if (c0->zombie || c0->softhangup || c1->zombie || c1->softhangup) 
		return -1;
	if (c0->bridge) {
		ast_log(LOG_WARNING, "%s is already in a bridge with %s\n", 
			c0->name, c0->bridge->name);
		return -1;
	}
	if (c1->bridge) {
		ast_log(LOG_WARNING, "%s is already in a bridge with %s\n", 
			c1->name, c1->bridge->name);
		return -1;
	}
	
	/* Keep track of bridge */
	c0->bridge = c1;
	c1->bridge = c0;
	cs[0] = c0;
	cs[1] = c1;
	for (/* ever */;;) {
		/* Stop if we're a zombie or need a soft hangup */
		if (c0->zombie || c0->softhangup || c1->zombie || c1->softhangup) {
			*fo = NULL;
			if (who) *rc = who;
			res = 0;
			break;
		}
		if (c0->pvt->bridge && 
			(c0->pvt->bridge == c1->pvt->bridge) && !nativefailed) {
				/* Looks like they share a bridge code */
			if (!(res = c0->pvt->bridge(c0, c1, flags, fo, rc))) {
				c0->bridge = NULL;
				c1->bridge = NULL;
				return 0;
			}
			/* If they return non-zero then continue on normally.  Let "-2" mean don't worry about
			   my not wanting to bridge */
			if ((res != -2) && (res != -3))
				ast_log(LOG_WARNING, "Private bridge between %s and %s failed\n", c0->name, c1->name);
			if (res != -3) nativefailed++;
		}
	
			
		if ((c0->writeformat != c1->readformat) || (c0->readformat != c1->writeformat)) {
			if (ast_channel_make_compatible(c0, c1)) {
				ast_log(LOG_WARNING, "Can't make %s and %s compatible\n", c0->name, c1->name);
				return -1;
			}
		}
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_WARNING, "Nobody there??\n"); 
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			res = 0;
			break;
		}
		if ((f->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			*fo = f;
			*rc = who;
			res =  0;
			break;
		}
		if ((f->frametype == AST_FRAME_VOICE) ||
			(f->frametype == AST_FRAME_TEXT) ||
			(f->frametype == AST_FRAME_VIDEO) || 
			(f->frametype == AST_FRAME_IMAGE) ||
			(f->frametype == AST_FRAME_DTMF)) {
			if ((f->frametype == AST_FRAME_DTMF) && 
				(flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))) {
				if ((who == c0)) {
					if  ((flags & AST_BRIDGE_DTMF_CHANNEL_0)) {
						*rc = c0;
						*fo = f;
						/* Take out of conference mode */
						res = 0;
						break;
					} else 
						goto tackygoto;
				} else
				if ((who == c1)) {
					if (flags & AST_BRIDGE_DTMF_CHANNEL_1) {
						*rc = c1;
						*fo = f;
						res =  0;
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
	c0->bridge = NULL;
	c1->bridge = NULL;
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
