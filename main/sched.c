/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Russell Bryant <russell@digium.com>
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

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

#include <sys/time.h>

#include "asterisk/sched.h"
#include "asterisk/channel.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dlinkedlists.h"
#include "asterisk/hashtab.h"
#include "asterisk/heap.h"
#include "asterisk/threadstorage.h"

/*!
 * \brief Max num of schedule structs
 *
 * \note The max number of schedule structs to keep around
 * for use.  Undefine to disable schedule structure
 * caching. (Only disable this on very low memory
 * machines)
 */
#define SCHED_MAX_CACHE 128

AST_THREADSTORAGE(last_del_id);

struct sched {
	AST_LIST_ENTRY(sched) list;
	int id;                       /*!< ID number of event */
	struct timeval when;          /*!< Absolute time event should take place */
	int resched;                  /*!< When to reschedule */
	int variable;                 /*!< Use return value from callback to reschedule */
	const void *data;             /*!< Data */
	ast_sched_cb callback;        /*!< Callback */
	ssize_t __heap_index;
	/*!
	 * Used to synchronize between thread running a task and thread
	 * attempting to delete a task
	 */
	ast_cond_t cond;
	/*! Indication that a running task was deleted. */
	unsigned int deleted:1;
};

struct sched_thread {
	pthread_t thread;
	ast_cond_t cond;
	unsigned int stop:1;
};

struct ast_sched_context {
	ast_mutex_t lock;
	unsigned int eventcnt;                  /*!< Number of events processed */
	unsigned int schedcnt;                  /*!< Number of outstanding schedule events */
	unsigned int highwater;					/*!< highest count so far */
	struct ast_hashtab *schedq_ht;             /*!< hash table for fast searching */
	struct ast_heap *sched_heap;
	struct sched_thread *sched_thread;
	/*! The scheduled task that is currently executing */
	struct sched *currently_executing;

#ifdef SCHED_MAX_CACHE
	AST_LIST_HEAD_NOLOCK(, sched) schedc;   /*!< Cache of unused schedule structures and how many */
	unsigned int schedccnt;
#endif
};

static void *sched_run(void *data)
{
	struct ast_sched_context *con = data;

	while (!con->sched_thread->stop) {
		int ms;
		struct timespec ts = {
			.tv_sec = 0,
		};

		ast_mutex_lock(&con->lock);

		if (con->sched_thread->stop) {
			ast_mutex_unlock(&con->lock);
			return NULL;
		}

		ms = ast_sched_wait(con);

		if (ms == -1) {
			ast_cond_wait(&con->sched_thread->cond, &con->lock);
		} else {
			struct timeval tv;
			tv = ast_tvadd(ast_tvnow(), ast_samp2tv(ms, 1000));
			ts.tv_sec = tv.tv_sec;
			ts.tv_nsec = tv.tv_usec * 1000;
			ast_cond_timedwait(&con->sched_thread->cond, &con->lock, &ts);
		}

		ast_mutex_unlock(&con->lock);

		if (con->sched_thread->stop) {
			return NULL;
		}

		ast_sched_runq(con);
	}

	return NULL;
}

static void sched_thread_destroy(struct ast_sched_context *con)
{
	if (!con->sched_thread) {
		return;
	}

	if (con->sched_thread->thread != AST_PTHREADT_NULL) {
		ast_mutex_lock(&con->lock);
		con->sched_thread->stop = 1;
		ast_cond_signal(&con->sched_thread->cond);
		ast_mutex_unlock(&con->lock);
		pthread_join(con->sched_thread->thread, NULL);
		con->sched_thread->thread = AST_PTHREADT_NULL;
	}

	ast_cond_destroy(&con->sched_thread->cond);

	ast_free(con->sched_thread);

	con->sched_thread = NULL;
}

int ast_sched_start_thread(struct ast_sched_context *con)
{
	struct sched_thread *st;

	if (con->sched_thread) {
		ast_log(LOG_ERROR, "Thread already started on this scheduler context\n");
		return -1;
	}

	if (!(st = ast_calloc(1, sizeof(*st)))) {
		return -1;
	}

	ast_cond_init(&st->cond, NULL);

	st->thread = AST_PTHREADT_NULL;

	con->sched_thread = st;

	if (ast_pthread_create_background(&st->thread, NULL, sched_run, con)) {
		ast_log(LOG_ERROR, "Failed to create scheduler thread\n");
		sched_thread_destroy(con);
		return -1;
	}

	return 0;
}

