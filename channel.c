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
#include <math.h>			/* For PI */
#include <asterisk/pbx.h>
#include <asterisk/frame.h>
#include <asterisk/sched.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/file.h>
#include <asterisk/translate.h>
#include <asterisk/manager.h>
#include <asterisk/chanvars.h>
#include <asterisk/linkedlists.h>
#include <asterisk/indications.h>
#include <asterisk/monitor.h>


static int shutting_down = 0;

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
	int (*devicestate)(void *data);
	struct chanlist *next;
} *backends = NULL;
struct ast_channel *channels = NULL;

/* Protect the channel list (highly unlikely that two things would change
   it at the same time, but still! */
   
static pthread_mutex_t chlock = AST_MUTEX_INITIALIZER;

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

void ast_begin_shutdown(int hangup)
{
	struct ast_channel *c;
	shutting_down = 1;
	if (hangup) {
		PTHREAD_MUTEX_LOCK(&chlock);
		c = channels;
		while(c) {
			ast_softhangup(c, AST_SOFTHANGUP_SHUTDOWN);
			c = c->next;
		}
		PTHREAD_MUTEX_UNLOCK(&chlock);
	}
}

int ast_active_channels(void)
{
	struct ast_channel *c;
	int cnt = 0;
	PTHREAD_MUTEX_LOCK(&chlock);
	c = channels;
	while(c) {
		cnt++;
		c = c->next;
	}
	PTHREAD_MUTEX_UNLOCK(&chlock);
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

int ast_channel_register(char *type, char *description, int capabilities,
		struct ast_channel *(*requester)(char *type, int format, void *data))
{
    return ast_channel_register_ex(type, description, capabilities, requester, NULL);
}

int ast_channel_register_ex(char *type, char *description, int capabilities,
		struct ast_channel *(*requester)(char *type, int format, void *data),
		int (*devicestate)(void *data))
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
		/* Speex is free, but computationally more expensive than GSM */
		AST_FORMAT_SPEEX,
		/* Ick, LPC10 sounds terrible, but at least we have code for it, if you're tacky enough
		   to use it */
		AST_FORMAT_LPC10,
		/* G.729a is faster than 723 and slightly less expensive */
		AST_FORMAT_G729A,
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
	PTHREAD_MUTEX_LOCK(&chlock);
	tmp = malloc(sizeof(struct ast_channel));
	memset(tmp, 0, sizeof(struct ast_channel));
	if (tmp) {
		pvt = malloc(sizeof(struct ast_channel_pvt));
		if (pvt) {
			memset(pvt, 0, sizeof(struct ast_channel_pvt));
			tmp->sched = sched_context_create();
			if (tmp->sched) {
				for (x=0;x<AST_MAX_FDS - 1;x++)
					tmp->fds[x] = -1;
				if (needqueue &&  
					pipe(pvt->alertpipe)) {
					ast_log(LOG_WARNING, "Alert pipe creation failed!\n");
					free(pvt);
					free(tmp);
					tmp = NULL;
					pvt = NULL;
				} else {
					/* Make sure we've got it done right if they don't */
					if (needqueue) {
						flags = fcntl(pvt->alertpipe[0], F_GETFL);
						fcntl(pvt->alertpipe[0], F_SETFL, flags | O_NONBLOCK);
						flags = fcntl(pvt->alertpipe[1], F_GETFL);
						fcntl(pvt->alertpipe[1], F_SETFL, flags | O_NONBLOCK);
					} else
						pvt->alertpipe[0] = pvt->alertpipe[1] = -1;
					/* Always watch the alertpipe */
					tmp->fds[AST_MAX_FDS-1] = pvt->alertpipe[0];
					strncpy(tmp->name, "**Unknown**", sizeof(tmp->name)-1);
					tmp->pvt = pvt;
					/* Initial state */
					tmp->_state = AST_STATE_DOWN;
					tmp->stack = -1;
					tmp->streamid = -1;
					tmp->appl = NULL;
					tmp->data = NULL;
					tmp->fin = 0;
					tmp->fout = 0;
					headp=&tmp->varshead;
					ast_pthread_mutex_init(&tmp->lock);
				        AST_LIST_HEAD_INIT(headp);
					tmp->vars=ast_var_assign("tempvar","tempval");
					AST_LIST_INSERT_HEAD(headp,tmp->vars,entries);
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
	PTHREAD_MUTEX_UNLOCK(&chlock);
	return tmp;
}

int ast_queue_frame(struct ast_channel *chan, struct ast_frame *fin, int lock)
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
	if (lock)
		ast_pthread_mutex_lock(&chan->lock);
	prev = NULL;
	cur = chan->pvt->readq;
	while(cur) {
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
			ast_frfree(fin);
			if (lock)
				ast_pthread_mutex_unlock(&chan->lock);
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
	} else if (chan->blocking) {
		pthread_kill(chan->blocker, SIGURG);
	}
	if (lock)
		ast_pthread_mutex_unlock(&chan->lock);
	return 0;
}

int ast_queue_hangup(struct ast_channel *chan, int lock)
{
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_HANGUP };
	chan->_softhangup |= AST_SOFTHANGUP_DEV;
	return ast_queue_frame(chan, &f, lock);
}

int ast_queue_control(struct ast_channel *chan, int control, int lock)
{
	struct ast_frame f = { AST_FRAME_CONTROL, };
	f.subclass = control;
	return ast_queue_frame(chan, &f, lock);
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

void ast_channel_free(struct ast_channel *chan)
{
	struct ast_channel *last=NULL, *cur;
	int fd;
	struct ast_var_t *vardata;
	struct ast_frame *f, *fp;
	struct varshead *headp;
	char name[AST_CHANNEL_NAME];
	
	headp=&chan->varshead;
	
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

	strncpy(name, chan->name, sizeof(name)-1);
	
	/* Stop monitoring */
	if (chan->monitor) {
		chan->monitor->stop( chan, 0 );
	}

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
	if (chan->ani)
		free(chan->ani);
	if (chan->rdnis)
		free(chan->rdnis);
	pthread_mutex_destroy(&chan->lock);
	/* Close pipes if appropriate */
	if ((fd = chan->pvt->alertpipe[0]) > -1)
		close(fd);
	if ((fd = chan->pvt->alertpipe[1]) > -1)
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
	            vardata = AST_LIST_FIRST(headp);
	            AST_LIST_REMOVE_HEAD(headp, entries);
//	            printf("deleting var %s=%s\n",ast_var_name(vardata),ast_var_value(vardata));
	            ast_var_delete(vardata);
	}
	                                                 

	free(chan->pvt);
	chan->pvt = NULL;
	free(chan);
	PTHREAD_MUTEX_UNLOCK(&chlock);

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
	ast_queue_frame(chan, &f, 0);
	/* Interrupt any select call or such */
	if (chan->blocking)
		pthread_kill(chan->blocker, SIGURG);
	return res;
}

int ast_softhangup(struct ast_channel *chan, int cause)
{
	int res;
	ast_pthread_mutex_lock(&chan->lock);
	res = ast_softhangup_nolock(chan, cause);
	ast_pthread_mutex_unlock(&chan->lock);
	return res;
}

static int ast_do_masquerade(struct ast_channel *original);

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
		if (ast_do_masquerade(chan)) 
			ast_log(LOG_WARNING, "Failed to perform masquerade\n");
	}

	if (chan->masq) {
		ast_log(LOG_WARNING, "%s getting hung up, but someone is trying to masq into us?!?\n", chan->name);
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
	manager_event(EVENT_FLAG_CALL, "Hangup", 
			"Channel: %s\r\n",
			chan->name);
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
	if (chan->zombie || ast_check_hangup(chan)) 
		return -1;
	switch(chan->_state) {
	case AST_STATE_RINGING:
	case AST_STATE_RING:
		if (chan->pvt->answer)
			res = chan->pvt->answer(chan);
		ast_setstate(chan, AST_STATE_UP);
		if (chan->cdr)
			ast_cdr_answer(chan->cdr);
		return res;
		break;
	case AST_STATE_UP:
		if (chan->cdr)
			ast_cdr_answer(chan->cdr);
		break;
	}
	return 0;
}

void ast_deactivate_generator(struct ast_channel *chan)
{
	if (chan->generatordata) {
		chan->generator->release(chan, chan->generatordata);
		chan->generatordata = NULL;
		chan->generator = NULL;
		chan->writeinterrupt = 0;
	}
}

int ast_activate_generator(struct ast_channel *chan, struct ast_generator *gen, void *params)
{
	if (chan->generatordata) {
		chan->generator->release(chan, chan->generatordata);
		chan->generatordata = NULL;
	}
	if ((chan->generatordata = gen->alloc(chan, params))) {
		chan->generator = gen;
	} else {
		return -1;
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
			if (c[x]->fds[y] > -1) {
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
		else {
			/* Just an interrupt */
#if 0
			*ms = 0;
#endif			
		}
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
	/* XXX Should I be merged with waitfordigit_full XXX */
	struct ast_frame *f;
	char result = 0;
	/* Stop if we're a zombie or need a soft hangup */
	if (c->zombie || ast_check_hangup(c)) 
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

char ast_waitfordigit_full(struct ast_channel *c, int ms, int audio, int ctrl)
{
	struct ast_frame *f;
	char result = 0;
	struct ast_channel *rchan;
	int outfd;
	/* Stop if we're a zombie or need a soft hangup */
	if (c->zombie || ast_check_hangup(c)) 
		return -1;
	/* Wait for a digit, no more than ms milliseconds total. */
	while(ms && !result) {
		rchan = ast_waitfor_nandfds(&c, 1, &audio, (audio > -1) ? 1 : 0, NULL, &outfd, &ms);
		if ((!rchan) && (outfd < 0) && (ms)) /* Error */
			result = -1; 
		else if (outfd > -1) {
			result = 1;
		} else if (rchan) {
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
	int blah;
	static struct ast_frame null_frame = 
	{
		AST_FRAME_NULL,
	};
	
	ast_pthread_mutex_lock(&chan->lock);
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
	if (chan->zombie || ast_check_hangup(chan)) {
		if (chan->generator)
			ast_deactivate_generator(chan);
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
	
	/* Read and ignore anything on the alertpipe, but read only
	   one sizeof(blah) per frame that we send from it */
	if (chan->pvt->alertpipe[0] > -1) {
		read(chan->pvt->alertpipe[0], &blah, sizeof(blah));
	}

	/* Check for pending read queue */
	if (chan->pvt->readq) {
		f = chan->pvt->readq;
		chan->pvt->readq = f->next;
		/* Interpret hangup and return NULL */
		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))
			f = NULL;
	} else {
		chan->blocker = pthread_self();
		if (chan->exception) {
			if (chan->pvt->exception) 
				f = chan->pvt->exception(chan);
			else {
				ast_log(LOG_WARNING, "Exception flag set on '%s', but no exception handler\n", chan->name);
				f = &null_frame;
			}
			/* Clear the exception flag */
			chan->exception = 0;
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
			ast_log(LOG_NOTICE, "Dropping incompatible voice frame on %s of format %d since our native format has changed to %d\n", chan->name, f->subclass, chan->nativeformats);
			ast_frfree(f);
			f = &null_frame;
		} else if (chan->pvt->readtrans) {
			f = ast_translate(chan->pvt->readtrans, f, 1);
			if (!f)
				f = &null_frame;
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
	} else if (chan->deferdtmf && f->frametype == AST_FRAME_DTMF) {
		if (strlen(chan->dtmfq) < sizeof(chan->dtmfq) - 2)
			chan->dtmfq[strlen(chan->dtmfq)] = f->subclass;
		else
			ast_log(LOG_WARNING, "Dropping deferred DTMF digits on %s\n", chan->name);
		f = &null_frame;
	} else if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_ANSWER)) {
		/* Answer the CDR */
		ast_setstate(chan, AST_STATE_UP);
		ast_cdr_answer(chan->cdr);
	} else if( ( f->frametype == AST_FRAME_VOICE ) && chan->monitor && chan->monitor->read_stream ) {
		if( ast_writestream( chan->monitor->read_stream, f ) < 0 ) {
			ast_log(LOG_WARNING, "Failed to write data to channel monitor read stream\n");
		}
	}
	pthread_mutex_unlock(&chan->lock);

	/* Run any generator sitting on the line */
	if (f && (f->frametype == AST_FRAME_VOICE) && chan->generatordata) {
		/* Mask generator data temporarily */
		void *tmp;
		int res;
		tmp = chan->generatordata;
		chan->generatordata = NULL;
		res = chan->generator->generate(chan, tmp, f->datalen, f->samples);
		chan->generatordata = tmp;
		if (res) {
			ast_log(LOG_DEBUG, "Auto-deactivating generator\n");
			ast_deactivate_generator(chan);
		}
	}
	if (chan->fin & 0x80000000)
		ast_frame_dump(chan->name, f, "<<");
	if ((chan->fin & 0x7fffffff) == 0x7fffffff)
		chan->fin &= 0x80000000;
	else
		chan->fin++;
	return f;
}

int ast_indicate(struct ast_channel *chan, int condition)
{
	int res = -1;
	/* Stop if we're a zombie or need a soft hangup */
	if (chan->zombie || ast_check_hangup(chan)) 
		return -1;
	if (chan->pvt->indicate)
		res = chan->pvt->indicate(chan, condition);
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
			}
			else  {
				/* not handled */
				ast_log(LOG_WARNING, "Unable to handle indication %d for '%s'\n", condition, chan->name);
				return -1;
			}
		}
		else ast_playtones_stop(chan);
	}
	return 0;
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
	if (chan->zombie || ast_check_hangup(chan)) 
		return -1;
	CHECK_BLOCKING(chan);
	if (chan->pvt->send_text)
		res = chan->pvt->send_text(chan, text);
	chan->blocking = 0;
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
			"!941+1336/50,!0/50",	/* 0 */
			"!697+1209/50,!0/50",	/* 1 */
			"!697+1336/50,!0/50",	/* 2 */
			"!697+1477/50,!0/50",	/* 3 */
			"!770+1209/50,!0/50",	/* 4 */
			"!770+1336/50,!0/50",	/* 5 */
			"!770+1477/50,!0/50",	/* 6 */
			"!852+1209/50,!0/50",	/* 7 */
			"!852+1336/50,!0/50",	/* 8 */
			"!852+1477/50,!0/50",	/* 9 */
			"!697+1633/50,!0/50",	/* A */
			"!770+1633/50,!0/50",	/* B */
			"!852+1633/50,!0/50",	/* C */
			"!941+1633/50,!0/50",	/* D */
			"!941+1209/50,!0/50",	/* * */
			"!941+1477/50,!0/50" };	/* # */
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
			ast_log(LOG_WARNING, "Unable to handle DTMF tone '%c' for '%s'\n", digit, chan->name);
			return -1;
		}
	}
	return 0;
}

