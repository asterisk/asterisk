/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * True call queues with optional send URL on answer
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * 2004-11-25: Persistent Dynamic Members added by:
 *             NetNation Communications (www.netnation.com)
 *             Kevin Lindsay <kevinl@netnation.com>
 * 
 *             Each dynamic agent in each queue is now stored in the astdb.
 *             When asterisk is restarted, each agent will be automatically
 *             readded into their recorded queues. This feature can be
 *             configured with the 'peristent_members=<1|0>' KVP under the
 *             '[general]' group in queues.conf. The default is on.
 * 
 * 2004-06-04: Priorities in queues added by inAccess Networks (work funded by Hellas On Line (HOL) www.hol.gr).
 *
 * These features added by David C. Troy <dave@toad.net>:
 *    - Per-queue holdtime calculation
 *    - Estimated holdtime announcement
 *    - Position announcement
 *    - Abandoned/completed call counters
 *    - Failout timer passed as optional app parameter
 *    - Optional monitoring of calls, started when call is answered
 *
 * Patch Version 1.07 2003-12-24 01
 *
 * Added servicelevel statistic by Michiel Betel <michiel@betel.nl>
 * Added Priority jumping code for adding and removing queue members by Jonathan Stanton <asterisk@doilooklikeicare.com>
 *
 * Fixed to work with CVS as of 2004-02-25 and released as 1.07a
 * by Matthew Enger <m.enger@xi.com.au>
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
#include <asterisk/features.h>
#include <asterisk/musiconhold.h>
#include <asterisk/cli.h>
#include <asterisk/manager.h>
#include <asterisk/config.h>
#include <asterisk/monitor.h>
#include <asterisk/utils.h>
#include <asterisk/causes.h>
#include <asterisk/astdb.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "../astconf.h"

#define QUEUE_STRATEGY_RINGALL		0
#define QUEUE_STRATEGY_ROUNDROBIN	1
#define QUEUE_STRATEGY_LEASTRECENT	2
#define QUEUE_STRATEGY_FEWESTCALLS	3
#define QUEUE_STRATEGY_RANDOM		4
#define QUEUE_STRATEGY_RRMEMORY		5

static struct strategy {
	int strategy;
	char *name;
} strategies[] = {
	{ QUEUE_STRATEGY_RINGALL, "ringall" },
	{ QUEUE_STRATEGY_ROUNDROBIN, "roundrobin" },
	{ QUEUE_STRATEGY_LEASTRECENT, "leastrecent" },
	{ QUEUE_STRATEGY_FEWESTCALLS, "fewestcalls" },
	{ QUEUE_STRATEGY_RANDOM, "random" },
	{ QUEUE_STRATEGY_RRMEMORY, "rrmemory" },
};

#define DEFAULT_RETRY		5
#define DEFAULT_TIMEOUT		15
#define RECHECK				1		/* Recheck every second to see we we're at the top yet */

#define	RES_OKAY	0			/* Action completed */
#define	RES_EXISTS	(-1)		/* Entry already exists */
#define	RES_OUTOFMEMORY	(-2)	/* Out of memory */
#define	RES_NOSUCHQUEUE	(-3)	/* No such queue */

static char *tdesc = "True Call Queueing";

static char *app = "Queue";

static char *synopsis = "Queue a call for a call queue";

static char *descrip =
"  Queue(queuename[|options[|URL][|announceoverride][|timeout]]):\n"
"Queues an incoming call in a particular call queue as defined in queues.conf.\n"
"  This application returns -1 if the originating channel hangs up, or if the\n"
"call is bridged and  either of the parties in the bridge terminate the call.\n"
"Returns 0 if the queue is full, nonexistant, or has no members.\n"
"The option string may contain zero or more of the following characters:\n"
"      't' -- allow the called user transfer the calling user\n"
"      'T' -- to allow the calling user to transfer the call.\n"
"      'd' -- data-quality (modem) call (minimum delay).\n"
"      'h' -- allow callee to hang up by hitting *.\n"
"      'H' -- allow caller to hang up by hitting *.\n"
"      'n' -- no retries on the timeout; will exit this application and go to the next step.\n"
"      'r' -- ring instead of playing MOH\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n"
"  The optional URL will be sent to the called party if the channel supports\n"
"it.\n"
"  The timeout will cause the queue to fail out after a specified number of\n"
"seconds, checked between each queues.conf 'timeout' and 'retry' cycle.\n";

/* PHM 06/26/03 */
static char *app_aqm = "AddQueueMember" ;
static char *app_aqm_synopsis = "Dynamically adds queue members" ;
static char *app_aqm_descrip =
"   AddQueueMember(queuename[|interface[|penalty]]):\n"
"Dynamically adds interface to an existing queue.\n"
"If the interface is already in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"Returns -1 if there is an error.\n"
"Example: AddQueueMember(techsupport|SIP/3000)\n"
"";

static char *app_rqm = "RemoveQueueMember" ;
static char *app_rqm_synopsis = "Dynamically removes queue members" ;
static char *app_rqm_descrip =
"   RemoveQueueMember(queuename[|interface]):\n"
"Dynamically removes interface to an existing queue\n"
"If the interface is NOT in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"Returns -1 if there is an error.\n"
"Example: RemoveQueueMember(techsupport|SIP/3000)\n"
"";

/* Persistent Members astdb family */
static const char *pm_family = "/Queue/PersistentMembers";
/* The maximum lengh of each persistent member queue database entry */
#define PM_MAX_LEN 2048
/* queues.conf [general] option */
static int queue_persistent_members = 0;

#define QUEUE_FLAG_RINGBACKONLY		(1 << 0)
#define QUEUE_FLAG_MUSICONHOLD		(1 << 1)
#define QUEUE_FLAG_DATAQUALITY		(1 << 2)
#define QUEUE_FLAG_REDIR_IN		(1 << 3)
#define QUEUE_FLAG_REDIR_OUT		(1 << 4)
#define QUEUE_FLAG_DISCON_IN		(1 << 5)
#define QUEUE_FLAG_DISCON_OUT		(1 << 6)
#define QUEUE_FLAG_MONJOIN		(1 << 7)	/* Should we join the two files when we are done with the call */
#define QUEUE_FLAG_DEAD			(1 << 8)	/* Whether the queue is dead or not */
#define QUEUE_FLAG_JOINEMPTY		(1 << 9)	/* Do we care if the queue has no members? */
#define QUEUE_FLAG_EVENTWHENCALLED	(1 << 10)	/* Generate an event when the agent is called (before pickup) */
#define QUEUE_FLAG_LEAVEWHENEMPTY	(1 << 11)	/* If all agents leave the queue, remove callers from the queue */
#define QUEUE_FLAG_REPORTHOLDTIME	(1 << 12)	/* Should we report caller hold time to answering member? */
#define QUEUE_FLAG_WRAPPED		(1 << 13)	/* Round Robin - wrapped around? */

/* We define a customer "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct localuser {
	struct ast_channel *chan;
	char interface[256];
	int stillgoing;
	int metric;
	int oldstatus;
	int flags;			/* flag bits */
	time_t lastcall;
	struct member *member;
	struct localuser *next;
};

LOCAL_USER_DECL;

struct queue_ent {
	struct ast_call_queue *parent;	/* What queue is our parent */
	char moh[80];			/* Name of musiconhold to be used */
	char announce[80];		/* Announcement to play for member when call is answered */
	char context[80];		/* Context when user exits queue */
	int pos;					/* Where we are in the queue */
	int prio;					/* Our priority */
	int last_pos_said;              /* Last position we told the user */
	time_t last_pos;                /* Last time we told the user their position */
	int opos;			/* Where we started in the queue */
	int handled;			/* Whether our call was handled */
	time_t start;			/* When we started holding */
	time_t expire;			/* When this entry should expire (time out of queue) */
	struct ast_channel *chan;	/* Our channel */
	struct queue_ent *next;		/* The next queue entry */
};

struct member {
	char interface[80];			/* Technology/Location */
	int penalty;				/* Are we a last resort? */
	int calls;					/* Number of calls serviced by this member */
	int dynamic;				/* Are we dynamically added? */
	int status;					/* Status of queue member */
	time_t lastcall;			/* When last successful call was hungup */
	struct member *next;		/* Next member */
};

struct ast_call_queue {
	ast_mutex_t	lock;	
	char name[80];			/* Name of the queue */
	char moh[80];			/* Name of musiconhold to be used */
	char announce[80];		/* Announcement to play when call is answered */
	char context[80];		/* Context for this queue */
	int flags;			/* flag bits */
	int strategy;			/* Queueing strategy */
	int announcefrequency;          /* How often to announce their position */
	int roundingseconds;            /* How many seconds do we round to? */
	int announceholdtime;           /* When to announce holdtime: 0 = never, -1 = every announcement, 1 = only once */
	int holdtime;                   /* Current avg holdtime for this queue, based on recursive boxcar filter */
	int callscompleted;             /* Number of queue calls completed */
	int callsabandoned;             /* Number of queue calls abandoned */
	int servicelevel;               /* seconds setting for servicelevel*/
	int callscompletedinsl;         /* Number of queue calls answererd with servicelevel*/
	char monfmt[8];                 /* Format to use when recording calls */
	char sound_next[80];            /* Sound file: "Your call is now first in line" (def. queue-youarenext) */
	char sound_thereare[80];        /* Sound file: "There are currently" (def. queue-thereare) */
	char sound_calls[80];           /* Sound file: "calls waiting to speak to a representative." (def. queue-callswaiting)*/
	char sound_holdtime[80];        /* Sound file: "The current estimated total holdtime is" (def. queue-holdtime) */
	char sound_minutes[80];         /* Sound file: "minutes." (def. queue-minutes) */
	char sound_lessthan[80];        /* Sound file: "less-than" (def. queue-lessthan) */
	char sound_seconds[80];         /* Sound file: "seconds." (def. queue-seconds) */
	char sound_thanks[80];          /* Sound file: "Thank you for your patience." (def. queue-thankyou) */
	char sound_reporthold[80];	/* Sound file: "Hold time" (def. queue-reporthold) */

	int count;			/* How many entries are in the queue */
	int maxlen;			/* Max number of entries in queue */
	int wrapuptime;		/* Wrapup Time */

	int retry;			/* Retry calling everyone after this amount of time */
	int timeout;			/* How long to wait for an answer */
	
	/* Queue strategy things */
	
	int rrpos;			/* Round Robin - position */
	int memberdelay;		/* Seconds to delay connecting member to caller */

	struct member *members;		/* Member channels to be tried */
	struct queue_ent *head;		/* Start of the actual queue */
	struct ast_call_queue *next;	/* Next call queue */
};

static struct ast_call_queue *queues = NULL;
AST_MUTEX_DEFINE_STATIC(qlock);

static char *int2strat(int strategy)
{
	int x;
	for (x=0;x<sizeof(strategies) / sizeof(strategies[0]);x++) {
		if (strategy == strategies[x].strategy)
			return strategies[x].name;
	}
	return "<unknown>";
}

static int strat2int(char *strategy)
{
	int x;
	for (x=0;x<sizeof(strategies) / sizeof(strategies[0]);x++) {
		if (!strcasecmp(strategy, strategies[x].name))
			return strategies[x].strategy;
	}
	return -1;
}

