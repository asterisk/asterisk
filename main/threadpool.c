/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012-2013, Digium, Inc.
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

/* Needs to stay prime if increased */
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
	 * \brief The container of zombie threads.
	 * Zombie threads may be running tasks, but they are scheduled to die soon
	 */
	struct ao2_container *zombie_threads;
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
	 * gets told a count of active, idle and zombie threads, it does not
	 * need to worry that internal state of the threadpool might be different
	 * from what it has been told.
	 * 2) It minimizes the locking required in both the threadpool and in
	 * threadpool listener's callbacks.
	 * 3) It ensures that listener callbacks are called in the same order
	 * that the threadpool had its state change.
	 */
	struct ast_taskprocessor *control_tps;
	/*! True if the threadpool is in the process of shutting down */
	int shutting_down;
	/*! Threadpool-specific options */
	struct ast_threadpool_options options;
};

/*!
 * \brief listener for a threadpool
 *
 * The listener is notified of changes in a threadpool. It can
 * react by doing things like increasing the number of threads
 * in the pool
 */
struct ast_threadpool_listener {
	/*! Callbacks called by the threadpool */
	const struct ast_threadpool_listener_callbacks *callbacks;
	/*! User data for the listener */
	void *user_data;
};

/*!
 * \brief states for worker threads
 */
enum worker_state {
	/*! The worker is either active or idle */
	ALIVE,
	/*!
	 * The worker has been asked to shut down but
	 * may still be in the process of executing tasks.
	 * This transition happens when the threadpool needs
	 * to shrink and needs to kill active threads in order
	 * to do so.
	 */
	ZOMBIE,
	/*!
	 * The worker has been asked to shut down. Typically
	 * only idle threads go to this state directly, but
	 * active threads may go straight to this state when
	 * the threadpool is shut down.
	 */
	DEAD,
};

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
	/*! Options for this threadpool */
	struct ast_threadpool_options options;
};

/* Worker thread forward declarations. See definitions for documentation */
static int worker_thread_hash(const void *obj, int flags);
static int worker_thread_cmp(void *obj, void *arg, int flags);
static void worker_thread_destroy(void *obj);
static void worker_active(struct worker_thread *worker);
static void *worker_start(void *arg);
static struct worker_thread *worker_thread_alloc(struct ast_threadpool *pool);
static int worker_thread_start(struct worker_thread *worker);
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

	if (pool->listener && pool->listener->callbacks->state_changed) {
		pool->listener->callbacks->state_changed(pool, pool->listener, active_size, idle_size);
	}
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
	struct thread_worker_pair *pair;
	SCOPED_AO2LOCK(lock, pool);
	if (pool->shutting_down) {
		return;
	}
	pair = thread_worker_pair_alloc(pool, worker);
	if (!pair) {
		return;
	}
	ast_taskprocessor_push(pool->control_tps, queued_active_thread_idle, pair);
}

/*!
 * \brief Kill a zombie thread
 *
 * This runs from the threadpool's control taskprocessor thread.
 *
 * \param data A thread_worker_pair containing the threadpool and the zombie thread
 * \return 0
 */
static int queued_zombie_thread_dead(void *data)
{
	struct thread_worker_pair *pair = data;

	ao2_unlink(pair->pool->zombie_threads, pair->worker);
	threadpool_send_state_changed(pair->pool);

	ao2_ref(pair, -1);
	return 0;
}

/*!
 * \brief Queue a task to kill a zombie thread
 *
 * This is called by a worker thread when it acknowledges that it is time for
 * it to die.
 */
static void threadpool_zombie_thread_dead(struct ast_threadpool *pool,
		struct worker_thread *worker)
{
	struct thread_worker_pair *pair;
	SCOPED_AO2LOCK(lock, pool);
	if (pool->shutting_down) {
		return;
	}
	pair = thread_worker_pair_alloc(pool, worker);
	if (!pair) {
		return;
	}
	ast_taskprocessor_push(pool->control_tps, queued_zombie_thread_dead, pair);
}

static int queued_idle_thread_dead(void *data)
{
	struct thread_worker_pair *pair = data;

	ao2_unlink(pair->pool->idle_threads, pair->worker);
	threadpool_send_state_changed(pair->pool);

	ao2_ref(pair, -1);
	return 0;
}

