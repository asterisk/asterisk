/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * True call queues with optional send URL on answer
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
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
 * Fixed ot work with CVS as of 2004-02-25 and released as 1.07a
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
"      'H' -- allow caller to hang up by hitting *.\n"
"      'n' -- no retries on the timeout; will exit this application and go to the next step.\n"
"      'r' -- ring instead of playing MOH\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n"
"  The optional URL will be sent to the called party if the channel supports\n"
"it.\n"
"  The timeout will cause the queue to fail out after a specified number of\n"
"seconds, checked between each queues.conf 'timeout' and 'retry' cycle.\n";

// [PHM 06/26/03]
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

/* We define a customer "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct localuser {
	struct ast_channel *chan;
	char numsubst[256];
	char tech[40];
	int stillgoing;
	int metric;
	int allowredirect_in;
	int allowredirect_out;
	int ringbackonly;
	int musiconhold;
	int dataquality;
	int allowdisconnect;
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
	int queuetimeout;               /* How many seconds before timing out of queue */
	struct ast_channel *chan;	/* Our channel */
	struct queue_ent *next;		/* The next queue entry */
};

struct member {
	char tech[80];				/* Technology */
	char loc[256];				/* Location */
	int penalty;				/* Are we a last resort? */
	int calls;					/* Number of calls serviced by this member */
	int dynamic;				/* Are we dynamically added? */
	time_t lastcall;			/* When last successful call was hungup */
	struct member *next;		/* Next member */
};

struct ast_call_queue {
	ast_mutex_t	lock;	
	char name[80];			/* Name of the queue */
	char moh[80];			/* Name of musiconhold to be used */
	char announce[80];		/* Announcement to play when call is answered */
	char context[80];		/* Context for this queue */
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
	int monjoin;                    /* Should we join the two files when we are done with the call */
	char sound_next[80];            /* Sound file: "Your call is now first in line" (def. queue-youarenext) */
	char sound_thereare[80];        /* Sound file: "There are currently" (def. queue-thereare) */
	char sound_calls[80];           /* Sound file: "calls waiting to speak to a representative." (def. queue-callswaiting)*/
	char sound_holdtime[80];        /* Sound file: "The current estimated total holdtime is" (def. queue-holdtime) */
	char sound_minutes[80];         /* Sound file: "minutes." (def. queue-minutes) */
	char sound_seconds[80];         /* Sound file: "seconds." (def. queue-seconds) */
	char sound_thanks[80];          /* Sound file: "Thank you for your patience." (def. queue-thankyou) */

	int count;			/* How many entries are in the queue */
	int maxlen;			/* Max number of entries in queue */
	int wrapuptime;		/* Wrapup Time */

	int dead;			/* Whether this queue is dead or not */
	int retry;			/* Retry calling everyone after this amount of time */
	int timeout;			/* How long to wait for an answer */
	
	/* Queue strategy things */
	
	int rrpos;			/* Round Robin - position */
	int wrapped;			/* Round Robin - wrapped around? */
	int joinempty;			/* Do we care if the queue has no members? */
	int eventwhencalled;			/* Generate an event when the agent is called (before pickup) */

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

static int join_queue(char *queuename, struct queue_ent *qe)
{
	struct ast_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	int inserted = 0;

	ast_mutex_lock(&qlock);
	q = queues;
	while(q) {
		if (!strcasecmp(q->name, queuename)) {
			/* This is our one */
			ast_mutex_lock(&q->lock);
			if ((q->members || q->joinempty) && (!q->maxlen || (q->count < q->maxlen))) {
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
					"Channel: %s\r\nCallerID: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\n",
					qe->chan->name, (qe->chan->callerid ? qe->chan->callerid : "unknown"), q->name, qe->pos, q->count );
#if 0
ast_log(LOG_NOTICE, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, qe->chan->name, qe->pos );
#endif
			}
			ast_mutex_unlock(&q->lock);
			break;
		}
		q = q->next;
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
		if (outgoing->chan && (outgoing->chan != exception))
			ast_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->next;
		free(oo);
	}
}

