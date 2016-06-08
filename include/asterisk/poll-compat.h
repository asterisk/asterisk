/*
 * Asterisk -- An open source telephony toolkit.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 */

/*---------------------------------------------------------------------------*\
  $Id$

  NAME

	poll - select(2)-based poll() emulation function for BSD systems.

  SYNOPSIS
	#include "poll.h"

	struct pollfd
	{
	    int     fd;
	    short   events;
	    short   revents;
	}

	int poll (struct pollfd *pArray, unsigned long n_fds, int timeout)

  DESCRIPTION

	This file, and the accompanying "poll.c", implement the System V
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

#ifndef __AST_POLL_COMPAT_H
#define __AST_POLL_COMPAT_H

#include "asterisk/select.h"

#ifndef AST_POLL_COMPAT

#include <poll.h>

#define ast_poll(a, b, c) poll(a, b, c)

#else /* AST_POLL_COMPAT */

#define POLLIN		0x01
#define POLLPRI		0x02
#define POLLOUT		0x04
#define POLLERR		0x08
#define POLLHUP		0x10
#define POLLNVAL	0x20

struct pollfd {
    int     fd;
    short   events;
    short   revents;
};

#ifdef __cplusplus
extern "C" {
#endif

#define ast_poll(a, b, c) ast_internal_poll(a, b, c)

int ast_internal_poll(struct pollfd *pArray, unsigned long n_fds, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* AST_POLL_COMPAT */

/*!
 * \brief Same as poll(2), except the time is specified in microseconds and
 * the tv argument is modified to indicate the time remaining.
 */
int ast_poll2(struct pollfd *pArray, unsigned long n_fds, struct timeval *tv);

/*!
 * \brief Shortcut for conversion of FD_ISSET to poll(2)-based
 */
static inline int ast_poll_fd_index(struct pollfd *haystack, int nfds, int needle)
{
	int i;
	for (i = 0; i < nfds; i++) {
		if (haystack[i].fd == needle) {
			return i;
		}
	}
	return -1;
}

#endif /* __AST_POLL_COMPAT_H */
