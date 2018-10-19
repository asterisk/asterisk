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


#ifndef _ASTERISK_THREADPOOL_H
#define _ASTERISK_THREADPOOL_H

struct ast_threadpool;
struct ast_taskprocessor;
struct ast_threadpool_listener;

struct ast_threadpool_listener_callbacks {
	/*!
	 * \brief Indicates that the state of threads in the pool has changed
	 *
	 * \param pool The pool whose state has changed
	 * \param listener The threadpool listener
	 * \param active_threads The number of active threads in the pool
	 * \param idle_threads The number of idle threads in the pool
	 */
	void (*state_changed)(struct ast_threadpool *pool,
			struct ast_threadpool_listener *listener,
			int active_threads,
			int idle_threads);
	/*!
	 * \brief Indicates that a task was pushed to the threadpool
	 *
	 * \param pool The pool that had a task pushed
	 * \param listener The threadpool listener
	 * \param was_empty Indicates whether there were any tasks prior to adding the new one.
	 */
	void (*task_pushed)(struct ast_threadpool *pool,
			struct ast_threadpool_listener *listener,
			int was_empty);
	/*!
	 * \brief Indicates the threadpool's taskprocessor has become empty
	 *
	 * \param pool The pool that has become empty
	 * \param listener The threadpool's listener
	 */
	void (*emptied)(struct ast_threadpool *pool, struct ast_threadpool_listener *listener);

	/*!
	 * \brief The threadpool is shutting down
	 *
	 * This would be an opportune time to free the listener's user data
	 * if one wishes. However, it is acceptable to not do so if the user data
	 * should persist beyond the lifetime of the pool.
	 *
	 * \param listener The threadpool's listener
	 */
	void (*shutdown)(struct ast_threadpool_listener *listener);
};

struct ast_threadpool_options {
#define AST_THREADPOOL_OPTIONS_VERSION 1
	/*! Version of threadpool options in use */
	int version;
	/*!
	 * \brief Time limit in seconds for idle threads
	 *
	 * A time of 0 or less will mean no timeout.
	 */
	int idle_timeout;
	/*!
	 * \brief Number of threads to increment pool by
	 *
	 * If a task is added into a pool and no idle thread is
	 * available to activate, then the pool can automatically
	 * grow by the given amount.
	 *
	 * Zero is a perfectly valid value to give here if you want
	 * to control threadpool growth yourself via your listener.
	 */
	int auto_increment;
	/*!
	 * \brief Number of threads the pool will start with
	 *
	 * When the threadpool is allocated, it will immediately size
	 * itself to have this number of threads in it.
	 *
	 * Zero is a valid value if the threadpool should start
	 * without any threads allocated.
	 */
	int initial_size;
	/*!
	 * \brief Maximum number of threads a pool may have
	 *
	 * When the threadpool's size increases, it can never increase
	 * beyond this number of threads.
	 *
	 * Zero is a valid value if the threadpool does not have a
	 * maximum size.
	 */
	int max_size;
	/*!
	 * \brief Function to call when a thread starts
	 *
	 * This is useful if there is something common that all threads
	 * in a threadpool need to do when they start.
	 */
	void (*thread_start)(void);
	/*!
	 * \brief Function to call when a thread ends
	 *
	 * This is useful if there is common cleanup to execute when
	 * a thread completes
	 */
	void (*thread_end)(void);
};

/*!
 * \brief Allocate a threadpool listener
 *
 * This function will call back into the alloc callback for the
 * listener.
 *
 * \param callbacks Listener callbacks to assign to the listener
 * \param user_data User data to be stored in the threadpool listener
 * \retval NULL Failed to allocate the listener
 * \retval non-NULL The newly-created threadpool listener
 */
struct ast_threadpool_listener *ast_threadpool_listener_alloc(
		const struct ast_threadpool_listener_callbacks *callbacks, void *user_data);

/*!
 * \brief Get the threadpool listener's user data
 * \param listener The threadpool listener
 * \return The user data
 */
void *ast_threadpool_listener_get_user_data(const struct ast_threadpool_listener *listener);

/*!
 * \brief Create a new threadpool
 *
 * This function creates a threadpool. Tasks may be pushed onto this thread pool
 * and will be automatically acted upon by threads within the pool.
 *
 * Only a single threadpool with a given name may exist. This function will fail
 * if a threadpool with the given name already exists.
 *
 * \param name The unique name for the threadpool
 * \param listener The listener the threadpool will notify of changes. Can be NULL.
 * \param options The behavioral options for this threadpool
 * \retval NULL Failed to create the threadpool
 * \retval non-NULL The newly-created threadpool
 */
