/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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


/*! \brief Max num of schedule structs
 * \note The max number of schedule structs to keep around
 * for use.  Undefine to disable schedule structure
 * caching. (Only disable this on very low memory
 * machines)
 */
#define SCHED_MAX_CACHE 128

/*! \brief a loop construct to ensure that
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
		if (_count == 10 && option_debug > 2) { \
			ast_log(LOG_DEBUG, "Unable to cancel schedule ID %d.\n", id); \
		} \
		id = -1; \
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
		if (_count == 10 && option_debug > 2) { \
			ast_log(LOG_DEBUG, "Unable to cancel schedule ID %d.\n", id); \
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

struct sched_context;

/*! \brief New schedule context
 * \note Create a scheduling context
 * \return Returns a malloc'd sched_context structure, NULL on failure
 */
struct sched_context *sched_context_create(void);

/*! \brief destroys a schedule context
 * Destroys (free's) the given sched_context structure
 * \param c Context to free
 * \return Returns 0 on success, -1 on failure
 */
void sched_context_destroy(struct sched_context *c);

/*! \brief callback for a cheops scheduler
 * A cheops scheduler callback takes a pointer with callback data and
 * \return returns a 0 if it should not be run again, or non-zero if it should be
 * rescheduled to run again
 */
typedef int (*ast_sched_cb)(const void *data);
#define AST_SCHED_CB(a) ((ast_sched_cb)(a))

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
void ast_sched_report(struct sched_context *con, struct ast_str **buf, struct ast_cb_names *cbnames);
		
/*! \brief Adds a scheduled event
 * Schedule an event to take place at some point in the future.  callback
 * will be called with data as the argument, when milliseconds into the
 * future (approximately)
 * If callback returns 0, no further events will be re-scheduled
 * \param con Scheduler context to add
 * \param when how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data data to pass to the callback
 * \return Returns a schedule item ID on success, -1 on failure
 */
int ast_sched_add(struct sched_context *con, int when, ast_sched_cb callback, const void *data) attribute_warn_unused_result;

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
int ast_sched_replace(int old_id, struct sched_context *con, int when, ast_sched_cb callback, const void *data) attribute_warn_unused_result;

/*!Adds a scheduled event with rescheduling support
 * \param con Scheduler context to add
 * \param when how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data data to pass to the callback
 * \param variable If true, the result value of callback function will be
 *       used for rescheduling
 * Schedule an event to take place at some point in the future.  Callback
 * will be called with data as the argument, when milliseconds into the
 * future (approximately)
 * If callback returns 0, no further events will be re-scheduled
 * \return Returns a schedule item ID on success, -1 on failure
 */
int ast_sched_add_variable(struct sched_context *con, int when, ast_sched_cb callback, const void *data, int variable) attribute_warn_unused_result;

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
int ast_sched_replace_variable(int old_id, struct sched_context *con, int when, ast_sched_cb callback, const void *data, int variable) attribute_warn_unused_result;

	
/*! 
 * \brief Find a sched structure and return the data field associated with it. 
 * \param con scheduling context in which to search fro the matching id
 * \param id ID of the scheduled item to find
 * \return the data field from the matching sched struct if found; else return NULL if not found.
 * \since 1.6.1
 */

const void *ast_sched_find_data(struct sched_context *con, int id);
	
/*! \brief Deletes a scheduled event
 * Remove this event from being run.  A procedure should not remove its own
 * event, but return 0 instead.  In most cases, you should not call this
 * routine directly, but use the AST_SCHED_DEL() macro instead (especially if
 * you don't intend to do something different when it returns failure).
 * \param con scheduling context to delete item from
 * \param id ID of the scheduled item to delete
 * \return Returns 0 on success, -1 on failure
 */
#ifndef AST_DEVMODE
int ast_sched_del(struct sched_context *con, int id) attribute_warn_unused_result;
#else
int _ast_sched_del(struct sched_context *con, int id, const char *file, int line, const char *function) attribute_warn_unused_result;
#define	ast_sched_del(a, b)	_ast_sched_del(a, b, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

/*! \brief Determines number of seconds until the next outstanding event to take place
 * Determine the number of seconds until the next outstanding event
 * should take place, and return the number of milliseconds until
 * it needs to be run.  This value is perfect for passing to the poll
 * call.
 * \param con context to act upon
 * \return Returns "-1" if there is nothing there are no scheduled events
 * (and thus the poll should not timeout)
 */
