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
"  Read(variable[|filename][|maxdigits]):  Reads a #-terminated string of digits from\n"
"the user, optionally playing a given filename first.  Returns -1 on hangup or\n"
"error and 0 otherwise.\n"
"  maxdigits   -- maximum acceptable number of digits. Stops reading after maxdigits\n"
"                 have been entered (without requiring the user press the '#' key).\n"
"                 Defaults to 0 - no limit - wait for the user press the '#' key.\n"
"                 Any value below 0 means the same. Max accepted value is 255.\n";	

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int read_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	char tmp2[256]="";
	char *filename;
	char *stringp;
	char *maxdigitstr;
	int maxdigits=255;
	if (!data || !strlen((char *)data)) {
		ast_log(LOG_WARNING, "Read requires an argument (variable)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	stringp=tmp;
	strsep(&stringp, "|");
	filename = strsep(&stringp, "|");
	maxdigitstr = strsep(&stringp,"|");
	if (maxdigitstr)
	{
	    maxdigits = atoi(maxdigitstr);
	    if ((maxdigits<1) || (maxdigits>255)) {
    		maxdigits = 255;
	    }
	    else
		ast_verbose(VERBOSE_PREFIX_3 "Accepting a maximum of %i digits.\n", maxdigits);
	}	
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
		res = ast_app_getdata(chan, filename, tmp2, maxdigits, 0);
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
