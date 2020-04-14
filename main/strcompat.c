/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Compatibility functions for strsep and strtoq missing on Solaris
 *
 * .. and lots of other functions too.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#define ASTMM_LIBC ASTMM_IGNORE
#include "asterisk.h"

#include <ctype.h>
#include <sys/time.h>       /* for getrlimit(2) */
#include <sys/resource.h>   /* for getrlimit(2) */
#include <sys/types.h>      /* for opendir(3) */
#include <dirent.h>         /* for opendir(3) */
#include <unistd.h>         /* for fcntl(2) */
#include <fcntl.h>          /* for fcntl(2) */

#include "asterisk/utils.h"

#define POLL_SIZE 1024

#ifndef HAVE_STRSEP
char *strsep(char **str, const char *delims)
{
	char *token;

	if (!*str) {
		/* No more tokens */
		return NULL;
	}

	token = *str;
	while (**str != '\0') {
		if (strchr(delims, **str)) {
			**str = '\0';
			(*str)++;
			return token;
		}
		(*str)++;
	}

	/* There is no other token */
	*str = NULL;

	return token;
}
#endif

#ifndef HAVE_SETENV
int setenv(const char *name, const char *value, int overwrite)
{
	unsigned char *buf;
	int buflen;

	if (!overwrite && getenv(name))
		return 0;

	buflen = strlen(name) + strlen(value) + 2;
	buf = ast_alloca(buflen);

	snprintf(buf, buflen, "%s=%s", name, value);

	return putenv(buf);
}
#endif

#ifndef HAVE_UNSETENV
int unsetenv(const char *name)
{
	return setenv(name, "", 0);
}
#endif

#ifndef HAVE_STRCASESTR
static char *upper(const char *orig, char *buf, int bufsize)
{
	int i = 0;

	while (i < (bufsize - 1) && orig[i]) {
		buf[i] = toupper(orig[i]);
		i++;
	}

	buf[i] = '\0';

	return buf;
}

char *strcasestr(const char *haystack, const char *needle)
{
	char *u1, *u2;
	char *offset;
	int u1len = strlen(haystack) + 1, u2len = strlen(needle) + 1;

	if (u2len > u1len) {
		/* Needle bigger than haystack */
		return NULL;
	}
	u1 = ast_alloca(u1len);
	u2 = ast_alloca(u2len);
	offset = strstr(upper(haystack, u1, u1len), upper(needle, u2, u2len));
	if (offset) {
		/* Return the offset into the original string */
		return ((char *)((unsigned long)haystack + (unsigned long)(offset - u1)));
	} else {
		return NULL;
	}
}
#endif /* !HAVE_STRCASESTR */

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t n)
{
	size_t len;

	for (len = 0; len < n; len++)
		if (s[len] == '\0')
			break;

	return len;
}
#endif /* !HAVE_STRNLEN */

#if !defined(HAVE_STRNDUP)
char *strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *new = malloc(len + 1);

	if (!new)
		return NULL;

	new[len] = '\0';
	return memcpy(new, s, len);
}
#endif /* !defined(HAVE_STRNDUP) */

#if !defined(HAVE_VASPRINTF)
int vasprintf(char **strp, const char *fmt, va_list ap)
{
	int size;
	va_list ap2;
	char s;

	*strp = NULL;
	va_copy(ap2, ap);
	size = vsnprintf(&s, 1, fmt, ap2);
	va_end(ap2);
	*strp = malloc(size + 1);
	if (!*strp)
		return -1;
	vsnprintf(*strp, size + 1, fmt, ap);

	return size;
}
#endif /* !defined(HAVE_VASPRINTF) */

#ifndef HAVE_TIMERSUB
void timersub(struct timeval *tvend, struct timeval *tvstart, struct timeval *tvdiff)
{
	tvdiff->tv_sec = tvend->tv_sec - tvstart->tv_sec;
	tvdiff->tv_usec = tvend->tv_usec - tvstart->tv_usec;
	if (tvdiff->tv_usec < 0) {
		tvdiff->tv_sec --;
		tvdiff->tv_usec += 1000000;
	}

}
#endif

/*
 * Based on Code from bsd-asprintf from OpenSSH
 * Copyright (c) 2004 Darren Tucker.
 *
 * Based originally on asprintf.c from OpenBSD:
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#if !defined(HAVE_ASPRINTF)
int asprintf(char **str, const char *fmt, ...)
{
	va_list ap;
	int ret;

	*str = NULL;
	va_start(ap, fmt);
	ret = vasprintf(str, fmt, ap);
	va_end(ap);

	return ret;
}
#endif /* !defined(HAVE_ASPRINTF) */

#ifndef HAVE_STRTOQ
#ifndef LONG_MIN
#define LONG_MIN        (-9223372036854775807L-1L)
	                                 /* min value of a "long int" */
#endif
#ifndef LONG_MAX
#define LONG_MAX        9223372036854775807L
	                                 /* max value of a "long int" */
#endif

