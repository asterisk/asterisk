/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * CVSID Macro for including CVS file Id in source files.
 * 
 * Copyright (C) 2004 - 2005, William Waites
 *
 * William Waites <ww@styx.org>
 *
 * This program is free software, distributed under the following
 * terms:
 *
 *        1. Any copies of this file must retain this copyright
 *           notice intact.
 *        2. A non-exclusive, non-cancellible license is given
 *           to Digium Inc. (Linux Support Services) in order that
 *           they may license this file to third parties under terms
 *           of their choosing on the condition that if they do so
 *           they must also make this file, or any derivative of
 *           it, available under terms of the the GNU General Public
 *           License.
 *        3. If you have not recieved this file under a proprietary
 *           license from Digium or one of their licensees, or the
 *           author, it is distributed to you under terms of the GNU
 *           General Public License.
 * 
 * If you do not have a copy of the GNU GPL, which should be
 * available in the root directory of this source tree, it can
 * be found at:
 *
 *        http://www.gnu.org/licenses/gpl.html
 *           
 */

/*
 * To use this macro, in the source file put the lines:
 *
 * #include <asterisk/cvsid.h>
 * #ifndef lint
 * CVSID("$Id$");
 * #endif
 *
 * You will then be able to run strings(1) on the resulting
 * binary and find out what revisions of each source file were
 * used to build it, since when checked into a CVS repository,
 * the portion of the string between the dollar signs will be
 * replaced with version information for the file.
 */

#ifndef ASTERISK_CVSID_H
#define ASTERISK_CVSID_H

#ifdef __GNUC__
#define CVSID(x) static char __cvsid[] __attribute__ ((unused)) = x
#else
#define CVSID(x) static char __cvsid[] = x
#endif

#endif /* ASTERISK_CVSID_H */


