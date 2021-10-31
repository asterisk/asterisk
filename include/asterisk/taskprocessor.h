/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2013, Digium, Inc.
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
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

/*!
 * \file taskprocessor.h
 * \brief An API for managing task processing threads that can be shared across modules
 *
 * \author Dwayne M. Hubbard <dhubbard@digium.com>
 *
 * \note A taskprocessor is a named object containing a task queue that
 * serializes tasks pushed into it by [a] module(s) that reference the taskprocessor.
 * A taskprocessor is created the first time its name is requested via the
 * ast_taskprocessor_get() function or the ast_taskprocessor_create_with_listener()
 * function and destroyed when the taskprocessor reference count reaches zero. A
 * taskprocessor also contains an accompanying listener that is notified when changes
 * in the task queue occur.
 *
 * A task is a wrapper around a task-handling function pointer and a data
 * pointer.  A task is pushed into a taskprocessor queue using the
 * ast_taskprocessor_push(taskprocessor, taskhandler, taskdata) function and freed by the
 * taskprocessor after the task handling function returns.  A module releases its
 * reference to a taskprocessor using the ast_taskprocessor_unreference() function which
 * may result in the destruction of the taskprocessor if the taskprocessor's reference
 * count reaches zero. When the taskprocessor's reference count reaches zero, its
 * listener's shutdown() callback will be called. Any further attempts to execute tasks
 * will be denied.
 *
 * The taskprocessor listener has the flexibility of doling out tasks to best fit the
 * module's needs. For instance, a taskprocessor listener may have a single dispatch
 * thread that handles all tasks, or it may dispatch tasks to a thread pool.
 *
 * There is a default taskprocessor listener that will be used if a taskprocessor is
 * created without any explicit listener. This default listener runs tasks sequentially
 * in a single thread. The listener will execute tasks as long as there are tasks to be
 * processed. When the taskprocessor is shut down, the default listener will stop
 * processing tasks and join its execution thread.
 */

#ifndef __AST_TASKPROCESSOR_H__
#define __AST_TASKPROCESSOR_H__

struct ast_taskprocessor;

/*! \brief Suggested maximum taskprocessor name length (less null terminator). */
#define AST_TASKPROCESSOR_MAX_NAME	70

/*! Default taskprocessor high water level alert trigger */
#define AST_TASKPROCESSOR_HIGH_WATER_LEVEL 500

/*!
 * \brief ast_tps_options for specification of taskprocessor options
 *
 * Specify whether a taskprocessor should be created via ast_taskprocessor_get() if the taskprocessor
 * does not already exist.  The default behavior is to create a taskprocessor if it does not already exist
 * and provide its reference to the calling function.  To only return a reference to a taskprocessor if
 * and only if it exists, use the TPS_REF_IF_EXISTS option in ast_taskprocessor_get().
 */
enum ast_tps_options {
	/*! \brief return a reference to a taskprocessor, create one if it does not exist */
	TPS_REF_DEFAULT = 0,
	/*! \brief return a reference to a taskprocessor ONLY if it already exists */
	TPS_REF_IF_EXISTS = (1 << 0),
};

struct ast_taskprocessor_listener;

struct ast_taskprocessor_listener_callbacks {
	/*!
	 * \brief The taskprocessor has started completely
	 *
	 * This indicates that the taskprocessor is fully set up and the listener
	 * can now start interacting with it.
	 *
	 * \param listener The listener to start
	 */
	int (*start)(struct ast_taskprocessor_listener *listener);
	/*!
	 * \brief Indicates a task was pushed to the processor
	 *
	 * \param listener The listener
	 * \param was_empty If non-zero, the taskprocessor was empty prior to the task being pushed
	 */
	void (*task_pushed)(struct ast_taskprocessor_listener *listener, int was_empty);
	/*!
	 * \brief Indicates the task processor has become empty
	 *
	 * \param listener The listener
	 */
	void (*emptied)(struct ast_taskprocessor_listener *listener);
	/*!
	 * \brief Indicates the taskprocessor wishes to die.
	 *
	 * All operations on the task processor must to be stopped in
	 * this callback. This is an opportune time to free the listener's
	 * user data if it is not going to be used anywhere else.
	 *
	 * After this callback returns, it is NOT safe to operate on the
	 * listener's reference to the taskprocessor.
	 *
	 * \param listener The listener
	 */
	void (*shutdown)(struct ast_taskprocessor_listener *listener);
	void (*dtor)(struct ast_taskprocessor_listener *listener);
};

