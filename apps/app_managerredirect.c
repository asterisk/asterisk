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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/features.h"

static char *tdesc = "Channel Redirect";
static char *app = "ChannelRedirect";
static char *synopsis = "Redirects given channel to a dialplan target.";
static char *descrip = 
"ChannelRedirect(channel|[[context|]extension|]priority):\n"
"  Sends the specified channel to the specified extension priority\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int asyncgoto_exec(struct ast_channel *chan, void *data)
{
	int res = -1;
	struct localuser *u;
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

	LOCAL_USER_ADD(u);

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

	if (!(prio = ast_findlabel_extension(chan2, S_OR(context, chan2->context), S_OR(exten, chan2->exten),
					     priority, chan2->cid.cid_num))) {
		ast_log(LOG_WARNING, "'%s' is not a known priority or label\n", priority);
		goto chanquit;
	}

	ast_log(LOG_DEBUG, "Attempting async goto (%s) to %s\n", args.channel, args.label);

	if (ast_async_goto_if_exists(chan2, context ? context : chan2->context, exten ? exten : chan2->exten, prio))
		ast_log(LOG_WARNING, "%s failed for %s\n", app, args.channel);
	else
		res = 0;

 chanquit:
	ast_mutex_unlock(&chan2->lock);
 quit:
	LOCAL_USER_REMOVE(u);

	return res;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

int load_module(void)
{
	return ast_register_application(app, asyncgoto_exec, synopsis, descrip);
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