/* Insert the 'new' entry after the 'prev' entry of queue 'q' */
static inline void insert_entry(struct ast_call_queue *q, 
					struct queue_ent *prev, struct queue_ent *new, int *pos)
{
	struct queue_ent *cur;

	if (!q || !new)
		return;
	if (prev) {
		cur = prev->next;
		prev->next = new;
	} else {
		cur = q->head;
		q->head = new;
	}
	new->next = cur;
	new->parent = q;
	new->pos = ++(*pos);
	new->opos = *pos;
}

static int has_no_members(struct ast_call_queue *q)
{
	struct member *member;
	int empty = 1;
	member = q->members;
	while(empty && member) {
		switch(member->status) {
		case AST_DEVICE_UNAVAILABLE:
		case AST_DEVICE_INVALID:
			/* Not logged on, etc */
			break;
		default:
			/* Not empty */
			empty = 0;
		}
		member = member->next;
	}
	return empty;
}

struct statechange {
	int state;
	char dev[0];
};

static void *changethread(void *data)
{
	struct ast_call_queue *q;
	struct statechange *sc = data;
	struct member *cur;
	char *loc;
	loc = strchr(sc->dev, '/');
	if (loc) {
		*loc = '\0';
		loc++;
	} else {
		ast_log(LOG_WARNING, "Can't change device with no technology!\n");
		free(sc);
		return NULL;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Device '%s/%s' changed to state '%d'\n", sc->dev, loc, sc->state);
	ast_mutex_lock(&qlock);
	for (q = queues; q; q = q->next) {
		ast_mutex_lock(&q->lock);
		cur = q->members;
		while(cur) {
			if (!strcasecmp(sc->dev, cur->interface)) {
				if (cur->status != sc->state) {
					cur->status = sc->state;
					manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %ld\r\n"
						"Status: %d\r\n",
					q->name, cur->interface, cur->dynamic ? "dynamic" : "static",
					cur->penalty, cur->calls, cur->lastcall, cur->status);
				}
			}
			cur = cur->next;
		}
		ast_mutex_unlock(&q->lock);
	}
	ast_mutex_unlock(&qlock);
	ast_log(LOG_DEBUG, "Device '%s/%s' changed to state '%d'\n", sc->dev, loc, sc->state);
	free(sc);
	return NULL;
}

static int statechange_queue(const char *dev, int state, void *ign)
{
	/* Avoid potential for deadlocks by spawning a new thread to handle
	   the event */
	struct statechange *sc;
	pthread_t t;
	pthread_attr_t attr;
	sc = malloc(sizeof(struct statechange) + strlen(dev) + 1);
	if (sc) {
		sc->state = state;
		strcpy(sc->dev, dev);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&t, &attr, changethread, sc)) {
			ast_log(LOG_WARNING, "Failed to create update thread!\n");
			free(sc);
		}
	}
	return 0;
}

static int join_queue(char *queuename, struct queue_ent *qe)
{
	struct ast_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	int inserted = 0;

	ast_mutex_lock(&qlock);
	for (q = queues; q; q = q->next) {
		if (!strcasecmp(q->name, queuename)) {
			/* This is our one */
			ast_mutex_lock(&q->lock);
			if ((!has_no_members(q) || ast_test_flag(q, QUEUE_FLAG_JOINEMPTY)) && (!q->maxlen || (q->count < q->maxlen))) {
				/* There's space for us, put us at the right position inside
				 * the queue. 
				 * Take into account the priority of the calling user */
				inserted = 0;
				prev = NULL;
				cur = q->head;
				while(cur) {
					/* We have higher priority than the current user, enter
					 * before him, after all the other users with priority
					 * higher or equal to our priority. */
					if ((!inserted) && (qe->prio > cur->prio)) {
						insert_entry(q, prev, qe, &pos);
						inserted = 1;
					}
					cur->pos = ++pos;
					prev = cur;
					cur = cur->next;
				}
				/* No luck, join at the end of the queue */
				if (!inserted)
					insert_entry(q, prev, qe, &pos);
				strncpy(qe->moh, q->moh, sizeof(qe->moh) - 1);
				strncpy(qe->announce, q->announce, sizeof(qe->announce) - 1);
				strncpy(qe->context, q->context, sizeof(qe->context) - 1);
				q->count++;
				res = 0;
				manager_event(EVENT_FLAG_CALL, "Join", 
					"Channel: %s\r\nCallerID: %s\r\nCallerIDName: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\n",
					qe->chan->name, 
					qe->chan->cid.cid_num ? qe->chan->cid.cid_num : "unknown",
					qe->chan->cid.cid_name ? qe->chan->cid.cid_name : "unknown",
					q->name, qe->pos, q->count );
#if 0
ast_log(LOG_NOTICE, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, qe->chan->name, qe->pos );
#endif
			}
			ast_mutex_unlock(&q->lock);
			break;
		}
	}
	ast_mutex_unlock(&qlock);
	return res;
}

static void free_members(struct ast_call_queue *q, int all)
{
	/* Free non-dynamic members */
	struct member *curm, *next, *prev;
	curm = q->members;
	prev = NULL;
	while(curm) {
		next = curm->next;
		if (all || !curm->dynamic) {
			if (prev)
				prev->next = next;
			else
				q->members = next;
			free(curm);
		} else 
			prev = curm;
		curm = next;
	}
}

static void destroy_queue(struct ast_call_queue *q)
{
	struct ast_call_queue *cur, *prev = NULL;
	ast_mutex_lock(&qlock);
	for (cur = queues; cur; cur = cur->next) {
		if (cur == q) {
			if (prev)
				prev->next = cur->next;
			else
				queues = cur->next;
		} else {
			prev = cur;
		}
	}
	ast_mutex_unlock(&qlock);
	free_members(q, 1);
        ast_mutex_destroy(&q->lock);
	free(q);
}

static int play_file(struct ast_channel *chan, char *filename)
{
	int res;

	ast_stopstream(chan);
	res = ast_streamfile(chan, filename, chan->language);

	if (!res)
		res = ast_waitstream(chan, "");
	else
		res = 0;

	if (res) {
		ast_log(LOG_WARNING, "ast_streamfile failed on %s \n", chan->name);
		res = 0;
	}
	ast_stopstream(chan);

	return res;
}

static int say_position(struct queue_ent *qe)
{
	int res = 0, avgholdmins, avgholdsecs;
	time_t now;

	/* Check to see if this is ludicrous -- if we just announced position, don't do it again*/
	time(&now);
	if ( (now - qe->last_pos) < 15 )
		return -1;

	/* If either our position has changed, or we are over the freq timer, say position */
	if ( (qe->last_pos_said == qe->pos) && ((now - qe->last_pos) < qe->parent->announcefrequency) )
		return -1;

	ast_moh_stop(qe->chan);
	/* Say we're next, if we are */
	if (qe->pos == 1) {
		res += play_file(qe->chan, qe->parent->sound_next);
		goto posout;
	} else {
		res += play_file(qe->chan, qe->parent->sound_thereare);
		res += ast_say_number(qe->chan, qe->pos, AST_DIGIT_ANY, qe->chan->language, (char *) NULL); /* Needs gender */
		res += play_file(qe->chan, qe->parent->sound_calls);
	}
	/* Round hold time to nearest minute */
	avgholdmins = abs(( (qe->parent->holdtime + 30) - (now - qe->start) ) / 60);

	/* If they have specified a rounding then round the seconds as well */
	if(qe->parent->roundingseconds) {
		avgholdsecs = (abs(( (qe->parent->holdtime + 30) - (now - qe->start) )) - 60 * avgholdmins) / qe->parent->roundingseconds;
		avgholdsecs*= qe->parent->roundingseconds;
	} else {
		avgholdsecs=0;
	}

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Hold time for %s is %d minutes %d seconds\n", qe->parent->name, avgholdmins, avgholdsecs);

	/* If the hold time is >1 min, if it's enabled, and if it's not
	   supposed to be only once and we have already said it, say it */
	if ((avgholdmins+avgholdsecs) > 0 && (qe->parent->announceholdtime) && (!(qe->parent->announceholdtime==1 && qe->last_pos)) ) {
		res += play_file(qe->chan, qe->parent->sound_holdtime);
		if(avgholdmins>0) {
			if (avgholdmins < 2) {
				res += play_file(qe->chan, qe->parent->sound_lessthan);
				res += ast_say_number(qe->chan, 2, AST_DIGIT_ANY, qe->chan->language, (char *)NULL);
			} else 
				res += ast_say_number(qe->chan, avgholdmins, AST_DIGIT_ANY, qe->chan->language, (char*) NULL);
			res += play_file(qe->chan, qe->parent->sound_minutes);
		}
		if(avgholdsecs>0) {
			res += ast_say_number(qe->chan, avgholdsecs, AST_DIGIT_ANY, qe->chan->language, (char*) NULL);
			res += play_file(qe->chan, qe->parent->sound_seconds);
		}

	}

	posout:
	/* Set our last_pos indicators */
 	qe->last_pos = now;
	qe->last_pos_said = qe->pos;

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Told %s in %s their queue position (which was %d)\n", qe->chan->name, qe->parent->name, qe->pos);
	res += play_file(qe->chan, qe->parent->sound_thanks);
	ast_moh_start(qe->chan, qe->moh);

	return (res>0);
}

static void record_abandoned(struct queue_ent *qe)
{
	ast_mutex_lock(&qe->parent->lock);
	qe->parent->callsabandoned++;
	ast_mutex_unlock(&qe->parent->lock);
}

static void recalc_holdtime(struct queue_ent *qe)
{
	int oldvalue, newvalue;

	/* Calculate holdtime using a recursive boxcar filter */
	/* Thanks to SRT for this contribution */
	/* 2^2 (4) is the filter coefficient; a higher exponent would give old entries more weight */

	newvalue = time(NULL) - qe->start;

	ast_mutex_lock(&qe->parent->lock);
	if (newvalue <= qe->parent->servicelevel)
       		qe->parent->callscompletedinsl++;
	oldvalue = qe->parent->holdtime;
	qe->parent->holdtime = (((oldvalue << 2) - oldvalue) + newvalue) >> 2;
	ast_mutex_unlock(&qe->parent->lock);
}


static void leave_queue(struct queue_ent *qe)
{
	struct ast_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int pos = 0;
	q = qe->parent;
	if (!q)
		return;
	ast_mutex_lock(&q->lock);

	prev = NULL;
	cur = q->head;
	while(cur) {
		if (cur == qe) {
			q->count--;

			/* Take us out of the queue */
			manager_event(EVENT_FLAG_CALL, "Leave",
				"Channel: %s\r\nQueue: %s\r\nCount: %d\r\n",
				qe->chan->name, q->name,  q->count);
#if 0
ast_log(LOG_NOTICE, "Queue '%s' Leave, Channel '%s'\n", q->name, qe->chan->name );
#endif
			/* Take us out of the queue */
			if (prev)
				prev->next = cur->next;
			else
				q->head = cur->next;
		} else {
			/* Renumber the people after us in the queue based on a new count */
			cur->pos = ++pos;
			prev = cur;
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&q->lock);
	if (ast_test_flag(q, QUEUE_FLAG_DEAD) && !q->count) {	
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
		if (outgoing->chan && (outgoing->chan != exception))
			ast_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->next;
		free(oo);
	}
}

static int update_status(struct ast_call_queue *q, struct member *member, int status)
{
	struct member *cur;
	/* Since a reload could have taken place, we have to traverse the list to
		be sure it's still valid */
	ast_mutex_lock(&q->lock);
	cur = q->members;
	while(cur) {
		if (member == cur) {
			cur->status = status;
			manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
				"Queue: %s\r\n"
				"Location: %s\r\n"
				"Membership: %s\r\n"
				"Penalty: %d\r\n"
				"CallsTaken: %d\r\n"
				"LastCall: %ld\r\n"
				"Status: %d\r\n",
					q->name, cur->interface, cur->dynamic ? "dynamic" : "static",
					cur->penalty, cur->calls, cur->lastcall, cur->status);
			break;
		}
		cur = cur->next;
	}
	q->callscompleted++;
	ast_mutex_unlock(&q->lock);
	return 0;
}

