/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Private channel definitions for channel implementations only.
 * 
 * Copyright (C) 1999, Mark Spencer
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
	/*! Private data used by channel backend */
	void *pvt;
	struct ast_frame *readq;
	int alertpipe[2];
	/*! Write translation path */
	struct ast_trans_pvt *writetrans;
	/*! Read translation path */
	struct ast_trans_pvt *readtrans;
	/*! Raw read format */
	int rawreadformat;
	/*! Raw write format */
	int rawwriteformat;
	/*! Send a literal DTMF digit */
	int (*send_digit)(struct ast_channel *chan, char digit);
	/*! Call a given phone number (address, etc), but don't
	   take longer than timeout seconds to do so.  */
	int (*call)(struct ast_channel *chan, char *addr, int timeout);
	/*! Hangup (and possibly destroy) the channel */
	int (*hangup)(struct ast_channel *chan);
	/*! Answer the line */
	int (*answer)(struct ast_channel *chan);
	/*! Read a frame, in standard format */
	struct ast_frame * (*read)(struct ast_channel *chan);
	/*! Write a frame, in standard format */
	int (*write)(struct ast_channel *chan, struct ast_frame *frame);
	/*! Display or transmit text */
	int (*send_text)(struct ast_channel *chan, char *text);
	/*! Display or send an image */
	int (*send_image)(struct ast_channel *chan, struct ast_frame *frame);
	/*! Send HTML data */
	int (*send_html)(struct ast_channel *chan, int subclass, char *data, int len);
	/*! Handle an exception, reading a frame */
	struct ast_frame * (*exception)(struct ast_channel *chan);
	/*! Bridge two channels of the same type together */
	int (*bridge)(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc);
	/*! Indicate a particular condition (e.g. AST_CONTROL_BUSY or AST_CONTROL_RINGING or AST_CONTROL_CONGESTION */
	int (*indicate)(struct ast_channel *c, int condition);
	/*! Fix up a channel:  If a channel is consumed, this is called.  Basically update any ->owner links */
	int (*fixup)(struct ast_channel *oldchan, struct ast_channel *newchan);
	/*! Set a given option */
	int (*setoption)(struct ast_channel *chan, int option, void *data, int datalen);
	/*! Query a given option */
	int (*queryoption)(struct ast_channel *chan, int option, void *data, int *datalen);
	/*! Blind transfer other side */
	int (*transfer)(struct ast_channel *chan, char *newdest);
	/*! Write a frame, in standard format */
	int (*write_video)(struct ast_channel *chan, struct ast_frame *frame);
	/*! Find bridged channel */
	struct ast_channel * (*bridged_channel)(struct ast_channel *chan, struct ast_channel *bridge);
};

/*! Create a channel structure */
/*! Returns NULL on failure to allocate */
struct ast_channel *ast_channel_alloc(int needalertpipe);

/*! Queue an outgoing frame */
int ast_queue_frame(struct ast_channel *chan, struct ast_frame *f);

int ast_queue_hangup(struct ast_channel *chan);

int ast_queue_control(struct ast_channel *chan, int control);

/*! Change the state of a channel */
int ast_setstate(struct ast_channel *chan, int state);

void ast_change_name(struct ast_channel *chan, char *newname);

/*! Free a channel structure */
void  ast_channel_free(struct ast_channel *);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
