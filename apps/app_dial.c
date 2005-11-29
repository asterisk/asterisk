/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to dial a channel
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#include <pthread.h>


static char *tdesc = "Trivial Dialing Application";

static char *app = "Dial";

/* We define a customer "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct localuser {
	struct ast_channel *chan;
	int stillgoing;
	int allowredirect;
	struct localuser *next;
};

LOCAL_USER_DECL;

static void hanguptree(struct localuser *outgoing, struct ast_channel *exception)
{
	/* Hang up a tree of stuff */
	struct localuser *oo;
	while(outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan != exception)
			ast_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->next;
		free(oo);
	}
}

static struct ast_channel *wait_for_answer(struct ast_channel *in, struct localuser *outgoing, int *to, int *allowredir)
{
	fd_set rfds, efds;
	struct localuser *o;
	int found;
	int numlines;
	int numbusies = 0;
	int orig = *to;
	struct timeval tv;
	struct ast_frame *f;
	struct ast_channel *peer = NULL;
	/* Watch all outgoing channels looking for an answer of some sort.  */
	tv.tv_sec = *to / 1000;
	tv.tv_usec = (*to % 1000) * 1000;
	while((tv.tv_sec || tv.tv_usec) && !peer) {
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		/* Always watch the input fd */
		FD_SET(in->fd, &rfds);
		FD_SET(in->fd, &efds);
		o = outgoing;
		found = -1;
		numlines = 0;
		while(o) {
			if (o->stillgoing) {
				/* Pay attention to this one */
				CHECK_BLOCKING(o->chan);
				FD_SET(o->chan->fd, &rfds);
				FD_SET(o->chan->fd, &efds);
				if (o->chan->fd > found)
					found = o->chan->fd;
			}
			numlines++;
			o = o->next;
		}
		/* If nobody is left, just go ahead and stop */
		if (found<0) {
			if (numlines == numbusies) {
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_2 "Everyone is busy at this time\n");
				/* See if there is a special busy message */
				if (ast_exists_extension(in, in->context, in->exten, in->priority + 101)) 
					in->priority+=100;
			} else {
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_2 "No one is available to answer at this time\n");
			}
			break;
		}
		if (in->fd > found)
			found = in->fd;
		if (*to > -1) 
			found = select(found + 1, &rfds, NULL, &efds, &tv);
		else
			found = select(found + 1, &rfds, NULL, &efds, NULL);
		if (found < 0) {
			ast_log(LOG_WARNING, "select failed, returned %d (%s)\n", errno, strerror(errno));
			*to = -1;
			o = outgoing;
			while(o) {
				if (o->stillgoing) {
					o->chan->blocking = 0;
				}
				o = o->next;
			}
			return NULL;
		}
		o = outgoing;
		while(o) {
			if (o->stillgoing) {
				o->chan->blocking = 0;
				if (FD_ISSET(o->chan->fd, &rfds) || FD_ISSET(o->chan->fd, &efds)) {
					f = ast_read(o->chan);
					if (f) {
						if (f->frametype == AST_FRAME_CONTROL) {
							switch(f->subclass) {
						    case AST_CONTROL_ANSWER:
								/* This is our guy if someone answered. */
								if (!peer) {
									if (option_verbose > 2)
										ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
									peer = o->chan;
									*allowredir = o->allowredirect;
								}
								break;
							case AST_CONTROL_BUSY:
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
								o->stillgoing = 0;
								numbusies++;
								break;
							case AST_CONTROL_RINGING:
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", o->chan->name);
								break;
							case AST_CONTROL_OFFHOOK:
								/* Ignore going off hook */
								break;
							default:
								ast_log(LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
							}
						}
						ast_frfree(f);
					} else {
						o->stillgoing = 0;
					}
					
				}
			}
			o = o->next;
		}
		if (FD_ISSET(in->fd, &rfds) || FD_ISSET(in->fd, &efds)) {
			/* After unblocking the entirity of the list, check for the main channel */
			f = ast_read(in);
#if 0
			if (f && (f->frametype != AST_FRAME_VOICE))
					printf("Frame type: %d, %d\n", f->frametype, f->subclass);
#endif
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass = AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to=-1;
				return NULL;
			}
		}
		
	}
	if (!(tv.tv_sec || tv.tv_usec) && (option_verbose > 2))
		ast_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
	*to = 0;
	return peer;
}

