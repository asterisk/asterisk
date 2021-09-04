/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

/*! \file
 *
 * \brief Terminal Routines
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "asterisk/term.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"

static int vt100compat;

static char enddata[80] = "";
static char quitdata[80] = "";

static const char * const termpath[] = {
	"/usr/share/terminfo",
	"/usr/local/share/misc/terminfo",
	"/usr/lib/terminfo",
	NULL
	};

AST_THREADSTORAGE(commonbuf);

struct commonbuf {
	short which;
	char buffer[AST_TERM_MAX_ROTATING_BUFFERS][AST_TERM_MAX_ESCAPE_CHARS];
};

static int opposite(int color)
{
	int lookup[] = {
		/* BLACK */ COLOR_BLACK,
		/* RED */ COLOR_MAGENTA,
		/* GREEN */ COLOR_GREEN,
		/* BROWN */ COLOR_BROWN,
		/* BLUE */ COLOR_CYAN,
		/* MAGENTA */ COLOR_RED,
		/* CYAN */ COLOR_BLUE,
		/* WHITE */ COLOR_BLACK };
	return color ? lookup[color - 30] : 0;
}

/* Ripped off from Ross Ridge, but it's public domain code (libmytinfo) */
static short convshort(unsigned char *s)
{
	register int a, b;

	a = (int) s[0] & 0377;
	b = (int) s[1] & 0377;

	if (a == 0377 && b == 0377)
		return -1;
	if (a == 0376 && b == 0377)
		return -2;

	return a + b * 256;
}

static inline int convint(unsigned char *s)
{
	return s[0]
		| s[1] << 8
		| s[2] << 16
		| s[3] << 24;
}

#define MAGIC_LEGACY (00432)
#define MAGIC_EXTNUM (01036)

#define HEADER_LEN (12)
#define MAX_COLORS_INDEX (13)

static int parse_terminfo_file(int fd)
{
	int bytes_read, bytes_needed, num_size;
	short magic, sz_names, sz_bools;
	unsigned char buffer[1024];

	bytes_read = read(fd, buffer, sizeof(buffer));
	if (bytes_read < HEADER_LEN) {
		return 0;
	}

	magic = convshort(buffer);

	if (magic == MAGIC_LEGACY) {
		num_size = 2;
	} else if (magic == MAGIC_EXTNUM) {
		/* Extended number format (ncurses 6.1) */
		num_size = 4;
	} else {
		/* We don't know how to parse this file */
		return 0;
	}

	sz_names = convshort(buffer + 2);
	sz_bools = convshort(buffer + 4);

	/* From term(5):
	 * Between the boolean section and the number section, a null byte will be
	 * inserted, if necessary, to ensure that the number section begins on an
	 * even byte. */
	if ((sz_names + sz_bools) & 1) {
		sz_bools++;
	}

	bytes_needed = HEADER_LEN + sz_names + sz_bools + ((MAX_COLORS_INDEX + 1) * num_size);
	if (bytes_needed <= bytes_read) {
		/* Offset 13 is defined in /usr/include/term.h, though we do not
		 * include it here, as it conflicts with include/asterisk/term.h */
		int max_colors;
		int offset = HEADER_LEN + sz_names + sz_bools + MAX_COLORS_INDEX * num_size;

		if (num_size == 2) {
			/* In the legacy terminfo format, numbers are signed shorts */
			max_colors = convshort(buffer + offset);
		} else {
			/* Extended number format makes them signed ints */
			max_colors = convint(buffer + offset);
		}

		if (max_colors > 0) {
			vt100compat = 1;
		}

		return 1;
	}

	return 0;
}

int ast_term_init(void)
{
	char *term = getenv("TERM");
	char termfile[256] = "";
	int termfd = -1, parseokay = 0, i;

	if (ast_opt_no_color) {
		return 0;
	}

	if (!ast_opt_console) {
		/* If any remote console is not compatible, we'll strip the color codes at that point */
		vt100compat = 1;
		goto end;
	}

	if (!term) {
		return 0;
	}

	for (i = 0; !parseokay && termpath[i]; i++) {
		snprintf(termfile, sizeof(termfile), "%s/%c/%s", termpath[i], *term, term);

		termfd = open(termfile, O_RDONLY);
		if (termfd > -1) {
			parseokay = parse_terminfo_file(termfd);
			close(termfd);
		}
	}

	if (!parseokay) {
		/* These comparisons should not be substrings nor case-insensitive, as
		 * terminal types are very particular about how they treat suffixes and
		 * capitalization.  For example, terminal type 'linux-m' does NOT
		 * support color, while 'linux' does.  Not even all vt100* terminals
		 * support color, either (e.g. 'vt100+fnkeys'). */
		if (!strcmp(term, "linux")) {
			vt100compat = 1;
		} else if (!strcmp(term, "xterm")) {
			vt100compat = 1;
		} else if (!strcmp(term, "xterm-color")) {
			vt100compat = 1;
		} else if (!strcmp(term, "xterm-256color")) {
			vt100compat = 1;
		} else if (!strncmp(term, "Eterm", 5)) {
			/* Both entries which start with Eterm support color */
			vt100compat = 1;
		} else if (!strcmp(term, "vt100")) {
			vt100compat = 1;
		} else if (!strncmp(term, "crt", 3)) {
			/* Both crt terminals support color */
			vt100compat = 1;
		}
	}

end:
	if (vt100compat) {
		/* Make commands show up in nice colors */
		if (ast_opt_light_background) {
			snprintf(enddata, sizeof(enddata), "%c[%dm", ESC, COLOR_BLACK);
		} else if (ast_opt_force_black_background) {
			snprintf(enddata, sizeof(enddata), "%c[%d;%d;%dm", ESC, ATTR_RESET, COLOR_WHITE, COLOR_BLACK + 10);
		} else {
			snprintf(enddata, sizeof(enddata), "%c[%dm", ESC, ATTR_RESET);
		}
		snprintf(quitdata, sizeof(quitdata), "%c[%dm", ESC, ATTR_RESET);
	}
	return 0;
}