static int update_dial_status(struct ast_call_queue *q, struct member *member, int status)
{
	if (status == AST_CAUSE_BUSY)
		status = AST_DEVICE_BUSY;
	else if (status == AST_CAUSE_UNREGISTERED)
		status = AST_DEVICE_UNAVAILABLE;
	else if (status == AST_CAUSE_NOSUCHDRIVER)
		status = AST_DEVICE_INVALID;
	else
		status = AST_DEVICE_UNKNOWN;
	return update_status(q, member, status);
}

static int ring_entry(struct queue_ent *qe, struct localuser *tmp)
{
	int res;
	int status;
	char tech[256];
	char *location;

	if (qe->parent->wrapuptime && (time(NULL) - tmp->lastcall < qe->parent->wrapuptime)) {
		ast_log(LOG_DEBUG, "Wrapuptime not yet expired for %s\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}
	
	strncpy(tech, tmp->interface, sizeof(tech) - 1);
	if ((location = strchr(tech, '/')))
		*location++ = '\0';
	else
		location = "";

	/* Request the peer */
	tmp->chan = ast_request(tech, qe->chan->nativeformats, location, &status);
	if (!tmp->chan) {			/* If we can't, just go on to the next call */
#if 0
		ast_log(LOG_NOTICE, "Unable to create channel of type '%s'\n", cur->tech);
#endif			
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		update_dial_status(qe->parent, tmp->member, status);
		return 0;
	} else if (status != tmp->oldstatus) 
		update_dial_status(qe->parent, tmp->member, status);
	
	tmp->chan->appl = "AppQueue";
	tmp->chan->data = "(Outgoing Line)";
	tmp->chan->whentohangup = 0;
	if (tmp->chan->cid.cid_num)
		free(tmp->chan->cid.cid_num);
	tmp->chan->cid.cid_num = NULL;
	if (tmp->chan->cid.cid_name)
		free(tmp->chan->cid.cid_name);
	tmp->chan->cid.cid_name = NULL;
	if (tmp->chan->cid.cid_ani)
		free(tmp->chan->cid.cid_ani);
	tmp->chan->cid.cid_ani = NULL;
	if (qe->chan->cid.cid_num)
		tmp->chan->cid.cid_num = strdup(qe->chan->cid.cid_num);
	if (qe->chan->cid.cid_name)
		tmp->chan->cid.cid_name = strdup(qe->chan->cid.cid_name);
	if (qe->chan->cid.cid_ani)
		tmp->chan->cid.cid_ani = strdup(qe->chan->cid.cid_ani);
	/* Presense of ADSI CPE on outgoing channel follows ours */
	tmp->chan->adsicpe = qe->chan->adsicpe;
	/* Place the call, but don't wait on the answer */
	res = ast_call(tmp->chan, location, 0);
	if (res) {
		/* Again, keep going even if there's an error */
		if (option_debug)
			ast_log(LOG_DEBUG, "ast call on peer returned %d\n", res);
		else if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", tmp->interface);
		ast_hangup(tmp->chan);
		tmp->chan = NULL;
		tmp->stillgoing = 0;
		return 0;
	} else {
		if (ast_test_flag(qe->parent, QUEUE_FLAG_EVENTWHENCALLED)) {
			manager_event(EVENT_FLAG_AGENT, "AgentCalled",
						"AgentCalled: %s\r\n"
						"ChannelCalling: %s\r\n"
						"CallerID: %s\r\n"
						"CallerIDName: %s\r\n"
						"Context: %s\r\n"
						"Extension: %s\r\n"
						"Priority: %d\r\n",
						tmp->interface, qe->chan->name,
						tmp->chan->cid.cid_num ? tmp->chan->cid.cid_num : "unknown",
						tmp->chan->cid.cid_name ? tmp->chan->cid.cid_name : "unknown",
						qe->chan->context, qe->chan->exten, qe->chan->priority);
		}
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Called %s\n", tmp->interface);
	}
	return 0;
}

static int ring_one(struct queue_ent *qe, struct localuser *outgoing)
{
	struct localuser *cur;
	struct localuser *best;
	int bestmetric=0;
	do {
		best = NULL;
		cur = outgoing;
		while(cur) {
			if (cur->stillgoing &&							/* Not already done */
				!cur->chan &&								/* Isn't already going */
				(!best || (cur->metric < bestmetric))) {	/* We haven't found one yet, or it's better */
					bestmetric = cur->metric;
					best = cur;
			}
			cur = cur->next;
		}
		if (best) {
			if (!qe->parent->strategy) {
				/* Ring everyone who shares this best metric (for ringall) */
				cur = outgoing;
				while(cur) {
					if (cur->stillgoing && !cur->chan && (cur->metric == bestmetric)) {
						ast_log(LOG_DEBUG, "(Parallel) Trying '%s' with metric %d\n", cur->interface, cur->metric);
						ring_entry(qe, cur);
					}
					cur = cur->next;
				}
			} else {
				/* Ring just the best channel */
				if (option_debug)
					ast_log(LOG_DEBUG, "Trying '%s' with metric %d\n", best->interface, best->metric);
				ring_entry(qe, best);
			}
		}
	} while (best && !best->chan);
	if (!best) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Nobody left to try ringing in queue\n");
		return 0;
	}
	return 1;
}

static int store_next(struct queue_ent *qe, struct localuser *outgoing)
{
	struct localuser *cur;
	struct localuser *best;
	int bestmetric=0;
	best = NULL;
	cur = outgoing;
	while(cur) {
		if (cur->stillgoing &&							/* Not already done */
			!cur->chan &&								/* Isn't already going */
			(!best || (cur->metric < bestmetric))) {	/* We haven't found one yet, or it's better */
				bestmetric = cur->metric;
				best = cur;
		}
		cur = cur->next;
	}
	if (best) {
		/* Ring just the best channel */
		ast_log(LOG_DEBUG, "Next is '%s' with metric %d\n", best->interface, best->metric);
		qe->parent->rrpos = best->metric % 1000;
	} else {
		/* Just increment rrpos */
		if (!ast_test_flag(qe->parent, QUEUE_FLAG_WRAPPED)) {
			/* No more channels, start over */
			qe->parent->rrpos = 0;
		} else {
			/* Prioritize next entry */
			qe->parent->rrpos++;
		}
	}
	ast_clear_flag(qe->parent, QUEUE_FLAG_WRAPPED);
	return 0;
}

static int valid_exit(struct queue_ent *qe, char digit)
{
	char tmp[2];
	if (ast_strlen_zero(qe->context))
		return 0;
	tmp[0] = digit;
	tmp[1] = '\0';
	if (ast_exists_extension(qe->chan, qe->context, tmp, 1, qe->chan->cid.cid_num)) {
		strncpy(qe->chan->context, qe->context, sizeof(qe->chan->context) - 1);
		strncpy(qe->chan->exten, tmp, sizeof(qe->chan->exten) - 1);
		qe->chan->priority = 0;
		return 1;
	}
	return 0;
}

#define AST_MAX_WATCHERS 256

static struct localuser *wait_for_answer(struct queue_ent *qe, struct localuser *outgoing, int *to, struct localuser *flags, char *digit)
{
	char *queue = qe->parent->name;
	struct localuser *o;
	int found;
	int numlines;
	int status;
	int sentringing = 0;
	int numbusies = 0;
	int numnochan = 0;
	int orig = *to;
	struct ast_frame *f;
	struct localuser *peer = NULL;
	struct ast_channel *watchers[AST_MAX_WATCHERS];
	int pos;
	struct ast_channel *winner;
	struct ast_channel *in = qe->chan;
	
	while(*to && !peer) {
		o = outgoing;
		found = -1;
		pos = 1;
		numlines = 0;
		watchers[0] = in;
		while(o) {
			/* Keep track of important channels */
			if (o->stillgoing && o->chan) {
				watchers[pos++] = o->chan;
				found = 1;
			}
			o = o->next;
			numlines++;
		}
		if (found < 0) {
			if (numlines == (numbusies + numnochan)) {
				ast_log(LOG_DEBUG, "Everyone is busy at this time\n");
			} else {
				ast_log(LOG_NOTICE, "No one is answering queue '%s'\n", queue);
			}
			*to = 0;
			return NULL;
		}
		winner = ast_waitfor_n(watchers, pos, to);
		o = outgoing;
		while(o) {
			if (o->stillgoing && (o->chan) &&  (o->chan->_state == AST_STATE_UP)) {
				if (!peer) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
					peer = o;
					ast_copy_flags(flags, o, QUEUE_FLAG_REDIR_IN & QUEUE_FLAG_REDIR_OUT & QUEUE_FLAG_DISCON_IN & QUEUE_FLAG_DISCON_OUT);
				}
			} else if (o->chan && (o->chan == winner)) {
				if (!ast_strlen_zero(o->chan->call_forward)) {
					char tmpchan[256]="";
					char *stuff;
					char *tech;
					strncpy(tmpchan, o->chan->call_forward, sizeof(tmpchan) - 1);
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff = '\0';
						stuff++;
						tech = tmpchan;
					} else {
						snprintf(tmpchan, sizeof(tmpchan), "%s@%s", o->chan->call_forward, o->chan->context);
						stuff = tmpchan;
						tech = "Local";
					}
					/* Before processing channel, go ahead and check for forwarding */
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, o->chan->name);
					/* Setup parameters */
					o->chan = ast_request(tech, in->nativeformats, stuff, &status);
					if (status != o->oldstatus) 
						update_dial_status(qe->parent, o->member, status);						
					if (!o->chan) {
						ast_log(LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s'\n", tech, stuff);
						o->stillgoing = 0;
						numnochan++;
					} else {
						if (o->chan->cid.cid_num)
							free(o->chan->cid.cid_num);
						o->chan->cid.cid_num = NULL;
						if (o->chan->cid.cid_name)
							free(o->chan->cid.cid_name);
						o->chan->cid.cid_name = NULL;

						if (in->cid.cid_num) {
							o->chan->cid.cid_num = strdup(in->cid.cid_num);
							if (!o->chan->cid.cid_num)
								ast_log(LOG_WARNING, "Out of memory\n");	
						}
						if (in->cid.cid_name) {
							o->chan->cid.cid_name = strdup(in->cid.cid_name);
							if (!o->chan->cid.cid_name)
								ast_log(LOG_WARNING, "Out of memory\n");	
						}
						strncpy(o->chan->accountcode, in->accountcode, sizeof(o->chan->accountcode) - 1);
						o->chan->cdrflags = in->cdrflags;

						if (in->cid.cid_ani) {
							if (o->chan->cid.cid_ani)
								free(o->chan->cid.cid_ani);
							o->chan->cid.cid_ani = malloc(strlen(in->cid.cid_ani) + 1);
							if (o->chan->cid.cid_ani)
								strncpy(o->chan->cid.cid_ani, in->cid.cid_ani, strlen(in->cid.cid_ani) + 1);
							else
								ast_log(LOG_WARNING, "Out of memory\n");
						}
						if (o->chan->cid.cid_rdnis) 
							free(o->chan->cid.cid_rdnis);
						if (!ast_strlen_zero(in->macroexten))
							o->chan->cid.cid_rdnis = strdup(in->macroexten);
						else
							o->chan->cid.cid_rdnis = strdup(in->exten);
						if (ast_call(o->chan, tmpchan, 0)) {
							ast_log(LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
							o->stillgoing = 0;
							ast_hangup(o->chan);
							o->chan = NULL;
							numnochan++;
						}
					}
					/* Hangup the original channel now, in case we needed it */
					ast_hangup(winner);
					continue;
				}
				f = ast_read(winner);
				if (f) {
					if (f->frametype == AST_FRAME_CONTROL) {
						switch(f->subclass) {
					    case AST_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
								peer = o;
								ast_copy_flags(flags, o, QUEUE_FLAG_REDIR_IN & QUEUE_FLAG_REDIR_OUT & QUEUE_FLAG_DISCON_IN & QUEUE_FLAG_DISCON_OUT);
							}
							break;
						case AST_CONTROL_BUSY:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
							o->stillgoing = 0;
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							ast_hangup(o->chan);
							o->chan = NULL;
							if (qe->parent->strategy)
								ring_one(qe, outgoing);
							numbusies++;
							break;
						case AST_CONTROL_CONGESTION:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
							o->stillgoing = 0;
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							ast_hangup(o->chan);
							o->chan = NULL;
							if (qe->parent->strategy)
								ring_one(qe, outgoing);
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
					ast_hangup(o->chan);
					o->chan = NULL;
					if (qe->parent->strategy)
						ring_one(qe, outgoing);
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
			if (f && (f->frametype == AST_FRAME_DTMF) && ast_test_flag(flags, QUEUE_FLAG_DISCON_OUT) && (f->subclass == '*')) {
			    if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
				*to=0;
				return NULL;
			}
			if (f && (f->frametype == AST_FRAME_DTMF) && (f->subclass != '*') && valid_exit(qe, f->subclass)) {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_3 "User pressed digit: %c", f->subclass);
				*to=0;
				*digit=f->subclass;
				return NULL;
			}
		}
		if (!*to && (option_verbose > 2))
			ast_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
	}

	return peer;
	
}

static int is_our_turn(struct queue_ent *qe)
{
	struct queue_ent *ch;
	int res;

	/* Atomically read the parent head -- does not need a lock */
	ch = qe->parent->head;
	/* If we are now at the top of the head, break out */
	if (ch == qe) {
		if (option_debug)
			ast_log(LOG_DEBUG, "It's our turn (%s).\n", qe->chan->name);
		res = 1;
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "It's not our turn (%s).\n", qe->chan->name);
		res = 0;
	}
	return res;
}

static int wait_our_turn(struct queue_ent *qe, int ringing)
{
	int res = 0;

	/* This is the holding pen for callers 2 through maxlen */
	for (;;) {
		if (is_our_turn(qe))
			break;

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) > qe->expire))
			break;

		/* leave the queue if no agents, if enabled */
		if (ast_test_flag(qe->parent, QUEUE_FLAG_LEAVEWHENEMPTY) && has_no_members(qe->parent)) {
			leave_queue(qe);
			break;
		}

		/* Make a position announcement, if enabled */
		if (qe->parent->announcefrequency && !ringing)
			say_position(qe);

		/* Wait a second before checking again */
		res = ast_waitfordigit(qe->chan, RECHECK * 1000);
		if (res)
			break;
	}
	return res;
}

