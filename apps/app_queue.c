/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * True call queues with optional send URL on answer
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/say.h>
#include <asterisk/parking.h>
#include <asterisk/musiconhold.h>
#include <asterisk/cli.h>
#include <asterisk/manager.h> /* JDG */
#include <asterisk/config.h>
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

#define DEFAULT_RETRY		5
#define DEFAULT_TIMEOUT		15
#define RECHECK				1		/* Recheck every second to see we we're at the top yet */

static char *tdesc = "True Call Queueing";

static char *app = "Queue";

static char *synopsis = "Queue a call for a call queue";

static char *descrip =
"  Queue(queuename[|options[|URL][|announceoverride]]):\n"
"Queues an incoming call in a particular call queue as defined in queues.conf.\n"
"  This application returns -1 if the originating channel hangs up, or if the\n"
"call is bridged and  either of the parties in the bridge terminate the call.\n"
"Returns 0 if the queue is full, nonexistant, or has no members.\n"
"The option string may contain zero or more of the following characters:\n"
"      't' -- allow the called user transfer the calling user\n"
"      'T' -- to allow the calling user to transfer the call.\n"
"      'd' -- data-quality (modem) call (minimum delay).\n"
"      'H' -- allow caller to hang up by hitting *.\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n"
"  The optionnal URL will be sent to the called party if the channel supports\n"
"it.\n";

/* We define a customer "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct localuser {
	struct ast_channel *chan;
	int stillgoing;
	int allowredirect;
	int ringbackonly;
	int musiconhold;
	int dataquality;
	int allowdisconnect;
	struct localuser *next;
};

LOCAL_USER_DECL;

struct queue_ent {
	struct ast_call_queue *parent;	/* What queue is our parent */
	char moh[80];				/* Name of musiconhold to be used */
	char announce[80];		/* Announcement to play */
	char context[80];		/* Context when user exits queue */
	int pos;					/* Where we are in the queue */
	time_t start;				/* When we started holding */
	struct ast_channel *chan;	/* Our channel */
	struct queue_ent *next;		/* The next queue entry */
};

struct member {
	char tech[80];				/* Technology */
	char loc[256];				/* Location */
	struct member *next;		/* Next member */
};

struct ast_call_queue {
	pthread_mutex_t	lock;	
	char name[80];			/* Name of the queue */
	char moh[80];			/* Name of musiconhold to be used */
	char announce[80];		/* Announcement to play */
	char context[80];		/* Announcement to play */
	int announcetimeout;	/* How often to announce their position */
	int count;				/* How many entries are in the queue */
	int maxlen;				/* Max number of entries in queue */

	int dead;				/* Whether this queue is dead or not */
	int retry;				/* Retry calling everyone after this amount of time */
	int timeout;			/* How long to wait for an answer */

	struct member *members;	/* Member channels to be tried */
	struct queue_ent *head;	/* Start of the actual queue */
	struct ast_call_queue *next;	/* Next call queue */
};

static struct ast_call_queue *queues = NULL;
static pthread_mutex_t qlock = AST_MUTEX_INITIALIZER;


static int join_queue(char *queuename, struct queue_ent *qe)
{
	struct ast_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	ast_pthread_mutex_lock(&qlock);
	q = queues;
	while(q) {
		if (!strcasecmp(q->name, queuename)) {
			/* This is our one */
			ast_pthread_mutex_lock(&q->lock);
			if (q->members && (!q->maxlen || (q->count < q->maxlen))) {
				/* There's space for us, put us at the end */
				prev = NULL;
				cur = q->head;
				while(cur) {
					cur->pos = ++pos;
					prev = cur;
					cur = cur->next;
				}
				if (prev)
					prev->next = qe;
				else
					q->head = qe;
				/* Fix additional pointers and
				  information  */
				qe->next = NULL;
				qe->parent = q;
				qe->pos = ++pos;
				strncpy(qe->moh, q->moh, sizeof(qe->moh));
				strncpy(qe->announce, q->announce, sizeof(qe->announce));
				strncpy(qe->context, q->context, sizeof(qe->context));
				q->count++;
				res = 0;
				manager_event(EVENT_FLAG_CALL, "Join", 
							 	"Channel: %s\r\nQueue: %s\r\nPosition: %d\r\n",
								qe->chan->name, q->name, qe->pos );

			}
			ast_pthread_mutex_unlock(&q->lock);
			break;
		}
		q = q->next;
	}
	ast_pthread_mutex_unlock(&qlock);
	return res;
}