int ast_write(struct ast_channel *chan, struct ast_frame *fr)
{
	int res = -1;
	struct ast_frame *f = NULL;
	/* Stop if we're a zombie or need a soft hangup */
	if (chan->zombie || ast_check_hangup(chan)) 
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
	if (chan->generatordata) {
		if (chan->writeinterrupt)
			ast_deactivate_generator(chan);
		else
			return 0;
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
		res = do_senddigit(chan,fr->subclass);
		break;
	case AST_FRAME_TEXT:
		if (chan->pvt->send_text)
			res = chan->pvt->send_text(chan, (char *) fr->data);
		break;
	default:
		if (chan->pvt->write) {
			if (chan->pvt->writetrans) {
				f = ast_translate(chan->pvt->writetrans, fr, 0);
			} else
				f = fr;
			if (f)	
			{
				res = chan->pvt->write(chan, f);
				if( chan->monitor &&
						chan->monitor->write_stream &&
						f && ( f->frametype == AST_FRAME_VOICE ) ) {
					if( ast_writestream( chan->monitor->write_stream, f ) < 0 ) {
						ast_log(LOG_WARNING, "Failed to write data to channel monitor write stream\n");
					}
				}
			}
			else
				res = 0;
		}
	}
	if (f && (f != fr))
		ast_frfree(f);
	chan->blocking = 0;
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
		ast_log(LOG_DEBUG, "Set channel %s to write format %d\n", chan->name, chan->writeformat);
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
	if (option_debug)
		ast_log(LOG_DEBUG, "Set channel %s to read format %d\n", chan->name, chan->readformat);
	return 0;
}

