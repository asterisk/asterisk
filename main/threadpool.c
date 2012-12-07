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

/*!
 * \brief An opaque threadpool structure
 *
 * A threadpool is a collection of threads that execute
 * tasks from a common queue.
 */
struct ast_threadpool {
	/*! Threadpool listener */
	struct ast_threadpool_listener *listener;
	/*! 
	 * \brief The container of active threads.
	 * Active threads are those that are currently running tasks
	 */
	struct ao2_container *active_threads;
	/*! 
	 * \brief The container of idle threads.
	 * Idle threads are those that are currenly waiting to run tasks
	 */
	struct ao2_container *idle_threads;
	/*! 
	 * \brief The main taskprocessor
	 * 
	 * Tasks that are queued in this taskprocessor are
	 * doled out to the worker threads. Worker threads that
	 * execute tasks from the threadpool are executing tasks
	 * in this taskprocessor.
	 *
	 * The threadpool itself is actually the private data for
	 * this taskprocessor's listener. This way, as taskprocessor
	 * changes occur, the threadpool can alert its listeners
	 * appropriately.
	 */
	struct ast_taskprocessor *tps;
	/*!
	 * \brief The control taskprocessor
	 *
	 * This is a standard taskprocessor that uses the default
	 * taskprocessor listener. In other words, all tasks queued to
	 * this taskprocessor have a single thread that executes the
	 * tasks.
	 *
	 * All tasks that modify the state of the threadpool and all tasks
	 * that call out to threadpool listeners are pushed to this
	 * taskprocessor.
	 *
	 * For instance, when the threadpool changes sizes, a task is put
	 * into this taskprocessor to do so. When it comes time to tell the
	 * threadpool listener that worker threads have changed state,
	 * the task is placed in this taskprocessor.
	 *
	 * This is done for three main reasons
	 * 1) It ensures that listeners are given an accurate portrayal
	 * of the threadpool's current state. In other words, when a listener
	 * gets told a count of active and idle threads, it does not
	 * need to worry that internal state of the threadpool might be different
	 * from what it has been told.
	 * 2) It minimizes the locking required in both the threadpool and in
	 * threadpool listener's callbacks.
	 * 3) It ensures that listener callbacks are called in the same order
	 * that the threadpool had its state change.
	 */
	struct ast_taskprocessor *control_tps;
};

/*!
 * \brief states for worker threads
 */
enum worker_state {
	/*! The worker is either active or idle */
	ALIVE,
	/*! The worker has been asked to shut down. */
	DEAD,
};

/* Worker thread forward declarations. See definitions for documentation */
struct worker_thread;
static int worker_thread_hash(const void *obj, int flags);
static int worker_thread_cmp(void *obj, void *arg, int flags);
static void worker_thread_destroy(void *obj);
static void worker_active(struct worker_thread *worker);
static void *worker_start(void *arg);
static struct worker_thread *worker_thread_alloc(struct ast_threadpool *pool);
static int worker_idle(struct worker_thread *worker);
static void worker_set_state(struct worker_thread *worker, enum worker_state state);
static void worker_shutdown(struct worker_thread *worker);

/*!
 * \brief Notify the threadpool listener that the state has changed.
 *
 * This notifies the threadpool listener via its state_changed callback.
 * \param pool The threadpool whose state has changed
 */
static void threadpool_send_state_changed(struct ast_threadpool *pool)
{
	int active_size = ao2_container_count(pool->active_threads);
	int idle_size = ao2_container_count(pool->idle_threads);

	pool->listener->callbacks->state_changed(pool, pool->listener, active_size, idle_size);
}

/*!
 * \brief Struct used for queued operations involving worker state changes
 */
struct thread_worker_pair {
	/*! Threadpool that contains the worker whose state has changed */
	struct ast_threadpool *pool;
	/*! Worker whose state has changed */
	struct worker_thread *worker;
};

/*!
 * \brief Destructor for thread_worker_pair
 */
static void thread_worker_pair_destructor(void *obj)
{
	struct thread_worker_pair *pair = obj;
	ao2_ref(pair->worker, -1);
}

/*!
 * \brief Allocate and initialize a thread_worker_pair
 * \param pool Threadpool to assign to the thread_worker_pair
 * \param worker Worker thread to assign to the thread_worker_pair
 */