/*! \brief
 * Convert a string to a quad integer.
 *
 * \note Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
uint64_t strtoq(const char *nptr, char **endptr, int base)
{
	 const char *s;
	 uint64_t acc;
	 unsigned char c;
	 uint64_t qbase, cutoff;
	 int neg, any, cutlim;

	 /*
	  * Skip white space and pick up leading +/- sign if any.
	  * If base is 0, allow 0x for hex and 0 for octal, else
	  * assume decimal; if base is already 16, allow 0x.
	  */
	 s = nptr;
	 do {
	         c = *s++;
	 } while (isspace(c));
	 if (c == '-') {
	         neg = 1;
	         c = *s++;
	 } else {
	         neg = 0;
	         if (c == '+')
	                 c = *s++;
	 }
	 if ((base == 0 || base == 16) &&
	     c == '\0' && (*s == 'x' || *s == 'X')) {
	         c = s[1];
	         s += 2;
	         base = 16;
	 }
	 if (base == 0)
	         base = c == '\0' ? 8 : 10;

	 /*
	  * Compute the cutoff value between legal numbers and illegal
	  * numbers.  That is the largest legal value, divided by the
	  * base.  An input number that is greater than this value, if
	  * followed by a legal input character, is too big.  One that
	  * is equal to this value may be valid or not; the limit
	  * between valid and invalid numbers is then based on the last
	  * digit.  For instance, if the range for quads is
	  * [-9223372036854775808..9223372036854775807] and the input base
	  * is 10, cutoff will be set to 922337203685477580 and cutlim to
	  * either 7 (neg==0) or 8 (neg==1), meaning that if we have
	  * accumulated a value > 922337203685477580, or equal but the
	  * next digit is > 7 (or 8), the number is too big, and we will
	  * return a range error.
	  *
	  * Set any if any `digits' consumed; make it negative to indicate
	  * overflow.
	  */
	 qbase = (unsigned)base;
	 cutoff = neg ? (uint64_t)-(LONG_MIN + LONG_MAX) + LONG_MAX : LONG_MAX;
	 cutlim = cutoff % qbase;
	 cutoff /= qbase;
	 for (acc = 0, any = 0;; c = *s++) {
	         if (!isascii(c))
	                 break;
	         if (isdigit(c))
	                 c -= '\0';
	         else if (isalpha(c))
	                 c -= isupper(c) ? 'A' - 10 : 'a' - 10;
	         else
	                 break;
	         if (c >= base)
	                 break;
	         if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
	                 any = -1;
	         else {
	                 any = 1;
	                 acc *= qbase;
	                 acc += c;
	         }
	 }
	 if (any < 0) {
	         acc = neg ? LONG_MIN : LONG_MAX;
	 } else if (neg)
	         acc = -acc;
	 if (endptr != 0)
	         *((const char **)endptr) = any ? s - 1 : nptr;
	 return acc;
}
#endif /* !HAVE_STRTOQ */

#ifndef HAVE_GETLOADAVG
#ifdef linux
/*! \brief Alternative method of getting load avg on Linux only */
int getloadavg(double *list, int nelem)
{
	FILE *LOADAVG;
	double avg[3] = { 0.0, 0.0, 0.0 };
	int i, res = -1;

	if ((LOADAVG = fopen("/proc/loadavg", "r"))) {
		fscanf(LOADAVG, "%lf %lf %lf", &avg[0], &avg[1], &avg[2]);
		res = 0;
		fclose(LOADAVG);
	}

	for (i = 0; (i < nelem) && (i < 3); i++) {
		list[i] = avg[i];
	}

	return res;
}
#else /* !linux */
/*! \brief Return something that won't cancel the call, but still return -1, in case
 * we correct the implementation to check return value */
int getloadavg(double *list, int nelem)
{
	int i;

	for (i = 0; i < nelem; i++) {
		list[i] = 0.1;
	}
	return -1;
}
#endif /* linux */
#endif /* !HAVE_GETLOADAVG */

#ifndef HAVE_NTOHLL
uint64_t ntohll(uint64_t net64)
{
#if BYTE_ORDER == BIG_ENDIAN
	return net64;
#elif BYTE_ORDER == LITTLE_ENDIAN
	union {
		unsigned char c[8];
		uint64_t u;
	} number;
	number.u = net64;
	return
		(((uint64_t) number.c[0]) << 56) |
		(((uint64_t) number.c[1]) << 48) |
		(((uint64_t) number.c[2]) << 40) |
		(((uint64_t) number.c[3]) << 32) |
		(((uint64_t) number.c[4]) << 24) |
		(((uint64_t) number.c[5]) << 16) |
		(((uint64_t) number.c[6]) <<  8) |
		(((uint64_t) number.c[7]) <<  0);
#else
	#error "Unknown byte order"
#endif
}
#endif

