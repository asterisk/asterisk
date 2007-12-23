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

struct video_desc;		/* opaque type for video support */

struct video_desc *get_video_desc(struct ast_channel *c);

/* linked by console_video.o */
int console_write_video(struct ast_channel *chan, struct ast_frame *f);
extern int console_video_formats;
int console_video_cli(struct video_desc *env, const char *var, int fd);
int console_video_config(struct video_desc **penv, const char *var, const char *val);
void console_video_uninit(struct video_desc *env);
void console_video_start(struct video_desc *env, struct ast_channel *owner);

#ifdef HAVE_VIDEO_CONSOLE
#define CONSOLE_VIDEO_CMDS                              \
        "console {videodevice|videocodec|sendvideo"     \
        "|video_size|bitrate|fps|qmin"                  \
        "|keypad|keypad_mask|keypad_entry"              \
	"|sdl_videodriver"				\
        "|device"					\
	"}"

#else
#define CONSOLE_VIDEO_CMDS					\
		"console {device}"
#endif

/* end of file */
