/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Time of day - Report the time of day
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/say.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Date and Time";

static char *app = "DateTime";

static char *synopsis = "Say the date and time";

static char *descrip = 
"  DateTime():  Says the current date and time.  Returns -1 on hangup or 0\n"
"otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int datetime_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	time_t t;
	struct localuser *u;
	LOCAL_USER_ADD(u);
	time(&t);
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res)
		res = ast_say_datetime(chan, t, "", chan->language);
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
	return ast_register_application(app, datetime_exec, synopsis, descrip);
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

char *key()
{
	return ASTERISK_GPL_KEY;
}
