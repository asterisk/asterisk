/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief True call queues with optional send URL on answer
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg Config in \ref Config_qu queues.conf
 *
 * \par Development notes
 * \note 2004-11-25: Persistent Dynamic Members added by:
 *             NetNation Communications (www.netnation.com)
 *             Kevin Lindsay <kevinl@netnation.com>
 *
 *             Each dynamic agent in each queue is now stored in the astdb.
 *             When asterisk is restarted, each agent will be automatically
 *             readded into their recorded queues. This feature can be
 *             configured with the 'persistent_members=<1|0>' setting in the
 *             '[general]' category in queues.conf. The default is on.
 *
 * \note 2004-06-04: Priorities in queues added by inAccess Networks (work funded by Hellas On Line (HOL) www.hol.gr).
 *
 * \note These features added by David C. Troy <dave@toad.net>:
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
 * \ingroup applications
 */

/*** MODULEINFO
        <depend>res_monitor</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/monitor.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astdb.h"
#include "asterisk/devicestate.h"
#include "asterisk/stringfields.h"
#include "asterisk/astobj2.h"
#include "asterisk/global_datastores.h"

/* Please read before modifying this file.
 * There are three locks which are regularly used
 * throughout this file, the queue list lock, the lock
 * for each individual queue, and the interface list lock.
 * Please be extra careful to always lock in the following order
 * 1) queue list lock
 * 2) individual queue lock
 * 3) interface list lock
 * This order has sort of "evolved" over the lifetime of this
 * application, but it is now in place this way, so please adhere
 * to this order!
 */


enum {
	QUEUE_STRATEGY_RINGALL = 0,
	QUEUE_STRATEGY_ROUNDROBIN,
	QUEUE_STRATEGY_LEASTRECENT,
	QUEUE_STRATEGY_FEWESTCALLS,
	QUEUE_STRATEGY_RANDOM,
	QUEUE_STRATEGY_RRMEMORY,
	QUEUE_STRATEGY_RRORDERED,
};

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
	{ QUEUE_STRATEGY_RRORDERED, "rrordered" },
};

#define DEFAULT_RETRY		5
#define DEFAULT_TIMEOUT		15
#define RECHECK			1		/* Recheck every second to see we we're at the top yet */
#define MAX_PERIODIC_ANNOUNCEMENTS 10 /* The maximum periodic announcements we can have */

#define	RES_OKAY	0		/* Action completed */
#define	RES_EXISTS	(-1)		/* Entry already exists */
#define	RES_OUTOFMEMORY	(-2)		/* Out of memory */
#define	RES_NOSUCHQUEUE	(-3)		/* No such queue */
#define RES_NOT_DYNAMIC (-4)		/* Member is not dynamic */

static char *app = "Queue";

static char *synopsis = "Queue a call for a call queue";

static char *descrip =
"  Queue(queuename[|options[|URL][|announceoverride][|timeout][|AGI]):\n"
"Queues an incoming call in a particular call queue as defined in queues.conf.\n"
"This application will return to the dialplan if the queue does not exist, or\n"
"any of the join options cause the caller to not enter the queue.\n"
"The option string may contain zero or more of the following characters:\n"
"      'd' -- data-quality (modem) call (minimum delay).\n"
"      'h' -- allow callee to hang up by hitting '*', or whatver disconnect sequence\n"
"             defined in the featuremap section in features.conf.\n"
"      'H' -- allow caller to hang up by hitting '*', or whatever disconnect sequence\n"
"             defined in the featuremap section in features.conf.\n"
"      'n' -- no retries on the timeout; will exit this application and \n"
"             go to the next step.\n"
"      'i' -- ignore call forward requests from queue members and do nothing\n"
"             when they are requested.\n"
"      'r' -- ring instead of playing MOH\n"
"      't' -- allow the called user transfer the calling user by pressing '#' or\n"
"             whatever blindxfer sequence defined in the featuremap section in\n"
"             features.conf\n"
"      'T' -- to allow the calling user to transfer the call by pressing '#' or\n"
"             whatever blindxfer sequence defined in the featuremap section in\n"
"             features.conf\n"
"      'w' -- allow the called user to write the conversation to disk via Monitor\n"
"             by pressing the automon sequence defined in the featuremap section in\n"
"             features.conf\n"
"      'W' -- allow the calling user to write the conversation to disk via Monitor\n"
"             by pressing the automon sequence defined in the featuremap section in\n"
"             features.conf\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user, by transferring to the parking lot extension. See features.conf.\n"
"  The optional URL will be sent to the called party if the channel supports\n"
"it.\n"
"  The optional AGI parameter will setup an AGI script to be executed on the \n"
"calling party's channel once they are connected to a queue member.\n"
"  The timeout will cause the queue to fail out after a specified number of\n"
"seconds, checked between each queues.conf 'timeout' and 'retry' cycle.\n"
"  This application sets the following channel variable upon completion:\n"
"      QUEUESTATUS    The status of the call as a text string, one of\n"
"             TIMEOUT | FULL | JOINEMPTY | LEAVEEMPTY | JOINUNAVAIL | LEAVEUNAVAIL\n";

static char *app_aqm = "AddQueueMember" ;
static char *app_aqm_synopsis = "Dynamically adds queue members" ;
static char *app_aqm_descrip =
"   AddQueueMember(queuename[|interface[|penalty[|options[|membername[|state_interface]]]]]):\n"
"Dynamically adds interface to an existing queue.\n"
"If the interface is already in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     AQMSTATUS    The status of the attempt to add a queue member as a \n"
"                     text string, one of\n"
"           ADDED | MEMBERALREADY | NOSUCHQUEUE \n"
"If a device is provided in the state_interface parameter, then this will\n"
"be the device which will be used to determine the device state of the\n"
"added queue member.\n"
"Example: AddQueueMember(techsupport|SIP/3000)\n"
"";

static char *app_rqm = "RemoveQueueMember" ;
static char *app_rqm_synopsis = "Dynamically removes queue members" ;
static char *app_rqm_descrip =
"   RemoveQueueMember(queuename[|interface[|options]]):\n"
"Dynamically removes interface to an existing queue\n"
"If the interface is NOT in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     RQMSTATUS      The status of the attempt to remove a queue member as a\n"
"                     text string, one of\n"
"           REMOVED | NOTINQUEUE | NOSUCHQUEUE \n"
"Example: RemoveQueueMember(techsupport|SIP/3000)\n"
"";

static char *app_pqm = "PauseQueueMember" ;
static char *app_pqm_synopsis = "Pauses a queue member" ;
static char *app_pqm_descrip =
"   PauseQueueMember([queuename]|interface[|options]):\n"
"Pauses (blocks calls for) a queue member.\n"
"The given interface will be paused in the given queue.  This prevents\n"
"any calls from being sent from the queue to the interface until it is\n"
"unpaused with UnpauseQueueMember or the manager interface.  If no\n"
"queuename is given, the interface is paused in every queue it is a\n"
"member of.  If the interface is not in the named queue, or if no queue\n"
"is given and the interface is not in any queue, it will jump to\n"
"priority n+101, if it exists and the appropriate options are set.\n"
"The application will fail if the interface is not found and no extension\n"
"to jump to exists.\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     PQMSTATUS      The status of the attempt to pause a queue member as a\n"
"                     text string, one of\n"
"           PAUSED | NOTFOUND\n"
"Example: PauseQueueMember(|SIP/3000)\n";

static char *app_upqm = "UnpauseQueueMember" ;
static char *app_upqm_synopsis = "Unpauses a queue member" ;
static char *app_upqm_descrip =
"   UnpauseQueueMember([queuename]|interface[|options]):\n"
"Unpauses (resumes calls to) a queue member.\n"
"This is the counterpart to PauseQueueMember and operates exactly the\n"
"same way, except it unpauses instead of pausing the given interface.\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     UPQMSTATUS       The status of the attempt to unpause a queue \n"
"                      member as a text string, one of\n"
"            UNPAUSED | NOTFOUND\n"
"Example: UnpauseQueueMember(|SIP/3000)\n";

static char *app_ql = "QueueLog" ;
static char *app_ql_synopsis = "Writes to the queue_log" ;
static char *app_ql_descrip =
"   QueueLog(queuename|uniqueid|agent|event[|additionalinfo]):\n"
"Allows you to write your own events into the queue log\n"
"Example: QueueLog(101|${UNIQUEID}|${AGENT}|WENTONBREAK|600)\n";

/*! \brief Persistent Members astdb family */
static const char *pm_family = "Queue/PersistentMembers";
/* The maximum length of each persistent member queue database entry */
#define PM_MAX_LEN 8192

/*! \brief queues.conf [general] option */
static int queue_persistent_members = 0;

/*! \brief queues.conf per-queue weight option */
static int use_weight = 0;

/*! \brief queues.conf [general] option */
static int autofill_default = 0;

/*! \brief queues.conf [general] option */
static int montype_default = 0;

enum queue_result {
	QUEUE_UNKNOWN = 0,
	QUEUE_TIMEOUT = 1,
	QUEUE_JOINEMPTY = 2,
	QUEUE_LEAVEEMPTY = 3,
	QUEUE_JOINUNAVAIL = 4,
	QUEUE_LEAVEUNAVAIL = 5,
	QUEUE_FULL = 6,
};

const struct {
	enum queue_result id;
	char *text;
} queue_results[] = {
	{ QUEUE_UNKNOWN, "UNKNOWN" },
	{ QUEUE_TIMEOUT, "TIMEOUT" },
	{ QUEUE_JOINEMPTY,"JOINEMPTY" },
	{ QUEUE_LEAVEEMPTY, "LEAVEEMPTY" },
	{ QUEUE_JOINUNAVAIL, "JOINUNAVAIL" },
	{ QUEUE_LEAVEUNAVAIL, "LEAVEUNAVAIL" },
	{ QUEUE_FULL, "FULL" },
};

/*! \brief We define a custom "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing.

   There are two "links" defined in this structure, q_next and call_next.
   q_next links ALL defined callattempt structures into a linked list. call_next is
   a link which allows for a subset of the callattempts to be traversed. This subset
   is used in wait_for_answer so that irrelevant callattempts are not traversed. This
   also is helpful so that queue logs are always accurate in the case where a call to 
   a member times out, especially if using the ringall strategy. */

struct callattempt {
	struct callattempt *q_next;
	struct callattempt *call_next;
	struct ast_channel *chan;
	char interface[256];
	int stillgoing;
	int metric;
	int oldstatus;
	time_t lastcall;
	struct member *member;
};


struct queue_ent {
	struct call_queue *parent;          /*!< What queue is our parent */
	char moh[80];                       /*!< Name of musiconhold to be used */
	char announce[80];                  /*!< Announcement to play for member when call is answered */
	char context[AST_MAX_CONTEXT];      /*!< Context when user exits queue */
	char digits[AST_MAX_EXTENSION];     /*!< Digits entered while in queue */
	int valid_digits;		    /*!< Digits entered correspond to valid extension. Exited */
	int pos;                            /*!< Where we are in the queue */
	int prio;                           /*!< Our priority */
	int last_pos_said;                  /*!< Last position we told the user */
	time_t last_periodic_announce_time; /*!< The last time we played a periodic announcement */
	int last_periodic_announce_sound;   /*!< The last periodic announcement we made */
	time_t last_pos;                    /*!< Last time we told the user their position */
	int opos;                           /*!< Where we started in the queue */
	int handled;                        /*!< Whether our call was handled */
	int pending;                        /*!< Non-zero if we are attempting to call a member */
	int max_penalty;                    /*!< Limit the members that can take this call to this penalty or lower */
	time_t start;                       /*!< When we started holding */
	time_t expire;                      /*!< When this entry should expire (time out of queue) */
	struct ast_channel *chan;           /*!< Our channel */
	struct queue_ent *next;             /*!< The next queue entry */
};

struct member {
	char interface[80];                 /*!< Technology/Location */
	char state_interface[80];			/*!< Technology/Location from which to read device state changes */
	char membername[80];                /*!< Member name to use in queue logs */
	int penalty;                        /*!< Are we a last resort? */
	int calls;                          /*!< Number of calls serviced by this member */
	int dynamic;                        /*!< Are we dynamically added? */
	int realtime;                       /*!< Is this member realtime? */
	int status;                         /*!< Status of queue member */
	int paused;                         /*!< Are we paused (not accepting calls)? */
	time_t lastcall;                    /*!< When last successful call was hungup */
	unsigned int dead:1;                /*!< Used to detect members deleted in realtime */
	unsigned int delme:1;               /*!< Flag to delete entry on reload */
};

struct member_interface {
	char interface[80];
	AST_LIST_ENTRY(member_interface) list;    /*!< Next call queue */
};

static AST_LIST_HEAD_STATIC(interfaces, member_interface);

/* values used in multi-bit flags in call_queue */
#define QUEUE_EMPTY_NORMAL 1
#define QUEUE_EMPTY_STRICT 2
#define ANNOUNCEHOLDTIME_ALWAYS 1
#define ANNOUNCEHOLDTIME_ONCE 2
#define QUEUE_EVENT_VARIABLES 3

struct call_queue {
	char name[80];                      /*!< Name */
	char moh[80];                       /*!< Music On Hold class to be used */
	char announce[80];                  /*!< Announcement to play when call is answered */
	char context[AST_MAX_CONTEXT];      /*!< Exit context */
	unsigned int monjoin:1;
	unsigned int dead:1;
	unsigned int joinempty:2;
	unsigned int eventwhencalled:2;
	unsigned int leavewhenempty:2;
	unsigned int ringinuse:1;
	unsigned int setinterfacevar:1;
	unsigned int reportholdtime:1;
	unsigned int wrapped:1;
	unsigned int timeoutrestart:1;
	unsigned int announceholdtime:2;
	int strategy:4;
	unsigned int maskmemberstatus:1;
	unsigned int realtime:1;
	unsigned int found:1;
	int announcefrequency;              /*!< How often to announce their position */
	int periodicannouncefrequency;      /*!< How often to play periodic announcement */
	int roundingseconds;                /*!< How many seconds do we round to? */
	int holdtime;                       /*!< Current avg holdtime, based on an exponential average */
	int callscompleted;                 /*!< Number of queue calls completed */
	int callsabandoned;                 /*!< Number of queue calls abandoned */
	int servicelevel;                   /*!< seconds setting for servicelevel*/
	int callscompletedinsl;             /*!< Number of calls answered with servicelevel*/
	char monfmt[8];                     /*!< Format to use when recording calls */
	int montype;                        /*!< Monitor type  Monitor vs. MixMonitor */
	char sound_next[80];                /*!< Sound file: "Your call is now first in line" (def. queue-youarenext) */
	char sound_thereare[80];            /*!< Sound file: "There are currently" (def. queue-thereare) */
	char sound_calls[80];               /*!< Sound file: "calls waiting to speak to a representative." (def. queue-callswaiting)*/
	char sound_holdtime[80];            /*!< Sound file: "The current estimated total holdtime is" (def. queue-holdtime) */
	char sound_minutes[80];             /*!< Sound file: "minutes." (def. queue-minutes) */
	char sound_lessthan[80];            /*!< Sound file: "less-than" (def. queue-lessthan) */
	char sound_seconds[80];             /*!< Sound file: "seconds." (def. queue-seconds) */
	char sound_thanks[80];              /*!< Sound file: "Thank you for your patience." (def. queue-thankyou) */
	char sound_reporthold[80];          /*!< Sound file: "Hold time" (def. queue-reporthold) */
	char sound_periodicannounce[MAX_PERIODIC_ANNOUNCEMENTS][80];/*!< Sound files: Custom announce, no default */

	int count;                          /*!< How many entries */
	int maxlen;                         /*!< Max number of entries */
	int wrapuptime;                     /*!< Wrapup Time */

	int retry;                          /*!< Retry calling everyone after this amount of time */
	int timeout;                        /*!< How long to wait for an answer */
	int weight;                         /*!< Respective weight */
	int autopause;                      /*!< Auto pause queue members if they fail to answer */

	/* Queue strategy things */
	int rrpos;                          /*!< Round Robin - position */
	int memberdelay;                    /*!< Seconds to delay connecting member to caller */
	int autofill;                       /*!< Ignore the head call status and ring an available agent */
	
	struct ao2_container *members;             /*!< Head of the list of members */
	/*! 
	 * \brief Number of members _logged in_
	 * \note There will be members in the members container that are not logged
	 *       in, so this can not simply be replaced with ao2_container_count(). 
	 */
	int membercount;
	struct queue_ent *head;             /*!< Head of the list of callers */
	AST_LIST_ENTRY(call_queue) list;    /*!< Next call queue */
};

static AST_LIST_HEAD_STATIC(queues, call_queue);

static int set_member_paused(const char *queuename, const char *interface, int paused);
static void queue_transfer_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan); 
static void free_members(struct call_queue *q, int all);

static void rr_dep_warning(void)
{
	static unsigned int warned = 0;

	if (!warned) {
		ast_log(LOG_NOTICE, "The 'roundrobin' queue strategy is deprecated. Please use the 'rrmemory' strategy instead.\n");
		warned = 1;
	}
}

static void monjoin_dep_warning(void)
{
	static unsigned int warned = 0;
	if (!warned) {
		ast_log(LOG_NOTICE, "The 'monitor-join' queue option is deprecated. Please use monitor-type=mixmonitor instead.\n");
		warned = 1;
	}
}
/*! \brief sets the QUEUESTATUS channel variable */
static void set_queue_result(struct ast_channel *chan, enum queue_result res)
{
	int i;

	for (i = 0; i < sizeof(queue_results) / sizeof(queue_results[0]); i++) {
		if (queue_results[i].id == res) {
			pbx_builtin_setvar_helper(chan, "QUEUESTATUS", queue_results[i].text);
			return;
		}
	}
}

static char *int2strat(int strategy)
{
	int x;

	for (x = 0; x < sizeof(strategies) / sizeof(strategies[0]); x++) {
		if (strategy == strategies[x].strategy)
			return strategies[x].name;
	}

	return "<unknown>";
}

static int strat2int(const char *strategy)
{
	int x;

	for (x = 0; x < sizeof(strategies) / sizeof(strategies[0]); x++) {
		if (!strcasecmp(strategy, strategies[x].name))
			return strategies[x].strategy;
	}

	return -1;
}

/*!
 * \brief removes a call_queue from the list of call_queues
 */
static void remove_queue(struct call_queue *q)
{
	AST_LIST_LOCK(&queues);
	if (AST_LIST_REMOVE(&queues, q, list)) {
		ao2_ref(q, -1);
	}
	AST_LIST_UNLOCK(&queues);
}

static void destroy_queue(void *obj)
{
	struct call_queue *q = obj;
	if (q->members) {
		free_members(q, 1);
		ao2_ref(q->members, -1);
	}
}

/*! \brief Insert the 'new' entry after the 'prev' entry of queue 'q' */
static inline void insert_entry(struct call_queue *q, struct queue_ent *prev, struct queue_ent *new, int *pos)
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

	/* every queue_ent must have a reference to it's parent call_queue, this
	 * reference does not go away until the end of the queue_ent's life, meaning
	 * that even when the queue_ent leaves the call_queue this ref must remain. */
	ao2_ref(q, +1);
	new->parent = q;
	new->pos = ++(*pos);
	new->opos = *pos;
}

enum queue_member_status {
	QUEUE_NO_MEMBERS,
	QUEUE_NO_REACHABLE_MEMBERS,
	QUEUE_NORMAL
};

/*! \brief Check if members are available
 *
 * This function checks to see if members are available to be called. If any member
 * is available, the function immediately returns QUEUE_NORMAL. If no members are available,
 * the appropriate reason why is returned
 */
static enum queue_member_status get_member_status(struct call_queue *q, int max_penalty)
{
	struct member *member;
	struct ao2_iterator mem_iter;
	enum queue_member_status result = QUEUE_NO_MEMBERS;
	int allpaused = 1, empty = 1;

	ao2_lock(q);
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((member = ao2_iterator_next(&mem_iter))) {
		empty = 0;

		if (max_penalty && (member->penalty > max_penalty)) {
			ao2_ref(member, -1);
			continue;
		}

		if (member->paused) {
			ao2_ref(member, -1);
			continue;
		} else {
			allpaused = 0;
		}

		switch (member->status) {
		case AST_DEVICE_INVALID:
			/* nothing to do */
			ao2_ref(member, -1);
			break;
		case AST_DEVICE_UNAVAILABLE:
			result = QUEUE_NO_REACHABLE_MEMBERS;
			ao2_ref(member, -1);
			break;
		default:
			ao2_unlock(q);
			ao2_ref(member, -1);
			return QUEUE_NORMAL;
		}
	}
	ao2_iterator_destroy(&mem_iter);
	ao2_unlock(q);

	if (!empty && allpaused) {
		result = QUEUE_NO_REACHABLE_MEMBERS;
	}
	return result;
}

struct statechange {
	AST_LIST_ENTRY(statechange) entry;
	int state;
	char dev[0];
};