/*!
 * \brief Get a reference to the listener's taskprocessor
 *
 * This will return the taskprocessor with its reference count increased. Release
 * the reference to this object by using ast_taskprocessor_unreference()
 *
 * \param listener The listener that has the taskprocessor
 * \return The taskprocessor
 */
struct ast_taskprocessor *ast_taskprocessor_listener_get_tps(const struct ast_taskprocessor_listener *listener);

/*!
 * \brief Get the user data from the listener
 * \param listener The taskprocessor listener
 * \return The listener's user data
 */
void *ast_taskprocessor_listener_get_user_data(const struct ast_taskprocessor_listener *listener);

/*!
 * \brief Allocate a taskprocessor listener
 *
 * \since 12.0.0
 *
 * This will result in the listener being allocated with the specified
 * callbacks.
 *
 * \param callbacks The callbacks to assign to the listener
 * \param user_data The user data for the listener
 * \retval NULL Failure
 * \retval non-NULL The newly allocated taskprocessor listener
 */
struct ast_taskprocessor_listener *ast_taskprocessor_listener_alloc(const struct ast_taskprocessor_listener_callbacks *callbacks, void *user_data);

/*!
 * \brief Get a reference to a taskprocessor with the specified name and create the taskprocessor if necessary
 *
 * The default behavior of instantiating a taskprocessor if one does not already exist can be
 * disabled by specifying the TPS_REF_IF_EXISTS ast_tps_options as the second argument to ast_taskprocessor_get().
 * \param name The name of the taskprocessor
 * \param create Use 0 by default or specify TPS_REF_IF_EXISTS to return NULL if the taskprocessor does
 * not already exist
 * return A pointer to a reference counted taskprocessor under normal conditions, or NULL if the
 * TPS_REF_IF_EXISTS reference type is specified and the taskprocessor does not exist
 * \since 1.6.1
 */
struct ast_taskprocessor *ast_taskprocessor_get(const char *name, enum ast_tps_options create);

/*!
 * \brief Create a taskprocessor with a custom listener
 *
 * \since 12.0.0
 *
 * Note that when a taskprocessor is created in this way, it does not create
 * any threads to execute the tasks. This job is left up to the listener.
 * The listener's start() callback will be called during this function.
 *
 * \param name The name of the taskprocessor to create
 * \param listener The listener for operations on this taskprocessor
 * \retval NULL Failure
 * \reval non-NULL success
 */
struct ast_taskprocessor *ast_taskprocessor_create_with_listener(const char *name, struct ast_taskprocessor_listener *listener);

/*!
 * \brief Sets the local data associated with a taskprocessor.
 *
 * \since 12.0.0
 *
 * See ast_taskprocessor_push_local().
 *
 * \param tps Task processor.
 * \param local_data Local data to associate with \a tps.
 */
void ast_taskprocessor_set_local(struct ast_taskprocessor *tps, void *local_data);

/*!
 * \brief Unreference the specified taskprocessor and its reference count will decrement.
 *
 * Taskprocessors use astobj2 and will unlink from the taskprocessor singleton container and destroy
 * themself when the taskprocessor reference count reaches zero.
 * \param tps taskprocessor to unreference
 * \return NULL
 * \since 1.6.1
 */
void *ast_taskprocessor_unreference(struct ast_taskprocessor *tps);

/*!
 * \brief Push a task into the specified taskprocessor queue and signal the taskprocessor thread
 * \param tps The taskprocessor structure
 * \param task_exe The task handling function to push into the taskprocessor queue
 * \param datap The data to be used by the task handling function
 * \retval 0 success
 * \retval -1 failure
 * \since 1.6.1
 */
int ast_taskprocessor_push(struct ast_taskprocessor *tps, int (*task_exe)(void *datap), void *datap)
	attribute_warn_unused_result;

/*! \brief Local data parameter */
struct ast_taskprocessor_local {
	/*! Local data, associated with the taskprocessor. */
	void *local_data;
	/*! Data pointer passed with this task. */
	void *data;
};

/*!
 * \brief Push a task into the specified taskprocessor queue and signal the
 * taskprocessor thread.
 *
 * The callback receives a \ref ast_taskprocessor_local struct, which contains
 * both the provided \a datap pointer, and any local data set on the
 * taskprocessor with ast_taskprocessor_set_local().
 *
 * \param tps The taskprocessor structure
 * \param task_exe The task handling function to push into the taskprocessor queue
 * \param datap The data to be used by the task handling function
 * \retval 0 success
 * \retval -1 failure
 * \since 12.0.0
 */
