/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Sangoma Technologies Corporation
 *
 * Joshua Colp <jcolp@sangoma.com>
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

#include "asterisk/_private.h"
#include "asterisk/taskpool.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/astobj2.h"
#include "asterisk/serializer_shutdown_group.h"
#include "asterisk/utils.h"
#include "asterisk/time.h"
#include "asterisk/sched.h"

/*!
 * \brief A taskpool taskprocessor
 */
struct taskpool_taskprocessor {
	/*! The underlying taskprocessor */
	struct ast_taskprocessor *taskprocessor;
	/*! The last time a task was pushed to this taskprocessor */
	struct timeval last_pushed;
};

/*!
 * \brief A container of taskprocessors
 */
struct taskpool_taskprocessors {
	/*! A vector of taskprocessors */
	AST_VECTOR(, struct taskpool_taskprocessor *) taskprocessors;
	/*! The next taskprocessor to use for pushing */
	unsigned int taskprocessor_num;
};

typedef void (*taskpool_selector)(struct ast_taskpool *pool, struct taskpool_taskprocessors *taskprocessors,
	struct taskpool_taskprocessor **taskprocessor, unsigned int *growth_threshold_reached);

/*!
 * \brief An opaque taskpool structure
 *
 * A taskpool is a collection of taskprocessors that
 * execute tasks, each from their own queue. A selector
 * determines which taskprocessor to queue to at push
 * time.
 */
struct ast_taskpool {
	/*! The static taskprocessors, those which will always exist */
	struct taskpool_taskprocessors static_taskprocessors;
	/*! The dynamic taskprocessors, those which will be created as needed */
	struct taskpool_taskprocessors dynamic_taskprocessors;
	/*! True if the taskpool is in the process of shutting down */
	int shutting_down;
	/*! Taskpool-specific options */
	struct ast_taskpool_options options;
	/*! Dynamic pool shrinking scheduled item */
	int shrink_sched_id;
	/*! The taskprocessor selector to use */
	taskpool_selector selector;
	/*! The name of the taskpool */
	char name[0];
};

/*! \brief The threshold for a taskprocessor at which we consider the pool needing to grow (50% of high water threshold) */
#define TASKPOOL_GROW_THRESHOLD (AST_TASKPROCESSOR_HIGH_WATER_LEVEL * 5) / 10

/*! \brief Scheduler used for dynamic pool shrinking */
static struct ast_sched_context *sched;

/*! \brief Thread storage for the current taskpool */
AST_THREADSTORAGE_RAW(current_taskpool_pool);

/*!
 * \internal
 * \brief Get the current taskpool associated with this thread.
 */
static struct ast_taskpool *ast_taskpool_get_current(void)
{
	return ast_threadstorage_get_ptr(&current_taskpool_pool);
}

/*!
 * \internal
 * \brief Shutdown task for taskpool taskprocessor
 */
 static int taskpool_taskprocessor_stop(void *data)
 {
	struct ast_taskpool *pool = ast_taskpool_get_current();

	/* If a thread stop callback is set on the options, call it */
	if (pool->options.thread_end) {
		pool->options.thread_end();
	}

	ao2_cleanup(pool);

	 return 0;
 }

/*! \internal */
static void taskpool_taskprocessor_dtor(void *obj)
{
	struct taskpool_taskprocessor *taskprocessor = obj;

	if (taskprocessor->taskprocessor && ast_taskprocessor_push(taskprocessor->taskprocessor, taskpool_taskprocessor_stop, NULL)) {
		/* We can't actually do anything if this fails, so just accept reality */
	}

	ast_taskprocessor_unreference(taskprocessor->taskprocessor);
}

/*!
 * \internal
 * \brief Startup task for taskpool taskprocessor
 */
static int taskpool_taskprocessor_start(void *data)
{
	struct ast_taskpool *pool = data;

	/* Set the pool on the thread for this taskprocessor, inheriting the
	 * reference passed to the task itself.
	 */
	ast_threadstorage_set_ptr(&current_taskpool_pool, pool);

	/* If a thread start callback is set on the options, call it */
	if (pool->options.thread_start) {
		pool->options.thread_start();
	}

	return 0;
}

