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
#include <asterisk/say.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>

#include <pthread.h>

static char *tdesc = "Dialing/Parking Application";

static char *app = "Dial";

static char *parkedcall = "ParkedCall";

/* No more than 90 seconds parked before you do something with them */
static int parkingtime = 90000;

/* Context for which parking is made accessible */
static char parking_con[AST_MAX_EXTENSION] = "default";

/* Extension you type to park the call */
static char parking_ext[AST_MAX_EXTENSION] = "700";

/* First available extension for parking */
static int parking_start = 701;

/* Last available extension for parking */
static int parking_stop = 750;

/* We define a customer "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct localuser {
	struct ast_channel *chan;
	int stillgoing;
	int allowredirect;
	struct localuser *next;
};

struct parkeduser {
	struct ast_channel *chan;
	struct timeval start;
	int parkingnum;
	/* Where to go if our parking time expires */
	char context[AST_MAX_EXTENSION];
	char exten[AST_MAX_EXTENSION];
	int priority;
	struct parkeduser *next;
};

static struct parkeduser *parkinglot;

static pthread_mutex_t parking_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t parking_thread;

LOCAL_USER_DECL;

static int bridge_call(struct ast_channel *chan, struct ast_channel *peer, int allowredirect);


static int park_call(struct ast_channel *chan, struct ast_channel *peer)
{
	/* We put the user in the parking list, then wake up the parking thread to be sure it looks
	   after these channels too */
	struct parkeduser *pu, *cur;
	int x;
	pu = malloc(sizeof(struct parkeduser));
	if (pu) {
		pthread_mutex_lock(&parking_lock);
		for (x=parking_start;x<=parking_stop;x++) {
			cur = parkinglot;
			while(cur) {
				if (cur->parkingnum == x) 
					break;
				cur = cur->next;
			}
			if (!cur)
				break;
		}
		if (x <= parking_stop) {
			pu->chan = chan;
			gettimeofday(&pu->start, NULL);
			pu->parkingnum = x;
			/* Remember what had been dialed, so that if the parking
			   expires, we try to come back to the same place */
			strncpy(pu->context, chan->context, sizeof(pu->context));
			strncpy(pu->exten, chan->exten, sizeof(pu->exten));
			pu->priority = chan->priority;
			pu->next = parkinglot;
			parkinglot = pu;
			pthread_mutex_unlock(&parking_lock);
			/* Wake up the (presumably select()ing) thread */
			pthread_kill(parking_thread, SIGURG);
			if (option_verbose > 1) 
				ast_verbose(VERBOSE_PREFIX_2 "Parked %s on %d\n", pu->chan->name, pu->parkingnum);
			ast_say_digits(peer, pu->parkingnum, peer->language);
			return 0;
		} else {
			ast_log(LOG_WARNING, "No more parking spaces\n");
			free(pu);
			pthread_mutex_unlock(&parking_lock);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	return 0;
}

static void *do_parking_thread(void *ignore)
{
	int ms, tms, max;
	struct parkeduser *pu, *pl, *pt = NULL;
	struct timeval tv;
	struct ast_frame *f;
	fd_set rfds, efds;
	fd_set nrfds, nefds;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);
	for (;;) {
		ms = -1;
		max = -1;
		pthread_mutex_lock(&parking_lock);
		pl = NULL;
		pu = parkinglot;
		gettimeofday(&tv, NULL);
		FD_ZERO(&nrfds);
		FD_ZERO(&nefds);
		while(pu) {
			tms = (tv.tv_sec - pu->start.tv_sec) * 1000 + (tv.tv_usec - pu->start.tv_usec) / 1000;
			if (tms > parkingtime) {
				/* They've been waiting too long, send them back to where they came.  Theoretically they
				   should have their original extensions and such, but we copy to be on the safe side */
				strncpy(pu->chan->exten, pu->exten, sizeof(pu->chan->exten));
				strncpy(pu->chan->context, pu->context, sizeof(pu->chan->context));
				pu->chan->priority = pu->priority;
				/* Start up the PBX, or hang them up */
				if (ast_pbx_start(pu->chan))  {
					ast_log(LOG_WARNING, "Unable to restart the PBX for user on '%s', hanging them up...\n", pu->chan->name);
					ast_hangup(pu->chan);
				}
				/* And take them out of the parking lot */
				if (pl) 
					pl->next = pu->next;
				else
					parkinglot = pu->next;
				pt = pu;
				pu = pu->next;
				free(pt);
			} else if (FD_ISSET(pu->chan->fd, &rfds) || FD_ISSET(pu->chan->fd, &efds)) {
				if (FD_ISSET(pu->chan->fd, &efds))
					pu->chan->exception = 1;
				/* See if they need servicing */
				f = ast_read(pu->chan);
				if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass ==  AST_CONTROL_HANGUP))) {
					/* There's a problem, hang them up*/
					if (option_verbose > 1) 
						ast_verbose(VERBOSE_PREFIX_2 "%s got tired of being parked\n", pu->chan->name);
					ast_hangup(pu->chan);
					/* And take them out of the parking lot */
					if (pl) 
						pl->next = pu->next;
					else
						parkinglot = pu->next;
					pt = pu;
					pu = pu->next;
					free(pt);
				} else {
					/* XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
					ast_frfree(f);
					goto std;	/* XXX Ick: jumping into an else statement??? XXX */
				}
			} else  {
				/* Keep this one for next one */
std:			FD_SET(pu->chan->fd, &nrfds);
				FD_SET(pu->chan->fd, &nefds);
				/* Keep track of our longest wait */
				if ((tms < ms) || (ms < 0))
					ms = tms;
				if (pu->chan->fd > max)
					max = pu->chan->fd;
				pl = pu;
				pu = pu->next;
			}
		}
		pthread_mutex_unlock(&parking_lock);
		rfds = nrfds;
		efds = nefds;
		tv.tv_sec = ms / 1000;
		tv.tv_usec = (ms % 1000) * 1000;
		/* Wait for something to happen */
		select(max + 1, &rfds, NULL, &efds, (ms > -1) ? &tv : NULL);
		pthread_testcancel();
	}
	return NULL;	/* Never reached */
}

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
					if (FD_ISSET(o->chan->fd, &efds))
						o->chan->exception = 1;
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
			if (FD_ISSET(in->fd, &efds))
				in->exception = 1;
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
	int len;
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
	for (;;) {
		res = ast_channel_bridge(chan, peer, AST_BRIDGE_DTMF_CHANNEL_1, &f, &who);
		if (res < 0) {
			ast_log(LOG_WARNING, "Bridge failed on channels %s and %s\n", chan->name, peer->name);
			return -1;
		}
		
		if (!f || ((f->frametype == AST_FRAME_CONTROL) && ((f->subclass == AST_CONTROL_HANGUP) || (f->subclass == AST_CONTROL_BUSY) || 
			(f->subclass == AST_CONTROL_CONGESTION)))) {
				res = -1;
				break;
		}
		if ((f->frametype == AST_FRAME_DTMF) && (who == peer) && allowredirect &&
		     (f->subclass == '#')) {
				memset(newext, 0, sizeof(newext));
				ptr = newext;
				len = ast_pbx_longest_extension(chan->context) + 1;
				if (len < ast_pbx_longest_extension("default") + 1)
					len = ast_pbx_longest_extension("default") + 1;
					/* Transfer */
				if ((res=ast_streamfile(peer, "pbx-transfer", chan->language)))
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
				if (!strcmp(newext, parking_ext)) {
					if (!park_call(chan, peer)) {
						/* We return non-zero, but tell the PBX not to hang the channel when
						   the thread dies -- We have to be careful now though.  We are responsible for 
						   hanging up the channel, else it will never be hung up! */
						res=AST_PBX_KEEPALIVE;
						break;
					} else {
						ast_log(LOG_WARNING, "Unable to park call %s\n", chan->name);
					}
					/* XXX Maybe we should have another message here instead of invalid extension XXX */
				} else if (ast_exists_extension(chan, peer->context, newext, 1)) {
					/* Set the channel's new extension, since it exists, using peer context */
					strncpy(chan->exten, newext, sizeof(chan->exten));
					strncpy(chan->context, peer->context, sizeof(chan->context));
					chan->priority = 0;
					ast_frfree(f);
					res=0;
					break;
				} else if (ast_exists_extension(chan, "default", newext, 1)) {
					/* Set the channel's new extension, since it exists, using peer context */
					strncpy(chan->exten, newext, sizeof(chan->exten));
					strncpy(chan->context, "default", sizeof(chan->context));
					chan->priority = 0;
					ast_frfree(f);
					res=0;
					break;
				}
				res = ast_streamfile(peer, "pbx-invalid", chan->language);
				if (res)
					break;
				res = ast_waitstream(peer, AST_DIGIT_ANY);
				ast_stopstream(peer);
				res = 0;
			} else {
#if 1
				ast_log(LOG_DEBUG, "Read from %s (%d,%d)\n", who->name, f->frametype, f->subclass);
#endif
			}
	}
	return res;
}

