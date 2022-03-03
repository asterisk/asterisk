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

#ifdef DEBUG_SCHEDULER
#define DEBUG(a) a
#else
#define DEBUG(a)
#endif

#include <sys/time.h>

#include "asterisk/sched.h"
#include "asterisk/channel.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
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

/*!
 * \brief Scheduler ID holder
 *
 * These form a queue on a scheduler context. When a new
 * scheduled item is created, a sched_id is popped off the
 * queue and its id is assigned to the new scheduled item.
 * When the scheduled task is complete, the sched_id on that
 * task is then pushed to the back of the queue to be re-used
 * on some future scheduled item.
 */
struct sched_id {
	/*! Immutable ID number that is copied onto the scheduled task */
	int id;
	AST_LIST_ENTRY(sched_id) list;
};

struct sched {
	AST_LIST_ENTRY(sched) list;
	/*! The ID that has been popped off the scheduler context's queue */
	struct sched_id *sched_id;
	struct timeval when;          /*!< Absolute time event should take place */
	/*!
	 * \brief Tie breaker in case the when is the same for multiple entries.
	 *
	 * \note The oldest expiring entry in the scheduler heap goes first.
	 * This is possible when multiple events are scheduled to expire at
	 * the same time by internal coding.
	 */
	unsigned int tie_breaker;
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
	/*! Indication that a running task was rescheduled. */
	unsigned int rescheduled:1;
};

struct sched_thread {
	pthread_t thread;
	ast_cond_t cond;
	unsigned int stop:1;
};

struct ast_sched_context {
	ast_mutex_t lock;
	unsigned int eventcnt;                  /*!< Number of events processed */
	unsigned int highwater;					/*!< highest count so far */
	/*! Next tie breaker in case events expire at the same time. */
	unsigned int tie_breaker;
	struct ast_heap *sched_heap;
	struct sched_thread *sched_thread;
	/*! The scheduled task that is currently executing */
	struct sched *currently_executing;
	/*! Valid while currently_executing is not NULL */
	pthread_t executing_thread_id;

#ifdef SCHED_MAX_CACHE
	AST_LIST_HEAD_NOLOCK(, sched) schedc;   /*!< Cache of unused schedule structures and how many */
	unsigned int schedccnt;
#endif
	/*! Queue of scheduler task IDs to assign */
	AST_LIST_HEAD_NOLOCK(, sched_id) id_queue;
	/*! The number of IDs in the id_queue */
	int id_queue_size;
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

static int sched_time_cmp(void *va, void *vb)
{
	struct sched *a = va;
	struct sched *b = vb;
	int cmp;

	cmp = ast_tvcmp(b->when, a->when);
	if (!cmp) {
		cmp = b->tie_breaker - a->tie_breaker;
	}
	return cmp;
}

struct ast_sched_context *ast_sched_context_create(void)
{
	struct ast_sched_context *tmp;

	if (!(tmp = ast_calloc(1, sizeof(*tmp)))) {
		return NULL;
	}

	ast_mutex_init(&tmp->lock);
	tmp->eventcnt = 1;

	AST_LIST_HEAD_INIT_NOLOCK(&tmp->id_queue);

	if (!(tmp->sched_heap = ast_heap_create(8, sched_time_cmp,
			offsetof(struct sched, __heap_index)))) {
		ast_sched_context_destroy(tmp);
		return NULL;
	}