static int bridge_call(struct ast_channel *chan, struct ast_channel *peer, int allowredirect)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_channel *cs[3];
	int to = -1, len;
	struct ast_frame *f;
	struct ast_channel *who;
	char newext[256], *ptr;
	int res;
	/* Answer if need be */
	if (chan->state != AST_STATE_UP)
		if (ast_answer(chan))
			return -1;
	peer->appl = "Bridged Call";
	peer->data = chan->name;
	cs[0] = chan;
	cs[1] = peer;
	for (/* ever */;;) {
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_WARNING, "Nobody there??\n");
			continue;
		}
		f = ast_read(who);
		if (!f || ((f->frametype == AST_FRAME_CONTROL) && 
					((f->subclass == AST_CONTROL_HANGUP) ||
					 (f->subclass == AST_CONTROL_BUSY)))) 
			return -1;
		if ((f->frametype == AST_FRAME_VOICE) ||
		    (f->frametype == AST_FRAME_DTMF)) {
			if ((f->frametype == AST_FRAME_DTMF) && (who == peer) && allowredirect) {
				if (f->subclass == '#') {
					memset(newext, 0, sizeof(newext));
					ptr = newext;
					len = ast_pbx_longest_extension(chan->context) + 1;

					/* Transfer */
					if ((res=ast_streamfile(peer, "pbx-transfer")))
						break;
					if ((res=ast_waitstream(peer, AST_DIGIT_ANY)) < 0)
						break;
					ast_stopstream(peer);
					if (res > 0) {
						/* If they've typed a digit already, handle it */
						newext[0] = res;
						ptr++;
						len --;
					}
					res = ast_readstring(peer, ptr, len, 3000, 2000, "#");
					if (res)
						break;
					if (ast_exists_extension(chan, chan->context, newext, 1)) {
						/* Set the channel's new extension, since it exists */
						strncpy(chan->exten, newext, sizeof(chan->exten));
						chan->priority = 0;
						ast_frfree(f);
						res=0;
						break;
					}
					res = ast_streamfile(peer, "pbx-invalid");
					if (res)
						break;
					res = ast_waitstream(peer, AST_DIGIT_ANY);
					ast_stopstream(peer);
					res = 0;
				}
			} else {
#if 0
				ast_log(LOG_DEBUG, "Read from %s\n", who->name);
#endif
				if (who == chan) 
					ast_write(peer, f);
				else 
					ast_write(chan, f);
			}
			ast_frfree(f);
			
		} else
			ast_frfree(f);
		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
	return res;
}

static int dial_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *info, *peers, *timeout, *tech, *number, *rest, *cur;
	struct localuser *outgoing=NULL, *tmp;
	struct ast_channel *peer, *npeer;
	int to;
	int allowredir=0;
	char numsubst[AST_MAX_EXTENSION];
	char *newnum;
	
	if (!data) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology1/number1&technology2/number2...|optional timeout)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);
	
	/* Parse our arguments XXX Check for failure XXX */
	info = malloc(strlen((char *)data) + AST_MAX_EXTENSION);
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION);
	peers = strtok(info, "|");
	if (!peers) {
		ast_log(LOG_WARNING, "Dial argument takes format (technology1/number1&technology2/number2...|optional timeout)\n");
		goto out;
	}
	timeout = strtok(NULL, "|");
	rest = peers;
	do {
		cur = strtok(rest, "&");
		/* Remember where to start next time */
		rest = strtok(NULL, "\128");
		/* Get a technology/[device:]number pair */
		tech = strtok(cur, "/");
		number = strtok(NULL, "&");
		if (!number) {
			ast_log(LOG_WARNING, "Dial argument takes format (technology1/[device:]number1&technology2/[device:]number2...|optional timeout)\n");
			goto out;
		}
		tmp = malloc(sizeof(struct localuser));
		if (!tmp) {
			ast_log(LOG_WARNING, "Out of memory\n");
			goto out;
		}
		tmp->allowredirect = 1;
		strncpy(numsubst, number, sizeof(numsubst));
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(numsubst, "BYEXTENSION"))) {
			snprintf(newnum, sizeof(numsubst) - (newnum - numsubst), "%s", chan->exten);
			/* By default, if we're dialing by extension, don't permit redirecting */
			tmp->allowredirect = 0;
			if (option_debug)
				ast_log(LOG_DEBUG, "Dialing by extension %s\n", numsubst);
		}
		/* Request the peer */
		tmp->chan = ast_request(tech, chan->format, numsubst);
		if (!tmp->chan) {
			/* If we can't, just go on to the next call */
			ast_log(LOG_WARNING, "Unable to create channel of type '%s'\n", tech);
			free(tmp);
			continue;
		}
		tmp->chan->appl = "AppDial";
		tmp->chan->data = "(Outgoing Line)";
		/* Place the call, but don't wait on the answer */
		res = ast_call(tmp->chan, numsubst, 0);
		if (res) {
			/* Again, keep going even if there's an error */
			if (option_debug)
				ast_log(LOG_DEBUG, "ast call on peer returned %d\n", res);
			else if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", numsubst);
			ast_hangup(tmp->chan);
			free(tmp);
			continue;
		} else
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Called %s\n", numsubst);
		/* Put them in the list of outgoing thingies...  We're ready now. 
		   XXX If we're forcibly removed, these outgoing calls won't get
		   hung up XXX */
		tmp->stillgoing = -1;
		tmp->next = outgoing;
		outgoing = tmp;
	} while(rest);
	if (timeout)
		to = atoi(timeout) * 1000;
	else
		to = -1;
	peer = wait_for_answer(chan, outgoing, &to, &allowredir);
	if (!peer) {
		if (to) 
			/* Musta gotten hung up */
			res = -1;
		 else 
		 	/* Nobody answered, next please? */
			res=0;
		
		goto out;
	}
	if (peer) {
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the 
		   conversation.  */
		hanguptree(outgoing, peer);
		outgoing = NULL;
		/* Build a translator if necessary */
		if (peer->format & chan->format) 
			npeer = peer;
		else
			npeer = ast_translator_create(peer, chan->format, AST_DIRECTION_BOTH);
		res = bridge_call(chan, npeer, allowredir);
		if (npeer != peer)
			ast_translator_destroy(npeer);
		ast_hangup(peer);
	}	
out:
	hanguptree(outgoing, NULL);
	free(info);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, dial_exec);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}
