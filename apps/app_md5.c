/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * MD5 checksum application
 * 
 * Copyright (C) 2005, Olle E. Johansson, Edvina.net
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/utils.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static char *tdesc = "MD5 checksum application";
static char *app_md5 = "md5";
static char *synopsis = 
"  md5(<var>=<string>): Calculates a MD5 checksum on <string>.\n"
"Returns hash value in a channel variable. Always return 0\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int md5_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *varname= NULL; /* Variable to set */
	char *string = NULL; /* String to calculate on */
	char retvar[50]; /* Return value */

	if (!data) {
		ast_log(LOG_WARNING, "Syntax: md5(<varname>=<string>) - missing argument!\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	memset(retvar,0, sizeof(retvar));
	string = ast_strdupa(data);
	varname = strsep(&string,"=");
	if (ast_strlen_zero(varname)) {
		ast_log(LOG_WARNING, "Syntax: md5(<varname>=<string>) - missing argument!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	ast_md5_hash(retvar, string);
	pbx_builtin_setvar_helper(chan, varname, retvar);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app_md5);
}

int load_module(void)
{
	return ast_register_application(app_md5, md5_exec, synopsis, tdesc);
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
