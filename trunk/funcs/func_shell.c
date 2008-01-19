/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
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
 * SHELL function to return the value of a system call.
 * 
 * \note Inspiration and Guidance from Russell! Thank You! 
 *
 * \author Brandon Kruse <bkruse@digium.com>
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static int shell_helper(struct ast_channel *chan, const char *cmd, char *data,
		                         char *buf, size_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Missing Argument!  Example:  Set(foo=${SHELL(echo \"bar\")})\n");
		return -1;
	}

	if (chan)
		ast_autoservice_start(chan);

	if (len >= 1) {
		FILE *ptr;
		char plbuff[4096];

		ptr = popen(data, "r");
		while (fgets(plbuff, sizeof(plbuff), ptr)) {
			strncat(buf, plbuff, len - strlen(buf) - 1);
		}
		pclose(ptr);
	}

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static struct ast_custom_function shell_function = {
	.name = "SHELL",
	.synopsis = "Executes a command as if you were at a shell.",
	.syntax = "SHELL(<command>)",
	.read = shell_helper,
	.desc =
"Returns the value from a system command\n"
"  Example:  Set(foo=${SHELL(echo \"bar\")})\n"
"  Note:  When using the SHELL() dialplan function, your \"SHELL\" is /bin/sh,\n"
"  which may differ as to the underlying shell, depending upon your production\n"
"  platform.  Also keep in mind that if you are using a common path, you should\n"
"  be mindful of race conditions that could result from two calls running\n"
"  SHELL() simultaneously.\n",
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&shell_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&shell_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Returns the output of a shell command");