static int ring_entry(struct queue_ent *qe, struct localuser *tmp)
{
	int res;
	if (qe->parent->wrapuptime && (time(NULL) - tmp->lastcall < qe->parent->wrapuptime)) {
		ast_log(LOG_DEBUG, "Wrapuptime not yet expired for %s/%s\n", tmp->tech, tmp->numsubst);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}
	/* Request the peer */
	tmp->chan = ast_request(tmp->tech, qe->chan->nativeformats, tmp->numsubst);
	if (!tmp->chan) {			/* If we can't, just go on to the next call */
#if 0
		ast_log(LOG_NOTICE, "Unable to create channel of type '%s'\n", cur->tech);
#endif			
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}
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
	res = ast_call(tmp->chan, tmp->numsubst, 0);
	if (res) {
		/* Again, keep going even if there's an error */
		if (option_debug)
			ast_log(LOG_DEBUG, "ast call on peer returned %d\n", res);
		else if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", tmp->numsubst);
		ast_hangup(tmp->chan);
		tmp->chan = NULL;
		tmp->stillgoing = 0;
		return 0;
	} else {
		if (qe->parent->eventwhencalled) {
			manager_event(EVENT_FLAG_AGENT, "AgentCalled",
						"AgentCalled: %s/%s\r\n"
						"ChannelCalling: %s\r\n"
						"CallerID: %s\r\n"
						"Context: %s\r\n"
						"Extension: %s\r\n"
						"Priority: %d\r\n",
						tmp->tech, tmp->numsubst, qe->chan->name,
						tmp->chan->callerid ? tmp->chan->callerid : "unknown <>",
						qe->chan->context, qe->chan->exten, qe->chan->priority);
		}
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Called %s/%s\n", tmp->tech, tmp->numsubst);
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
						ast_log(LOG_DEBUG, "(Parallel) Trying '%s/%s' with metric %d\n", cur->tech, cur->numsubst, cur->metric);
						ring_entry(qe, cur);
					}
					cur = cur->next;
				}
			} else {
				/* Ring just the best channel */
				if (option_debug)
					ast_log(LOG_DEBUG, "Trying '%s/%s' with metric %d\n", 
									best->tech, best->numsubst, best->metric);
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
		ast_log(LOG_DEBUG, "Next is '%s/%s' with metric %d\n", best->tech, best->numsubst, best->metric);
		qe->parent->rrpos = best->metric % 1000;
	} else {
		/* Just increment rrpos */
		if (!qe->parent->wrapped) {
			/* No more channels, start over */
			qe->parent->rrpos = 0;
		} else {
			/* Prioritize next entry */
			qe->parent->rrpos++;
		}
	}
	qe->parent->wrapped = 0;
	return 0;
}

