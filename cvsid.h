/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * CVSID Macro for including CVS file Id in source files.
 * 
 * Copyright (C) 2004, William Waites
 *
 * William Waites <ww@styx.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.  This file has been disclaimed
 * to Digium.
 *
 * To use, in the source file put the lines:
 *
 * #include <asterisk/cvsid.h>
 * #ifndef lint
 * CVSID("$Id$");
 * #endif /* lint */
 *
 * You will then be able to run strings(1) on the resulting
 * binary and find out what revisions of each source file were
 * used to build it.
 *
 */

#ifndef ASTERISK_CVSID_H
#define ASTERISK_CVSID_H

#ifdef __GNUC__
#define CVSID(x) static char __cvsid[] __attribute__ ((unused)) = x
#else
#define CVSID(x) static char __cvsid[] = x
#endif

#endif /* ASTERISK_CVSID_H */
