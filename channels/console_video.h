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

#include <ffmpeg/avcodec.h>
#ifndef OLD_FFMPEG
#include <ffmpeg/swscale.h>     /* requires a recent ffmpeg */
#endif

#define CONSOLE_VIDEO_CMDS			\
	"console {videodevice|videocodec"	\
	"|video_size|bitrate|fps|qmin"		\
	"|sendvideo|keypad"			\
	"|sdl_videodriver"			\
	"|device|startgui|stopgui"		\
	"}"

#endif	/* HAVE_VIDEO_CONSOLE and others */

#define	SRC_WIN_W	80	/* width of video thumbnails */
#define	SRC_WIN_H	60	/* height of video thumbnails */
/* we only support a limited number of video sources in the GUI,
 * because we need screen estate to switch between them.
 */
#define	MAX_VIDEO_SOURCES	9

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
	/* offsets and size of the copy in Picture-in-Picture mode */
	int	win_x;
	int	win_y;
	int	win_w;
	int	win_h;
};

void fbuf_free(struct fbuf_t *);

/* descriptor for a grabber */
struct grab_desc {
	const char *name;
	void *(*open)(const char *name, struct fbuf_t *geom, int fps);
	struct fbuf_t *(*read)(void *d);
	void (*move)(void *d, int dx, int dy);
	void *(*close)(void *d);
};

extern struct grab_desc *console_grabbers[];

struct video_desc;		/* opaque type for video support */
struct video_desc *get_video_desc(struct ast_channel *c);

/* linked by console_video.o */
int console_write_video(struct ast_channel *chan, struct ast_frame *f);
extern int console_video_formats;
int console_video_cli(struct video_desc *env, const char *var, int fd);
int console_video_config(struct video_desc **penv, const char *var, const char *val);
void console_video_uninit(struct video_desc *env);
void console_video_start(struct video_desc *env, struct ast_channel *owner);
int get_gui_startup(struct video_desc* env);

/* console_board.c */

/* Where do we send the keyboard/keypad output */
enum kb_output {
	KO_NONE,
	KO_INPUT,	/* the local input window */
	KO_DIALED,	/* the 'dialed number' window */
	KO_MESSAGE,	/* the 'message' window */
};

enum drag_window {	/* which window are we dragging */
	DRAG_NONE,
	DRAG_LOCAL,	/* local video */
	DRAG_REMOTE,	/* remote video */
	DRAG_DIALED,	/* dialed number */
	DRAG_INPUT,	/* input window */
	DRAG_MESSAGE,	/* message window */
	DRAG_PIP,	/* picture in picture */
};

/*! \brief support for drag actions */
struct drag_info {
	int		x_start;	/* last known mouse position */
	int		y_start;
	enum drag_window drag_window;
};
/*! \brief info related to the gui: button status, mouse coords, etc. */
struct board;
/* !\brief print a message on a board */
void move_message_board(struct board *b, int dy);
int print_message(struct board *b, const char *s);

/*! \brief return the whole text from a board */
const char *read_message(const struct board *b);

/*! \brief reset the board to blank */
int reset_board(struct board *b);

/*! \brief deallocates memory space for a board */
void delete_board(struct board *b);
#endif /* CONSOLE_VIDEO_H */
/* end of file */
