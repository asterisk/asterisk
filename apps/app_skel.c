/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Skeleton application
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
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static char *tdesc = "Trivial skeleton Application";
static char *app = "skel";
static char *synopsis = 
"  This is a skeleton application that shows you the basic structure to create your\n"
"own asterisk applications.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int skel_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	if (!data) {
		ast_log(LOG_WARNING, "skel requires an argument (filename)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	/* Do our thing here */
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
	return ast_register_application(app, skel_exec, synopsis, tdesc);
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