static void free_members(struct ast_call_queue *q)
{
	struct member *curm, *next;
	curm = q->members;
	while(curm) {
		next = curm->next;
		free(curm);
		curm = next;
	}
	q->members = NULL;
}

static void destroy_queue(struct ast_call_queue *q)
{
	struct ast_call_queue *cur, *prev = NULL;
	ast_pthread_mutex_lock(&qlock);
	cur = queues;
	while(cur) {
		if (cur == q) {
			if (prev)
				prev->next = cur->next;
			else
				queues = cur->next;
		} else {
			prev = cur;
		}
		cur = cur->next;
	}
	ast_pthread_mutex_unlock(&qlock);
	free_members(q);
	free(q);
}

static void leave_queue(struct queue_ent *qe)
{
	struct ast_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int pos = 0;
	q = qe->parent;
	if (!q)
		return;
	ast_pthread_mutex_lock(&q->lock);
	/* Take us out of the queue */
	manager_event(EVENT_FLAG_CALL, "Leave",
						 "Channel: %s\r\nQueue: %s\r\n", 
						 qe->chan->name, q->name );
	prev = NULL;
	cur = q->head;
	while(cur) {
		if (cur == qe) {
			q->count--;
			/* Take us out of the queue */
			if (prev)
				prev->next = cur->next;
			else
				q->head = cur->next;
		} else {
			cur->pos = ++pos;
			prev = cur;
		}
		cur = cur->next;
	}
	ast_pthread_mutex_unlock(&q->lock);
	if (q->dead && !q->count) {	
		/* It's dead and nobody is in it, so kill it */
		destroy_queue(q);
	}
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

#define MAX 256

static struct ast_channel *wait_for_answer(struct ast_channel *in, struct localuser *outgoing, int *to, int *allowredir, int *allowdisconnect, char *queue)
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
	struct ast_channel *winner;
		
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
				ast_log(LOG_DEBUG, "Everyone is busy at this time\n");
			} else {
				ast_log(LOG_NOTICE, "No one is answered queue %s\n", queue);
			}
			*to = 0;
			return NULL;
		}
		winner = ast_waitfor_n(watchers, pos, to);
		o = outgoing;
		while(o) {
			if (o->stillgoing && (o->chan->_state == AST_STATE_UP)) {
				if (!peer) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
					peer = o->chan;
					*allowredir = o->allowredirect;
					*allowdisconnect = o->allowdisconnect;
				}
			} else if (o->chan == winner) {
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
								*allowdisconnect = o->allowdisconnect;
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
#if 0
								ast_indicate(in, AST_CONTROL_RINGING);
#endif								
								sentringing++;
							}
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
			if (f && (f->frametype == AST_FRAME_DTMF) && allowdisconnect &&
				(f->subclass == '*')) {
			    if (option_verbose > 3)
				ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
				*to=0;
				return NULL;
			}
		}
		if (!*to && (option_verbose > 2))
			ast_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
	}

	return peer;
	
}

static int wait_our_turn(struct queue_ent *qe)
{
	struct queue_ent *ch;
	int res = 0;
	for (;;) {
		/* Atomically read the parent head */
		pthread_mutex_lock(&qe->parent->lock);
		ch = qe->parent->head;
		pthread_mutex_unlock(&qe->parent->lock);
		/* If we are now at the top of the head, break out */
		if (qe->parent->head == qe)
			break;
		/* Wait a second before checking again */
		res = ast_waitfordigit(qe->chan, RECHECK * 1000);
		if (res)
			break;
	}
	return res;
}