static int update_status(const char *interface, const int status)
{
	struct member *cur;
	struct ao2_iterator mem_iter;
	struct call_queue *q;
	char tmp_interface[80];

	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((cur = ao2_iterator_next(&mem_iter))) {
			char *slash_pos;
			ast_copy_string(tmp_interface, cur->state_interface, sizeof(tmp_interface));
			if ((slash_pos = strchr(tmp_interface, '/')))
				if ((slash_pos = strchr(slash_pos + 1, '/')))
					*slash_pos = '\0';

			if (strcasecmp(interface, tmp_interface)) {
				ao2_ref(cur, -1);
				continue;
			}

			if (cur->status != status) {
				cur->status = status;
				if (q->maskmemberstatus) {
					ao2_ref(cur, -1);
					continue;
				}

				manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
					"Queue: %s\r\n"
					"Location: %s\r\n"
					"MemberName: %s\r\n"
					"Membership: %s\r\n"
					"Penalty: %d\r\n"
					"CallsTaken: %d\r\n"
					"LastCall: %d\r\n"
					"Status: %d\r\n"
					"Paused: %d\r\n",
					q->name, cur->interface, cur->membername, cur->dynamic ? "dynamic" : cur->realtime ? "realtime" : "static",
					cur->penalty, cur->calls, (int)cur->lastcall, cur->status, cur->paused);
			}
			ao2_ref(cur, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ao2_unlock(q);
	}
	AST_LIST_UNLOCK(&queues);

	return 0;
}

/*! \brief set a member's status based on device state of that member's interface*/
static void *handle_statechange(struct statechange *sc)
{
	struct member_interface *curint;
	char *loc;
	char *technology;
	char interface[80];

	technology = ast_strdupa(sc->dev);
	loc = strchr(technology, '/');
	if (loc) {
		*loc++ = '\0';
	} else {
		return NULL;
	}

	AST_LIST_LOCK(&interfaces);
	AST_LIST_TRAVERSE(&interfaces, curint, list) {
		char *slash_pos;
		ast_copy_string(interface, curint->interface, sizeof(interface));
		if ((slash_pos = strchr(interface, '/')))
			if ((slash_pos = strchr(slash_pos + 1, '/')))
				*slash_pos = '\0';

		if (!strcasecmp(interface, sc->dev))
			break;
	}
	AST_LIST_UNLOCK(&interfaces);

	if (!curint) {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Device '%s/%s' changed to state '%d' (%s) but we don't care because they're not a member of any queue.\n", technology, loc, sc->state, devstate2str(sc->state));
		return NULL;
	}

	if (option_debug)
		ast_log(LOG_DEBUG, "Device '%s/%s' changed to state '%d' (%s)\n", technology, loc, sc->state, devstate2str(sc->state));

	update_status(sc->dev, sc->state);

	return NULL;
}

/*!
 * \brief Data used by the device state thread
 */
static struct {
	/*! Set to 1 to stop the thread */
	unsigned int stop:1;
	/*! The device state monitoring thread */
	pthread_t thread;
	/*! Lock for the state change queue */
	ast_mutex_t lock;
	/*! Condition for the state change queue */
	ast_cond_t cond;
	/*! Queue of state changes */
	AST_LIST_HEAD_NOLOCK(, statechange) state_change_q;
} device_state = {
	.thread = AST_PTHREADT_NULL,
};

/*! \brief Consumer of the statechange queue */
static void *device_state_thread(void *data)
{
	struct statechange *sc = NULL;

	while (!device_state.stop) {
		ast_mutex_lock(&device_state.lock);
		if (!(sc = AST_LIST_REMOVE_HEAD(&device_state.state_change_q, entry))) {
			ast_cond_wait(&device_state.cond, &device_state.lock);
			sc = AST_LIST_REMOVE_HEAD(&device_state.state_change_q, entry);
		}
		ast_mutex_unlock(&device_state.lock);

		/* Check to see if we were woken up to see the request to stop */
		if (device_state.stop)
			break;

		if (!sc)
			continue;

		handle_statechange(sc);

		free(sc);
		sc = NULL;
	}

	if (sc)
		free(sc);

	while ((sc = AST_LIST_REMOVE_HEAD(&device_state.state_change_q, entry)))
		free(sc);

	return NULL;
}
/*! \brief Producer of the statechange queue */
static int statechange_queue(const char *dev, int state, void *ign)
{
	struct statechange *sc;

	if (!(sc = ast_calloc(1, sizeof(*sc) + strlen(dev) + 1)))
		return 0;

	sc->state = state;
	strcpy(sc->dev, dev);

	ast_mutex_lock(&device_state.lock);
	AST_LIST_INSERT_TAIL(&device_state.state_change_q, sc, entry);
	ast_cond_signal(&device_state.cond);
	ast_mutex_unlock(&device_state.lock);

	return 0;
}
/*! \brief allocate space for new queue member and set fields based on parameters passed */
static struct member *create_queue_member(const char *interface, const char *membername, int penalty, int paused, const char *state_interface)
{
	struct member *cur;
	
	if ((cur = ao2_alloc(sizeof(*cur), NULL))) {
		cur->penalty = penalty;
		cur->paused = paused;
		ast_copy_string(cur->interface, interface, sizeof(cur->interface));
		if (!ast_strlen_zero(state_interface)) {
			ast_copy_string(cur->state_interface, state_interface, sizeof(cur->state_interface));
		} else {
			ast_copy_string(cur->state_interface, interface, sizeof(cur->state_interface));
		}
		if (!ast_strlen_zero(membername))
			ast_copy_string(cur->membername, membername, sizeof(cur->membername));
		else
			ast_copy_string(cur->membername, interface, sizeof(cur->membername));
		if (!strchr(cur->interface, '/'))
			ast_log(LOG_WARNING, "No location at interface '%s'\n", interface);
		cur->status = ast_device_state(cur->state_interface);
	}

	return cur;
}

static struct call_queue *alloc_queue(const char *queuename)
{
	struct call_queue *q;

	if ((q = ao2_alloc(sizeof(*q), destroy_queue))) {
		ast_copy_string(q->name, queuename, sizeof(q->name));
	}
	return q;
}

static int compress_char(const char c)
{
	if (c < 32)
		return 0;
	else if (c > 96)
		return c - 64;
	else
		return c - 32;
}

static int member_hash_fn(const void *obj, const int flags)
{
	const struct member *mem = obj;
	const char *chname = strchr(mem->interface, '/');
	int ret = 0, i;
	if (!chname)
		chname = mem->interface;
	for (i = 0; i < 5 && chname[i]; i++)
		ret += compress_char(chname[i]) << (i * 6);
	return ret;
}

static int member_cmp_fn(void *obj1, void *obj2, int flags)
{
	struct member *mem1 = obj1, *mem2 = obj2;
	return strcmp(mem1->interface, mem2->interface) ? 0 : CMP_MATCH | CMP_STOP;
}

static void init_queue(struct call_queue *q)
{
	int i;

	q->dead = 0;
	q->retry = DEFAULT_RETRY;
	q->timeout = -1;
	q->maxlen = 0;
	q->announcefrequency = 0;
	q->announceholdtime = 0;
	q->roundingseconds = 0; /* Default - don't announce seconds */
	q->servicelevel = 0;
	q->ringinuse = 1;
	q->setinterfacevar = 0;
	q->autofill = autofill_default;
	q->montype = montype_default;
	q->moh[0] = '\0';
	q->announce[0] = '\0';
	q->context[0] = '\0';
	q->monfmt[0] = '\0';
	q->periodicannouncefrequency = 0;
	q->reportholdtime = 0;
	q->monjoin = 0;
	q->wrapuptime = 0;
	q->joinempty = 0;
	q->leavewhenempty = 0;
	q->memberdelay = 0;
	q->maskmemberstatus = 0;
	q->eventwhencalled = 0;
	q->weight = 0;
	q->timeoutrestart = 0;
	if (!q->members) {
		if (q->strategy == QUEUE_STRATEGY_RRORDERED) {
			q->members = ao2_container_alloc(1, member_hash_fn, member_cmp_fn);
		} else {
			q->members = ao2_container_alloc(37, member_hash_fn, member_cmp_fn);
		}
	}
	q->membercount = 0;
	q->found = 1;
	ast_copy_string(q->sound_next, "queue-youarenext", sizeof(q->sound_next));
	ast_copy_string(q->sound_thereare, "queue-thereare", sizeof(q->sound_thereare));
	ast_copy_string(q->sound_calls, "queue-callswaiting", sizeof(q->sound_calls));
	ast_copy_string(q->sound_holdtime, "queue-holdtime", sizeof(q->sound_holdtime));
	ast_copy_string(q->sound_minutes, "queue-minutes", sizeof(q->sound_minutes));
	ast_copy_string(q->sound_seconds, "queue-seconds", sizeof(q->sound_seconds));
	ast_copy_string(q->sound_thanks, "queue-thankyou", sizeof(q->sound_thanks));
	ast_copy_string(q->sound_lessthan, "queue-less-than", sizeof(q->sound_lessthan));
	ast_copy_string(q->sound_reporthold, "queue-reporthold", sizeof(q->sound_reporthold));
	ast_copy_string(q->sound_periodicannounce[0], "queue-periodic-announce", sizeof(q->sound_periodicannounce[0]));
	for (i = 1; i < MAX_PERIODIC_ANNOUNCEMENTS; i++) {
		q->sound_periodicannounce[i][0]='\0';
	}
}

static void clear_queue(struct call_queue *q)
{
	q->holdtime = 0;
	q->callscompleted = 0;
	q->callsabandoned = 0;
	q->callscompletedinsl = 0;
	q->wrapuptime = 0;
}

static int add_to_interfaces(const char *interface)
{
	struct member_interface *curint;

	AST_LIST_LOCK(&interfaces);
	AST_LIST_TRAVERSE(&interfaces, curint, list) {
		if (!strcasecmp(curint->interface, interface))
			break;
	}

	if (curint) {
		AST_LIST_UNLOCK(&interfaces);
		return 0;
	}

	if (option_debug)
		ast_log(LOG_DEBUG, "Adding %s to the list of interfaces that make up all of our queue members.\n", interface);
	
	if ((curint = ast_calloc(1, sizeof(*curint)))) {
		ast_copy_string(curint->interface, interface, sizeof(curint->interface));
		AST_LIST_INSERT_HEAD(&interfaces, curint, list);
	}
	AST_LIST_UNLOCK(&interfaces);

	return 0;
}

static int interface_exists_global(const char *interface)
{
	struct call_queue *q;
	struct member *mem;
	struct ao2_iterator mem_iter;
	int ret = 0;

	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((mem = ao2_iterator_next(&mem_iter))) {
			if (!strcasecmp(mem->state_interface, interface)) {
				ao2_ref(mem, -1);
				ret = 1;
				break;
			}
			ao2_ref(mem, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ao2_unlock(q);
		if (ret)
			break;
	}
	AST_LIST_UNLOCK(&queues);

	return ret;
}

static int remove_from_interfaces(const char *interface)
{
	struct member_interface *curint;

	if (interface_exists_global(interface))
		return 0;

	AST_LIST_LOCK(&interfaces);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&interfaces, curint, list) {
		if (!strcasecmp(curint->interface, interface)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Removing %s from the list of interfaces that make up all of our queue members.\n", interface);
			AST_LIST_REMOVE_CURRENT(&interfaces, list);
			free(curint);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&interfaces);

	return 0;
}

static void clear_and_free_interfaces(void)
{
	struct member_interface *curint;

	AST_LIST_LOCK(&interfaces);
	while ((curint = AST_LIST_REMOVE_HEAD(&interfaces, list)))
		free(curint);
	AST_LIST_UNLOCK(&interfaces);
}

/*! \brief Configure a queue parameter.
\par
   For error reporting, line number is passed for .conf static configuration.
   For Realtime queues, linenum is -1.
   The failunknown flag is set for config files (and static realtime) to show
   errors for unknown parameters. It is cleared for dynamic realtime to allow
   extra fields in the tables. */
static void queue_set_param(struct call_queue *q, const char *param, const char *val, int linenum, int failunknown)
{
	if (!strcasecmp(param, "musicclass") || 
		!strcasecmp(param, "music") || !strcasecmp(param, "musiconhold")) {
		ast_copy_string(q->moh, val, sizeof(q->moh));
	} else if (!strcasecmp(param, "announce")) {
		ast_copy_string(q->announce, val, sizeof(q->announce));
	} else if (!strcasecmp(param, "context")) {
		ast_copy_string(q->context, val, sizeof(q->context));
	} else if (!strcasecmp(param, "timeout")) {
		q->timeout = atoi(val);
		if (q->timeout < 0)
			q->timeout = DEFAULT_TIMEOUT;
	} else if (!strcasecmp(param, "ringinuse")) {
		q->ringinuse = ast_true(val);
	} else if (!strcasecmp(param, "setinterfacevar")) {
		q->setinterfacevar = ast_true(val);
	} else if (!strcasecmp(param, "monitor-join")) {
		monjoin_dep_warning();
		q->monjoin = ast_true(val);
	} else if (!strcasecmp(param, "monitor-format")) {
		ast_copy_string(q->monfmt, val, sizeof(q->monfmt));
	} else if (!strcasecmp(param, "queue-youarenext")) {
		ast_copy_string(q->sound_next, val, sizeof(q->sound_next));
	} else if (!strcasecmp(param, "queue-thereare")) {
		ast_copy_string(q->sound_thereare, val, sizeof(q->sound_thereare));
	} else if (!strcasecmp(param, "queue-callswaiting")) {
		ast_copy_string(q->sound_calls, val, sizeof(q->sound_calls));
	} else if (!strcasecmp(param, "queue-holdtime")) {
		ast_copy_string(q->sound_holdtime, val, sizeof(q->sound_holdtime));
	} else if (!strcasecmp(param, "queue-minutes")) {
		ast_copy_string(q->sound_minutes, val, sizeof(q->sound_minutes));
	} else if (!strcasecmp(param, "queue-seconds")) {
		ast_copy_string(q->sound_seconds, val, sizeof(q->sound_seconds));
	} else if (!strcasecmp(param, "queue-lessthan")) {
		ast_copy_string(q->sound_lessthan, val, sizeof(q->sound_lessthan));
	} else if (!strcasecmp(param, "queue-thankyou")) {
		ast_copy_string(q->sound_thanks, val, sizeof(q->sound_thanks));
	} else if (!strcasecmp(param, "queue-reporthold")) {
		ast_copy_string(q->sound_reporthold, val, sizeof(q->sound_reporthold));
	} else if (!strcasecmp(param, "announce-frequency")) {
		q->announcefrequency = atoi(val);
	} else if (!strcasecmp(param, "announce-round-seconds")) {
		q->roundingseconds = atoi(val);
		if (q->roundingseconds>60 || q->roundingseconds<0) {
			if (linenum >= 0) {
				ast_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s' at line %d of queues.conf\n",
					val, param, q->name, linenum);
			} else {
				ast_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s'\n", val, param, q->name);
			}
			q->roundingseconds=0;
		}
	} else if (!strcasecmp(param, "announce-holdtime")) {
		if (!strcasecmp(val, "once"))
			q->announceholdtime = ANNOUNCEHOLDTIME_ONCE;
		else if (ast_true(val))
			q->announceholdtime = ANNOUNCEHOLDTIME_ALWAYS;
		else
			q->announceholdtime = 0;
	} else if (!strcasecmp(param, "periodic-announce")) {
		if (strchr(val, '|')) {
			char *s, *buf = ast_strdupa(val);
			unsigned int i = 0;

			while ((s = strsep(&buf, "|"))) {
				ast_copy_string(q->sound_periodicannounce[i], s, sizeof(q->sound_periodicannounce[i]));
				i++;
				if (i == MAX_PERIODIC_ANNOUNCEMENTS)
					break;
			}
		} else {
			ast_copy_string(q->sound_periodicannounce[0], val, sizeof(q->sound_periodicannounce[0]));
		}
	} else if (!strcasecmp(param, "periodic-announce-frequency")) {
		q->periodicannouncefrequency = atoi(val);
	} else if (!strcasecmp(param, "retry")) {
		q->retry = atoi(val);
		if (q->retry <= 0)
			q->retry = DEFAULT_RETRY;
	} else if (!strcasecmp(param, "wrapuptime")) {
		q->wrapuptime = atoi(val);
	} else if (!strcasecmp(param, "autofill")) {
		q->autofill = ast_true(val);
	} else if (!strcasecmp(param, "monitor-type")) {
		if (!strcasecmp(val, "mixmonitor"))
			q->montype = 1;
	} else if (!strcasecmp(param, "autopause")) {
		q->autopause = ast_true(val);
	} else if (!strcasecmp(param, "maxlen")) {
		q->maxlen = atoi(val);
		if (q->maxlen < 0)
			q->maxlen = 0;
	} else if (!strcasecmp(param, "servicelevel")) {
		q->servicelevel= atoi(val);
	} else if (!strcasecmp(param, "strategy")) {
		q->strategy = strat2int(val);
		if (q->strategy < 0) {
			ast_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n",
				val, q->name);
			q->strategy = QUEUE_STRATEGY_RINGALL;
		}
	} else if (!strcasecmp(param, "joinempty")) {
		if (!strcasecmp(val, "strict"))
			q->joinempty = QUEUE_EMPTY_STRICT;
		else if (ast_true(val))
			q->joinempty = QUEUE_EMPTY_NORMAL;
		else
			q->joinempty = 0;
	} else if (!strcasecmp(param, "leavewhenempty")) {
		if (!strcasecmp(val, "strict"))
			q->leavewhenempty = QUEUE_EMPTY_STRICT;
		else if (ast_true(val))
			q->leavewhenempty = QUEUE_EMPTY_NORMAL;
		else
			q->leavewhenempty = 0;
	} else if (!strcasecmp(param, "eventmemberstatus")) {
		q->maskmemberstatus = !ast_true(val);
	} else if (!strcasecmp(param, "eventwhencalled")) {
		if (!strcasecmp(val, "vars")) {
			q->eventwhencalled = QUEUE_EVENT_VARIABLES;
		} else {
			q->eventwhencalled = ast_true(val) ? 1 : 0;
		}
	} else if (!strcasecmp(param, "reportholdtime")) {
		q->reportholdtime = ast_true(val);
	} else if (!strcasecmp(param, "memberdelay")) {
		q->memberdelay = atoi(val);
	} else if (!strcasecmp(param, "weight")) {
		q->weight = atoi(val);
		if (q->weight)
			use_weight++;
		/* With Realtime queues, if the last queue using weights is deleted in realtime,
		   we will not see any effect on use_weight until next reload. */
	} else if (!strcasecmp(param, "timeoutrestart")) {
		q->timeoutrestart = ast_true(val);
	} else if (failunknown) {
		if (linenum >= 0) {
			ast_log(LOG_WARNING, "Unknown keyword in queue '%s': %s at line %d of queues.conf\n",
				q->name, param, linenum);
		} else {
			ast_log(LOG_WARNING, "Unknown keyword in queue '%s': %s\n", q->name, param);
		}
	}
}

static void rt_handle_member_record(struct call_queue *q, char *interface, const char *membername, const char *penalty_str, const char *paused_str, const char *state_interface)
{
	struct member *m, tmpmem;
	int penalty = 0;
	int paused  = 0;

	if (penalty_str) {
		penalty = atoi(penalty_str);
		if (penalty < 0)
			penalty = 0;
	}

	if (paused_str) {
		paused = atoi(paused_str);
		if (paused < 0)
			paused = 0;
	}

	/* Find the member, or the place to put a new one. */
	ast_copy_string(tmpmem.interface, interface, sizeof(tmpmem.interface));
	m = ao2_find(q->members, &tmpmem, OBJ_POINTER);

	/* Create a new one if not found, else update penalty */
	if (!m) {
		if ((m = create_queue_member(interface, membername, penalty, paused, state_interface))) {
			m->dead = 0;
			m->realtime = 1;
			add_to_interfaces(m->state_interface);
			ao2_link(q->members, m);
			ao2_ref(m, -1);
			m = NULL;
			q->membercount++;
		}
	} else {
		m->dead = 0;	/* Do not delete this one. */
		if (paused_str)
			m->paused = paused;
		if (strcasecmp(state_interface, m->state_interface)) {
			remove_from_interfaces(m->state_interface);
			ast_copy_string(m->state_interface, state_interface, sizeof(m->state_interface));
			add_to_interfaces(m->state_interface);
		}
		m->penalty = penalty;
		ao2_ref(m, -1);
	}
}

static void free_members(struct call_queue *q, int all)
{
	/* Free non-dynamic members */
	struct member *cur;
	struct ao2_iterator mem_iter = ao2_iterator_init(q->members, 0);

	while ((cur = ao2_iterator_next(&mem_iter))) {
		if (all || !cur->dynamic) {
			ao2_unlink(q->members, cur);
			remove_from_interfaces(cur->state_interface);
			q->membercount--;
		}
		ao2_ref(cur, -1);
	}
	ao2_iterator_destroy(&mem_iter);
}

/*!\brief Reload a single queue via realtime.
   \return Return the queue, or NULL if it doesn't exist.
   \note Should be called with the global qlock locked. */
