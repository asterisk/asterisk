/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ENUM support
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_SRV_H
#define _ASTERISK_SRV_H

struct ast_channel;

/* Lookup entry in SRV records Returns 1 if found, 0 if not found, -1 on hangup */
extern int ast_get_srv(struct ast_channel *chan, char *host, int hostlen, int *port, const char *service);

#endif
