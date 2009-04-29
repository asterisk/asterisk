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

/*!\brief Evaluate the given expression
 * \param expr An expression
 * \param buf Result buffer
 * \param length Size of the result buffer, in bytes
 * \param chan Channel to use for evaluating included dialplan functions, if any
 * \return Length of the result string, in bytes
 */
int ast_expr(char *expr, char *buf, int length, struct ast_channel *chan);

/*!\brief Evaluate the given expression
 * \param str Dynamic result buffer
 * \param maxlen <0 if the size of the buffer should remain constant, >0 if the size of the buffer should expand to that many bytes, maximum, or 0 for unlimited expansion of the result buffer
 * \param chan Channel to use for evaluating included dialplan functions, if any
 * \param expr An expression
 * \return Length of the result string, in bytes
 */
int ast_str_expr(struct ast_str **str, ssize_t maxlen, struct ast_channel *chan, char *expr);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_EXPR_H */