static struct call_queue *find_queue_by_name_rt(const char *queuename, struct ast_variable *queue_vars, struct ast_config *member_config)
{
	struct ast_variable *v;
	struct call_queue *q;
	struct member *m;
	struct ao2_iterator mem_iter;
	char *interface = NULL;
	char *tmp, *tmp_name;
	char tmpbuf[64];	/* Must be longer than the longest queue param name. */

	/* Find the queue in the in-core list (we will create a new one if not found). */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, queuename))
			break;
	}

	/* Static queues override realtime. */
	if (q) {
		ao2_lock(q);
		if (!q->realtime) {
			if (q->dead) {
				ao2_unlock(q);
				return NULL;
			} else {
				ast_log(LOG_WARNING, "Static queue '%s' already exists. Not loading from realtime\n", q->name);
				ao2_unlock(q);
				return q;
			}
		}
	} else if (!member_config)
		/* Not found in the list, and it's not realtime ... */
		return NULL;

	/* Check if queue is defined in realtime. */
	if (!queue_vars) {
		/* Delete queue from in-core list if it has been deleted in realtime. */
		if (q) {
			/*! \note Hmm, can't seem to distinguish a DB failure from a not
			   found condition... So we might delete an in-core queue
			   in case of DB failure. */
			ast_log(LOG_DEBUG, "Queue %s not found in realtime.\n", queuename);

			q->dead = 1;
			/* Delete if unused (else will be deleted when last caller leaves). */
			if (!q->count) {
				/* Delete. */
				ao2_unlock(q);
				remove_queue(q);
			} else
				ao2_unlock(q);
		}
		return NULL;
	}

	/* Create a new queue if an in-core entry does not exist yet. */
	if (!q) {
		struct ast_variable *tmpvar;
		if (!(q = alloc_queue(queuename)))
			return NULL;
		ao2_lock(q);
		clear_queue(q);
		q->realtime = 1;
		AST_LIST_INSERT_HEAD(&queues, q, list);
	
		/* Due to the fact that the "rrordered" strategy will have a different allocation
 		 * scheme for queue members, we must devise the queue's strategy before other initializations.
		 * To be specific, the rrordered strategy needs to function like a linked list, meaning the ao2
		 * container used will have only a single bucket instead of the typical number.
		 */
		for (tmpvar = queue_vars; tmpvar; tmpvar = tmpvar->next) {
			if (!strcasecmp(tmpvar->name, "strategy")) {
				q->strategy = strat2int(tmpvar->value);
				if (q->strategy < 0) {
					ast_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n",
					tmpvar->value, q->name);
					q->strategy = QUEUE_STRATEGY_RINGALL;
				}
				break;
			}
		}
		/* We traversed all variables and didn't find a strategy */
		if (!tmpvar) {
			q->strategy = QUEUE_STRATEGY_RINGALL;
		}
	}
	init_queue(q);		/* Ensure defaults for all parameters not set explicitly. */

	memset(tmpbuf, 0, sizeof(tmpbuf));
	for (v = queue_vars; v; v = v->next) {
		/* Convert to dashes `-' from underscores `_' as the latter are more SQL friendly. */
		if ((tmp = strchr(v->name, '_'))) {
			ast_copy_string(tmpbuf, v->name, sizeof(tmpbuf));
			tmp_name = tmpbuf;
			tmp = tmp_name;
			while ((tmp = strchr(tmp, '_')))
				*tmp++ = '-';
		} else
			tmp_name = v->name;

		/* NULL values don't get returned from realtime; blank values should
		 * still get set.  If someone doesn't want a value to be set, they
		 * should set the realtime column to NULL, not blank. */
		queue_set_param(q, tmp_name, v->value, -1, 0);
	}

	if (q->strategy == QUEUE_STRATEGY_ROUNDROBIN)
		rr_dep_warning();

	/* Temporarily set realtime members dead so we can detect deleted ones. 
	 * Also set the membercount correctly for realtime*/
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		q->membercount++;
		if (m->realtime)
			m->dead = 1;
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	while ((interface = ast_category_browse(member_config, interface))) {
		rt_handle_member_record(q, interface,
			ast_variable_retrieve(member_config, interface, "membername"),
			ast_variable_retrieve(member_config, interface, "penalty"),
			ast_variable_retrieve(member_config, interface, "paused"),
			S_OR(ast_variable_retrieve(member_config, interface, "state_interface"),interface));
	}

	/* Delete all realtime members that have been deleted in DB. */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->dead) {
			ao2_unlink(q->members, m);
			ao2_unlock(q);
			remove_from_interfaces(m->state_interface);
			ao2_lock(q);
			q->membercount--;
		}
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	ao2_unlock(q);

	return q;
}

static int update_realtime_member_field(struct member *mem, const char *queue_name, const char *field, const char *value)
{
	struct ast_variable *var, *save;
	int ret = -1;

	if (!(var = ast_load_realtime("queue_members", "interface", mem->interface, "queue_name", queue_name, NULL))) 
		return ret;
	save = var;
	while (var) {
		if (!strcmp(var->name, "uniqueid"))
			break;
		var = var->next;
	}
	if (var && !ast_strlen_zero(var->value)) {
		if ((ast_update_realtime("queue_members", "uniqueid", var->value, field, value, NULL)) > -1)
			ret = 0;
	}
	ast_variables_destroy(save);
	return ret;
}

static void update_realtime_members(struct call_queue *q)
{
	struct ast_config *member_config = NULL;
	struct member *m;
	char *interface = NULL;
	struct ao2_iterator mem_iter;

	if (!(member_config = ast_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", q->name , NULL))) {
		/*This queue doesn't have realtime members*/
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Queue %s has no realtime members defined. No need for update\n", q->name);
		return;
	}

	ao2_lock(q);
	
	/* Temporarily set realtime  members dead so we can detect deleted ones.*/ 
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->realtime)
			m->dead = 1;
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	while ((interface = ast_category_browse(member_config, interface))) {
		rt_handle_member_record(q, interface,
			S_OR(ast_variable_retrieve(member_config, interface, "membername"), interface),
			ast_variable_retrieve(member_config, interface, "penalty"),
			ast_variable_retrieve(member_config, interface, "paused"),
			S_OR(ast_variable_retrieve(member_config, interface, "state_interface"), interface));
	}

	/* Delete all realtime members that have been deleted in DB. */
	mem_iter = ao2_iterator_init(q->members, 0);
	while ((m = ao2_iterator_next(&mem_iter))) {
		if (m->dead) {
			ao2_unlink(q->members, m);
			ao2_unlock(q);
			remove_from_interfaces(m->state_interface);
			ao2_lock(q);
			q->membercount--;
		}
		ao2_ref(m, -1);
	}
	ao2_iterator_destroy(&mem_iter);
	ao2_unlock(q);
	ast_config_destroy(member_config);
}

static struct call_queue *load_realtime_queue(const char *queuename)
{
	struct ast_variable *queue_vars;
	struct ast_config *member_config = NULL;
	struct call_queue *q;

	/* Find the queue in the in-core list first. */
	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, queuename)) {
			break;
		}
	}
	AST_LIST_UNLOCK(&queues);

	if (!q || q->realtime) {
		/*! \note Load from realtime before taking the global qlock, to avoid blocking all
		   queue operations while waiting for the DB.

		   This will be two separate database transactions, so we might
		   see queue parameters as they were before another process
		   changed the queue and member list as it was after the change.
		   Thus we might see an empty member list when a queue is
		   deleted. In practise, this is unlikely to cause a problem. */

		queue_vars = ast_load_realtime("queues", "name", queuename, NULL);
		if (queue_vars) {
			member_config = ast_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", queuename, NULL);
			if (!member_config) {
				ast_log(LOG_ERROR, "no queue_members defined in your config (extconfig.conf).\n");
				ast_variables_destroy(queue_vars);
				return NULL;
			}
		}

		AST_LIST_LOCK(&queues);

		q = find_queue_by_name_rt(queuename, queue_vars, member_config);
		if (member_config)
			ast_config_destroy(member_config);
		if (queue_vars)
			ast_variables_destroy(queue_vars);

		AST_LIST_UNLOCK(&queues);
	} else { 
		update_realtime_members(q);
	}
	return q;
}

static int join_queue(char *queuename, struct queue_ent *qe, enum queue_result *reason)
{
	struct call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	int inserted = 0;
	enum queue_member_status stat;

	if (!(q = load_realtime_queue(queuename)))
		return res;

	AST_LIST_LOCK(&queues);
	ao2_lock(q);

	/* This is our one */
	stat = get_member_status(q, qe->max_penalty);
	if (!q->joinempty && (stat == QUEUE_NO_MEMBERS))
		*reason = QUEUE_JOINEMPTY;
	else if ((q->joinempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS || stat == QUEUE_NO_MEMBERS))
		*reason = QUEUE_JOINUNAVAIL;
	else if (q->maxlen && (q->count >= q->maxlen))
		*reason = QUEUE_FULL;
	else {
		/* There's space for us, put us at the right position inside
		 * the queue.
		 * Take into account the priority of the calling user */
		inserted = 0;
		prev = NULL;
		cur = q->head;
		while (cur) {
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
		ast_copy_string(qe->moh, q->moh, sizeof(qe->moh));
		ast_copy_string(qe->announce, q->announce, sizeof(qe->announce));
		ast_copy_string(qe->context, q->context, sizeof(qe->context));
		q->count++;
		res = 0;
		manager_event(EVENT_FLAG_CALL, "Join",
			"Channel: %s\r\nCallerID: %s\r\nCallerIDName: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\nUniqueid: %s\r\n",
			qe->chan->name,
			S_OR(qe->chan->cid.cid_num, "unknown"), /* XXX somewhere else it is <unknown> */
			S_OR(qe->chan->cid.cid_name, "unknown"),
			q->name, qe->pos, q->count, qe->chan->uniqueid );
		if (option_debug)
			ast_log(LOG_DEBUG, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, qe->chan->name, qe->pos );
	}
	ao2_unlock(q);
	AST_LIST_UNLOCK(&queues);

	return res;
}

static int play_file(struct ast_channel *chan, char *filename)
{
	int res;

	if (ast_strlen_zero(filename)) {
		return 0;
	}

	if (!ast_fileexists(filename, NULL, chan->language)) {
		return 0;
	}

	ast_stopstream(chan);

	res = ast_streamfile(chan, filename, chan->language);
	if (!res)
		res = ast_waitstream(chan, AST_DIGIT_ANY);

	ast_stopstream(chan);

	return res;
}

static int valid_exit(struct queue_ent *qe, char digit)
{
	int digitlen = strlen(qe->digits);

	/* Prevent possible buffer overflow */
	if (digitlen < sizeof(qe->digits) - 2) {
		qe->digits[digitlen] = digit;
		qe->digits[digitlen + 1] = '\0';
	} else {
		qe->digits[0] = '\0';
		return 0;
	}

	/* If there's no context to goto, short-circuit */
	if (ast_strlen_zero(qe->context))
		return 0;

	/* If the extension is bad, then reset the digits to blank */
	if (!ast_canmatch_extension(qe->chan, qe->context, qe->digits, 1, qe->chan->cid.cid_num)) {
		qe->digits[0] = '\0';
		return 0;
	}

	/* We have an exact match */
	if (!ast_goto_if_exists(qe->chan, qe->context, qe->digits, 1)) {
		qe->valid_digits = 1;
		/* Return 1 on a successful goto */
		return 1;
	}

	return 0;
}

static int say_position(struct queue_ent *qe)
{
	int res = 0, avgholdmins, avgholdsecs;
	time_t now;

	/* Check to see if this is ludicrous -- if we just announced position, don't do it again*/
	time(&now);
	if ((now - qe->last_pos) < 15)
		return 0;

	/* If either our position has changed, or we are over the freq timer, say position */
	if ((qe->last_pos_said == qe->pos) && ((now - qe->last_pos) < qe->parent->announcefrequency))
		return 0;

	ast_moh_stop(qe->chan);
	/* Say we're next, if we are */
	if (qe->pos == 1) {
		res = play_file(qe->chan, qe->parent->sound_next);
		if (res)
			goto playout;
		else
			goto posout;
	} else {
		res = play_file(qe->chan, qe->parent->sound_thereare);
		if (res)
			goto playout;
		res = ast_say_number(qe->chan, qe->pos, AST_DIGIT_ANY, qe->chan->language, (char *) NULL); /* Needs gender */
		if (res)
			goto playout;
		res = play_file(qe->chan, qe->parent->sound_calls);
		if (res)
			goto playout;
	}
	/* Round hold time to nearest minute */
	avgholdmins = abs(((qe->parent->holdtime + 30) - (now - qe->start)) / 60);

	/* If they have specified a rounding then round the seconds as well */
	if (qe->parent->roundingseconds) {
		avgholdsecs = (abs(((qe->parent->holdtime + 30) - (now - qe->start))) - 60 * avgholdmins) / qe->parent->roundingseconds;
		avgholdsecs *= qe->parent->roundingseconds;
	} else {
		avgholdsecs = 0;
	}

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Hold time for %s is %d minutes %d seconds\n", qe->parent->name, avgholdmins, avgholdsecs);

	/* If the hold time is >1 min, if it's enabled, and if it's not
	   supposed to be only once and we have already said it, say it */
    if ((avgholdmins+avgholdsecs) > 0 && qe->parent->announceholdtime &&
        ((qe->parent->announceholdtime == ANNOUNCEHOLDTIME_ONCE && !qe->last_pos) ||
        !(qe->parent->announceholdtime == ANNOUNCEHOLDTIME_ONCE))) {
		res = play_file(qe->chan, qe->parent->sound_holdtime);
		if (res)
			goto playout;

		if (avgholdmins > 0) {
			if (avgholdmins < 2) {
				res = play_file(qe->chan, qe->parent->sound_lessthan);
				if (res)
					goto playout;

				res = ast_say_number(qe->chan, 2, AST_DIGIT_ANY, qe->chan->language, NULL);
				if (res)
					goto playout;
			} else {
				res = ast_say_number(qe->chan, avgholdmins, AST_DIGIT_ANY, qe->chan->language, NULL);
				if (res)
					goto playout;
			}
			
			res = play_file(qe->chan, qe->parent->sound_minutes);
			if (res)
				goto playout;
		}
		if (avgholdsecs>0) {
			res = ast_say_number(qe->chan, avgholdsecs, AST_DIGIT_ANY, qe->chan->language, NULL);
			if (res)
				goto playout;

			res = play_file(qe->chan, qe->parent->sound_seconds);
			if (res)
				goto playout;
		}

	}

posout:
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Told %s in %s their queue position (which was %d)\n",
			qe->chan->name, qe->parent->name, qe->pos);
	res = play_file(qe->chan, qe->parent->sound_thanks);

playout:

	if ((res > 0 && !valid_exit(qe, res)))
		res = 0;

	/* Set our last_pos indicators */
	qe->last_pos = now;
	qe->last_pos_said = qe->pos;

	/* Don't restart music on hold if we're about to exit the caller from the queue */
	if (!res)
		ast_moh_start(qe->chan, qe->moh, NULL);

	return res;
}

static void recalc_holdtime(struct queue_ent *qe, int newholdtime)
{
	int oldvalue;

	/* Calculate holdtime using an exponential average */
	/* Thanks to SRT for this contribution */
	/* 2^2 (4) is the filter coefficient; a higher exponent would give old entries more weight */

	ao2_lock(qe->parent);
	oldvalue = qe->parent->holdtime;
	qe->parent->holdtime = (((oldvalue << 2) - oldvalue) + newholdtime) >> 2;
	ao2_unlock(qe->parent);
}


static void leave_queue(struct queue_ent *qe)
{
	struct call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int pos = 0;

	if (!(q = qe->parent))
		return;
	ao2_lock(q);

	prev = NULL;
	for (cur = q->head; cur; cur = cur->next) {
		if (cur == qe) {
			q->count--;

			/* Take us out of the queue */
			manager_event(EVENT_FLAG_CALL, "Leave",
				"Channel: %s\r\nQueue: %s\r\nCount: %d\r\nUniqueid: %s\r\n",
				qe->chan->name, q->name,  q->count, qe->chan->uniqueid);
			if (option_debug)
				ast_log(LOG_DEBUG, "Queue '%s' Leave, Channel '%s'\n", q->name, qe->chan->name );
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
	}
	ao2_unlock(q);

	if (q->dead && !q->count) {	
		/* It's dead and nobody is in it, so kill it */
		remove_queue(q);
	}
}

/* Hang up a list of outgoing calls */
static void hangupcalls(struct callattempt *outgoing, struct ast_channel *exception)
{
	struct callattempt *oo;

	while (outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception))
			ast_hangup(outgoing->chan);
		oo = outgoing;
		outgoing = outgoing->q_next;
		if (oo->member)
			ao2_ref(oo->member, -1);
		free(oo);
	}
}

/*!
 * \brief Get the number of members available to accept a call.
 *
 * \note The queue passed in should be locked prior to this function call
 *
 * \param[in] q The queue for which we are couting the number of available members
 * \return Return the number of available members in queue q
 */
static int num_available_members(struct call_queue *q)
{
	struct member *mem;
	int avl = 0;
	struct ao2_iterator mem_iter;

	mem_iter = ao2_iterator_init(q->members, 0);
	while ((mem = ao2_iterator_next(&mem_iter))) {
		switch (mem->status) {
		case AST_DEVICE_INUSE:
			if (!q->ringinuse)
				break;
			/* else fall through */
		case AST_DEVICE_NOT_INUSE:
		case AST_DEVICE_UNKNOWN:
			if (!mem->paused) {
				avl++;
			}
			break;
		}
		ao2_ref(mem, -1);

		/* If autofill is not enabled or if the queue's strategy is ringall, then
		 * we really don't care about the number of available members so much as we
		 * do that there is at least one available.
		 *
		 * In fact, we purposely will return from this function stating that only
		 * one member is available if either of those conditions hold. That way,
		 * functions which determine what action to take based on the number of available
		 * members will operate properly. The reasoning is that even if multiple
		 * members are available, only the head caller can actually be serviced.
		 */
		if ((!q->autofill || q->strategy == QUEUE_STRATEGY_RINGALL) && avl) {
			break;
		}
	}
	ao2_iterator_destroy(&mem_iter);

	return avl;
}

/* traverse all defined queues which have calls waiting and contain this member
   return 0 if no other queue has precedence (higher weight) or 1 if found  */
static int compare_weight(struct call_queue *rq, struct member *member)
{
	struct call_queue *q;
	struct member *mem;
	int found = 0;
	
	/* &qlock and &rq->lock already set by try_calling()
	 * to solve deadlock */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (q == rq) /* don't check myself, could deadlock */
			continue;
		ao2_lock(q);
		if (q->count && q->members) {
			if ((mem = ao2_find(q->members, member, OBJ_POINTER))) {
				ast_log(LOG_DEBUG, "Found matching member %s in queue '%s'\n", mem->interface, q->name);
				if (q->weight > rq->weight && q->count >= num_available_members(q)) {
					ast_log(LOG_DEBUG, "Queue '%s' (weight %d, calls %d) is preferred over '%s' (weight %d, calls %d)\n", q->name, q->weight, q->count, rq->name, rq->weight, rq->count);
					found = 1;
				}
				ao2_ref(mem, -1);
			}
		}
		ao2_unlock(q);
		if (found)
			break;
	}
	return found;
}

/*! \brief common hangup actions */
static void do_hang(struct callattempt *o)
{
	o->stillgoing = 0;
	ast_hangup(o->chan);
	o->chan = NULL;
}

static char *vars2manager(struct ast_channel *chan, char *vars, size_t len)
{
	char *tmp = alloca(len);

	if (pbx_builtin_serialize_variables(chan, tmp, len)) {
		int i, j;

		/* convert "\n" to "\nVariable: " */
		strcpy(vars, "Variable: ");

		for (i = 0, j = 10; (i < len - 1) && (j < len - 1); i++, j++) {
			vars[j] = tmp[i];

			if (tmp[i + 1] == '\0')
				break;
			if (tmp[i] == '\n') {
				vars[j++] = '\r';
				vars[j++] = '\n';

				ast_copy_string(&(vars[j]), "Variable: ", len - j);
				j += 9;
			}
		}
		if (j > len - 3)
			j = len - 3;
		vars[j++] = '\r';
		vars[j++] = '\n';
		vars[j] = '\0';
	} else {
		/* there are no channel variables; leave it blank */
		*vars = '\0';
	}
	return vars;
}

/*! \brief Part 2 of ring_one
 *
 * Does error checking before attempting to request a channel and call a member. This
 * function is only called from ring_one
 */
