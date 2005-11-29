/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Execute arbitrary system commands
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
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
#include <asterisk/module.h>
#include <asterisk/app.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static char *tdesc = "Generic System() application";

static char *app = "System";

static char *app2 = "TrySystem";

static char *synopsis = "Execute a system command";

static char *synopsis2 = "Try executing a system command";

static char *descrip =
"  System(command): Executes a command  by  using  system(). Returns -1 on\n"
"failure to execute the specified command. If  the command itself executes\n"
"but is in error, and if there exists a priority n + 101, where 'n' is the\n"
"priority of the current instance, then  the  channel  will  be  setup  to\n"
"continue at that priority level.  Otherwise, System returns 0.\n";

static char *descrip2 =
"  TrySystem(command): Executes a command  by  using  system(). Returns 0\n"
"on any situation. If  the command itself executes but is in error, and if\n"
"there exists a priority n + 101, where 'n' is the priority of the current\n"
"instance, then  the  channel  will  be  setup  to continue at that\n"
"priority level.  Otherwise, System returns 0.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int system_exec_helper(struct ast_channel *chan, void *data, int failmode)
{
	int res=0;
	struct localuser *u;
	if (!data) {
		ast_log(LOG_WARNING, "System requires an argument(command)\n");
		return failmode;
	}
	LOCAL_USER_ADD(u);
	/* Do our thing here */
	res = ast_safe_system((char *)data);
	if ((res < 0) && (errno != ECHILD)) {
		ast_log(LOG_WARNING, "Unable to execute '%s'\n", (char *)data);
		res = failmode;
	} else if (res == 127) {
		ast_log(LOG_WARNING, "Unable to execute '%s'\n", (char *)data);
		res = failmode;
	} else {
		if (res < 0)
			res = 0;
		if (res && ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid)) 
			chan->priority+=100;
		res = 0;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int system_exec(struct ast_channel *chan, void *data)
{
	return system_exec_helper(chan, data, -1);
}

static int trysystem_exec(struct ast_channel *chan, void *data)
{
	return system_exec_helper(chan, data, 0);
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_unregister_application(app2);
	return ast_unregister_application(app);
}

int load_module(void)
{
	ast_register_application(app2, trysystem_exec, synopsis2, descrip2);
	return ast_register_application(app, system_exec, synopsis, descrip);
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