static int try_calling(struct queue_ent *qe, char *options, char *announceoverride, char *url)
{
	struct member *cur;
	struct localuser *outgoing=NULL, *tmp = NULL;
	int to;
	int allowredir=0;
	int allowdisconnect=0;
	char numsubst[AST_MAX_EXTENSION];
	char restofit[AST_MAX_EXTENSION];
	char *newnum;
	struct ast_channel *peer;
	int res = 0, bridge = 0;
	char *announce = NULL;
	/* Hold the lock while we setup the outgoing calls */
	ast_pthread_mutex_lock(&qe->parent->lock);
	cur = qe->parent->members;
	if (strlen(qe->announce))
		announce = qe->announce;
	if (announceoverride && strlen(announceoverride))
		announce = announceoverride;
	while(cur) {
		/* Get a technology/[device:]number pair */
		tmp = malloc(sizeof(struct localuser));
		if (!tmp) {
			ast_log(LOG_WARNING, "Out of memory\n");
			goto out;
		}
		memset(tmp, 0, sizeof(struct localuser));
		if (options) {
			if (strchr(options, 't'))
				tmp->allowredirect = 1;
			if (strchr(options, 'r'))
				tmp->ringbackonly = 1;
			if (strchr(options, 'm'))
				tmp->musiconhold = 1;
			if (strchr(options, 'd'))
				tmp->dataquality = 1;
			if (strchr(options, 'H'))
				tmp->allowdisconnect = 1;
		}
		if (url) {
			ast_log(LOG_DEBUG, "Queue with URL=%s_\n", url);
		} else 
			ast_log(LOG_DEBUG, "Simple queue (no URL)\n");

		strncpy(numsubst, cur->loc, sizeof(numsubst)-1);
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(numsubst, "BYEXTENSION"))) {
			strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit)-1);
			snprintf(newnum, sizeof(numsubst) - (newnum - numsubst), "%s%s", qe->chan->exten,restofit);
			if (option_debug)
				ast_log(LOG_DEBUG, "Dialing by extension %s\n", numsubst);
		}
		/* Request the peer */
		tmp->chan = ast_request(cur->tech, qe->chan->nativeformats, numsubst);
		if (!tmp->chan) {
			/* If we can't, just go on to the next call */
#if 0
			ast_log(LOG_NOTICE, "Unable to create channel of type '%s'\n", cur->tech);
#endif			
			if (qe->chan->cdr)
				ast_cdr_busy(qe->chan->cdr);
			free(tmp);
			cur = cur->next;
			continue;
		}
#if 0		
		/* Don't honor call forwarding on a queue! */
		if (strlen(tmp->chan->call_forward)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Forwarding call to '%s@%s'\n", tmp->chan->call_forward, tmp->chan->context);
			/* Setup parameters */
			strncpy(chan->exten, tmp->chan->call_forward, sizeof(chan->exten));
			strncpy(chan->context, tmp->chan->context, sizeof(chan->context));
			chan->priority = 0;
			to = 0;
			ast_hangup(tmp->chan);
			free(tmp);
			cur = rest;
			break;
		}