/*!
 * \internal
 * \brief Allocate a taskpool specific taskprocessor
 */
static struct taskpool_taskprocessor *taskpool_taskprocessor_alloc(struct ast_taskpool *pool, char type)
{
	struct taskpool_taskprocessor *taskprocessor;
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

	/* We don't actually need locking for each pool taskprocessor, as the only thing
	 * mutable is the underlying taskprocessor which has its own internal locking.
	 */
	taskprocessor = ao2_alloc_options(sizeof(*taskprocessor), taskpool_taskprocessor_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!taskprocessor) {
		return NULL;
	}

	/* Create name with seq number appended. */
	ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "taskpool/%c:%s", type, pool->name);

	taskprocessor->taskprocessor = ast_taskprocessor_get(tps_name, TPS_REF_DEFAULT);
	if (!taskprocessor->taskprocessor) {
		ao2_ref(taskprocessor, -1);
		return NULL;
	}

	taskprocessor->last_pushed = ast_tvnow();

	if (ast_taskprocessor_push(taskprocessor->taskprocessor, taskpool_taskprocessor_start, ao2_bump(pool))) {
		ao2_ref(pool, -1);
		/* Prevent the taskprocessor from queueing the stop task by explicitly unreferencing and setting it to
		 * NULL here.
		 */
		ast_taskprocessor_unreference(taskprocessor->taskprocessor);
		taskprocessor->taskprocessor = NULL;
		return NULL;
	}

	return taskprocessor;
}

/*!
 * \internal
 * \brief Initialize the taskpool taskprocessors structure
 */
