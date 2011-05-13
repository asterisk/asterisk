/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Joshua Colp
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Directed Call Pickup Support
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

#define PICKUPMARK "PICKUPMARK"

static const char *app = "Pickup";
static const char *synopsis = "Directed Call Pickup";
static const char *descrip =
"  Pickup(extension[@context][&extension2@context...]): This application can pickup any ringing channel\n"
"that is calling the specified extension. If no context is specified, the current\n"
"context will be used. If you use the special string \"PICKUPMARK\" for the context parameter, for example\n"
"10@PICKUPMARK, this application tries to find a channel which has defined a channel variable with the same content\n"
"as \"extension\".";

/*!
 * \internal
 * \brief Perform actual pickup between two channels.
 * \note Must remain in sync with same function in res/res_features.c.
 */
static int pickup_do(struct ast_channel *chan, struct ast_channel *target)
{
	if (option_debug)
		ast_log(LOG_DEBUG, "Call pickup on '%s' by '%s'\n", target->name, chan->name);

	if (ast_answer(chan)) {
		ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan->name);
		return -1;
	}

	if (ast_queue_control(chan, AST_CONTROL_ANSWER)) {
		ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan->name);
		return -1;
	}

	if (ast_channel_masquerade(target, chan)) {
		ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan->name, target->name);
		return -1;
	}

	return 0;
}

/* Helper function that determines whether a channel is capable of being picked up */
static int can_pickup(struct ast_channel *chan)
{
	if (!chan->pbx && !chan->masq &&
		!ast_test_flag(chan, AST_FLAG_ZOMBIE) &&
		(chan->_state == AST_STATE_RINGING ||
		 chan->_state == AST_STATE_RING ||
		 chan->_state == AST_STATE_DOWN)) {
		return 1;
	}
	return 0;
}

/* Attempt to pick up specified extension with context */
static int pickup_by_exten(struct ast_channel *chan, char *exten, char *context)
{
	int res = -1;
	struct ast_channel *target = NULL;

	while ((target = ast_channel_walk_locked(target))) {
		if ((!strcasecmp(target->macroexten, exten) || !strcasecmp(target->exten, exten)) &&
		    !strcasecmp(target->dialcontext, context) &&
		    (chan != target) && can_pickup(target)) {
			res = pickup_do(chan, target);
			ast_channel_unlock(target);
			break;
		}
		ast_channel_unlock(target);
	}

	return res;
}

/* Attempt to pick up specified mark */
static int pickup_by_mark(struct ast_channel *chan, char *mark)
{
	int res = -1;
	const char *tmp = NULL;
	struct ast_channel *target = NULL;

	while ((target = ast_channel_walk_locked(target))) {
		if ((tmp = pbx_builtin_getvar_helper(target, PICKUPMARK)) &&
		    !strcasecmp(tmp, mark) &&
		    can_pickup(target)) {
			res = pickup_do(chan, target);
			ast_channel_unlock(target);
			break;
		}
		ast_channel_unlock(target);
	}

	return res;
}

/* Main application entry point */
static int pickup_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u = NULL;
	char *tmp = ast_strdupa(data);
	char *exten = NULL, *context = NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Pickup requires an argument (extension)!\n");
		return -1;	
	}

	u = ast_module_user_add(chan);
	
	/* Parse extension (and context if there) */
	while (!ast_strlen_zero(tmp) && (exten = strsep(&tmp, "&"))) {
		if ((context = strchr(exten, '@')))
			*context++ = '\0';
		if (context && !strcasecmp(context, PICKUPMARK)) {
			if (!pickup_by_mark(chan, exten))
				break;
		} else {
			if (!pickup_by_exten(chan, exten, context ? context : chan->context))
				break;
		}
		ast_log(LOG_NOTICE, "No target channel found for %s.\n", exten);
	}

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	
	return res;
}

static int load_module(void)
{
	return ast_register_application(app, pickup_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Directed Call Pickup Application");
