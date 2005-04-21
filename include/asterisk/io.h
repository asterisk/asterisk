/*
 * Asterisk
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C) Mark Spencer
 * 
 * Distributed under the terms of the GNU General Public License (GPL) Version 2
 *
 * I/O Managment (derived from Cheops-NG)
 *
 */

#ifndef _IO_H
#define _IO_H

#ifdef __APPLE__
#include "asterisk/poll-compat.h"
#else
#include <sys/poll.h>		/* For POLL* constants */
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Input ready */
#define AST_IO_IN 	POLLIN
/*! Output ready */
#define AST_IO_OUT 	POLLOUT
/*! Priority input ready */
#define AST_IO_PRI	POLLPRI

/* Implicitly polled for */
/*! Error condition (errno or getsockopt) */
#define AST_IO_ERR	POLLERR
/*! Hangup */
#define AST_IO_HUP	POLLHUP
/*! Invalid fd */
#define AST_IO_NVAL	POLLNVAL

/*
 * An Asterisk IO callback takes its id, a file descriptor, list of events, and
 * callback data as arguments and returns 0 if it should not be
 * run again, or non-zero if it should be run again.
 */

struct io_context;

/*! Creates a context */
/*!
 * Create a context for I/O operations
 * Basically mallocs an IO structure and sets up some default values.
 * Returns an allocated io_context structure
 */
extern struct io_context *io_context_create(void);

/*! Destroys a context */
/*
 * \param ioc structure to destroy
 * Destroy a context for I/O operations
 * Frees all memory associated with the given io_context structure along with the structure itself
 */
extern void io_context_destroy(struct io_context *ioc);

typedef int (*ast_io_cb)(int *id, int fd, short events, void *cbdata);
#define AST_IO_CB(a) ((ast_io_cb)(a))

/*! Adds an IO context */
/*! 
 * \param ioc which context to use
 * \param fd which fd to monitor
 * \param callback callback function to run
 * \param events event mask of events to wait for
 * \param data data to pass to the callback
 * Watch for any of revents activites on fd, calling callback with data as 
 * callback data.  Returns a pointer to ID of the IO event, or NULL on failure.
 */
extern int *ast_io_add(struct io_context *ioc, int fd, ast_io_cb callback, short events, void *data);

/*! Changes an IO handler */
/*!
 * \param ioc which context to use
 * \param id
 * \param fd the fd you wish it to contain now
 * \param callback new callback function
 * \param events event mask to wait for
 * \param data data to pass to the callback function
 * Change an i/o handler, updating fd if > -1, callback if non-null, and revents
 * if >-1, and data if non-null.  Returns a pointero to the ID of the IO event,
 * or NULL on failure.
 */
extern int *ast_io_change(struct io_context *ioc, int *id, int fd, ast_io_cb callback, short events, void *data);

/*! Removes an IO context */
/*! 
 * \param ioc which io_context to remove it from
 * \param id which ID to remove
 * Remove an I/O id from consideration  Returns 0 on success or -1 on failure.
 */
extern int ast_io_remove(struct io_context *ioc, int *id);

/*! Waits for IO */
/*!
 * \param ioc which context to act upon
 * \param howlong how many milliseconds to wait
 * Wait for I/O to happen, returning after
 * howlong milliseconds, and after processing
 * any necessary I/O.  Returns the number of
 * I/O events which took place.
 */
extern int ast_io_wait(struct io_context *ioc, int howlong);

/*! Dumps the IO array */
/*
 * Debugging: Dump everything in the I/O array
 */
extern void ast_io_dump(struct io_context *ioc);

/*! Set fd into non-echoing mode (if fd is a tty) */

extern int ast_hide_password(int fd);

/*! Restores TTY mode */
/*
 * Call with result from previous ast_hide_password
 */
extern int ast_restore_tty(int fd, int oldstatus);

extern int ast_get_termcols(int fd);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
