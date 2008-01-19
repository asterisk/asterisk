/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Gary Cook
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
 * \brief Pickup a ringing channel
 *
 * \author Gary Cook
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

static const char *app = "PickupChan";
static const char *synopsis = "Pickup a ringing channel";
static const char *descrip =
"  PickupChan(channel[&channel...]): This application can pickup any ringing channel\n";

/*! \todo This application should return a result code, like PICKUPRESULT */

/*! \brief Helper function that determines whether a channel is capable of being picked up */
static int can_pickup(struct ast_channel *chan)
{
	ast_debug(3, "Checking Pickup '%s' state '%s ( %d )'\n", chan->name, ast_state2str(chan->_state), chan->_state);

	if (!chan->pbx && (chan->_state == AST_STATE_RINGING || chan->_state == AST_STATE_RING)) {
		return 1;
	} else {
		return 0;
	}
}

/*! \brief Helper Function to walk through ALL channels checking NAME and STATE */
static struct ast_channel *my_ast_get_channel_by_name_locked(char *channame)
{
	struct ast_channel *chan;
	char *chkchan = alloca(strlen(channame) + 2);

	/* need to append a '-' for the comparison so we check full channel name,
	 * i.e SIP/hgc- , use a temporary variable so original stays the same for
	 * debugging.
	 */
	strcpy(chkchan, channame);
	strcat(chkchan, "-");

	for (chan = ast_walk_channel_by_name_prefix_locked(NULL, channame, strlen(channame));
		 chan;
		 chan = ast_walk_channel_by_name_prefix_locked(chan, channame, strlen(channame))) {
		if (!strncasecmp(chan->name, chkchan, strlen(chkchan)) && can_pickup(chan))
			return chan;
		ast_channel_unlock(chan);
	}
	return NULL;
}

/*! \brief Perform actual pickup between two channels */
static int pickup_do(struct ast_channel *chan, struct ast_channel *target)
{
	int res = 0;

	ast_debug(3, "Call pickup on '%s' by '%s'\n", target->name, chan->name);

	if ((res = ast_answer(chan))) {
		ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan->name);
		return -1;
	}

	if ((res = ast_queue_control(chan, AST_CONTROL_ANSWER))) {
		ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan->name);
		return -1;
	}

	if ((res = ast_channel_masquerade(target, chan))) {
		ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan->name, target->name);
		return -1;
	}

	return res;
}

/*! \brief Attempt to pick up specified channel named , does not use context */
static int pickup_by_channel(struct ast_channel *chan, char *pickup)
{
	int res = 0;
	struct ast_channel *target;

	if (!(target = my_ast_get_channel_by_name_locked(pickup)))
		return -1;

	/* Just check that we are not picking up the SAME as target */
	if (chan->name != target->name && chan != target) {
		res = pickup_do(chan, target);
		ast_channel_unlock(target);
	}

	return res;
}

/*! \brief Main application entry point */
static int pickupchan_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u = NULL;
	char *tmp = ast_strdupa(data);
	char *pickup = NULL, *context = NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Pickup requires an argument (channel)!\n");
		return -1;	
	}

	u = ast_module_user_add(chan);

	/* Parse channel (and ignore context if there) */
	while (!ast_strlen_zero(tmp) && (pickup = strsep(&tmp, "&"))) {
		if ((context = strchr(pickup , '@'))) {
			*context++ = '\0';
		}
		if (!strncasecmp(chan->name, pickup , strlen(pickup))) {
			ast_log(LOG_NOTICE, "Cannot pickup your own channel %s.\n", pickup);
		} else {
			if (!pickup_by_channel(chan, pickup)) {
				break;
			}
			ast_log(LOG_NOTICE, "No target channel found for %s.\n", pickup);
		}
	}

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, pickupchan_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel Pickup Application");
