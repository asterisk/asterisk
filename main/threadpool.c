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

struct ast_threadpool;

enum worker_state {
	ALIVE,
	ZOMBIE,
	DEAD,
};

struct worker_thread {
	ast_cond_t cond;
	ast_mutex_t lock;
	pthread_t thread;
	struct ast_threadpool *pool;
	AST_LIST_ENTRY(struct worker_thread) next;
	int wake_up;
	enum worker_state state;
};

static int worker_idle(struct worker_thread *worker)
{
	SCOPED_MUTEX(lock, &worker->lock);
	if (worker->state != ALIVE) {
		return false;
	}
	threadpool_active_thread_idle(worker->pool, worker);
	while (!worker->wake_up) {
		ast_cond_wait(&worker->cond, lock);
	}
	worker->wake_up = false;
	return worker->state == ALIVE;
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

struct ast_threadpool {
	struct ast_threadpool_listener *threadpool_listener;
	int active_threads;
	int idle_threads;
	int zombie_threads;
}

static void *threadpool_tps_listener_alloc(struct ast_taskprocessor_listener *listener)
{
	RAII_VAR(ast_threadpool *, threadpool,
			ao2_alloc(sizeof(*threadpool), threadpool_destroy), ao2_cleanup);

	return threadpool;
}

static void threadpool_tps_task_pushed(struct ast_taskprocessor_listener *listener)
{
	/* XXX stub */
}

static void threadpool_tps_emptied(struct ast_taskprocessor_listener *listener)
{
	/* XXX stub */
}

static void threadpool_tps_shutdown(struct ast_taskprocessor_listener *listener)
{
	/* XXX stub */
}

static void threadpool_tps_listener_destroy(struct ast_taskprocessor_listener *listener)
{
	/* XXX stub */
}

static struct ast_taskprocessor_listener_callbacks threadpool_tps_listener_callbacks = {
	.alloc = threadpool_tps_listener_alloc,
	.task_pushed = threadpool_tps_task_pushed,
	.emptied = threadpool_tps_emptied,
	.shutdown = threadpool_tps_shutdown,
	.destroy = threadpool_tps_listener_destroy,
};

/*!
 * \brief Allocate the taskprocessor to be used for the threadpool
 *
 * We use a custom taskprocessor listener. We allocate our custom
 * listener and then create a taskprocessor.
 */
static struct ast_taskprocessor_listener *threadpool_tps_alloc(void)
{
	RAII_VAR(struct threadpool_tps_listener *, tps_listener,
			ast_taskprocessor_listener_alloc(&threadpool_tps_listener_callbacks),
			ao2_cleanup);

	if (!tps_listener) {
		return NULL;
	}

	return ast_taskprocessor_create_with_listener(tps_listener);
}

void ast_threadpool_set_size(struct ast_threadpool *pool, int size)
{
}

struct ast_threadpool *ast_threadpool_create(struct ast_threadpool_listener *listener, int initial_size)
{
	struct ast_threadpool *pool;
	RAII_VAR(ast_taskprocessor *, tps, threadpool_tps_alloc(), ast_taskprocessor_unreference);

	if (!tps) {
		return NULL;
	}

	pool = tps->listener->private_data;
	pool->tps = tps;
	ast_threadpool_set_size(pool, initial_size);

	return pool;
}