struct ast_threadpool *ast_threadpool_create(const char *name,
		struct ast_threadpool_listener *listener,
		const struct ast_threadpool_options *options);

/*!
 * \brief Set the number of threads for the thread pool
 *
 * This number may be more or less than the current number of
 * threads in the threadpool.
 *
 * \param threadpool The threadpool to adjust
 * \param size The new desired size of the threadpool
 */
void ast_threadpool_set_size(struct ast_threadpool *threadpool, unsigned int size);

/*!
 * \brief Push a task to the threadpool
 *
 * Tasks pushed into the threadpool will be automatically taken by
 * one of the threads within
 * \param pool The threadpool to add the task to
 * \param task The task to add
 * \param data The parameter for the task
 * \retval 0 success
 * \retval -1 failure
 */
int ast_threadpool_push(struct ast_threadpool *pool, int (*task)(void *data), void *data)
	attribute_warn_unused_result;

/*!
 * \brief Shut down a threadpool and destroy it
 *
 * \param pool The pool to shut down
 */
void ast_threadpool_shutdown(struct ast_threadpool *pool);

struct ast_serializer_shutdown_group;

/*!
 * \brief Create a serializer group shutdown control object.
 * \since 13.5.0
 *
 * \return ao2 object to control shutdown of a serializer group.
 */
struct ast_serializer_shutdown_group *ast_serializer_shutdown_group_alloc(void);

/*!
 * \brief Wait for the serializers in the group to shutdown with timeout.
 * \since 13.5.0
 *
 * \param shutdown_group Group shutdown controller. (Returns 0 immediately if NULL)
 * \param timeout Number of seconds to wait for the serializers in the group to shutdown.
 *     Zero if the timeout is disabled.
 *
 * \return Number of seriaizers that did not get shutdown within the timeout.
 */
int ast_serializer_shutdown_group_join(struct ast_serializer_shutdown_group *shutdown_group, int timeout);

/*!
 * \brief Get the threadpool serializer currently associated with this thread.
 * \since 14.0.0
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
struct ast_taskprocessor *ast_threadpool_serializer_get_current(void);

/*!
 * \brief Serialized execution of tasks within a \ref ast_threadpool.
 *
 * \since 12.0.0
 *
 * A \ref ast_taskprocessor with the same contract as a default taskprocessor
 * (tasks execute serially) except instead of executing out of a dedicated
 * thread, execution occurs in a thread from a \ref ast_threadpool. Think of it
 * as a lightweight thread.
 *
 * While it guarantees that each task will complete before executing the next,
 * there is no guarantee as to which thread from the \c pool individual tasks
 * will execute. This normally only matters if your code relys on thread
 * specific information, such as thread locals.
 *
 * Use ast_taskprocessor_unreference() to dispose of the returned \ref
 * ast_taskprocessor.
 *
 * Only a single taskprocessor with a given name may exist. This function will fail
 * if a taskprocessor with the given name already exists.
 *
 * \param name Name of the serializer. (must be unique)
 * \param pool \ref ast_threadpool for execution.
 *
 * \return \ref ast_taskprocessor for enqueuing work.
 * \return \c NULL on error.
 */
struct ast_taskprocessor *ast_threadpool_serializer(const char *name, struct ast_threadpool *pool);

/*!
 * \brief Serialized execution of tasks within a \ref ast_threadpool.
 * \since 13.5.0
 *
 * A \ref ast_taskprocessor with the same contract as a default taskprocessor
 * (tasks execute serially) except instead of executing out of a dedicated
 * thread, execution occurs in a thread from a \ref ast_threadpool. Think of it
 * as a lightweight thread.
 *
 * While it guarantees that each task will complete before executing the next,
 * there is no guarantee as to which thread from the \c pool individual tasks
 * will execute. This normally only matters if your code relys on thread
 * specific information, such as thread locals.
 *
 * Use ast_taskprocessor_unreference() to dispose of the returned \ref
 * ast_taskprocessor.
 *
 * Only a single taskprocessor with a given name may exist. This function will fail
 * if a taskprocessor with the given name already exists.
 *
 * \param name Name of the serializer. (must be unique)
 * \param pool \ref ast_threadpool for execution.
 * \param shutdown_group Group shutdown controller. (NULL if no group association)
 *
 * \return \ref ast_taskprocessor for enqueuing work.
 * \return \c NULL on error.
 */
struct ast_taskprocessor *ast_threadpool_serializer_group(const char *name,
	struct ast_threadpool *pool, struct ast_serializer_shutdown_group *shutdown_group);

/*!
 * \brief Return the size of the threadpool's task queue
 * \since 13.7.0
 */
long ast_threadpool_queue_size(struct ast_threadpool *pool);

#endif /* ASTERISK_THREADPOOL_H */