#endif		
		tmp->chan->appl = "AppQueue";
		tmp->chan->data = "(Outgoing Line)";
		tmp->chan->whentohangup = 0;
		if (tmp->chan->callerid)
			free(tmp->chan->callerid);
		if (tmp->chan->ani)
			free(tmp->chan->ani);
		if (qe->chan->callerid)
			tmp->chan->callerid = strdup(qe->chan->callerid);
		else
			tmp->chan->callerid = NULL;
		if (qe->chan->ani)
			tmp->chan->ani = strdup(qe->chan->ani);
		else
			tmp->chan->ani = NULL;
		/* Presense of ADSI CPE on outgoing channel follows ours */
		tmp->chan->adsicpe = qe->chan->adsicpe;
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
			cur = cur->next;
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
		/* If this line is up, don't try anybody else */
		if (outgoing->chan->_state == AST_STATE_UP)
			break;

		cur = cur->next;
	}
	if (qe->parent->timeout)
		to = qe->parent->timeout * 1000;
	else
		to = -1;
	ast_pthread_mutex_unlock(&qe->parent->lock);
	
	peer = wait_for_answer(qe->chan, outgoing, &to, &allowredir, &allowdisconnect, qe->parent->name);
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
		/* Stop music on hold */
		ast_moh_stop(qe->chan);
		outgoing = NULL;
		if (announce) {
			int res2;
			res2 = ast_streamfile(peer, announce, peer->language);
			/* XXX Need a function to wait on *both* streams XXX */
			if (!res2)
				res2 = ast_waitstream(peer, "");
			else
				res2 = 0;
			if (res2) {
				/* Agent must have hung up */
				ast_log(LOG_WARNING, "Agent on %s hungup on the customer.  They're going to be pissed.\n", peer->name);
				ast_hangup(peer);
				return -1;
			}
		}
		/* If appropriate, log that we have a destination channel */
		if (qe->chan->cdr)
			ast_cdr_setdestchan(qe->chan->cdr, peer->name);
		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(qe->chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", qe->chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		if (!strcmp(qe->chan->type,"Zap")) {
			int x = 2;
			if (tmp->dataquality) x = 0;
			ast_channel_setoption(qe->chan,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		}			
		if (!strcmp(peer->type,"Zap")) {
			int x = 2;
			if (tmp->dataquality) x = 0;
			ast_channel_setoption(peer,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		}
		/* Drop out of the queue at this point, to prepare for next caller */
		leave_queue(qe);			
 		/* JDG: sendurl */
 		if( url && strlen(url) && ast_channel_supports_html(peer) ) {
 			ast_log(LOG_DEBUG, "app_queue: sendurl=%s.\n", url);
 			ast_channel_sendurl( peer, url );
 		} /* /JDG */
		bridge = ast_bridge_call(qe->chan, peer, allowredir, allowdisconnect);
		ast_hangup(peer);
		if( bridge == 0 ) res=1; /* JDG: bridge successfull, leave app_queue */
		else res = bridge; /* bridge error, stay in the queue */
	}	
out:
	hanguptree(outgoing, NULL);
	return res;
}

static int wait_a_bit(struct queue_ent *qe)
{
	int retrywait;
	/* Hold the lock while we setup the outgoing calls */
	ast_pthread_mutex_lock(&qe->parent->lock);
	retrywait = qe->parent->retry * 1000;
	ast_pthread_mutex_unlock(&qe->parent->lock);
	return ast_waitfordigit(qe->chan, retrywait);
}

static int valid_exit(struct queue_ent *qe, char digit)
{
	char tmp[2];
	if (!strlen(qe->context))
		return 0;
	tmp[0] = digit;
	tmp[1] = '\0';
	if (ast_exists_extension(qe->chan, qe->context, tmp, 1, qe->chan->callerid)) {
		strncpy(qe->chan->context, qe->context, sizeof(qe->chan->context) - 1);
		strncpy(qe->chan->exten, tmp, sizeof(qe->chan->exten) - 1);
		qe->chan->priority = 0;
		return 1;
	}
	return 0;
}

