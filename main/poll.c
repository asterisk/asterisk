/*---------------------------------------------------------------------------*\
  $Id$

  NAME

	poll - select(2)-based poll() emulation function for BSD systems.

  SYNOPSIS
	#include "poll.h"

	struct pollfd
	{
		int	 fd;
		short   events;
		short   revents;
	}

	int poll (struct pollfd *pArray, unsigned long n_fds, int timeout)

  DESCRIPTION

	This file, and the accompanying "poll.h", implement the System V
	poll(2) system call for BSD systems (which typically do not provide
	poll()).  Poll() provides a method for multiplexing input and output
	on multiple open file descriptors; in traditional BSD systems, that
	capability is provided by select().  While the semantics of select()
	differ from those of poll(), poll() can be readily emulated in terms
	of select() -- which is how this function is implemented.

  REFERENCES
	Stevens, W. Richard. Unix Network Programming.  Prentice-Hall, 1990.

  NOTES
	1. This software requires an ANSI C compiler.

  LICENSE

	This software is released under the following license:

		Copyright (c) 1995-2002 Brian M. Clapper
		All rights reserved.

		Redistribution and use in source and binary forms are
		permitted provided that: (1) source distributions retain
		this entire copyright notice and comment; (2) modifications
		made to the software are prominently mentioned, and a copy
		of the original software (or a pointer to its location) are
		included; and (3) distributions including binaries display
		the following acknowledgement: "This product includes
		software developed by Brian M. Clapper <bmc@clapper.org>"
		in the documentation or other materials provided with the
		distribution. The name of the author may not be used to
		endorse or promote products derived from this software
		without specific prior written permission.

		THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS
		OR IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE
		IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
		PARTICULAR PURPOSE.

	Effectively, this means you can do what you want with the software
	except remove this notice or take advantage of the author's name.
	If you modify the software and redistribute your modified version,
	you must indicate that your version is a modification of the
	original, and you must provide either a pointer to or a copy of the
	original.
\*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*\
				 Includes
\*---------------------------------------------------------------------------*/

#include "asterisk.h"

#include <unistd.h>				 /* standard Unix definitions */
#include <sys/types.h>					   /* system types */
#include <sys/time.h>						/* time definitions */
#include <assert.h>						  /* assertion macros */
#include <string.h>						  /* string functions */
#include <errno.h>

#include "asterisk/utils.h"                            /* this package */
#include "asterisk/poll-compat.h"                            /* this package */

unsigned int ast_FD_SETSIZE = FD_SETSIZE;

#ifndef MAX
#define MAX(a,b)	a > b ? a : b
#endif

/*---------------------------------------------------------------------------*\
				 Private Functions
\*---------------------------------------------------------------------------*/

#if defined(AST_POLL_COMPAT)
static int map_poll_spec(struct pollfd *pArray, unsigned long n_fds,
		ast_fdset *pReadSet, ast_fdset *pWriteSet, ast_fdset *pExceptSet)
{
	register unsigned long  i;     /* loop control */
	register struct pollfd *pCur;  /* current array element */
	register int max_fd = -1;      /* return value */

	/*
	 * Map the poll() structures into the file descriptor sets required
	 * by select().
	 */
	for (i = 0, pCur = pArray; i < n_fds; i++, pCur++) {
		/* Skip any bad FDs in the array. */

		if (pCur->fd < 0) {
			continue;
		}

		if (pCur->events & POLLIN) {
			/* "Input Ready" notification desired. */
			FD_SET(pCur->fd, pReadSet);
		}

		if (pCur->events & POLLOUT) {
			/* "Output Possible" notification desired. */
			FD_SET(pCur->fd, pWriteSet);
		}

		if (pCur->events & POLLPRI) {
			/*!\note
			 * "Exception Occurred" notification desired.  (Exceptions
			 * include out of band data.)
			 */
			FD_SET(pCur->fd, pExceptSet);
		}

		max_fd = MAX(max_fd, pCur->fd);
	}

	return max_fd;
}

#ifdef AST_POLL_COMPAT
static struct timeval *map_timeout(int poll_timeout, struct timeval *pSelTimeout)
{
	struct timeval *pResult;

	/*
	   Map the poll() timeout value into a select() timeout.  The possible
	   values of the poll() timeout value, and their meanings, are:

	   VALUE	MEANING

	   -1	wait indefinitely (until signal occurs)
		0	return immediately, don't block
	   >0	wait specified number of milliseconds

	   select() uses a "struct timeval", which specifies the timeout in
	   seconds and microseconds, so the milliseconds value has to be mapped
	   accordingly.
	*/

	assert(pSelTimeout != NULL);

