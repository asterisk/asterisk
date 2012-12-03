/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Mark Michelson <mmmichelson@digium.com>
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


#include "asterisk.h"

#include "asterisk/threadpool.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"

#define THREAD_BUCKETS 89

static int id_counter;

struct ast_threadpool {
	struct ast_threadpool_listener *listener;
	struct ao2_container *active_threads;
	struct ao2_container *idle_threads;
	struct ao2_container *zombie_threads;
	struct ast_taskprocessor *tps;
	struct ast_taskprocessor *control_tps;
};

enum worker_state {
	ALIVE,
	ZOMBIE,
	DEAD,
};

struct worker_thread {
	int id;
	ast_cond_t cond;
	ast_mutex_t lock;
	pthread_t thread;
	struct ast_threadpool *pool;
	enum worker_state state;
	int wake_up;
};

static int worker_thread_hash(const void *obj, int flags)
{
	const struct worker_thread *worker = obj;

	return worker->id;
}

static int worker_thread_cmp(void *obj, void *arg, int flags)
{
	struct worker_thread *worker1 = obj;
	struct worker_thread *worker2 = arg;

	return worker1->id == worker2->id ? CMP_MATCH : 0;
}

static void worker_thread_destroy(void *obj)
{
	struct worker_thread *worker = obj;
	ast_mutex_destroy(&worker->lock);
	ast_cond_destroy(&worker->cond);
}

static int worker_active(struct worker_thread *worker);

static void *worker_start(void *arg)
{
	struct worker_thread *worker = arg;

	worker_active(worker);
	return NULL;
}

static struct worker_thread *worker_thread_alloc(struct ast_threadpool *pool)
{
	struct worker_thread *worker = ao2_alloc(sizeof(*worker), worker_thread_destroy);
	if (!worker) {
		return NULL;
	}
	worker->id = ast_atomic_fetchadd_int(&id_counter, 1);
	ast_mutex_init(&worker->lock);
	ast_cond_init(&worker->cond, NULL);
	worker->pool = pool;
	worker->thread = AST_PTHREADT_NULL;
	worker->state = ALIVE;
	if (ast_pthread_create(&worker->thread, NULL, worker_start, worker) < 0) {
		ast_log(LOG_ERROR, "Unable to start worker thread!\n");
		ao2_ref(worker, -1);
		return NULL;
	}
	return worker;
}

static void threadpool_send_state_changed(struct ast_threadpool *pool)
{
	int active_size = ao2_container_count(pool->active_threads);
	int idle_size = ao2_container_count(pool->idle_threads);
	int zombie_size = ao2_container_count(pool->zombie_threads);

	pool->listener->callbacks->state_changed(pool->listener, active_size, idle_size, zombie_size);
}

struct thread_worker_pair {
	struct ast_threadpool *pool;
	struct worker_thread *worker;
};

static void thread_worker_pair_destructor(void *obj)
{
	struct thread_worker_pair *pair = obj;
	ao2_ref(pair->pool, -1);
	ao2_ref(pair->worker, -1);
}

static struct thread_worker_pair *thread_worker_pair_alloc(struct ast_threadpool *pool,
		struct worker_thread *worker)
{
	struct thread_worker_pair *pair = ao2_alloc(sizeof(*pair), thread_worker_pair_destructor);
	if (!pair) {
		return NULL;
	}
	ao2_ref(pool, +1);
	pair->pool = pool;
	ao2_ref(worker, +1);
	pair->worker = worker;
	return pair;
}

static int queued_active_thread_idle(void *data)
{
	struct thread_worker_pair *pair = data;

	ao2_link(pair->pool->idle_threads, pair->worker);
	ao2_unlink(pair->pool->active_threads, pair->worker);

	threadpool_send_state_changed(pair->pool);

	ao2_ref(pair, -1);
	return 0;
}

static void threadpool_active_thread_idle(struct ast_threadpool *pool,
		struct worker_thread *worker)
{
	struct thread_worker_pair *pair = thread_worker_pair_alloc(pool, worker);
	if (!pair) {
		return;
	}
	ast_taskprocessor_push(pool->control_tps, queued_active_thread_idle, pair);
}