static int queue_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *queuename;
	char info[512];
	char *options = NULL;
	char *url = NULL;
	char *announceoverride = NULL;
	
	/* Our queue entry */
	struct queue_ent qe;
	
	if (!data) {
		ast_log(LOG_WARNING, "Queue requires an argument (queuename|optional timeout|optional URL)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);
	
	/* Parse our arguments XXX Check for failure XXX */
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION-1);
	queuename = info;
	if (queuename) {
		options = strchr(queuename, '|');
		if (options) {
			*options = '\0';
			options++;
			url = strchr(options, '|');
			if (url) {
				*url = '\0';
				url++;
				announceoverride = strchr(url, '|');
				if (announceoverride) {
					*announceoverride = '\0';
					announceoverride++;
				}
			}
		}
	}
	printf("queue: %s, options: %s, url: %s, announce: %s\n",
		queuename, options, url, announceoverride);
	/* Setup our queue entry */
	memset(&qe, 0, sizeof(qe));
	qe.chan = chan;
	qe.start = time(NULL);
	if (!join_queue(queuename, &qe)) {
		/* Start music on hold */
		ast_moh_start(chan, qe.moh);
		for (;;) {
			res = wait_our_turn(&qe);
			/* If they hungup, return immediately */
			if (res < 0) {
				if (option_verbose > 2) {
					ast_verbose(VERBOSE_PREFIX_3 "User disconnected while waiting their turn\n");
					res = -1;
				}
				break;
			}
			if (!res)
				break;
			if (valid_exit(&qe, res))
				break;
		}
		if (!res) {
			for (;;) {
				res = try_calling(&qe, options, announceoverride, url);
				if (res)
					break;
				res = wait_a_bit(&qe);
				if (res < 0) {
					if (option_verbose > 2) {
						ast_verbose(VERBOSE_PREFIX_3 "User disconnected when they almost made it\n");
						res = -1;
					}
					break;
				}
				if (res && valid_exit(&qe, res))
					break;
			}
		}
		/* Don't allow return code > 0 */
		if (res > 0)
			res = 0;	
		ast_moh_stop(chan);
		leave_queue(&qe);
	} else {
		ast_log(LOG_WARNING, "Unable to join queue '%s'\n", queuename);
		res =  0;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static void reload_queues(void)
{
	struct ast_call_queue *q, *ql, *qn;
	struct ast_config *cfg;
	char *cat, *tmp;
	struct ast_variable *var;
	struct member *prev, *cur;
	int new;
	cfg = ast_load("queues.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "No call queueing config file, so no call queues\n");
		return;
	}
	ast_pthread_mutex_lock(&qlock);
	/* Mark all queues as dead for the moment */
	q = queues;
	while(q) {
		q->dead = 1;
		q = q->next;
	}
	/* Chug through config file */
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			/* Look for an existing one */
			q = queues;
			while(q) {
				if (!strcmp(q->name, cat))
					break;
				q = q->next;
			}
			if (!q) {
				/* Make one then */
				q = malloc(sizeof(struct ast_call_queue));
				if (q) {
					/* Initialize it */
					memset(q, 0, sizeof(struct ast_call_queue));
					strncpy(q->name, cat, sizeof(q->name));
					new = 1;
				} else new = 0;
			} else
					new = 0;
			if (q) {
				if (!new) 
					ast_pthread_mutex_lock(&q->lock);
				/* Re-initialize the queue */
				q->dead = 0;
				q->retry = 0;
				q->timeout = -1;
				q->maxlen = 0;
				free_members(q);
				strcpy(q->moh, "");
				strcpy(q->announce, "");
				strcpy(q->context, "");
				prev = NULL;
				var = ast_variable_browse(cfg, cat);
				while(var) {
					if (!strcasecmp(var->name, "member")) {
						/* Add a new member */
						cur = malloc(sizeof(struct member));
						if (cur) {
							memset(cur, 0, sizeof(struct member));
							strncpy(cur->tech, var->value, sizeof(cur->tech) - 1);
							if ((tmp = strchr(cur->tech, '/')))
								*tmp = '\0';
							if ((tmp = strchr(var->value, '/'))) {
								tmp++;
								strncpy(cur->loc, tmp, sizeof(cur->loc) - 1);
							} else
								ast_log(LOG_WARNING, "No location at line %d of queue.conf\n", var->lineno);
							if (prev)
								prev->next = cur;
							else
								q->members = cur;
							prev = cur;
						}
					} else if (!strcasecmp(var->name, "music")) {
						strncpy(q->moh, var->value, sizeof(q->moh) - 1);
					} else if (!strcasecmp(var->name, "announce")) {
						strncpy(q->announce, var->value, sizeof(q->announce) - 1);
					} else if (!strcasecmp(var->name, "context")) {
						strncpy(q->context, var->value, sizeof(q->context) - 1);
					} else if (!strcasecmp(var->name, "timeout")) {
						q->timeout = atoi(var->value);
					} else if (!strcasecmp(var->name, "retry")) {
						q->retry = atoi(var->value);
					} else if (!strcasecmp(var->name, "maxlen")) {
						q->maxlen = atoi(var->value);
					} else {
						ast_log(LOG_WARNING, "Unknown keyword in queue '%s': %s at line %d of queue.conf\n", cat, var->name, var->lineno);
					}
					var = var->next;
				}
				if (q->retry < 1)
					q->retry = DEFAULT_RETRY;
				if (q->timeout < 0)
					q->timeout = DEFAULT_TIMEOUT;
				if (q->maxlen < 0)
					q->maxlen = 0;
				if (!new) 
					ast_pthread_mutex_unlock(&q->lock);
				if (new) {
					q->next = queues;
					queues = q;
				}
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	q = queues;
	ql = NULL;
	while(q) {
		qn = q->next;
		if (q->dead) {
			if (ql)
				ql->next = q->next;
			else
				queues = q->next;
			if (!q->count) {
				free(q);
			} else
				ast_log(LOG_WARNING, "XXX Leaking a litttle memory :( XXX\n");
		} else
			ql = q;
		q = qn;
	}
	ast_pthread_mutex_unlock(&qlock);
}

static int queues_show(int fd, int argc, char **argv)
{
	struct ast_call_queue *q;
	struct queue_ent *qe;
	struct member *mem;
	int pos;
	time_t now;
	char max[80];
	
	time(&now);
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	q = queues;
	if (!q) {	
		ast_cli(fd, "No queues.\n");
		return RESULT_SUCCESS;
	}
	while(q) {
		ast_pthread_mutex_lock(&q->lock);
		if (q->maxlen)
			snprintf(max, sizeof(max), "%d", q->maxlen);
		else
			strcpy(max, "unlimited");
		ast_cli(fd, "%-12.12s has %d calls (max %s)\n", q->name, q->count, max);
		if (q->members) {
			ast_cli(fd, "   Members: \n");
			for (mem = q->members; mem; mem = mem->next) 
				ast_cli(fd, "      %s/%s\n", mem->tech, mem->loc);
		} else
			ast_cli(fd, "   No Members\n");
		if (q->head) {
			pos = 1;
			ast_cli(fd, "   Callers: \n");
			for (qe = q->head; qe; qe = qe->next) 
				ast_cli(fd, "      %d. %s (wait: %d:%02.2d)\n", pos++, qe->chan->name,
								(now - qe->start) / 60, (now - qe->start) % 60);
		} else
			ast_cli(fd, "   No Callers\n");
		ast_cli(fd, "\n");
		ast_pthread_mutex_unlock(&q->lock);
		q = q->next;
	}
	return RESULT_SUCCESS;
}

/* JDG: callback to display queues status in manager */
static int manager_queues_show( struct mansession *s, struct message *m )
{
	char *a[] = { "show", "queues" };
	return queues_show( s->fd, 2, a );
} /* /JDG */

static char show_queues_usage[] = 
"Usage: show queues\n"
"       Provides summary information on call queues.\n";

static struct ast_cli_entry cli_show_queues = {
	{ "show", "queues", NULL }, queues_show, 
	"Show status of queues", show_queues_usage, NULL };

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&cli_show_queues);
	ast_manager_unregister( "Queues" );
	return ast_unregister_application(app);
}

int load_module(void)
{
	int res;
	res = ast_register_application(app, queue_exec, synopsis, descrip);
	if (!res) {
		ast_cli_register(&cli_show_queues);
		ast_manager_register( "Queues", 0, manager_queues_show, "Queues" );
	}
	reload_queues();
	return res;
}

int reload(void)
{
	reload_queues();
	return 0;
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