static int update_queue(struct ast_call_queue *q, struct member *member)
{
	struct member *cur;
	/* Since a reload could have taken place, we have to traverse the list to
		be sure it's still valid */
	ast_mutex_lock(&q->lock);
	cur = q->members;
	while(cur) {
		if (member == cur) {
			time(&cur->lastcall);
			cur->calls++;
			break;
		}
		cur = cur->next;
	}
	q->callscompleted++;
	ast_mutex_unlock(&q->lock);
	return 0;
}

static int calc_metric(struct ast_call_queue *q, struct member *mem, int pos, struct queue_ent *qe, struct localuser *tmp)
{
	switch (q->strategy) {
	case QUEUE_STRATEGY_RINGALL:
		/* Everyone equal, except for penalty */
		tmp->metric = mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_ROUNDROBIN:
		if (!pos) {
			if (!ast_test_flag(q, QUEUE_FLAG_WRAPPED)) {
				/* No more channels, start over */
				q->rrpos = 0;
			} else {
				/* Prioritize next entry */
				q->rrpos++;
			}
			ast_clear_flag(q, QUEUE_FLAG_WRAPPED);
		}
		/* Fall through */
	case QUEUE_STRATEGY_RRMEMORY:
		if (pos < q->rrpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > q->rrpos) {
				/* Indicate there is another priority */
				ast_set_flag(q, QUEUE_FLAG_WRAPPED);
			}
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_RANDOM:
		tmp->metric = rand() % 1000;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_FEWESTCALLS:
		tmp->metric = mem->calls;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_LEASTRECENT:
		if (!mem->lastcall)
			tmp->metric = 0;
		else
			tmp->metric = 1000000 - (time(NULL) - mem->lastcall);
		tmp->metric += mem->penalty * 1000000;
		break;
	default:
		ast_log(LOG_WARNING, "Can't calculate metric for unknown strategy %d\n", q->strategy);
		break;
	}
	return 0;
}

static int try_calling(struct queue_ent *qe, char *options, char *announceoverride, char *url, int *go_on)
{
	struct member *cur;
	struct localuser *outgoing=NULL, *tmp = NULL;
	int to;
	struct localuser flags_dummy;
	char restofit[AST_MAX_EXTENSION];
	char oldexten[AST_MAX_EXTENSION]="";
	char oldcontext[AST_MAX_EXTENSION]="";
	char queuename[256]="";
	char *newnum;
	char *monitorfilename;
	struct ast_channel *peer;
	struct localuser *lpeer;
	struct member *member;
	int res = 0, bridge = 0;
	int zapx = 2;
	int x=0;
	char *announce = NULL;
	char digit = 0;
	time_t callstart;
	time_t now;
	struct ast_bridge_config config;
	/* Hold the lock while we setup the outgoing calls */
	ast_mutex_lock(&qe->parent->lock);
	if (option_debug)
		ast_log(LOG_DEBUG, "%s is trying to call a queue member.\n", 
							qe->chan->name);
	strncpy(queuename, qe->parent->name, sizeof(queuename) - 1);
	time(&now);
	cur = qe->parent->members;
	if (!ast_strlen_zero(qe->announce))
		announce = qe->announce;
	if (announceoverride && !ast_strlen_zero(announceoverride))
		announce = announceoverride;
	while(cur) {
		/* Get a technology/[device:]number pair */
		tmp = malloc(sizeof(struct localuser));
		if (!tmp) {
			ast_mutex_unlock(&qe->parent->lock);
			ast_log(LOG_WARNING, "Out of memory\n");
			goto out;
		}
		memset(tmp, 0, sizeof(struct localuser));
		tmp->stillgoing = -1;
		for (; options && *options; options++)
			switch (*options) {
			case 't':
				ast_set_flag(tmp, QUEUE_FLAG_REDIR_IN);
				break;
			case 'T':
				ast_set_flag(tmp, QUEUE_FLAG_REDIR_OUT);
				break;
			case 'r':
				ast_set_flag(tmp, QUEUE_FLAG_RINGBACKONLY);
				break;
			case 'm':
				ast_set_flag(tmp, QUEUE_FLAG_MUSICONHOLD);
				break;
			case 'd':
				ast_set_flag(tmp, QUEUE_FLAG_DATAQUALITY);
				break;
			case 'h':
				ast_set_flag(tmp, QUEUE_FLAG_DISCON_IN);
				break;
			case 'H':
				ast_set_flag(tmp, QUEUE_FLAG_DISCON_OUT);
				break;
			case 'n':
			        if ((now - qe->start >= qe->parent->timeout))
					*go_on = 1;
				break;
			}
		if (option_debug) {
			if (url)
				ast_log(LOG_DEBUG, "Queue with URL=%s_\n", url);
			else 
				ast_log(LOG_DEBUG, "Simple queue (no URL)\n");
		}

		tmp->member = cur;		/* Never directly dereference!  Could change on reload */
		tmp->oldstatus = cur->status;
		tmp->lastcall = cur->lastcall;
		strncpy(tmp->interface, cur->interface, sizeof(tmp->interface)-1);
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(tmp->interface, "/BYEXTENSION"))) {
			newnum++;
			strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit) - 1);
			snprintf(newnum, sizeof(tmp->interface) - (newnum - tmp->interface), "%s%s", qe->chan->exten, restofit);
			if (option_debug)
				ast_log(LOG_DEBUG, "Dialing by extension %s\n", tmp->interface);
		}
		/* Special case: If we ring everyone, go ahead and ring them, otherwise
		   just calculate their metric for the appropriate strategy */
		calc_metric(qe->parent, cur, x++, qe, tmp);
		/* Put them in the list of outgoing thingies...  We're ready now. 
		   XXX If we're forcibly removed, these outgoing calls won't get
		   hung up XXX */
		tmp->next = outgoing;
		outgoing = tmp;		
		/* If this line is up, don't try anybody else */
		if (outgoing->chan && (outgoing->chan->_state == AST_STATE_UP))
			break;

		cur = cur->next;
	}
	if (qe->parent->timeout)
		to = qe->parent->timeout * 1000;
	else
		to = -1;
	ring_one(qe, outgoing);
	ast_mutex_unlock(&qe->parent->lock);
	lpeer = wait_for_answer(qe, outgoing, &to, &flags_dummy, &digit);
	ast_mutex_lock(&qe->parent->lock);
	if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY) {
		store_next(qe, outgoing);
	}
	ast_mutex_unlock(&qe->parent->lock);
	if (lpeer)
		peer = lpeer->chan;
	else
		peer = NULL;
	if (!peer) {
		if (to) {
			/* Musta gotten hung up */
			record_abandoned(qe);
			res = -1;
		} else {
			if (digit && valid_exit(qe, digit))
				res=digit;
			else
				/* Nobody answered, next please? */
				res=0;
		}
		if (option_debug)
			ast_log(LOG_DEBUG, "%s: Nobody answered.\n", qe->chan->name);
		goto out;
	}
	if (peer) {
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the 
		   conversation.  */
		qe->handled++;
		if (!strcmp(qe->chan->type,"Zap")) {
			zapx = !ast_test_flag(tmp, QUEUE_FLAG_DATAQUALITY);
			ast_channel_setoption(qe->chan,AST_OPTION_TONE_VERIFY,&zapx,sizeof(char),0);
		}			
		if (!strcmp(peer->type,"Zap")) {
			zapx = !ast_test_flag(tmp, QUEUE_FLAG_DATAQUALITY);
			ast_channel_setoption(peer,AST_OPTION_TONE_VERIFY,&zapx,sizeof(char),0);
		}
		/* Update parameters for the queue */
		recalc_holdtime(qe);
		member = lpeer->member;
		hanguptree(outgoing, peer);
		outgoing = NULL;
		if (announce || ast_test_flag(qe->parent, QUEUE_FLAG_REPORTHOLDTIME) || qe->parent->memberdelay) {
			int res2;
			res2 = ast_autoservice_start(qe->chan);
			if (!res2) {
				if (qe->parent->memberdelay) {
					ast_log(LOG_NOTICE, "Delaying member connect for %d seconds\n", qe->parent->memberdelay);
					res2 |= ast_safe_sleep(peer, qe->parent->memberdelay * 1000);
				}
				if (!res2 && announce) {
					if (play_file(peer, announce))
						ast_log(LOG_WARNING, "Announcement file '%s' is unavailable, continuing anyway...\n", announce);
				}
				if (!res2 && ast_test_flag(qe->parent, QUEUE_FLAG_REPORTHOLDTIME)) {
					if (!play_file(peer, qe->parent->sound_reporthold)) {
						int holdtime;
						time_t now;

						time(&now);
						holdtime = abs((now - qe->start) / 60);
						if (holdtime < 2) {
							play_file(peer, qe->parent->sound_lessthan);
							ast_say_number(peer, 2, AST_DIGIT_ANY, peer->language, NULL);
						} else 
							ast_say_number(peer, holdtime, AST_DIGIT_ANY, peer->language, NULL);
						play_file(peer, qe->parent->sound_minutes);
					}
				}
			}
			res2 |= ast_autoservice_stop(qe->chan);
			if (res2) {
				/* Agent must have hung up */
				ast_log(LOG_WARNING, "Agent on %s hungup on the customer.  They're going to be pissed.\n", peer->name);
				ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "AGENTDUMP", "%s", "");
				ast_hangup(peer);
				return -1;
			}
		}
		/* Stop music on hold */
		ast_moh_stop(qe->chan);
		/* If appropriate, log that we have a destination channel */
		if (qe->chan->cdr)
			ast_cdr_setdestchan(qe->chan->cdr, peer->name);
		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(qe->chan, peer);
		if (res < 0) {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "SYSCOMPAT", "%s", "");
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", qe->chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		/* Begin Monitoring */
		if (qe->parent->monfmt && *qe->parent->monfmt) {
			monitorfilename = pbx_builtin_getvar_helper( qe->chan, "MONITOR_FILENAME");
			if(monitorfilename) {
				ast_monitor_start( peer, qe->parent->monfmt, monitorfilename, 1 );
			} else {
				ast_monitor_start( peer, qe->parent->monfmt, qe->chan->cdr->uniqueid, 1 );
			}
			if(ast_test_flag(qe->parent, QUEUE_FLAG_MONJOIN)) {
				ast_monitor_setjoinfiles( peer, 1);
			}
		}
		/* Drop out of the queue at this point, to prepare for next caller */
		leave_queue(qe);			
 		if( url && !ast_strlen_zero(url) && ast_channel_supports_html(peer) ) {
			if (option_debug)
	 			ast_log(LOG_DEBUG, "app_queue: sendurl=%s.\n", url);
 			ast_channel_sendurl( peer, url );
 		}
		ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "CONNECT", "%ld", (long)time(NULL) - qe->start);
		strncpy(oldcontext, qe->chan->context, sizeof(oldcontext) - 1);
		strncpy(oldexten, qe->chan->exten, sizeof(oldexten) - 1);
		time(&callstart);

		memset(&config,0,sizeof(struct ast_bridge_config));
		config.allowredirect_in = ast_test_flag(&flags_dummy, QUEUE_FLAG_REDIR_IN);
		config.allowredirect_out = ast_test_flag(&flags_dummy, QUEUE_FLAG_REDIR_OUT);
		config.allowdisconnect_in = ast_test_flag(&flags_dummy, QUEUE_FLAG_DISCON_IN);
		config.allowdisconnect_out = ast_test_flag(&flags_dummy, QUEUE_FLAG_DISCON_OUT);
		bridge = ast_bridge_call(qe->chan,peer,&config);

		if (strcasecmp(oldcontext, qe->chan->context) || strcasecmp(oldexten, qe->chan->exten)) {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "TRANSFER", "%s|%s", qe->chan->exten, qe->chan->context);
		} else if (qe->chan->_softhangup) {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETECALLER", "%ld|%ld", (long)(callstart - qe->start), (long)(time(NULL) - callstart));
		} else {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETEAGENT", "%ld|%ld", (long)(callstart - qe->start), (long)(time(NULL) - callstart));
		}

		if(bridge != AST_PBX_NO_HANGUP_PEER)
			ast_hangup(peer);
		update_queue(qe->parent, member);
		if( bridge == 0 ) res=1; /* JDG: bridge successfull, leave app_queue */
		else res = bridge; /* bridge error, stay in the queue */
	}	
