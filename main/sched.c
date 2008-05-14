/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Scheduler Routines (from cheops-NG)
 *
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#ifdef DEBUG_SCHEDULER
#define DEBUG(a) do { \
	if (option_debug) \
		DEBUG_M(a) \
	} while (0)
#else
#define DEBUG(a) 
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/sched.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/options.h"

struct sched {
	AST_LIST_ENTRY(sched) list;
	int id;                       /*!< ID number of event */
	struct timeval when;          /*!< Absolute time event should take place */
	int resched;                  /*!< When to reschedule */
	int variable;                 /*!< Use return value from callback to reschedule */
	const void *data;             /*!< Data */
	ast_sched_cb callback;        /*!< Callback */
};

struct sched_context {
	ast_mutex_t lock;
	unsigned int eventcnt;                  /*!< Number of events processed */
	unsigned int schedcnt;                  /*!< Number of outstanding schedule events */
	AST_LIST_HEAD_NOLOCK(, sched) schedq;   /*!< Schedule entry and main queue */

#ifdef SCHED_MAX_CACHE
	AST_LIST_HEAD_NOLOCK(, sched) schedc;   /*!< Cache of unused schedule structures and how many */
	unsigned int schedccnt;
#endif
};

struct sched_context *sched_context_create(void)
{
	struct sched_context *tmp;

	if (!(tmp = ast_calloc(1, sizeof(*tmp))))
		return NULL;

	ast_mutex_init(&tmp->lock);
	tmp->eventcnt = 1;
	
	return tmp;
}

void sched_context_destroy(struct sched_context *con)
{
	struct sched *s;

	ast_mutex_lock(&con->lock);

#ifdef SCHED_MAX_CACHE
	/* Eliminate the cache */
	while ((s = AST_LIST_REMOVE_HEAD(&con->schedc, list)))
		free(s);
#endif

	/* And the queue */
	while ((s = AST_LIST_REMOVE_HEAD(&con->schedq, list)))
		free(s);
	
	/* And the context */
	ast_mutex_unlock(&con->lock);
	ast_mutex_destroy(&con->lock);
	free(con);
}

static struct sched *sched_alloc(struct sched_context *con)
{
	struct sched *tmp;

	/*
	 * We keep a small cache of schedule entries
	 * to minimize the number of necessary malloc()'s
	 */
#ifdef SCHED_MAX_CACHE
	if ((tmp = AST_LIST_REMOVE_HEAD(&con->schedc, list)))
		con->schedccnt--;
	else
#endif
		tmp = ast_calloc(1, sizeof(*tmp));

	return tmp;
}

static void sched_release(struct sched_context *con, struct sched *tmp)
{
	/*
	 * Add to the cache, or just free() if we
	 * already have too many cache entries
	 */

#ifdef SCHED_MAX_CACHE	 
	if (con->schedccnt < SCHED_MAX_CACHE) {
		AST_LIST_INSERT_HEAD(&con->schedc, tmp, list);
		con->schedccnt++;
	} else
#endif
		free(tmp);
}

/*! \brief
 * Return the number of milliseconds 
 * until the next scheduled event
 */
int ast_sched_wait(struct sched_context *con)
{
	int ms;

	DEBUG(ast_log(LOG_DEBUG, "ast_sched_wait()\n"));

	ast_mutex_lock(&con->lock);
	if (AST_LIST_EMPTY(&con->schedq)) {
		ms = -1;
	} else {
		ms = ast_tvdiff_ms(AST_LIST_FIRST(&con->schedq)->when, ast_tvnow());
		if (ms < 0)
			ms = 0;
	}
	ast_mutex_unlock(&con->lock);

	return ms;
}


/*! \brief
 * Take a sched structure and put it in the
 * queue, such that the soonest event is
 * first in the list. 
 */
