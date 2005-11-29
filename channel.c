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
	if (chan->trans)
		ast_log(LOG_WARNING, "Hard hangup called on '%s' while a translator is in place!  Expect a failure.\n", chan->name);
	if (chan->pbx) 
		ast_log(LOG_WARNING, "PBX may not have been terminated properly on '%s'\n", chan->name);
	if (chan->dnid)
		free(chan->dnid);
	if (chan->callerid)
		free(chan->callerid);	
	pthread_mutex_destroy(&chan->lock);
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
	if (chan->trans)
		ast_log(LOG_WARNING, "Soft hangup called on '%s' while a translator is in place!  Expect a failure.\n", chan->name);
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
	if (chan->blocking)
		ast_log(LOG_WARNING, "Hard hangup called, while fd is blocking!  Expect a failure\n");
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

int ast_waitfor_n_fd(int *fds, int n, int *ms)
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
		if ((FD_ISSET(fds[x], &rfds) || FD_ISSET(fds[x], &efds)) && (winner < 0))
			winner = fds[x];
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
		if ((FD_ISSET(c[x]->fd, &rfds) || FD_ISSET(c[x]->fd, &efds)) && !winner)
			winner = c[x];
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
	chan->blocker = pthread_self();
	if (chan->pvt->read)
		f = chan->pvt->read(chan);
	else
		ast_log(LOG_WARNING, "No read routine on channel %s\n", chan);
	return f;
}

int ast_write(struct ast_channel *chan, struct ast_frame *fr)
{
	int res = -1;
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
		if (chan->pvt->write)
			res = chan->pvt->write(chan, fr);
	}
	chan->blocking = 0;
	return res;
}

struct ast_channel *ast_request(char *type, int format, void *data)
{
	struct chanlist *chan;
	struct ast_channel *c = NULL;
	if (PTHREAD_MUTEX_LOCK(&chlock)) {
		ast_log(LOG_WARNING, "Unable to lock channel list\n");
		return NULL;
	}
	chan = backends;
	while(chan) {
		if (!strcasecmp(type, chan->type)) {
			if (!(chan->capabilities & format)) {
				format = ast_translator_best_choice(format, chan->capabilities);
			}
			PTHREAD_MUTEX_UNLOCK(&chlock);
			if (chan->requester)
				c = chan->requester(type, format, data);
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
		if ((c->streamid > -1) || (c->trans && (c->trans->streamid > -1))) {
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
