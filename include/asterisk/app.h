/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Application convenience functions, designed to give consistent
 * look and feel to asterisk apps.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_APP_H
#define _ASTERISK_APP_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif
extern int ast_app_getdata(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