static int taskpool_taskprocessors_init(struct taskpool_taskprocessors *taskprocessors, unsigned int size)
{
	if (AST_VECTOR_INIT(&taskprocessors->taskprocessors, size)) {
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Clean up the taskpool taskprocessors structure
 */
static void taskpool_taskprocessors_cleanup(struct taskpool_taskprocessors *taskprocessors)
{
	/* Access/manipulation of taskprocessors is done with the lock held, and
	 * with a check of the shutdown flag done. This means that outside of holding
	 * the lock we can safely muck with it. Pushing to the taskprocessor is done
	 * outside of the lock, but with a reference to the taskprocessor held.
	 */
	AST_VECTOR_CALLBACK_VOID(&taskprocessors->taskprocessors, ao2_cleanup);
	AST_VECTOR_FREE(&taskprocessors->taskprocessors);
}

/*!
 * \internal
 * \brief Determine if a taskpool taskprocessor is idle
 */
#define TASKPROCESSOR_IS_IDLE(tps, timeout) (ast_tvdiff_ms(ast_tvnow(), tps->last_pushed) > (timeout))

/*! \internal
 * \brief Taskpool dynamic pool shrink function
 */
static int taskpool_dynamic_pool_shrink(const void *data)
{
	struct ast_taskpool *pool = (struct ast_taskpool *)data;
	int num_removed;

	ao2_lock(pool);

	/* If the pool is shutting down, do nothing and don't reschedule */
	if (pool->shutting_down) {
		ao2_unlock(pool);
		ao2_ref(pool, -1);
		return 0;
	}

	/* Go through the dynamic taskprocessors and find any which have been idle long enough and remove them */
	num_removed = AST_VECTOR_REMOVE_ALL_CMP_UNORDERED(&pool->dynamic_taskprocessors.taskprocessors, pool->options.idle_timeout * 1000,
		TASKPROCESSOR_IS_IDLE, ao2_cleanup);
	if (num_removed) {
		/* If we've removed any taskprocessors the taskprocessor_num may no longer be valid, so update it */
		if (pool->dynamic_taskprocessors.taskprocessor_num >= AST_VECTOR_SIZE(&pool->dynamic_taskprocessors.taskprocessors)) {
			pool->dynamic_taskprocessors.taskprocessor_num = 0;
		}
	}

	ao2_unlock(pool);

	/* It is possible for the pool to have been shut down between unlocking and returning, this is
	 * inherently a race condition we can't eliminate so we will catch it on the next iteration.
	 */
	return pool->options.idle_timeout * 1000;
}

/*!
 * \internal
 * \brief Sequential taskprocessor selector
 */
 static void taskpool_sequential_selector(struct ast_taskpool *pool, struct taskpool_taskprocessors *taskprocessors,
	struct taskpool_taskprocessor **taskprocessor, unsigned int *growth_threshold_reached)
{
	unsigned int taskprocessor_num = taskprocessors->taskprocessor_num;

	if (!AST_VECTOR_SIZE(&taskprocessors->taskprocessors)) {
		*growth_threshold_reached = 1;
		return;
	}

	taskprocessors->taskprocessor_num++;
	if (taskprocessors->taskprocessor_num == AST_VECTOR_SIZE(&taskprocessors->taskprocessors)) {
		taskprocessors->taskprocessor_num = 0;
	}

	*taskprocessor = AST_VECTOR_GET(&taskprocessors->taskprocessors, taskprocessor_num);

	/* Check to see if this has reached the growth threshold */
	*growth_threshold_reached = (ast_taskprocessor_size((*taskprocessor)->taskprocessor) >= pool->options.growth_threshold) ? 1 : 0;
}

/*!
 * \interal
 * \brief Least full taskprocessor selector
 */
static void taskpool_least_full_selector(struct ast_taskpool *pool, struct taskpool_taskprocessors *taskprocessors,
	struct taskpool_taskprocessor **taskprocessor, unsigned int *growth_threshold_reached)
{
	struct taskpool_taskprocessor *least_full = NULL;
	unsigned int i;

	if (!AST_VECTOR_SIZE(&taskprocessors->taskprocessors)) {
		*growth_threshold_reached = 1;
		return;
	}

	/* We assume that the growth threshold has not yet been reached, until proven otherwise */
	*growth_threshold_reached = 0;

	for (i = 0; i < AST_VECTOR_SIZE(&taskprocessors->taskprocessors); i++) {
		struct taskpool_taskprocessor *tp = AST_VECTOR_GET(&taskprocessors->taskprocessors, i);

		/* If this taskprocessor has no outstanding tasks, it is the best choice */
		if (!ast_taskprocessor_size(tp->taskprocessor)) {
			*taskprocessor = tp;
			return;
		}

		/* If any of the taskprocessors have reached the growth threshold then we should grow the pool */
		if (ast_taskprocessor_size(tp->taskprocessor) >= pool->options.growth_threshold) {
			*growth_threshold_reached = 1;
		}

		/* The taskprocessor with the fewest tasks should be used */
		if (!least_full || ast_taskprocessor_size(tp->taskprocessor) < ast_taskprocessor_size(least_full->taskprocessor)) {
			least_full = tp;
		}
	}

	*taskprocessor = least_full;
}

struct ast_taskpool *ast_taskpool_create(const char *name,
		const struct ast_taskpool_options *options)
{
	struct ast_taskpool *pool;

	/* Enforce versioning on the passed-in options */
	if (options->version != AST_TASKPOOL_OPTIONS_VERSION) {
		return NULL;
	}

	pool = ao2_alloc(sizeof(*pool) + strlen(name) + 1, NULL);
	if (!pool) {
		return NULL;
	}

	strcpy(pool->name, name); /* Safe */
	memcpy(&pool->options, options, sizeof(pool->options));
	pool->shrink_sched_id = -1;

	/* Verify the passed-in options are valid, and adjust if needed */
	if (options->initial_size < options->minimum_size) {
		pool->options.initial_size = options->minimum_size;
		ast_log(LOG_WARNING, "Taskpool '%s' has an initial size of %d, which is less than the minimum size of %d. Adjusting to %d.\n",
			name, options->initial_size, options->minimum_size, options->minimum_size);
	}

	if (options->max_size && pool->options.initial_size > options->max_size) {
		pool->options.max_size = pool->options.initial_size;
		ast_log(LOG_WARNING, "Taskpool '%s' has a max size of %d, which is less than the initial size of %d. Adjusting to %d.\n",
			name, options->max_size, pool->options.initial_size, pool->options.initial_size);
	}

	if (!options->auto_increment) {
		if (!pool->options.minimum_size) {
			pool->options.minimum_size = 1;
			ast_log(LOG_WARNING, "Taskpool '%s' has a minimum size of 0, which is not valid without auto increment. Adjusting to 1.\n", name);
		}
		if (!pool->options.max_size) {
			pool->options.max_size = pool->options.minimum_size;
			ast_log(LOG_WARNING, "Taskpool '%s' has a max size of 0, which is not valid without auto increment. Adjusting to %d.\n", name, pool->options.minimum_size);
		}
		if (pool->options.minimum_size != pool->options.max_size) {
			pool->options.minimum_size = pool->options.max_size;
			pool->options.initial_size = pool->options.max_size;
			ast_log(LOG_WARNING, "Taskpool '%s' has a minimum size of %d, while max size is %d. Adjusting all sizes to %d due to lack of auto increment.\n",
				name, options->minimum_size, pool->options.max_size, pool->options.max_size);
		}
	} else if (!options->growth_threshold) {
		pool->options.growth_threshold = TASKPOOL_GROW_THRESHOLD;
	}

	if (options->selector == AST_TASKPOOL_SELECTOR_DEFAULT || options->selector == AST_TASKPOOL_SELECTOR_LEAST_FULL) {
		pool->selector = taskpool_least_full_selector;
	} else if (options->selector == AST_TASKPOOL_SELECTOR_SEQUENTIAL) {
		pool->selector = taskpool_sequential_selector;
	} else {
		ast_log(LOG_WARNING, "Taskpool '%s' has an invalid selector of %d. Adjusting to default selector.\n",
			name, options->selector);
		pool->selector = taskpool_least_full_selector;
	}

	if (taskpool_taskprocessors_init(&pool->static_taskprocessors, pool->options.minimum_size)) {
		ao2_ref(pool, -1);
		return NULL;
	}

	/* Create the static taskprocessors based on the passed-in options */
	for (int i = 0; i < pool->options.minimum_size; i++) {
		struct taskpool_taskprocessor *taskprocessor;

		taskprocessor = taskpool_taskprocessor_alloc(pool, 's');
		if (!taskprocessor) {
			/* The reference to pool is passed to ast_taskpool_shutdown */
			ast_taskpool_shutdown(pool);
			return NULL;
		}

		if (AST_VECTOR_APPEND(&pool->static_taskprocessors.taskprocessors, taskprocessor)) {
			ao2_ref(taskprocessor, -1);
			/* The reference to pool is passed to ast_taskpool_shutdown */
			ast_taskpool_shutdown(pool);
			return NULL;
		}
	}

	if (taskpool_taskprocessors_init(&pool->dynamic_taskprocessors,
		pool->options.initial_size - pool->options.minimum_size)) {
		ast_taskpool_shutdown(pool);
		return NULL;
	}

	/* Create the dynamic taskprocessor based on the passed-in options */
	for (int i = 0; i < (pool->options.initial_size - pool->options.minimum_size); i++) {
		struct taskpool_taskprocessor *taskprocessor;

		taskprocessor = taskpool_taskprocessor_alloc(pool, 'd');
		if (!taskprocessor) {
			/* The reference to pool is passed to ast_taskpool_shutdown */
			ast_taskpool_shutdown(pool);
			return NULL;
		}

		if (AST_VECTOR_APPEND(&pool->dynamic_taskprocessors.taskprocessors, taskprocessor)) {
			ao2_ref(taskprocessor, -1);
			/* The reference to pool is passed to ast_taskpool_shutdown */
			ast_taskpool_shutdown(pool);
			return NULL;
		}
	}

	/* If idle timeout support is enabled kick off a scheduled task to shrink the dynamic pool periodically, we do
	 * this no matter if there are dynamic taskprocessor present to reduce the work needed within the push function
	 * and to reduce complexity.
	 */
	if (options->idle_timeout && options->auto_increment) {
		pool->shrink_sched_id = ast_sched_add(sched, options->idle_timeout * 1000, taskpool_dynamic_pool_shrink, ao2_bump(pool));
		if (pool->shrink_sched_id < 0) {
			ao2_ref(pool, -1);
			/* The second reference to pool is passed to ast_taskpool_shutdown */
			ast_taskpool_shutdown(pool);
			return NULL;
		}
	}

	return pool;
}

size_t ast_taskpool_taskprocessors_count(struct ast_taskpool *pool)
{
	size_t count;

	ao2_lock(pool);
	count = AST_VECTOR_SIZE(&pool->static_taskprocessors.taskprocessors) + AST_VECTOR_SIZE(&pool->dynamic_taskprocessors.taskprocessors);
	ao2_unlock(pool);

	return count;
}

#define TASKPOOL_QUEUE_SIZE_ADD(tps, size) (size += ast_taskprocessor_size(tps->taskprocessor))

long ast_taskpool_queue_size(struct ast_taskpool *pool)
{
	long queue_size = 0;

	ao2_lock(pool);
	AST_VECTOR_CALLBACK_VOID(&pool->static_taskprocessors.taskprocessors, TASKPOOL_QUEUE_SIZE_ADD, queue_size);
	AST_VECTOR_CALLBACK_VOID(&pool->dynamic_taskprocessors.taskprocessors, TASKPOOL_QUEUE_SIZE_ADD, queue_size);
	ao2_unlock(pool);

	return queue_size;
}

/*! \internal
 * \brief Taskpool dynamic pool grow function
 */
static void taskpool_dynamic_pool_grow(struct ast_taskpool *pool, struct taskpool_taskprocessor **taskprocessor)
{
	unsigned int num_to_add = pool->options.auto_increment;
	int i;

	if (!num_to_add) {
		return;
	}

	/* If a maximum size is enforced, then determine if we have to limit how many taskprocessors we add */
	if (pool->options.max_size) {
		unsigned int current_size = AST_VECTOR_SIZE(&pool->dynamic_taskprocessors.taskprocessors) + AST_VECTOR_SIZE(&pool->static_taskprocessors.taskprocessors);

		if (current_size + num_to_add > pool->options.max_size) {
			num_to_add = pool->options.max_size - current_size;
		}
	}

	for (i = 0; i < num_to_add; i++) {
		struct taskpool_taskprocessor *new_taskprocessor;

		new_taskprocessor = taskpool_taskprocessor_alloc(pool, 'd');
		if (!new_taskprocessor) {
			return;
		}

		if (AST_VECTOR_APPEND(&pool->dynamic_taskprocessors.taskprocessors, new_taskprocessor)) {
			ao2_ref(new_taskprocessor, -1);
			return;
		}

		if (i == 0) {
			/* On the first iteration we return the taskprocessor we just added */
			*taskprocessor = new_taskprocessor;
			/* We assume we will be going back to the first taskprocessor, since we are at the end of the vector */
			pool->dynamic_taskprocessors.taskprocessor_num = 0;
		} else if (i == 1) {
			/* On the second iteration we update the next taskprocessor to use to be this one */
			pool->dynamic_taskprocessors.taskprocessor_num = AST_VECTOR_SIZE(&pool->dynamic_taskprocessors.taskprocessors) - 1;
		}
	}
}

int ast_taskpool_push(struct ast_taskpool *pool, int (*task)(void *data), void *data)
{
	RAII_VAR(struct taskpool_taskprocessor *, taskprocessor, NULL, ao2_cleanup);

	/* Select the taskprocessor in the pool to use for pushing this task */
	ao2_lock(pool);
	if (!pool->shutting_down) {
		unsigned int growth_threshold_reached = 0;

		/* A selector doesn't set taskprocessor to NULL, it will only change the value if a better
		 * taskprocessor is found. This means that even if the selector for a dynamic taskprocessor
		 * fails for some reason, it will still fall back to the initially found static one if
		 * it is present.
		 */
		pool->selector(pool, &pool->static_taskprocessors, &taskprocessor, &growth_threshold_reached);
		if (pool->options.auto_increment && growth_threshold_reached) {
			/* If we need to grow then try dynamic taskprocessors */
			pool->selector(pool, &pool->dynamic_taskprocessors, &taskprocessor, &growth_threshold_reached);
			if (growth_threshold_reached) {
				/* If we STILL need to grow then grow the dynamic taskprocessor pool if allowed */
				taskpool_dynamic_pool_grow(pool, &taskprocessor);
			}

			/* If a dynamic taskprocessor was used update its last push time */
			if (taskprocessor) {
				taskprocessor->last_pushed = ast_tvnow();
			}
		}
		ao2_bump(taskprocessor);
	}
	ao2_unlock(pool);

	if (!taskprocessor) {
		return -1;
	}

	if (ast_taskprocessor_push(taskprocessor->taskprocessor, task, data)) {
		return -1;
	}

	return 0;
}

/*!
 * \internal Structure used for synchronous task
 */
struct taskpool_sync_task {
	ast_mutex_t lock;
	ast_cond_t cond;
	int complete;
	int fail;
	int (*task)(void *);
	void *task_data;
};

/*!
 * \internal Initialization function for synchronous task
 */
static int taskpool_sync_task_init(struct taskpool_sync_task *sync_task, int (*task)(void *), void *data)
{
	ast_mutex_init(&sync_task->lock);
	ast_cond_init(&sync_task->cond, NULL);
	sync_task->complete = 0;
	sync_task->fail = 0;
	sync_task->task = task;
	sync_task->task_data = data;
	return 0;
}

/*!
 * \internal Cleanup function for synchronous task
 */
static void taskpool_sync_task_cleanup(struct taskpool_sync_task *sync_task)
{
	ast_mutex_destroy(&sync_task->lock);
	ast_cond_destroy(&sync_task->cond);
}

/*!
 * \internal Function for executing a sychronous task
 */
static int taskpool_sync_task(void *data)
{
	struct taskpool_sync_task *sync_task = data;
	int ret;

	sync_task->fail = sync_task->task(sync_task->task_data);

	/*
	 * Once we unlock sync_task->lock after signaling, we cannot access
	 * sync_task again.  The thread waiting within ast_taskpool_push_wait()
	 * is free to continue and release its local variable (sync_task).
	 */
	ast_mutex_lock(&sync_task->lock);
	sync_task->complete = 1;
	ast_cond_signal(&sync_task->cond);
	ret = sync_task->fail;
	ast_mutex_unlock(&sync_task->lock);
	return ret;
}

int ast_taskpool_push_wait(struct ast_taskpool *pool, int (*task)(void *data), void *data)
{
	struct taskpool_sync_task sync_task;

	/* If we are already executing within a taskpool taskprocessor then
	 * don't bother pushing a new task, just directly execute the task.
	 */
	if (ast_taskpool_get_current()) {
		return task(data);
	}

	if (taskpool_sync_task_init(&sync_task, task, data)) {
		return -1;
	}

	if (ast_taskpool_push(pool, taskpool_sync_task, &sync_task)) {
		taskpool_sync_task_cleanup(&sync_task);
		return -1;
	}

	ast_mutex_lock(&sync_task.lock);
	while (!sync_task.complete) {
		ast_cond_wait(&sync_task.cond, &sync_task.lock);
	}
	ast_mutex_unlock(&sync_task.lock);

	taskpool_sync_task_cleanup(&sync_task);
	return sync_task.fail;
}

void ast_taskpool_shutdown(struct ast_taskpool *pool)
{
	if (!pool) {
		return;
	}

	/* Mark this pool as shutting down so nothing new is pushed */
	ao2_lock(pool);
	pool->shutting_down = 1;
	ao2_unlock(pool);

	/* Stop the shrink scheduled item if present */
	AST_SCHED_DEL_UNREF(sched, pool->shrink_sched_id, ao2_ref(pool, -1));

	/* Clean up all the taskprocessors */
	taskpool_taskprocessors_cleanup(&pool->static_taskprocessors);
	taskpool_taskprocessors_cleanup(&pool->dynamic_taskprocessors);

	ao2_ref(pool, -1);
}

struct serializer {
	/*! Taskpool the serializer will use to process the jobs. */
	struct ast_taskpool *pool;
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

static struct serializer *serializer_create(struct ast_taskpool *pool,
	struct ast_serializer_shutdown_group *shutdown_group)
{
	struct serializer *ser;

	/* This object has a lock so it can be used to ensure exclusive access
	 * to the execution of tasks within the serializer.
	 */
	ser = ao2_alloc(sizeof(*ser), serializer_dtor);
	if (!ser) {
		return NULL;
	}
	ser->pool = ao2_bump(pool);
	ser->shutdown_group = ao2_bump(shutdown_group);
	return ser;
}

AST_THREADSTORAGE_RAW(current_taskpool_serializer);

static int execute_tasks(void *data)
{
	struct ast_taskpool *pool = ast_taskpool_get_current();
	struct ast_taskprocessor *tps = data;
	struct ast_taskprocessor_listener *listener = ast_taskprocessor_listener(tps);
	struct serializer *ser = ast_taskprocessor_listener_get_user_data(listener);
	size_t remaining, requeue = 0;

	/* In a normal scenario this lock will not be in contention with
	 * anything else. It is only if a synchronous task is pushed to
	 * the serializer that it may be blocked on the synchronous
	 * task thread. This is done to ensure that only one thread is executing
	 * tasks from the serializer at a given time, and not out of order
	 * either.
	 */
	ao2_lock(ser);

	ast_threadstorage_set_ptr(&current_taskpool_serializer, tps);
	for (remaining = ast_taskprocessor_size(tps); remaining > 0; remaining--) {
		requeue = ast_taskprocessor_execute(tps);
	}
	ast_threadstorage_set_ptr(&current_taskpool_serializer, NULL);

	ao2_unlock(ser);

	/* If there are remaining tasks we requeue, this way the serializer
	 * does not hold exclusivity of the taskpool taskprocessor
	 */
	if (requeue) {
		/* Ownership passes to the new task */
		if (ast_taskpool_push(pool, execute_tasks, tps)) {
			ast_taskprocessor_unreference(tps);
		}
	} else {
		ast_taskprocessor_unreference(tps);
	}

	return 0;
}

static void serializer_task_pushed(struct ast_taskprocessor_listener *listener, int was_empty)
{
	if (was_empty) {
		struct serializer *ser = ast_taskprocessor_listener_get_user_data(listener);
		struct ast_taskprocessor *tps = ast_taskprocessor_listener_get_tps(listener);

		if (ast_taskpool_push(ser->pool, execute_tasks, tps)) {
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
		ast_serializer_shutdown_group_dec(ser->shutdown_group);
	}
	ao2_cleanup(ser);
}

static struct ast_taskprocessor_listener_callbacks serializer_tps_listener_callbacks = {
	.task_pushed = serializer_task_pushed,
	.start = serializer_start,
	.shutdown = serializer_shutdown,
};

struct ast_taskprocessor *ast_taskpool_serializer_get_current(void)
{
	return ast_threadstorage_get_ptr(&current_taskpool_serializer);
}

struct ast_taskprocessor *ast_taskpool_serializer_group(const char *name,
	struct ast_taskpool *pool, struct ast_serializer_shutdown_group *shutdown_group)
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

	tps = ast_taskprocessor_create_with_listener(name, listener);
	if (!tps) {
		/* ser ref transferred to listener but not cleaned without tps */
		ao2_ref(ser, -1);
	} else if (shutdown_group) {
		ast_serializer_shutdown_group_inc(shutdown_group);
	}

	ao2_ref(listener, -1);
	return tps;
}

struct ast_taskprocessor *ast_taskpool_serializer(const char *name, struct ast_taskpool *pool)
{
	return ast_taskpool_serializer_group(name, pool, NULL);
}

/*!
 * \internal An empty task callback, used to ensure the serializer does not
 * go empty. */
static int taskpool_serializer_empty_task(void *data)
{
	return 0;
}

int ast_taskpool_serializer_push_wait(struct ast_taskprocessor *serializer, int (*task)(void *data), void *data)
{
	struct ast_taskprocessor_listener *listener = ast_taskprocessor_listener(serializer);
	struct serializer *ser = ast_taskprocessor_listener_get_user_data(listener);
	struct ast_taskprocessor *prior_serializer;
	struct taskpool_sync_task sync_task;

	/* If not in a taskpool taskprocessor we can just queue the task like normal and
	 * wait. */
	if (!ast_taskpool_get_current()) {
		if (taskpool_sync_task_init(&sync_task, task, data)) {
			return -1;
		}

		if (ast_taskprocessor_push(serializer, taskpool_sync_task, &sync_task)) {
			taskpool_sync_task_cleanup(&sync_task);
			return -1;
		}

		ast_mutex_lock(&sync_task.lock);
		while (!sync_task.complete) {
			ast_cond_wait(&sync_task.cond, &sync_task.lock);
		}
		ast_mutex_unlock(&sync_task.lock);

		taskpool_sync_task_cleanup(&sync_task);
		return sync_task.fail;
	}

	/* It is possible that we are already executing within a serializer, so stash the existing
	 * away so we can restore it.
	 */
	prior_serializer = ast_taskpool_serializer_get_current();

	ao2_lock(ser);

	/* There are two cases where we can or have to directly execute this task:
	 * 1. There are no other tasks in the serializer
	 * 2. We are already in the serializer
	 * In the second case if we don't execute the task now, we will deadlock waiting
	 * on it as it will never occur.
	 */
	if (!ast_taskprocessor_size(serializer) || prior_serializer == serializer) {
		ast_threadstorage_set_ptr(&current_taskpool_serializer, serializer);
		sync_task.fail = task(data);
		ao2_unlock(ser);
		ast_threadstorage_set_ptr(&current_taskpool_serializer, prior_serializer);
		return sync_task.fail;
	}

	if (taskpool_sync_task_init(&sync_task, task, data)) {
		ao2_unlock(ser);
		return -1;
	}

	/* First we queue the serialized task */
	if (ast_taskprocessor_push(serializer, taskpool_sync_task, &sync_task)) {
		taskpool_sync_task_cleanup(&sync_task);
		ao2_unlock(ser);
		return -1;
	}

	/* Next we queue the empty task to ensure the serializer doesn't reach empty, this
	 * stops two tasks from being queued for the same serializer at the same time.
	 */
	if (ast_taskprocessor_push(serializer, taskpool_serializer_empty_task, NULL)) {
		taskpool_sync_task_cleanup(&sync_task);
		ao2_unlock(ser);
		return -1;
	}

	/* Now we execute the tasks on the serializer until our sync task is complete */
	ast_threadstorage_set_ptr(&current_taskpool_serializer, serializer);
	while (!sync_task.complete) {
		/* The sync task is guaranteed to be executed, so doing a while loop on the complete
		 * flag is safe.
		 */
		ast_taskprocessor_execute(serializer);
	}
	taskpool_sync_task_cleanup(&sync_task);
	ao2_unlock(ser);

	ast_threadstorage_set_ptr(&current_taskpool_serializer, prior_serializer);

	return sync_task.fail;
}

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void taskpool_shutdown(void)
{
	if (sched) {
		ast_sched_context_destroy(sched);
		sched = NULL;
	}
}

int ast_taskpool_init(void)
{
	sched = ast_sched_context_create();
	if (!sched) {
		return -1;
	}

	if (ast_sched_start_thread(sched)) {
		return -1;
	}

	ast_register_cleanup(taskpool_shutdown);

	return 0;
}