out:
	hanguptree(outgoing, NULL);
	return res;
}

static int wait_a_bit(struct queue_ent *qe)
{
	/* Don't need to hold the lock while we setup the outgoing calls */
	int retrywait = qe->parent->retry * 1000;
	return ast_waitfordigit(qe->chan, retrywait);
}

/* [PHM 06/26/03] */

static struct member * interface_exists(struct ast_call_queue *q, char *interface)
{
	struct member *mem;

	if (q)
		for (mem = q->members; mem; mem = mem->next)
			if (!strcmp(interface, mem->interface))
				return mem;

	return NULL;
}


static struct member *create_queue_node(char *interface, int penalty)
{
	struct member *cur;
	
	/* Add a new member */

	cur = malloc(sizeof(struct member));

	if (cur) {
		memset(cur, 0, sizeof(struct member));
		cur->penalty = penalty;
		strncpy(cur->interface, interface, sizeof(cur->interface) - 1);
		if (!strchr(cur->interface, '/'))
			ast_log(LOG_WARNING, "No location at interface '%s'\n", interface);
		cur->status = ast_device_state(interface);
	}

	return cur;
}

/* Dump all members in a specific queue to the databse
 *
 * <pm_family>/<queuename> = <interface>;<penalty>;...
 *
 */
static void dump_queue_members(struct ast_call_queue *pm_queue)
{
	struct member *cur_member = NULL;
	char value[PM_MAX_LEN];
	int value_len = 0;
	int res;

	memset(value, 0, sizeof(value));

	if (pm_queue) {
		cur_member = pm_queue->members;
		while (cur_member) {
			if (cur_member->dynamic) {
				value_len = strlen(value);
				res = snprintf(value+value_len, sizeof(value)-value_len, "%s;%d;", cur_member->interface, cur_member->penalty);
				if (res != strlen(value + value_len)) {
					ast_log(LOG_WARNING, "Could not create persistent member string, out of space\n");
					break;
				}
			}					
			cur_member = cur_member->next;
		}

		if (!ast_strlen_zero(value) && !cur_member) {
			if (ast_db_put(pm_family, pm_queue->name, value))
			    ast_log(LOG_WARNING, "failed to create persistent dynamic entry!\n");
		} else {
			/* Delete the entry if the queue is empty or there is an error */
		    ast_db_del(pm_family, pm_queue->name);
		}

	}

}

static int remove_from_queue(char *queuename, char *interface)
{
	struct ast_call_queue *q;
	struct member *last_member, *look;
	int res = RES_NOSUCHQUEUE;

	ast_mutex_lock(&qlock);
	for (q = queues ; q ; q = q->next) {
		ast_mutex_lock(&q->lock);
		if (!strcmp(q->name, queuename)) {
			if ((last_member = interface_exists(q, interface))) {
				if ((look = q->members) == last_member) {
					q->members = last_member->next;
				} else {
					while (look != NULL) {
						if (look->next == last_member) {
							look->next = last_member->next;
							break;
						} else {
							 look = look->next;
						}
					}
				}
				manager_event(EVENT_FLAG_AGENT, "QueueMemberRemoved",
						"Queue: %s\r\n"
						"Location: %s\r\n",
					q->name, last_member->interface);
				free(last_member);

				if (queue_persistent_members)
				    dump_queue_members(q);

				res = RES_OKAY;
			} else {
				res = RES_EXISTS;
			}
			ast_mutex_unlock(&q->lock);
			break;
		}
		ast_mutex_unlock(&q->lock);
	}
	ast_mutex_unlock(&qlock);
	return res;
}

static int add_to_queue(char *queuename, char *interface, int penalty)
{
	struct ast_call_queue *q;
	struct member *new_member;
	int res = RES_NOSUCHQUEUE;

	ast_mutex_lock(&qlock);
	for (q = queues ; q ; q = q->next) {
		ast_mutex_lock(&q->lock);
		if (!strcmp(q->name, queuename)) {
			if (interface_exists(q, interface) == NULL) {
				new_member = create_queue_node(interface, penalty);

				if (new_member != NULL) {
					new_member->dynamic = 1;
					new_member->next = q->members;
					q->members = new_member;
					manager_event(EVENT_FLAG_AGENT, "QueueMemberAdded",
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %ld\r\n"
						"Status: %d\r\n",
					q->name, new_member->interface, new_member->dynamic ? "dynamic" : "static",
					new_member->penalty, new_member->calls, new_member->lastcall, new_member->status);
					
					if (queue_persistent_members)
					    dump_queue_members(q);

					res = RES_OKAY;
				} else {
					res = RES_OUTOFMEMORY;
				}
			} else {
				res = RES_EXISTS;
			}
			ast_mutex_unlock(&q->lock);
			break;
		}
		ast_mutex_unlock(&q->lock);
	}
	ast_mutex_unlock(&qlock);
	return res;
}

/* Add members saved in the queue members DB file saves
 * created by dump_queue_members(), back into the queues */
static void reload_queue_members(void)
{
	char *cur_pm_ptr;	
	char *pm_queue_name;
	char *pm_interface;
	char *pm_penalty_tok;
	int pm_penalty = 0;
	struct ast_db_entry *pm_db_tree = NULL;
	int pm_family_len = 0;
	struct ast_call_queue *cur_queue = NULL;
	char queue_data[PM_MAX_LEN];

	pm_db_tree = ast_db_gettree(pm_family, NULL);

	pm_family_len = strlen(pm_family);
	ast_mutex_lock(&qlock);
	/* Each key in 'pm_family' is the name of a specific queue in which
	 * we will reload members into. */
	while (pm_db_tree) {
		pm_queue_name = pm_db_tree->key+pm_family_len+2;

		cur_queue = queues;
		while (cur_queue) {
			ast_mutex_lock(&cur_queue->lock);
			
			if (strcmp(pm_queue_name, cur_queue->name) == 0)
			    break;
			
			ast_mutex_unlock(&cur_queue->lock);
			
			cur_queue = cur_queue->next;
		}

		if (!cur_queue) {
			/* If the queue no longer exists, remove it from the
			 * database */
			ast_db_del(pm_family, pm_queue_name);
			pm_db_tree = pm_db_tree->next;
			continue;
		} else
		    ast_mutex_unlock(&cur_queue->lock);

		if (!ast_db_get(pm_family, pm_queue_name, queue_data, PM_MAX_LEN)) {
			/* Parse each <interface>;<penalty>; from the value of the
			 * queuename key and add it to the respective queue */
			cur_pm_ptr = queue_data;
			while ((pm_interface = strsep(&cur_pm_ptr, ";"))) {
				if (!(pm_penalty_tok = strsep(&cur_pm_ptr, ";"))) {
					ast_log(LOG_WARNING, "Error parsing corrupted Queue DB string for '%s'\n", pm_queue_name);
					break;
				}
				pm_penalty = strtol(pm_penalty_tok, NULL, 10);
				if (errno == ERANGE) {
					ast_log(LOG_WARNING, "Error converting penalty: %s: Out of range.\n", pm_penalty_tok);
					break;
				}
	
				if (option_debug)
				    ast_log(LOG_DEBUG, "Reload Members: Queue: %s  Member: %s  Penalty: %d\n", pm_queue_name, pm_interface, pm_penalty);
	
				if (add_to_queue(pm_queue_name, pm_interface, pm_penalty) == RES_OUTOFMEMORY) {
					ast_log(LOG_ERROR, "Out of Memory\n");
					break;
				}
			}
		}
		
		pm_db_tree = pm_db_tree->next;
	}

	ast_log(LOG_NOTICE, "Queue members sucessfully reloaded from database.\n");
	ast_mutex_unlock(&qlock);
	if (pm_db_tree) {
		ast_db_freetree(pm_db_tree);
		pm_db_tree = NULL;
	}
}

