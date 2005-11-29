/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Asterisk architecture endianess compatibility definitions
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser General Public License.  Other components of
 * Asterisk are distributed under The GNU General Public License
 * only.
 */

#ifndef _ASTERISK_ENDIAN_H
#define _ASTERISK_ENDIAN_H

/*
 * Autodetect system endianess
 */

#ifdef SOLARIS
#include "solaris-compat/compat.h"
#endif

#ifndef __BYTE_ORDER
#ifdef __linux__
#include <endian.h>
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#if defined(__OpenBSD__)
#include <machine/types.h>
#endif /* __OpenBSD__ */
#include <machine/endian.h>
#define __BYTE_ORDER BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#else
#ifdef __LITTLE_ENDIAN__
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif /* __LITTLE_ENDIAN */

#if defined(i386) || defined(__i386__)
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif /* defined i386 */

#if defined(sun) && defined(unix) && defined(sparc)
#define __BYTE_ORDER __BIG_ENDIAN
#endif /* sun unix sparc */

#endif /* linux */

#endif /* __BYTE_ORDER */

#ifndef __BYTE_ORDER
#error Need to know endianess
#endif /* __BYTE_ORDER */

#endif /* _ASTERISK_ENDIAN_H */