static int sched_cmp(const void *a, const void *b)
{
	const struct sched *as = a;
	const struct sched *bs = b;
	return as->id != bs->id; /* return 0 on a match like strcmp would */
}

static unsigned int sched_hash(const void *obj)
{
	const struct sched *s = obj;
	unsigned int h = s->id;
	return h;
}

static int sched_time_cmp(void *a, void *b)
{
	return ast_tvcmp(((struct sched *) b)->when, ((struct sched *) a)->when);
}

struct ast_sched_context *ast_sched_context_create(void)
{
	struct ast_sched_context *tmp;

	if (!(tmp = ast_calloc(1, sizeof(*tmp)))) {
		return NULL;
	}

	ast_mutex_init(&tmp->lock);
	tmp->eventcnt = 1;

	tmp->schedq_ht = ast_hashtab_create(23, sched_cmp, ast_hashtab_resize_java, ast_hashtab_newsize_java, sched_hash, 1);

	if (!(tmp->sched_heap = ast_heap_create(8, sched_time_cmp,
			offsetof(struct sched, __heap_index)))) {
		ast_sched_context_destroy(tmp);
		return NULL;
	}

	return tmp;
}

static void sched_free(struct sched *task)
{
	ast_cond_destroy(&task->cond);
	ast_free(task);
}

void ast_sched_context_destroy(struct ast_sched_context *con)
{
	struct sched *s;

	sched_thread_destroy(con);
	con->sched_thread = NULL;

	ast_mutex_lock(&con->lock);

#ifdef SCHED_MAX_CACHE
	while ((s = AST_LIST_REMOVE_HEAD(&con->schedc, list))) {
		sched_free(s);
	}
#endif

	if (con->sched_heap) {
		while ((s = ast_heap_pop(con->sched_heap))) {
			sched_free(s);
		}
		ast_heap_destroy(con->sched_heap);
		con->sched_heap = NULL;
	}

	ast_hashtab_destroy(con->schedq_ht, NULL);
	con->schedq_ht = NULL;

	ast_mutex_unlock(&con->lock);
	ast_mutex_destroy(&con->lock);

	ast_free(con);
}

static struct sched *sched_alloc(struct ast_sched_context *con)
{
	struct sched *tmp;

	/*
	 * We keep a small cache of schedule entries
	 * to minimize the number of necessary malloc()'s
	 */
#ifdef SCHED_MAX_CACHE
	if ((tmp = AST_LIST_REMOVE_HEAD(&con->schedc, list))) {
		con->schedccnt--;
	} else 
#endif
	{
		tmp = ast_calloc(1, sizeof(*tmp));
		ast_cond_init(&tmp->cond, NULL);
	}

	return tmp;
}

static void sched_release(struct ast_sched_context *con, struct sched *tmp)
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
		sched_free(tmp);
}

/*! \brief
 * Return the number of milliseconds
 * until the next scheduled event
 */
int ast_sched_wait(struct ast_sched_context *con)
{
	int ms;
	struct sched *s;

	DEBUG(ast_debug(1, "ast_sched_wait()\n"));

	ast_mutex_lock(&con->lock);
	if ((s = ast_heap_peek(con->sched_heap, 1))) {
		ms = ast_tvdiff_ms(s->when, ast_tvnow());
		if (ms < 0) {
			ms = 0;
		}
	} else {
		ms = -1;
	}
	ast_mutex_unlock(&con->lock);

	return ms;
}


/*! \brief
 * Take a sched structure and put it in the
 * queue, such that the soonest event is
 * first in the list.
 */
static void schedule(struct ast_sched_context *con, struct sched *s)
{
	ast_heap_push(con->sched_heap, s);

	if (!ast_hashtab_insert_safe(con->schedq_ht, s)) {
		ast_log(LOG_WARNING,"Schedule Queue entry %d is already in table!\n", s->id);
	}

	con->schedcnt++;

	if (con->schedcnt > con->highwater) {
		con->highwater = con->schedcnt;
	}
}

/*! \brief
 * given the last event *tv and the offset in milliseconds 'when',
 * computes the next value,
 */
static int sched_settime(struct timeval *t, int when)
{
	struct timeval now = ast_tvnow();

	/*ast_debug(1, "TV -> %lu,%lu\n", tv->tv_sec, tv->tv_usec);*/
	if (ast_tvzero(*t))	/* not supplied, default to now */
		*t = now;
	*t = ast_tvadd(*t, ast_samp2tv(when, 1000));
	if (ast_tvcmp(*t, now) < 0) {
		*t = now;
	}
	return 0;
}

