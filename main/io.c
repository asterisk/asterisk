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
 *
 * \brief I/O Managment (Derived from Cheops-NG)
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <termios.h>
#include <sys/ioctl.h>

#include "asterisk/io.h"
#include "asterisk/utils.h"

#ifdef DEBUG_IO
#define DEBUG DEBUG_M
#else
#define DEBUG(a)
#endif

/*! \brief
 * Kept for each file descriptor
 */
struct io_rec {
	ast_io_cb callback;		/*!< What is to be called */
	void *data;			/*!< Data to be passed */
	int *id;			/*!< ID number */
};

/* These two arrays are keyed with
   the same index.  it's too bad that
   pollfd doesn't have a callback field
   or something like that.  They grow as
   needed, by GROW_SHRINK_SIZE structures
   at once */

#define GROW_SHRINK_SIZE 512

/*! \brief Global IO variables are now in a struct in order to be
   made threadsafe */
struct io_context {
	struct pollfd *fds;           /*!< Poll structure */
	struct io_rec *ior;           /*!< Associated I/O records */
	unsigned int fdcnt;           /*!< First available fd */
	unsigned int maxfdcnt;        /*!< Maximum available fd */
	int current_ioc;              /*!< Currently used io callback */
	int needshrink;               /*!< Whether something has been deleted */
};

/*! \brief Create an I/O context */
struct io_context *io_context_create(void)
{
	struct io_context *tmp = NULL;

	if (!(tmp = ast_malloc(sizeof(*tmp))))
		return NULL;

	tmp->needshrink = 0;
	tmp->fdcnt = 0;
	tmp->maxfdcnt = GROW_SHRINK_SIZE/2;
	tmp->current_ioc = -1;

	if (!(tmp->fds = ast_calloc(1, (GROW_SHRINK_SIZE / 2) * sizeof(*tmp->fds)))) {
		ast_free(tmp);
		tmp = NULL;
	} else {
		if (!(tmp->ior = ast_calloc(1, (GROW_SHRINK_SIZE / 2) * sizeof(*tmp->ior)))) {
			ast_free(tmp->fds);
			ast_free(tmp);
			tmp = NULL;
		}
	}

	return tmp;
}

void io_context_destroy(struct io_context *ioc)
{
	/* Free associated memory with an I/O context */
	if (ioc->fds)
		ast_free(ioc->fds);
	if (ioc->ior)
		ast_free(ioc->ior);

	ast_free(ioc);
}

/*! \brief
 * Grow the size of our arrays.
 * \return 0 on success or -1 on failure
 */
static int io_grow(struct io_context *ioc)
{
	void *tmp;

	DEBUG(ast_debug(1, "io_grow()\n"));

	ioc->maxfdcnt += GROW_SHRINK_SIZE;

	if ((tmp = ast_realloc(ioc->ior, (ioc->maxfdcnt + 1) * sizeof(*ioc->ior)))) {
		ioc->ior = tmp;
		if ((tmp = ast_realloc(ioc->fds, (ioc->maxfdcnt + 1) * sizeof(*ioc->fds)))) {
			ioc->fds = tmp;
		} else {
			/*
			 * Failed to allocate enough memory for the pollfd.  Not
			 * really any need to shrink back the iorec's as we'll
			 * probably want to grow them again soon when more memory
			 * is available, and then they'll already be the right size
			 */
			ioc->maxfdcnt -= GROW_SHRINK_SIZE;
			return -1;
		}
	} else {
		/*
		 * Memory allocation failure.  We return to the old size, and
		 * return a failure
		 */
		ioc->maxfdcnt -= GROW_SHRINK_SIZE;
		return -1;
	}

	return 0;
}

/*! \brief
 * Add a new I/O entry for this file descriptor
 * with the given event mask, to call callback with
 * data as an argument.
 * \return Returns NULL on failure.
 */
int *ast_io_add(struct io_context *ioc, int fd, ast_io_cb callback, short events, void *data)
{
	int *ret;

	DEBUG(ast_debug(1, "ast_io_add()\n"));

	if (ioc->fdcnt >= ioc->maxfdcnt) {
		/*
		 * We don't have enough space for this entry.  We need to
		 * reallocate maxfdcnt poll fd's and io_rec's, or back out now.
		 */
		if (io_grow(ioc))
			return NULL;
	}

	/*
	 * At this point, we've got sufficiently large arrays going
	 * and we can make an entry for it in the pollfd and io_r
	 * structures.
	 */
	ioc->fds[ioc->fdcnt].fd = fd;
	ioc->fds[ioc->fdcnt].events = events;
	ioc->fds[ioc->fdcnt].revents = 0;
	ioc->ior[ioc->fdcnt].callback = callback;
	ioc->ior[ioc->fdcnt].data = data;

	if (!(ioc->ior[ioc->fdcnt].id = ast_malloc(sizeof(*ioc->ior[ioc->fdcnt].id)))) {
		/* Bonk if we couldn't allocate an int */
		return NULL;
	}

	*(ioc->ior[ioc->fdcnt].id) = ioc->fdcnt;
	ret = ioc->ior[ioc->fdcnt].id;
	ioc->fdcnt++;

	return ret;
}