static void threadpool_idle_thread_dead(struct ast_threadpool *pool,
		struct worker_thread *worker)
{
	struct thread_worker_pair *pair;
	SCOPED_AO2LOCK(lock, pool);
	if (pool->shutting_down) {
		return;
	}
	pair = thread_worker_pair_alloc(pool, worker);
	if (!pair) {
		return;
	}
	ast_taskprocessor_push(pool->control_tps, queued_idle_thread_dead, pair);
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
	ao2_lock(pool);
	if (!pool->shutting_down) {
		ao2_unlock(pool);
		return ast_taskprocessor_execute(pool->tps);
	}
	ao2_unlock(pool);
	return 0;
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
 * \param name The name of the threadpool.
 * \param options The options the threadpool uses.
 * \retval NULL Could not initialize threadpool properly
 * \retval non-NULL The newly-allocated threadpool
 */
static struct ast_threadpool *threadpool_alloc(const char *name, const struct ast_threadpool_options *options)
{
	RAII_VAR(struct ast_threadpool *, pool, NULL, ao2_cleanup);
	struct ast_str *control_tps_name;

	pool = ao2_alloc(sizeof(*pool), threadpool_destructor);
	control_tps_name = ast_str_create(64);
	if (!pool || !control_tps_name) {
		ast_free(control_tps_name);
		return NULL;
	}

	ast_str_set(&control_tps_name, 0, "%s-control", name);

	pool->control_tps = ast_taskprocessor_get(ast_str_buffer(control_tps_name), TPS_REF_DEFAULT);
	ast_free(control_tps_name);
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
	pool->options = *options;

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
 * This function always returns CMP_MATCH because all workers that this
 * function acts on need to be seen as matches so they are unlinked from the
 * list of idle threads.
 *
 * Called as an ao2_callback in the threadpool's control taskprocessor thread.
 * \param obj The worker to activate
 * \param arg The pool where the worker belongs
 * \retval CMP_MATCH
 */
static int activate_thread(void *obj, void *arg, int flags)
{
	struct worker_thread *worker = obj;
	struct ast_threadpool *pool = arg;

	if (!ao2_link(pool->active_threads, worker)) {
		/* If we can't link the idle thread into the active container, then
		 * we'll just leave the thread idle and not wake it up.
		 */
		ast_log(LOG_WARNING, "Failed to activate thread %d. Remaining idle\n",
				worker->id);
		return 0;
	}
	worker_set_state(worker, ALIVE);
	return CMP_MATCH;
}

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

	int current_size = ao2_container_count(pool->active_threads) +
		ao2_container_count(pool->idle_threads);

	if (pool->options.max_size && current_size + delta > pool->options.max_size) {
		delta = pool->options.max_size - current_size;
	}

	ast_debug(3, "Increasing threadpool %s's size by %d\n",
			ast_taskprocessor_name(pool->tps), delta);

