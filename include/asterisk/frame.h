/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Asterisk internal frame definitions.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_FRAME_H
#define _ASTERISK_FRAME_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* A frame of data read used to communicate between 
   between channels and applications */
struct ast_frame {
	int frametype;				/* Kind of frame */
	int subclass;				/* Subclass, frame dependent */
	int datalen;				/* Length of data */
	int timelen;				/* Amount of time associated with this frame */
	int mallocd;				/* Was the data malloc'd?  i.e. should we
								   free it when we discard the frame? */
	int offset;					/* How far into "data" the data really starts */
	char *src;					/* Optional source of frame for debugging */
	void *data;					/* Pointer to actual data */
};

struct ast_frame_chain {
	/* XXX Should ast_frame chain's be just prt of frames, i.e. should they just link? XXX */
	struct ast_frame *fr;
	struct ast_frame_chain *next;
};

#define AST_FRIENDLY_OFFSET 	64		/* It's polite for a a new frame to
										   have at least this number of bytes
										   of offset before your real frame data
										   so that additional headers can be
										   added. */

#define AST_MALLOCD_HDR		(1 << 0)	/* Need the header be free'd? */
#define AST_MALLOCD_DATA	(1 << 1)	/* Need the data be free'd? */
#define AST_MALLOCD_SRC		(1 << 2)	/* Need the source be free'd? (haha!) */

/* Frame types */
#define AST_FRAME_DTMF		1		/* A DTMF digit, subclass is the digit */
#define AST_FRAME_VOICE		2		/* Voice data, subclass is AST_FORMAT_* */
#define AST_FRAME_VIDEO		3		/* Video frame, maybe?? :) */
#define AST_FRAME_CONTROL	4		/* A control frame, subclass is AST_CONTROL_* */
#define AST_FRAME_NULL		5		/* An empty, useless frame */
#define AST_FRAME_IAX		6		/* Inter Aterisk Exchange private frame type */
#define AST_FRAME_TEXT		7		/* Text messages */

/* Data formats for capabilities and frames alike */
#define AST_FORMAT_G723_1	(1 << 0)	/* G.723.1 compression */
#define AST_FORMAT_GSM		(1 << 1)	/* GSM compression */
#define AST_FORMAT_ULAW		(1 << 2)	/* Raw mu-law data (G.711) */
#define AST_FORMAT_ALAW		(1 << 3)	/* Raw A-law data (G.711) */
#define AST_FORMAT_MP3		(1 << 4)	/* MPEG-2 layer 3 */
#define AST_FORMAT_ADPCM	(1 << 5)	/* ADPCM (whose?) */
#define AST_FORMAT_SLINEAR	(1 << 6)	/* Raw 16-bit Signed Linear (8000 Hz) PCM */
#define AST_FORMAT_LPC10	(1 << 7)	/* LPC10, 180 samples/frame */
#define AST_FORMAT_MAX_AUDIO (1 << 15)	/* Maximum audio format */
#define AST_FORMAT_JPEG		(1 << 16)	/* JPEG Images */
#define AST_FORMAT_PNG		(1 << 17)	/* PNG Images */
#define AST_FORMAT_H261		(1 << 18)	/* H.261 Video */
#define AST_FORMAT_H263		(1 << 19)	/* H.263 Video */

/* Control frame types */
#define AST_CONTROL_HANGUP		1			/* Other end has hungup */
#define AST_CONTROL_RING		2			/* Local ring */
#define AST_CONTROL_RINGING 	3			/* Remote end is ringing */
#define AST_CONTROL_ANSWER		4			/* Remote end has answered */
#define AST_CONTROL_BUSY		5			/* Remote end is busy */
#define AST_CONTROL_TAKEOFFHOOK 6			/* Make it go off hook */
#define AST_CONTROL_OFFHOOK		7			/* Line is off hook */

/* Request a frame be allocated.  source is an optional source of the frame, 
   len is the requested length, or "0" if the caller will supply the buffer */
struct ast_frame *ast_fralloc(char *source, int len);

/* Free a frame, and the memory it used if applicable */
void ast_frfree(struct ast_frame *fr);

/* Take a frame, and if it's not been malloc'd, make a malloc'd copy
   and if the data hasn't been malloced then make the
   data malloc'd.  If you need to store frames, say for queueing, then
   you should call this function. */
struct ast_frame *ast_frisolate(struct ast_frame *fr);

/* Dupliates a frame -- should only rarely be used, typically frisolate is
   good enough */
struct ast_frame *ast_frdup(struct ast_frame *fr);

void ast_frchain(struct ast_frame_chain *fc);

/* Read a frame from a stream or packet fd, as written by fd_write */
struct ast_frame *ast_fr_fdread(int fd);

/* Write a frame to an fd */
int ast_fr_fdwrite(int fd, struct ast_frame *frame);

/* Get a format by its name */
extern int ast_getformatbyname(char *name);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
