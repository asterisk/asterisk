/*
 * Asterisk
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C) Mark Spencer
 * 
 * Distributed under the terms of the GNU General Public License (GPL) Version 2
 *
 * Scheduler Routines (derived from cheops)
 *
 */

#ifndef _ASTERISK_SCHED_H
#define _ASTERISK_SCHED_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Max num of schedule structs */
/*!
 * The max number of schedule structs to keep around
 * for use.  Undefine to disable schedule structure
 * caching. (Only disable this on very low memory 
 * machines)
 */
#define SCHED_MAX_CACHE 128

struct sched_context;

/*! New schedule context */
/* !
 * Create a scheduling context
 * Returns a malloc'd sched_context structure, NULL on failure
 */
extern struct sched_context *sched_context_create(void);

/*! destroys a schedule context */
/*!
 * \param c Context to free
 * Destroys (free's) the given sched_context structure
 * Returns 0 on success, -1 on failure
 */
void sched_context_destroy(struct sched_context *c);

/*! callback for a cheops scheduler */
/*! 
 * A cheops scheduler callback takes a pointer with callback data and
 * returns a 0 if it should not be run again, or non-zero if it should be
 * rescheduled to run again
 */
typedef int (*ast_sched_cb)(void *data);
#define AST_SCHED_CB(a) ((ast_sched_cb)(a))

/*!Adds a scheduled event */
/*! 
 * \param con Schduler context to add
 * \param when how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data data to pass to the callback
 * Schedule an event to take place at some point in the future.  callback 
 * will be called with data as the argument, when milliseconds into the
 * future (approximately)
 * Returns 0 on success, -1 on failure
 */
extern int ast_sched_add(struct sched_context *con, int when, ast_sched_cb callback, void *data);

/*! Deletes a scheduled event */
/*!
 * \param con scheduling context to delete item from
 * \param id ID of the scheduled item to delete
 * Remove this event from being run.  A procedure should not remove its
 * own event, but return 0 instead.
 * Returns 0 on success, -1 on failure
 */
extern int ast_sched_del(struct sched_context *con, int id);

/*! Determines number of seconds until the next outstanding event to take place */
/*!
 * \param con context to act upon
 * Determine the number of seconds until the next outstanding event
 * should take place, and return the number of milliseconds until
 * it needs to be run.  This value is perfect for passing to the poll
 * call.  Returns "-1" if there is nothing there are no scheduled events
 * (and thus the poll should not timeout)
 */
extern int ast_sched_wait(struct sched_context *con);

/*! Runs the queue */
/*!
 * \param con Scheduling context to run
 * Run the queue, executing all callbacks which need to be performed
 * at this time.  Returns the number of events processed.
 */
extern int ast_sched_runq(struct sched_context *con);

/*!Dumps the scheduler contents */
/*!
 * \param con Context to dump
 * Debugging: Dump the contents of the scheduler to stderr
 */
extern void ast_sched_dump(struct sched_context *con);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