	return tmp;
}

static void sched_free(struct sched *task)
{
	/* task->sched_id will be NULL most of the time, but when the
	 * scheduler context shuts down, it will free all scheduled
	 * tasks, and in that case, the task->sched_id will be non-NULL
	 */
	ast_free(task->sched_id);
	ast_cond_destroy(&task->cond);
	ast_free(task);
}

void ast_sched_context_destroy(struct ast_sched_context *con)
{
	struct sched *s;
	struct sched_id *sid;

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

	while ((sid = AST_LIST_REMOVE_HEAD(&con->id_queue, list))) {
		ast_free(sid);
	}

	ast_mutex_unlock(&con->lock);
	ast_mutex_destroy(&con->lock);

	ast_free(con);
}

#define ID_QUEUE_INCREMENT 16

/*!
 * \brief Add new scheduler IDs to the queue.
 *
 * \retval The number of IDs added to the queue
 */
static int add_ids(struct ast_sched_context *con)
{
	int new_size;
	int original_size;
	int i;

	original_size = con->id_queue_size;
	/* So we don't go overboard with the mallocs here, we'll just up
	 * the size of the list by a fixed amount each time instead of
	 * multiplying the size by any particular factor
	 */
	new_size = original_size + ID_QUEUE_INCREMENT;
	if (new_size < 0) {
		/* Overflow. Cap it at INT_MAX. */
		new_size = INT_MAX;
	}
	for (i = original_size; i < new_size; ++i) {
		struct sched_id *new_id;

		new_id = ast_calloc(1, sizeof(*new_id));
		if (!new_id) {
			break;
		}

		/*
		 * According to the API doxygen a sched ID of 0 is valid.
		 * Unfortunately, 0 was never returned historically and
		 * several users incorrectly coded usage of the returned
		 * sched ID assuming that 0 was invalid.
		 */
		new_id->id = ++con->id_queue_size;

		AST_LIST_INSERT_TAIL(&con->id_queue, new_id, list);
	}

	return con->id_queue_size - original_size;
}

static int set_sched_id(struct ast_sched_context *con, struct sched *new_sched)
{
	if (AST_LIST_EMPTY(&con->id_queue) && (add_ids(con) == 0)) {
		return -1;
	}

	new_sched->sched_id = AST_LIST_REMOVE_HEAD(&con->id_queue, list);
	return 0;
}

static void sched_release(struct ast_sched_context *con, struct sched *tmp)
{
	if (tmp->sched_id) {
		AST_LIST_INSERT_TAIL(&con->id_queue, tmp->sched_id, list);
		tmp->sched_id = NULL;
	}

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
		if (!tmp) {
			return NULL;
		}
		ast_cond_init(&tmp->cond, NULL);
	}

	if (set_sched_id(con, tmp)) {
		sched_release(con, tmp);
		return NULL;
	}

	return tmp;
}

void ast_sched_clean_by_callback(struct ast_sched_context *con, ast_sched_cb match, ast_sched_cb cleanup_cb)
{
	int i = 1;
	struct sched *current;

	ast_mutex_lock(&con->lock);
	while ((current = ast_heap_peek(con->sched_heap, i))) {
		if (current->callback != match) {
			i++;
			continue;
		}

		ast_heap_remove(con->sched_heap, current);

		cleanup_cb(current->data);
		sched_release(con, current);
	}
	ast_mutex_unlock(&con->lock);
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
	size_t size;

	size = ast_heap_size(con->sched_heap);

	/* Record the largest the scheduler heap became for reporting purposes. */
	if (con->highwater <= size) {
		con->highwater = size + 1;
	}

	/* Determine the tie breaker value for the new entry. */
	if (size) {
		++con->tie_breaker;
	} else {
		/*
		 * Restart the sequence for the first entry to make integer
		 * roll over more unlikely.
		 */
		con->tie_breaker = 0;
	}
	s->tie_breaker = con->tie_breaker;

	ast_heap_push(con->sched_heap, s);
}

/*! \brief
 * given the last event *tv and the offset in milliseconds 'when',
 * computes the next value,
 */
static void sched_settime(struct timeval *t, int when)
{
	struct timeval now = ast_tvnow();

	if (when < 0) {
		/*
		 * A negative when value is likely a bug as it
		 * represents a VERY large timeout time.
		 */
		ast_log(LOG_WARNING,
			"Bug likely: Negative time interval %d (interpreted as %u ms) requested!\n",
			when, (unsigned int) when);
		ast_assert(0);
	}

	/*ast_debug(1, "TV -> %lu,%lu\n", tv->tv_sec, tv->tv_usec);*/
	if (ast_tvzero(*t))	/* not supplied, default to now */
		*t = now;
	*t = ast_tvadd(*t, ast_samp2tv(when, 1000));
	if (ast_tvcmp(*t, now) < 0) {
		*t = now;
	}
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
		con->eventcnt++;
		tmp->callback = callback;
		tmp->data = data;
		tmp->resched = when;
		tmp->variable = variable;
		tmp->when = ast_tv(0, 0);
		tmp->deleted = 0;

		sched_settime(&tmp->when, when);
		schedule(con, tmp);
		res = tmp->sched_id->id;
	}
