/*
 * Asterisk
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C) 1999, Adtran, Inc.
 * 
 * Distributed under the terms of the GNU General Public License (GPL) Version 2
 *
 * I/O Managment (derived from Cheops-NG)
 *
 */

#ifndef _IO_H
#define _IO_H

#include <sys/poll.h>		/* For POLL* constants */

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_IO_IN 	POLLIN		/* Input ready */
#define AST_IO_OUT 	POLLOUT 	/* Output ready */
#define AST_IO_PRI	POLLPRI 	/* Priority input ready */

/* Implicitly polled for */
#define AST_IO_ERR	POLLERR 	/* Error condition (errno or getsockopt) */
#define AST_IO_HUP	POLLHUP 	/* Hangup */
#define AST_IO_NVAL	POLLNVAL	/* Invalid fd */

/*
 * An Asterisk IO callback takes its id, a file descriptor, list of events, and
 * callback data as arguments and returns 0 if it should not be
 * run again, or non-zero if it should be run again.
 */

struct io_context;

/* Create a context for I/O operations */
struct io_context *io_context_create();

/* Destroy a context for I/O operations */
void io_context_destroy(struct io_context *ioc);

typedef int (*ast_io_cb)(int *id, int fd, short events, void *cbdata);
#define AST_IO_CB(a) ((ast_io_cb)(a))

/* 
 * Watch for any of revents activites on fd, calling callback with data as 
 * callback data.  Returns a pointer to ID of the IO event, or NULL on failure.
 */
extern int *ast_io_add(struct io_context *ioc, int fd, ast_io_cb callback, short events, void *data);

/*
 * Change an i/o handler, updating fd if > -1, callback if non-null, and revents
 * if >-1, and data if non-null.  Returns a pointero to the ID of the IO event,
 * or NULL on failure.
 */
extern int *ast_io_change(struct io_context *ioc, int *id, int fd, ast_io_cb callback, short events, void *data);

/* 
 * Remove an I/O id from consideration  Returns 0 on success or -1 on failure.
 */
extern int ast_io_remove(struct io_context *ioc, int *id);

/*
 * Wait for I/O to happen, returning after
 * howlong milliseconds, and after processing
 * any necessary I/O.  Returns the number of
 * I/O events which took place.
 */
extern int ast_io_wait(struct io_context *ioc, int howlong);

/*
 * Debugging: Dump everything in the I/O array
 */
extern void ast_io_dump(struct io_context *ioc);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
