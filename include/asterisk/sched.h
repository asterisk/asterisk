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
 * \brief Scheduler Routines (derived from cheops)
 */

#ifndef _ASTERISK_SCHED_H
#define _ASTERISK_SCHED_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! 
 * \brief Remove a scheduler entry
 *
 * This is a loop construct to ensure that
 * the scheduled task get deleted. The idea is that
 * if we loop attempting to remove the scheduled task,
 * then whatever callback had been running will complete
 * and reinsert the task into the scheduler.
 *
 * Since macro expansion essentially works like pass-by-name
 * parameter passing, this macro will still work correctly even
 * if the id of the task to delete changes. This holds as long as 
 * the name of the id which could change is passed to the macro 
 * and not a copy of the value of the id.
 */
#define AST_SCHED_DEL(sched, id) \
	({ \
		int _count = 0; \
		int _sched_res = -1; \
		while (id > -1 && (_sched_res = ast_sched_del(sched, id)) && ++_count < 10) \
			usleep(1); \
		if (_count == 10) { \
			ast_debug(3, "Unable to cancel schedule ID %d.\n", id); \
		} \
		id = -1; \
		(_sched_res); \
	})

#define AST_SCHED_DEL_ACCESSOR(sched, obj, getter, setter) \
	({ \
		int _count = 0; \
		int _sched_res = -1; \
		while (getter(obj) > -1 && (_sched_res = ast_sched_del(sched, getter(obj))) && ++_count < 10) \
			usleep(1); \
		if (_count == 10) { \
			ast_debug(3, "Unable to cancel schedule ID %d.\n", getter(obj)); \
		} \
		setter(obj, -1); \
		(_sched_res); \
	})

/*!
 * \brief schedule task to get deleted and call unref function
 * \sa AST_SCHED_DEL
 * \since 1.6.1
 */
#define AST_SCHED_DEL_UNREF(sched, id, refcall)			\
	do { \
		int _count = 0; \
		while (id > -1 && ast_sched_del(sched, id) && ++_count < 10) { \
			usleep(1); \
		} \
		if (_count == 10) \
			ast_log(LOG_WARNING, "Unable to cancel schedule ID %d.  This is probably a bug (%s: %s, line %d).\n", id, __FILE__, __PRETTY_FUNCTION__, __LINE__); \
		if (id > -1) \
			refcall; \
		id = -1; \
	} while (0);

/*!
 * \brief schedule task to get deleted releasing the lock between attempts
 * \since 1.6.1
 */
#define AST_SCHED_DEL_SPINLOCK(sched, id, lock) \
	({ \
		int _count = 0; \
		int _sched_res = -1; \
		while (id > -1 && (_sched_res = ast_sched_del(sched, id)) && ++_count < 10) { \
			ast_mutex_unlock(lock); \
			usleep(1); \
			ast_mutex_lock(lock); \
		} \
		if (_count == 10) { \
			ast_debug(3, "Unable to cancel schedule ID %d.\n", id); \
		} \
		id = -1; \
		(_sched_res); \
	})

#define AST_SCHED_REPLACE_VARIABLE(id, sched, when, callback, data, variable) \
	do { \
		int _count = 0; \
		while (id > -1 && ast_sched_del(sched, id) && ++_count < 10) { \
			usleep(1); \
		} \
		if (_count == 10) \
			ast_log(LOG_WARNING, "Unable to cancel schedule ID %d.  This is probably a bug (%s: %s, line %d).\n", id, __FILE__, __PRETTY_FUNCTION__, __LINE__); \
		id = ast_sched_add_variable(sched, when, callback, data, variable); \
	} while (0);

#define AST_SCHED_REPLACE(id, sched, when, callback, data) \
		AST_SCHED_REPLACE_VARIABLE(id, sched, when, callback, data, 0)

/*!
 * \note Not currently used in the source?
 * \since 1.6.1
 */
