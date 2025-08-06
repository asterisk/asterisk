/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Sangoma Technologies Corporation
 *
 * Joshua C. Colp <jcolp@sangoma.com>
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
 * \ref Taskpool
 *
 * \page Taskpool API providing queued task execution across threads.

The taskpool API is a specialized API for the queueing of tasks
in a synchronous or asynchronous manner, to be executed across
a pool of threads. For cases where serialization is needed a
serializer API is also provided ensuring that tasks queued to
the serializer are executed in a serialized fashion within the
taskpool.

On creation of a taskpool various options can be set and used to
control the operation of the pool. This includes how many taskprocessors
are present, whether the pool can grow, whether the pool can shrink,
and how long idle taskprocessors should exist before being terminated.
This provides flexibility based on the specific needs of the user of
the taskpool and the environment.

The queueing of tasks to the taskpool is done using a selector. The
selector examines the available taskprocessors and decides which one
to queue the task to. This operation can also examine the state of
the pool to see if it needs to grow and if enabled and possible does so.

The taskpool API is preferred for many cases over the use of the
threadpool due to the far lower overhead involved. Taskpools require
no additional thread or task queue for management of the pool itself and
the act of queueing tasks, the most common operation, is written to be as
simple and minimal as possible. Threadpools are best used for long
running tasks and operations.

*/

#ifndef _ASTERISK_TASKPOOL_H
#define _ASTERISK_TASKPOOL_H

struct ast_taskpool;
struct ast_taskprocessor;
struct ast_serializer_shutdown_group;

/*!
 * \brief Selectors for choosing which taskprocessor in a pool to use
 */
enum ast_taskpool_selector {
	AST_TASKPOOL_SELECTOR_DEFAULT = 0, /* The selector that is generally the best for most use cases */
	AST_TASKPOOL_SELECTOR_LEAST_FULL,  /* Select the least full taskprocessor */
	AST_TASKPOOL_SELECTOR_SEQUENTIAL,  /* Select taskprocessors in a sequential manner */
};

struct ast_taskpool_options {
#define AST_TASKPOOL_OPTIONS_VERSION 1
	/*! Version of taskpool options in use */
	int version;
	/*!
	 * \brief The selector to use for choosing a taskprocessor
	 */
	enum ast_taskpool_selector selector;
	/*!
	 * \brief Time limit in seconds for idle dynamic taskprocessors
	 *
	 * A time of 0 or less will mean no timeout.
	 */
	int idle_timeout;
	/*!
	 * \brief Number of taskprocessors to increment the pool by
	 */
	int auto_increment;
	/*!
	 * \brief Number of taskprocessors that will always exist
	 *
	 * Zero is a valid value if the taskpool will never have taskprocessors
	 * that always exist, allowing the pool to drop to zero if not used.
	 */
	int minimum_size;
	/*!
	 * \brief Number of taskprocessors the pool will start with
	 *
	 * Zero is a valid value if the taskpool should start
	 * without any taskprocessors allocated.
	 *
	 * \note This must be equal to or greater than the minimum_size,
	 * otherwise the taskpool will adjust this to the minimum_size.
	 */
	int initial_size;
	/*!
	 * \brief Maximum number of taskprocessors a pool may have
	 *
	 * When the taskpool's size increases, it can never increase
	 * beyond this number of taskprocessors.
	 *
	 * Zero is a valid value if the taskpool does not have a
	 * maximum size for taskprocessors.
	 *
	 * \note This must be equal to or greater than the initial_size,
	 * otherwise the taskpool will adjust this to the initial_size.
	 */
	int max_size;
	/*!
	 * \brief The threshold for when to grow the pool
	 *
	 * This is the number of tasks that must be in queue before the pool will grow.
	 *
	 * \note If not specified a default of the 50% of the high water threshold defined
	 * in taskprocessor.h will be used.
	 */
	int growth_threshold;
	/*!
	 * \brief Function to call when a taskprocessor starts
	 *
	 * This is useful if there is something common that all
	 * taskprocessors in a taskpool need to do when they start.
	 */
	void (*thread_start)(void);
	/*!
	 * \brief Function to call when a taskprocessor ends
	 *
	 * This is useful if there is common cleanup to execute when
	 * a taskprocessor completes
	 */
	void (*thread_end)(void);
};

/*!
 * \brief Create a new taskpool
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * This function creates a taskpool. Tasks may be pushed onto this task pool
 * and will be automatically acted upon by taskprocessors within the pool.
 *
 * Only a single taskpool with a given name may exist. This function will fail
 * if a taskpool with the given name already exists.
 *
 * \param name The unique name for the taskpool
 * \param options The behavioral options for this taskpool
 * \retval NULL Failed to create the taskpool
 * \retval non-NULL The newly-created taskpool
 *
 * \note The \ref ast_taskpool_shutdown function must be called to shut down the
 * taskpool and clean up underlying resources fully.
 */
struct ast_taskpool *ast_taskpool_create(const char *name,
		const struct ast_taskpool_options *options);

