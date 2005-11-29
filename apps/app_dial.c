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
#include <asterisk/parking.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include <pthread.h>

static char *tdesc = "Dialing Application";

static char *app = "Dial";

static char *synopsis = "Place an call and connect to the current channel";

static char *descrip =
"  Dial(Technology/resource[&Technology2/resource2...][|timeout][|transfer]):\n"
"Requests  one  or more channels and places specified outgoing calls on them.\n"
"As soon as a  channel  answers, the  Dial  app  will  answer the originating\n"
"channel (if it needs to be answered) and will bridge a call with the channel\n"
"which first answered. All other calls placed by the Dial app will be hunp up\n"
"If a timeout is not specified, the Dial  application  will wait indefinitely\n"
"until either one of the  called channels  answers, the user hangs up, or all\n"
"channels return busy or  error. In general,  the dialler will return 0 if it\n"
"was  unable  to  place  the  call, or the timeout expired.  However, if  all\n"
"channels were busy, and there exists an extension with priority n+101 (where\n"
"n is the priority of  the  dialler  instance), then  it  will  be  the  next\n"
"executed extension (this allows you to setup different behavior on busy from\n"
"no-answer).\n"
"  This application returns -1 if the originating channel hangs up, or if the\n"
"call is bridged and  either of the parties in the bridge terminate the call.\n"
"The transfer string may contain  a  't' to  allow the called user transfer a\n"
"call or 'T' to allow the calling user to transfer the call.\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n";

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

#define MAX 256

static struct ast_channel *wait_for_answer(struct ast_channel *in, struct localuser *outgoing, int *to, int *allowredir)
{
	struct localuser *o;
	int found;
	int numlines;
	int sentringing = 0;
	int numbusies = 0;
	int orig = *to;
	struct ast_frame *f;
	struct ast_channel *peer = NULL;
	struct ast_channel *watchers[MAX];
	int pos;
	int single;
	struct ast_channel *winner;
	
	single = (outgoing && !outgoing->next);
	
	if (single) {
		/* If we are calling a single channel, make them compatible for in-band tone purpose */
		ast_channel_make_compatible(outgoing->chan, in);
	}
	
	while(*to && !peer) {
		o = outgoing;
		found = -1;
		pos = 1;
		numlines = 0;
		watchers[0] = in;
		while(o) {
			/* Keep track of important channels */
			if (o->stillgoing) {
				watchers[pos++] = o->chan;
				found = 1;
			}
			o = o->next;
			numlines++;
		}
		if (found < 0) {
			if (numlines == numbusies) {
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_2 "Everyone is busy at this time\n");
				/* See if there is a special busy message */
				if (ast_exists_extension(in, in->context, in->exten, in->priority + 101, in->callerid)) 
					in->priority+=100;
			} else {
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_2 "No one is available to answer at this time\n");
			}
			*to = 0;
			return NULL;
		}
		winner = ast_waitfor_n(watchers, pos, to);
		o = outgoing;
		while(o) {
			if (o->chan == winner) {
				f = ast_read(winner);
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
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							numbusies++;
							break;
						case AST_CONTROL_CONGESTION:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
							o->stillgoing = 0;
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							numbusies++;
							break;
						case AST_CONTROL_RINGING:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", o->chan->name);
							if (!sentringing) {
								ast_indicate(in, AST_CONTROL_RINGING);
								sentringing++;
							}
							break;
						case AST_CONTROL_OFFHOOK:
							/* Ignore going off hook */
							break;
						default:
							ast_log(LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
						}
					} else if (single && (f->frametype == AST_FRAME_VOICE)) {
						if (ast_write(in, f)) 
							ast_log(LOG_WARNING, "Unable to forward frame\n");
					} else if (single && (f->frametype == AST_FRAME_IMAGE)) {
						if (ast_write(in, f))
							ast_log(LOG_WARNING, "Unable to forward image\n");
					}
					ast_frfree(f);
				} else {
					o->stillgoing = 0;
				}
			}
			o = o->next;
		}
		if (winner == in) {
			f = ast_read(in);
#if 0
			if (f && (f->frametype != AST_FRAME_VOICE))
					printf("Frame type: %d, %d\n", f->frametype, f->subclass);
			else if (!f || (f->frametype != AST_FRAME_VOICE))
				printf("Hangup received on %s\n", in->name);
#endif
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to=-1;
				return NULL;
			}
		}
		if (!*to && (option_verbose > 2))
			ast_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
	}
	return peer;
	
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
	char *transfer = NULL;
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
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION-1);
	peers = info;
	if (peers) {
		timeout = strchr(info, '|');
		if (timeout) {
			*timeout = '\0';
			timeout++;
			transfer = strchr(timeout, '|');
			if (transfer) {
				*transfer = '\0';
				transfer++;
			}
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
		if (transfer && (strchr(transfer, 't')))
			tmp->allowredirect = 1;
		else
			tmp->allowredirect = 0;
		strncpy(numsubst, number, sizeof(numsubst)-1);
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(numsubst, "BYEXTENSION"))) {
			strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit)-1);
			snprintf(newnum, sizeof(numsubst) - (newnum - numsubst), "%s%s", chan->exten,restofit);
			if (option_debug)
				ast_log(LOG_DEBUG, "Dialing by extension %s\n", numsubst);
		}
		/* Request the peer */
		tmp->chan = ast_request(tech, chan->nativeformats, numsubst);
		if (!tmp->chan) {
			/* If we can't, just go on to the next call */
			ast_log(LOG_NOTICE, "Unable to create channel of type '%s'\n", tech);
			if (chan->cdr)
				ast_cdr_busy(chan->cdr);
			free(tmp);
			cur = rest;
			continue;
		}
		tmp->chan->appl = "AppDial";
		tmp->chan->data = "(Outgoing Line)";
		tmp->chan->whentohangup = 0;
		if (tmp->chan->callerid)
			free(tmp->chan->callerid);
		if (chan->callerid)
			tmp->chan->callerid = strdup(chan->callerid);
		else
			tmp->chan->callerid = NULL;
		/* Presense of ADSI CPE on outgoing channel follows ours */
		tmp->chan->adsicpe = chan->adsicpe;
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
			cur = rest;
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
	
	if (timeout && strlen(timeout))
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
		/* If appropriate, log that we have a destination channel */
		if (chan->cdr)
			ast_cdr_setdestchan(chan->cdr, peer->name);
		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		if (!strcmp(chan->type,"Zap")) {
			int x = 2;
			ast_channel_setoption(chan,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		}			
		if (!strcmp(peer->type,"Zap")) {
			int x = 2;
			ast_channel_setoption(peer,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		}			
		res = ast_bridge_call(chan, peer, allowredir);
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
	res = ast_register_application(app, dial_exec, synopsis, descrip);
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
