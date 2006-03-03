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

#ifndef _POLL_EMUL_H_
#define _POLL_EMUL_H_

#define POLLIN		0x01
#define POLLPRI		0x02
#define POLLOUT		0x04
#define POLLERR		0x08
#define POLLHUP		0x10
#define POLLNVAL	0x20

struct pollfd
{
    int     fd;
    short   events;
    short   revents;
};

#ifdef __cplusplus
extern "C"
{
#endif

#if (__STDC__ > 0) || defined(__cplusplus)
extern int poll (struct pollfd *pArray, unsigned long n_fds, int timeout);
#else
extern int poll();
#endif

#ifdef __cplusplus
}
#endif

#endif /* _POLL_EMUL_H_ */
