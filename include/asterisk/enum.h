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

#ifndef _ASTERISK_ENUM_H
#define _ASTERISK_ENUM_H
#include <asterisk/channel.h>
/* Lookup entry in ENUM Returns 1 if found, 0 if not found, -1 on hangup */
extern int ast_get_enum(struct ast_channel *chan, const char *number, char *location, int maxloc, char *technology, int maxtech);
extern int ast_get_txt(struct ast_channel *chan, const char *number, char *location, int maxloc, char *technology, int maxtech, char *txt, int maxtxt);

extern int ast_enum_init(void);
extern int ast_enum_reload(void);
#endif