static int ring_entry(struct queue_ent *qe, struct callattempt *tmp, int *busies)
{
	int res;
	int status;
	char tech[256];
	char *location;
	const char *macrocontext, *macroexten;

	/* on entry here, we know that tmp->chan == NULL */
	if (qe->parent->wrapuptime && (time(NULL) - tmp->lastcall < qe->parent->wrapuptime)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Wrapuptime not yet expired for %s\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}

	if (!qe->parent->ringinuse && (tmp->member->status != AST_DEVICE_NOT_INUSE) && (tmp->member->status != AST_DEVICE_UNKNOWN)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "%s in use, can't receive call\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}

	if (tmp->member->paused) {
		if (option_debug)
			ast_log(LOG_DEBUG, "%s paused, can't receive call\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}
	if (use_weight && compare_weight(qe->parent,tmp->member)) {
		ast_log(LOG_DEBUG, "Priority queue delaying call to %s:%s\n", qe->parent->name, tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}

	ast_copy_string(tech, tmp->interface, sizeof(tech));
	if ((location = strchr(tech, '/')))
		*location++ = '\0';
	else
		location = "";

	/* Request the peer */
	tmp->chan = ast_request(tech, qe->chan->nativeformats, location, &status);
	if (!tmp->chan) {			/* If we can't, just go on to the next call */
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;

		update_status(tmp->member->state_interface, ast_device_state(tmp->member->state_interface));

		ao2_lock(qe->parent);
		qe->parent->rrpos++;
		ao2_unlock(qe->parent);

		(*busies)++;
		return 0;
	}
	
	tmp->chan->appl = "AppQueue";
	tmp->chan->data = "(Outgoing Line)";
	tmp->chan->whentohangup = 0;
	if (tmp->chan->cid.cid_num)
		free(tmp->chan->cid.cid_num);
	tmp->chan->cid.cid_num = ast_strdup(qe->chan->cid.cid_num);
	if (tmp->chan->cid.cid_name)
		free(tmp->chan->cid.cid_name);
	tmp->chan->cid.cid_name = ast_strdup(qe->chan->cid.cid_name);
	if (tmp->chan->cid.cid_ani)
		free(tmp->chan->cid.cid_ani);
	tmp->chan->cid.cid_ani = ast_strdup(qe->chan->cid.cid_ani);

	/* Inherit specially named variables from parent channel */
	ast_channel_inherit_variables(qe->chan, tmp->chan);
	ast_channel_datastore_inherit(qe->chan, tmp->chan);

	/* Presense of ADSI CPE on outgoing channel follows ours */
	tmp->chan->adsicpe = qe->chan->adsicpe;

	/* Inherit context and extension */
	ast_channel_lock(qe->chan);
	macrocontext = pbx_builtin_getvar_helper(qe->chan, "MACRO_CONTEXT");
	if (!ast_strlen_zero(macrocontext))
		ast_copy_string(tmp->chan->dialcontext, macrocontext, sizeof(tmp->chan->dialcontext));
	else
		ast_copy_string(tmp->chan->dialcontext, qe->chan->context, sizeof(tmp->chan->dialcontext));
	macroexten = pbx_builtin_getvar_helper(qe->chan, "MACRO_EXTEN");
	if (!ast_strlen_zero(macroexten))
		ast_copy_string(tmp->chan->exten, macroexten, sizeof(tmp->chan->exten));
	else
		ast_copy_string(tmp->chan->exten, qe->chan->exten, sizeof(tmp->chan->exten));
	if (ast_cdr_isset_unanswered()) {
		/* they want to see the unanswered dial attempts! */
		/* set up the CDR fields on all the CDRs to give sensical information */
		ast_cdr_setdestchan(tmp->chan->cdr, tmp->chan->name);
		strcpy(tmp->chan->cdr->clid, qe->chan->cdr->clid);
		strcpy(tmp->chan->cdr->channel, qe->chan->cdr->channel);
		strcpy(tmp->chan->cdr->src, qe->chan->cdr->src);
		strcpy(tmp->chan->cdr->dst, qe->chan->exten);
		strcpy(tmp->chan->cdr->dcontext, qe->chan->context);
		strcpy(tmp->chan->cdr->lastapp, qe->chan->cdr->lastapp);
		strcpy(tmp->chan->cdr->lastdata, qe->chan->cdr->lastdata);
		tmp->chan->cdr->amaflags = qe->chan->cdr->amaflags;
		strcpy(tmp->chan->cdr->accountcode, qe->chan->cdr->accountcode);
		strcpy(tmp->chan->cdr->userfield, qe->chan->cdr->userfield);
	}
	ast_channel_unlock(qe->chan);

	/* Place the call, but don't wait on the answer */
	if ((res = ast_call(tmp->chan, location, 0))) {
		/* Again, keep going even if there's an error */
		if (option_debug)
			ast_log(LOG_DEBUG, "ast call on peer returned %d\n", res);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", tmp->interface);
		do_hang(tmp);
		(*busies)++;
		update_status(tmp->member->state_interface, ast_device_state(tmp->member->state_interface));
		return 0;
	} else if (qe->parent->eventwhencalled) {
		char vars[2048];

		manager_event(EVENT_FLAG_AGENT, "AgentCalled",
					"AgentCalled: %s\r\n"
					"AgentName: %s\r\n"
					"ChannelCalling: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n"
					"Context: %s\r\n"
					"Extension: %s\r\n"
					"Priority: %d\r\n"
					"%s",
					tmp->interface, tmp->member->membername, qe->chan->name,
					tmp->chan->cid.cid_num ? tmp->chan->cid.cid_num : "unknown",
					tmp->chan->cid.cid_name ? tmp->chan->cid.cid_name : "unknown",
					qe->chan->context, qe->chan->exten, qe->chan->priority,
					qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Called %s\n", tmp->interface);
	}

	update_status(tmp->member->state_interface, ast_device_state(tmp->member->state_interface));
	return 1;
}

/*! \brief find the entry with the best metric, or NULL */
static struct callattempt *find_best(struct callattempt *outgoing)
{
	struct callattempt *best = NULL, *cur;

	for (cur = outgoing; cur; cur = cur->q_next) {
		if (cur->stillgoing &&					/* Not already done */
			!cur->chan &&					/* Isn't already going */
			(!best || cur->metric < best->metric)) {		/* We haven't found one yet, or it's better */
			best = cur;
		}
	}

	return best;
}

/*! \brief Place a call to a queue member
 *
 * Once metrics have been calculated for each member, this function is used
 * to place a call to the appropriate member (or members). The low-level
 * channel-handling and error detection is handled in ring_entry
 *
 * Returns 1 if a member was called successfully, 0 otherwise
 */
static int ring_one(struct queue_ent *qe, struct callattempt *outgoing, int *busies)
{
	int ret = 0;

	while (ret == 0) {
		struct callattempt *best = find_best(outgoing);
		if (!best) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Nobody left to try ringing in queue\n");
			break;
		}
		if (qe->parent->strategy == QUEUE_STRATEGY_RINGALL) {
			struct callattempt *cur;
			/* Ring everyone who shares this best metric (for ringall) */
			for (cur = outgoing; cur; cur = cur->q_next) {
				if (cur->stillgoing && !cur->chan && cur->metric <= best->metric) {
					if (option_debug)
						ast_log(LOG_DEBUG, "(Parallel) Trying '%s' with metric %d\n", cur->interface, cur->metric);
					ret |= ring_entry(qe, cur, busies);
				}
			}
		} else {
			/* Ring just the best channel */
			if (option_debug)
				ast_log(LOG_DEBUG, "Trying '%s' with metric %d\n", best->interface, best->metric);
			ret = ring_entry(qe, best, busies);
		}
	}

	return ret;
}

static int store_next(struct queue_ent *qe, struct callattempt *outgoing)
{
	struct callattempt *best = find_best(outgoing);

	if (best) {
		/* Ring just the best channel */
		if (option_debug)
			ast_log(LOG_DEBUG, "Next is '%s' with metric %d\n", best->interface, best->metric);
		qe->parent->rrpos = best->metric % 1000;
	} else {
		/* Just increment rrpos */
		if (qe->parent->wrapped) {
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

static int say_periodic_announcement(struct queue_ent *qe)
{
	int res = 0;
	time_t now;

	/* Get the current time */
	time(&now);

	/* Check to see if it is time to announce */
	if ((now - qe->last_periodic_announce_time) < qe->parent->periodicannouncefrequency)
		return 0;

	/* Stop the music on hold so we can play our own file */
	ast_moh_stop(qe->chan);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Playing periodic announcement\n");

	/* Check to make sure we have a sound file. If not, reset to the first sound file */
	if (qe->last_periodic_announce_sound >= MAX_PERIODIC_ANNOUNCEMENTS || !strlen(qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound])) {
		qe->last_periodic_announce_sound = 0;
	}
	
	/* play the announcement */
	res = play_file(qe->chan, qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound]);

	if (res > 0 && !valid_exit(qe, res))
		res = 0;

	/* Resume Music on Hold if the caller is going to stay in the queue */
	if (!res)
		ast_moh_start(qe->chan, qe->moh, NULL);

	/* update last_periodic_announce_time */
	qe->last_periodic_announce_time = now;

	/* Update the current periodic announcement to the next announcement */
	qe->last_periodic_announce_sound++;
	
	return res;
}

static void record_abandoned(struct queue_ent *qe)
{
	ao2_lock(qe->parent);
	manager_event(EVENT_FLAG_AGENT, "QueueCallerAbandon",
		"Queue: %s\r\n"
		"Uniqueid: %s\r\n"
		"Position: %d\r\n"
		"OriginalPosition: %d\r\n"
		"HoldTime: %d\r\n",
		qe->parent->name, qe->chan->uniqueid, qe->pos, qe->opos, (int)(time(NULL) - qe->start));

	qe->parent->callsabandoned++;
	ao2_unlock(qe->parent);
}

/*! \brief RNA == Ring No Answer. Common code that is executed when we try a queue member and they don't answer. */
static void rna(int rnatime, struct queue_ent *qe, char *interface, char *membername, int pause)
{
	if (option_verbose > 2)
		ast_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", rnatime);
	ast_queue_log(qe->parent->name, qe->chan->uniqueid, membername, "RINGNOANSWER", "%d", rnatime);
	if (qe->parent->autopause && pause) {
		if (!set_member_paused(qe->parent->name, interface, 1)) {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "Auto-Pausing Queue Member %s in queue %s since they failed to answer.\n", interface, qe->parent->name);
		} else {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "Failed to pause Queue Member %s in queue %s!\n", interface, qe->parent->name);
		}
	}
	return;
}

#define AST_MAX_WATCHERS 256
/*! \brief Wait for a member to answer the call
 *
 * \param[in] qe the queue_ent corresponding to the caller in the queue
 * \param[in] outgoing the list of callattempts. Relevant ones will have their chan and stillgoing parameters non-zero
 * \param[in] to the amount of time (in milliseconds) to wait for a response
 * \param[out] digit if a user presses a digit to exit the queue, this is the digit the caller pressed
 * \param[in] prebusies number of busy members calculated prior to calling wait_for_answer
 * \param[in] caller_disconnect if the 'H' option is used when calling Queue(), this is used to detect if the caller pressed * to disconnect the call
 * \param[in] forwardsallowed used to detect if we should allow call forwarding, based on the 'i' option to Queue()
 */
static struct callattempt *wait_for_answer(struct queue_ent *qe, struct callattempt *outgoing, int *to, char *digit, int prebusies, int caller_disconnect, int forwardsallowed)
{
	char *queue = qe->parent->name;
	struct callattempt *o, *start = NULL, *prev = NULL;
	int status;
	int numbusies = prebusies;
	int numnochan = 0;
	int stillgoing = 0;
	int orig = *to;
	struct ast_frame *f;
	struct callattempt *peer = NULL;
	struct ast_channel *winner;
	struct ast_channel *in = qe->chan;
	char on[80] = "";
	char membername[80] = "";
	long starttime = 0;
	long endtime = 0;	

	starttime = (long) time(NULL);
	
	while (*to && !peer) {
		int numlines, retry, pos = 1;
		struct ast_channel *watchers[AST_MAX_WATCHERS];
		watchers[0] = in;
		start = NULL;

		for (retry = 0; retry < 2; retry++) {
			numlines = 0;
			for (o = outgoing; o; o = o->q_next) { /* Keep track of important channels */
				if (o->stillgoing) {	/* Keep track of important channels */
					stillgoing = 1;
					if (o->chan) {
						watchers[pos++] = o->chan;
						if (!start)
							start = o;
						else
							prev->call_next = o;
						prev = o;
					}
				}
				numlines++;
			}
			if (pos > 1 /* found */ || !stillgoing /* nobody listening */ ||
				(qe->parent->strategy != QUEUE_STRATEGY_RINGALL) /* ring would not be delivered */)
				break;
			/* On "ringall" strategy we only move to the next penalty level
			   when *all* ringing phones are done in the current penalty level */
			ring_one(qe, outgoing, &numbusies);
			/* and retry... */
		}
		if (pos == 1 /* not found */) {
			if (numlines == (numbusies + numnochan)) {
				ast_log(LOG_DEBUG, "Everyone is busy at this time\n");
			} else {
				ast_log(LOG_NOTICE, "No one is answering queue '%s' (%d/%d/%d)\n", queue, numlines, numbusies, numnochan);
			}
			*to = 0;
			return NULL;
		}

		/* Poll for events from both the incoming channel as well as any outgoing channels */
		winner = ast_waitfor_n(watchers, pos, to);

		/* Service all of the outgoing channels */
		for (o = start; o; o = o->call_next) {
			if (o->stillgoing && (o->chan) &&  (o->chan->_state == AST_STATE_UP)) {
				if (!peer) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
					peer = o;
				}
			} else if (o->chan && (o->chan == winner)) {

				ast_copy_string(on, o->member->interface, sizeof(on));
				ast_copy_string(membername, o->member->membername, sizeof(membername));

				if (!ast_strlen_zero(o->chan->call_forward) && !forwardsallowed) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Forwarding %s to '%s' prevented.\n", in->name, o->chan->call_forward);
					numnochan++;
					do_hang(o);
					winner = NULL;
					continue;
				} else if (!ast_strlen_zero(o->chan->call_forward)) {
					char tmpchan[256];
					char *stuff;
					char *tech;

					ast_copy_string(tmpchan, o->chan->call_forward, sizeof(tmpchan));
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff++ = '\0';
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
					if (!o->chan) {
						ast_log(LOG_NOTICE,
							"Forwarding failed to create channel to dial '%s/%s'\n",
							tech, stuff);
						o->stillgoing = 0;
						numnochan++;
					} else {
						ast_channel_inherit_variables(in, o->chan);
						ast_channel_datastore_inherit(in, o->chan);
						if (o->chan->cid.cid_num)
							free(o->chan->cid.cid_num);
						o->chan->cid.cid_num = ast_strdup(in->cid.cid_num);

						if (o->chan->cid.cid_name)
							free(o->chan->cid.cid_name);
						o->chan->cid.cid_name = ast_strdup(in->cid.cid_name);

						ast_string_field_set(o->chan, accountcode, in->accountcode);
						o->chan->cdrflags = in->cdrflags;

						if (in->cid.cid_ani) {
							if (o->chan->cid.cid_ani)
								free(o->chan->cid.cid_ani);
							o->chan->cid.cid_ani = ast_strdup(in->cid.cid_ani);
						}
						if (o->chan->cid.cid_rdnis)
							free(o->chan->cid.cid_rdnis);
						o->chan->cid.cid_rdnis = ast_strdup(S_OR(in->macroexten, in->exten));
						if (ast_call(o->chan, stuff, 0)) {
							ast_log(LOG_NOTICE, "Forwarding failed to dial '%s/%s'\n",
								tech, stuff);
							do_hang(o);
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
						switch (f->subclass) {
						case AST_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
								peer = o;
							}
							break;
						case AST_CONTROL_BUSY:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							do_hang(o);
							endtime = (long)time(NULL);
							endtime -= starttime;
							rna(endtime * 1000, qe, on, membername, 0);
							if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								/* Have enough time for a queue member to answer? */
								if (*to > 500) {
									ring_one(qe, outgoing, &numbusies);
									starttime = (long) time(NULL);
								}
							}
							numbusies++;
							break;
						case AST_CONTROL_CONGESTION:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							endtime = (long)time(NULL);
							endtime -= starttime;
							rna(endtime * 1000, qe, on, membername, 0);
							do_hang(o);
							if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								if (*to > 500) {
									ring_one(qe, outgoing, &numbusies);
									starttime = (long) time(NULL);
								}
							}
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
				} else { /* ast_read() returned NULL */
					endtime = (long) time(NULL) - starttime;
					rna(endtime * 1000, qe, on, membername, 1);
					do_hang(o);
					if (qe->parent->strategy != QUEUE_STRATEGY_RINGALL) {
						if (qe->parent->timeoutrestart)
							*to = orig;
						if (*to > 500) {
							ring_one(qe, outgoing, &numbusies);
							starttime = (long) time(NULL);
						}
					}
				}
			}
		}

		/* If we received an event from the caller, deal with it. */
		if (winner == in) {
			f = ast_read(in);
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to = -1;
				if (f)
					ast_frfree(f);
				return NULL;
			}
			if ((f->frametype == AST_FRAME_DTMF) && caller_disconnect && (f->subclass == '*')) {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
				*to = 0;
				ast_frfree(f);
				return NULL;
			}
			if ((f->frametype == AST_FRAME_DTMF) && valid_exit(qe, f->subclass)) {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_3 "User pressed digit: %c\n", f->subclass);
				*to = 0;
				*digit = f->subclass;
				ast_frfree(f);
				return NULL;
			}
			ast_frfree(f);
		}
		if (!*to) {
			for (o = start; o; o = o->call_next)
				rna(orig, qe, o->interface, o->member->membername, 1);
		}
	}

	return peer;
}

/*! \brief Check if we should start attempting to call queue members
 *
 * A simple process, really. Count the number of members who are available
 * to take our call and then see if we are in a position in the queue at
 * which a member could accept our call.
 *
 * \param[in] qe The caller who wants to know if it is his turn
 * \retval 0 It is not our turn
 * \retval 1 It is our turn
 */
static int is_our_turn(struct queue_ent *qe)
{
	struct queue_ent *ch;
	int res;
	int avl;
	int idx = 0;
	/* This needs a lock. How many members are available to be served? */
	ao2_lock(qe->parent);

	avl = num_available_members(qe->parent);

	ch = qe->parent->head;

	if (option_debug) {
		ast_log(LOG_DEBUG, "There %s %d available %s.\n", avl != 1 ? "are" : "is", avl, avl != 1 ? "members" : "member");
	}

	while ((idx < avl) && (ch) && (ch != qe)) {
		if (!ch->pending)
			idx++;
		ch = ch->next;			
	}

	ao2_unlock(qe->parent);
	/* If the queue entry is within avl [the number of available members] calls from the top ... 
	 * Autofill and position check added to support autofill=no (as only calls
	 * from the front of the queue are valid when autofill is disabled)
	 */
	if (ch && idx < avl && (qe->parent->autofill || qe->pos == 1)) {
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
/*! \brief The waiting areas for callers who are not actively calling members
 *
 * This function is one large loop. This function will return if a caller
 * either exits the queue or it becomes that caller's turn to attempt calling
 * queue members. Inside the loop, we service the caller with periodic announcements,
 * holdtime announcements, etc. as configured in queues.conf
 *
 * \retval  0 if the caller's turn has arrived
 * \retval -1 if the caller should exit the queue.
 */
static int wait_our_turn(struct queue_ent *qe, int ringing, enum queue_result *reason)
{
	int res = 0;

	/* This is the holding pen for callers 2 through maxlen */
	for (;;) {
		enum queue_member_status stat;

		if (is_our_turn(qe))
			break;

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}

		stat = get_member_status(qe->parent, qe->max_penalty);

		/* leave the queue if no agents, if enabled */
		if (qe->parent->leavewhenempty && (stat == QUEUE_NO_MEMBERS)) {
			*reason = QUEUE_LEAVEEMPTY;
			ast_queue_log(qe->parent->name, qe->chan->uniqueid, "NONE", "EXITEMPTY", "%d|%d|%ld", qe->pos, qe->opos, (long)time(NULL) - qe->start);
			leave_queue(qe);
			break;
		}

		/* leave the queue if no reachable agents, if enabled */
		if ((qe->parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS)) {
			*reason = QUEUE_LEAVEUNAVAIL;
			ast_queue_log(qe->parent->name, qe->chan->uniqueid, "NONE", "EXITEMPTY", "%d|%d|%ld", qe->pos, qe->opos, (long)time(NULL) - qe->start);
			leave_queue(qe);
			break;
		}

		/* Make a position announcement, if enabled */
		if (qe->parent->announcefrequency && !ringing &&
			(res = say_position(qe)))
			break;

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}

		/* Make a periodic announcement, if enabled */
		if (qe->parent->periodicannouncefrequency && !ringing &&
			(res = say_periodic_announcement(qe)))
			break;

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}
		
		/* Wait a second before checking again */
		if ((res = ast_waitfordigit(qe->chan, RECHECK * 1000))) {
			if (res > 0 && !valid_exit(qe, res))
				res = 0;
			else
				break;
		}
		
		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) >= qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			break;
		}
	}

	return res;
}