static struct thread_worker_pair *thread_worker_pair_alloc(struct ast_threadpool *pool,
		struct worker_thread *worker)
{
	struct thread_worker_pair *pair = ao2_alloc(sizeof(*pair), thread_worker_pair_destructor);
	if (!pair) {
		return NULL;
	}
	pair->pool = pool;
	ao2_ref(worker, +1);
	pair->worker = worker;
	return pair;
}

/*!
 * \brief Move a worker thread from the active container to the idle container.
 *
 * This function is called from the threadpool's control taskprocessor thread.
 * \param data A thread_worker_pair containing the threadpool and the worker to move.
 * \return 0
 */
static int queued_active_thread_idle(void *data)
{
	struct thread_worker_pair *pair = data;

	ao2_link(pair->pool->idle_threads, pair->worker);
	ao2_unlink(pair->pool->active_threads, pair->worker);

	threadpool_send_state_changed(pair->pool);

	ao2_ref(pair, -1);
	return 0;
}

/*!
 * \brief Queue a task to move a thread from the active list to the idle list
 *
 * This is called by a worker thread when it runs out of tasks to perform and
 * goes idle.
 * \param pool The threadpool to which the worker belongs
 * \param worker The worker thread that has gone idle
 */
static void threadpool_active_thread_idle(struct ast_threadpool *pool,
		struct worker_thread *worker)
{
	struct thread_worker_pair *pair = thread_worker_pair_alloc(pool, worker);
	if (!pair) {
		return;
	}
	ast_taskprocessor_push(pool->control_tps, queued_active_thread_idle, pair);
}

/*!
 * \brief Execute a task in the threadpool
 * 
 * This is the function that worker threads call in order to execute tasks
 * in the threadpool
 *
 * \param pool The pool to which the tasks belong.
 * \retval 0 Either the pool has been shut down or there are no tasks.
 * \retval 1 There are still tasks remaining in the pool.
 */
static int threadpool_execute(struct ast_threadpool *pool)
{
	return ast_taskprocessor_execute(pool->tps);
}

/*!
 * \brief Destroy a threadpool's components.
 *
 * This is the destructor called automatically when the threadpool's
 * reference count reaches zero. This is not to be confused with
 * threadpool_destroy.
 *
 * By the time this actually gets called, most of the cleanup has already
 * been done in the pool. The only thing left to do is to release the
 * final reference to the threadpool listener.
 *
 * \param obj The pool to destroy
 */
static void threadpool_destructor(void *obj)
{
	struct ast_threadpool *pool = obj;
	ao2_cleanup(pool->listener);
}

/*
 * \brief Allocate a threadpool
 *
 * This is implemented as a taskprocessor listener's alloc callback. This
 * is because the threadpool exists as the private data on a taskprocessor
 * listener.
 *
 * \param listener The taskprocessor listener where the threadpool will live.
 * \retval NULL Could not initialize threadpool properly
 * \retval non-NULL The newly-allocated threadpool
 */
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

	ao2_ref(pool, +1);
	return pool;
}

static int threadpool_tps_start(struct ast_taskprocessor_listener *listener)
{
	return 0;
}

/*!
 * \brief helper used for queued task when tasks are pushed
 */
struct task_pushed_data {
	/*! Pool into which a task was pushed */
	struct ast_threadpool *pool;
	/*! Indicator of whether the pool had no tasks prior to the new task being added */
	int was_empty;
};

/*!
 * \brief Allocate and initialize a task_pushed_data
 * \param pool The threadpool to set in the task_pushed_data
 * \param was_empty The was_empty value to set in the task_pushed_data
 * \retval NULL Unable to allocate task_pushed_data
 * \retval non-NULL The newly-allocated task_pushed_data
 */
static struct task_pushed_data *task_pushed_data_alloc(struct ast_threadpool *pool,
		int was_empty)
{
	struct task_pushed_data *tpd = ao2_alloc(sizeof(*tpd), NULL);

	if (!tpd) {
		return NULL;
	}
	tpd->pool = pool;
	tpd->was_empty = was_empty;
	return tpd;
}