int ast_sched_replace_variable(int old_id, struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data, int variable)
{
	/* 0 means the schedule item is new; do not delete */
	if (old_id > 0) {
		AST_SCHED_DEL(con, old_id);
	}
	return ast_sched_add_variable(con, when, callback, data, variable);
}

/*! \brief
 * Schedule callback(data) to happen when ms into the future
 */
int ast_sched_add_variable(struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data, int variable)
{
	struct sched *tmp;
	int res = -1;

	DEBUG(ast_debug(1, "ast_sched_add()\n"));

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
	if (con->sched_thread) {
		ast_cond_signal(&con->sched_thread->cond);
	}
	ast_mutex_unlock(&con->lock);

	return res;
}

int ast_sched_replace(int old_id, struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data)
{
	if (old_id > -1) {
		AST_SCHED_DEL(con, old_id);
	}
	return ast_sched_add(con, when, callback, data);
}

int ast_sched_add(struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data)
{
	return ast_sched_add_variable(con, when, callback, data, 0);
}

const void *ast_sched_find_data(struct ast_sched_context *con, int id)
{
	struct sched tmp,*res;
	tmp.id = id;
	res = ast_hashtab_lookup(con->schedq_ht, &tmp);
	if (res)
		return res->data;
	return NULL;
}

/*! \brief
 * Delete the schedule entry with number
 * "id".  It's nearly impossible that there
 * would be two or more in the list with that
 * id.
 */
#ifndef AST_DEVMODE
int ast_sched_del(struct ast_sched_context *con, int id)
#else
int _ast_sched_del(struct ast_sched_context *con, int id, const char *file, int line, const char *function)
#endif
{
	struct sched *s, tmp = {
		.id = id,
	};
	int *last_id = ast_threadstorage_get(&last_del_id, sizeof(int));

	DEBUG(ast_debug(1, "ast_sched_del(%d)\n", id));

	if (id < 0) {
		return 0;
	}

	ast_mutex_lock(&con->lock);
	s = ast_hashtab_lookup(con->schedq_ht, &tmp);
	if (s) {
		if (!ast_heap_remove(con->sched_heap, s)) {
			ast_log(LOG_WARNING,"sched entry %d not in the sched heap?\n", s->id);
		}

		if (!ast_hashtab_remove_this_object(con->schedq_ht, s)) {
			ast_log(LOG_WARNING,"Found sched entry %d, then couldn't remove it?\n", s->id);
		}
		con->schedcnt--;
		sched_release(con, s);
	} else if (con->currently_executing && (id == con->currently_executing->id)) {
		s = con->currently_executing;
		s->deleted = 1;
		/* Wait for executing task to complete so that caller of ast_sched_del() does not
		 * free memory out from under the task.
		 */
		ast_cond_wait(&s->cond, &con->lock);
		/* Do not sched_release() here because ast_sched_runq() will do it */
	}

#ifdef DUMP_SCHEDULER
	/* Dump contents of the context while we have the lock so nothing gets screwed up by accident. */
	if (option_debug)
		ast_sched_dump(con);
#endif
	if (con->sched_thread) {
		ast_cond_signal(&con->sched_thread->cond);
	}
	ast_mutex_unlock(&con->lock);

	if (!s && *last_id != id) {
		ast_debug(1, "Attempted to delete nonexistent schedule entry %d!\n", id);
#ifndef AST_DEVMODE
		ast_assert(s != NULL);
#else
		{
		char buf[100];
		snprintf(buf, sizeof(buf), "s != NULL, id=%d", id);
		_ast_assert(0, buf, file, line, function);
		}
#endif
		*last_id = id;
		return -1;
	} else if (!s) {
		return -1;
	}

	return 0;
}

void ast_sched_report(struct ast_sched_context *con, struct ast_str **buf, struct ast_cb_names *cbnames)
{
	int i, x;
	struct sched *cur;
	int countlist[cbnames->numassocs + 1];
	size_t heap_size;

	memset(countlist, 0, sizeof(countlist));
	ast_str_set(buf, 0, " Highwater = %u\n schedcnt = %u\n", con->highwater, con->schedcnt);

	ast_mutex_lock(&con->lock);

	heap_size = ast_heap_size(con->sched_heap);
	for (x = 1; x <= heap_size; x++) {
		cur = ast_heap_peek(con->sched_heap, x);
		/* match the callback to the cblist */
		for (i = 0; i < cbnames->numassocs; i++) {
			if (cur->callback == cbnames->cblist[i]) {
				break;
			}
		}
		if (i < cbnames->numassocs) {
			countlist[i]++;
		} else {
			countlist[cbnames->numassocs]++;
		}
	}

	ast_mutex_unlock(&con->lock);

	for (i = 0; i < cbnames->numassocs; i++) {
		ast_str_append(buf, 0, "    %s : %d\n", cbnames->list[i], countlist[i]);
	}

	ast_str_append(buf, 0, "   <unknown> : %d\n", countlist[cbnames->numassocs]);
}

