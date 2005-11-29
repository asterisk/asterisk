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
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <asterisk/channel.h>
#include <asterisk/file.h>
#include <asterisk/app.h>

/* set timeout to 0 for "standard" timeouts. Set timeout to -1 for 
   "ludicrous time" (essentially never times out) */
int ast_app_getdata(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout)
{
	int res,to,fto;
	if (prompt) {
		res = ast_streamfile(c, prompt, c->language);
		if (res < 0)
			return res;
	}
	fto = 6000;
	to = 2000;
	if (timeout > 0) fto = to = timeout;
	if (timeout < 0) fto = to = 1000000000;
	res = ast_readstring(c, s, maxlen, to, fto, "#");
	return res;
}

