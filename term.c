/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Management
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/term.h"
#include "asterisk/options.h"
#include "asterisk/lock.h"
#include "asterisk.h"

static int vt100compat = 0;

static char prepdata[80] = "";
static char enddata[80] = "";
static char quitdata[80] = "";

static const char *termpath[] = {
	"/usr/share/terminfo",
	"/usr/local/share/misc/terminfo",
	"/usr/lib/terminfo",
	NULL
	};

/* Ripped off from Ross Ridge, but it's public domain code (libmytinfo) */
static short convshort(char *s)
{
	register int a,b;

	a = (int) s[0] & 0377;
	b = (int) s[1] & 0377;

	if (a == 0377 && b == 0377)
		return -1;
	if (a == 0376 && b == 0377)
		return -2;

	return a + b * 256;
}

int term_init(void)
{
	char *term = getenv("TERM");
	char termfile[256] = "";
	char buffer[512] = "";
	int termfd = -1, parseokay = 0, i;

	if (!term)
		return 0;
	if (!option_console || option_nocolor || !option_nofork)
		return 0;

	for (i=0 ;; i++) {
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

	if (vt100compat) {
		/* Make commands show up in nice colors */
		snprintf(prepdata, sizeof(prepdata), "%c[%d;%d;%dm", ESC, ATTR_BRIGHT, COLOR_BROWN, COLOR_BLACK + 10);
		snprintf(enddata, sizeof(enddata), "%c[%d;%d;%dm", ESC, ATTR_RESET, COLOR_WHITE, COLOR_BLACK + 10);
		snprintf(quitdata, sizeof(quitdata), "%c[0m", ESC);
	}
	return 0;
}

char *term_color(char *outbuf, const char *inbuf, int fgcolor, int bgcolor, int maxout)
{
	int attr=0;
	char tmp[40];
	if (!vt100compat) {
		strncpy(outbuf, inbuf, maxout -1);
		return outbuf;
	}
	if (!fgcolor && !bgcolor) {
		strncpy(outbuf, inbuf, maxout - 1);
		return outbuf;
	}
	if ((fgcolor & 128) && (bgcolor & 128)) {
		/* Can't both be highlighted */
		strncpy(outbuf, inbuf, maxout - 1);
		return outbuf;
	}
	if (!bgcolor)
		bgcolor = COLOR_BLACK;

	if (bgcolor) {
		bgcolor &= ~128;
		bgcolor += 10;
	}
	if (fgcolor & 128) {
		attr = ATTR_BRIGHT;
		fgcolor &= ~128;
	}
	if (fgcolor && bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d;%d", fgcolor, bgcolor);
	} else if (bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", bgcolor);
	} else if (fgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", fgcolor);
	}
	if (attr) {
		snprintf(outbuf, maxout, "%c[%d;%sm%s%c[0;%d;%dm", ESC, attr, tmp, inbuf, ESC, COLOR_WHITE, COLOR_BLACK + 10);
	} else {
		snprintf(outbuf, maxout, "%c[%sm%s%c[0;%d;%dm", ESC, tmp, inbuf, ESC, COLOR_WHITE, COLOR_BLACK + 10);
	}
	return outbuf;
}

char *term_color_code(char *outbuf, int fgcolor, int bgcolor, int maxout)
{
	int attr=0;
	char tmp[40];
	if ((!vt100compat) || (!fgcolor && !bgcolor)) {
		*outbuf = '\0';
		return outbuf;
	}
	if ((fgcolor & 128) && (bgcolor & 128)) {
		/* Can't both be highlighted */
		*outbuf = '\0';
		return outbuf;
	}
	if (!bgcolor)
		bgcolor = COLOR_BLACK;

	if (bgcolor) {
		bgcolor &= ~128;
		bgcolor += 10;
	}
	if (fgcolor & 128) {
		attr = ATTR_BRIGHT;
		fgcolor &= ~128;
	}
	if (fgcolor && bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d;%d", fgcolor, bgcolor);
	} else if (bgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", bgcolor);
	} else if (fgcolor) {
		snprintf(tmp, sizeof(tmp), "%d", fgcolor);
	}
	if (attr) {
		snprintf(outbuf, maxout, "%c[%d;%sm", ESC, attr, tmp);
	} else {
		snprintf(outbuf, maxout, "%c[%sm", ESC, tmp);
	}
	return outbuf;
}

char *term_strip(char *outbuf, char *inbuf, int maxout)
{
	char *outbuf_ptr = outbuf, *inbuf_ptr = inbuf;

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
		strncpy(outbuf, inbuf, maxout -1);
		return outbuf;
	}
	snprintf(outbuf, maxout, "%c[%d;%d;%dm%c%c[%d;%d;%dm%s",
		ESC, ATTR_BRIGHT, COLOR_BLUE, COLOR_BLACK + 10,
		inbuf[0],
		ESC, 0, COLOR_WHITE, COLOR_BLACK + 10,
		inbuf + 1);
	return outbuf;
}

char *term_prep(void)
{
	return prepdata;
}

char *term_end(void)
{
	return enddata;
}

char *term_quit(void)
{
	return quitdata;
}
