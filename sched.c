/*
 * Asterisk
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C) Mark Spencer
 * 
 * Distributed under the terms of the GNU General Public License (GPL) Version 2
 *
 * Scheduler Routines (form cheops-NG)
 *
 */

#ifdef DEBUG_SCHEDULER
#define DEBUG(a) DEBUG_M(a)
#else
#define DEBUG(a) 
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <asterisk/sched.h>
#include <asterisk/logger.h>

/* Determine if a is sooner than b */
#define SOONER(a,b) (((b).tv_sec > (a).tv_sec) || \
					 (((b).tv_sec == (a).tv_sec) && ((b).tv_usec > (a).tv_usec)))

struct sched {
	struct sched *next;				/* Next event in the list */
	int id; 						/* ID number of event */
	struct timeval when;			/* Absolute time event should take place */
	int resched;					/* When to reschedule */
	void *data; 					/* Data */
	ast_sched_cb callback;		/* Callback */
};

struct sched_context {
	/* Number of events processed */
	int eventcnt;

	/* Number of outstanding schedule events */
	int schedcnt;

	/* Schedule entry and main queue */
 	struct sched *schedq;

#ifdef SCHED_MAX_CACHE
	/* Cache of unused schedule structures and how many */
	struct sched *schedc;
	int schedccnt;
#endif
};

struct sched_context *sched_context_create(void)
{
	struct sched_context *tmp;
	tmp = malloc(sizeof(struct sched_context));
	if (tmp) {
		tmp->eventcnt = 1;
		tmp->schedcnt = 0;
		tmp->schedq = NULL;
		tmp->schedc = NULL;
		tmp->schedccnt = 0;
	}
	return tmp;
}

void sched_context_destroy(struct sched_context *con)
{
	struct sched *s, *sl;
	/* Eliminate the cache */
	s = con->schedc;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
	/* And the queue */
	s = con->schedq;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
	/* And the context */
	free(con);
}

static struct sched *sched_alloc(struct sched_context *con)
{
	/*
	 * We keep a small cache of schedule entries
	 * to minimize the number of necessary malloc()'s
	 */
	struct sched *tmp;
#ifdef SCHED_MAX_CACHE
	if (con->schedc) {
		tmp = con->schedc;
		con->schedc = con->schedc->next;
		con->schedccnt--;
	} else
#endif
		tmp = malloc(sizeof(struct sched));
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
		tmp->next = con->schedc;
		con->schedc = tmp;
		con->schedccnt++;
	} else
#endif
		free(tmp);
}

int ast_sched_wait(struct sched_context *con)
{
	/*
	 * Return the number of milliseconds 
	 * until the next scheduled event
	 */
	struct timeval tv;
	int ms;
	DEBUG(ast_log(LOG_DEBUG, "ast_sched_wait()\n"));
	if (!con->schedq)
		return -1;
	if (gettimeofday(&tv, NULL) < 0) {
		/* This should never happen */
		return 0;
	};
	ms = (con->schedq->when.tv_sec - tv.tv_sec) * 1000;
	ms += (con->schedq->when.tv_usec - tv.tv_usec) / 1000;
	if (ms < 0)
		ms = 0;
	return ms;
	
}


static void schedule(struct sched_context *con, struct sched *s)
{
	/*
	 * Take a sched structure and put it in the
	 * queue, such that the soonest event is
	 * first in the list. 
	 */
	 
	struct sched *last=NULL;
	struct sched *current=con->schedq;
	while(current) {
		if (SOONER(s->when, current->when))
			break;
		last = current;
		current = current->next;
	}
	/* Insert this event into the schedule */
	s->next = current;
	if (last) 
		last->next = s;
	else
		con->schedq = s;
	con->schedcnt++;
}

static inline int sched_settime(struct timeval *tv, int when)
{
	if (gettimeofday(tv, NULL) < 0) {
			/* This shouldn't ever happen, but let's be sure */
			ast_log(LOG_NOTICE, "gettimeofday() failed!\n");
			return -1;
	}
	tv->tv_sec += when/1000;
	tv->tv_usec += (when % 1000) * 1000;
	if (tv->tv_usec > 1000000) {
		tv->tv_sec++;
		tv->tv_usec-= 1000000;
	}
	return 0;
}