	switch (poll_timeout) {
	case -1:
		/*
		 * A NULL timeout structure tells select() to wait indefinitely.
		 */
		pResult = (struct timeval *) NULL;
		break;

	case 0:
		/*
		 * "Return immediately" (test) is specified by all zeros in
		 * a timeval structure.
		 */
		pSelTimeout->tv_sec  = 0;
		pSelTimeout->tv_usec = 0;
		pResult = pSelTimeout;
		break;

	default:
		/* Wait the specified number of milliseconds. */
		pSelTimeout->tv_sec  = poll_timeout / 1000; /* get seconds */
		poll_timeout        %= 1000;                /* remove seconds */
		pSelTimeout->tv_usec = poll_timeout * 1000; /* get microseconds */
		pResult = pSelTimeout;
		break;
	}

	return pResult;
}
#endif /* AST_POLL_COMPAT */

static void map_select_results(struct pollfd *pArray, unsigned long n_fds,
			  ast_fdset *pReadSet, ast_fdset *pWriteSet, ast_fdset *pExceptSet)
{
	register unsigned long  i;    /* loop control */
	register struct pollfd *pCur; /* current array element */

	for (i = 0, pCur = pArray; i < n_fds; i++, pCur++) {
		/* Skip any bad FDs in the array. */

		if (pCur->fd < 0) {
			continue;
		}

		/* Exception events take priority over input events. */
		pCur->revents = 0;
		if (FD_ISSET(pCur->fd, (fd_set *) pExceptSet)) {
			pCur->revents |= POLLPRI;
		} else if (FD_ISSET(pCur->fd, (fd_set *) pReadSet)) {
			pCur->revents |= POLLIN;
		}

		if (FD_ISSET(pCur->fd, (fd_set *) pWriteSet)) {
			pCur->revents |= POLLOUT;
		}
	}

	return;
}
#endif /* defined(AST_POLL_COMPAT) || !defined(HAVE_PPOLL) */

/*---------------------------------------------------------------------------*\
				 Public Functions
\*---------------------------------------------------------------------------*/
#ifdef AST_POLL_COMPAT
int ast_internal_poll(struct pollfd *pArray, unsigned long n_fds, int timeout)
{
	ast_fdset  read_descs;                       /* input file descs */
	ast_fdset  write_descs;                      /* output file descs */
	ast_fdset  except_descs;                     /* exception descs */
	struct  timeval stime;                       /* select() timeout value */
	int     ready_descriptors;                   /* function result */
	int     max_fd = 0;                          /* maximum fd value */
	struct  timeval *pTimeout;                   /* actually passed */
	int save_errno;

	FD_ZERO(&read_descs);
	FD_ZERO(&write_descs);
	FD_ZERO(&except_descs);

	/* Map the poll() file descriptor list in the select() data structures. */

	if (pArray) {
		max_fd = map_poll_spec (pArray, n_fds,
				&read_descs, &write_descs, &except_descs);
	}

	/* Map the poll() timeout value in the select() timeout structure. */

	pTimeout = map_timeout (timeout, &stime);

	/* Make the select() call. */

	ready_descriptors = ast_select(max_fd + 1, &read_descs, &write_descs,
				&except_descs, pTimeout);
	save_errno = errno;

	if (ready_descriptors >= 0) {
		map_select_results (pArray, n_fds,
				&read_descs, &write_descs, &except_descs);
	}

	errno = save_errno;
	return ready_descriptors;
}
#endif /* AST_POLL_COMPAT */

int ast_poll2(struct pollfd *pArray, unsigned long n_fds, struct timeval *tv)
{
#if !defined(AST_POLL_COMPAT)
	struct timeval start = ast_tvnow();
#if defined(HAVE_PPOLL)
	struct timespec ts = { tv ? tv->tv_sec : 0, tv ? tv->tv_usec * 1000 : 0 };
	int res = ppoll(pArray, n_fds, tv ? &ts : NULL, NULL);
#else
	int res = poll(pArray, n_fds, tv ? tv->tv_sec * 1000 + tv->tv_usec / 1000 : -1);
#endif
	struct timeval after = ast_tvnow();
	if (res > 0 && tv && ast_tvdiff_ms(ast_tvadd(*tv, start), after) > 0) {
		*tv = ast_tvsub(*tv, ast_tvsub(after, start));
	} else if (res > 0 && tv) {
		*tv = ast_tv(0, 0);
	}
	return res;
#else
	ast_fdset read_descs, write_descs, except_descs;
	int ready_descriptors, max_fd = 0;

	FD_ZERO(&read_descs);
	FD_ZERO(&write_descs);
	FD_ZERO(&except_descs);

	if (pArray) {
		max_fd = map_poll_spec(pArray, n_fds, &read_descs, &write_descs, &except_descs);
	}

	ready_descriptors = ast_select(max_fd + 1, &read_descs, &write_descs, &except_descs, tv);

	if (ready_descriptors >= 0) {
		map_select_results(pArray, n_fds, &read_descs, &write_descs, &except_descs);
	}

	return ready_descriptors;
#endif
}
