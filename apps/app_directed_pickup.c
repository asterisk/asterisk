/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Joshua Colp
 *
 * Joshua Colp <jcolp@asterlink.com>
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
 */

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

static const char *tdesc = "Directed Call Pickup Application";
static const char *app = "Pickup";
static const char *synopsis = "Directed Call Pickup application.";
static const char *descrip =
" Pickup(extension@context):\n"
"Steals any calls to a specified extension that are in a ringing state and bridges them to the current channel. Context is an optional argument.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int pickup_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u = NULL;
	struct ast_channel *origin = NULL, *target = NULL;
	char *tmp = NULL, *exten = NULL, *context = NULL;
	char workspace[256] = "";

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Pickup requires an argument (extension) !\n");
		return -1;	
	}

	LOCAL_USER_ADD(u);
	
	/* Get the extension and context if present */
	exten = data;
	context = strchr(data, '@');
	if (context) {
		*context = '\0';
		context++;
	}

	/* Find a channel to pickup */
	origin = ast_get_channel_by_exten_locked(exten, context);
	if (origin) {
		ast_cdr_getvar(origin->cdr, "dstchannel", &tmp, workspace,
			       sizeof(workspace), 0);
		if (tmp) {
			/* We have a possible channel... now we need to find it! */
			target = ast_get_channel_by_name_locked(tmp);
		} else {
			ast_log(LOG_DEBUG, "No target channel found.\n");
			res = -1;
		}
		ast_mutex_unlock(&origin->lock);
	} else {
		ast_log(LOG_DEBUG, "No originating channel found.\n");
	}
	
	if (res)
		goto out;

	if (target && (!target->pbx) && ((target->_state == AST_STATE_RINGING) || (target->_state == AST_STATE_RING))) {
		ast_log(LOG_DEBUG, "Call pickup on chan '%s' by '%s'\n", target->name,
			chan->name);
		res = ast_answer(chan);
		if (res) {
			ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan->name);
			res = -1;
			goto out;
		}
		res = ast_queue_control(chan, AST_CONTROL_ANSWER);
		if (res) {
			ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n",
				chan->name);
			res = -1;
			goto out;
		}
		res = ast_channel_masquerade(target, chan);
		if (res) {
			ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan->name, target->name);
			res = -1;
			goto out;
		}
	} else {
		ast_log(LOG_DEBUG, "No call pickup possible...\n");
		res = -1;
	}
	/* Done */
 out:
	if (target) 
		ast_mutex_unlock(&target->lock);
	
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
	return ast_register_application(app, pickup_exec, synopsis, descrip);
}

char *description(void)
{
	return (char *) tdesc;
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