int ast_taskprocessor_push_local(struct ast_taskprocessor *tps,
	int (*task_exe)(struct ast_taskprocessor_local *local), void *datap)
	attribute_warn_unused_result;

/*!
 * \brief Indicate the taskprocessor is suspended.
 *
 * \since 13.12.0
 *
 * \param tps Task processor.
 * \retval 0 success
 * \retval -1 failure
 */
int ast_taskprocessor_suspend(struct ast_taskprocessor *tps);

/*!
 * \brief Indicate the taskprocessor is unsuspended.
 *
 * \since 13.12.0
 *
 * \param tps Task processor.
 * \retval 0 success
 * \retval -1 failure
 */
int ast_taskprocessor_unsuspend(struct ast_taskprocessor *tps);

/*!
 * \brief Get the task processor suspend status
 *
 * \since 13.12.0
 *
 * \param tps Task processor.
 * \retval non-zero if the task processor is suspended
 */
int ast_taskprocessor_is_suspended(struct ast_taskprocessor *tps);

/*!
 * \brief Pop a task off the taskprocessor and execute it.
 *
 * \since 12.0.0
 *
 * \param tps The taskprocessor from which to execute.
 * \retval 0 There is no further work to be done.
 * \retval 1 Tasks still remain in the taskprocessor queue.
 */
int ast_taskprocessor_execute(struct ast_taskprocessor *tps);

/*!
 * \brief Am I the given taskprocessor's current task.
 * \since 12.7.0
 *
 * \param tps Taskprocessor to check.
 *
 * \retval non-zero if current thread is the taskprocessor thread.
 */
int ast_taskprocessor_is_task(struct ast_taskprocessor *tps);

/*!
 * \brief Get the next sequence number to create a human friendly taskprocessor name.
 * \since 13.8.0
 *
 * \return Sequence number for use in creating human friendly taskprocessor names.
 */
unsigned int ast_taskprocessor_seq_num(void);

/*!
 * \brief Append the next sequence number to the given string, and copy into the buffer.
 *
 * \param buf Where to copy the appended taskprocessor name.
 * \param size How large is buf including null terminator.
 * \param name A name to append the sequence number to.
 */
void ast_taskprocessor_name_append(char *buf, unsigned int size, const char *name);

/*!
 * \brief Build a taskprocessor name with a sequence number on the end.
 * \since 13.8.0
 *
 * \param buf Where to put the built taskprocessor name.
 * \param size How large is buf including null terminator.
 * \param format printf format to create the non-sequenced part of the name.
 *
 * \note The user supplied part of the taskprocessor name is truncated
 * to allow the full sequence number to be appended within the supplied
 * buffer size.
 *
 * \return Nothing
 */
void __attribute__((format(printf, 3, 4))) ast_taskprocessor_build_name(char *buf, unsigned int size, const char *format, ...);

/*!
 * \brief Return the name of the taskprocessor singleton
 * \since 1.6.1
 */
const char *ast_taskprocessor_name(struct ast_taskprocessor *tps);

/*!
 * \brief Return the current size of the taskprocessor queue
 * \since 13.7.0
 */
long ast_taskprocessor_size(struct ast_taskprocessor *tps);

/*!
 * \brief Get the current taskprocessor high water alert count.
 * \since 13.10.0
 *
 * \retval 0 if no taskprocessors are in high water alert.
 * \retval non-zero if some task processors are in high water alert.
 */
unsigned int ast_taskprocessor_alert_get(void);


/*!
 * \brief Get the current taskprocessor high water alert count by subsystem.
 * \since 13.26.0
 * \since 16.3.0
 *
 * \param subsystem The subsystem name
 *
 * \retval 0 if no taskprocessors are in high water alert.
 * \retval non-zero if some task processors are in high water alert.
 */
unsigned int ast_taskprocessor_get_subsystem_alert(const char *subsystem);

/*!
 * \brief Set the high and low alert water marks of the given taskprocessor queue.
 * \since 13.10.0
 *
 * \param tps Taskprocessor to update queue water marks.
 * \param low_water New queue low water mark. (-1 to set as 90% of high_water)
 * \param high_water New queue high water mark.
 *
 * \retval 0 on success.
 * \retval -1 on error (water marks not changed).
 */
int ast_taskprocessor_alert_set_levels(struct ast_taskprocessor *tps, long low_water, long high_water);

#endif /* __AST_TASKPROCESSOR_H__ */