#ifdef DUMP_SCHEDULER
	/* Dump contents of the context while we have the lock so nothing gets screwed up by accident. */
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

static struct sched *sched_find(struct ast_sched_context *con, int id)
{
	int x;
	size_t heap_size;

	heap_size = ast_heap_size(con->sched_heap);
	for (x = 1; x <= heap_size; x++) {
		struct sched *cur = ast_heap_peek(con->sched_heap, x);

		if (cur->sched_id->id == id) {
			return cur;
		}
	}

	return NULL;
}

const void *ast_sched_find_data(struct ast_sched_context *con, int id)
{
	struct sched *s;
	const void *data = NULL;

	ast_mutex_lock(&con->lock);

	s = sched_find(con, id);
	if (s) {
		data = s->data;
	}

	ast_mutex_unlock(&con->lock);

	return data;
}

/*! \brief
 * Delete the schedule entry with number
 * "id".  It's nearly impossible that there
 * would be two or more in the list with that
 * id.
 * Deprecated in favor of ast_sched_del_nonrunning
 * which checks running event status.
 */
int ast_sched_del(struct ast_sched_context *con, int id)
{
	return ast_sched_del_nonrunning(con, id) ? -1 : 0;
}

/*! \brief
 * Delete the schedule entry with number "id".
 * If running, wait for the task to complete,
 * check to see if it is rescheduled then
 * schedule the release.
 * It's nearly impossible that there would be
 * two or more in the list with that id.
 */
int ast_sched_del_nonrunning(struct ast_sched_context *con, int id)
{
	struct sched *s = NULL;
	int *last_id = ast_threadstorage_get(&last_del_id, sizeof(int));
	int res = 0;

	DEBUG(ast_debug(1, "ast_sched_del(%d)\n", id));

	if (id < 0) {
		return 0;
	}

	ast_mutex_lock(&con->lock);

	s = sched_find(con, id);
	if (s) {
		if (!ast_heap_remove(con->sched_heap, s)) {
			ast_log(LOG_WARNING,"sched entry %d not in the sched heap?\n", s->sched_id->id);
		}
		sched_release(con, s);
	} else if (con->currently_executing && (id == con->currently_executing->sched_id->id)) {
		if (con->executing_thread_id == pthread_self()) {
			/* The scheduled callback is trying to delete itself.
			 * Not good as that is a deadlock. */
			ast_log(LOG_ERROR,
				"BUG! Trying to delete sched %d from within the callback %p.  "
				"Ignoring so we don't deadlock\n",
				id, con->currently_executing->callback);
			ast_log_backtrace();
			/* We'll return -1 below because s is NULL.
			 * The caller will rightly assume that the unscheduling failed. */
		} else {
			s = con->currently_executing;
			s->deleted = 1;
			/* Wait for executing task to complete so that the caller of
			 * ast_sched_del() does not free memory out from under the task. */
			while (con->currently_executing && (id == con->currently_executing->sched_id->id)) {
				ast_cond_wait(&s->cond, &con->lock);
			}
			/* This is not rescheduled so the caller of ast_sched_del_nonrunning needs to know
			 * that it was still deleted
			 */
			if (!s->rescheduled) {
				res = -2;
			}
			/* ast_sched_runq knows we are waiting on this item and is passing responsibility for
			 * its destruction to us
			 */
			sched_release(con, s);
			s = NULL;
		}
	}

#ifdef DUMP_SCHEDULER
	/* Dump contents of the context while we have the lock so nothing gets screwed up by accident. */
	ast_sched_dump(con);
#endif
	if (con->sched_thread) {
		ast_cond_signal(&con->sched_thread->cond);
	}
	ast_mutex_unlock(&con->lock);

	if(res == -2){
		return res;
	}
	else if (!s && *last_id != id) {
		ast_debug(1, "Attempted to delete nonexistent schedule entry %d!\n", id);
		/* Removing nonexistent schedule entry shouldn't trigger assert (it was enabled in DEV_MODE);
		 * because in many places entries is deleted without having valid id. */
		*last_id = id;
		return -1;
	} else if (!s) {
		return -1;
	}

	return res;
}

