/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Asterisk internal frame definitions.
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

#if defined( __OpenBSD__ )
#  include <machine/types.h>
#  include <sys/endian.h>
#elif defined( __FreeBSD__ ) || defined( __NetBSD__ )
#  include <sys/types.h>
#  include <sys/endian.h>
#elif defined( BSD ) && ( BSD >= 199103 ) || defined(__APPLE__)
#  include <machine/endian.h>
#elif defined ( SOLARIS )
#  include <solaris-compat/compat.h>
#elif defined( __GNUC__ ) || defined( __GNU_LIBRARY__ )
#  include <endian.h>
#if !defined(__APPLE__)
#  include <byteswap.h>
#endif
#elif defined( linux )
#  include <endian.h>
#endif

#ifndef BYTE_ORDER
#define BYTE_ORDER __BYTE_ORDER
#endif

#ifndef __BYTE_ORDER
#error Endianess needs to be defined
#endif
#endif /* _ASTERISK_ENDIAN_H */

