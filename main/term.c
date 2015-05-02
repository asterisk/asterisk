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

ASTERISK_REGISTER_FILE()

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

static char prepdata[80] = "";
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
static short convshort(char *s)
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

int ast_term_init(void)
{
	char *term = getenv("TERM");
	char termfile[256] = "";
	char buffer[512] = "";
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

	for (i = 0;; i++) {
		if (termpath[i] == NULL) {
			break;
		}
		snprintf(termfile, sizeof(termfile), "%s/%c/%s", termpath[i], *term, term);
		termfd = open(termfile, O_RDONLY);
		if (termfd > -1) {
			break;
		}
	}
	if (termfd > -1) {
		int actsize = read(termfd, buffer, sizeof(buffer) - 1);
		short sz_names = convshort(buffer + 2);
		short sz_bools = convshort(buffer + 4);
		short n_nums   = convshort(buffer + 6);

		/* if ((sz_names + sz_bools) & 1)
			sz_bools++; */

		if (sz_names + sz_bools + n_nums < actsize) {
			/* Offset 13 is defined in /usr/include/term.h, though we do not
			 * include it here, as it conflicts with include/asterisk/term.h */
			short max_colors = convshort(buffer + 12 + sz_names + sz_bools + 13 * 2);
			if (max_colors > 0) {
				vt100compat = 1;
			}
			parseokay = 1;
		}
		close(termfd);
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
			snprintf(prepdata, sizeof(prepdata), "%c[%dm", ESC, COLOR_BROWN);
			snprintf(enddata, sizeof(enddata), "%c[%dm", ESC, COLOR_BLACK);
			snprintf(quitdata, sizeof(quitdata), "%c[0m", ESC);
		} else if (ast_opt_force_black_background) {
			snprintf(prepdata, sizeof(prepdata), "%c[%d;%d;%dm", ESC, ATTR_BRIGHT, COLOR_BROWN, COLOR_BLACK + 10);
			snprintf(enddata, sizeof(enddata), "%c[%d;%d;%dm", ESC, ATTR_RESET, COLOR_WHITE, COLOR_BLACK + 10);
			snprintf(quitdata, sizeof(quitdata), "%c[0m", ESC);
		} else {
			snprintf(prepdata, sizeof(prepdata), "%c[%d;%dm", ESC, ATTR_BRIGHT, COLOR_BROWN);
			snprintf(enddata, sizeof(enddata), "%c[%d;%dm", ESC, ATTR_RESET, COLOR_WHITE);
			snprintf(quitdata, sizeof(quitdata), "%c[0m", ESC);
		}
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
		snprintf(outbuf, maxout, "%c[%d;%d;%dm%s%c[%d;%dm", ESC, attr, fgcolor, bgcolor + 10, inbuf, ESC, COLOR_WHITE, COLOR_BLACK + 10);
	} else {
		snprintf(outbuf, maxout, "%c[%d;%dm%s%c[0m", ESC, attr, fgcolor, inbuf, ESC);
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

static int check_colors_allowed(int fgcolor)
{
	return (!vt100compat || !fgcolor) ? 0 : 1;
}

int ast_term_color_code(struct ast_str **str, int fgcolor, int bgcolor)
{
	int attr = 0;

	if (!check_colors_allowed(fgcolor)) {
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

	if (!check_colors_allowed(fgcolor)) {
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
	if (ast_opt_force_black_background) {
		return enddata;
	} else {
		return quitdata;
	}
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

char *term_prompt(char *outbuf, const char *inbuf, int maxout)
{
	if (!vt100compat) {
		ast_copy_string(outbuf, inbuf, maxout);
		return outbuf;
	}
	if (ast_opt_force_black_background) {
		snprintf(outbuf, maxout, "%c[%d;%d;%dm%c%c[%d;%dm%s",
			ESC, ATTR_BRIGHT, COLOR_BLUE, COLOR_BLACK + 10,
			inbuf[0],
			ESC, COLOR_WHITE, COLOR_BLACK + 10,
			inbuf + 1);
	} else if (ast_opt_light_background) {
		snprintf(outbuf, maxout, "%c[%d;0m%c%c[0m%s",
			ESC, COLOR_BLUE,
			inbuf[0],
			ESC,
			inbuf + 1);
	} else {
		snprintf(outbuf, maxout, "%c[%d;%d;0m%c%c[0m%s",
			ESC, ATTR_BRIGHT, COLOR_BLUE,
			inbuf[0],
			ESC,
			inbuf + 1);
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

const char *term_prep(void)
{
	return prepdata;
}

const char *term_end(void)
{
	return enddata;
}

const char *term_quit(void)
{
	return quitdata;
}
