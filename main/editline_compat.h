/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sean Bright
 *
 * Sean Bright <sean@seanbright.com>
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

#ifndef EDITLINE_COMPAT_PRIVATE_H
#define EDITLINE_COMPAT_PRIVATE_H

#include <histedit.h>

int editline_read_char(EditLine *el, wchar_t *cp);

#endif /* EDITLINE_COMPAT_PRIVATE_H */