struct ast_channel *ast_request_and_dial(char *type, int format, void *data, int timeout, int *outstate, char *callerid)
{
	int state = 0;
	struct ast_channel *chan;
	struct ast_frame *f;
	int res;
	
	chan = ast_request(type, format, data);
	if (chan) {
		if (callerid)
			ast_set_callerid(chan, callerid, 1);
		if (!ast_call(chan, data, 0)) {
			while(timeout && (chan->_state != AST_STATE_UP)) {
				res = ast_waitfor(chan, timeout);
				if (res < 0) {
					/* Something not cool, or timed out */
					ast_hangup(chan);
					chan = NULL;
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
					ast_hangup(chan);
					chan = NULL;
					break;
				}
				if (f->frametype == AST_FRAME_CONTROL) {
					if (f->subclass == AST_CONTROL_RINGING)
						state = AST_CONTROL_RINGING;
					else if ((f->subclass == AST_CONTROL_BUSY) || (f->subclass == AST_CONTROL_CONGESTION)) {
						state = f->subclass;
						break;
					} else if (f->subclass == AST_CONTROL_ANSWER) {
						state = f->subclass;
						break;
					} else {
						ast_log(LOG_NOTICE, "Don't know what to do with control frame %d\n", f->subclass);
					}
				}
				ast_frfree(f);
			}
		} else {
			ast_hangup(chan);
			chan = NULL;
			ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		}
	} else
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
	if (chan && (chan->_state == AST_STATE_UP))
		state = AST_CONTROL_ANSWER;
	if (outstate)
		*outstate = state;
	return chan;
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
			if (c) {
//				ast_device_state_changed(c->name);
				manager_event(EVENT_FLAG_CALL, "Newchannel",
				"Channel: %s\r\n"
				"State: %s\r\n"
				"Callerid: %s\r\n",
				c->name, ast_state2str(c->_state), c->callerid ? c->callerid : "<unknown>");
			}
			return c;
		}
		chan = chan->next;
	}
	if (!chan)
		ast_log(LOG_WARNING, "No channel type registered for '%s'\n", type);
	PTHREAD_MUTEX_UNLOCK(&chlock);
	return c;
}

