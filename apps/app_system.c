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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"	/* autoservice */
#include "asterisk/strings.h"
#include "asterisk/threadstorage.h"

/*** DOCUMENTATION
	<application name="System" language="en_US">
		<synopsis>
			Execute a system command.
		</synopsis>
		<syntax>
			<parameter name="command" required="true">
				<para>Command to execute</para>
			</parameter>
		</syntax>
		<description>
			<para>Executes a command  by  using  system(). If the command
			fails, the console should report a fallthrough.</para>
			<para>Result of execution is returned in the <variable>SYSTEMSTATUS</variable> channel variable:</para>
			<variablelist>
				<variable name="SYSTEMSTATUS">
					<value name="FAILURE">
						Could not execute the specified command.
					</value>
					<value name="SUCCESS">
						Specified command successfully executed.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="TrySystem" language="en_US">
		<synopsis>
			Try executing a system command.
		</synopsis>
		<syntax>
			<parameter name="command" required="true">
				<para>Command to execute</para>
			</parameter>
		</syntax>
		<description>
			<para>Executes a command  by  using  system().</para>
			<para>Result of execution is returned in the <variable>SYSTEMSTATUS</variable> channel variable:</para>
			<variablelist>
				<variable name="SYSTEMSTATUS">
					<value name="FAILURE">
						Could not execute the specified command.
					</value>
					<value name="SUCCESS">
						Specified command successfully executed.
					</value>
					<value name="APPERROR">
						Specified command successfully executed, but returned error code.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>

 ***/

AST_THREADSTORAGE(buf_buf);

static char *app = "System";

static char *app2 = "TrySystem";

static char *chanvar = "SYSTEMSTATUS";

static int system_exec_helper(struct ast_channel *chan, const char *data, int failmode)
{
	int res = 0;
	struct ast_str *buf = ast_str_thread_get(&buf_buf, 16);
	char *cbuf;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "System requires an argument(command)\n");
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		return failmode;
	}

	ast_autoservice_start(chan);

	/* Do our thing here */
	ast_str_get_encoded_str(&buf, 0, (char *) data);
	cbuf = ast_str_buffer(buf);

	if (strchr("\"'", cbuf[0]) && cbuf[ast_str_strlen(buf) - 1] == cbuf[0]) {
		cbuf[ast_str_strlen(buf) - 1] = '\0';
		cbuf++;
		ast_log(LOG_NOTICE, "It is not necessary to quote the argument to the System application.\n");
	}

	res = ast_safe_system(cbuf);

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

static int system_exec(struct ast_channel *chan, const char *data)
{
	return system_exec_helper(chan, data, -1);
}

static int trysystem_exec(struct ast_channel *chan, const char *data)
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

	res = ast_register_application_xml(app2, trysystem_exec);
	res |= ast_register_application_xml(app, system_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Generic System() application");
