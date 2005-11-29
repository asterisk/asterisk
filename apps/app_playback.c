/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to playback a sound file
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static char *tdesc = "Trivial Playback Application";

static char *app = "Playback";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int playback_exec(struct ast_channel *chan, void *data)
{
	int res;
	struct localuser *u;
	if (!data) {
		ast_log(LOG_WARNING, "Playback requires an argument (filename)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	ast_stopstream(chan);
	res = ast_streamfile(chan, (char *)data);
	if (!res)
		res = ast_waitstream(chan, "");
	ast_stopstream(chan);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, playback_exec);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}
