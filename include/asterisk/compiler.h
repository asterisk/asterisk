/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Compiler-specific macros and other items
 *
 * Copyright (C) 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_COMPILER_H
#define _ASTERISK_COMPILER_H

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 96)
#define __builtin_expect(exp, c) (exp)
#endif

#endif /* _ASTERISK_COMPILER_H */