#ifndef HAVE_HTONLL
uint64_t htonll(uint64_t host64)
{
#if BYTE_ORDER == BIG_ENDIAN
	return host64;
#elif BYTE_ORDER == LITTLE_ENDIAN
	union {
		unsigned char c[8];
		uint64_t u;
	} number;
	number.u = host64;
	return
		(((uint64_t) number.c[0]) << 56) |
		(((uint64_t) number.c[1]) << 48) |
		(((uint64_t) number.c[2]) << 40) |
		(((uint64_t) number.c[3]) << 32) |
		(((uint64_t) number.c[4]) << 24) |
		(((uint64_t) number.c[5]) << 16) |
		(((uint64_t) number.c[6]) <<  8) |
		(((uint64_t) number.c[7]) <<  0);
#else
	#error "Unknown byte order"
#endif
}
#endif

#ifndef HAVE_FFSLL
int ffsll(long long n)
{
	int i;
	for (i = 0; i < 64; i++) {
		if ((1LL << i) & n) {
			return i + 1;
		}
	}
	return 0;
}
#endif

#ifndef HAVE_CLOSEFROM
void closefrom(int n)
{
	int maxfd;
#ifndef _SC_OPEN_MAX
	struct rlimit rl;
#endif
	struct pollfd fds[POLL_SIZE];
	int fd=n, loopmax, i;
#ifndef STRICT_COMPAT
	long flags;
#endif

#ifndef _SC_OPEN_MAX
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
		maxfd = -1;
	} else {
		maxfd = rl.rlim_cur;
	}
#else
	maxfd = sysconf (_SC_OPEN_MAX);
#endif

	if (maxfd == -1 || maxfd > 65536) {
		/* A more reasonable value.  Consider that the primary source of
		 * file descriptors in Asterisk are UDP sockets, of which we are
		 * limited to 65,535 per address.  We additionally limit that down
		 * to about 10,000 sockets per protocol.  While the kernel will
		 * allow us to set the fileno limit higher (up to 4.2 billion),
		 * there really is no practical reason for it to be that high.
		 *
		 * sysconf as well as getrlimit can return -1 on error. Let's set
		 * maxfd to the mentioned reasonable value of 65,535 in this case.
		 */
		maxfd = 65536;
	}

	while (fd < maxfd) {
		loopmax = maxfd - fd;
		if (loopmax > POLL_SIZE) {
			loopmax = POLL_SIZE;
		}
		for (i = 0; i < loopmax; i++) {
			fds[i].fd = fd+i;
			fds[i].events = 0;
		}
		poll(fds, loopmax, 0);
		for (i = 0; i < loopmax; i++) {
			if (fds[i].revents == POLLNVAL) {
				continue;
			}
#ifdef STRICT_COMPAT
			close(fds[i].fd);
#else
			/* This isn't strictly compatible, but it's actually faster
			 * for our purposes to set the CLOEXEC flag than to close
			 * file descriptors.
			 */
			flags = fcntl(fds[i].fd, F_GETFD);
			if (flags == -1 && errno == EBADF) {
				continue;
			}
			fcntl(fds[i].fd, F_SETFD, flags | FD_CLOEXEC);
#endif
		}
		fd += loopmax;
	}
}
#endif

#ifndef HAVE_MKDTEMP
/*	$OpenBSD: mktemp.c,v 1.30 2010/03/21 23:09:30 schwarze Exp $ */
/*
 * Copyright (c) 1996-1998, 2008 Theo de Raadt
 * Copyright (c) 1997, 2008-2009 Todd C. Miller
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define MKTEMP_NAME	0
#define MKTEMP_FILE	1
#define MKTEMP_DIR	2

#define TEMPCHARS	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_."
#define NUM_CHARS	(sizeof(TEMPCHARS) - 1)

static int mktemp_internal(char *path, int slen, int mode)
{
	char *start, *cp, *ep;
	const char *tempchars = TEMPCHARS;
	unsigned int r, tries;
	struct stat sb;
	size_t len;
	int fd;

	len = strlen(path);
	if (len == 0 || slen >= len) {
		errno = EINVAL;
		return(-1);
	}
	ep = path + len - slen;

	tries = 1;
	for (start = ep; start > path && start[-1] == 'X'; start--) {
		if (tries < INT_MAX / NUM_CHARS) {
			tries *= NUM_CHARS;
		}
	}
	tries *= 2;

	do {
		for (cp = start; cp != ep; cp++) {
			r = ast_random() % NUM_CHARS;
			*cp = tempchars[r];
		}

		switch (mode) {
		case MKTEMP_NAME:
			if (lstat(path, &sb) != 0) {
				return (errno == ENOENT ? 0 : -1);
			}
			break;
		case MKTEMP_FILE:
			fd = open(path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
			if (fd != -1 || errno != EEXIST) {
				return (fd);
			}
			break;
		case MKTEMP_DIR:
			if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) == 0) {
				return (0);
			}
			if (errno != EEXIST) {
				return (-1);
			}
			break;
		}
	} while (--tries);

	errno = EEXIST;
	return(-1);
}

char *mkdtemp(char *path)
{
	return mktemp_internal(path, 0, MKTEMP_DIR) ? NULL : path;
}
#endif

#ifndef HAVE_ROUNDF
#ifndef HAVE_ROUND
float roundf(float x) {
	if (x < 0.0) {
		return (float)(int)((x) - 0.5);
	} else {
		return (float)(int)((x) + 0.5);
	}
}
#endif
#endif
