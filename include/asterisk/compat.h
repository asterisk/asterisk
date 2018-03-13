/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999-2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief General Definitions for Asterisk top level program
 * Included by asterisk.h to handle platform-specific issues
 * especially those related to header files.
 */

#ifndef _COMPAT_H
#define _COMPAT_H
/* IWYU pragma: private, include "asterisk.h" */
/* IWYU pragma: begin_exports */

#include "asterisk/compiler.h"

#ifndef __STDC_VERSION__
/* flex output wants to find this defined. */
#define	__STDC_VERSION__ 0
#endif

#include <inttypes.h>
#include <limits.h>
#include <unistd.h>

#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif

#include <stdint.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdlib.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>    /* not necessarily present - could be in stdlib */
#elif defined(HAVE_ALLOCA) && defined(__MINGW32__)
#include <malloc.h>    /* see if it is here... */
#endif

#include <stdio.h>	/* this is always present */

#include <string.h>

#ifndef AST_POLL_COMPAT
#include <poll.h>
#else
#include "asterisk/poll-compat.h"
#endif

#ifndef HAVE_LLONG_MAX
#define	LLONG_MAX	9223372036854775807LL
#endif

#ifndef HAVE_CLOSEFROM
void closefrom(int lowfd);
#endif

#if !defined(HAVE_ASPRINTF)
int __attribute__((format(printf, 2, 3))) asprintf(char **str, const char *fmt, ...);
#endif

#ifndef HAVE_FFSLL
int ffsll(long long n);
#endif

#ifndef HAVE_GETLOADAVG
int getloadavg(double *list, int nelem);
#endif

#ifndef HAVE_HTONLL
uint64_t htonll(uint64_t host64);
#endif

#ifndef HAVE_MKDTEMP
char *mkdtemp(char *template_s);
#endif

#ifndef HAVE_NTOHLL
uint64_t ntohll(uint64_t net64);
#endif

#ifndef HAVE_SETENV
int setenv(const char *name, const char *value, int overwrite);
#endif

#ifndef HAVE_STRCASESTR
char *strcasestr(const char *, const char *);
#endif

#if !defined(HAVE_STRNDUP)
char *strndup(const char *, size_t);
#endif

#ifndef HAVE_STRNLEN
size_t strnlen(const char *, size_t);
#endif

#ifndef HAVE_STRSEP
char* strsep(char** str, const char* delims);
#endif

#ifndef HAVE_STRTOQ
uint64_t strtoq(const char *nptr, char **endptr, int base);
#endif

#ifndef HAVE_UNSETENV
int unsetenv(const char *name);
#endif

#if !defined(HAVE_VASPRINTF)
int __attribute__((format(printf, 2, 0))) vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#ifndef HAVE_TIMERSUB
void timersub(struct timeval *tvend, struct timeval *tvstart, struct timeval *tvdiff);
#endif

#define	strlcat	__use__ast_str__functions_not__strlcat__
#define	strlcpy	__use__ast_copy_string__not__strlcpy__

#include <errno.h>

#ifdef SOLARIS
#define __BEGIN_DECLS
#define __END_DECLS

#ifndef __P
#define __P(p) p
#endif

#include <alloca.h>
#include <strings.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/loadavg.h>
#include <dat/dat_platform_specific.h>

#ifndef BYTE_ORDER
#define LITTLE_ENDIAN	1234
#define BIG_ENDIAN	4321

#ifdef __sparc__
#define BYTE_ORDER	BIG_ENDIAN
#else
#define BYTE_ORDER	LITTLE_ENDIAN
#endif
#endif

#ifndef __BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#define __BYTE_ORDER BYTE_ORDER
#endif

#ifndef __BIT_TYPES_DEFINED__
#define __BIT_TYPES_DEFINED__
typedef unsigned char	u_int8_t;
typedef unsigned short	u_int16_t;
typedef unsigned int	u_int32_t;
typedef unsigned int	uint;
#endif

#endif /* SOLARIS */

#ifdef __CYGWIN__
#define _WIN32_WINNT 0x0500
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN  16
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif
#endif /* __CYGWIN__ */

#ifdef __CYGWIN__
typedef unsigned long long uint64_t;
#endif

/* glob compat stuff */
#if defined(__Darwin__) || defined(__CYGWIN__)
#define GLOB_ABORTED GLOB_ABEND
#endif
#include <glob.h>
#if !defined(HAVE_GLOB_NOMAGIC) || !defined(HAVE_GLOB_BRACE)
#define MY_GLOB_FLAGS   GLOB_NOCHECK
#else
#define MY_GLOB_FLAGS   (GLOB_NOMAGIC | GLOB_BRACE)
#endif

#ifndef HAVE_ROUNDF
#ifdef HAVE_ROUND
#define roundf(x) ((float)round(x))
#else
float roundf(float x);
#endif
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif

#ifndef NAN
#define NAN (0.0/0.0)
#endif
/* IWYU pragma: end_exports */
#endif