int *ast_io_change(struct io_context *ioc, int *id, int fd, ast_io_cb callback, short events, void *data)
{
	/* If this id exceeds our file descriptor count it doesn't exist here */
	if (*id > ioc->fdcnt)
		return NULL;

	if (fd > -1)
		ioc->fds[*id].fd = fd;
	if (callback)
		ioc->ior[*id].callback = callback;
	if (events)
		ioc->fds[*id].events = events;
	if (data)
		ioc->ior[*id].data = data;

	return id;
}

static int io_shrink(struct io_context *ioc)
{
	int getfrom, putto = 0;

	/*
	 * Bring the fields from the very last entry to cover over
	 * the entry we are removing, then decrease the size of the
	 * arrays by one.
	 */
	for (getfrom = 0; getfrom < ioc->fdcnt; getfrom++) {
		if (ioc->ior[getfrom].id) {
			/* In use, save it */
			if (getfrom != putto) {
				ioc->fds[putto] = ioc->fds[getfrom];
				ioc->ior[putto] = ioc->ior[getfrom];
				*(ioc->ior[putto].id) = putto;
			}
			putto++;
		}
	}
	ioc->fdcnt = putto;
	ioc->needshrink = 0;
	/* FIXME: We should free some memory if we have lots of unused
	   io structs */
	return 0;
}

int ast_io_remove(struct io_context *ioc, int *_id)
{
	int x;

	if (!_id) {
		ast_log(LOG_WARNING, "Asked to remove NULL?\n");
		return -1;
	}

	for (x = 0; x < ioc->fdcnt; x++) {
		if (ioc->ior[x].id == _id) {
			/* Free the int immediately and set to NULL so we know it's unused now */
			ast_free(ioc->ior[x].id);
			ioc->ior[x].id = NULL;
			ioc->fds[x].events = 0;
			ioc->fds[x].revents = 0;
			ioc->needshrink = 1;
			if (ioc->current_ioc == -1)
				io_shrink(ioc);
			return 0;
		}
	}

	ast_log(LOG_NOTICE, "Unable to remove unknown id %p\n", _id);

	return -1;
}

/*! \brief
 * Make the poll call, and call
 * the callbacks for anything that needs
 * to be handled
 */
int ast_io_wait(struct io_context *ioc, int howlong)
{
	int res, x, origcnt;

	DEBUG(ast_debug(1, "ast_io_wait()\n"));

	if ((res = ast_poll(ioc->fds, ioc->fdcnt, howlong)) <= 0) {
		return res;
	}

	/* At least one event tripped */
	origcnt = ioc->fdcnt;
	for (x = 0; x < origcnt; x++) {
		/* Yes, it is possible for an entry to be deleted and still have an
		   event waiting if it occurs after the original calling id */
		if (ioc->fds[x].revents && ioc->ior[x].id) {
			/* There's an event waiting */
			ioc->current_ioc = *ioc->ior[x].id;
			if (ioc->ior[x].callback) {
				if (!ioc->ior[x].callback(ioc->ior[x].id, ioc->fds[x].fd, ioc->fds[x].revents, ioc->ior[x].data)) {
					/* Time to delete them since they returned a 0 */
					ast_io_remove(ioc, ioc->ior[x].id);
				}
			}
			ioc->current_ioc = -1;
		}
	}

	if (ioc->needshrink)
		io_shrink(ioc);

	return res;
}

void ast_io_dump(struct io_context *ioc)
{
	/*
	 * Print some debugging information via
	 * the logger interface
	 */
	int x;

	ast_debug(1, "Asterisk IO Dump: %u entries, %u max entries\n", ioc->fdcnt, ioc->maxfdcnt);
	ast_debug(1, "================================================\n");
	ast_debug(1, "| ID    FD     Callback    Data        Events  |\n");
	ast_debug(1, "+------+------+-----------+-----------+--------+\n");
	for (x = 0; x < ioc->fdcnt; x++) {
		ast_debug(1, "| %.4d | %.4d | %p | %p | %.6x |\n",
				*ioc->ior[x].id,
				ioc->fds[x].fd,
				ioc->ior[x].callback,
				ioc->ior[x].data,
				(unsigned)ioc->fds[x].events);
	}
	ast_debug(1, "================================================\n");
}

/* Unrelated I/O functions */

int ast_hide_password(int fd)
{
	struct termios tios;
	int res;
	int old;
	if (!isatty(fd))
		return -1;
	res = tcgetattr(fd, &tios);
	if (res < 0)
		return -1;
	old = tios.c_lflag & (ECHO | ECHONL);
	tios.c_lflag &= ~ECHO;
	tios.c_lflag |= ECHONL;
	res = tcsetattr(fd, TCSAFLUSH, &tios);
	if (res < 0)
		return -1;
	return old;
}

int ast_restore_tty(int fd, int oldstate)
{
	int res;
	struct termios tios;
	if (oldstate < 0)
		return 0;
	res = tcgetattr(fd, &tios);
	if (res < 0)
		return -1;
	tios.c_lflag &= ~(ECHO | ECHONL);
	tios.c_lflag |= oldstate;
	res = tcsetattr(fd, TCSAFLUSH, &tios);
	if (res < 0)
		return -1;
	return 0;
}

int ast_get_termcols(int fd)
{
	struct winsize win;
	int cols = 0;

	if (!isatty(fd))
		return -1;

	if ( ioctl(fd, TIOCGWINSZ, &win) != -1 ) {
		if ( !cols && win.ws_col > 0 )
			cols = (int) win.ws_col;
	} else {
		/* assume 80 characters if the ioctl fails for some reason */
		cols = 80;
	}

	return cols;
}