static int update_queue(struct call_queue *q, struct member *member, int callcompletedinsl)
{
	ao2_lock(q);
	time(&member->lastcall);
	member->calls++;
	q->callscompleted++;
	if (callcompletedinsl)
		q->callscompletedinsl++;
	ao2_unlock(q);
	return 0;
}

/*! \brief Calculate the metric of each member in the outgoing callattempts
 *
 * A numeric metric is given to each member depending on the ring strategy used
 * by the queue. Members with lower metrics will be called before members with
 * higher metrics
 */
static int calc_metric(struct call_queue *q, struct member *mem, int pos, struct queue_ent *qe, struct callattempt *tmp)
{
	if (qe->max_penalty && (mem->penalty > qe->max_penalty))
		return -1;

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
	case QUEUE_STRATEGY_RRORDERED:
	case QUEUE_STRATEGY_RRMEMORY:
		if (pos < q->rrpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > q->rrpos)
				/* Indicate there is another priority */
				q->wrapped = 1;
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_RANDOM:
		tmp->metric = ast_random() % 1000;
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

struct queue_transfer_ds {
	struct queue_ent *qe;
	struct member *member;
	time_t starttime;
	int callcompletedinsl;
};

static void queue_transfer_destroy(void *data)
{
	struct queue_transfer_ds *qtds = data;
	ast_free(qtds);
}

/*! \brief a datastore used to help correctly log attended transfers of queue callers
 */
static const struct ast_datastore_info queue_transfer_info = {
	.type = "queue_transfer",
	.chan_fixup = queue_transfer_fixup,
	.destroy = queue_transfer_destroy,
};

/*! \brief Log an attended transfer when a queue caller channel is masqueraded
 *
 * When a caller is masqueraded, we want to log a transfer. Fixup time is the closest we can come to when
 * the actual transfer occurs. This happens during the masquerade after datastores are moved from old_chan
 * to new_chan. This is why new_chan is referenced for exten, context, and datastore information.
 *
 * At the end of this, we want to remove the datastore so that this fixup function is not called on any
 * future masquerades of the caller during the current call.
 */
static void queue_transfer_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct queue_transfer_ds *qtds = data;
	struct queue_ent *qe = qtds->qe;
	struct member *member = qtds->member;
	time_t callstart = qtds->starttime;
	int callcompletedinsl = qtds->callcompletedinsl;
	struct ast_datastore *datastore;

	ast_queue_log(qe->parent->name, qe->chan->uniqueid, member->membername, "TRANSFER", "%s|%s|%ld|%ld",
				new_chan->exten, new_chan->context, (long) (callstart - qe->start),
				(long) (time(NULL) - callstart));

	update_queue(qe->parent, member, callcompletedinsl);
	
	/* No need to lock the channels because they are already locked in ast_do_masquerade */
	if ((datastore = ast_channel_datastore_find(old_chan, &queue_transfer_info, NULL))) {
		ast_channel_datastore_remove(old_chan, datastore);
	} else {
		ast_log(LOG_WARNING, "Can't find the queue_transfer datastore.\n");
	}
}

/*! \brief mechanism to tell if a queue caller was atxferred by a queue member.
 *
 * When a caller is atxferred, then the queue_transfer_info datastore
 * is removed from the channel. If it's still there after the bridge is
 * broken, then the caller was not atxferred.
 *
 * \note Only call this with chan locked
 */
static int attended_transfer_occurred(struct ast_channel *chan)
{
	return ast_channel_datastore_find(chan, &queue_transfer_info, NULL) ? 0 : 1;
}

/*! \brief create a datastore for storing relevant info to log attended transfers in the queue_log
 */
static struct ast_datastore *setup_transfer_datastore(struct queue_ent *qe, struct member *member, time_t starttime, int callcompletedinsl)
{
	struct ast_datastore *ds;
	struct queue_transfer_ds *qtds = ast_calloc(1, sizeof(*qtds));

	if (!qtds) {
		ast_log(LOG_WARNING, "Memory allocation error!\n");
		return NULL;
	}

	ast_channel_lock(qe->chan);
	if (!(ds = ast_channel_datastore_alloc(&queue_transfer_info, NULL))) {
		ast_channel_unlock(qe->chan);
		ast_log(LOG_WARNING, "Unable to create transfer datastore. queue_log will not show attended transfer\n");
		return NULL;
	}

	qtds->qe = qe;
	/* This member is refcounted in try_calling, so no need to add it here, too */
	qtds->member = member;
	qtds->starttime = starttime;
	qtds->callcompletedinsl = callcompletedinsl;
	ds->data = qtds;
	ast_channel_datastore_add(qe->chan, ds);
	ast_channel_unlock(qe->chan);
	return ds;
}


/*! \brief A large function which calls members, updates statistics, and bridges the caller and a member
 * 
 * Here is the process of this function
 * 1. Process any options passed to the Queue() application. Options here mean the third argument to Queue()
 * 2. Iterate trough the members of the queue, creating a callattempt corresponding to each member. During this
 *    iteration, we also check the dialed_interfaces datastore to see if we have already attempted calling this
 *    member. If we have, we do not create a callattempt. This is in place to prevent call forwarding loops. Also
 *    during each iteration, we call calc_metric to determine which members should be rung when.
 * 3. Call ring_one to place a call to the appropriate member(s)
 * 4. Call wait_for_answer to wait for an answer. If no one answers, return.
 * 5. Take care of any holdtime announcements, member delays, or other options which occur after a call has been answered.
 * 6. Start the monitor or mixmonitor if the option is set
 * 7. Remove the caller from the queue to allow other callers to advance
 * 8. Bridge the call.
 * 9. Do any post processing after the call has disconnected.
 *
 * \param[in] qe the queue_ent structure which corresponds to the caller attempting to reach members
 * \param[in] options the options passed as the third parameter to the Queue() application
 * \param[in] url the url passed as the fourth parameter to the Queue() application
 * \param[in,out] tries the number of times we have tried calling queue members
 * \param[out] noption set if the call to Queue() has the 'n' option set.
 * \param[in] agi the agi passed as the fifth parameter to the Queue() application
 */

static int try_calling(struct queue_ent *qe, const char *options, char *announceoverride, const char *url, int *tries, int *noption, const char *agi)
{
	struct member *cur;
	struct callattempt *outgoing = NULL; /* the list of calls we are building */
	int to;
	char oldexten[AST_MAX_EXTENSION]="";
	char oldcontext[AST_MAX_CONTEXT]="";
	char queuename[256]="";
	struct ast_channel *peer;
	struct ast_channel *which;
	struct callattempt *lpeer;
	struct member *member;
	struct ast_app *app;
	int res = 0, bridge = 0;
	int numbusies = 0;
	int x=0;
	char *announce = NULL;
	char digit = 0;
	time_t callstart;
	time_t now = time(NULL);
	struct ast_bridge_config bridge_config;
	char nondataquality = 1;
	char *agiexec = NULL;
	int ret = 0;
	const char *monitorfilename;
	const char *monitor_exec;
	const char *monitor_options;
	char tmpid[256], tmpid2[256];
	char meid[1024], meid2[1024];
	char mixmonargs[1512];
	struct ast_app *mixmonapp = NULL;
	char *p;
	char vars[2048];
	int forwardsallowed = 1;
	int callcompletedinsl;
	struct ao2_iterator memi;
	struct ast_datastore *datastore, *transfer_ds;
	const int need_weight = use_weight;

	ast_channel_lock(qe->chan);
	datastore = ast_channel_datastore_find(qe->chan, &dialed_interface_info, NULL);
	ast_channel_unlock(qe->chan);

	memset(&bridge_config, 0, sizeof(bridge_config));
	time(&now);

	/* If we've already exceeded our timeout, then just stop
	 * This should be extremely rare. queue_exec will take care
	 * of removing the caller and reporting the timeout as the reason.
	 */
	if (qe->expire && now >= qe->expire) {
		res = 0;
		goto out;
	}
		
	for (; options && *options; options++)
		switch (*options) {
		case 't':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_REDIRECT);
			break;
		case 'T':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_REDIRECT);
			break;
		case 'w':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_AUTOMON);
			break;
		case 'W':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_AUTOMON);
			break;
		case 'd':
			nondataquality = 0;
			break;
		case 'h':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_DISCONNECT);
			break;
		case 'H':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT);
			break;
		case 'n':
			if (qe->parent->strategy == QUEUE_STRATEGY_ROUNDROBIN || qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY || qe->parent->strategy == QUEUE_STRATEGY_RRORDERED)
				(*tries)++;
			else
				*tries = qe->parent->membercount;
			*noption = 1;
			break;
		case 'i':
			forwardsallowed = 0;
			break;
		}

	/* Hold the lock while we setup the outgoing calls */

	if (need_weight)
		AST_LIST_LOCK(&queues);
	ao2_lock(qe->parent);
	if (option_debug)
		ast_log(LOG_DEBUG, "%s is trying to call a queue member.\n",
							qe->chan->name);
	ast_copy_string(queuename, qe->parent->name, sizeof(queuename));
	if (!ast_strlen_zero(qe->announce))
		announce = qe->announce;
	if (!ast_strlen_zero(announceoverride))
		announce = announceoverride;

	memi = ao2_iterator_init(qe->parent->members, 0);
	while ((cur = ao2_iterator_next(&memi))) {
		struct callattempt *tmp = ast_calloc(1, sizeof(*tmp));
		struct ast_dialed_interface *di;
		AST_LIST_HEAD(, ast_dialed_interface) *dialed_interfaces;
		if (!tmp) {
			ao2_iterator_destroy(&memi);
			ao2_ref(cur, -1);
			ao2_unlock(qe->parent);
			if (need_weight)
				AST_LIST_UNLOCK(&queues);
			goto out;
		}
		if (!datastore) {
			if (!(datastore = ast_channel_datastore_alloc(&dialed_interface_info, NULL))) {
				ao2_iterator_destroy(&memi);
				ao2_ref(cur, -1);
				ao2_unlock(qe->parent);
				if (need_weight)
					AST_LIST_UNLOCK(&queues);
				free(tmp);
				goto out;
			}
			datastore->inheritance = DATASTORE_INHERIT_FOREVER;
			if (!(dialed_interfaces = ast_calloc(1, sizeof(*dialed_interfaces)))) {
				ao2_iterator_destroy(&memi);
				ao2_ref(cur, -1);
				ao2_unlock(qe->parent);
				if (need_weight)
					AST_LIST_UNLOCK(&queues);
				free(tmp);
				goto out;
			}
			datastore->data = dialed_interfaces;
			AST_LIST_HEAD_INIT(dialed_interfaces);

			ast_channel_lock(qe->chan);
			ast_channel_datastore_add(qe->chan, datastore);
			ast_channel_unlock(qe->chan);
		} else
			dialed_interfaces = datastore->data;

		AST_LIST_LOCK(dialed_interfaces);
		AST_LIST_TRAVERSE(dialed_interfaces, di, list) {
			if (!strcasecmp(cur->interface, di->interface)) {
				ast_log(LOG_DEBUG, "Skipping dialing interface '%s' since it has already been dialed\n", 
					di->interface);
				break;
			}
		}
		AST_LIST_UNLOCK(dialed_interfaces);
		
		if (di) {
			free(tmp);
			continue;
		}

		/* It is always ok to dial a Local interface.  We only keep track of
		 * which "real" interfaces have been dialed.  The Local channel will
		 * inherit this list so that if it ends up dialing a real interface,
		 * it won't call one that has already been called. */
		if (strncasecmp(cur->interface, "Local/", 6)) {
			if (!(di = ast_calloc(1, sizeof(*di) + strlen(cur->interface)))) {
				ao2_iterator_destroy(&memi);
				ao2_ref(cur, -1);
				ao2_unlock(qe->parent);
				if (need_weight)
					AST_LIST_UNLOCK(&queues);
				free(tmp);
				goto out;
			}
			strcpy(di->interface, cur->interface);

			AST_LIST_LOCK(dialed_interfaces);
			AST_LIST_INSERT_TAIL(dialed_interfaces, di, list);
			AST_LIST_UNLOCK(dialed_interfaces);
		}

		tmp->stillgoing = -1;
		tmp->member = cur;
		tmp->oldstatus = cur->status;
		tmp->lastcall = cur->lastcall;
		ast_copy_string(tmp->interface, cur->interface, sizeof(tmp->interface));
		/* Special case: If we ring everyone, go ahead and ring them, otherwise
		   just calculate their metric for the appropriate strategy */
		if (!calc_metric(qe->parent, cur, x++, qe, tmp)) {
			/* Put them in the list of outgoing thingies...  We're ready now.
			   XXX If we're forcibly removed, these outgoing calls won't get
			   hung up XXX */
			tmp->q_next = outgoing;
			outgoing = tmp;		
			/* If this line is up, don't try anybody else */
			if (outgoing->chan && (outgoing->chan->_state == AST_STATE_UP))
				break;
		} else {
			ao2_ref(cur, -1);
			free(tmp);
		}
	}
	ao2_iterator_destroy(&memi);
	if (qe->expire && (!qe->parent->timeout || (qe->expire - now) <= qe->parent->timeout))
		to = (qe->expire - now) * 1000;
	else
		to = (qe->parent->timeout) ? qe->parent->timeout * 1000 : -1;
	++qe->pending;
	ao2_unlock(qe->parent);
	ring_one(qe, outgoing, &numbusies);
	if (need_weight)
		AST_LIST_UNLOCK(&queues);
	lpeer = wait_for_answer(qe, outgoing, &to, &digit, numbusies, ast_test_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT), forwardsallowed);
	ao2_lock(qe->parent);
	if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY || qe->parent->strategy == QUEUE_STRATEGY_RRORDERED) {
		store_next(qe, outgoing);
	}
	ao2_unlock(qe->parent);
	peer = lpeer ? lpeer->chan : NULL;
	if (!peer) {
		qe->pending = 0;
		if (to) {
			/* Must gotten hung up */
			res = -1;
		} else {
			/* User exited by pressing a digit */
			res = digit;
		}
		if (option_debug && res == -1)
			ast_log(LOG_DEBUG, "%s: Nobody answered.\n", qe->chan->name);
		if (ast_cdr_isset_unanswered()) {
			/* channel contains the name of one of the outgoing channels
			   in its CDR; zero out this CDR to avoid a dual-posting */
			struct callattempt *o;
			for (o = outgoing; o; o = o->q_next) {
				if (!o->chan) {
					continue;
				}
				if (strcmp(o->chan->cdr->dstchannel, qe->chan->cdr->dstchannel) == 0) {
					ast_set_flag(o->chan->cdr, AST_CDR_FLAG_POST_DISABLED);
					break;
				}
			}
		}
	} else { /* peer is valid */
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the
		   conversation.  */
		if (!strcmp(qe->chan->tech->type, "Zap"))
			ast_channel_setoption(qe->chan, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		if (!strcmp(peer->tech->type, "Zap"))
			ast_channel_setoption(peer, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		/* Update parameters for the queue */
		time(&now);
		recalc_holdtime(qe, (now - qe->start));
		ao2_lock(qe->parent);
		callcompletedinsl = ((now - qe->start) <= qe->parent->servicelevel);
		ao2_unlock(qe->parent);
		member = lpeer->member;
		/* Increment the refcount for this member, since we're going to be using it for awhile in here. */
		ao2_ref(member, 1);
		hangupcalls(outgoing, peer);
		outgoing = NULL;
		if (announce || qe->parent->reportholdtime || qe->parent->memberdelay) {
			int res2;

			res2 = ast_autoservice_start(qe->chan);
			if (!res2) {
				if (qe->parent->memberdelay) {
					ast_log(LOG_NOTICE, "Delaying member connect for %d seconds\n", qe->parent->memberdelay);
					res2 |= ast_safe_sleep(peer, qe->parent->memberdelay * 1000);
				}
				if (!res2 && announce) {
					play_file(peer, announce);
				}
				if (!res2 && qe->parent->reportholdtime) {
					if (!play_file(peer, qe->parent->sound_reporthold)) {
						int holdtime;

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
			if (peer->_softhangup) {
				/* Agent must have hung up */
				ast_log(LOG_WARNING, "Agent on %s hungup on the customer.\n", peer->name);
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "AGENTDUMP", "%s", "");
				if (qe->parent->eventwhencalled)
					manager_event(EVENT_FLAG_AGENT, "AgentDump",
							"Queue: %s\r\n"
							"Uniqueid: %s\r\n"
							"Channel: %s\r\n"
							"Member: %s\r\n"
							"MemberName: %s\r\n"
							"%s",
							queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername,
							qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
				ast_hangup(peer);
				ao2_ref(member, -1);
				goto out;
			} else if (res2) {
				/* Caller must have hung up just before being connected*/
				ast_log(LOG_NOTICE, "Caller was about to talk to agent on %s but the caller hungup.\n", peer->name);
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "ABANDON", "%d|%d|%ld", qe->pos, qe->opos, (long)time(NULL) - qe->start);
				record_abandoned(qe);
				ast_hangup(peer);
				ao2_ref(member, -1);
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
			ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "SYSCOMPAT", "%s", "");
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", qe->chan->name, peer->name);
			record_abandoned(qe);
			ast_cdr_failed(qe->chan->cdr);
			ast_hangup(peer);
			ao2_ref(member, -1);
			return -1;
		}

		if (qe->parent->setinterfacevar)
				pbx_builtin_setvar_helper(qe->chan, "MEMBERINTERFACE", member->interface);

		/* Begin Monitoring */
		if (qe->parent->monfmt && *qe->parent->monfmt) {
			if (!qe->parent->montype) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Starting Monitor as requested.\n");
				monitorfilename = pbx_builtin_getvar_helper(qe->chan, "MONITOR_FILENAME");
				if (pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC") || pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC_ARGS"))
					which = qe->chan;
				else
					which = peer;
				if (monitorfilename)
					ast_monitor_start(which, qe->parent->monfmt, monitorfilename, 1 );
				else if (qe->chan->cdr)
					ast_monitor_start(which, qe->parent->monfmt, qe->chan->cdr->uniqueid, 1 );
				else {
					/* Last ditch effort -- no CDR, make up something */
					snprintf(tmpid, sizeof(tmpid), "chan-%lx", ast_random());
					ast_monitor_start(which, qe->parent->monfmt, tmpid, 1 );
				}
				if (qe->parent->monjoin)
					ast_monitor_setjoinfiles(which, 1);
			} else {
				if (option_debug)
					ast_log(LOG_DEBUG, "Starting MixMonitor as requested.\n");
				monitorfilename = pbx_builtin_getvar_helper(qe->chan, "MONITOR_FILENAME");
				if (!monitorfilename) {
					if (qe->chan->cdr)
						ast_copy_string(tmpid, qe->chan->cdr->uniqueid, sizeof(tmpid)-1);
					else
						snprintf(tmpid, sizeof(tmpid), "chan-%lx", ast_random());
				} else {
					ast_copy_string(tmpid2, monitorfilename, sizeof(tmpid2)-1);
					for (p = tmpid2; *p ; p++) {
						if (*p == '^' && *(p+1) == '{') {
							*p = '$';
						}
					}

					memset(tmpid, 0, sizeof(tmpid));
					pbx_substitute_variables_helper(qe->chan, tmpid2, tmpid, sizeof(tmpid) - 1);
				}

				monitor_exec = pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC");
				monitor_options = pbx_builtin_getvar_helper(qe->chan, "MONITOR_OPTIONS");

				if (monitor_exec) {
					ast_copy_string(meid2, monitor_exec, sizeof(meid2)-1);
					for (p = meid2; *p ; p++) {
						if (*p == '^' && *(p+1) == '{') {
							*p = '$';
						}
					}

					memset(meid, 0, sizeof(meid));
					pbx_substitute_variables_helper(qe->chan, meid2, meid, sizeof(meid) - 1);
				}
	
				snprintf(tmpid2, sizeof(tmpid2)-1, "%s.%s", tmpid, qe->parent->monfmt);

				mixmonapp = pbx_findapp("MixMonitor");

				if (strchr(tmpid2, '|')) {
					ast_log(LOG_WARNING, "monitor-format (in queues.conf) and MONITOR_FILENAME cannot contain a '|'! Not recording.\n");
					mixmonapp = NULL;
				}

				if (!monitor_options)
					monitor_options = "";
				
				if (strchr(monitor_options, '|')) {
					ast_log(LOG_WARNING, "MONITOR_OPTIONS cannot contain a '|'! Not recording.\n");
					mixmonapp = NULL;
				}

				if (mixmonapp) {
					if (!ast_strlen_zero(monitor_exec))
						snprintf(mixmonargs, sizeof(mixmonargs)-1, "%s|b%s|%s", tmpid2, monitor_options, monitor_exec);
					else
						snprintf(mixmonargs, sizeof(mixmonargs)-1, "%s|b%s", tmpid2, monitor_options);
						
					if (option_debug)
						ast_log(LOG_DEBUG, "Arguments being passed to MixMonitor: %s\n", mixmonargs);
					/* We purposely lock the CDR so that pbx_exec does not update the application data */
					if (qe->chan->cdr)
						ast_set_flag(qe->chan->cdr, AST_CDR_FLAG_LOCKED);
					ret = pbx_exec(qe->chan, mixmonapp, mixmonargs);
					if (qe->chan->cdr)
						ast_clear_flag(qe->chan->cdr, AST_CDR_FLAG_LOCKED);

				} else
					ast_log(LOG_WARNING, "Asked to run MixMonitor on this call, but cannot find the MixMonitor app!\n");

			}
		}
		/* Drop out of the queue at this point, to prepare for next caller */
		leave_queue(qe);			
		if (!ast_strlen_zero(url) && ast_channel_supports_html(peer)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "app_queue: sendurl=%s.\n", url);
			ast_channel_sendurl(peer, url);
		}
		if (!ast_strlen_zero(agi)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "app_queue: agi=%s.\n", agi);
			app = pbx_findapp("agi");
			if (app) {
				agiexec = ast_strdupa(agi);
				ret = pbx_exec(qe->chan, app, agiexec);
			} else
				ast_log(LOG_WARNING, "Asked to execute an AGI on this channel, but could not find application (agi)!\n");
		}
		qe->handled++;
		ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "CONNECT", "%ld|%s", (long)time(NULL) - qe->start, peer->uniqueid);
		if (qe->parent->eventwhencalled)
			manager_event(EVENT_FLAG_AGENT, "AgentConnect",
					"Queue: %s\r\n"
					"Uniqueid: %s\r\n"
					"Channel: %s\r\n"
					"Member: %s\r\n"
					"MemberName: %s\r\n"
					"Holdtime: %ld\r\n"
					"BridgedChannel: %s\r\n"
					"%s",
					queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername,
					(long)time(NULL) - qe->start, peer->uniqueid,
					qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
		ast_copy_string(oldcontext, qe->chan->context, sizeof(oldcontext));
		ast_copy_string(oldexten, qe->chan->exten, sizeof(oldexten));
		time(&callstart);

		if (member->status == AST_DEVICE_NOT_INUSE)
			ast_log(LOG_WARNING, "The device state of this queue member, %s, is still 'Not in Use' when it probably should not be! Please check UPGRADE.txt for correct configuration settings.\n", member->membername);
			
		transfer_ds = setup_transfer_datastore(qe, member, callstart, callcompletedinsl);
		bridge = ast_bridge_call(qe->chan,peer, &bridge_config);

		ast_channel_lock(qe->chan);
		if (!attended_transfer_occurred(qe->chan)) {
			struct ast_datastore *tds;

			/* detect a blind transfer */
			if (!(qe->chan->_softhangup | peer->_softhangup) && (strcasecmp(oldcontext, qe->chan->context) || strcasecmp(oldexten, qe->chan->exten))) {
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "TRANSFER", "%s|%s|%ld|%ld",
					qe->chan->exten, qe->chan->context, (long) (callstart - qe->start),
					(long) (time(NULL) - callstart));
				if (qe->parent->eventwhencalled)
					manager_event(EVENT_FLAG_AGENT, "AgentComplete",
							"Queue: %s\r\n"
							"Uniqueid: %s\r\n"
							"Channel: %s\r\n"
							"Member: %s\r\n"
							"MemberName: %s\r\n"
							"HoldTime: %ld\r\n"
							"TalkTime: %ld\r\n"
							"Reason: transfer\r\n"
							"%s",
							queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername,
							(long)(callstart - qe->start), (long)(time(NULL) - callstart),
							qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
			} else if (qe->chan->_softhangup) {
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "COMPLETECALLER", "%ld|%ld|%d",
					(long) (callstart - qe->start), (long) (time(NULL) - callstart), qe->opos);
				if (qe->parent->eventwhencalled)
					manager_event(EVENT_FLAG_AGENT, "AgentComplete",
							"Queue: %s\r\n"
							"Uniqueid: %s\r\n"
							"Channel: %s\r\n"
							"Member: %s\r\n"
							"MemberName: %s\r\n"
							"HoldTime: %ld\r\n"
							"TalkTime: %ld\r\n"
							"Reason: caller\r\n"
							"%s",
							queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername,
							(long)(callstart - qe->start), (long)(time(NULL) - callstart),
							qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
			} else {
				ast_queue_log(queuename, qe->chan->uniqueid, member->membername, "COMPLETEAGENT", "%ld|%ld|%d",
					(long) (callstart - qe->start), (long) (time(NULL) - callstart), qe->opos);
				if (qe->parent->eventwhencalled)
					manager_event(EVENT_FLAG_AGENT, "AgentComplete",
							"Queue: %s\r\n"
							"Uniqueid: %s\r\n"
							"Channel: %s\r\n"
							"Member: %s\r\n"
							"MemberName: %s\r\n"
							"HoldTime: %ld\r\n"
							"TalkTime: %ld\r\n"
							"Reason: agent\r\n"
							"%s",
							queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername, (long)(callstart - qe->start),
							(long)(time(NULL) - callstart),
							qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
			}
			if ((tds = ast_channel_datastore_find(qe->chan, &queue_transfer_info, NULL))) {	
				ast_channel_datastore_remove(qe->chan, tds);
			}
			update_queue(qe->parent, member, callcompletedinsl);
		} else {
			if (qe->parent->eventwhencalled)
				manager_event(EVENT_FLAG_AGENT, "AgentComplete",
						"Queue: %s\r\n"
						"Uniqueid: %s\r\n"
						"Channel: %s\r\n"
						"Member: %s\r\n"
						"MemberName: %s\r\n"
						"HoldTime: %ld\r\n"
						"TalkTime: %ld\r\n"
						"Reason: transfer\r\n"
						"%s",
						queuename, qe->chan->uniqueid, peer->name, member->interface, member->membername, (long)(callstart - qe->start),
						(long)(time(NULL) - callstart),
						qe->parent->eventwhencalled == QUEUE_EVENT_VARIABLES ? vars2manager(qe->chan, vars, sizeof(vars)) : "");
		}

		if (transfer_ds) {
			ast_channel_datastore_free(transfer_ds);
		}
		ast_channel_unlock(qe->chan);
		ast_hangup(peer);
		res = bridge ? bridge : 1;
		ao2_ref(member, -1);
	}
out:
	hangupcalls(outgoing, NULL);

	return res;
}

