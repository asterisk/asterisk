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
#include <unistd.h>
#include <asterisk/term.h>
#include <asterisk/options.h>
#include <asterisk/lock.h>
#include "asterisk.h"

static int vt100compat = 0;

static char prepdata[80] = "";
static char enddata[80] = "";
static char quitdata[80] = "";

int term_init(void)
{
	char *term = getenv("TERM");
	if (!term)
		return 0;
	if (!option_console || option_nocolor || !option_nofork)
		return 0;
	if (!strncasecmp(term, "linux", 5)) 
		vt100compat = 1; else
	if (!strncasecmp(term, "xterm", 5))
		vt100compat = 1; else
	if (!strncasecmp(term, "Eterm", 5))
		vt100compat = 1; else
	if (!strncasecmp(term, "crt", 3))
		vt100compat = 1; else
	if (!strncasecmp(term, "vt", 2))
		vt100compat = 1;
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