void ast_sched_report(struct ast_sched_context *con, struct ast_str **buf, struct ast_cb_names *cbnames)
{
	int i, x;
	struct sched *cur;
	int countlist[cbnames->numassocs + 1];
	size_t heap_size;

	memset(countlist, 0, sizeof(countlist));
	ast_str_set(buf, 0, " Highwater = %u\n schedcnt = %zu\n", con->highwater, ast_heap_size(con->sched_heap));

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
	struct timeval when;
	int x;
	size_t heap_size;

	if (!DEBUG_ATLEAST(1)) {
		return;
	}

	when = ast_tvnow();
#ifdef SCHED_MAX_CACHE
	ast_log(LOG_DEBUG, "Asterisk Schedule Dump (%zu in Q, %u Total, %u Cache, %u high-water)\n",
		ast_heap_size(con->sched_heap), con->eventcnt - 1, con->schedccnt, con->highwater);
#else
	ast_log(LOG_DEBUG, "Asterisk Schedule Dump (%zu in Q, %u Total, %u high-water)\n",
		ast_heap_size(con->sched_heap), con->eventcnt - 1, con->highwater);
#endif

	ast_log(LOG_DEBUG, "=============================================================\n");
	ast_log(LOG_DEBUG, "|ID    Callback          Data              Time  (sec:ms)   |\n");
	ast_log(LOG_DEBUG, "+-----+-----------------+-----------------+-----------------+\n");
	ast_mutex_lock(&con->lock);
	heap_size = ast_heap_size(con->sched_heap);
	for (x = 1; x <= heap_size; x++) {
		struct timeval delta;
		q = ast_heap_peek(con->sched_heap, x);
		delta = ast_tvsub(q->when, when);
		ast_log(LOG_DEBUG, "|%.4d | %-15p | %-15p | %.6ld : %.6ld |\n",
			q->sched_id->id,
			q->callback,
			q->data,
			(long)delta.tv_sec,
			(long int)delta.tv_usec);
	}
	ast_mutex_unlock(&con->lock);
	ast_log(LOG_DEBUG, "=============================================================\n");
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

		/*
		 * At this point, the schedule queue is still intact.  We
		 * have removed the first event and the rest is still there,
		 * so it's permissible for the callback to add new events, but
		 * trying to delete itself won't work because it isn't in
		 * the schedule queue.  If that's what it wants to do, it
		 * should return 0.
		 */

		con->currently_executing = current;
		con->executing_thread_id = pthread_self();
		ast_mutex_unlock(&con->lock);
		res = current->callback(current->data);
		ast_mutex_lock(&con->lock);
		con->currently_executing = NULL;
		ast_cond_signal(&current->cond);

		if (current->deleted) {
			/*
			 * Another thread is waiting on this scheduled item.  That thread
			 * will be responsible for it's destruction
			 */
			current->rescheduled = res ? 1 : 0;
		} else if (res) {
			/*
			 * If they return non-zero, we should schedule them to be
			 * run again.
			 */
			sched_settime(&current->when, current->variable ? res : current->resched);
			schedule(con, current);
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
	struct sched *s;
	long secs = -1;
	DEBUG(ast_debug(1, "ast_sched_when()\n"));

	ast_mutex_lock(&con->lock);

	s = sched_find(con, id);
	if (s) {
		struct timeval now = ast_tvnow();
		secs = s->when.tv_sec - now.tv_sec;
	}

	ast_mutex_unlock(&con->lock);

	return secs;
}