#define AST_SCHED_REPLACE_VARIABLE_UNREF(id, sched, when, callback, data, variable, unrefcall, addfailcall, refcall) \
	do { \
		int _count = 0, _res=1;											 \
		void *_data = (void *)ast_sched_find_data(sched, id);			\
		while (id > -1 && (_res = ast_sched_del(sched, id) && _count++ < 10)) { \
			usleep(1); \
		} \
		if (!_res && _data)							\
			unrefcall;	/* should ref _data! */		\
		if (_count == 10) \
			ast_log(LOG_WARNING, "Unable to cancel schedule ID %d.  This is probably a bug (%s: %s, line %d).\n", id, __FILE__, __PRETTY_FUNCTION__, __LINE__); \
		refcall; \
		id = ast_sched_add_variable(sched, when, callback, data, variable); \
		if (id == -1)  \
			addfailcall;	\
	} while (0);

#define AST_SCHED_REPLACE_UNREF(id, sched, when, callback, data, unrefcall, addfailcall, refcall) \
	AST_SCHED_REPLACE_VARIABLE_UNREF(id, sched, when, callback, data, 0, unrefcall, addfailcall, refcall)

/*!
 * \brief Create a scheduler context
 *
 * \return Returns a malloc'd sched_context structure, NULL on failure
 */
struct ast_sched_context *ast_sched_context_create(void);

/*!
 * \brief destroys a schedule context
 *
 * \param c Context to free
 */
void ast_sched_context_destroy(struct ast_sched_context *c);

/*!
 * \brief scheduler callback
 *
 * A scheduler callback takes a pointer with callback data and
 *
 * \retval 0 if the callback should not be rescheduled
 * \retval non-zero if the callback should be scheduled again
 */
typedef int (*ast_sched_cb)(const void *data);
#define AST_SCHED_CB(a) ((ast_sched_cb)(a))

/*!
 * \brief Clean all scheduled events with matching callback.
 *
 * \param con Scheduler Context
 * \param match Callback to match
 * \param cleanup_cb Callback to run
 *
 * \note The return of cleanup_cb is ignored. No events are rescheduled.
 */
void ast_sched_clean_by_callback(struct ast_sched_context *con, ast_sched_cb match, ast_sched_cb cleanup_cb);

struct ast_cb_names {
	int numassocs;
	char *list[10];
	ast_sched_cb cblist[10];
};

/*!
 * \brief Show statics on what it is in the schedule queue
 * \param con Schedule context to check
 * \param buf dynamic string to store report
 * \param cbnames to check against
 * \since 1.6.1
 */
void ast_sched_report(struct ast_sched_context *con, struct ast_str **buf, struct ast_cb_names *cbnames);

/*!
 * \brief Adds a scheduled event
 *
 * Schedule an event to take place at some point in the future.  callback
 * will be called with data as the argument, when milliseconds into the
 * future (approximately)
 *
 * If callback returns 0, no further events will be re-scheduled
 *
 * \param con Scheduler context to add
 * \param when how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data data to pass to the callback
 *
 * \return Returns a schedule item ID on success, -1 on failure
 */
int ast_sched_add(struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data) attribute_warn_unused_result;

/*!
 * \brief replace a scheduler entry
 * \deprecated You should use the AST_SCHED_REPLACE() macro instead.
 *
 * This deletes the scheduler entry for old_id if it exists, and then
 * calls ast_sched_add to create a new entry.  A negative old_id will
 * be ignored.
 *
 * \retval -1 failure
 * \retval otherwise, returns scheduled item ID
 */
int ast_sched_replace(int old_id, struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data) attribute_warn_unused_result;

/*!
 * \brief Adds a scheduled event with rescheduling support
 *
 * \param con Scheduler context to add
 * \param when how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data data to pass to the callback
 * \param variable If true, the result value of callback function will be
 *       used for rescheduling
 *
 * Schedule an event to take place at some point in the future.  Callback
 * will be called with data as the argument, when milliseconds into the
 * future (approximately)
 *
 * If callback returns 0, no further events will be re-scheduled
 *
 * \return Returns a schedule item ID on success, -1 on failure
 */
