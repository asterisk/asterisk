/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2
 */

#ifndef _ASTERISK_EXPR_H
#define _ASTERISK_EXPR_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

int ast_expr(char *expr, char *buf, int length);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_EXPR_H */
