/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Redfish Solutions
 *
 * Philip Prindeville <philipp@redfish-solutions.com>
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
 * \brief Sleep until the given epoch
 *
 * \author Philip Prindeville <philipp@redfish-solutions.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"

/*** DOCUMENTATION
	<application name="WaitUntil" language="en_US">
		<synopsis>
			Wait (sleep) until the current time is the given epoch.
		</synopsis>
		<syntax>
			<parameter name="epoch" required="true" />
		</syntax>
		<description>
			<para>Waits until the given <replaceable>epoch</replaceable>.</para>
			<para>Sets <variable>WAITUNTILSTATUS</variable> to one of the following values:</para>
			<variablelist>
				<variable name="WAITUNTILSTATUS">
					<value name="OK">
						Wait succeeded.
					</value>
					<value name="FAILURE">
						Invalid argument.
					</value>
					<value name="HANGUP">
						Channel hungup before time elapsed.
					</value>
					<value name="PAST">
						Time specified had already past.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static char *app = "WaitUntil";

static int waituntil_exec(struct ast_channel *chan, const char *data)
{
	int res;
	double fraction;
	long seconds;
	struct timeval future = { 0, };
	struct timeval now = ast_tvnow();
	int msec;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "WaitUntil requires an argument(epoch)\n");
		pbx_builtin_setvar_helper(chan, "WAITUNTILSTATUS", "FAILURE");
		return 0;
	}

	if (sscanf(data, "%ld%lf", &seconds, &fraction) == 0) {
		ast_log(LOG_WARNING, "WaitUntil called with non-numeric argument\n");
		pbx_builtin_setvar_helper(chan, "WAITUNTILSTATUS", "FAILURE");
		return 0;
	}

	future.tv_sec = seconds;
	future.tv_usec = fraction * 1000000;

	if ((msec = ast_tvdiff_ms(future, now)) < 0) {
		ast_log(LOG_NOTICE, "WaitUntil called in the past (now %ld, arg %ld)\n", (long)now.tv_sec, (long)future.tv_sec);
		pbx_builtin_setvar_helper(chan, "WAITUNTILSTATUS", "PAST");
		return 0;
	}

	if ((res = ast_safe_sleep(chan, msec)))
		pbx_builtin_setvar_helper(chan, "WAITUNTILSTATUS", "HANGUP");
	else
		pbx_builtin_setvar_helper(chan, "WAITUNTILSTATUS", "OK");

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, waituntil_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Wait until specified time");
