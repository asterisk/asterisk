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
 * \brief I/O Management (derived from Cheops-NG)
 */

#ifndef _ASTERISK_IO_H
#define _ASTERISK_IO_H

#include "asterisk/poll-compat.h"
#include "asterisk/netsock2.h"

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

/*! \brief
 * An Asterisk IO callback takes its id, a file descriptor, list of events, and
 * callback data as arguments and returns 0 if it should not be
 * run again, or non-zero if it should be run again.
 */

struct io_context;

/*!
 * \brief Creates a context
 * Create a context for I/O operations
 * Basically mallocs an IO structure and sets up some default values.
 * \return an allocated io_context structure
 */
struct io_context *io_context_create(void);

/*!
 * \brief Destroys a context
 * \param ioc structure to destroy
 * Destroy a context for I/O operations
 * Frees all memory associated with the given io_context structure along with the structure itself
 */
void io_context_destroy(struct io_context *ioc);

typedef int (*ast_io_cb)(int *id, int fd, short events, void *cbdata);
#define AST_IO_CB(a) ((ast_io_cb)(a))

/*!
 * \brief Adds an IO context
 * \param ioc which context to use
 * \param fd which fd to monitor
 * \param callback callback function to run
 * \param events event mask of events to wait for
 * \param data data to pass to the callback
 * Watch for any of revents activites on fd, calling callback with data as
 * callback data.
 * \retval a pointer to ID of the IO event
 * \retval NULL on failure
 */
int *ast_io_add(struct io_context *ioc, int fd, ast_io_cb callback, short events, void *data);

/*!
 * \brief Changes an IO handler
 * \param ioc which context to use
 * \param id
 * \param fd the fd you wish it to contain now
 * \param callback new callback function
 * \param events event mask to wait for
 * \param data data to pass to the callback function
 * Change an I/O handler, updating fd if > -1, callback if non-null,
 * and revents if >-1, and data if non-null.
 * \retval a pointer to the ID of the IO event
 * \retval NULL on failure
 */
int *ast_io_change(struct io_context *ioc, int *id, int fd, ast_io_cb callback, short events, void *data);

/*!
 * \brief Removes an IO context
 * \param ioc which io_context to remove it from
 * \param id which ID to remove
 * Remove an I/O id from consideration
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_io_remove(struct io_context *ioc, int *id);

/*!
 * \brief Waits for IO
 * \param ioc which context to act upon
 * \param howlong how many milliseconds to wait
 * Wait for I/O to happen, returning after
 * howlong milliseconds, and after processing
 * any necessary I/O.
 * \return he number of I/O events which took place.
 */
int ast_io_wait(struct io_context *ioc, int howlong);

/*!
 * \brief Dumps the IO array.
 * Debugging: Dump everything in the I/O array
 */
void ast_io_dump(struct io_context *ioc);

/*! Set fd into non-echoing mode (if fd is a tty) */

int ast_hide_password(int fd);

/*!
 * \brief Restores TTY mode.
 * Call with result from previous ast_hide_password
 */
int ast_restore_tty(int fd, int oldstatus);

int ast_get_termcols(int fd);

/*!
 * \brief a wrapper for sd_notify(): notify systemd of any state changes.
 * \param state a string that states the changes. See sd_notify(3).
 * The wrapper does nothing if systemd ('s development headers) was not
 * detected on the system.
 * \returns >=0 on success, negative value on error.
 */
int ast_sd_notify(const char *state);

/*!
 * \brief Find a listening file descriptor provided by socket activation.
 * \param type SOCK_STREAM or SOCK_DGRAM
 * \param addr The socket address of the bound listener.
 * \retval <0 No match.
 * \retval >0 File Descriptor matching sockaddr.
 *
 * \note This function returns -1 if systemd's development headers were not
 * detected on the system.
 */
int ast_sd_get_fd(int type, const struct ast_sockaddr *addr);

/*!
 * \brief Find a listening AF_LOCAL file descriptor provided by socket activation.
 * \param type SOCK_STREAM or SOCK_DGRAM
 * \param path The path of the listener.
 * \retval <0 No match.
 * \retval >0 File Descriptor matching path.
 *
 * \note This function returns -1 if systemd's development headers were not
 * detected on the system.
 */
int ast_sd_get_fd_un(int type, const char *path);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_IO_H */
