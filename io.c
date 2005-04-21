/*
 * Asterisk
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C) Mark Spencer
 * 
 * Distributed under the terms of the GNU General Public License (GPL) Version 2
 *
 * I/O Managment (Derived from Cheops-NG)
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h> /* for memset */
#include <sys/ioctl.h>
#include "asterisk/io.h"
#include "asterisk/logger.h"

#ifdef DEBUG_IO
#define DEBUG DEBUG_M
#else
#define DEBUG(a) 
#endif

/* 
 * Kept for each file descriptor
 */
struct io_rec {
	ast_io_cb callback;		/* What is to be called */
	void *data; 				/* Data to be passed */
	int *id; 					/* ID number */
};

/* These two arrays are keyed with
   the same index.  it's too bad that
   pollfd doesn't have a callback field
   or something like that.  They grow as
   needed, by GROW_SHRINK_AMOUNT structures
   at once */

#define GROW_SHRINK_SIZE 512

/* Global variables are now in a struct in order to be
   made threadsafe */
struct io_context {
	/* Poll structure */
	struct pollfd *fds;
	/* Associated I/O records */
	struct io_rec *ior;
	/* First available fd */
	unsigned int fdcnt;
	/* Maximum available fd */
	unsigned int maxfdcnt;
	/* Currently used io callback */
	int current_ioc;
	/* Whether something has been deleted */
	int needshrink;
};


struct io_context *io_context_create(void)
{
	/* Create an I/O context */
	struct io_context *tmp;
	tmp = malloc(sizeof(struct io_context));
	if (tmp) {
		tmp->needshrink = 0;
		tmp->fdcnt = 0;
		tmp->maxfdcnt = GROW_SHRINK_SIZE/2;
		tmp->current_ioc = -1;
		tmp->fds = malloc((GROW_SHRINK_SIZE/2) * sizeof(struct pollfd));
		if (!tmp->fds) {
			free(tmp);
			tmp = NULL;
		} else {
			memset(tmp->fds, 0, (GROW_SHRINK_SIZE/2) * sizeof(struct pollfd));
			tmp->ior =  malloc((GROW_SHRINK_SIZE/2) * sizeof(struct io_rec));
			if (!tmp->ior) {
				free(tmp->fds);
				free(tmp);
				tmp = NULL;
			} else
				memset(tmp->ior, 0, (GROW_SHRINK_SIZE/2) * sizeof(struct io_rec));
		}
	}
	return tmp;
}

void io_context_destroy(struct io_context *ioc)
{
	/* Free associated memory with an I/O context */
	if (ioc->fds)
		free(ioc->fds);
	if (ioc->ior)
		free(ioc->ior);
	free(ioc);
}

static int io_grow(struct io_context *ioc)
{
	/* 
	 * Grow the size of our arrays.  Return 0 on success or
	 * -1 on failure
	 */
	void *tmp;
	DEBUG(ast_log(LOG_DEBUG, "io_grow()\n"));
	ioc->maxfdcnt += GROW_SHRINK_SIZE;
	tmp = realloc(ioc->ior, (ioc->maxfdcnt + 1) * sizeof(struct io_rec));
	if (tmp) {
		ioc->ior = (struct io_rec *)tmp;
		tmp = realloc(ioc->fds, (ioc->maxfdcnt + 1) * sizeof(struct pollfd));
		if (tmp) {
			ioc->fds = tmp;
		} else {
			/*
			 * Not enough memory for the pollfd.  Not really any need
			 * to shrink back the iorec's as we'll probably want to
			 * grow them again soon when more memory is available, and
			 * then they'll already be the right size
			 */
			ioc->maxfdcnt -= GROW_SHRINK_SIZE;
			return -1;
		}
		
	} else {
		/*
		 * Out of memory.  We return to the old size, and return a failure
		 */
		ioc->maxfdcnt -= GROW_SHRINK_SIZE;
		return -1;
	}
	return 0;
}