int ast_sched_wait(struct sched_context *con) attribute_warn_unused_result;

/*! \brief Runs the queue
 * \param con Scheduling context to run
 * Run the queue, executing all callbacks which need to be performed
 * at this time.
 * \param con context to act upon
 * \return Returns the number of events processed.
 */
int ast_sched_runq(struct sched_context *con);

/*! \brief Dumps the scheduler contents
 * Debugging: Dump the contents of the scheduler to stderr
 * \param con Context to dump
 */
void ast_sched_dump(struct sched_context *con);

/*! \brief Returns the number of seconds before an event takes place
 * \param con Context to use
 * \param id Id to dump
 */
long ast_sched_when(struct sched_context *con,int id);

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
 * \brief An opaque type representing a scheduler thread
 *
 * The purpose of the ast_sched_thread API is to provide a common implementation
 * of the case where a module wants to have a dedicated thread for handling the
 * scheduler.
 */
struct ast_sched_thread;

/*!
 * \brief Create a scheduler with a dedicated thread
 *
 * This function should be used to allocate a scheduler context and a dedicated
 * thread for processing scheduler entries.  The thread is started immediately.
 *
 * \retval NULL error
 * \retval non-NULL a handle to the scheduler and its dedicated thread.
 */
struct ast_sched_thread *ast_sched_thread_create(void);

/*!
 * \brief Destroy a scheduler and its thread
 *
 * This function is used to destroy a scheduler context and the dedicated thread
 * that was created for handling scheduler entries.  Any entries in the scheduler
 * that have not yet been processed will be thrown away.  Once this function is
 * called, the handle must not be used again.
 *
 * \param st the handle to the scheduler and thread
 *
 * \return NULL for convenience
 */
struct ast_sched_thread *ast_sched_thread_destroy(struct ast_sched_thread *st);

/*!
 * \brief Add a scheduler entry
 *
 * \param st the handle to the scheduler and thread
 * \param when the number of ms in the future to run the task.  A value <= 0
 *        is treated as "run now".
 * \param cb the function to call when the scheduled time arrives
 * \param data the parameter to pass to the scheduler callback
 *
 * \retval -1 Failure
 * \retval >=0 Sched ID of added task
 */
int ast_sched_thread_add(struct ast_sched_thread *st, int when, ast_sched_cb cb,
		const void *data);

/*!
 * \brief Add a variable reschedule time scheduler entry
 *
 * \param st the handle to the scheduler and thread
 * \param when the number of ms in the future to run the task.  A value <= 0
 *        is treated as "run now".
 * \param cb the function to call when the scheduled time arrives
 * \param data the parameter to pass to the scheduler callback
 * \param variable If this value is non-zero, then the scheduler will use the return
 *        value of the scheduler as the amount of time in the future to run the
 *        task again.  Normally, a return value of 0 means do not re-schedule, and
 *        non-zero means re-schedule using the time provided when the scheduler
 *        entry was first created.
 *
 * \retval -1 Failure
 * \retval >=0 Sched ID of added task
 */
int ast_sched_thread_add_variable(struct ast_sched_thread *st, int when, ast_sched_cb cb,
		const void *data, int variable);

/*!
 * \brief Get the scheduler context for a given ast_sched_thread
 *
 * This function should be used only when direct access to the scheduler context
 * is required.  Its use is discouraged unless necessary.  The cases where 
 * this is currently required is when you want to take advantage of one of the 
 * AST_SCHED macros.
 *
 * \param st the handle to the scheduler and thread
 *
 * \return the sched_context associated with an ast_sched_thread
 */
struct sched_context *ast_sched_thread_get_context(struct ast_sched_thread *st);

/*!
 * \brief Delete a scheduler entry
 *
 * This uses the AST_SCHED_DEL macro internally.
 *
 * \param st the handle to the scheduler and thread
 * \param id scheduler entry id to delete
 *
 * \retval 0 success
 * \retval non-zero failure
 */
#define ast_sched_thread_del(st, id) ({ \
	struct sched_context *__tmp_context = ast_sched_thread_get_context(st); \
	AST_SCHED_DEL(__tmp_context, id); \
})

/*!
 * \brief Force re-processing of the scheduler context
 *
 * \param st the handle to the scheduler and thread
 *
 * \return nothing
 */
void ast_sched_thread_poke(struct ast_sched_thread *st);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_SCHED_H */