static int queued_zombie_thread_dead(void *data)
{
	struct thread_worker_pair *pair = data;

	ao2_unlink(pair->pool->zombie_threads, pair->worker);
	threadpool_send_state_changed(pair->pool);

	ao2_ref(pair, -1);
	return 0;
}

static void threadpool_zombie_thread_dead(struct ast_threadpool *pool,
		struct worker_thread *worker)
{
	struct thread_worker_pair *pair = thread_worker_pair_alloc(pool, worker);
	if (!pair) {
		return;
	}
	ast_taskprocessor_push(pool->control_tps, queued_zombie_thread_dead, pair);
}

static int worker_idle(struct worker_thread *worker)
{
	SCOPED_MUTEX(lock, &worker->lock);
	if (worker->state != ALIVE) {
		return 0;
	}
	threadpool_active_thread_idle(worker->pool, worker);
	while (!worker->wake_up) {
		ast_cond_wait(&worker->cond, lock);
	}
	worker->wake_up = 0;
	return worker->state == ALIVE;
}

static int threadpool_execute(struct ast_threadpool *pool)
{
	return ast_taskprocessor_execute(pool->tps);
}

static int worker_active(struct worker_thread *worker)
{
	int alive = 1;
	while (alive) {
		if (threadpool_execute(worker->pool)) {
			alive = worker_idle(worker);
		}
	}

	/* Reaching this portion means the thread is
	 * on death's door. It may have been killed while
	 * it was idle, in which case it can just die
	 * peacefully. If it's a zombie, though, then
	 * it needs to let the pool know so
	 * that the thread can be removed from the
	 * list of zombie threads.
	 */
	if (worker->state == ZOMBIE) {
		threadpool_zombie_thread_dead(worker->pool, worker);
	}

	return 0;
}

static void worker_set_state(struct worker_thread *worker, enum worker_state state)
{
	SCOPED_MUTEX(lock, &worker->lock);
	worker->state = state;
	worker->wake_up = 1;
	ast_cond_signal(&worker->cond);
}

static int worker_shutdown(void *obj, void *arg, int flags)
{
	struct worker_thread *worker = obj;

	worker_set_state(worker, DEAD);
	if (worker->thread != AST_PTHREADT_NULL) {
		pthread_join(worker->thread, NULL);
		worker->thread = AST_PTHREADT_NULL;
	}
	return 0;
}

static void threadpool_destructor(void *private_data)
{
	struct ast_threadpool *pool = private_data;
	/* XXX Probably should let the listener know we're being destroyed? */

	/* Threads should all be shut down by now, so this should be a painless
	 * operation
	 */
	ao2_cleanup(pool->active_threads);
	ao2_cleanup(pool->idle_threads);
	ao2_cleanup(pool->zombie_threads);
	ao2_cleanup(pool->listener);
}

static void *threadpool_alloc(struct ast_taskprocessor_listener *listener)
{
	RAII_VAR(struct ast_threadpool *, pool,
			ao2_alloc(sizeof(*pool), threadpool_destructor), ao2_cleanup);

	pool->control_tps = ast_taskprocessor_get("CHANGE THIS", TPS_REF_DEFAULT);
	if (!pool->control_tps) {
		return NULL;
	}
	pool->active_threads = ao2_container_alloc(THREAD_BUCKETS, worker_thread_hash, worker_thread_cmp);
	if (!pool->active_threads) {
		return NULL;
	}
	pool->idle_threads = ao2_container_alloc(THREAD_BUCKETS, worker_thread_hash, worker_thread_cmp);
	if (!pool->idle_threads) {
		return NULL;
	}
	pool->zombie_threads = ao2_container_alloc(THREAD_BUCKETS, worker_thread_hash, worker_thread_cmp);
	if (!pool->zombie_threads) {
		return NULL;
	}

	pool->tps = listener->tps;

	ao2_ref(pool, +1);
	return pool;
}

