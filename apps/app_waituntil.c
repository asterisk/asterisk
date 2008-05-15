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

static char *app = "WaitUntil";
static char *synopsis = "Wait (sleep) until the current time is the given epoch";
static char *descrip =
"  WaitUntil(<epoch>): Waits until the given time.  Sets WAITUNTILSTATUS to\n"
"one of the following values:\n"
"  OK       Wait succeeded\n"
"  FAILURE  Invalid argument\n"
"  HANGUP   Channel hung up before time elapsed\n"
"  PAST     The time specified was already past\n";

static int waituntil_exec(struct ast_channel *chan, void *data)
{
	int res;
	double fraction;
	long seconds;
	struct timeval future = { 0, };
	struct timeval tv = ast_tvnow();
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

	if ((msec = ast_tvdiff_ms(future, tv)) < 0) {
		ast_log(LOG_NOTICE, "WaitUntil called in the past (now %ld, arg %ld)\n", (long)tv.tv_sec, (long)future.tv_sec);
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
	return ast_register_application(app, waituntil_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Wait until specified time");
