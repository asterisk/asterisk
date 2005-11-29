/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to read a variable
 * 
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/app.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/options.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static char *tdesc = "Read Variable Application";

static char *app = "Read";

static char *synopsis = "Read a variable";

static char *descrip = 
"  Read(variable[|filename]):  Reads a '#' terminated string of digits from\n"
"the user, optionally playing a given filename first.  Returns -1 on hangup or\n"
"error and 0 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int read_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	char tmp2[128]="";
	char *filename;
	char *stringp;
	if (!data || !strlen((char *)data)) {
		ast_log(LOG_WARNING, "Read requires an argument (variable)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	stringp=tmp;
	strsep(&stringp, "|");
	filename = strsep(&stringp, "|");
	if (!strlen(tmp)) {
		ast_log(LOG_WARNING, "Read requires an variable name\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	if (chan->_state != AST_STATE_UP) {
		/* Answer if the line isn't up. */
		res = ast_answer(chan);
	}
	if (!res) {
		ast_stopstream(chan);
		res = ast_app_getdata(chan, filename, tmp2, sizeof(tmp2) - 1, 0);
		if (!res)
			pbx_builtin_setvar_helper(chan, tmp, tmp2);
		ast_verbose(VERBOSE_PREFIX_3 "User entered '%s'\n", tmp2);
	}
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
	return ast_register_application(app, read_exec, synopsis, descrip);
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