struct task_pushed_data {
	struct ast_threadpool *pool;
	int was_empty;
};

static void task_pushed_data_destroy(void *obj)
{
	struct task_pushed_data *tpd = obj;
	ao2_ref(tpd->pool, -1);
}

static struct task_pushed_data *task_pushed_data_alloc(struct ast_threadpool *pool,
		int was_empty)
{
	struct task_pushed_data *tpd = ao2_alloc(sizeof(*tpd),
			task_pushed_data_destroy);

	if (!tpd) {
		return NULL;
	}
	ao2_ref(pool, +1);
	tpd->pool = pool;
	tpd->was_empty = was_empty;
	return tpd;
}

static int activate_threads(void *obj, void *arg, int flags)
{
	struct worker_thread *worker = obj;
	struct ast_threadpool *pool = arg;

	ao2_link(pool->active_threads, worker);
	worker_set_state(worker, ALIVE);
	return 0;
}

static int handle_task_pushed(void *data)
{
	struct task_pushed_data *tpd = data;
	struct ast_threadpool *pool = tpd->pool;
	int was_empty = tpd->was_empty;

	pool->listener->callbacks->tps_task_pushed(pool->listener, was_empty);
	ao2_callback(pool->idle_threads, OBJ_UNLINK, activate_threads, pool);
	ao2_ref(tpd, -1);
	return 0;
}

static void threadpool_tps_task_pushed(struct ast_taskprocessor_listener *listener,
		int was_empty)
{
	struct ast_threadpool *pool = listener->private_data;
	struct task_pushed_data *tpd = task_pushed_data_alloc(pool, was_empty);

	if (!tpd) {
		return;
	}

	ast_taskprocessor_push(pool->control_tps, handle_task_pushed, tpd);
}

static int handle_emptied(void *data)
{
	struct ast_threadpool *pool = data;

	pool->listener->callbacks->emptied(pool->listener);
	ao2_ref(pool, -1);
	return 0;
}

static void threadpool_tps_emptied(struct ast_taskprocessor_listener *listener)
{
	struct ast_threadpool *pool = listener->private_data;

	ao2_ref(pool, +1);
	ast_taskprocessor_push(pool->control_tps, handle_emptied, pool);
}

static void threadpool_tps_shutdown(struct ast_taskprocessor_listener *listener)
{
	/*
	 * The threadpool triggers the taskprocessor to shut down. As a result,
	 * we have the freedom of shutting things down in three stages:
	 *
	 * 1) Before the tasprocessor is shut down
	 * 2) During taskprocessor shutdown (here)
	 * 3) After taskprocessor shutdown
	 *
	 * In the spirit of the taskprocessor shutdown, this would be
	 * where we make sure that all the worker threads are no longer
	 * executing. We could just do this before we even shut down
	 * the taskprocessor, but this feels more "right".
	 */

	struct ast_threadpool *pool = listener->private_data;
	ao2_callback(pool->active_threads, 0, worker_shutdown, NULL);
	ao2_callback(pool->idle_threads, 0, worker_shutdown, NULL);
	ao2_callback(pool->zombie_threads, 0, worker_shutdown, NULL);
}

static void threadpool_destroy(void *private_data)
{
	struct ast_threadpool *pool = private_data;
	ao2_cleanup(pool);
}

static struct ast_taskprocessor_listener_callbacks threadpool_tps_listener_callbacks = {
	.alloc = threadpool_alloc,
	.task_pushed = threadpool_tps_task_pushed,
	.emptied = threadpool_tps_emptied,
	.shutdown = threadpool_tps_shutdown,
	.destroy = threadpool_destroy,
};

static void grow(struct ast_threadpool *pool, int delta)
{
	int i;
	for (i = 0; i < delta; ++i) {
		struct worker_thread *worker = worker_thread_alloc(pool);
		if (!worker) {
			return;
		}
		ao2_link(pool->active_threads, worker);
	}
}

static int kill_threads(void *obj, void *arg, int flags)
{
	struct worker_thread *worker = obj;
	int *num_to_kill = arg;

	if ((*num_to_kill)-- > 0) {
		worker_shutdown(worker, arg, flags);
		return CMP_MATCH;
	} else {
		return CMP_STOP;
	}
}