	for (i = 0; i < delta; ++i) {
		struct worker_thread *worker = worker_thread_alloc(pool);
		if (!worker) {
			return;
		}
		if (ao2_link(pool->idle_threads, worker)) {
			if (worker_thread_start(worker)) {
				ast_log(LOG_ERROR, "Unable to start worker thread %d. Destroying.\n", worker->id);
				ao2_unlink(pool->active_threads, worker);
			}
		} else {
			ast_log(LOG_WARNING, "Failed to activate worker thread %d. Destroying.\n", worker->id);
		}
		ao2_ref(worker, -1);
	}
}

/*!
 * \brief Queued task called when tasks are pushed into the threadpool
 *
 * This function first calls into the threadpool's listener to let it know
 * that a task has been pushed. It then wakes up all idle threads and moves
 * them into the active thread container.
 * \param data A task_pushed_data
 * \return 0
 */
static int queued_task_pushed(void *data)
{
	struct task_pushed_data *tpd = data;
	struct ast_threadpool *pool = tpd->pool;
	int was_empty = tpd->was_empty;

	if (pool->listener && pool->listener->callbacks->task_pushed) {
		pool->listener->callbacks->task_pushed(pool, pool->listener, was_empty);
	}
	if (ao2_container_count(pool->idle_threads) == 0) {
		if (!pool->options.auto_increment) {
			return 0;
		}
		grow(pool, pool->options.auto_increment);
	}

	ao2_callback(pool->idle_threads, OBJ_UNLINK | OBJ_NOLOCK | OBJ_NODATA,
			activate_thread, pool);

	threadpool_send_state_changed(pool);
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
	struct ast_threadpool *pool = ast_taskprocessor_listener_get_user_data(listener);
	struct task_pushed_data *tpd;
	SCOPED_AO2LOCK(lock, pool);

	if (pool->shutting_down) {
		return;
	}
	tpd = task_pushed_data_alloc(pool, was_empty);
	if (!tpd) {
		return;
	}

	ast_taskprocessor_push(pool->control_tps, queued_task_pushed, tpd);
}

/*!
 * \brief Queued task that handles the case where the threadpool's taskprocessor is emptied
 *
 * This simply lets the threadpool's listener know that the threadpool is devoid of tasks
 * \param data The pool that has become empty
 * \return 0
 */
static int queued_emptied(void *data)
{
	struct ast_threadpool *pool = data;

	/* We already checked for existence of this callback when this was queued */
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
	struct ast_threadpool *pool = ast_taskprocessor_listener_get_user_data(listener);
	SCOPED_AO2LOCK(lock, pool);

	if (pool->shutting_down) {
		return;
	}

	if (pool->listener && pool->listener->callbacks->emptied) {
		ast_taskprocessor_push(pool->control_tps, queued_emptied, pool);
	}
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
	struct ast_threadpool *pool = ast_taskprocessor_listener_get_user_data(listener);

	if (pool->listener && pool->listener->callbacks->shutdown) {
		pool->listener->callbacks->shutdown(pool->listener);
	}
	ao2_cleanup(pool->active_threads);
	ao2_cleanup(pool->idle_threads);
	ao2_cleanup(pool->zombie_threads);
	ao2_cleanup(pool);
}

/*!
 * \brief Table of taskprocessor listener callbacks for threadpool's main taskprocessor
 */
static struct ast_taskprocessor_listener_callbacks threadpool_tps_listener_callbacks = {
	.start = threadpool_tps_start,
	.task_pushed = threadpool_tps_task_pushed,
	.emptied = threadpool_tps_emptied,
	.shutdown = threadpool_tps_shutdown,
};

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

	if (*num_to_kill > 0) {
		--(*num_to_kill);
		return CMP_MATCH;
	} else {
		return CMP_STOP;
	}
}

/*!
 * \brief ao2 callback to zombify a set number of threads.
 *
 * Threads will be zombified as long as as the counter has not reached
 * zero. The counter is decremented with each thread that is zombified.
 *
 * Zombifying a thread involves removing it from its current container,
 * adding it to the zombie container, and changing the state of the
 * worker to a zombie
 *
 * This callback is called from the threadpool control taskprocessor thread.
 *
 * \param obj The worker thread that may be zombified
 * \param arg The pool to which the worker belongs
 * \param data The counter
 * \param flags Unused
 * \retval CMP_MATCH The zombified thread should be removed from its current container
 * \retval CMP_STOP Stop attempting to zombify threads
 */
