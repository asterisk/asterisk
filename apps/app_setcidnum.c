/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to set callerid
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 * Oliver Daudey <traveler@xs4all.nl>
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
#include <asterisk/callerid.h>
#include <asterisk/utils.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Set CallerID Number";

static char *app = "SetCIDNum";

static char *synopsis = "Set CallerID Number";

static char *descrip = 
"  SetCIDNum(cnum[|a]): Set Caller*ID Number on a call to a new\n"
"value, while preserving the original Caller*ID name.  This is\n"
"useful for providing additional information to the called\n"
"party. Sets ANI as well if a flag is used.  Always returns 0\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int setcallerid_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char tmp[256] = "";
	char oldcid[256] = "", *l, *n;
	char newcid[256] = "";
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
	if (chan->callerid) {
		strncpy(oldcid, chan->callerid, sizeof(oldcid) - 1);
		ast_callerid_parse(oldcid, &n, &l);
		l = tmp;
		if (!ast_strlen_zero(l)) {
			if (n && !ast_strlen_zero(n))
				snprintf(newcid, sizeof(newcid), "\"%s\" <%s>", n, l);
			else
				strncpy(newcid, tmp, sizeof(newcid) - 1);
		} else if (n && !ast_strlen_zero(n)) {
			strncpy(newcid, n, sizeof(newcid) - 1);
		}
	} else
		strncpy(newcid, tmp, sizeof(newcid) - 1);
	ast_set_callerid(chan, !ast_strlen_zero(newcid) ? newcid : NULL, anitoo);
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
