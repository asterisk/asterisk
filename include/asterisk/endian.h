/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Asterisk architecture endianess compatibility definitions
 */

#ifndef _ASTERISK_ENDIAN_H
#define _ASTERISK_ENDIAN_H

/*
 * Autodetect system endianess
 */


#ifndef __BYTE_ORDER
#ifdef __linux__
#include <endian.h>
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__APPLE__)
#if defined(__OpenBSD__)
#include "asterisk/compat.h"
#endif
#include <machine/endian.h>
#define __BYTE_ORDER BYTE_ORDER
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#else

#ifndef	__LITTLE_ENDIAN
#define	__LITTLE_ENDIAN		1234
#endif

#ifndef	__BIG_ENDIAN
#define	__BIG_ENDIAN		4321
#endif

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