static int wait_a_bit(struct queue_ent *qe)
{
	/* Don't need to hold the lock while we setup the outgoing calls */
	int retrywait = qe->parent->retry * 1000;

	int res = ast_waitfordigit(qe->chan, retrywait);
	if (res > 0 && !valid_exit(qe, res))
		res = 0;

	return res;
}

static struct member *interface_exists(struct call_queue *q, const char *interface)
{
	struct member *mem;
	struct ao2_iterator mem_iter;

	if (!q)
		return NULL;

	mem_iter = ao2_iterator_init(q->members, 0);
	while ((mem = ao2_iterator_next(&mem_iter))) {
		if (!strcasecmp(interface, mem->interface)) {
			ao2_iterator_destroy(&mem_iter);
			return mem;
		}
		ao2_ref(mem, -1);
	}
	ao2_iterator_destroy(&mem_iter);

	return NULL;
}


/* Dump all members in a specific queue to the database
 *
 * <pm_family>/<queuename> = <interface>;<penalty>;<paused>;<state_interface>[|...]
 *
 */
static void dump_queue_members(struct call_queue *pm_queue)
{
	struct member *cur_member;
	char value[PM_MAX_LEN];
	int value_len = 0;
	int res;
	struct ao2_iterator mem_iter;

	memset(value, 0, sizeof(value));

	if (!pm_queue)
		return;

	mem_iter = ao2_iterator_init(pm_queue->members, 0);
	while ((cur_member = ao2_iterator_next(&mem_iter))) {
		if (!cur_member->dynamic) {
			ao2_ref(cur_member, -1);
			continue;
		}

		res = snprintf(value + value_len, sizeof(value) - value_len, "%s%s;%d;%d;%s;%s",
			value_len ? "|" : "", cur_member->interface, cur_member->penalty, cur_member->paused, cur_member->membername, cur_member->state_interface);

		ao2_ref(cur_member, -1);

		if (res != strlen(value + value_len)) {
			ast_log(LOG_WARNING, "Could not create persistent member string, out of space\n");
			break;
		}
		value_len += res;
	}
	ao2_iterator_destroy(&mem_iter);
	
	if (value_len && !cur_member) {
		if (ast_db_put(pm_family, pm_queue->name, value))
			ast_log(LOG_WARNING, "failed to create persistent dynamic entry!\n");
	} else
		/* Delete the entry if the queue is empty or there is an error */
		ast_db_del(pm_family, pm_queue->name);
}

static int remove_from_queue(const char *queuename, const char *interface)
{
	struct call_queue *q;
	struct member *mem, tmpmem;
	int res = RES_NOSUCHQUEUE;

	ast_copy_string(tmpmem.interface, interface, sizeof(tmpmem.interface));

	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		ao2_lock(q);
		if (strcmp(q->name, queuename)) {
			ao2_unlock(q);
			continue;
		}

		if ((mem = ao2_find(q->members, &tmpmem, OBJ_POINTER))) {
			/* XXX future changes should beware of this assumption!! */
			if (!mem->dynamic) {
				res = RES_NOT_DYNAMIC;
				ao2_ref(mem, -1);
				ao2_unlock(q);
				break;
			}
			q->membercount--;
			manager_event(EVENT_FLAG_AGENT, "QueueMemberRemoved",
				"Queue: %s\r\n"
				"Location: %s\r\n"
				"MemberName: %s\r\n",
				q->name, mem->interface, mem->membername);
			ao2_unlink(q->members, mem);
			remove_from_interfaces(mem->state_interface);
			ao2_ref(mem, -1);

			if (queue_persistent_members)
				dump_queue_members(q);
			
			res = RES_OKAY;
		} else {
			res = RES_EXISTS;
		}
		ao2_unlock(q);
		break;
	}

	AST_LIST_UNLOCK(&queues);

	return res;
}


static int add_to_queue(const char *queuename, const char *interface, const char *membername, int penalty, int paused, int dump, const char *state_interface)
{
	struct call_queue *q;
	struct member *new_member, *old_member;
	int res = RES_NOSUCHQUEUE;

	/* \note Ensure the appropriate realtime queue is loaded.  Note that this
	 * short-circuits if the queue is already in memory. */
	if (!(q = load_realtime_queue(queuename)))
		return res;

	AST_LIST_LOCK(&queues);

	ao2_lock(q);
	if ((old_member = interface_exists(q, interface)) == NULL) {
		if ((new_member = create_queue_member(interface, membername, penalty, paused, state_interface))) {
			add_to_interfaces(new_member->state_interface);
			new_member->dynamic = 1;
			ao2_link(q->members, new_member);
			q->membercount++;
			manager_event(EVENT_FLAG_AGENT, "QueueMemberAdded",
				"Queue: %s\r\n"
				"Location: %s\r\n"
				"MemberName: %s\r\n"
				"Membership: %s\r\n"
				"Penalty: %d\r\n"
				"CallsTaken: %d\r\n"
				"LastCall: %d\r\n"
				"Status: %d\r\n"
				"Paused: %d\r\n",
				q->name, new_member->interface, new_member->membername,
				"dynamic",
				new_member->penalty, new_member->calls, (int) new_member->lastcall,
				new_member->status, new_member->paused);
			
			ao2_ref(new_member, -1);
			new_member = NULL;

			if (dump)
				dump_queue_members(q);
			
			res = RES_OKAY;
		} else {
			res = RES_OUTOFMEMORY;
		}
	} else {
		ao2_ref(old_member, -1);
		res = RES_EXISTS;
	}
	ao2_unlock(q);
	AST_LIST_UNLOCK(&queues);

	return res;
}

static int set_member_paused(const char *queuename, const char *interface, int paused)
{
	int found = 0;
	struct call_queue *q;
	struct member *mem;

	/* Special event for when all queues are paused - individual events still generated */
	/* XXX In all other cases, we use the membername, but since this affects all queues, we cannot */
	if (ast_strlen_zero(queuename))
		ast_queue_log("NONE", "NONE", interface, (paused ? "PAUSEALL" : "UNPAUSEALL"), "%s", "");

	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		ao2_lock(q);
		if (ast_strlen_zero(queuename) || !strcasecmp(q->name, queuename)) {
			if ((mem = interface_exists(q, interface))) {
				found++;
				if (mem->paused == paused)
					ast_log(LOG_DEBUG, "%spausing already-%spaused queue member %s:%s\n", (paused ? "" : "un"), (paused ? "" : "un"), q->name, interface);
				mem->paused = paused;

				if (queue_persistent_members)
					dump_queue_members(q);

				if (mem->realtime)
					update_realtime_member_field(mem, q->name, "paused", paused ? "1" : "0");

				ast_queue_log(q->name, "NONE", mem->membername, (paused ? "PAUSE" : "UNPAUSE"), "%s", "");

				manager_event(EVENT_FLAG_AGENT, "QueueMemberPaused",
					"Queue: %s\r\n"
					"Location: %s\r\n"
					"MemberName: %s\r\n"
					"Paused: %d\r\n",
						q->name, mem->interface, mem->membername, paused);
				ao2_ref(mem, -1);
			}
		}
		ao2_unlock(q);
	}
	AST_LIST_UNLOCK(&queues);

	return found ? RESULT_SUCCESS : RESULT_FAILURE;
}

/* Reload dynamic queue members persisted into the astdb */
static void reload_queue_members(void)
{
	char *cur_ptr;
	char *queue_name;
	char *member;
	char *interface;
	char *membername = NULL;
	char *state_interface;
	char *penalty_tok;
	int penalty = 0;
	char *paused_tok;
	int paused = 0;
	struct ast_db_entry *db_tree;
	struct ast_db_entry *entry;
	struct call_queue *cur_queue;
	char queue_data[PM_MAX_LEN];

	AST_LIST_LOCK(&queues);

	/* Each key in 'pm_family' is the name of a queue */
	db_tree = ast_db_gettree(pm_family, NULL);
	for (entry = db_tree; entry; entry = entry->next) {

		queue_name = entry->key + strlen(pm_family) + 2;

		AST_LIST_TRAVERSE(&queues, cur_queue, list) {
			ao2_lock(cur_queue);
			if (!strcmp(queue_name, cur_queue->name))
				break;
			ao2_unlock(cur_queue);
		}
		
		if (!cur_queue)
			cur_queue = load_realtime_queue(queue_name);

		if (!cur_queue) {
			/* If the queue no longer exists, remove it from the
			 * database */
			ast_log(LOG_WARNING, "Error loading persistent queue: '%s': it does not exist\n", queue_name);
			ast_db_del(pm_family, queue_name);
			continue;
		} else
			ao2_unlock(cur_queue);

		if (ast_db_get(pm_family, queue_name, queue_data, PM_MAX_LEN))
			continue;

		cur_ptr = queue_data;
		while ((member = strsep(&cur_ptr, "|"))) {
			if (ast_strlen_zero(member))
				continue;

			interface = strsep(&member, ";");
			penalty_tok = strsep(&member, ";");
			paused_tok = strsep(&member, ";");
			membername = strsep(&member, ";");
			state_interface = strsep(&member,";");

			if (!penalty_tok) {
				ast_log(LOG_WARNING, "Error parsing persistent member string for '%s' (penalty)\n", queue_name);
				break;
			}
			penalty = strtol(penalty_tok, NULL, 10);
			if (errno == ERANGE) {
				ast_log(LOG_WARNING, "Error converting penalty: %s: Out of range.\n", penalty_tok);
				break;
			}
			
			if (!paused_tok) {
				ast_log(LOG_WARNING, "Error parsing persistent member string for '%s' (paused)\n", queue_name);
				break;
			}
			paused = strtol(paused_tok, NULL, 10);
			if ((errno == ERANGE) || paused < 0 || paused > 1) {
				ast_log(LOG_WARNING, "Error converting paused: %s: Expected 0 or 1.\n", paused_tok);
				break;
			}
			if (ast_strlen_zero(membername))
				membername = interface;

			if (option_debug)
				ast_log(LOG_DEBUG, "Reload Members: Queue: %s  Member: %s  Name: %s  Penalty: %d  Paused: %d\n", queue_name, interface, membername, penalty, paused);
			
			if (add_to_queue(queue_name, interface, membername, penalty, paused, 0, state_interface) == RES_OUTOFMEMORY) {
				ast_log(LOG_ERROR, "Out of Memory when reloading persistent queue member\n");
				break;
			}
		}
	}

	AST_LIST_UNLOCK(&queues);
	if (db_tree) {
		ast_log(LOG_NOTICE, "Queue members successfully reloaded from database.\n");
		ast_db_freetree(db_tree);
	}
}

static int pqm_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *lu;
	char *parse;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "PauseQueueMember requires an argument ([queuename]|interface[|options])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	lu = ast_module_user_add(chan);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (ast_strlen_zero(args.interface)) {
		ast_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename]|interface[|options])\n");
		ast_module_user_remove(lu);
		return -1;
	}

	if (set_member_paused(args.queuename, args.interface, 1)) {
		ast_log(LOG_WARNING, "Attempt to pause interface %s, not found\n", args.interface);
		if (priority_jump || ast_opt_priority_jumping) {
			if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
				pbx_builtin_setvar_helper(chan, "PQMSTATUS", "NOTFOUND");
				ast_module_user_remove(lu);
				return 0;
			}
		}
		ast_module_user_remove(lu);
		pbx_builtin_setvar_helper(chan, "PQMSTATUS", "NOTFOUND");
		return 0;
	}

	ast_module_user_remove(lu);
	pbx_builtin_setvar_helper(chan, "PQMSTATUS", "PAUSED");

	return 0;
}

static int upqm_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *lu;
	char *parse;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "UnpauseQueueMember requires an argument ([queuename]|interface[|options])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	lu = ast_module_user_add(chan);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (ast_strlen_zero(args.interface)) {
		ast_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename]|interface[|options])\n");
		ast_module_user_remove(lu);
		return -1;
	}

	if (set_member_paused(args.queuename, args.interface, 0)) {
		ast_log(LOG_WARNING, "Attempt to unpause interface %s, not found\n", args.interface);
		if (priority_jump || ast_opt_priority_jumping) {
			if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
				pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "NOTFOUND");
				ast_module_user_remove(lu);
				return 0;
			}
		}
		ast_module_user_remove(lu);
		pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "NOTFOUND");
		return 0;
	}

	ast_module_user_remove(lu);
	pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "UNPAUSED");

	return 0;
}

static int rqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct ast_module_user *lu;
	char *parse, *temppos = NULL;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
	);


	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "RemoveQueueMember requires an argument (queuename[|interface[|options]])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	lu = ast_module_user_add(chan);

	if (ast_strlen_zero(args.interface)) {
		args.interface = ast_strdupa(chan->name);
		temppos = strrchr(args.interface, '-');
		if (temppos)
			*temppos = '\0';
	}

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	switch (remove_from_queue(args.queuename, args.interface)) {
	case RES_OKAY:
		ast_queue_log(args.queuename, chan->uniqueid, args.interface, "REMOVEMEMBER", "%s", "");
		ast_log(LOG_NOTICE, "Removed interface '%s' from queue '%s'\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "REMOVED");
		res = 0;
		break;
	case RES_EXISTS:
		ast_log(LOG_DEBUG, "Unable to remove interface '%s' from queue '%s': Not there\n", args.interface, args.queuename);
		if (priority_jump || ast_opt_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOTINQUEUE");
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to remove interface from queue '%s': No such queue\n", args.queuename);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOSUCHQUEUE");
		res = 0;
		break;
	case RES_NOT_DYNAMIC:
		ast_log(LOG_WARNING, "Unable to remove interface from queue '%s': '%s' is not a dynamic member\n", args.queuename, args.interface);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOTDYNAMIC");
		res = 0;
		break;
	}

	ast_module_user_remove(lu);

	return res;
}

static int aqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct ast_module_user *lu;
	char *parse, *temppos = NULL;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(penalty);
		AST_APP_ARG(options);
		AST_APP_ARG(membername);
		AST_APP_ARG(state_interface);
	);
	int penalty = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "AddQueueMember requires an argument (queuename[|interface[|penalty[|options[|membername[|state_interface]]]]])\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	lu = ast_module_user_add(chan);

	if (ast_strlen_zero(args.interface)) {
		args.interface = ast_strdupa(chan->name);
		temppos = strrchr(args.interface, '-');
		if (temppos)
			*temppos = '\0';
	}

	if (!ast_strlen_zero(args.penalty)) {
		if ((sscanf(args.penalty, "%30d", &penalty) != 1) || penalty < 0) {
			ast_log(LOG_WARNING, "Penalty '%s' is invalid, must be an integer >= 0\n", args.penalty);
			penalty = 0;
		}
	}
	
	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	switch (add_to_queue(args.queuename, args.interface, args.membername, penalty, 0, queue_persistent_members, args.state_interface)) {
	case RES_OKAY:
		ast_queue_log(args.queuename, chan->uniqueid, args.interface, "ADDMEMBER", "%s", "");
		ast_log(LOG_NOTICE, "Added interface '%s' to queue '%s'\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "ADDED");
		res = 0;
		break;
	case RES_EXISTS:
		ast_log(LOG_WARNING, "Unable to add interface '%s' to queue '%s': Already there\n", args.interface, args.queuename);
		if (priority_jump || ast_opt_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "MEMBERALREADY");
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to add interface to queue '%s': No such queue\n", args.queuename);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "NOSUCHQUEUE");
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		ast_log(LOG_ERROR, "Out of memory adding member %s to queue %s\n", args.interface, args.queuename);
		break;
	}

	ast_module_user_remove(lu);

	return res;
}

