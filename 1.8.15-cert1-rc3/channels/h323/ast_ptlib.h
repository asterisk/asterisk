/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
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

/* PTLib is Copyright (c) 2003 Equivalence Pty. Ltd. */

/*! 
 * \file
 * \brief PTLib compatibility with previous versions of OPAL/PTLib/PWLib
 */

#ifndef AST_PTLIB_H
#define AST_PTLIB_H

#include <ptbuildopts.h>
#if !defined(P_USE_STANDARD_CXX_BOOL) && !defined(P_USE_INTEGER_BOOL)
typedef BOOL PBoolean;
#define PTrue TRUE
#define PFalse FALSE
#endif

#endif /* !defined AST_PTLIB_H */