char *term_color(char *outbuf, const char *inbuf, int fgcolor, int bgcolor, int maxout)
{
	int attr = 0;

	if (!vt100compat) {
		ast_copy_string(outbuf, inbuf, maxout);
		return outbuf;
	}
	if (!fgcolor) {
		ast_copy_string(outbuf, inbuf, maxout);
		return outbuf;
	}

	if (fgcolor & 128) {
		attr = ast_opt_light_background ? 0 : ATTR_BRIGHT;
		fgcolor &= ~128;
	}

	if (bgcolor) {
		bgcolor &= ~128;
	}

	if (ast_opt_light_background) {
		fgcolor = opposite(fgcolor);
	}

	if (ast_opt_force_black_background) {
		if (!bgcolor) {
			bgcolor = COLOR_BLACK;
		}
		snprintf(outbuf, maxout, "%c[%d;%d;%dm%s%s", ESC, attr, fgcolor, bgcolor + 10, inbuf, term_end());
	} else {
		snprintf(outbuf, maxout, "%c[%d;%dm%s%s", ESC, attr, fgcolor, inbuf, term_end());
	}
	return outbuf;
}

static void check_fgcolor(int *fgcolor, int *attr)
{
	*attr = ast_opt_light_background ? 0 : ATTR_BRIGHT;
	if (*fgcolor & 128) {
		*fgcolor &= ~128;
	}

	if (ast_opt_light_background) {
		*fgcolor = opposite(*fgcolor);
	}
}

static void check_bgcolor(int *bgcolor)
{
	if (*bgcolor) {
		*bgcolor &= ~128;
	}
}

static int check_colors_allowed(void)
{
	return vt100compat;
}

int ast_term_color_code(struct ast_str **str, int fgcolor, int bgcolor)
{
	int attr = 0;

	if (!check_colors_allowed()) {
		return -1;
	}

	check_fgcolor(&fgcolor, &attr);
	check_bgcolor(&bgcolor);

	if (ast_opt_force_black_background) {
		ast_str_append(str, 0, "%c[%d;%d;%dm", ESC, attr, fgcolor, COLOR_BLACK + 10);
	} else if (bgcolor) {
		ast_str_append(str, 0, "%c[%d;%d;%dm", ESC, attr, fgcolor, bgcolor + 10);
	} else {
		ast_str_append(str, 0, "%c[%d;%dm", ESC, attr, fgcolor);
	}

	return 0;
}

char *term_color_code(char *outbuf, int fgcolor, int bgcolor, int maxout)
{
	int attr = 0;

	if (!check_colors_allowed()) {
		*outbuf = '\0';
		return outbuf;
	}

	check_fgcolor(&fgcolor, &attr);
	check_bgcolor(&bgcolor);

	if (ast_opt_force_black_background) {
		snprintf(outbuf, maxout, "%c[%d;%d;%dm", ESC, attr, fgcolor, COLOR_BLACK + 10);
	} else if (bgcolor) {
		snprintf(outbuf, maxout, "%c[%d;%d;%dm", ESC, attr, fgcolor, bgcolor + 10);
	} else {
		snprintf(outbuf, maxout, "%c[%d;%dm", ESC, attr, fgcolor);
	}

	return outbuf;
}

const char *ast_term_color(int fgcolor, int bgcolor)
{
	struct commonbuf *cb = ast_threadstorage_get(&commonbuf, sizeof(*cb));
	char *buf;

	if (!cb) {
		return "";
	}
	buf = cb->buffer[cb->which++];
	if (cb->which == AST_TERM_MAX_ROTATING_BUFFERS) {
		cb->which = 0;
	}

	return term_color_code(buf, fgcolor, bgcolor, AST_TERM_MAX_ESCAPE_CHARS);
}

const char *ast_term_reset(void)
{
	return term_end();
}

char *term_strip(char *outbuf, const char *inbuf, int maxout)
{
	char *outbuf_ptr = outbuf;
	const char *inbuf_ptr = inbuf;

	while (outbuf_ptr < outbuf + maxout) {
		switch (*inbuf_ptr) {
			case ESC:
				while (*inbuf_ptr && (*inbuf_ptr != 'm'))
					inbuf_ptr++;
				break;
			default:
				*outbuf_ptr = *inbuf_ptr;
				outbuf_ptr++;
		}
		if (! *inbuf_ptr)
			break;
		inbuf_ptr++;
	}
	return outbuf;
}

/* filter escape sequences */
void term_filter_escapes(char *line)
{
	int i;
	int len = strlen(line);

	for (i = 0; i < len; i++) {
		if (line[i] != ESC)
			continue;
		if ((i < (len - 2)) &&
		    (line[i + 1] == 0x5B)) {
			switch (line[i + 2]) {
			case 0x30:
			case 0x31:
			case 0x33:
				continue;
			}
		}
		/* replace ESC with a space */
		line[i] = ' ';
	}
}

const char *term_end(void)
{
	return enddata;
}

const char *term_quit(void)
{
	return quitdata;
}