/*!
 * \brief Activate idle threads
 *
 * This function always returns CMP_MATCH because all threads that this
 * function acts on need to be seen as matches so they are unlinked from the
 * list of idle threads.
 *
 * Called as an ao2_callback in the threadpool's control taskprocessor thread.
 * \param obj The worker to activate
 * \param arg The pool where the worker belongs
 * \retval CMP_MATCH
 */
static int activate_threads(void *obj, void *arg, int flags)
{
	struct worker_thread *worker = obj;
	struct ast_threadpool *pool = arg;

	ao2_link(pool->active_threads, worker);
	worker_set_state(worker, ALIVE);
	return CMP_MATCH;
}

/*!
 * \brief Queue task called when tasks are pushed into the threadpool
 *
 * This function first calls into the threadpool's listener to let it know
 * that a task has been pushed. It then wakes up all idle threads and moves
 * them into the active thread container.
 * \param data A task_pushed_data
 * \return 0
 */
static int handle_task_pushed(void *data)
{
	struct task_pushed_data *tpd = data;
	struct ast_threadpool *pool = tpd->pool;
	int was_empty = tpd->was_empty;

	pool->listener->callbacks->task_pushed(pool, pool->listener, was_empty);
	ao2_callback(pool->idle_threads, OBJ_UNLINK | OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE,
			activate_threads, pool);
	ao2_ref(tpd, -1);
	return 0;
}

/*!
 * \brief Taskprocessor listener callback called when a task is added
 *
 * The threadpool uses this opportunity to queue a task on its control taskprocessor
 * in order to activate idle threads and notify the threadpool listener that the
 * task has been pushed.
 * \param listener The taskprocessor listener. The threadpool is the listener's private data
 * \param was_empty True if the taskprocessor was empty prior to the task being pushed
 */
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

/*!
 * \brief Queued task that handles the case where the threadpool's taskprocessor is emptied
 *
 * This simply lets the threadpool's listener know that the threadpool is devoid of tasks
 * \param data The pool that has become empty
 * \return 0
 */
static int handle_emptied(void *data)
{
	struct ast_threadpool *pool = data;

	pool->listener->callbacks->emptied(pool, pool->listener);
	return 0;
}

/*!
 * \brief Taskprocessor listener emptied callback
 *
 * The threadpool queues a task to let the threadpool listener know that
 * the threadpool no longer contains any tasks.
 * \param listener The taskprocessor listener. The threadpool is the listener's private data.
 */
static void threadpool_tps_emptied(struct ast_taskprocessor_listener *listener)
{
	struct ast_threadpool *pool = listener->private_data;

	ast_taskprocessor_push(pool->control_tps, handle_emptied, pool);
}

/*!
 * \brief Taskprocessor listener shutdown callback
 *
 * The threadpool will shut down and destroy all of its worker threads when
 * this is called back. By the time this gets called, the taskprocessor's
 * control taskprocessor has already been destroyed. Therefore there is no risk
 * in outright destroying the worker threads here.
 * \param listener The taskprocessor listener. The threadpool is the listener's private data.
 */
static void threadpool_tps_shutdown(struct ast_taskprocessor_listener *listener)
{
	struct ast_threadpool *pool = listener->private_data;

	ao2_cleanup(pool->active_threads);
	ao2_cleanup(pool->idle_threads);
}

/*!
 * \brief Taskprocessor listener destroy callback
 *
 * Since the threadpool is an ao2 object, all that is necessary is to
 * decrease the refcount. Since the control taskprocessor should already
 * be destroyed by this point, this should be the final reference to the
 * threadpool.
 *
 * \param private_data The threadpool to destroy
 */
static void threadpool_destroy(void *private_data)
{
	struct ast_threadpool *pool = private_data;
	ao2_cleanup(pool);
}

/*!
 * \brief Table of taskprocessor listener callbacks for threadpool's main taskprocessor
 */
static struct ast_taskprocessor_listener_callbacks threadpool_tps_listener_callbacks = {
	.alloc = threadpool_alloc,
	.start = threadpool_tps_start,
	.task_pushed = threadpool_tps_task_pushed,
	.emptied = threadpool_tps_emptied,
	.shutdown = threadpool_tps_shutdown,
	.destroy = threadpool_destroy,
};

/*!
 * \brief Add threads to the threadpool
 *
 * This function is called from the threadpool's control taskprocessor thread.
 * \param pool The pool that is expanding
 * \delta The number of threads to add to the pool
 */
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

