/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
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

/*! 
 * \file
 * \brief Find tonezone header in the right place (DAHDI or Zaptel)
 */

#ifndef TONEZONE_COMPAT_H
#define TONEZONE_COMPAT_H

#if defined(HAVE_ZAPTEL)

#include <zaptel/tonezone.h>

#elif defined(HAVE_DAHDI)

#include <dahdi/tonezone.h>

#endif

#endif /* TONEZONE_COMPAT_H */