int ast_sched_add_variable(struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data, int variable) attribute_warn_unused_result;

/*!
 * \brief replace a scheduler entry
 * \deprecated You should use the AST_SCHED_REPLACE_VARIABLE() macro instead.
 *
 * This deletes the scheduler entry for old_id if it exists, and then
 * calls ast_sched_add to create a new entry.  A negative old_id will
 * be ignored.
 *
 * \retval -1 failure
 * \retval otherwise, returns scheduled item ID
 */
int ast_sched_replace_variable(int old_id, struct ast_sched_context *con, int when, ast_sched_cb callback, const void *data, int variable) attribute_warn_unused_result;

/*! 
 * \brief Find a sched structure and return the data field associated with it. 
 *
 * \param con scheduling context in which to search fro the matching id
 * \param id ID of the scheduled item to find
 * \return the data field from the matching sched struct if found; else return NULL if not found.
 *
 * \since 1.6.1
 */
const void *ast_sched_find_data(struct ast_sched_context *con, int id);

/*!
 * \brief Deletes a scheduled event
 *
 * Remove this event from being run.  A procedure should not remove its own
 * event, but return 0 instead.  In most cases, you should not call this
 * routine directly, but use the AST_SCHED_DEL() macro instead (especially if
 * you don't intend to do something different when it returns failure).
 *
 * \param con scheduling context to delete item from
 * \param id ID of the scheduled item to delete
 *
 * \return Returns 0 on success, -1 on failure
 */
#ifndef AST_DEVMODE
int ast_sched_del(struct ast_sched_context *con, int id) attribute_warn_unused_result;
#else
int _ast_sched_del(struct ast_sched_context *con, int id, const char *file, int line, const char *function) attribute_warn_unused_result;
#define	ast_sched_del(a, b)	_ast_sched_del(a, b, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

/*!
 * \brief Determines number of seconds until the next outstanding event to take place
 *
 * Determine the number of seconds until the next outstanding event
 * should take place, and return the number of milliseconds until
 * it needs to be run.  This value is perfect for passing to the poll
 * call.
 *
 * \param con context to act upon
 *
 * \return Returns "-1" if there is nothing there are no scheduled events
 * (and thus the poll should not timeout)
 */
int ast_sched_wait(struct ast_sched_context *con) attribute_warn_unused_result;

/*!
 * \brief Runs the queue
 *
 * Run the queue, executing all callbacks which need to be performed
 * at this time.
 *
 * \param con Scheduling context to run
 * \param con context to act upon
 *
 * \return Returns the number of events processed.
 */
int ast_sched_runq(struct ast_sched_context *con);

/*!
 * \brief Dumps the scheduler contents
 *
 * Debugging: Dump the contents of the scheduler to stderr
 *
 * \param con Context to dump
 */
void ast_sched_dump(struct ast_sched_context *con);

/*!
 * \brief Returns the number of seconds before an event takes place
 *
 * \param con Context to use
 * \param id Id to dump
 */
long ast_sched_when(struct ast_sched_context *con,int id);

/*!
 * \brief Convenience macro for objects and reference (add)
 *
 */
#define ast_sched_add_object(obj,con,when,callback) ast_sched_add((con),(when),(callback), ASTOBJ_REF((obj)))

/*!
 * \brief Convenience macro for objects and reference (del)
 *
 */
#define ast_sched_del_object(obj,destructor,con,id) do { \
	if ((id) > -1) { \
		ast_sched_del((con),(id)); \
		(id) = -1; \
		ASTOBJ_UNREF((obj),(destructor)); \
	} \
} while(0)

/*!
 * \brief Start a thread for processing scheduler entries
 *
 * \param con the scheduler context this thread will manage
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_sched_start_thread(struct ast_sched_context *con);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_SCHED_H */