/*!
 * \brief Get the current number of taskprocessors in the taskpool
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * \param pool The taskpool to query
 * \retval The number of taskprocessors in the taskpool
 */
size_t ast_taskpool_taskprocessors_count(struct ast_taskpool *pool);

/*!
 * \brief Get the current number of queued tasks in the taskpool
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * \param pool The taskpool to query
 * \retval The number of queued tasks in the taskpool
 */
long ast_taskpool_queue_size(struct ast_taskpool *pool);

/*!
 * \brief Push a task to the taskpool
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * Tasks pushed into the taskpool will be automatically taken by
 * one of the taskprocessors within
 * \param pool The taskpool to add the task to
 * \param task The task to add
 * \param data The parameter for the task
 * \retval 0 success
 * \retval -1 failure
 */
int ast_taskpool_push(struct ast_taskpool *pool, int (*task)(void *data), void *data)
	attribute_warn_unused_result;

/*!
 * \brief Push a task to the taskpool, and wait for completion
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * Tasks pushed into the taskpool will be automatically taken by
 * one of the taskprocessors within
 * \param pool The taskpool to add the task to
 * \param task The task to add
 * \param data The parameter for the task
 * \retval 0 success
 * \retval -1 failure
 */
int ast_taskpool_push_wait(struct ast_taskpool *pool, int (*task)(void *data), void *data)
	attribute_warn_unused_result;

/*!
 * \brief Shut down a taskpool and remove the underlying taskprocessors
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * \param pool The pool to shut down
 *
 * \note This will decrement the reference to the pool
 */
void ast_taskpool_shutdown(struct ast_taskpool *pool);

/*!
 * \brief Get the taskpool serializer currently associated with this thread.
 *
 * \note The returned pointer is valid while the serializer
 * thread is running.
 *
 * \note Use ao2_ref() on serializer if you are going to keep it
 * for another thread.  To unref it you must then use
 * ast_taskprocessor_unreference().
 *
 * \retval serializer on success.
 * \retval NULL on error or no serializer associated with the thread.
 */
 struct ast_taskprocessor *ast_taskpool_serializer_get_current(void);

/*!
 * \brief Serialized execution of tasks within a \ref ast_taskpool.
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * A \ref ast_taskprocessor with the same contract as a default taskprocessor
 * (tasks execute serially) except instead of executing out of a dedicated
 * thread, execution occurs in a taskprocessor from a \ref ast_taskpool.
 *
 * While it guarantees that each task will complete before executing the next,
 * there is no guarantee as to which thread from the \c pool individual tasks
 * will execute. This normally only matters if your code relies on thread
 * specific information, such as thread locals.
 *
 * Use ast_taskprocessor_unreference() to dispose of the returned \ref
 * ast_taskprocessor.
 *
 * Only a single taskprocessor with a given name may exist. This function will fail
 * if a taskprocessor with the given name already exists.
 *
 * \param name Name of the serializer. (must be unique)
 * \param pool \ref ast_taskpool for execution.
 *
 * \return \ref ast_taskprocessor for enqueuing work.
 * \retval NULL on error.
 */
struct ast_taskprocessor *ast_taskpool_serializer(const char *name, struct ast_taskpool *pool);

/*!
 * \brief Serialized execution of tasks within a \ref ast_taskpool.
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * A \ref ast_taskprocessor with the same contract as a default taskprocessor
 * (tasks execute serially) except instead of executing out of a dedicated
 * thread, execution occurs in a taskprocessor from a \ref ast_taskpool.
 *
 * While it guarantees that each task will complete before executing the next,
 * there is no guarantee as to which thread from the \c pool individual tasks
 * will execute. This normally only matters if your code relies on thread
 * specific information, such as thread locals.
 *
 * Use ast_taskprocessor_unreference() to dispose of the returned \ref
 * ast_taskprocessor.
 *
 * Only a single taskprocessor with a given name may exist. This function will fail
 * if a taskprocessor with the given name already exists.
 *
 * \param name Name of the serializer. (must be unique)
 * \param pool \ref ast_taskpool for execution.
 * \param shutdown_group Group shutdown controller. (NULL if no group association)
 *
 * \return \ref ast_taskprocessor for enqueuing work.
 * \retval NULL on error.
 */
struct ast_taskprocessor *ast_taskpool_serializer_group(const char *name,
	 struct ast_taskpool *pool, struct ast_serializer_shutdown_group *shutdown_group);

/*!
 * \brief Push a task to a serializer, and wait for completion
 * \since 23.1.0
 * \since 22.7.0
 * \since 20.17.0
 *
 * \param serializer The serializer to add the task to
 * \param task The task to add
 * \param data The parameter for the task
 * \retval 0 success
 * \retval -1 failure
 */
int ast_taskpool_serializer_push_wait(struct ast_taskprocessor *serializer, int (*task)(void *data), void *data);

#endif /* ASTERISK_TASKPOOL_H */
