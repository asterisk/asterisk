/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Execute arbitrary system commands
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"	/* autoservice */

static char *app = "System";

static char *app2 = "TrySystem";

static char *synopsis = "Execute a system command";

static char *synopsis2 = "Try executing a system command";

static char *chanvar = "SYSTEMSTATUS";

static char *descrip =
"  System(command): Executes a command  by  using  system(). If the command\n"
"fails, the console should report a fallthrough. \n"
"Result of execution is returned in the SYSTEMSTATUS channel variable:\n"
"   FAILURE	Could not execute the specified command\n"
"   SUCCESS	Specified command successfully executed\n";

static char *descrip2 =
"  TrySystem(command): Executes a command  by  using  system().\n"
"on any situation.\n"
"Result of execution is returned in the SYSTEMSTATUS channel variable:\n"
"   FAILURE	Could not execute the specified command\n"
"   SUCCESS	Specified command successfully executed\n"
"   APPERROR	Specified command successfully executed, but returned error code\n";

static int system_exec_helper(struct ast_channel *chan, void *data, int failmode)
{
	int res = 0;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "System requires an argument(command)\n");
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		return failmode;
	}

	ast_autoservice_start(chan);

	/* Do our thing here */
	res = ast_safe_system((char *)data);
	if ((res < 0) && (errno != ECHILD)) {
		ast_log(LOG_WARNING, "Unable to execute '%s'\n", (char *)data);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		res = failmode;
	} else if (res == 127) {
		ast_log(LOG_WARNING, "Unable to execute '%s'\n", (char *)data);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		res = failmode;
	} else {
		if (res < 0) 
			res = 0;
		if (res != 0)
			pbx_builtin_setvar_helper(chan, chanvar, "APPERROR");
		else
			pbx_builtin_setvar_helper(chan, chanvar, "SUCCESS");
		res = 0;
	} 

	ast_autoservice_stop(chan);

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

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_unregister_application(app2);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application(app2, trysystem_exec, synopsis2, descrip2);
	res |= ast_register_application(app, system_exec, synopsis, descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Generic System() application");
