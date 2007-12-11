/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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
 *
 * Sometimes one really wonders why we need a copyright
 * for less than ten lines of preprocessor directives...
 */

/*! \file
 * \brief Stub to find zaptel headers
*
 * Stub to find the zaptel headers. The configure script will
 * define HAVE_ZAPTEL_VERSION according to what it has found.
 * Applications should include "zapata.h" and not (directly)
 * <foo/zaptel.h> or <foo/tonezone.h>.
 * For the mapping of version numbers to location see below.
 *
 */
#ifndef _AST_ZAPATA_H
#define	_AST_ZAPATA_H

#ifdef HAVE_ZAPTEL
#include <sys/ioctl.h>

#if defined(HAVE_ZAPTEL_VERSION) && HAVE_ZAPTEL_VERSION < 100
/* Very old versions of zaptel drivers on FreeBSD install in ${PREFIX} */
#include <zaptel.h>
#include <tonezone.h>
#else
/* newer versions install in ${PREFIX}/zaptel */
#include <zaptel/zaptel.h>
#include <zaptel/tonezone.h>
#endif	/* HAVE_ZAPTEL_VERSION < 100 */

#endif	/* HAVE_ZAPTEL */

#endif	/* _AST_ZAPATA_H */