/*!
 * \brief ao2 callback to kill a set number of threads.
 *
 * Threads will be unlinked from the container as long as the
 * counter has not reached zero. The counter is decremented with
 * each thread that is removed.
 * \param obj The worker thread up for possible destruction
 * \param arg The counter
 * \param flags Unused
 * \retval CMP_MATCH The counter has not reached zero, so this flag should be removed.
 * \retval CMP_STOP The counter has reached zero so no more threads should be removed.
 */
static int kill_threads(void *obj, void *arg, int flags)
{
	int *num_to_kill = arg;

	if ((*num_to_kill)-- > 0) {
		return CMP_MATCH;
	} else {
		return CMP_STOP;
	}
}

/*!
 * \brief Remove threads from the threadpool
 *
 * The preference is to kill idle threads. However, if there are
 * more threads to remove than there are idle threads, then active
 * threads will be removed too.
 *
 * This function is called from the threadpool control taskprocessor thread.
 *
 * \param pool The threadpool to remove threads from
 * \param delta The number of threads to remove
 */
static void shrink(struct ast_threadpool *pool, int delta)
{
	int idle_threads = ao2_container_count(pool->idle_threads);
	int idle_threads_to_kill = MIN(delta, idle_threads);
	int active_threads_to_kill = delta - idle_threads_to_kill;

	ao2_callback(pool->idle_threads, OBJ_UNLINK | OBJ_NOLOCK,
			kill_threads, &idle_threads_to_kill);

	ao2_callback(pool->active_threads, OBJ_UNLINK | OBJ_NOLOCK,
			kill_threads, &active_threads_to_kill);
}

/*!
 * \brief Helper struct used for queued operations that change the size of the threadpool
 */
struct set_size_data {
	/*! The pool whose size is to change */
	struct ast_threadpool *pool;
	/*! The requested new size of the pool */
	unsigned int size;
};

/*!
 * \brief Allocate and initialize a set_size_data
 * \param pool The pool for the set_size_data
 * \param size The size to store in the set_size_data
 */
static struct set_size_data *set_size_data_alloc(struct ast_threadpool *pool,
		unsigned int size)
{
	struct set_size_data *ssd = ao2_alloc(sizeof(*ssd), NULL);
	if (!ssd) {
		return NULL;
	}

	ssd->pool = pool;
	ssd->size = size;
	return ssd;
}

/*!
 * \brief Change the size of the threadpool
 *
 * This can either result in shrinking or growing the threadpool depending
 * on the new desired size and the current size.
 *
 * This function is run from the threadpool control taskprocessor thread
 *
 * \param data A set_size_data used for determining how to act
 * \return 0
 */