static int ql_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(uniqueid);
		AST_APP_ARG(membername);
		AST_APP_ARG(event);
		AST_APP_ARG(params);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "QueueLog requires arguments (queuename|uniqueid|membername|event[|additionalinfo]\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.queuename) || ast_strlen_zero(args.uniqueid)
	    || ast_strlen_zero(args.membername) || ast_strlen_zero(args.event)) {
		ast_log(LOG_WARNING, "QueueLog requires arguments (queuename|uniqueid|membername|event[|additionalinfo])\n");
		ast_module_user_remove(u);
		return -1;
	}

	ast_queue_log(args.queuename, args.uniqueid, args.membername, args.event, 
		"%s", args.params ? args.params : "");

	ast_module_user_remove(u);

	return 0;
}

/*!\brief The starting point for all queue calls
 *
 * The process involved here is to 
 * 1. Parse the options specified in the call to Queue()
 * 2. Join the queue
 * 3. Wait in a loop until it is our turn to try calling a queue member
 * 4. Attempt to call a queue member
 * 5. If 4. did not result in a bridged call, then check for between
 *    call options such as periodic announcements etc.
 * 6. Try 4 again uless some condition (such as an expiration time) causes us to 
 *    exit the queue.
 */
static int queue_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	int ringing=0;
	struct ast_module_user *lu;
	const char *user_priority;
	const char *max_penalty_str;
	int prio;
	int max_penalty;
	enum queue_result reason = QUEUE_UNKNOWN;
	/* whether to exit Queue application after the timeout hits */
	int tries = 0;
	int noption = 0;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(options);
		AST_APP_ARG(url);
		AST_APP_ARG(announceoverride);
		AST_APP_ARG(queuetimeoutstr);
		AST_APP_ARG(agi);
	);
	/* Our queue entry */
	struct queue_ent qe = { 0 };
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Queue requires an argument: queuename[|options[|URL[|announceoverride[|timeout[|agi]]]]]\n");
		return -1;
	}
	
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	lu = ast_module_user_add(chan);

	/* Setup our queue entry */
	qe.start = time(NULL);

	/* set the expire time based on the supplied timeout; */
	if (!ast_strlen_zero(args.queuetimeoutstr))
		qe.expire = qe.start + atoi(args.queuetimeoutstr);
	else
		qe.expire = 0;

	/* Get the priority from the variable ${QUEUE_PRIO} */
	user_priority = pbx_builtin_getvar_helper(chan, "QUEUE_PRIO");
	if (user_priority) {
		if (sscanf(user_priority, "%30d", &prio) == 1) {
			if (option_debug)
				ast_log(LOG_DEBUG, "%s: Got priority %d from ${QUEUE_PRIO}.\n",
					chan->name, prio);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_PRIO}: Invalid value (%s), channel %s.\n",
				user_priority, chan->name);
			prio = 0;
		}
	} else {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "NO QUEUE_PRIO variable found. Using default.\n");
		prio = 0;
	}

	/* Get the maximum penalty from the variable ${QUEUE_MAX_PENALTY} */
	if ((max_penalty_str = pbx_builtin_getvar_helper(chan, "QUEUE_MAX_PENALTY"))) {
		if (sscanf(max_penalty_str, "%30d", &max_penalty) == 1) {
			if (option_debug)
				ast_log(LOG_DEBUG, "%s: Got max penalty %d from ${QUEUE_MAX_PENALTY}.\n",
					chan->name, max_penalty);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_MAX_PENALTY}: Invalid value (%s), channel %s.\n",
				max_penalty_str, chan->name);
			max_penalty = 0;
		}
	} else {
		max_penalty = 0;
	}

	if (args.options && (strchr(args.options, 'r')))
		ringing = 1;

	if (option_debug)
		ast_log(LOG_DEBUG, "queue: %s, options: %s, url: %s, announce: %s, expires: %ld, priority: %d\n",
			args.queuename, args.options, args.url, args.announceoverride, (long)qe.expire, prio);

	qe.chan = chan;
	qe.prio = prio;
	qe.max_penalty = max_penalty;
	qe.last_pos_said = 0;
	qe.last_pos = 0;
	qe.last_periodic_announce_time = time(NULL);
	qe.last_periodic_announce_sound = 0;
	qe.valid_digits = 0;
	if (!join_queue(args.queuename, &qe, &reason)) {
		int makeannouncement = 0;

		ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ENTERQUEUE", "%s|%s", S_OR(args.url, ""),
			S_OR(chan->cid.cid_num, ""));
check_turns:
		if (ringing) {
			ast_indicate(chan, AST_CONTROL_RINGING);
		} else {
			ast_moh_start(chan, qe.moh, NULL);
		}

		/* This is the wait loop for callers 2 through maxlen */
		res = wait_our_turn(&qe, ringing, &reason);
		if (res)
			goto stop;

		for (;;) {
			/* This is the wait loop for the head caller*/
			/* To exit, they may get their call answered; */
			/* they may dial a digit from the queue context; */
			/* or, they may timeout. */

			enum queue_member_status stat;

			/* Leave if we have exceeded our queuetimeout */
			if (qe.expire && (time(NULL) >= qe.expire)) {
				record_abandoned(&qe);
				reason = QUEUE_TIMEOUT;
				res = 0;
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
				break;
			}

			if (makeannouncement) {
				/* Make a position announcement, if enabled */
				if (qe.parent->announcefrequency && !ringing)
					if ((res = say_position(&qe)))
						goto stop;

			}
			makeannouncement = 1;

			/* Leave if we have exceeded our queuetimeout */
			if (qe.expire && (time(NULL) >= qe.expire)) {
				record_abandoned(&qe);
				reason = QUEUE_TIMEOUT;
				res = 0;
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
				break;
			}
			/* Make a periodic announcement, if enabled */
			if (qe.parent->periodicannouncefrequency && !ringing)
				if ((res = say_periodic_announcement(&qe)))
					goto stop;

			/* Leave if we have exceeded our queuetimeout */
			if (qe.expire && (time(NULL) >= qe.expire)) {
				record_abandoned(&qe);
				reason = QUEUE_TIMEOUT;
				res = 0;
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
				break;
			}
			/* Try calling all queue members for 'timeout' seconds */
			res = try_calling(&qe, args.options, args.announceoverride, args.url, &tries, &noption, args.agi);
			if (res)
				goto stop;

			stat = get_member_status(qe.parent, qe.max_penalty);

			/* exit after 'timeout' cycle if 'n' option enabled */
			if (noption && tries >= qe.parent->membercount) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Exiting on time-out cycle\n");
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
				record_abandoned(&qe);
				reason = QUEUE_TIMEOUT;
				res = 0;
				break;
			}

			/* leave the queue if no agents, if enabled */
			if (qe.parent->leavewhenempty && (stat == QUEUE_NO_MEMBERS)) {
				record_abandoned(&qe);
				reason = QUEUE_LEAVEEMPTY;
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITEMPTY", "%d|%d|%ld", qe.pos, qe.opos, (long)(time(NULL) - qe.start));
				res = 0;
				break;
			}

			/* leave the queue if no reachable agents, if enabled */
			if ((qe.parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS)) {
				record_abandoned(&qe);
				reason = QUEUE_LEAVEUNAVAIL;
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITEMPTY", "%d|%d|%ld", qe.pos, qe.opos, (long)(time(NULL) - qe.start));
				res = 0;
				break;
			}

			/* Leave if we have exceeded our queuetimeout */
			if (qe.expire && (time(NULL) >= qe.expire)) {
				record_abandoned(&qe);
				reason = QUEUE_TIMEOUT;
				res = 0;
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
				break;
			}

			/* If using dynamic realtime members, we should regenerate the member list for this queue */
			update_realtime_members(qe.parent);

			/* OK, we didn't get anybody; wait for 'retry' seconds; may get a digit to exit with */
			res = wait_a_bit(&qe);
			if (res)
				goto stop;

			/* Since this is a priority queue and
			 * it is not sure that we are still at the head
			 * of the queue, go and check for our turn again.
			 */
			if (!is_our_turn(&qe)) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Darn priorities, going back in queue (%s)!\n",
						qe.chan->name);
				goto check_turns;
			}
		}

stop:
		if (res) {
			if (res < 0) {
				if (!qe.handled) {
					record_abandoned(&qe);
					ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ABANDON",
						"%d|%d|%ld", qe.pos, qe.opos,
						(long) time(NULL) - qe.start);
				}
				res = -1;
			} else if (qe.valid_digits) {
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHKEY",
					"%s|%d", qe.digits, qe.pos);
			}
		}

		/* Don't allow return code > 0 */
		if (res >= 0) {
			res = 0;	
			if (ringing) {
				ast_indicate(chan, -1);
			} else {
				ast_moh_stop(chan);
			}			
			ast_stopstream(chan);
		}
		leave_queue(&qe);
		if (reason != QUEUE_UNKNOWN)
			set_queue_result(chan, reason);
	} else {
		ast_log(LOG_WARNING, "Unable to join queue '%s'\n", args.queuename);
		set_queue_result(chan, reason);
		res = 0;
	}
	if (qe.parent) {
		/* every queue_ent is given a reference to it's parent call_queue when it joins the queue.
		 * This ref must be taken away right before the queue_ent is destroyed.  In this case
		 * the queue_ent is about to be returned on the stack */
		ao2_ref(qe.parent, -1);
	}
	ast_module_user_remove(lu);

	return res;
}

static int queue_function_qac(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct call_queue *q;
	struct ast_module_user *lu;
	struct member *m;
	struct ao2_iterator mem_iter;

	buf[0] = '\0';
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}

	lu = ast_module_user_add(chan);

	if ((q = load_realtime_queue(data))) {
		ao2_lock(q);
		mem_iter = ao2_iterator_init(q->members, 0);
		while ((m = ao2_iterator_next(&mem_iter))) {
			/* Count the agents who are logged in and presently answering calls */
			if ((m->status != AST_DEVICE_UNAVAILABLE) && (m->status != AST_DEVICE_INVALID)) {
				count++;
			}
			ao2_ref(m, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ao2_unlock(q);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	snprintf(buf, len, "%d", count);
	ast_module_user_remove(lu);

	return 0;
}

static int queue_function_queuewaitingcount(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct call_queue *q;
	struct ast_module_user *lu;
	struct ast_variable *var = NULL;

	buf[0] = '\0';
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}

	lu = ast_module_user_add(chan);
	
	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, data)) {
			ao2_lock(q);
			break;
		}
	}
	AST_LIST_UNLOCK(&queues);

	if (q) {
		count = q->count;
		ao2_unlock(q);
	} else if ((var = ast_load_realtime("queues", "name", data, NULL))) {
		/* if the queue is realtime but was not found in memory, this
		 * means that the queue had been deleted from memory since it was 
		 * "dead." This means it has a 0 waiting count
		 */
		count = 0;
		ast_variables_destroy(var);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	snprintf(buf, len, "%d", count);
	ast_module_user_remove(lu);
	return 0;
}

static int queue_function_queuememberlist(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct ast_module_user *u;
	struct call_queue *q;
	struct member *m;

	/* Ensure an otherwise empty list doesn't return garbage */
	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "QUEUE_MEMBER_LIST requires an argument: queuename\n");
		return -1;
	}
	
	u = ast_module_user_add(chan);

	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, data)) {
			ao2_lock(q);
			break;
		}
	}
	AST_LIST_UNLOCK(&queues);

	if (q) {
		int buflen = 0, count = 0;
		struct ao2_iterator mem_iter = ao2_iterator_init(q->members, 0);

		while ((m = ao2_iterator_next(&mem_iter))) {
			/* strcat() is always faster than printf() */
			if (count++) {
				strncat(buf + buflen, ",", len - buflen - 1);
				buflen++;
			}
			strncat(buf + buflen, m->interface, len - buflen - 1);
			buflen += strlen(m->interface);
			/* Safeguard against overflow (negative length) */
			if (buflen >= len - 2) {
				ao2_ref(m, -1);
				ast_log(LOG_WARNING, "Truncating list\n");
				break;
			}
			ao2_ref(m, -1);
		}
		ao2_iterator_destroy(&mem_iter);
		ao2_unlock(q);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	/* We should already be terminated, but let's make sure. */
	buf[len - 1] = '\0';
	ast_module_user_remove(u);

	return 0;
}

static struct ast_custom_function queueagentcount_function = {
	.name = "QUEUEAGENTCOUNT",
	.synopsis = "Count number of agents answering a queue",
	.syntax = "QUEUEAGENTCOUNT(<queuename>)",
	.desc =
"Returns the number of members currently associated with the specified queue.\n"
"This function is deprecated.  You should use QUEUE_MEMBER_COUNT() instead.\n",
	.read = queue_function_qac,
};

static struct ast_custom_function queuemembercount_function = {
	.name = "QUEUE_MEMBER_COUNT",
	.synopsis = "Count number of members answering a queue",
	.syntax = "QUEUE_MEMBER_COUNT(<queuename>)",
	.desc =
"Returns the number of members currently associated with the specified queue.\n",
	.read = queue_function_qac,
};

static struct ast_custom_function queuewaitingcount_function = {
	.name = "QUEUE_WAITING_COUNT",
	.synopsis = "Count number of calls currently waiting in a queue",
	.syntax = "QUEUE_WAITING_COUNT(<queuename>)",
	.desc =
"Returns the number of callers currently waiting in the specified queue.\n",
	.read = queue_function_queuewaitingcount,
};

static struct ast_custom_function queuememberlist_function = {
	.name = "QUEUE_MEMBER_LIST",
	.synopsis = "Returns a list of interfaces on a queue",
	.syntax = "QUEUE_MEMBER_LIST(<queuename>)",
	.desc =
"Returns a comma-separated list of members associated with the specified queue.\n",
	.read = queue_function_queuememberlist,
};

static int reload_queues(void)
{
	struct call_queue *q;
	struct ast_config *cfg;
	char *cat, *tmp;
	struct ast_variable *var;
	struct member *cur, *newm;
	struct ao2_iterator mem_iter;
	int new;
	const char *general_val = NULL;
	char *parse;
	char *interface, *state_interface;
	char *membername = NULL;
	int penalty;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(interface);
		AST_APP_ARG(penalty);
		AST_APP_ARG(membername);
		AST_APP_ARG(state_interface);
	);
	
	if (!(cfg = ast_config_load("queues.conf"))) {
		ast_log(LOG_NOTICE, "No call queueing config file (queues.conf), so no call queues\n");
		return 0;
	}
	AST_LIST_LOCK(&queues);
	use_weight=0;
	/* Mark all non-realtime queues as dead for the moment */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!q->realtime) {
			q->dead = 1;
			q->found = 0;
		}
	}

	/* Chug through config file */
	cat = NULL;
	while ((cat = ast_category_browse(cfg, cat)) ) {
		if (!strcasecmp(cat, "general")) {	
			/* Initialize global settings */
			queue_persistent_members = 0;
			if ((general_val = ast_variable_retrieve(cfg, "general", "persistentmembers")))
				queue_persistent_members = ast_true(general_val);
			autofill_default = 0;
			if ((general_val = ast_variable_retrieve(cfg, "general", "autofill")))
				autofill_default = ast_true(general_val);
			montype_default = 0;
			if ((general_val = ast_variable_retrieve(cfg, "general", "monitor-type")))
				if (!strcasecmp(general_val, "mixmonitor"))
					montype_default = 1;
		} else {	/* Define queue */
			/* Look for an existing one */
			AST_LIST_TRAVERSE(&queues, q, list) {
				if (!strcmp(q->name, cat))
					break;
			}
			if (!q) {
				/* Make one then */
				if (!(q = alloc_queue(cat))) {
					/* TODO: Handle memory allocation failure */
				}
				new = 1;
			} else
				new = 0;
			if (q) {
				const char *tmpvar;
				if (!new)
					ao2_lock(q);
				/* Check if a queue with this name already exists */
				if (q->found) {
					ast_log(LOG_WARNING, "Queue '%s' already defined! Skipping!\n", cat);
					if (!new)
						ao2_unlock(q);
					continue;
				}

				/* Due to the fact that the "rrordered" strategy will have a different allocation
				 * scheme for queue members, we must devise the queue's strategy before other initializations.
				 * To be specific, the rrordered strategy needs to function like a linked list, meaning the ao2
				 * container used will have only a single bucket instead of the typical number.
				 */
				if ((tmpvar = ast_variable_retrieve(cfg, cat, "strategy"))) {
					q->strategy = strat2int(tmpvar);
					if (q->strategy < 0) {
						ast_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n", tmpvar, q->name);
						q->strategy = QUEUE_STRATEGY_RINGALL;
					}
				} else {
					q->strategy = QUEUE_STRATEGY_RINGALL;
				}

				/* Re-initialize the queue, and clear statistics */
				init_queue(q);
				clear_queue(q);
				mem_iter = ao2_iterator_init(q->members, 0);
				while ((cur = ao2_iterator_next(&mem_iter))) {
					if (!cur->dynamic) {
						cur->delme = 1;
					}
					ao2_ref(cur, -1);
				}
				ao2_iterator_destroy(&mem_iter);
				for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
					if (!strcasecmp(var->name, "member")) {
						struct member tmpmem;
						membername = NULL;

						if (ast_strlen_zero(var->value)) {
							ast_log(LOG_WARNING, "Empty queue member definition at line %d. Moving on!\n", var->lineno);
							continue;
						}

						/* Add a new member */
						if (!(parse = ast_strdup(var->value))) {
							continue;
						}
						
						AST_NONSTANDARD_APP_ARGS(args, parse, ',');

						interface = args.interface;
						if (!ast_strlen_zero(args.penalty)) {
							tmp = ast_skip_blanks(args.penalty);
							penalty = atoi(tmp);
							if (penalty < 0) {
								penalty = 0;
							}
						} else
							penalty = 0;

						if (!ast_strlen_zero(args.membername)) {
							membername = ast_skip_blanks(args.membername);
						}

						if (!ast_strlen_zero(args.state_interface)) {
							state_interface = ast_skip_blanks(args.state_interface);
						} else {
							state_interface = interface;
						}

						/* Find the old position in the list */
						ast_copy_string(tmpmem.interface, interface, sizeof(tmpmem.interface));
						cur = ao2_find(q->members, &tmpmem, OBJ_POINTER | OBJ_UNLINK);

						/* Only attempt removing from interfaces list if the new state_interface is different than the old one */
						if (cur && strcasecmp(cur->state_interface, state_interface)) {
							remove_from_interfaces(cur->state_interface);
						}

						newm = create_queue_member(interface, membername, penalty, cur ? cur->paused : 0, state_interface);
						if (!cur || (cur && strcasecmp(cur->state_interface, state_interface))) {
							add_to_interfaces(state_interface);
						}
						ao2_link(q->members, newm);
						ao2_ref(newm, -1);
						newm = NULL;

						if (cur)
							ao2_ref(cur, -1);
						else {
							q->membercount++;
						}
						ast_free(parse);
					} else {
						queue_set_param(q, var->name, var->value, var->lineno, 1);
					}
				}

				/* Free remaining members marked as delme */
				mem_iter = ao2_iterator_init(q->members, 0);
				while ((cur = ao2_iterator_next(&mem_iter))) {
					if (! cur->delme) {
						ao2_ref(cur, -1);
						continue;
					}

					q->membercount--;
					ao2_unlink(q->members, cur);
					remove_from_interfaces(cur->state_interface);
					ao2_ref(cur, -1);
				}
				ao2_iterator_destroy(&mem_iter);

				if (q->strategy == QUEUE_STRATEGY_ROUNDROBIN)
					rr_dep_warning();

				if (new) {
					AST_LIST_INSERT_HEAD(&queues, q, list);
				} else
					ao2_unlock(q);
			}
		}
	}
	ast_config_destroy(cfg);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&queues, q, list) {
		if (q->dead) {
			AST_LIST_REMOVE_CURRENT(&queues, list);
			ao2_ref(q, -1);
		} else {
			ao2_lock(q);
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((cur = ao2_iterator_next(&mem_iter))) {
				if (cur->dynamic)
					q->membercount++;
				cur->status = ast_device_state(cur->state_interface);
				ao2_ref(cur, -1);
			}
			ao2_iterator_destroy(&mem_iter);
			ao2_unlock(q);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&queues);
	return 1;
}