int ast_sched_add(struct sched_context *con, int when, ast_sched_cb callback, void *data)
{
	/*
	 * Schedule callback(data) to happen when ms into the future
	 */
	struct sched *tmp;
	DEBUG(ast_log(LOG_DEBUG, "ast_sched_add()\n"));
	if (!when) {
		ast_log(LOG_NOTICE, "Scheduled event in 0 ms?");
		return -1;
	}
	if ((tmp = sched_alloc(con))) {
		tmp->id = con->eventcnt++;
		tmp->callback = callback;
		tmp->data = data;
		tmp->resched = when;
		if (sched_settime(&tmp->when, when)) {
			sched_release(con, tmp);
			return -1;
		} else
			schedule(con, tmp);
	} else 
		return -1;
	return tmp->id;
}

int ast_sched_del(struct sched_context *con, int id)
{
	/*
	 * Delete the schedule entry with number
	 * "id".  It's nearly impossible that there
	 * would be two or more in the list with that
	 * id.
	 */
	struct sched *last=NULL, *s;
	DEBUG(ast_log(LOG_DEBUG, "ast_sched_del()\n"));
	s = con->schedq;
	while(s) {
		if (s->id == id) {
			if (last)
				last->next = s->next;
			else
				con->schedq = s->next;
			con->schedcnt--;
			return 0;
		}
		last = s;
		s = s->next;
	}
	ast_log(LOG_NOTICE, "Attempted to delete non-existant schedule entry %d!\n", id);
#ifdef FORCE_CRASH
	crash();
#endif
	return -1;
}

void ast_sched_dump(struct sched_context *con)
{
	/*
	 * Dump the contents of the scheduler to
	 * stderr
	 */
	struct sched *q;
	struct timeval tv;
	time_t s, ms;
	gettimeofday(&tv, NULL);
	ast_log(LOG_DEBUG, "Cheops Schedule Dump (%d in Q, %d Total, %d Cache)\n", 
							 con-> schedcnt, con->eventcnt - 1, con->schedccnt);
	ast_log(LOG_DEBUG, "=================================================\n");
	ast_log(LOG_DEBUG, "|ID    Callback    Data        Time  (sec:ms)   |\n");
	ast_log(LOG_DEBUG, "+-----+-----------+-----------+-----------------+\n");
	q = con->schedq;
	while(q) {
		s =  q->when.tv_sec - tv.tv_sec;
		ms = q->when.tv_usec - tv.tv_usec;
		if (ms < 0) {
			ms += 1000000;
			s--;
		}
		ast_log(LOG_DEBUG, "|%.4d | %p | %p | %.6ld : %.6ld |\n", 
				q->id,
				q->callback,
				q->data,
				s,
				ms);
		q=q->next;
	}
	ast_log(LOG_DEBUG, "=================================================\n");
	
}

int ast_sched_runq(struct sched_context *con)
{
	/*
	 * Launch all events which need to be run at this time.
	 */
	struct sched *current;
	struct timeval tv;
	int x=0;
	DEBUG(ast_log(LOG_DEBUG, "ast_sched_runq()\n"));
		
	for(;;) {
		if (!con->schedq)
			break;
		if (gettimeofday(&tv, NULL)) {
			/* This should never happen */
			ast_log(LOG_NOTICE, "gettimeofday() failed!\n");
			return 0;
		}
		/* We only care about millisecond accuracy anyway, so this will
		   help us get more than one event at one time if they are very
		   close together. */
		tv.tv_usec += 1000;
		if (SOONER(con->schedq->when, tv)) {
			current = con->schedq;
			con->schedq = con->schedq->next;
			con->schedcnt--;

			/*
			 * At this point, the schedule queue is still intact.  We
			 * have removed the first event and the rest is still there,
			 * so it's permissible for the callback to add new events, but
			 * trying to delete itself won't work because it isn't in
			 * the schedule queue.  If that's what it wants to do, it 
			 * should return 0.
			 */
			if (current->callback(current->data)) {
			 	/*
				 * If they return non-zero, we should schedule them to be
				 * run again.
				 */
				if (sched_settime(&current->when, current->resched)) {
					sched_release(con, current);
				} else
					schedule(con, current);
			} else {
				/* No longer needed, so release it */
			 	sched_release(con, current);
			}
			x++;
		} else
			break;
	}
	return x;
}
