/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Say numbers and dates (maybe words one day too)
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_SAY_H
#define _ASTERISK_SAY_H

#include <asterisk/channel.h>
#include <asterisk/file.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

int ast_say_number(struct ast_channel *chan, int num, char *lang);
int ast_say_digits(struct ast_channel *chan, int num, char *lang);
int ast_say_digit_str(struct ast_channel *chan, char *num, char *lang);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