static int valid_exit(struct queue_ent *qe, char digit)
{
	char tmp[2];
	if (ast_strlen_zero(qe->context))
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

#define AST_MAX_WATCHERS 256

static struct localuser *wait_for_answer(struct queue_ent *qe, struct localuser *outgoing, int *to, int *allowredir_in, int *allowredir_out, int *allowdisconnect, char *digit)
{
	char *queue = qe->parent->name;
	struct localuser *o;
	int found;
	int numlines;
	int sentringing = 0;
	int numbusies = 0;
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
			if (numlines == numbusies) {
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
					*allowredir_in = o->allowredirect_in;
					*allowredir_out = o->allowredirect_out;
					*allowdisconnect = o->allowdisconnect;
				}
			} else if (o->chan && (o->chan == winner)) {
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
								*allowredir_in = o->allowredirect_in;
								*allowredir_out = o->allowredirect_out;
								*allowdisconnect = o->allowdisconnect;
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
			if (f && (f->frametype == AST_FRAME_DTMF) && allowdisconnect && (f->subclass == '*')) {
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
	struct queue_ent *ch;
	int res = 0;
	time_t now;

	/* This is the holding pen for callers 2 through maxlen */
	for (;;) {
		/* Atomically read the parent head -- does not need a lock */
		ch = qe->parent->head;

		/* If we are now at the top of the head, break out */
		if (ch == qe) {
			if (option_debug)
				ast_log(LOG_DEBUG, "It's our turn (%s).\n", qe->chan->name);
			break;
		}

		/* If we have timed out, break out */
		if ( qe->queuetimeout ) {
			time(&now);
			if ( (now - qe->start) >= qe->queuetimeout )
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
			if (!q->wrapped) {
				/* No more channels, start over */
				q->rrpos = 0;
			} else {
				/* Prioritize next entry */
				q->rrpos++;
			}
			q->wrapped = 0;
		}
		/* Fall through */
	case QUEUE_STRATEGY_RRMEMORY:
		if (pos < q->rrpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > q->rrpos) {
				/* Indicate there is another priority */
				q->wrapped = 1;
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
	int allowredir_in=0;
	int allowredir_out=0;
	int allowdisconnect=0;
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
		if (options) {
			if (strchr(options, 't'))
				tmp->allowredirect_in = 1;
			if (strchr(options, 'T'))
				tmp->allowredirect_out = 1;
			if (strchr(options, 'r'))
				tmp->ringbackonly = 1;
			if (strchr(options, 'm'))
				tmp->musiconhold = 1;
			if (strchr(options, 'd'))
				tmp->dataquality = 1;
			if (strchr(options, 'H'))
				tmp->allowdisconnect = 1;
			if ((strchr(options, 'n')) && (now - qe->start >= qe->parent->timeout))
				*go_on = 1;
		}
		if (option_debug) {
			if (url)
				ast_log(LOG_DEBUG, "Queue with URL=%s_\n", url);
			else 
				ast_log(LOG_DEBUG, "Simple queue (no URL)\n");
		}

		tmp->member = cur;		/* Never directly dereference!  Could change on reload */
		strncpy(tmp->tech, cur->tech, sizeof(tmp->tech)-1);
		strncpy(tmp->numsubst, cur->loc, sizeof(tmp->numsubst)-1);
		tmp->lastcall = cur->lastcall;
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(tmp->numsubst, "BYEXTENSION"))) {
			strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit)-1);
			snprintf(newnum, sizeof(tmp->numsubst) - (newnum - tmp->numsubst), "%s%s", qe->chan->exten,restofit);
			if (option_debug)
				ast_log(LOG_DEBUG, "Dialing by extension %s\n", tmp->numsubst);
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
	lpeer = wait_for_answer(qe, outgoing, &to, &allowredir_in, &allowredir_out, &allowdisconnect, &digit);
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
			if (tmp->dataquality) zapx = 0;
			ast_channel_setoption(qe->chan,AST_OPTION_TONE_VERIFY,&zapx,sizeof(char),0);
		}			
		if (!strcmp(peer->type,"Zap")) {
			if (tmp->dataquality) zapx = 0;
			ast_channel_setoption(peer,AST_OPTION_TONE_VERIFY,&zapx,sizeof(char),0);
		}
		/* Update parameters for the queue */
		recalc_holdtime(qe);
		member = lpeer->member;
		hanguptree(outgoing, peer);
		outgoing = NULL;
		if (announce) {
			int res2;
			res2 = ast_autoservice_start(qe->chan);
			if (!res2) {
				res2 = ast_streamfile(peer, announce, peer->language);
				if (!res2)
					res2 = ast_waitstream(peer, "");
				else {
					ast_log(LOG_WARNING, "Announcement file '%s' is unavailable, continuing anyway...\n", announce);
					res2 = 0;
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
			if(qe->parent->monjoin) {
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
        config.allowredirect_in = allowredir_in;
        config.allowredirect_out = allowredir_out;
        config.allowdisconnect = allowdisconnect;
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

// [PHM 06/26/03]

static struct member * interface_exists( struct ast_call_queue * q, char * interface )
{
	struct member * ret = NULL ;
	struct member *mem;
	char buf[500] ;

	if( q != NULL )
	{
		mem = q->members ;

		while( mem != NULL ) {
			snprintf( buf, sizeof(buf), "%s/%s", mem->tech, mem->loc);

			if( strcmp( buf, interface ) == 0 ) {
				ret = mem ;
				break ;
			}
			else
				mem = mem->next ;
		}
	}

	return( ret ) ;
}


static struct member * create_queue_node( char * interface, int penalty )
{
	struct member * cur ;
	char * tmp ;
	
	/* Add a new member */

	cur = malloc(sizeof(struct member));

	if (cur) {
		memset(cur, 0, sizeof(struct member));
		cur->penalty = penalty;
		strncpy(cur->tech, interface, sizeof(cur->tech) - 1);
		if ((tmp = strchr(cur->tech, '/')))
			*tmp = '\0';
		if ((tmp = strchr(interface, '/'))) {
			tmp++;
			strncpy(cur->loc, tmp, sizeof(cur->loc) - 1);
		} else
			ast_log(LOG_WARNING, "No location at interface '%s'\n", interface);
	}

	return( cur ) ;
}


static int rqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *queuename;
	struct member * node ;
	struct member * look ;
	char info[512];
	char tmpchan[256]="";
	char *interface=NULL;
	struct ast_call_queue *q;
	int found=0 ;

	if (!data) {
		ast_log(LOG_WARNING, "RemoveQueueMember requires an argument (queuename|optional interface)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u); // not sure if we need this, but better be safe than sorry ;-)
	
	/* Parse our arguments XXX Check for failure XXX */
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION-1);
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

	if( ( q = queues) != NULL )
	{
		while( q && ( res != 0 ) && (!found) ) 
		{
			ast_mutex_lock(&q->lock);
			if( strcmp( q->name, queuename) == 0 )
			{
				// found queue, try to remove  interface
				found=1 ;

				if( ( node = interface_exists( q, interface ) ) != NULL )
				{
					if( ( look = q->members ) == node )
					{
						// 1st
						q->members = node->next;
					}
					else
					{
						while( look != NULL )
							if( look->next == node )
							{
								look->next = node->next ;
								break ;
							}
							else
								look = look->next ;
					}

					free( node ) ;

					ast_log(LOG_NOTICE, "Removed interface '%s' to queue '%s'\n", 
						interface, queuename);
					res = 0 ;
				}
				else
				{
					ast_log(LOG_WARNING, "Unable to remove interface '%s' from queue '%s': "
						"Not there\n", interface, queuename);
	                                if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
						{
						chan->priority += 100;
						res = 0 ;
						}
				}
			}

			ast_mutex_unlock(&q->lock);
			q = q->next;
		}
	}

	if( ! found )
		ast_log(LOG_WARNING, "Unable to remove interface from queue '%s': No such queue\n", queuename);

	LOCAL_USER_REMOVE(u);
	return res;
}



static int aqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *queuename;
	char info[512];
	char tmpchan[512]="";
	char *interface=NULL;
	char *penaltys=NULL;
	int penalty = 0;
	struct ast_call_queue *q;
	struct member *save;
	int found=0 ;

	if (!data) {
		ast_log(LOG_WARNING, "AddQueueMember requires an argument (queuename|optional interface|optional penalty)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u); // not sure if we need this, but better be safe than sorry ;-)
	
	/* Parse our arguments XXX Check for failure XXX */
	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION-1);
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
				*penaltys = 0;
				penaltys++;
			}
		}
		if (!interface || !strlen(interface)) {
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

	if( ( q = queues) != NULL )
	{
		while( q && ( res != 0 ) && (!found) ) 
		{
			ast_mutex_lock(&q->lock);
			if( strcmp( q->name, queuename) == 0 )
			{
				// found queue, try to enable interface
				found=1 ;

				if( interface_exists( q, interface ) == NULL )
				{
					save = q->members ;
					q->members = create_queue_node( interface, penalty ) ;

					if( q->members != NULL ) {
						q->members->dynamic = 1;
						q->members->next = save ;
					} else
						q->members = save ;

					ast_log(LOG_NOTICE, "Added interface '%s' to queue '%s'\n", interface, queuename);
					res = 0 ;
				}
				else
				{
					ast_log(LOG_WARNING, "Unable to add interface '%s' to queue '%s': "
						"Already there\n", interface, queuename);
			                if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
                                        {
                                                chan->priority += 100;
                                                res = 0 ;
                                        }
				}
			}

			ast_mutex_unlock(&q->lock);
			q = q->next;
		}
	}

	if( ! found )
		ast_log(LOG_WARNING, "Unable to add interface to queue '%s': No such queue\n", queuename);

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
		ast_log(LOG_WARNING, "Queue requires an argument (queuename|optional timeout|optional URL)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	/* Setup our queue entry */
	memset(&qe, 0, sizeof(qe));
	
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
					queuetimeoutstr = strchr(announceoverride, '|');
					if (queuetimeoutstr) {
						*queuetimeoutstr = '\0';
						queuetimeoutstr++;
						qe.queuetimeout = atoi(queuetimeoutstr);
					} else {
						qe.queuetimeout = 0;
					}
				}
			}
		}
	}

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

//	if (option_debug) 
		ast_log(LOG_DEBUG, "queue: %s, options: %s, url: %s, announce: %s, timeout: %d, priority: %d\n",
				queuename, options, url, announceoverride, qe.queuetimeout, (int)prio);

	qe.chan = chan;
	qe.start = time(NULL);
	qe.prio = (int)prio;
	qe.last_pos_said = 0;
	qe.last_pos = 0;
	if (!join_queue(queuename, &qe)) {
		ast_queue_log(queuename, chan->uniqueid, "NONE", "ENTERQUEUE", "%s|%s", url ? url : "", chan->callerid ? chan->callerid : "");
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
			for (;;) {
				/* This is the wait loop for the head caller*/
				/* To exit, they may get their call answered; */
				/* they may dial a digit from the queue context; */
				/* or, they may may timeout. */

				/* Leave if we have exceeded our queuetimeout */
				if (qe.queuetimeout && ( (time(NULL) - qe.start) >= qe.queuetimeout) ) {
					res = 0;
					break;
				}

				/* Make a position announcement, if enabled */
				if (qe.parent->announcefrequency && !ringing)
					say_position(&qe);

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

				/* Leave if we have exceeded our queuetimeout */
				if (qe.queuetimeout && ( (time(NULL) - qe.start) >= qe.queuetimeout) ) {
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
		if (res > 0 && res != AST_PBX_KEEPALIVE) {
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
	cfg = ast_load("queues.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "No call queueing config file, so no call queues\n");
		return;
	}
	ast_mutex_lock(&qlock);
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
				q->dead = 0;
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
							strncpy(cur->tech, var->value, sizeof(cur->tech) - 1);
							if ((tmp = strchr(cur->tech, ','))) {
								*tmp = '\0';
								tmp++;
								cur->penalty = atoi(tmp);
								if (cur->penalty < 0)
									cur->penalty = 0;
							}
							if ((tmp = strchr(cur->tech, '/')))
								*tmp = '\0';
							if ((tmp = strchr(var->value, '/'))) {
								tmp++;
								strncpy(cur->loc, tmp, sizeof(cur->loc) - 1);
								if ((tmp = strchr(cur->loc, ',')))
									*tmp = '\0';
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
					} else if (!strcasecmp(var->name, "monitor-join")) {
						q->monjoin = ast_true(var->value);
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
					} else if (!strcasecmp(var->name, "queue-thankyou")) {
						strncpy(q->sound_thanks, var->value, sizeof(q->sound_thanks) - 1);
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
						q->joinempty = ast_true(var->value);
					} else if (!strcasecmp(var->name, "eventwhencalled")) {
						q->eventwhencalled = ast_true(var->value);
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
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_destroy(cfg);
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
				ast_log(LOG_WARNING, "XXX Leaking a little memory :( XXX\n");
		} else
			ql = q;
		q = qn;
	}
	ast_mutex_unlock(&qlock);
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
				if (mem->calls) {
					snprintf(calls, sizeof(calls), " has taken %d calls (last was %ld secs ago)",
							mem->calls, (long)(time(NULL) - mem->lastcall));
				} else
					strncpy(calls, " has taken no calls yet", sizeof(calls) - 1);
				ast_cli(fd, "      %s/%s%s%s\n", mem->tech, mem->loc, max, calls);
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
	q = queues;
	while(q) {
		if (!strncasecmp(word, q->name, strlen(word))) {
			if (++which > state)
				break;
		}
		q = q->next;
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
	q = queues;
	if (id && !ast_strlen_zero(id)) {
		snprintf(idText,256,"ActionID: %s\r\n",id);
	}
	while(q) {
		ast_mutex_lock(&q->lock);

		/* List queue properties */
		if(q->callscompleted > 0)
			sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
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
				"Location: %s/%s\r\n"
				"Membership: %s\r\n"
				"Penalty: %d\r\n"
				"CallsTaken: %d\r\n"
				"LastCall: %ld\r\n"
				"%s"
				"\r\n",
					q->name, mem->tech, mem->loc, mem->dynamic ? "dynamic" : "static",
					mem->penalty, mem->calls, mem->lastcall, idText);

		/* List Queue Entries */

		pos = 1;
		for (qe = q->head; qe; qe = qe->next) 
			ast_cli(s->fd, "Event: QueueEntry\r\n"
				"Queue: %s\r\n"
				"Position: %d\r\n"
				"Channel: %s\r\n"
				"CallerID: %s\r\n"
				"Wait: %ld\r\n"
				"%s"
				"\r\n", 
					q->name, pos++, qe->chan->name, (qe->chan->callerid ? qe->chan->callerid : ""), (long)(now - qe->start), idText);
		ast_mutex_unlock(&q->lock);
		q = q->next;
	}
	ast_mutex_unlock(&qlock);
	return RESULT_SUCCESS;
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

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&cli_show_queue);
	ast_cli_unregister(&cli_show_queues);
	ast_manager_unregister( "Queues" );
	ast_manager_unregister( "QueueStatus" );
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
		ast_manager_register( "Queues", 0, manager_queues_show, "Queues" );
		ast_manager_register( "QueueStatus", 0, manager_queues_status, "Queue Status" );

		// [PHM 06/26/03]
		ast_register_application(app_aqm, aqm_exec, app_aqm_synopsis, app_aqm_descrip) ;
		ast_register_application(app_rqm, rqm_exec, app_rqm_synopsis, app_rqm_descrip) ;
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
