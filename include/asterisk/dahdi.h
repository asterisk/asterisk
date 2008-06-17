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
 * \brief Stub to find DAHDI headers
*
 * Stub to find the DAHDI headers. The configure script will
 * define HAVE_DAHDI_VERSION according to what it has found.
 * Applications should include "user.h" and not (directly)
 * <foo/user.h>
 * For the mapping of version numbers to location see below.
 *
 */
#ifndef _AST_DAHDI_H
#define	_AST_DAHDI_H

#ifdef HAVE_DAHDI
#include <sys/ioctl.h>

/* newer versions install in ${PREFIX}/dahdi */
#include <dahdi/user.h>
#include <dahdi/tonezone.h>

#endif	/* HAVE_DAHDI */

#endif	/* _AST_DAHDI_H */