static int park_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	struct ast_channel *peer=NULL;
	struct parkeduser *pu, *pl=NULL;
	int park;
	if (!data) {
		ast_log(LOG_WARNING, "Park requires an argument (extension number)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	park = atoi((char *)data);
	pthread_mutex_lock(&parking_lock);
	pu = parkinglot;
	while(pu) {
		if (pu->parkingnum == park) {
			if (pl)
				pl->next = pu->next;
			else
				parkinglot = pu->next;
			break;
		}
		pu = pu->next;
	}
	pthread_mutex_unlock(&parking_lock);
	if (pu) {
		peer = pu->chan;
		free(pu);
	}
	if (peer) {
		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		/* This runs sorta backwards, since we give the incoming channel control, as if it
		   were the person called. */
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Channel %s connected to parked call %d\n", chan->name, park);
		res = bridge_call(peer, chan, 1);
		/* Simulate the PBX hanging up */
		if (res != AST_PBX_KEEPALIVE)
			ast_hangup(peer);
		return -1;
	} else {
		/* XXX Play a message XXX */
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Channel %s tried to talk to non-existant parked call %d\n", chan->name, park);
		res = -1;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int dial_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *info, *peers, *timeout, *tech, *number, *rest, *cur;
	struct localuser *outgoing=NULL, *tmp;
	struct ast_channel *peer;
	int to;
	int allowredir=0;
	char numsubst[AST_MAX_EXTENSION];
	char restofit[AST_MAX_EXTENSION];
	char *newnum;
	
	if (!data) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology1/number1&technology2/number2...|optional timeout)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);
	
	/* Parse our arguments XXX Check for failure XXX */
	info = malloc(strlen((char *)data) + AST_MAX_EXTENSION);
	if (!info) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION);
	peers = info;
	if (peers) {
		timeout = strchr(info, '|');
		if (timeout) {
			*timeout = '\0';
			timeout++;
		}
	} else
		timeout = NULL;
	if (!peers || !strlen(peers)) {
		ast_log(LOG_WARNING, "Dial argument takes format (technology1/number1&technology2/number2...|optional timeout)\n");
		goto out;
	}
	
	cur = peers;
	do {
		/* Remember where to start next time */
		rest = strchr(cur, '&');
		if (rest) {
			*rest = 0;
			rest++;
		}
		/* Get a technology/[device:]number pair */
		tech = cur;
		number = strchr(tech, '/');
		if (!number) {
			ast_log(LOG_WARNING, "Dial argument takes format (technology1/[device:]number1&technology2/[device:]number2...|optional timeout)\n");
			goto out;
		}
		*number = '\0';
		number++;
		tmp = malloc(sizeof(struct localuser));
		if (!tmp) {
			ast_log(LOG_WARNING, "Out of memory\n");
			goto out;
		}
		tmp->allowredirect = 1;
		strncpy(numsubst, number, sizeof(numsubst));
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(numsubst, "BYEXTENSION"))) {
			strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit));
			snprintf(newnum, sizeof(numsubst) - (newnum - numsubst), "%s%s", chan->exten,restofit);
			/* By default, if we're dialing by extension, don't permit redirecting */
			tmp->allowredirect = 0;
			if (option_debug)
				ast_log(LOG_DEBUG, "Dialing by extension %s\n", numsubst);
		}
		/* Request the peer */
		tmp->chan = ast_request(tech, chan->nativeformats, numsubst);
		if (!tmp->chan) {
			/* If we can't, just go on to the next call */
			ast_log(LOG_WARNING, "Unable to create channel of type '%s'\n", tech);
			free(tmp);
			cur = rest;
			continue;
		}
		tmp->chan->appl = "AppDial";
		tmp->chan->data = "(Outgoing Line)";
		if (chan->callerid)
			tmp->chan->callerid = strdup(chan->callerid);
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
		cur = rest;
	} while(cur);
	
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
		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		res = bridge_call(chan, peer, allowredir);
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
	int res;
	int x;
	struct ast_context *con;
	char exten[AST_MAX_EXTENSION];
	con = ast_context_find(parking_con);
	if (!con) {
		ast_log(LOG_ERROR, "Parking context '%s' does not exist\n", parking_con);
		return -1;
	}
	for(x=parking_start; x<=parking_stop;x++) {
		snprintf(exten, sizeof(exten), "%d", x);
		ast_add_extension2(con, 1, exten, 1, parkedcall, strdup(exten), free);
	}
	pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	res = ast_register_application(parkedcall, park_exec);
	if (!res)
		res = ast_register_application(app, dial_exec);
	return res;
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

char *key()
{
	return ASTERISK_GPL_KEY;
}