int ast_parse_device_state(char *device)
{
	char name[AST_CHANNEL_NAME] = "";
	char *cut;
	struct ast_channel *chan;

	chan = ast_channel_walk(NULL);
	while (chan) {
		strncpy(name, chan->name, sizeof(name)-1);
		cut = strchr(name,'-');
		if (cut)
		        *cut = 0;
		if (!strcmp(name, device))
		        return AST_DEVICE_INUSE;
		chan = ast_channel_walk(chan);
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
		
	if (PTHREAD_MUTEX_LOCK(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return -1;
	}
	chanls = backends;
	while(chanls) {
		if (!strcasecmp(tech, chanls->type)) {
			PTHREAD_MUTEX_UNLOCK(&chlock);
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
	PTHREAD_MUTEX_UNLOCK(&chlock);
	return AST_DEVICE_INVALID;
}

int ast_call(struct ast_channel *chan, char *addr, int timeout) 
{
	/* Place an outgoing call, but don't wait any longer than timeout ms before returning. 
	   If the remote end does not answer within the timeout, then do NOT hang up, but 
	   return anyway.  */
	int res = -1;
	/* Stop if we're a zombie or need a soft hangup */
	ast_pthread_mutex_lock(&chan->lock);
	if (!chan->zombie && !ast_check_hangup(chan)) 
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
	/* XXX Merge with full version? XXX */
	/* Stop if we're a zombie or need a soft hangup */
	if (c->zombie || ast_check_hangup(c)) 
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
	if (c->zombie || ast_check_hangup(c)) 
		return -1;
	if (!len)
		return -1;
	do {
		if (c->streamid > -1) {
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
	peerf = peer->nativeformats;
	chanf = chan->nativeformats;
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
	/* XXX can't really hold the lock here, but at the same time, it' s
	   not really safe not to XXX */
	ast_queue_frame(original, &null, 0);
	ast_queue_frame(clone, &null, 0);
	return 0;
}

void ast_change_name(struct ast_channel *chan, char *newname)
{
	char tmp[256];
	strncpy(tmp, chan->name, 256);
	strncpy(chan->name, newname, sizeof(chan->name) - 1);
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\n", tmp, chan->name);
}

static int ast_do_masquerade(struct ast_channel *original)
{
	int x;
	int res=0;
	char *tmp;
	void *tmpv;
	struct ast_channel_pvt *p;
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

	/* Having remembered the original read/write formats, we turn off any translation on either
	   one */
	free_translation(clone);
	free_translation(original);

	/* We need the clone's lock, too */
	ast_pthread_mutex_lock(&clone->lock);

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
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\n", newn, masqn);
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\n", orig, newn);

	/* Swap the guts */	
	p = original->pvt;
	original->pvt = clone->pvt;
	clone->pvt = p;
	
	clone->_softhangup = AST_SOFTHANGUP_DEV;


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
	
	snprintf(zombn, sizeof(zombn), "%s<ZOMBIE>", orig);
	/* Mangle the name of the clone channel */
	strncpy(clone->name, zombn, sizeof(clone->name) - 1);
	manager_event(EVENT_FLAG_CALL, "Rename", "Oldname: %s\r\nNewname: %s\r\n", masqn, zombn);

	/* Keep the same language.  */
	/* Update the type. */
	original->type = clone->type;
	/* Copy the FD's */
	for (x=0;x<AST_MAX_FDS;x++)
		original->fds[x] = clone->fds[x];
	/* Move the variables */
	tmpv = original->varshead.first;
	original->varshead.first = clone->varshead.first;
	clone->varshead.first = tmpv;
	/* Presense of ADSI capable CPE follows clone */
	original->adsicpe = clone->adsicpe;
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

	/* And of course, so does our current state.  Note we need not
	   call ast_setstate since the event manager doesn't really consider
	   these separate */
	original->_state = clone->_state;
	
	/* Context, extension, priority, app data, jump table,  remain the same */
	/* pvt switches.  pbx stays the same, as does next */
	
	/* Now, at this point, the "clone" channel is totally F'd up.  We mark it as
	   a zombie so nothing tries to touch it.  If it's already been marked as a
	   zombie, then free it now (since it already is considered invalid). */
	if (clone->zombie) {
		pthread_mutex_unlock(&clone->lock);
		ast_channel_free(clone);
		manager_event(EVENT_FLAG_CALL, "Hangup", "Channel: %s\r\n", zombn);
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
	return 0;
}

void ast_set_callerid(struct ast_channel *chan, char *callerid, int anitoo)
{
	if (chan->callerid)
		free(chan->callerid);
	if (anitoo && chan->ani)
		free(chan->ani);
	if (callerid) {
		chan->callerid = strdup(callerid);
		if (anitoo)
			chan->ani = strdup(callerid);
	} else {
		chan->callerid = NULL;
		if (anitoo)
			chan->ani = NULL;
	}
	if (chan->cdr)
		ast_cdr_setcid(chan->cdr, chan);
	manager_event(EVENT_FLAG_CALL, "Newcallerid", 
				"Channel: %s\r\n"
				"Callerid: %s\r\n",
				chan->name, chan->callerid ? 
				chan->callerid : "<Unknown>");
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
			"Callerid: %s\r\n",
			chan->name, ast_state2str(chan->_state), chan->callerid ? chan->callerid : "<unknown>");
		} else {
			manager_event(EVENT_FLAG_CALL, "Newstate", 
				"Channel: %s\r\n"
				"State: %s\r\n"
				"Callerid: %s\r\n",
				chan->name, ast_state2str(chan->_state), chan->callerid ? chan->callerid : "<unknown>");
		}
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
	struct ast_channel *who = NULL;
	int res;
	int nativefailed=0;

	/* Stop if we're a zombie or need a soft hangup */
	if (c0->zombie || ast_check_hangup(c0) || c1->zombie || ast_check_hangup(c1)) 
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
	
	manager_event(EVENT_FLAG_CALL, "Link", 
			"Channel1: %s\r\n"
			"Channel2: %s\r\n",
			c0->name, c1->name);

	for (/* ever */;;) {
		/* Stop if we're a zombie or need a soft hangup */
		if (c0->zombie || ast_check_hangup(c0) || c1->zombie || ast_check_hangup(c1)) {
			*fo = NULL;
			if (who) *rc = who;
			res = 0;
			ast_log(LOG_DEBUG, "Bridge stops because we're zombie or need a soft hangup: c0=%s, c1=%s, flags: %s,%s,%s,%s\n",c0->name,c1->name,c0->zombie?"Yes":"No",ast_check_hangup(c0)?"Yes":"No",c1->zombie?"Yes":"No",ast_check_hangup(c1)?"Yes":"No");
			break;
		}
		if (c0->pvt->bridge && 
			(c0->pvt->bridge == c1->pvt->bridge) && !nativefailed && !c0->monitor && !c1->monitor) {
				/* Looks like they share a bridge code */
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "Attempting native bridge of %s and %s\n", c0->name, c1->name);
			if (!(res = c0->pvt->bridge(c0, c1, flags, fo, rc))) {
				c0->bridge = NULL;
				c1->bridge = NULL;
				manager_event(EVENT_FLAG_CALL, "Unlink", 
					"Channel1: %s\r\n"
					"Channel2: %s\r\n",
					c0->name, c1->name);
				ast_log(LOG_DEBUG, "Returning from native bridge, channels: %s, %s\n",c0->name ,c1->name);
				return 0;
			}
			/* If they return non-zero then continue on normally.  Let "-2" mean don't worry about
			   my not wanting to bridge */
			if ((res != -2) && (res != -3))
				ast_log(LOG_WARNING, "Private bridge between %s and %s failed\n", c0->name, c1->name);
			if (res != -3) nativefailed++;
		}
	
			
		if (((c0->writeformat != c1->readformat) || (c0->readformat != c1->writeformat)) &&
			!(c0->generator || c1->generator))  {
			if (ast_channel_make_compatible(c0, c1)) {
				ast_log(LOG_WARNING, "Can't make %s and %s compatible\n", c0->name, c1->name);
				manager_event(EVENT_FLAG_CALL, "Unlink", 
					"Channel1: %s\r\n"
					"Channel2: %s\r\n",
					c0->name, c1->name);
				return -1;
			}
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

		if ((f->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			*fo = f;
			*rc = who;
			res =  0;
			ast_log(LOG_DEBUG, "Got a FRAME_CONTROL frame on channel %s\n",who->name);
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
						ast_log(LOG_DEBUG, "Got AST_BRIDGE_DTMF_CHANNEL_0 on c0 (%s)\n",c0->name);
						break;
					} else 
						goto tackygoto;
				} else
				if ((who == c1)) {
					if (flags & AST_BRIDGE_DTMF_CHANNEL_1) {
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
	c0->bridge = NULL;
	c1->bridge = NULL;
	manager_event(EVENT_FLAG_CALL, "Unlink", 
					"Channel1: %s\r\n"
					"Channel2: %s\r\n",
					c0->name, c1->name);
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
	chan->writeinterrupt = 1;
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