static int __queues_show(struct mansession *s, int manager, int fd, int argc, char **argv)
{
	struct call_queue *q;
	struct queue_ent *qe;
	struct member *mem;
	int pos, queue_show;
	time_t now;
	char max_buf[150];
	char *max;
	size_t max_left;
	float sl = 0;
	char *term = manager ? "\r\n" : "\n";
	struct ao2_iterator mem_iter;

	time(&now);
	if (argc == 2)
		queue_show = 0;
	else if (argc == 3)
		queue_show = 1;
	else
		return RESULT_SHOWUSAGE;

	/* We only want to load realtime queues when a specific queue is asked for. */
	if (queue_show) {
		load_realtime_queue(argv[2]);
	} else if (ast_check_realtime("queues")) {
		struct ast_config *cfg = ast_load_realtime_multientry("queues", "name LIKE", "%", (char *) NULL);
		char *queuename;
		if (cfg) {
			for (queuename = ast_category_browse(cfg, NULL); !ast_strlen_zero(queuename); queuename = ast_category_browse(cfg, queuename)) {
				load_realtime_queue(queuename);
			}
			ast_config_destroy(cfg);
		}
	}

	AST_LIST_LOCK(&queues);
	if (AST_LIST_EMPTY(&queues)) {
		AST_LIST_UNLOCK(&queues);
		if (queue_show) {
			if (s)
				astman_append(s, "No such queue: %s.%s",argv[2], term);
			else
				ast_cli(fd, "No such queue: %s.%s",argv[2], term);
		} else {
			if (s)
				astman_append(s, "No queues.%s", term);
			else
				ast_cli(fd, "No queues.%s", term);
		}
		return RESULT_SUCCESS;
	}
	AST_LIST_TRAVERSE(&queues, q, list) {
		ao2_lock(q);
		if (queue_show) {
			if (strcasecmp(q->name, argv[2]) != 0) {
				ao2_unlock(q);
				if (!AST_LIST_NEXT(q, list)) {
					ast_cli(fd, "No such queue: %s.%s",argv[2], term);
					break;
				}
				continue;
			}
		}
		max_buf[0] = '\0';
		max = max_buf;
		max_left = sizeof(max_buf);
		if (q->maxlen)
			ast_build_string(&max, &max_left, "%d", q->maxlen);
		else
			ast_build_string(&max, &max_left, "unlimited");
		sl = 0;
		if (q->callscompleted > 0)
			sl = 100 * ((float) q->callscompletedinsl / (float) q->callscompleted);
		if (s)
			astman_append(s, "%-12.12s has %d calls (max %s) in '%s' strategy (%ds holdtime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds%s",
				q->name, q->count, max_buf, int2strat(q->strategy), q->holdtime, q->weight,
				q->callscompleted, q->callsabandoned,sl,q->servicelevel, term);
		else
			ast_cli(fd, "%-12.12s has %d calls (max %s) in '%s' strategy (%ds holdtime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds%s",
				q->name, q->count, max_buf, int2strat(q->strategy), q->holdtime, q->weight, q->callscompleted, q->callsabandoned,sl,q->servicelevel, term);
		if (ao2_container_count(q->members)) {
			if (s)
				astman_append(s, "   Members: %s", term);
			else
				ast_cli(fd, "   Members: %s", term);
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((mem = ao2_iterator_next(&mem_iter))) {
				max_buf[0] = '\0';
				max = max_buf;
				max_left = sizeof(max_buf);
				if (strcasecmp(mem->membername, mem->interface)) {
					ast_build_string(&max, &max_left, " (%s)", mem->interface);
				}
				if (mem->penalty)
					ast_build_string(&max, &max_left, " with penalty %d", mem->penalty);
				if (mem->dynamic)
					ast_build_string(&max, &max_left, " (dynamic)");
				if (mem->realtime)
					ast_build_string(&max, &max_left, " (realtime)");
				if (mem->paused)
					ast_build_string(&max, &max_left, " (paused)");
				ast_build_string(&max, &max_left, " (%s)", devstate2str(mem->status));
				if (mem->calls) {
					ast_build_string(&max, &max_left, " has taken %d calls (last was %ld secs ago)",
						mem->calls, (long) (time(NULL) - mem->lastcall));
				} else
					ast_build_string(&max, &max_left, " has taken no calls yet");
				if (s)
					astman_append(s, "      %s%s%s", mem->membername, max_buf, term);
				else
					ast_cli(fd, "      %s%s%s", mem->membername, max_buf, term);
				ao2_ref(mem, -1);
			}
			ao2_iterator_destroy(&mem_iter);
		} else if (s)
			astman_append(s, "   No Members%s", term);
		else	
			ast_cli(fd, "   No Members%s", term);
		if (q->head) {
			pos = 1;
			if (s)
				astman_append(s, "   Callers: %s", term);
			else
				ast_cli(fd, "   Callers: %s", term);
			for (qe = q->head; qe; qe = qe->next) {
				if (s)
					astman_append(s, "      %d. %s (wait: %ld:%2.2ld, prio: %d)%s",
						pos++, qe->chan->name, (long) (now - qe->start) / 60,
						(long) (now - qe->start) % 60, qe->prio, term);
				else
					ast_cli(fd, "      %d. %s (wait: %ld:%2.2ld, prio: %d)%s", pos++,
						qe->chan->name, (long) (now - qe->start) / 60,
						(long) (now - qe->start) % 60, qe->prio, term);
			}
		} else if (s)
			astman_append(s, "   No Callers%s", term);
		else
			ast_cli(fd, "   No Callers%s", term);
		if (s)
			astman_append(s, "%s", term);
		else
			ast_cli(fd, "%s", term);
		ao2_unlock(q);
		if (queue_show)
			break;
	}
	AST_LIST_UNLOCK(&queues);
	return RESULT_SUCCESS;
}

static int queue_show(int fd, int argc, char **argv)
{
	return __queues_show(NULL, 0, fd, argc, argv);
}

static char *complete_queue(const char *line, const char *word, int pos, int state)
{
	struct call_queue *q;
	char *ret = NULL;
	int which = 0;
	int wordlen = strlen(word);
	
	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strncasecmp(word, q->name, wordlen) && ++which > state) {
			ret = ast_strdup(q->name);	
			break;
		}
	}
	AST_LIST_UNLOCK(&queues);

	return ret;
}

static char *complete_queue_show(const char *line, const char *word, int pos, int state)
{
	if (pos == 2)
		return complete_queue(line, word, pos, state);
	return NULL;
}

/*!\brief callback to display queues status in manager
   \addtogroup Group_AMI
 */
static int manager_queues_show(struct mansession *s, const struct message *m)
{
	char *a[] = { "queue", "show" };

	__queues_show(s, 1, -1, 2, a);
	astman_append(s, "\r\n\r\n");	/* Properly terminate Manager output */

	return RESULT_SUCCESS;
}

/* Dump queue status */
static int manager_queues_status(struct mansession *s, const struct message *m)
{
	time_t now;
	int pos;
	const char *id = astman_get_header(m,"ActionID");
	const char *queuefilter = astman_get_header(m,"Queue");
	const char *memberfilter = astman_get_header(m,"Member");
	char idText[256] = "";
	struct call_queue *q;
	struct queue_ent *qe;
	float sl = 0;
	struct member *mem;
	struct ao2_iterator mem_iter;

	astman_send_ack(s, m, "Queue status will follow");
	time(&now);
	AST_LIST_LOCK(&queues);
	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	AST_LIST_TRAVERSE(&queues, q, list) {
		ao2_lock(q);

		/* List queue properties */
		if (ast_strlen_zero(queuefilter) || !strcmp(q->name, queuefilter)) {
			sl = ((q->callscompleted > 0) ? 100 * ((float)q->callscompletedinsl / (float)q->callscompleted) : 0);
			astman_append(s, "Event: QueueParams\r\n"
				"Queue: %s\r\n"
				"Max: %d\r\n"
				"Calls: %d\r\n"
				"Holdtime: %d\r\n"
				"Completed: %d\r\n"
				"Abandoned: %d\r\n"
				"ServiceLevel: %d\r\n"
				"ServicelevelPerf: %2.1f\r\n"
				"Weight: %d\r\n"
				"%s"
				"\r\n",
				q->name, q->maxlen, q->count, q->holdtime, q->callscompleted,
				q->callsabandoned, q->servicelevel, sl, q->weight, idText);
			/* List Queue Members */
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((mem = ao2_iterator_next(&mem_iter))) {
				if (ast_strlen_zero(memberfilter) || !strcmp(mem->interface, memberfilter)) {
					astman_append(s, "Event: QueueMember\r\n"
						"Queue: %s\r\n"
						"Name: %s\r\n"
						"Location: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %d\r\n"
						"Status: %d\r\n"
						"Paused: %d\r\n"
						"%s"
						"\r\n",
						q->name, mem->membername, mem->interface, mem->dynamic ? "dynamic" : "static",
						mem->penalty, mem->calls, (int)mem->lastcall, mem->status, mem->paused, idText);
				}
				ao2_ref(mem, -1);
			}
			ao2_iterator_destroy(&mem_iter);
			/* List Queue Entries */
			pos = 1;
			for (qe = q->head; qe; qe = qe->next) {
				astman_append(s, "Event: QueueEntry\r\n"
					"Queue: %s\r\n"
					"Position: %d\r\n"
					"Channel: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n"
					"Wait: %ld\r\n"
					"%s"
					"\r\n",
					q->name, pos++, qe->chan->name,
					S_OR(qe->chan->cid.cid_num, "unknown"),
					S_OR(qe->chan->cid.cid_name, "unknown"),
					(long) (now - qe->start), idText);
			}
		}
		ao2_unlock(q);
	}

	astman_append(s,
		"Event: QueueStatusComplete\r\n"
		"%s"
		"\r\n",idText);

	AST_LIST_UNLOCK(&queues);


	return RESULT_SUCCESS;
}

static int manager_add_queue_member(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface, *penalty_s, *paused_s, *membername, *state_interface;
	int paused, penalty = 0;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");
	penalty_s = astman_get_header(m, "Penalty");
	paused_s = astman_get_header(m, "Paused");
	membername = astman_get_header(m, "MemberName");
	state_interface = astman_get_header(m, "StateInterface");

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
	else if (sscanf(penalty_s, "%30d", &penalty) != 1 || penalty < 0)
		penalty = 0;

	if (ast_strlen_zero(paused_s))
		paused = 0;
	else
		paused = abs(ast_true(paused_s));

	switch (add_to_queue(queuename, interface, membername, penalty, paused, queue_persistent_members, state_interface)) {
	case RES_OKAY:
		ast_queue_log(queuename, "MANAGER", interface, "ADDMEMBER", "%s", "");
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

static int manager_remove_queue_member(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");

	if (ast_strlen_zero(queuename) || ast_strlen_zero(interface)) {
		astman_send_error(s, m, "Need 'Queue' and 'Interface' parameters.");
		return 0;
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		ast_queue_log(queuename, "MANAGER", interface, "REMOVEMEMBER", "%s", "");
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
	case RES_NOT_DYNAMIC:
		astman_send_error(s, m, "Member not dynamic");
		break;
	}

	return 0;
}

static int manager_pause_queue_member(struct mansession *s, const struct message *m)
{
	const char *queuename, *interface, *paused_s;
	int paused;

	interface = astman_get_header(m, "Interface");
	paused_s = astman_get_header(m, "Paused");
	queuename = astman_get_header(m, "Queue");	/* Optional - if not supplied, pause the given Interface in all queues */

	if (ast_strlen_zero(interface) || ast_strlen_zero(paused_s)) {
		astman_send_error(s, m, "Need 'Interface' and 'Paused' parameters.");
		return 0;
	}

	paused = abs(ast_true(paused_s));

	if (set_member_paused(queuename, interface, paused))
		astman_send_error(s, m, "Interface not found");
	else
		astman_send_ack(s, m, paused ? "Interface paused successfully" : "Interface unpaused successfully");
	return 0;
}

static int handle_queue_add_member(int fd, int argc, char *argv[])
{
	char *queuename, *interface, *membername = NULL, *state_interface = NULL;
	int penalty;

	if ((argc != 6) && (argc != 8) && (argc != 10) && (argc != 12)) {
		return RESULT_SHOWUSAGE;
	} else if (strcmp(argv[4], "to")) {
		return RESULT_SHOWUSAGE;
	} else if ((argc == 8) && strcmp(argv[6], "penalty")) {
		return RESULT_SHOWUSAGE;
	} else if ((argc == 10) && strcmp(argv[8], "as")) {
		return RESULT_SHOWUSAGE;
	} else if ((argc == 12) && strcmp(argv[10], "state_interface")) {
		return RESULT_SHOWUSAGE;
	}

	queuename = argv[5];
	interface = argv[3];
	if (argc >= 8) {
		if (sscanf(argv[7], "%30d", &penalty) == 1) {
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

	if (argc >= 10) {
		membername = argv[9];
	}

	if (argc >= 12) {
		state_interface = argv[11];
	}

	switch (add_to_queue(queuename, interface, membername, penalty, 0, queue_persistent_members, state_interface)) {
	case RES_OKAY:
		ast_queue_log(queuename, "CLI", interface, "ADDMEMBER", "%s", "");
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

static char *complete_queue_add_member(const char *line, const char *word, int pos, int state)
{
	/* 0 - queue; 1 - add; 2 - member; 3 - <interface>; 4 - to; 5 - <queue>; 6 - penalty; 7 - <penalty>; 8 - as; 9 - <membername> */
	switch (pos) {
	case 3:	/* Don't attempt to complete name of interface (infinite possibilities) */
		return NULL;
	case 4:	/* only one possible match, "to" */
		return state == 0 ? ast_strdup("to") : NULL;
	case 5:	/* <queue> */
		return complete_queue(line, word, pos, state);
	case 6: /* only one possible match, "penalty" */
		return state == 0 ? ast_strdup("penalty") : NULL;
	case 7:
		if (state < 100) {	/* 0-99 */
			char *num;
			if ((num = ast_malloc(3))) {
				sprintf(num, "%d", state);
			}
			return num;
		} else {
			return NULL;
		}
	case 8: /* only one possible match, "as" */
		return state == 0 ? ast_strdup("as") : NULL;
	case 9:	/* Don't attempt to complete name of member (infinite possibilities) */
		return NULL;
	case 10:
		return state == 0 ? ast_strdup("state_interface") : NULL;
	default:
		return NULL;
	}
}

static int handle_queue_remove_member(int fd, int argc, char *argv[])
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
		ast_queue_log(queuename, "CLI", interface, "REMOVEMEMBER", "%s", "");
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
	case RES_NOT_DYNAMIC:
		ast_cli(fd, "Member not dynamic\n");
		return RESULT_FAILURE;
	default:
		return RESULT_FAILURE;
	}
}

static char *complete_queue_remove_member(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	struct call_queue *q;
	struct member *m;
	struct ao2_iterator mem_iter;

	/* 0 - queue; 1 - remove; 2 - member; 3 - <member>; 4 - from; 5 - <queue> */
	if (pos > 5 || pos < 3)
		return NULL;
	if (pos == 4)	/* only one possible match, 'from' */
		return state == 0 ? ast_strdup("from") : NULL;

	if (pos == 5)	/* No need to duplicate code */
		return complete_queue(line, word, pos, state);

	/* here is the case for 3, <member> */
	if (!AST_LIST_EMPTY(&queues)) { /* XXX unnecessary ? the traverse does that for us */
		AST_LIST_TRAVERSE(&queues, q, list) {
			ao2_lock(q);
			mem_iter = ao2_iterator_init(q->members, 0);
			while ((m = ao2_iterator_next(&mem_iter))) {
				if (++which > state) {
					char *tmp;
					ao2_iterator_destroy(&mem_iter);
					ao2_unlock(q);
					tmp = ast_strdup(m->interface);
					ao2_ref(m, -1);
					return tmp;
				}
				ao2_ref(m, -1);
			}
			ao2_iterator_destroy(&mem_iter);
			ao2_unlock(q);
		}
	}

	return NULL;
}

static char queue_show_usage[] =
"Usage: queue show\n"
"       Provides summary information on a specified queue.\n";

static char qam_cmd_usage[] =
"Usage: queue add member <channel> to <queue> [penalty <penalty> [as <membername> [state_interface <state_interface>]]]\n";

static char qrm_cmd_usage[] =
"Usage: queue remove member <channel> from <queue>\n";

static struct ast_cli_entry cli_show_queue_deprecated = {
	{ "show", "queue", NULL },
	queue_show, NULL,
	NULL, complete_queue_show };

static struct ast_cli_entry cli_add_queue_member_deprecated = {
	{ "add", "queue", "member", NULL },
	handle_queue_add_member, NULL,
	NULL, complete_queue_add_member };

static struct ast_cli_entry cli_remove_queue_member_deprecated = {
	{ "remove", "queue", "member", NULL },
	handle_queue_remove_member, NULL,
	NULL, complete_queue_remove_member };

static struct ast_cli_entry cli_queue[] = {
	/* Deprecated */
	{ { "show", "queues", NULL },
	queue_show, NULL,
	NULL, NULL },

	{ { "queue", "show", NULL },
	queue_show, "Show status of a specified queue",
	queue_show_usage, complete_queue_show, &cli_show_queue_deprecated },

	{ { "queue", "add", "member", NULL },
	handle_queue_add_member, "Add a channel to a specified queue",
	qam_cmd_usage, complete_queue_add_member, &cli_add_queue_member_deprecated },

	{ { "queue", "remove", "member", NULL },
	handle_queue_remove_member, "Removes a channel from a specified queue",
	qrm_cmd_usage, complete_queue_remove_member, &cli_remove_queue_member_deprecated },
};

static int unload_module(void)
{
	int res;

	if (device_state.thread != AST_PTHREADT_NULL) {
		device_state.stop = 1;
		ast_mutex_lock(&device_state.lock);
		ast_cond_signal(&device_state.cond);
		ast_mutex_unlock(&device_state.lock);
		pthread_join(device_state.thread, NULL);
	}

	ast_cli_unregister_multiple(cli_queue, sizeof(cli_queue) / sizeof(struct ast_cli_entry));
	res = ast_manager_unregister("QueueStatus");
	res |= ast_manager_unregister("Queues");
	res |= ast_manager_unregister("QueueAdd");
	res |= ast_manager_unregister("QueueRemove");
	res |= ast_manager_unregister("QueuePause");
	res |= ast_unregister_application(app_aqm);
	res |= ast_unregister_application(app_rqm);
	res |= ast_unregister_application(app_pqm);
	res |= ast_unregister_application(app_upqm);
	res |= ast_unregister_application(app_ql);
	res |= ast_unregister_application(app);
	res |= ast_custom_function_unregister(&queueagentcount_function);
	res |= ast_custom_function_unregister(&queuemembercount_function);
	res |= ast_custom_function_unregister(&queuememberlist_function);
	res |= ast_custom_function_unregister(&queuewaitingcount_function);
	ast_devstate_del(statechange_queue, NULL);

	ast_module_user_hangup_all();

	clear_and_free_interfaces();

	return res;
}

static int load_module(void)
{
	int res;

	if (!reload_queues())
		return AST_MODULE_LOAD_DECLINE;

	if (queue_persistent_members)
		reload_queue_members();

	ast_mutex_init(&device_state.lock);
	ast_cond_init(&device_state.cond, NULL);
	ast_pthread_create(&device_state.thread, NULL, device_state_thread, NULL);

	ast_cli_register_multiple(cli_queue, sizeof(cli_queue) / sizeof(struct ast_cli_entry));
	res = ast_register_application(app, queue_exec, synopsis, descrip);
	res |= ast_register_application(app_aqm, aqm_exec, app_aqm_synopsis, app_aqm_descrip);
	res |= ast_register_application(app_rqm, rqm_exec, app_rqm_synopsis, app_rqm_descrip);
	res |= ast_register_application(app_pqm, pqm_exec, app_pqm_synopsis, app_pqm_descrip);
	res |= ast_register_application(app_upqm, upqm_exec, app_upqm_synopsis, app_upqm_descrip);
	res |= ast_register_application(app_ql, ql_exec, app_ql_synopsis, app_ql_descrip);
	res |= ast_manager_register("Queues", 0, manager_queues_show, "Queues");
	res |= ast_manager_register("QueueStatus", 0, manager_queues_status, "Queue Status");
	res |= ast_manager_register("QueueAdd", EVENT_FLAG_AGENT, manager_add_queue_member, "Add interface to queue.");
	res |= ast_manager_register("QueueRemove", EVENT_FLAG_AGENT, manager_remove_queue_member, "Remove interface from queue.");
	res |= ast_manager_register("QueuePause", EVENT_FLAG_AGENT, manager_pause_queue_member, "Makes a queue member temporarily unavailable");
	res |= ast_custom_function_register(&queueagentcount_function);
	res |= ast_custom_function_register(&queuemembercount_function);
	res |= ast_custom_function_register(&queuememberlist_function);
	res |= ast_custom_function_register(&queuewaitingcount_function);
	res |= ast_devstate_add(statechange_queue, NULL);

	return res;
}

static int reload(void)
{
	reload_queues();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "True Call Queueing",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