/*! \brief Dump the contents of the scheduler to LOG_DEBUG */
void ast_sched_dump(struct ast_sched_context *con)
{
	struct sched *q;
	struct timeval when = ast_tvnow();
	int x;
	size_t heap_size;
#ifdef SCHED_MAX_CACHE
	ast_debug(1, "Asterisk Schedule Dump (%u in Q, %u Total, %u Cache, %u high-water)\n", con->schedcnt, con->eventcnt - 1, con->schedccnt, con->highwater);
#else
	ast_debug(1, "Asterisk Schedule Dump (%u in Q, %u Total, %u high-water)\n", con->schedcnt, con->eventcnt - 1, con->highwater);
#endif

	ast_debug(1, "=============================================================\n");
	ast_debug(1, "|ID    Callback          Data              Time  (sec:ms)   |\n");
	ast_debug(1, "+-----+-----------------+-----------------+-----------------+\n");
	ast_mutex_lock(&con->lock);
	heap_size = ast_heap_size(con->sched_heap);
	for (x = 1; x <= heap_size; x++) {
		struct timeval delta;
		q = ast_heap_peek(con->sched_heap, x);
		delta = ast_tvsub(q->when, when);
		ast_debug(1, "|%.4d | %-15p | %-15p | %.6ld : %.6ld |\n",
			q->id,
			q->callback,
			q->data,
			(long)delta.tv_sec,
			(long int)delta.tv_usec);
	}
	ast_mutex_unlock(&con->lock);
	ast_debug(1, "=============================================================\n");
}

/*! \brief
 * Launch all events which need to be run at this time.
 */
int ast_sched_runq(struct ast_sched_context *con)
{
	struct sched *current;
	struct timeval when;
	int numevents;
	int res;

	DEBUG(ast_debug(1, "ast_sched_runq()\n"));

	ast_mutex_lock(&con->lock);

	when = ast_tvadd(ast_tvnow(), ast_tv(0, 1000));
	for (numevents = 0; (current = ast_heap_peek(con->sched_heap, 1)); numevents++) {
		/* schedule all events which are going to expire within 1ms.
		 * We only care about millisecond accuracy anyway, so this will
		 * help us get more than one event at one time if they are very
		 * close together.
		 */
		if (ast_tvcmp(current->when, when) != -1) {
			break;
		}

		current = ast_heap_pop(con->sched_heap);

		if (!ast_hashtab_remove_this_object(con->schedq_ht, current)) {
			ast_log(LOG_ERROR,"Sched entry %d was in the schedq list but not in the hashtab???\n", current->id);
		}

		con->schedcnt--;

		/*
		 * At this point, the schedule queue is still intact.  We
		 * have removed the first event and the rest is still there,
		 * so it's permissible for the callback to add new events, but
		 * trying to delete itself won't work because it isn't in
		 * the schedule queue.  If that's what it wants to do, it
		 * should return 0.
		 */

		con->currently_executing = current;
		ast_mutex_unlock(&con->lock);
		res = current->callback(current->data);
		ast_mutex_lock(&con->lock);
		con->currently_executing = NULL;
		ast_cond_signal(&current->cond);

		if (res && !current->deleted) {
			/*
			 * If they return non-zero, we should schedule them to be
			 * run again.
			 */
			if (sched_settime(&current->when, current->variable? res : current->resched)) {
				sched_release(con, current);
			} else {
				schedule(con, current);
			}
		} else {
			/* No longer needed, so release it */
			sched_release(con, current);
		}
	}

	ast_mutex_unlock(&con->lock);

	return numevents;
}

long ast_sched_when(struct ast_sched_context *con,int id)
{
	struct sched *s, tmp;
	long secs = -1;
	DEBUG(ast_debug(1, "ast_sched_when()\n"));

	ast_mutex_lock(&con->lock);

	/* these next 2 lines replace a lookup loop */
	tmp.id = id;
	s = ast_hashtab_lookup(con->schedq_ht, &tmp);

	if (s) {
		struct timeval now = ast_tvnow();
		secs = s->when.tv_sec - now.tv_sec;
	}
	ast_mutex_unlock(&con->lock);

	return secs;
}
