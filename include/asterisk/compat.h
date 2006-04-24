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

#ifndef HAVE_STRSEP
char* strsep(char** str, const char* delims);
#endif

#ifndef HAVE_SETENV
int setenv(const char *name, const char *value, int overwrite);
#endif

#ifndef HAVE_UNSETENV
int unsetenv(const char *name);
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

#ifdef __linux__
#include <inttypes.h>
#endif

#ifdef __Darwin__
#include <inttypes.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#ifdef __CYGWIN__
typedef unsigned long long uint64_t;
#endif

#endif