static int queued_set_size(void *data)
{
	struct set_size_data *ssd = data;
	struct ast_threadpool *pool = ssd->pool;
	unsigned int new_size = ssd->size;
	unsigned int current_size = ao2_container_count(pool->active_threads) +
		ao2_container_count(pool->idle_threads);

	if (current_size == new_size) {
		ast_log(LOG_NOTICE, "Not changing threadpool size since new size %u is the same as current %u\n",
				new_size, current_size);
		return 0;
	}

	if (current_size < new_size) {
		grow(pool, new_size - current_size);
	} else {
		shrink(pool, current_size - new_size);
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

static void listener_destructor(void *obj)
{
	struct ast_threadpool_listener *listener = obj;

	listener->callbacks->destroy(listener->private_data);
}

struct ast_threadpool_listener *ast_threadpool_listener_alloc(
		const struct ast_threadpool_listener_callbacks *callbacks)
{
	struct ast_threadpool_listener *listener = ao2_alloc(sizeof(*listener), listener_destructor);
	if (!listener) {
		return NULL;
	}
	listener->callbacks = callbacks;
	listener->private_data = listener->callbacks->alloc(listener);
	if (!listener->private_data) {
		ao2_ref(listener, -1);
		return NULL;
	}
	return listener;
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
	pool->tps = tps;
	ast_log(LOG_NOTICE, "The taskprocessor I've created is located at %p\n", pool->tps);
	ao2_ref(listener, +1);
	pool->listener = listener;
	ast_threadpool_set_size(pool, initial_size);
	return pool;
}

int ast_threadpool_push(struct ast_threadpool *pool, int (*task)(void *data), void *data)
{
	return ast_taskprocessor_push(pool->tps, task, data);
}

void ast_threadpool_shutdown(struct ast_threadpool *pool)
{
	/* Shut down the taskprocessors and everything else just
	 * takes care of itself via the taskprocessor callbacks
	 */
	ast_taskprocessor_unreference(pool->control_tps);
	ast_taskprocessor_unreference(pool->tps);
}

/*!
 * A thread that executes threadpool tasks
 */
struct worker_thread {
	/*! A unique (within a run of Asterisk) ID for the thread. Used for hashing and searching */
	int id;
	/*! Condition used in conjunction with state changes */
	ast_cond_t cond;
	/*! Lock used alongside the condition for state changes */
	ast_mutex_t lock;
	/*! The actual thread that is executing tasks */
	pthread_t thread;
	/*! A pointer to the threadpool. Needed to be able to execute tasks */
	struct ast_threadpool *pool;
	/*! The current state of the worker thread */
	enum worker_state state;
	/*! A boolean used to determine if an idle thread should become active */
	int wake_up;
};

/*!
 * A monotonically increasing integer used for worker
 * thread identification.
 */
static int worker_id_counter;

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

/*!
 * \brief shut a worker thread down
 *
 * Set the worker dead and then wait for its thread
 * to finish executing.
 *
 * \param worker The worker thread to shut down
 */
static void worker_shutdown(struct worker_thread *worker)
{
	worker_set_state(worker, DEAD);
	if (worker->thread != AST_PTHREADT_NULL) {
		pthread_join(worker->thread, NULL);
		worker->thread = AST_PTHREADT_NULL;
	}
}

/*!
 * \brief Worker thread destructor
 *
 * Called automatically when refcount reaches 0. Shuts
 * down the worker thread and destroys its component
 * parts
 */
static void worker_thread_destroy(void *obj)
{
	struct worker_thread *worker = obj;
	worker_shutdown(worker);
	ast_mutex_destroy(&worker->lock);
	ast_cond_destroy(&worker->cond);
}

/*!
 * \brief start point for worker threads
 *
 * Worker threads start in the active state but may
 * immediately go idle if there is no work to be
 * done
 *
 * \param arg The worker thread
 * \retval NULL
 */
static void *worker_start(void *arg)
{
	struct worker_thread *worker = arg;

	worker_active(worker);
	return NULL;
}

/*!
 * \brief Allocate and initialize a new worker thread
 *
 * This will create, initialize, and start the thread.
 *
 * \param pool The threadpool to which the worker will be added
 * \retval NULL Failed to allocate or start the worker thread
 * \retval non-NULL The newly-created worker thread
 */
static struct worker_thread *worker_thread_alloc(struct ast_threadpool *pool)
{
	struct worker_thread *worker = ao2_alloc(sizeof(*worker), worker_thread_destroy);
	if (!worker) {
		return NULL;
	}
	worker->id = ast_atomic_fetchadd_int(&worker_id_counter, 1);
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

/*!
 * \brief Active loop for worker threads
 *
 * The worker will stay in this loop for its lifetime,
 * executing tasks as they become available. If there
 * are no tasks currently available, then the thread
 * will go idle.
 *
 * \param worker The worker thread executing tasks.
 */
static void worker_active(struct worker_thread *worker)
{
	int alive = 1;
	while (alive) {
		if (threadpool_execute(worker->pool) == 0) {
			alive = worker_idle(worker);
		}
	}
}

/*!
 * \brief Idle function for worker threads
 *
 * The worker waits here until it gets told by the threadpool
 * to wake up.
 *
 * \param worker The idle worker
 * \retval 0 The thread is being woken up so that it can conclude.
 * \retval non-zero The thread is being woken up to do more work.
 */
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

/*!
 * \brief Change a worker's state
 *
 * The threadpool calls into this function in order to let a worker know
 * how it should proceed.
 */
static void worker_set_state(struct worker_thread *worker, enum worker_state state)
{
	SCOPED_MUTEX(lock, &worker->lock);
	worker->state = state;
	worker->wake_up = 1;
	ast_cond_signal(&worker->cond);
}

