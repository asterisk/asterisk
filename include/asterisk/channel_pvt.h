/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Private channel definitions for channel implementations only.
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_CHANNEL_PVT_H
#define _ASTERISK_CHANNEL_PVT_H

#include <asterisk/channel.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


struct ast_channel_pvt {
	/* Private data used by channel backend */
	void *pvt;	
	/* Send a literal DTMF digit */
	int (*send_digit)(struct ast_channel *chan, char digit);
	/* Call a given phone number (address, etc), but don't
	   take longer than timeout seconds to do so.  */
	int (*call)(struct ast_channel *chan, char *addr, int timeout);
	/* Hangup (and possibly destroy) the channel */
	int (*hangup)(struct ast_channel *chan);
	/* Answer the line */
	int (*answer)(struct ast_channel *chan);
	/* Read a frame, in standard format */
	struct ast_frame * (*read)(struct ast_channel *chan);
	/* Write a frame, in standard format */
	int (*write)(struct ast_channel *chan, struct ast_frame *frame);
};

/* Create a channel structure */
struct ast_channel *ast_channel_alloc();
#define ast_channel_free(a) free(a)

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
