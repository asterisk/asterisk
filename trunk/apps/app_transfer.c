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
 * \brief Transfer a caller
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * Requires transfer support from channel driver
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"


static const char *app = "Transfer";

static const char *synopsis = "Transfer caller to remote extension";

static const char *descrip = 
"  Transfer([Tech/]dest[,options]):  Requests the remote caller be transferred\n"
"to a given destination. If TECH (SIP, IAX2, LOCAL etc) is used, only\n"
"an incoming call with the same channel technology will be transfered.\n"
"Note that for SIP, if you transfer before call is setup, a 302 redirect\n"
"SIP message will be returned to the caller.\n"
"\nThe result of the application will be reported in the TRANSFERSTATUS\n"
"channel variable:\n"
"       SUCCESS      Transfer succeeded\n"
"       FAILURE      Transfer failed\n"
"       UNSUPPORTED  Transfer unsupported by channel driver\n";

static int transfer_exec(struct ast_channel *chan, void *data)
{
	int res;
	int len;
	char *slash;
	char *tech = NULL;
	char *dest = NULL;
	char *status;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dest);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "Transfer requires an argument ([Tech/]destination[,options])\n");
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "FAILURE");
		return 0;
	} else
		parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.options) {
	}

	dest = args.dest;

	if ((slash = strchr(dest, '/')) && (len = (slash - dest))) {
		tech = dest;
		dest = slash + 1;
		/* Allow execution only if the Tech/destination agrees with the type of the channel */
		if (strncasecmp(chan->tech->type, tech, len)) {
			pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "FAILURE");
			return 0;
		}
	}

	/* Check if the channel supports transfer before we try it */
	if (!chan->tech->transfer) {
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "UNSUPPORTED");
		return 0;
	}

	res = ast_transfer(chan, dest);

	if (res < 0) {
		status = "FAILURE";
		res = 0;
	} else {
		status = "SUCCESS";
		res = 0;
	}

	pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", status);

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, transfer_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Transfers a caller to another extension");