int *ast_io_add(struct io_context *ioc, int fd, ast_io_cb callback, short events, void *data)
{
	/*
	 * Add a new I/O entry for this file descriptor
	 * with the given event mask, to call callback with
	 * data as an argument.  Returns NULL on failure.
	 */
	int *ret;
	DEBUG(ast_log(LOG_DEBUG, "ast_io_add()\n"));
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
	ioc->ior[ioc->fdcnt].callback = callback;
	ioc->ior[ioc->fdcnt].data = data;
	ioc->ior[ioc->fdcnt].id = (int *)malloc(sizeof(int));
	/* Bonk if we couldn't allocate an int */
	if (!ioc->ior[ioc->fdcnt].id)
		return NULL;
	*(ioc->ior[ioc->fdcnt].id) = ioc->fdcnt;
	ret = ioc->ior[ioc->fdcnt].id;
	ioc->fdcnt++;
	return ret;
}

int *ast_io_change(struct io_context *ioc, int *id, int fd, ast_io_cb callback, short events, void *data)
{
	if (*id < ioc->fdcnt) {
		if (fd > -1)
			ioc->fds[*id].fd = fd;
		if (callback)
			ioc->ior[*id].callback = callback;
		if (events)
			ioc->fds[*id].events = events;
		if (data)
			ioc->ior[*id].data = data;
		return id;
	} else return NULL;
}

static int io_shrink(struct io_context *ioc)
{
	int getfrom;
	int putto = 0;
	/* 
	 * Bring the fields from the very last entry to cover over
	 * the entry we are removing, then decrease the size of the 
	 * arrays by one.
	 */
	for (getfrom=0;getfrom<ioc->fdcnt;getfrom++) {
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
	for (x=0;x<ioc->fdcnt;x++) {
		if (ioc->ior[x].id == _id) {
			/* Free the int immediately and set to NULL so we know it's unused now */
			free(ioc->ior[x].id);
			ioc->ior[x].id = NULL;
			ioc->fds[x].events = 0;
			ioc->fds[x].revents = 0;
			ioc->needshrink = 1;
			if (!ioc->current_ioc)
				io_shrink(ioc);
			return 0;
		}
	}
	
	ast_log(LOG_NOTICE, "Unable to remove unknown id %p\n", _id);
	return -1;
}

int ast_io_wait(struct io_context *ioc, int howlong)
{
	/*
	 * Make the poll call, and call
	 * the callbacks for anything that needs
	 * to be handled
	 */
	int res;
	int x;
	int origcnt;
	DEBUG(ast_log(LOG_DEBUG, "ast_io_wait()\n"));
	res = poll(ioc->fds, ioc->fdcnt, howlong);
	if (res > 0) {
		/*
		 * At least one event
		 */
		origcnt = ioc->fdcnt;
		for(x=0;x<origcnt;x++) {
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
	}
	return res;
}

void ast_io_dump(struct io_context *ioc)
{
	/*
	 * Print some debugging information via
	 * the logger interface
	 */
	int x;
	ast_log(LOG_DEBUG, "Asterisk IO Dump: %d entries, %d max entries\n", ioc->fdcnt, ioc->maxfdcnt);
	ast_log(LOG_DEBUG, "================================================\n");
	ast_log(LOG_DEBUG, "| ID    FD     Callback    Data        Events  |\n");
	ast_log(LOG_DEBUG, "+------+------+-----------+-----------+--------+\n");
	for (x=0;x<ioc->fdcnt;x++) {
		ast_log(LOG_DEBUG, "| %.4d | %.4d | %p | %p | %.6x |\n", 
				*ioc->ior[x].id,
				ioc->fds[x].fd,
				ioc->ior[x].callback,
				ioc->ior[x].data,
				ioc->fds[x].events);
	}
	ast_log(LOG_DEBUG, "================================================\n");
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

