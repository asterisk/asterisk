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
 *  
 *      ???????  
 *	\todo Explain this file!
 */


#ifndef _ASTERISK_EXPR_H
#define _ASTERISK_EXPR_H
#ifndef STANDALONE
#endif
#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

int ast_expr(char *expr, char *buf, int length, struct ast_channel *chan);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_EXPR_H */
