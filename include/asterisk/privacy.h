/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Persistant data storage (akin to *doze registry)
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_PRIVACY_H
#define _ASTERISK_PRIVACY_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_PRIVACY_DENY	(1 << 0)		/* Don't bother ringing, send to voicemail */
#define AST_PRIVACY_ALLOW   (1 << 1)		/* Pass directly to me */
#define AST_PRIVACY_KILL	(1 << 2)		/* Play anti-telemarketer message and hangup */
#define AST_PRIVACY_TORTURE	(1 << 3)		/* Send directly to tele-torture */
#define AST_PRIVACY_UNKNOWN (1 << 16)

int ast_privacy_check(char *dest, char *cid);

int ast_privacy_set(char *dest, char *cid, int status);

int ast_privacy_reset(char *dest);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