static int zombify_threads(void *obj, void *arg, void *data, int flags)
{
	struct worker_thread *worker = obj;
	struct ast_threadpool *pool = arg;
	int *num_to_zombify = data;

	if ((*num_to_zombify)-- > 0) {
		ao2_link(pool->zombie_threads, worker);
		worker_set_state(worker, ZOMBIE);
		return CMP_MATCH;
	} else {
		return CMP_STOP;
	}
}

static void shrink(struct ast_threadpool *pool, int delta)
{
	/* 
	 * Preference is to kill idle threads, but
	 * we'll move on to deactivating active threads
	 * if we have to
	 */
	int idle_threads = ao2_container_count(pool->idle_threads);
	int idle_threads_to_kill = MIN(delta, idle_threads);
	int active_threads_to_zombify = delta - idle_threads_to_kill;

	ao2_callback(pool->idle_threads, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE | OBJ_NOLOCK,
			kill_threads, &idle_threads_to_kill);

	ao2_callback_data(pool->active_threads, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE | OBJ_NOLOCK,
			zombify_threads, pool, &active_threads_to_zombify);
}

struct set_size_data {
	struct ast_threadpool *pool;
	unsigned int size;
};

static void set_size_data_destroy(void *obj)
{
	struct set_size_data *ssd = obj;
	ao2_ref(ssd->pool, -1);
}

static struct set_size_data *set_size_data_alloc(struct ast_threadpool *pool,
		unsigned int size)
{
	struct set_size_data *ssd = ao2_alloc(sizeof(*ssd), set_size_data_destroy);
	if (!ssd) {
		return NULL;
	}

	ao2_ref(pool, +1);
	ssd->pool = pool;
	ssd->size = size;
	return ssd;
}

static int queued_set_size(void *data)
{
	struct set_size_data *ssd = data;
	struct ast_threadpool *pool = ssd->pool;
	unsigned int num_threads = ssd->size;

	/* We don't count zombie threads as being "live when potentially resizing */
	unsigned int current_size = ao2_container_count(pool->active_threads) +
		ao2_container_count(pool->idle_threads);

	if (current_size == num_threads) {
		ast_log(LOG_NOTICE, "Not changing threadpool size since new size %u is the same as current %u\n",
				num_threads, current_size);
		return 0;
	}

	if (current_size < num_threads) {
		grow(pool, num_threads - current_size);
	} else {
		shrink(pool, current_size - num_threads);
	}

	threadpool_send_state_changed(pool);
	ao2_ref(ssd, -1);
	return 0;
}

void ast_threadpool_set_size(struct ast_threadpool *pool, unsigned int size)
{
	struct set_size_data *ssd;

	ssd = set_size_data_alloc(pool, size);
	if (!ssd) {
		return;
	}

	ast_taskprocessor_push(pool->control_tps, queued_set_size, ssd);
}

struct ast_threadpool *ast_threadpool_create(struct ast_threadpool_listener *listener, int initial_size)
{
	struct ast_threadpool *pool;
	struct ast_taskprocessor *tps;
	RAII_VAR(struct ast_taskprocessor_listener *, tps_listener,
			ast_taskprocessor_listener_alloc(&threadpool_tps_listener_callbacks),
			ao2_cleanup);

	if (!tps_listener) {
		return NULL;
	}

	tps = ast_taskprocessor_create_with_listener("XXX CHANGE THIS XXX", tps_listener);

	if (!tps) {
		return NULL;
	}

	pool = tps_listener->private_data;
	ast_threadpool_set_size(pool, initial_size);
	return pool;
}

void ast_threadpool_shutdown(struct ast_threadpool *pool)
{
	/* Pretty simple really. We just shut down the
	 * taskprocessors and everything else just
	 * takes care of itself via the taskprocessor callbacks
	 */
	ast_taskprocessor_unreference(pool->control_tps);
	ast_taskprocessor_unreference(pool->tps);
}
