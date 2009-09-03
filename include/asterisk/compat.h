/*
 * Asterisk -- A telephony toolkit for Linux.
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
 */

#ifndef _COMPAT_H
#define _COMPAT_H

#include "asterisk/autoconfig.h"
#include "asterisk/compiler.h"
#include <inttypes.h>
#include <sys/types.h>
#include <stdarg.h>

#if !defined(HAVE_ASPRINTF) && !defined(__AST_DEBUG_MALLOC)
int asprintf(char **str, const char *fmt, ...);
#endif

#ifndef HAVE_GETLOADAVG
int getloadavg(double *list, int nelem);
#endif

#ifndef HAVE_SETENV
int setenv(const char *name, const char *value, int overwrite);
#endif

#ifndef HAVE_STRCASESTR
char *strcasestr(const char *, const char *);
#endif

#if !defined(HAVE_STRNDUP) && !defined(__AST_DEBUG_MALLOC)
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

#if !defined(HAVE_VASPRINTF) && !defined(__AST_DEBUG_MALLOC)
int vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t siz) attribute_deprecated;
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz) attribute_deprecated;
#endif

#ifdef SOLARIS
#define __BEGIN_DECLS
#define __END_DECLS

#ifndef __P
#define __P(p) p
#endif

#include <alloca.h>
#include <strings.h>
#include <string.h>
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

#if !defined(HAVE_GLOB_NOMAGIC) || !defined(HAVE_GLOB_BRACE)
#define MY_GLOB_FLAGS   GLOB_NOCHECK
#else
#define MY_GLOB_FLAGS   (GLOB_NOMAGIC | GLOB_BRACE)
#endif

#endif
