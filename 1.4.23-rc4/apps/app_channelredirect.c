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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/features.h"
#include "asterisk/options.h"

static char *app = "ChannelRedirect";
static char *synopsis = "Redirects given channel to a dialplan target.";
static char *descrip = 
"ChannelRedirect(channel|[[context|]extension|]priority):\n"
"  Sends the specified channel to the specified extension priority\n";


static int asyncgoto_exec(struct ast_channel *chan, void *data)
{
	int res = -1;
	struct ast_module_user *u;
	char *info, *context, *exten, *priority;
	int prio = 1;
	struct ast_channel *chan2 = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(label);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (channel|[[context|]exten|]priority)\n", app);
		return -1;
	}

	u = ast_module_user_add(chan);

	info = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, info);

	if (ast_strlen_zero(args.channel) || ast_strlen_zero(args.label)) {
		ast_log(LOG_WARNING, "%s requires an argument (channel|[[context|]exten|]priority)\n", app);
		goto quit;
	}

	chan2 = ast_get_channel_by_name_locked(args.channel);
	if (!chan2) {
		ast_log(LOG_WARNING, "No such channel: %s\n", args.channel);
		goto quit;
	}

	/* Parsed right to left, so standard parsing won't work */
	context = strsep(&args.label, "|");
	exten = strsep(&args.label, "|");
	if (exten) {
		priority = strsep(&args.label, "|");
		if (!priority) {
			priority = exten;
			exten = context;
			context = NULL;
		}
	} else {
		priority = context;
		context = NULL;
	}

	/* ast_findlabel_extension does not convert numeric priorities; it only does a lookup */
	if (!(prio = atoi(priority)) && !(prio = ast_findlabel_extension(chan2, S_OR(context, chan2->context),
									S_OR(exten, chan2->exten), priority, chan2->cid.cid_num))) {
		ast_log(LOG_WARNING, "'%s' is not a known priority or label\n", priority);
		goto chanquit;
	}

	if (option_debug > 1)
		ast_log(LOG_DEBUG, "Attempting async goto (%s) to %s|%s|%d\n", args.channel, S_OR(context, chan2->context), S_OR(exten, chan2->exten), prio);

	if (ast_async_goto_if_exists(chan2, S_OR(context, chan2->context), S_OR(exten, chan2->exten), prio))
		ast_log(LOG_WARNING, "%s failed for %s\n", app, args.channel);
	else
		res = 0;

 chanquit:
	ast_mutex_unlock(&chan2->lock);
 quit:
	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	return ast_register_application(app, asyncgoto_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel Redirect");
