/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to set callerid
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
#include <asterisk/translate.h>
#include <asterisk/image.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Set CallerID Application";

static char *app = "SetCallerID";

static char *synopsis = "Set CallerID";

static char *descrip = 
"  SetCallerID(clid[|a]): Set Caller*ID on a call to a new\n"
"value.  Sets ANI as well if a flag is used.  Always returns 0\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int setcallerid_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char tmp[256] = "";
	struct localuser *u;
	char *opt;
	int anitoo = 0;
	if (data)
		strncpy(tmp, (char *)data, sizeof(tmp) - 1);
	opt = strchr(tmp, '|');
	if (opt) {
		*opt = '\0';
		opt++;
		if (*opt == 'a')
			anitoo = 1;
	}
	LOCAL_USER_ADD(u);
	ast_set_callerid(chan, strlen(tmp) ? tmp : NULL, anitoo);
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
	return ast_register_application(app, setcallerid_exec, synopsis, descrip);
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
