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


#ifndef _ASTERISK_THREADPOOL_H
#define _ASTERISK_THREADPOOL_H

struct ast_threadpool;
struct ast_taskprocessor;
struct ast_threadpool_listener;

struct ast_threadpool_listener_callbacks {
	/*!
	 * \brief Allocate the listener's private data
	 *
	 * It is not necessary to assign the private data to the listener.
	 * \param listener The listener the private data will belong to
	 * \retval NULL Failure to allocate private data
	 * \retval non-NULL The newly allocated private data
	 */
	void *(*alloc)(struct ast_threadpool_listener *listener);
	/*!
	 * \brief Indicates that the state of threads in the pool has changed
	 *
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
	 * \param listener The threadpool listener
	 * \param was_empty Indicates whether there were any tasks prior to adding the new one.
	 */
	void (*task_pushed)(struct ast_threadpool *pool,
			struct ast_threadpool_listener *listener,
			int was_empty);
	/*!
	 * \brief Indicates the threadpoo's taskprocessor has become empty
	 * 
	 * \param listener The threadpool's listener
	 */
	void (*emptied)(struct ast_threadpool *pool, struct ast_threadpool_listener *listener);

	/*!
	 * \brief Free the listener's private data
	 * \param private_data The private data to destroy
	 */
	void (*destroy)(void *private_data);
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
	void *private_data;
};

/*!
 * \brief Allocate a threadpool listener
 *
 * This function will call back into the alloc callback for the
 * listener.
 *
 * \param callbacks Listener callbacks to assign to the listener
 * \retval NULL Failed to allocate the listener
 * \retval non-NULL The newly-created threadpool listener
 */
struct ast_threadpool_listener *ast_threadpool_listener_alloc(
		const struct ast_threadpool_listener_callbacks *callbacks);

/*!
 * \brief Create a new threadpool
 *
 * This function creates a threadpool. Tasks may be pushed onto this thread pool
 * in and will be automatically acted upon by threads within the pool.
 *
 * \param listener The listener the threadpool will notify of changes
 * \param initial_size The number of threads for the pool to start with
 * \retval NULL Failed to create the threadpool
 * \retval non-NULL The newly-created threadpool
 */
struct ast_threadpool *ast_threadpool_create(struct ast_threadpool_listener *listener, int initial_size);

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
int ast_threadpool_push(struct ast_threadpool *pool, int (*task)(void *data), void *data);

/*!
 * \brief Shut down a threadpool and destroy it
 *
 * \param pool The pool to shut down
 */
void ast_threadpool_shutdown(struct ast_threadpool *pool);
#endif /* ASTERISK_THREADPOOL_H */