static int zombify_threads(void *obj, void *arg, void *data, int flags)
{
	struct worker_thread *worker = obj;
	struct ast_threadpool *pool = arg;
	int *num_to_zombify = data;

	if ((*num_to_zombify)-- > 0) {
		if (!ao2_link(pool->zombie_threads, worker)) {
			ast_log(LOG_WARNING, "Failed to zombify active thread %d. Thread will remain active\n", worker->id);
			return 0;
		}
		worker_set_state(worker, ZOMBIE);
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
 * threads will be zombified instead.
 *
 * This function is called from the threadpool control taskprocessor thread.
 *
 * \param pool The threadpool to remove threads from
 * \param delta The number of threads to remove
 */
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

	ast_debug(3, "Destroying %d idle threads in threadpool %s\n", idle_threads_to_kill,
			ast_taskprocessor_name(pool->tps));

	ao2_callback(pool->idle_threads, OBJ_UNLINK | OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE,
			kill_threads, &idle_threads_to_kill);

	ast_debug(3, "Destroying %d active threads in threadpool %s\n", active_threads_to_zombify,
			ast_taskprocessor_name(pool->tps));

	ao2_callback_data(pool->active_threads, OBJ_UNLINK | OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE,
			zombify_threads, pool, &active_threads_to_zombify);
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
	RAII_VAR(struct set_size_data *, ssd, data, ao2_cleanup);
	struct ast_threadpool *pool = ssd->pool;
	unsigned int num_threads = ssd->size;

	/* We don't count zombie threads as being "live" when potentially resizing */
	unsigned int current_size = ao2_container_count(pool->active_threads) +
		ao2_container_count(pool->idle_threads);

	if (current_size == num_threads) {
		ast_debug(3, "Not changing threadpool size since new size %u is the same as current %u\n",
			  num_threads, current_size);
		return 0;
	}

	if (current_size < num_threads) {
		grow(pool, num_threads - current_size);
		ao2_callback(pool->idle_threads, OBJ_UNLINK | OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE,
				activate_thread, pool);
	} else {
		shrink(pool, current_size - num_threads);
	}

	threadpool_send_state_changed(pool);
	return 0;
}

void ast_threadpool_set_size(struct ast_threadpool *pool, unsigned int size)
{
	struct set_size_data *ssd;
	SCOPED_AO2LOCK(lock, pool);
	if (pool->shutting_down) {
		return;
	}

	ssd = set_size_data_alloc(pool, size);
	if (!ssd) {
		return;
	}

	ast_taskprocessor_push(pool->control_tps, queued_set_size, ssd);
}

struct ast_threadpool_listener *ast_threadpool_listener_alloc(
		const struct ast_threadpool_listener_callbacks *callbacks, void *user_data)
{
	struct ast_threadpool_listener *listener = ao2_alloc(sizeof(*listener), NULL);
	if (!listener) {
		return NULL;
	}
	listener->callbacks = callbacks;
	listener->user_data = user_data;
	return listener;
}

void *ast_threadpool_listener_get_user_data(const struct ast_threadpool_listener *listener)
{
	return listener->user_data;
}

struct pool_options_pair {
	struct ast_threadpool *pool;
	struct ast_threadpool_options options;
};

struct ast_threadpool *ast_threadpool_create(const char *name,
		struct ast_threadpool_listener *listener,
		const struct ast_threadpool_options *options)
{
	struct ast_taskprocessor *tps;
	RAII_VAR(struct ast_taskprocessor_listener *, tps_listener, NULL, ao2_cleanup);
	RAII_VAR(struct ast_threadpool *, pool, NULL, ao2_cleanup);

	pool = threadpool_alloc(name, options);
	if (!pool) {
		return NULL;
	}

	tps_listener = ast_taskprocessor_listener_alloc(&threadpool_tps_listener_callbacks, pool);
	if (!tps_listener) {
		return NULL;
	}

	if (options->version != AST_THREADPOOL_OPTIONS_VERSION) {
		ast_log(LOG_WARNING, "Incompatible version of threadpool options in use.\n");
		return NULL;
	}

	tps = ast_taskprocessor_create_with_listener(name, tps_listener);
	if (!tps) {
		return NULL;
	}

	pool->tps = tps;
	if (listener) {
		ao2_ref(listener, +1);
		pool->listener = listener;
	}
	ast_threadpool_set_size(pool, pool->options.initial_size);
	ao2_ref(pool, +1);
	return pool;
}

int ast_threadpool_push(struct ast_threadpool *pool, int (*task)(void *data), void *data)
{
	SCOPED_AO2LOCK(lock, pool);
	if (!pool->shutting_down) {
		return ast_taskprocessor_push(pool->tps, task, data);
	}
	return -1;
}

void ast_threadpool_shutdown(struct ast_threadpool *pool)
{
	if (!pool) {
		return;
	}
	/* Shut down the taskprocessors and everything else just
	 * takes care of itself via the taskprocessor callbacks
	 */
	ao2_lock(pool);
	pool->shutting_down = 1;
	ao2_unlock(pool);
	ast_taskprocessor_unreference(pool->control_tps);
	ast_taskprocessor_unreference(pool->tps);
}

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
	ast_debug(3, "Destroying worker thread %d\n", worker->id);
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

	if (worker->options.thread_start) {
		worker->options.thread_start();
	}

	ast_mutex_lock(&worker->lock);
	while (worker_idle(worker)) {
		ast_mutex_unlock(&worker->lock);
		worker_active(worker);
		ast_mutex_lock(&worker->lock);
		if (worker->state != ALIVE) {
			break;
		}
		threadpool_active_thread_idle(worker->pool, worker);
	}
	ast_mutex_unlock(&worker->lock);

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

	if (worker->options.thread_end) {
		worker->options.thread_end();
	}
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
	worker->options = pool->options;
	return worker;
}