static int rqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *info, *queuename;
	char tmpchan[256]="";
	char *interface = NULL;

	if (!data) {
		ast_log(LOG_WARNING, "RemoveQueueMember requires an argument (queuename[|interface])\n");
		return -1;
	}

	info = ast_strdupa((char *)data);
	if (!info) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	queuename = info;
	if (queuename) {
		interface = strchr(queuename, '|');
		if (interface) {
			*interface = '\0';
			interface++;
		}
		else {
			strncpy(tmpchan, chan->name, sizeof(tmpchan) - 1);
			interface = strrchr(tmpchan, '-');
			if (interface)
				*interface = '\0';
			interface = tmpchan;
		}
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		ast_log(LOG_NOTICE, "Removed interface '%s' from queue '%s'\n", interface, queuename);
		res = 0;
		break;
	case RES_EXISTS:
		ast_log(LOG_WARNING, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num)) {
			chan->priority += 100;
		}
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to remove interface from queue '%s': No such queue\n", queuename);
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static int aqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *queuename;
	char *info;
	char tmpchan[512]="";
	char *interface=NULL;
	char *penaltys=NULL;
	int penalty = 0;

	if (!data) {
		ast_log(LOG_WARNING, "AddQueueMember requires an argument (queuename[|[interface][|penalty]])\n");
		return -1;
	}

	info = ast_strdupa((char *)data);
	if (!info) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	LOCAL_USER_ADD(u);

	queuename = info;
	if (queuename) {
		interface = strchr(queuename, '|');
		if (interface) {
			*interface = '\0';
			interface++;
		}
		if (interface) {
			penaltys = strchr(interface, '|');
			if (penaltys) {
				*penaltys = '\0';
				penaltys++;
			}
		}
		if (!interface || ast_strlen_zero(interface)) {
			strncpy(tmpchan, chan->name, sizeof(tmpchan) - 1);
			interface = strrchr(tmpchan, '-');
			if (interface)
				*interface = '\0';
			interface = tmpchan;
		}
		if (penaltys && strlen(penaltys)) {
			if ((sscanf(penaltys, "%d", &penalty) != 1) || penalty < 0) {
				ast_log(LOG_WARNING, "Penalty '%s' is invalid, must be an integer >= 0\n", penaltys);
				penalty = 0;
			}
		}
	}

	switch (add_to_queue(queuename, interface, penalty)) {
	case RES_OKAY:
		ast_log(LOG_NOTICE, "Added interface '%s' to queue '%s'\n", interface, queuename);
		res = 0;
		break;
	case RES_EXISTS:
		ast_log(LOG_WARNING, "Unable to add interface '%s' to queue '%s': Already there\n", interface, queuename);
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num)) {
			chan->priority += 100;
		}
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to add interface to queue '%s': No such queue\n", queuename);
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static int queue_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	int ringing=0;
	struct localuser *u;
	char *queuename;
	char info[512];
	char *info_ptr = info;
	char *options = NULL;
	char *url = NULL;
	char *announceoverride = NULL;
	char *user_priority;
	int prio;
	char *queuetimeoutstr = NULL;

	/* whether to exit Queue application after the timeout hits */
	int go_on = 0;

	/* Our queue entry */
	struct queue_ent qe;
	
	if (!data) {
		ast_log(LOG_WARNING, "Queue requires an argument (queuename[|[timeout][|URL]])\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	/* Setup our queue entry */
	memset(&qe, 0, sizeof(qe));
	qe.start = time(NULL);
	
	/* Parse our arguments XXX Check for failure XXX */
	strncpy(info, (char *) data, sizeof(info) - 1);
	queuename = strsep(&info_ptr, "|");
	options = strsep(&info_ptr, "|");
	url = strsep(&info_ptr, "|");
	announceoverride = strsep(&info_ptr, "|");
	queuetimeoutstr = info_ptr;

	/* set the expire time based on the supplied timeout; */
	if (queuetimeoutstr)
		qe.expire = qe.start + atoi(queuetimeoutstr);
	else
		qe.expire = 0;

	/* Get the priority from the variable ${QUEUE_PRIO} */
	user_priority = pbx_builtin_getvar_helper(chan, "QUEUE_PRIO");
	if (user_priority) {
		if (sscanf(user_priority, "%d", &prio) == 1) {
			if (option_debug)
				ast_log(LOG_DEBUG, "%s: Got priority %d from ${QUEUE_PRIO}.\n",
								chan->name, prio);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_PRIO}: Invalid value (%s), channel %s.\n",
							user_priority, chan->name);
			prio = 0;
		}
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "NO QUEUE_PRIO variable found. Using default.\n");
		prio = 0;
	}

	if (options) {
		if (strchr(options, 'r')) {
			ringing = 1;
		}
	}

