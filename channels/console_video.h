/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 Luigi Rizzo
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

/*
 * Common header for console video support
 *
 * $Revision$
 */

#ifndef CONSOLE_VIDEO_H
#define CONSOLE_VIDEO_H

#if !defined(HAVE_VIDEO_CONSOLE) || !defined(HAVE_FFMPEG)
#define CONSOLE_VIDEO_CMDS					\
		"console {device}"
#else

#ifdef HAVE_X11
#include <X11/Xlib.h>           /* this should be conditional */
#endif
        
#include <ffmpeg/avcodec.h>
#ifndef OLD_FFMPEG
#include <ffmpeg/swscale.h>     /* requires a recent ffmpeg */
#endif

#define CONSOLE_VIDEO_CMDS                              \
        "console {videodevice|videocodec|sendvideo"     \
        "|video_size|bitrate|fps|qmin"                  \
        "|keypad|keypad_mask|keypad_entry"              \
	"|sdl_videodriver"				\
        "|device"					\
	"}"

#endif	/* HAVE_VIDEO_CONSOLE and others */

/*
 * In many places we use buffers to store the raw frames (but not only),
 * so here is a structure to keep all the info. data = NULL means the
 * structure is not initialized, so the other fields are invalid.
 * size = 0 means the buffer is not malloc'ed so we don't have to free it.
 */
struct fbuf_t {		/* frame buffers, dynamically allocated */
	uint8_t	*data;	/* memory, malloced if size > 0, just reference
			 * otherwise */
	int	size;	/* total size in bytes */
	int	used;	/* space used so far */
	int	ebit;	/* bits to ignore at the end */
	int	x;	/* origin, if necessary */
	int	y;
	int	w;	/* size */ 
	int	h;
	int	pix_fmt;
};

struct video_desc;		/* opaque type for video support */
struct video_desc *get_video_desc(struct ast_channel *c);

/* linked by console_video.o */
int console_write_video(struct ast_channel *chan, struct ast_frame *f);
extern int console_video_formats;
int console_video_cli(struct video_desc *env, const char *var, int fd);
int console_video_config(struct video_desc **penv, const char *var, const char *val);
void console_video_uninit(struct video_desc *env);
void console_video_start(struct video_desc *env, struct ast_channel *owner);

#endif /* CONSOLE_VIDEO_H */
/* end of file */