static int worker_thread_start(struct worker_thread *worker)
{
	return ast_pthread_create(&worker->thread, NULL, worker_start, worker);
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
	int alive;

	/* The following is equivalent to 
	 *
	 * while (threadpool_execute(worker->pool));
	 *
	 * However, reviewers have suggested in the past
	 * doing that can cause optimizers to (wrongly)
	 * optimize the code away.
	 */
	do {
		alive = threadpool_execute(worker->pool);
	} while (alive);
}

/*!
 * \brief Idle function for worker threads
 *
 * The worker waits here until it gets told by the threadpool
 * to wake up.
 *
 * worker is locked before entering this function.
 *
 * \param worker The idle worker
 * \retval 0 The thread is being woken up so that it can conclude.
 * \retval non-zero The thread is being woken up to do more work.
 */
static int worker_idle(struct worker_thread *worker)
{
	struct timeval start = ast_tvnow();
	struct timespec end = {
		.tv_sec = start.tv_sec + worker->options.idle_timeout,
		.tv_nsec = start.tv_usec * 1000,
	};
	while (!worker->wake_up) {
		if (worker->options.idle_timeout <= 0) {
			ast_cond_wait(&worker->cond, &worker->lock);
		} else if (ast_cond_timedwait(&worker->cond, &worker->lock, &end) == ETIMEDOUT) {
			break;
		}
	}

	if (!worker->wake_up) {
		ast_debug(1, "Worker thread idle timeout reached. Dying.\n");
		threadpool_idle_thread_dead(worker->pool, worker);
		worker->state = DEAD;
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

/*! Serializer group shutdown control object. */
struct ast_serializer_shutdown_group {
	/*! Shutdown thread waits on this conditional. */
	ast_cond_t cond;
	/*! Count of serializers needing to shutdown. */
	int count;
};

static void serializer_shutdown_group_dtor(void *vdoomed)
{
	struct ast_serializer_shutdown_group *doomed = vdoomed;

	ast_cond_destroy(&doomed->cond);
}

struct ast_serializer_shutdown_group *ast_serializer_shutdown_group_alloc(void)
{
	struct ast_serializer_shutdown_group *shutdown_group;

	shutdown_group = ao2_alloc(sizeof(*shutdown_group), serializer_shutdown_group_dtor);
	if (!shutdown_group) {
		return NULL;
	}
	ast_cond_init(&shutdown_group->cond, NULL);
	return shutdown_group;
}

int ast_serializer_shutdown_group_join(struct ast_serializer_shutdown_group *shutdown_group, int timeout)
{
	int remaining;
	ast_mutex_t *lock;

	if (!shutdown_group) {
		return 0;
	}

	lock = ao2_object_get_lockaddr(shutdown_group);
	ast_assert(lock != NULL);

	ao2_lock(shutdown_group);
	if (timeout) {
		struct timeval start;
		struct timespec end;

		start = ast_tvnow();
		end.tv_sec = start.tv_sec + timeout;
		end.tv_nsec = start.tv_usec * 1000;
		while (shutdown_group->count) {
			if (ast_cond_timedwait(&shutdown_group->cond, lock, &end)) {
				/* Error or timed out waiting for the count to reach zero. */
				break;
			}
		}
	} else {
		while (shutdown_group->count) {
			if (ast_cond_wait(&shutdown_group->cond, lock)) {
				/* Error */
				break;
			}
		}
	}
	remaining = shutdown_group->count;
	ao2_unlock(shutdown_group);
	return remaining;
}

/*!
 * \internal
 * \brief Increment the number of serializer members in the group.
 * \since 13.5.0
 *
 * \param shutdown_group Group shutdown controller.
 *
 * \return Nothing
 */
static void serializer_shutdown_group_inc(struct ast_serializer_shutdown_group *shutdown_group)
{
	ao2_lock(shutdown_group);
	++shutdown_group->count;
	ao2_unlock(shutdown_group);
}

/*!
 * \internal
 * \brief Decrement the number of serializer members in the group.
 * \since 13.5.0
 *
 * \param shutdown_group Group shutdown controller.
 *
 * \return Nothing
 */
static void serializer_shutdown_group_dec(struct ast_serializer_shutdown_group *shutdown_group)
{
	ao2_lock(shutdown_group);
	--shutdown_group->count;
	if (!shutdown_group->count) {
		ast_cond_signal(&shutdown_group->cond);
	}
	ao2_unlock(shutdown_group);
}

struct serializer {
	/*! Threadpool the serializer will use to process the jobs. */
	struct ast_threadpool *pool;
	/*! Which group will wait for this serializer to shutdown. */
	struct ast_serializer_shutdown_group *shutdown_group;
};

static void serializer_dtor(void *obj)
{
	struct serializer *ser = obj;

	ao2_cleanup(ser->pool);
	ser->pool = NULL;
	ao2_cleanup(ser->shutdown_group);
	ser->shutdown_group = NULL;
}

static struct serializer *serializer_create(struct ast_threadpool *pool,
	struct ast_serializer_shutdown_group *shutdown_group)
{
	struct serializer *ser;

	ser = ao2_alloc_options(sizeof(*ser), serializer_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!ser) {
		return NULL;
	}
	ao2_ref(pool, +1);
	ser->pool = pool;
	ser->shutdown_group = ao2_bump(shutdown_group);
	return ser;
}

AST_THREADSTORAGE_RAW(current_serializer);

static int execute_tasks(void *data)
{
	struct ast_taskprocessor *tps = data;

	ast_threadstorage_set_ptr(&current_serializer, tps);
	while (ast_taskprocessor_execute(tps)) {
		/* No-op */
	}
	ast_threadstorage_set_ptr(&current_serializer, NULL);

	ast_taskprocessor_unreference(tps);
	return 0;
}

static void serializer_task_pushed(struct ast_taskprocessor_listener *listener, int was_empty)
{
	if (was_empty) {
		struct serializer *ser = ast_taskprocessor_listener_get_user_data(listener);
		struct ast_taskprocessor *tps = ast_taskprocessor_listener_get_tps(listener);

		if (ast_threadpool_push(ser->pool, execute_tasks, tps)) {
			ast_taskprocessor_unreference(tps);
		}
	}
}

static int serializer_start(struct ast_taskprocessor_listener *listener)
{
	/* No-op */
	return 0;
}

static void serializer_shutdown(struct ast_taskprocessor_listener *listener)
{
	struct serializer *ser = ast_taskprocessor_listener_get_user_data(listener);

	if (ser->shutdown_group) {
		serializer_shutdown_group_dec(ser->shutdown_group);
	}
	ao2_cleanup(ser);
}

static struct ast_taskprocessor_listener_callbacks serializer_tps_listener_callbacks = {
	.task_pushed = serializer_task_pushed,
	.start = serializer_start,
	.shutdown = serializer_shutdown,
};

struct ast_taskprocessor *ast_threadpool_serializer_get_current(void)
{
	return ast_threadstorage_get_ptr(&current_serializer);
}

struct ast_taskprocessor *ast_threadpool_serializer_group(const char *name,
	struct ast_threadpool *pool, struct ast_serializer_shutdown_group *shutdown_group)
{
	struct serializer *ser;
	struct ast_taskprocessor_listener *listener;
	struct ast_taskprocessor *tps;

	ser = serializer_create(pool, shutdown_group);
	if (!ser) {
		return NULL;
	}

	listener = ast_taskprocessor_listener_alloc(&serializer_tps_listener_callbacks, ser);
	if (!listener) {
		ao2_ref(ser, -1);
		return NULL;
	}
	/* ser ref transferred to listener */

	tps = ast_taskprocessor_create_with_listener(name, listener);
	if (tps && shutdown_group) {
		serializer_shutdown_group_inc(shutdown_group);
	}

	ao2_ref(listener, -1);
	return tps;
}

struct ast_taskprocessor *ast_threadpool_serializer(const char *name, struct ast_threadpool *pool)
{
	return ast_threadpool_serializer_group(name, pool, NULL);
}