static void schedule(struct sched_context *con, struct sched *s)
{
	 
	struct sched *cur = NULL;
	
	AST_LIST_TRAVERSE_SAFE_BEGIN(&con->schedq, cur, list) {
		if (ast_tvcmp(s->when, cur->when) == -1) {
			AST_LIST_INSERT_BEFORE_CURRENT(&con->schedq, s, list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	if (!cur)
		AST_LIST_INSERT_TAIL(&con->schedq, s, list);
	
	con->schedcnt++;
}

/*! \brief
 * given the last event *tv and the offset in milliseconds 'when',
 * computes the next value,
 */
static int sched_settime(struct timeval *tv, int when)
{
	struct timeval now = ast_tvnow();

	/*ast_log(LOG_DEBUG, "TV -> %lu,%lu\n", tv->tv_sec, tv->tv_usec);*/
	if (ast_tvzero(*tv))	/* not supplied, default to now */
		*tv = now;
	*tv = ast_tvadd(*tv, ast_samp2tv(when, 1000));
	if (ast_tvcmp(*tv, now) < 0) {
		ast_log(LOG_DEBUG, "Request to schedule in the past?!?!\n");
		*tv = now;
	}
	return 0;
}


/*! \brief
 * Schedule callback(data) to happen when ms into the future
 */
int ast_sched_add_variable(struct sched_context *con, int when, ast_sched_cb callback, const void *data, int variable)
{
	struct sched *tmp;
	int res = -1;
	DEBUG(ast_log(LOG_DEBUG, "ast_sched_add()\n"));
	if (!when) {
		ast_log(LOG_NOTICE, "Scheduled event in 0 ms?\n");
		return -1;
	}
	ast_mutex_lock(&con->lock);
	if ((tmp = sched_alloc(con))) {
		tmp->id = con->eventcnt++;
		tmp->callback = callback;
		tmp->data = data;
		tmp->resched = when;
		tmp->variable = variable;
		tmp->when = ast_tv(0, 0);
		if (sched_settime(&tmp->when, when)) {
			sched_release(con, tmp);
		} else {
			schedule(con, tmp);
			res = tmp->id;
		}
	}
#ifdef DUMP_SCHEDULER
	/* Dump contents of the context while we have the lock so nothing gets screwed up by accident. */
	if (option_debug)
		ast_sched_dump(con);
#endif
	ast_mutex_unlock(&con->lock);
	return res;
}

int ast_sched_add(struct sched_context *con, int when, ast_sched_cb callback, const void *data)
{
	return ast_sched_add_variable(con, when, callback, data, 0);
}

/*! \brief
 * Delete the schedule entry with number
 * "id".  It's nearly impossible that there
 * would be two or more in the list with that
 * id.
 */
int ast_sched_del(struct sched_context *con, int id)
{
	struct sched *s;

	DEBUG(ast_log(LOG_DEBUG, "ast_sched_del()\n"));
	
	ast_mutex_lock(&con->lock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&con->schedq, s, list) {
		if (s->id == id) {
			AST_LIST_REMOVE_CURRENT(&con->schedq, list);
			con->schedcnt--;
			sched_release(con, s);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END

#ifdef DUMP_SCHEDULER
	/* Dump contents of the context while we have the lock so nothing gets screwed up by accident. */
	if (option_debug)
		ast_sched_dump(con);
#endif
	ast_mutex_unlock(&con->lock);

	if (!s) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Attempted to delete nonexistent schedule entry %d!\n", id);
		ast_assert(0);
		return -1;
	}
	
	return 0;
}

/*! \brief Dump the contents of the scheduler to LOG_DEBUG */
void ast_sched_dump(const struct sched_context *con)
{
	struct sched *q;
	struct timeval tv = ast_tvnow();
#ifdef SCHED_MAX_CACHE
	ast_log(LOG_DEBUG, "Asterisk Schedule Dump (%d in Q, %d Total, %d Cache)\n", con->schedcnt, con->eventcnt - 1, con->schedccnt);
#else
	ast_log(LOG_DEBUG, "Asterisk Schedule Dump (%d in Q, %d Total)\n", con->schedcnt, con->eventcnt - 1);
#endif

	ast_log(LOG_DEBUG, "=============================================================\n");
	ast_log(LOG_DEBUG, "|ID    Callback          Data              Time  (sec:ms)   |\n");
	ast_log(LOG_DEBUG, "+-----+-----------------+-----------------+-----------------+\n");
	AST_LIST_TRAVERSE(&con->schedq, q, list) {
		struct timeval delta = ast_tvsub(q->when, tv);

		ast_log(LOG_DEBUG, "|%.4d | %-15p | %-15p | %.6ld : %.6ld |\n", 
			q->id,
			q->callback,
			q->data,
			delta.tv_sec,
			(long int)delta.tv_usec);
	}
	ast_log(LOG_DEBUG, "=============================================================\n");
	
}

/*! \brief
 * Launch all events which need to be run at this time.
 */
int ast_sched_runq(struct sched_context *con)
{
	struct sched *current;
	struct timeval tv;
	int numevents;
	int res;

	DEBUG(ast_log(LOG_DEBUG, "ast_sched_runq()\n"));
		
	ast_mutex_lock(&con->lock);

	for (numevents = 0; !AST_LIST_EMPTY(&con->schedq); numevents++) {
		/* schedule all events which are going to expire within 1ms.
		 * We only care about millisecond accuracy anyway, so this will
		 * help us get more than one event at one time if they are very
		 * close together.
		 */
		tv = ast_tvadd(ast_tvnow(), ast_tv(0, 1000));
		if (ast_tvcmp(AST_LIST_FIRST(&con->schedq)->when, tv) != -1)
			break;
		
		current = AST_LIST_REMOVE_HEAD(&con->schedq, list);
		con->schedcnt--;

		/*
		 * At this point, the schedule queue is still intact.  We
		 * have removed the first event and the rest is still there,
		 * so it's permissible for the callback to add new events, but
		 * trying to delete itself won't work because it isn't in
		 * the schedule queue.  If that's what it wants to do, it 
		 * should return 0.
		 */
			
		ast_mutex_unlock(&con->lock);
		res = current->callback(current->data);
		ast_mutex_lock(&con->lock);
			
		if (res) {
		 	/*
			 * If they return non-zero, we should schedule them to be
			 * run again.
			 */
			if (sched_settime(&current->when, current->variable? res : current->resched)) {
				sched_release(con, current);
			} else
				schedule(con, current);
		} else {
			/* No longer needed, so release it */
		 	sched_release(con, current);
		}
	}

	ast_mutex_unlock(&con->lock);
	
	return numevents;
}

long ast_sched_when(struct sched_context *con,int id)
{
	struct sched *s;
	long secs = -1;
	DEBUG(ast_log(LOG_DEBUG, "ast_sched_when()\n"));

	ast_mutex_lock(&con->lock);
	AST_LIST_TRAVERSE(&con->schedq, s, list) {
		if (s->id == id)
			break;
	}
	if (s) {
		struct timeval now = ast_tvnow();
		secs = s->when.tv_sec - now.tv_sec;
	}
	ast_mutex_unlock(&con->lock);
	
	return secs;
}
