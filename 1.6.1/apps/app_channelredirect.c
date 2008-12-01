/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Sergey Basmanov
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
 * \brief ChannelRedirect application
 *
 * \author Sergey Basmanov <sergey_basmanov@mail.ru>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/features.h"

static char *app = "ChannelRedirect";
static char *synopsis = "Redirects given channel to a dialplan target.";
static char *descrip =
"ChannelRedirect(channel,[[context,]extension,]priority)\n"
"  Sends the specified channel to the specified extension priority\n"
"This application sets the following channel variables upon completion:\n"
"  CHANNELREDIRECT_STATUS - Are set to the result of the redirection\n"
"                           either NOCHANNEL or SUCCESS\n";

static int asyncgoto_exec(struct ast_channel *chan, void *data)
{
	int res = -1;
	char *info;
	struct ast_channel *chan2 = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(label);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (channel,[[context,]exten,]priority)\n", app);
		return -1;
	}

	info = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, info);

	if (ast_strlen_zero(args.channel) || ast_strlen_zero(args.label)) {
		ast_log(LOG_WARNING, "%s requires an argument (channel,[[context,]exten,]priority)\n", app);
		return -1;
	}

	chan2 = ast_get_channel_by_name_locked(args.channel);
	if (!chan2) {
		ast_log(LOG_WARNING, "No such channel: %s\n", args.channel);
		pbx_builtin_setvar_helper(chan, "CHANNELREDIRECT_STATUS", "NOCHANNEL");
		return 0;
	}

	res = ast_async_parseable_goto(chan2, args.label);
	pbx_builtin_setvar_helper(chan, "CHANNELREDIRECT_STATUS", "SUCCESS");
	ast_channel_unlock(chan2);

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, asyncgoto_exec, synopsis, descrip) ?
		AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Redirects a given channel to a dialplan target");
