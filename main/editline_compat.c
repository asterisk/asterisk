/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sean Bright
 *
 * Sean Bright <sean@seanbright.com>
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
#include "asterisk.h"

#include <histedit.h>

#include "editline_compat.h"

/*
 * The `read_char` function below is a modified (see inline comments)
 * version of the `read_char` function in libedit's src/read.c file which,
 * as of 2026-01-02, can be found here:
 *
 # https://cvsweb.netbsd.org/bsdweb.cgi/src/lib/libedit/read.c?rev=HEAD
 *
 * The copyright and license information is reproduced here:
 */

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/* read_char():
 *	Read a character from the tty.
 */
static int
read_char(EditLine *el, wchar_t *cp)
{
	ssize_t num_read;
#ifdef EDITLINE_ORIG
	/* Removed for Asterisk. FIXIO is set when the EL_SAFEREAD
	   flag is set on the EditLine handle, which we do not do
	   so `tried` will always be 1 */
	int tried = (el->el_flags & FIXIO) == 0;
#endif
	char cbuf[MB_LEN_MAX];
	size_t cbp = 0;
#ifdef EDITLINE_ORIG
	/* Removed for Asterisk. This is only needed for FIXIO,
	   as above. */
	int save_errno = errno;
#endif

 again:
#ifdef EDITLINE_ORIG
	/* Removed for Asterisk. libedit only sets up signal handlers
	   for SIGCONT and SIGWINCH if the EL_SIGNAL flag is set on
	   the EditLine handle, which we do not do.

	   Additionally, we don't have access to the internals of `el`
	   to get the file descriptor to read from, but we know that
	   we will always be reading from stdin, so hardcode it */
	el->el_signal->sig_no = 0;
	while ((num_read = read(el->el_infd, cbuf + cbp, (size_t)1)) == -1) {
		int e = errno;
		switch (el->el_signal->sig_no) {
		case SIGCONT:
			el_wset(el, EL_REFRESH);
			/*FALLTHROUGH*/
		case SIGWINCH:
			sig_set(el);
			goto again;
		default:
			break;
		}
		if (!tried && read__fixio(el->el_infd, e) == 0) {
			errno = save_errno;
			tried = 1;
		} else {
			errno = e;
			*cp = L'\0';
			return -1;
		}
	}
#else
	while ((num_read = read(STDIN_FILENO, cbuf + cbp, (size_t)1)) == -1) {
		*cp = L'\0';
		return -1;
	}
#endif

	/* Test for EOF */
	if (num_read == 0) {
		*cp = L'\0';
		return 0;
	}

	for (;;) {
		mbstate_t mbs;

		++cbp;
		/* This only works because UTF8 is stateless. */
		memset(&mbs, 0, sizeof(mbs));
		switch (mbrtowc(cp, cbuf, cbp, &mbs)) {
		case (size_t)-1:
			if (cbp > 1) {
				/*
				 * Invalid sequence, discard all bytes
				 * except the last one.
				 */
				cbuf[0] = cbuf[cbp - 1];
				cbp = 0;
				break;
			} else {
				/* Invalid byte, discard it. */
				cbp = 0;
				goto again;
			}
		case (size_t)-2:
			if (cbp >= MB_LEN_MAX) {
				errno = EILSEQ;
				*cp = L'\0';
				return -1;
			}
			/* Incomplete sequence, read another byte. */
			goto again;
		default:
			/* Valid character, process it. */
			return 1;
		}
	}
}

int editline_read_char(EditLine *el, wchar_t *cp)
{
	return read_char(el, cp);
}