/*	if (option_debug)  */
		ast_log(LOG_DEBUG, "queue: %s, options: %s, url: %s, announce: %s, expires: %ld, priority: %d\n",
				queuename, options, url, announceoverride, (long)qe.expire, (int)prio);

	qe.chan = chan;
	qe.prio = (int)prio;
	qe.last_pos_said = 0;
	qe.last_pos = 0;
	if (!join_queue(queuename, &qe)) {
		ast_queue_log(queuename, chan->uniqueid, "NONE", "ENTERQUEUE", "%s|%s", url ? url : "", chan->cid.cid_num ? chan->cid.cid_num : "");
		/* Start music on hold */
check_turns:
		if (ringing) {
			ast_indicate(chan, AST_CONTROL_RINGING);
		} else {              
			ast_moh_start(chan, qe.moh);
		}
		for (;;) {
			/* This is the wait loop for callers 2 through maxlen */

			res = wait_our_turn(&qe, ringing);
			/* If they hungup, return immediately */
			if (res < 0) {
				/* Record this abandoned call */
				record_abandoned(&qe);
				ast_queue_log(queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
				if (option_verbose > 2) {
					ast_verbose(VERBOSE_PREFIX_3 "User disconnected while waiting their turn\n");
					res = -1;
				}
				break;
			}
			if (!res) 
				break;
			if (valid_exit(&qe, res)) {
				ast_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%c|%d", res, qe.pos);
				break;
			}
		}
		if (!res) {
			int makeannouncement = 0;
			for (;;) {
				/* This is the wait loop for the head caller*/
				/* To exit, they may get their call answered; */
				/* they may dial a digit from the queue context; */
				/* or, they may timeout. */

				/* Leave if we have exceeded our queuetimeout */
				if (qe.expire && (time(NULL) > qe.expire)) {
					res = 0;
					break;
				}

				if (makeannouncement) {
					/* Make a position announcement, if enabled */
					if (qe.parent->announcefrequency && !ringing)
						say_position(&qe);
				}
				makeannouncement = 1;

				/* Try calling all queue members for 'timeout' seconds */
				res = try_calling(&qe, options, announceoverride, url, &go_on);
				if (res) {
					if (res < 0) {
						if (!qe.handled)
							ast_queue_log(queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
					} else if (res > 0)
						ast_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%c|%d", res, qe.pos);
					break;
				}

				/* leave the queue if no agents, if enabled */
				if (ast_test_flag(qe.parent, QUEUE_FLAG_LEAVEWHENEMPTY) && has_no_members(qe.parent)) {
					res = 0;
					break;
				}

				/* Leave if we have exceeded our queuetimeout */
				if (qe.expire && (time(NULL) > qe.expire)) {
					res = 0;
					break;
				}

				/* OK, we didn't get anybody; wait for 'retry' seconds; may get a digit to exit with */
				res = wait_a_bit(&qe);
				if (res < 0) {
					ast_queue_log(queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
					if (option_verbose > 2) {
						ast_verbose(VERBOSE_PREFIX_3 "User disconnected when they almost made it\n");
						res = -1;
					}
					break;
				}
				if (res && valid_exit(&qe, res)) {
					ast_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%c|%d", res, qe.pos);
					break;
				}
				/* exit after 'timeout' cycle if 'n' option enabled */
				if (go_on) {
					if (option_verbose > 2) {
						ast_verbose(VERBOSE_PREFIX_3 "Exiting on time-out cycle\n");
						res = -1;
					}
					ast_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
					res = 0;
					break;
				}
				/* Since this is a priority queue and 
				 * it is not sure that we are still at the head
				 * of the queue, go and check for our turn again.
				 */
				if (!is_our_turn(&qe)) {
					ast_log(LOG_DEBUG, "Darn priorities, going back in queue (%s)!\n",
								qe.chan->name);
					goto check_turns;
				}
			}
		}
		/* Don't allow return code > 0 */
		if (res >= 0 && res != AST_PBX_KEEPALIVE) {
			res = 0;	
			if (ringing) {
				ast_indicate(chan, -1);
			} else {
				ast_moh_stop(chan);
			}			
			ast_stopstream(chan);
		}
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
	char *general_val = NULL;
	
	cfg = ast_load("queues.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "No call queueing config file, so no call queues\n");
		return;
	}
	ast_mutex_lock(&qlock);
	/* Mark all queues as dead for the moment */
	q = queues;
	while(q) {
		ast_set_flag(q, QUEUE_FLAG_DEAD);
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
					ast_mutex_init(&q->lock);
					strncpy(q->name, cat, sizeof(q->name) - 1);
					new = 1;
				} else new = 0;
			} else
					new = 0;
			if (q) {
				if (!new) 
					ast_mutex_lock(&q->lock);
				/* Re-initialize the queue */
				ast_clear_flag(q, QUEUE_FLAG_DEAD);
				q->retry = 0;
				q->timeout = -1;
				q->maxlen = 0;
				q->announcefrequency = 0;
				q->announceholdtime = 0;
				q->roundingseconds = 0; /* Default - don't announce seconds */
				q->holdtime = 0;
				q->callscompleted = 0;
				q->callsabandoned = 0;
				q->callscompletedinsl = 0;
				q->servicelevel = 0;
				q->wrapuptime = 0;
				free_members(q, 0);
				q->moh[0] = '\0';
				q->announce[0] = '\0';
				q->context[0] = '\0';
				q->monfmt[0] = '\0';
				strncpy(q->sound_next, "queue-youarenext", sizeof(q->sound_next) - 1);
				strncpy(q->sound_thereare, "queue-thereare", sizeof(q->sound_thereare) - 1);
				strncpy(q->sound_calls, "queue-callswaiting", sizeof(q->sound_calls) - 1);
				strncpy(q->sound_holdtime, "queue-holdtime", sizeof(q->sound_holdtime) - 1);
				strncpy(q->sound_minutes, "queue-minutes", sizeof(q->sound_minutes) - 1);
				strncpy(q->sound_seconds, "queue-seconds", sizeof(q->sound_seconds) - 1);
				strncpy(q->sound_thanks, "queue-thankyou", sizeof(q->sound_thanks) - 1);
				strncpy(q->sound_lessthan, "queue-less-than", sizeof(q->sound_lessthan) - 1);
				strncpy(q->sound_reporthold, "queue-reporthold", sizeof(q->sound_reporthold) - 1);
				prev = q->members;
				if (prev) {
					/* find the end of any dynamic members */
					while(prev->next)
						prev = prev->next;
				}
				var = ast_variable_browse(cfg, cat);
				while(var) {
					if (!strcasecmp(var->name, "member")) {
						/* Add a new member */
						cur = malloc(sizeof(struct member));
						if (cur) {
							memset(cur, 0, sizeof(struct member));
							strncpy(cur->interface, var->value, sizeof(cur->interface) - 1);
							if ((tmp = strchr(cur->interface, ','))) {
								*tmp = '\0';
								tmp++;
								cur->penalty = atoi(tmp);
								if (cur->penalty < 0)
									cur->penalty = 0;
							}
							if (!strchr(cur->interface, '/'))
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
					} else if (!strcasecmp(var->name, "monitor-join")) {
						ast_set2_flag(q, ast_true(var->value), QUEUE_FLAG_MONJOIN);
					} else if (!strcasecmp(var->name, "monitor-format")) {
						strncpy(q->monfmt, var->value, sizeof(q->monfmt) - 1);
					} else if (!strcasecmp(var->name, "queue-youarenext")) {
						strncpy(q->sound_next, var->value, sizeof(q->sound_next) - 1);
					} else if (!strcasecmp(var->name, "queue-thereare")) {
						strncpy(q->sound_thereare, var->value, sizeof(q->sound_thereare) - 1);
					} else if (!strcasecmp(var->name, "queue-callswaiting")) {
						strncpy(q->sound_calls, var->value, sizeof(q->sound_calls) - 1);
					} else if (!strcasecmp(var->name, "queue-holdtime")) {
						strncpy(q->sound_holdtime, var->value, sizeof(q->sound_holdtime) - 1);
					} else if (!strcasecmp(var->name, "queue-minutes")) {
						strncpy(q->sound_minutes, var->value, sizeof(q->sound_minutes) - 1);
					} else if (!strcasecmp(var->name, "queue-seconds")) {
						strncpy(q->sound_seconds, var->value, sizeof(q->sound_seconds) - 1);
					} else if (!strcasecmp(var->name, "queue-lessthan")) {
						strncpy(q->sound_lessthan, var->value, sizeof(q->sound_lessthan) - 1);
					} else if (!strcasecmp(var->name, "queue-thankyou")) {
						strncpy(q->sound_thanks, var->value, sizeof(q->sound_thanks) - 1);
					} else if (!strcasecmp(var->name, "queue-reporthold")) {
						strncpy(q->sound_reporthold, var->value, sizeof(q->sound_reporthold) - 1);
					} else if (!strcasecmp(var->name, "announce-frequency")) {
						q->announcefrequency = atoi(var->value);
					} else if (!strcasecmp(var->name, "announce-round-seconds")) {
						q->roundingseconds = atoi(var->value);
						if(q->roundingseconds>60 || q->roundingseconds<0) {
							ast_log(LOG_WARNING, "'%s' isn't a valid value for queue-rounding-seconds using 0 instead at line %d of queue.conf\n", var->value, var->lineno);
							q->roundingseconds=0;
						}
					} else if (!strcasecmp(var->name, "announce-holdtime")) {
						q->announceholdtime = (!strcasecmp(var->value,"once")) ? 1 : ast_true(var->value);
					} else if (!strcasecmp(var->name, "retry")) {
						q->retry = atoi(var->value);
					} else if (!strcasecmp(var->name, "wrapuptime")) {
						q->wrapuptime = atoi(var->value);
					} else if (!strcasecmp(var->name, "maxlen")) {
						q->maxlen = atoi(var->value);
					} else if (!strcasecmp(var->name, "servicelevel")) {
						q->servicelevel= atoi(var->value);
					} else if (!strcasecmp(var->name, "strategy")) {
						q->strategy = strat2int(var->value);
						if (q->strategy < 0) {
							ast_log(LOG_WARNING, "'%s' isn't a valid strategy, using ringall instead\n", var->value);
							q->strategy = 0;
						}
					} else if (!strcasecmp(var->name, "joinempty")) {
						ast_set2_flag(q, ast_true(var->value), QUEUE_FLAG_JOINEMPTY);
					} else if (!strcasecmp(var->name, "leavewhenempty")) {
						ast_set2_flag(q, ast_true(var->value), QUEUE_FLAG_LEAVEWHENEMPTY);
					} else if (!strcasecmp(var->name, "eventwhencalled")) {
						ast_set2_flag(q, ast_true(var->value), QUEUE_FLAG_EVENTWHENCALLED);
					} else if (!strcasecmp(var->name, "reportholdtime")) {
						ast_set2_flag(q, ast_true(var->value), QUEUE_FLAG_REPORTHOLDTIME);
					} else if (!strcasecmp(var->name, "memberdelay")) {
						q->memberdelay = atoi(var->value);
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
					ast_mutex_unlock(&q->lock);
				if (new) {
					q->next = queues;
					queues = q;
				}
			}
		} else {
			/* Initialize global settings */
			queue_persistent_members = 0;
			if ((general_val = ast_variable_retrieve(cfg, "general", "persistentmembers")))
			    queue_persistent_members = ast_true(general_val);
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_destroy(cfg);
	q = queues;
	ql = NULL;
	while(q) {
		qn = q->next;
		if (ast_test_flag(q, QUEUE_FLAG_DEAD)) {
			if (ql)
				ql->next = q->next;
			else
				queues = q->next;
			if (!q->count) {
				free(q);
			} else
				ast_log(LOG_WARNING, "XXX Leaking a little memory :( XXX\n");
		} else {
			for (cur = q->members; cur; cur = cur->next)
				cur->status = ast_device_state(cur->interface);
			ql = q;
		}
		q = qn;
	}
	ast_mutex_unlock(&qlock);
}

static char *status2str(int status, char *buf, int buflen)
{
	switch(status) {
	case AST_DEVICE_UNKNOWN:
		strncpy(buf, "unknown", buflen - 1);
		break;
	case AST_DEVICE_NOT_INUSE:
		strncpy(buf, "notinuse", buflen - 1);
		break;
	case AST_DEVICE_INUSE:
		strncpy(buf, "inuse", buflen - 1);
		break;
	case AST_DEVICE_BUSY:
		strncpy(buf, "busy", buflen - 1);
		break;
	case AST_DEVICE_INVALID:
		strncpy(buf, "invalid", buflen - 1);
		break;
	case AST_DEVICE_UNAVAILABLE:
		strncpy(buf, "unavailable", buflen - 1);
		break;
	default:
		snprintf(buf, buflen, "unknown status %d", status);
	}
	return buf;
}

static int __queues_show(int fd, int argc, char **argv, int queue_show)
{
	struct ast_call_queue *q;
	struct queue_ent *qe;
	struct member *mem;
	int pos;
	time_t now;
	char max[80] = "";
	char calls[80] = "";
	char tmpbuf[80] = "";
	float sl = 0;

	time(&now);
	if ((!queue_show && argc != 2) || (queue_show && argc != 3))
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&qlock);
	q = queues;
	if (!q) {	
		ast_mutex_unlock(&qlock);
		if (queue_show)
			ast_cli(fd, "No such queue: %s.\n",argv[2]);
		else
			ast_cli(fd, "No queues.\n");
		return RESULT_SUCCESS;
	}
	while(q) {
		ast_mutex_lock(&q->lock);
		if (queue_show) {
			if (strcasecmp(q->name, argv[2]) != 0) {
				ast_mutex_unlock(&q->lock);
				q = q->next;
				if (!q) {
					ast_cli(fd, "No such queue: %s.\n",argv[2]);
					break;
				}
				continue;
			}
		}
		if (q->maxlen)
			snprintf(max, sizeof(max), "%d", q->maxlen);
		else
			strncpy(max, "unlimited", sizeof(max) - 1);
		sl = 0;
		if(q->callscompleted > 0)
			sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
		ast_cli(fd, "%-12.12s has %d calls (max %s) in '%s' strategy (%ds holdtime), C:%d, A:%d, SL:%2.1f%% within %ds\n",
			q->name, q->count, max, int2strat(q->strategy), q->holdtime, q->callscompleted, q->callsabandoned,sl,q->servicelevel);
		if (q->members) {
			ast_cli(fd, "   Members: \n");
			for (mem = q->members; mem; mem = mem->next) {
				if (mem->penalty)
					snprintf(max, sizeof(max) - 20, " with penalty %d", mem->penalty);
				else
					max[0] = '\0';
				if (mem->dynamic)
					strncat(max, " (dynamic)", sizeof(max) - strlen(max) - 1);
				if (mem->status)
					snprintf(max + strlen(max), sizeof(max) - strlen(max), " (%s)", status2str(mem->status, tmpbuf, sizeof(tmpbuf)));
				if (mem->calls) {
					snprintf(calls, sizeof(calls), " has taken %d calls (last was %ld secs ago)",
							mem->calls, (long)(time(NULL) - mem->lastcall));
				} else
					strncpy(calls, " has taken no calls yet", sizeof(calls) - 1);
				ast_cli(fd, "      %s%s%s\n", mem->interface, max, calls);
			}
		} else
			ast_cli(fd, "   No Members\n");
		if (q->head) {
			pos = 1;
			ast_cli(fd, "   Callers: \n");
			for (qe = q->head; qe; qe = qe->next) 
				ast_cli(fd, "      %d. %s (wait: %ld:%2.2ld, prio: %d)\n", pos++, qe->chan->name,
								(long)(now - qe->start) / 60, (long)(now - qe->start) % 60, qe->prio);
		} else
			ast_cli(fd, "   No Callers\n");
		ast_cli(fd, "\n");
		ast_mutex_unlock(&q->lock);
		q = q->next;
		if (queue_show)
			break;
	}
	ast_mutex_unlock(&qlock);
	return RESULT_SUCCESS;
}

static int queues_show(int fd, int argc, char **argv)
{
	return __queues_show(fd, argc, argv, 0);
}

static int queue_show(int fd, int argc, char **argv)
{
	return __queues_show(fd, argc, argv, 1);
}

static char *complete_queue(char *line, char *word, int pos, int state)
{
	struct ast_call_queue *q;
	int which=0;
	
	ast_mutex_lock(&qlock);
	for (q = queues; q; q = q->next) {
		if (!strncasecmp(word, q->name, strlen(word))) {
			if (++which > state)
				break;
		}
	}
	ast_mutex_unlock(&qlock);
	return q ? strdup(q->name) : NULL;
}

/* JDG: callback to display queues status in manager */
static int manager_queues_show( struct mansession *s, struct message *m )
{
	char *a[] = { "show", "queues" };
	return queues_show( s->fd, 2, a );
} /* /JDG */


/* Dump queue status */
static int manager_queues_status( struct mansession *s, struct message *m )
{
	time_t now;
	int pos;
	char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	struct ast_call_queue *q;
	struct queue_ent *qe;
	float sl = 0;
	struct member *mem;
	astman_send_ack(s, m, "Queue status will follow");
	time(&now);
	ast_mutex_lock(&qlock);
	if (!ast_strlen_zero(id)) {
		snprintf(idText,256,"ActionID: %s\r\n",id);
	}
	for (q = queues; q; q = q->next) {
		ast_mutex_lock(&q->lock);

		/* List queue properties */
		if(q->callscompleted > 0)
			sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
		ast_mutex_lock(&s->lock);
		ast_cli(s->fd, "Event: QueueParams\r\n"
					"Queue: %s\r\n"
					"Max: %d\r\n"
					"Calls: %d\r\n"
					"Holdtime: %d\r\n"
					"Completed: %d\r\n"
					"Abandoned: %d\r\n"
					"ServiceLevel: %d\r\n"
					"ServicelevelPerf: %2.1f\r\n"
					"%s"
					"\r\n",
						q->name, q->maxlen, q->count, q->holdtime, q->callscompleted,
						q->callsabandoned, q->servicelevel, sl, idText);

		/* List Queue Members */
		for (mem = q->members; mem; mem = mem->next) 
			ast_cli(s->fd, "Event: QueueMember\r\n"
				"Queue: %s\r\n"
				"Location: %s\r\n"
				"Membership: %s\r\n"
				"Penalty: %d\r\n"
				"CallsTaken: %d\r\n"
				"LastCall: %ld\r\n"
				"Status: %d\r\n"
				"%s"
				"\r\n",
					q->name, mem->interface, mem->dynamic ? "dynamic" : "static",
					mem->penalty, mem->calls, mem->lastcall, mem->status, idText);

		/* List Queue Entries */

		pos = 1;
		for (qe = q->head; qe; qe = qe->next) 
			ast_cli(s->fd, "Event: QueueEntry\r\n"
				"Queue: %s\r\n"
				"Position: %d\r\n"
				"Channel: %s\r\n"
				"CallerID: %s\r\n"
				"CallerIDName: %s\r\n"
				"Wait: %ld\r\n"
				"%s"
				"\r\n", 
					q->name, pos++, qe->chan->name, 
					qe->chan->cid.cid_num ? qe->chan->cid.cid_num : "unknown",
					qe->chan->cid.cid_name ? qe->chan->cid.cid_name : "unknown",
					(long)(now - qe->start), idText);
		ast_mutex_unlock(&s->lock);
		ast_mutex_unlock(&q->lock);
	}
	ast_mutex_unlock(&qlock);
	return RESULT_SUCCESS;
}

static int manager_add_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface, *penalty_s;
	int penalty = 0;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");
	penalty_s = astman_get_header(m, "Penalty");

	if (ast_strlen_zero(queuename)) {
		astman_send_error(s, m, "'Queue' not specified.");
		return 0;
	}

	if (ast_strlen_zero(interface)) {
		astman_send_error(s, m, "'Interface' not specified.");
		return 0;
	}

	if (ast_strlen_zero(penalty_s))
		penalty = 0;
	else if (sscanf(penalty_s, "%d", &penalty) != 1) {
		penalty = 0;
	}

	switch (add_to_queue(queuename, interface, penalty)) {
	case RES_OKAY:
		astman_send_ack(s, m, "Added interface to queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to add interface: Already there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to add interface to queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	}
	return 0;
}

static int manager_remove_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");

	if (ast_strlen_zero(queuename) || ast_strlen_zero(interface)) {
		astman_send_error(s, m, "Need 'Queue' and 'Interface' parameters.");
		return 0;
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		astman_send_ack(s, m, "Removed interface from queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to remove interface: Not there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to remove interface from queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	}
	return 0;
}

static int handle_add_queue_member(int fd, int argc, char *argv[])
{
	char *queuename, *interface;
	int penalty;

	if ((argc != 6) && (argc != 8)) {
		return RESULT_SHOWUSAGE;
	} else if (strcmp(argv[4], "to")) {
		return RESULT_SHOWUSAGE;
	} else if ((argc == 8) && strcmp(argv[6], "penalty")) {
		return RESULT_SHOWUSAGE;
	}

	queuename = argv[5];
	interface = argv[3];
	if (argc == 8) {
		if (sscanf(argv[7], "%d", &penalty) == 1) {
			if (penalty < 0) {
				ast_cli(fd, "Penalty must be >= 0\n");
				penalty = 0;
			}
		} else {
			ast_cli(fd, "Penalty must be an integer >= 0\n");
			penalty = 0;
		}
	} else {
		penalty = 0;
	}

	switch (add_to_queue(queuename, interface, penalty)) {
	case RES_OKAY:
		ast_cli(fd, "Added interface '%s' to queue '%s'\n", interface, queuename);
		return RESULT_SUCCESS;
	case RES_EXISTS:
		ast_cli(fd, "Unable to add interface '%s' to queue '%s': Already there\n", interface, queuename);
		return RESULT_FAILURE;
	case RES_NOSUCHQUEUE:
		ast_cli(fd, "Unable to add interface to queue '%s': No such queue\n", queuename);
		return RESULT_FAILURE;
	case RES_OUTOFMEMORY:
		ast_cli(fd, "Out of memory\n");
		return RESULT_FAILURE;
	default:
		return RESULT_FAILURE;
	}
}

static char *complete_add_queue_member(char *line, char *word, int pos, int state)
{
	/* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - to; 5 - <queue>; 6 - penalty; 7 - <penalty> */
	switch (pos) {
	case 3:
		/* Don't attempt to complete name of member (infinite possibilities) */
		return NULL;
	case 4:
		if (state == 0) {
			return strdup("to");
		} else {
			return NULL;
		}
	case 5:
		/* No need to duplicate code */
		return complete_queue(line, word, pos, state);
	case 6:
		if (state == 0) {
			return strdup("penalty");
		} else {
			return NULL;
		}
	case 7:
		if (state < 100) {	/* 0-99 */
			char *num = malloc(3);
			if (num) {
				sprintf(num, "%d", state);
			}
			return num;
		} else {
			return NULL;
		}
	default:
		return NULL;
	}
}

static int handle_remove_queue_member(int fd, int argc, char *argv[])
{
	char *queuename, *interface;

	if (argc != 6) {
		return RESULT_SHOWUSAGE;
	} else if (strcmp(argv[4], "from")) {
		return RESULT_SHOWUSAGE;
	}

	queuename = argv[5];
	interface = argv[3];

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		ast_cli(fd, "Removed interface '%s' from queue '%s'\n", interface, queuename);
		return RESULT_SUCCESS;
	case RES_EXISTS:
		ast_cli(fd, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
		return RESULT_FAILURE;
	case RES_NOSUCHQUEUE:
		ast_cli(fd, "Unable to remove interface from queue '%s': No such queue\n", queuename);
		return RESULT_FAILURE;
	case RES_OUTOFMEMORY:
		ast_cli(fd, "Out of memory\n");
		return RESULT_FAILURE;
	default:
		return RESULT_FAILURE;
	}
}

static char *complete_remove_queue_member(char *line, char *word, int pos, int state)
{
	int which = 0;
	struct ast_call_queue *q;
	struct member *m;

	/* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - to; 5 - <queue> */
	if ((pos > 5) || (pos < 3)) {
		return NULL;
	}
	if (pos == 4) {
		if (state == 0) {
			return strdup("from");
		} else {
			return NULL;
		}
	}

	if (pos == 5) {
		/* No need to duplicate code */
		return complete_queue(line, word, pos, state);
	}

	if (queues != NULL) {
		for (q = queues ; q ; q = q->next) {
			ast_mutex_lock(&q->lock);
			for (m = q->members ; m ; m = m->next) {
				if (++which > state) {
					ast_mutex_unlock(&q->lock);
					return strdup(m->interface);
				}
			}
			ast_mutex_unlock(&q->lock);
		}
	}
	return NULL;
}

static char show_queues_usage[] = 
"Usage: show queues\n"
"       Provides summary information on call queues.\n";

static struct ast_cli_entry cli_show_queues = {
	{ "show", "queues", NULL }, queues_show, 
	"Show status of queues", show_queues_usage, NULL };

static char show_queue_usage[] = 
"Usage: show queue\n"
"       Provides summary information on a specified queue.\n";

static struct ast_cli_entry cli_show_queue = {
	{ "show", "queue", NULL }, queue_show, 
	"Show status of a specified queue", show_queue_usage, complete_queue };

static char aqm_cmd_usage[] =
"Usage: add queue member <channel> to <queue> [penalty <penalty>]\n";

static struct ast_cli_entry cli_add_queue_member = {
	{ "add", "queue", "member", NULL }, handle_add_queue_member,
	"Add a channel to a specified queue", aqm_cmd_usage, complete_add_queue_member };

static char rqm_cmd_usage[] =
"Usage: remove queue member <channel> from <queue>\n";

static struct ast_cli_entry cli_remove_queue_member = {
	{ "remove", "queue", "member", NULL }, handle_remove_queue_member,
	"Removes a channel from a specified queue", rqm_cmd_usage, complete_remove_queue_member };

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&cli_show_queue);
	ast_cli_unregister(&cli_show_queues);
	ast_cli_unregister(&cli_add_queue_member);
	ast_cli_unregister(&cli_remove_queue_member);
	ast_manager_unregister("Queues");
	ast_manager_unregister("QueueStatus");
	ast_manager_unregister("QueueAdd");
	ast_manager_unregister("QueueRemove");
	ast_devstate_del(statechange_queue, NULL);
	ast_unregister_application(app_aqm);
	ast_unregister_application(app_rqm);
	return ast_unregister_application(app);
}

int load_module(void)
{
	int res;
	res = ast_register_application(app, queue_exec, synopsis, descrip);
	if (!res) {
		ast_cli_register(&cli_show_queue);
		ast_cli_register(&cli_show_queues);
		ast_cli_register(&cli_add_queue_member);
		ast_cli_register(&cli_remove_queue_member);
		ast_devstate_add(statechange_queue, NULL);
		ast_manager_register( "Queues", 0, manager_queues_show, "Queues" );
		ast_manager_register( "QueueStatus", 0, manager_queues_status, "Queue Status" );
		ast_manager_register( "QueueAdd", EVENT_FLAG_AGENT, manager_add_queue_member, "Add interface to queue." );
		ast_manager_register( "QueueRemove", EVENT_FLAG_AGENT, manager_remove_queue_member, "Remove interface from queue." );
		ast_register_application(app_aqm, aqm_exec, app_aqm_synopsis, app_aqm_descrip) ;
		ast_register_application(app_rqm, rqm_exec, app_rqm_synopsis, app_rqm_descrip) ;
	}
	reload_queues();
	
	if (queue_persistent_members)
	    reload_queue_members();

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
