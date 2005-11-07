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
 * \brief Trivial application to playback a sound file
 * 
 * \ingroup applications
 */
 
#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/app.h"

static char *tdesc = "Sound File Playback Application";

static char *app = "Playback";

static char *synopsis = "Play a file";

static char *descrip = 
"  Playback(filename[&filename2...][|option]):  Plays back given filenames (do not put\n"
"extension). Options may also be included following a pipe symbol. The 'skip'\n"
"option causes the playback of the message to be skipped if the channel\n"
"is not in the 'up' state (i.e. it hasn't been  answered  yet). If 'skip' is \n"
"specified, the application will return immediately should the channel not be\n"
"off hook.  Otherwise, unless 'noanswer' is specified, the channel will\n"
"be answered before the sound is played. Not all channels support playing\n"
"messages while still on hook. If 'j' is specified, the application\n"
"will jump to priority n+101 if present when a file specified to be played\n"
"does not exist.\n"
"This application sets the following channel variable upon completion:\n"
" PLAYBACKSTATUS    The status of the playback attempt as a text string, one of\n"
"               SUCCESS | FAILED\n"
;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int playback_exec(struct ast_channel *chan, void *data)
{
	int res = 0, mres = 0;
	struct localuser *u;
	char *tmp = NULL;
	int option_skip=0;
	int option_noanswer = 0;
	char *front = NULL, *back = NULL;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filenames);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Playback requires an argument (filename)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	tmp = ast_strdupa(data);
	if (!tmp) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;	
	}

	AST_STANDARD_APP_ARGS(args, tmp);

	if (args.options) {
		if (!strcasestr(args.options, "skip"))
			option_skip = 1;
		if (!strcasestr(args.options, "noanswer"))
			option_noanswer = 1;
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}
	
	if (chan->_state != AST_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
			LOCAL_USER_REMOVE(u);
			return 0;
		} else if (!option_noanswer)
			/* Otherwise answer unless we're supposed to send this while on-hook */
			res = ast_answer(chan);
	}
	if (!res) {
		ast_stopstream(chan);
		front = tmp;
		while (!res && front) {
			if ((back = strchr(front, '&'))) {
				*back = '\0';
				back++;
			}
			res = ast_streamfile(chan, front, chan->language);
			if (!res) { 
				res = ast_waitstream(chan, "");	
				ast_stopstream(chan);
			} else {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", chan->name, (char *)data);
				if (priority_jump || option_priority_jumping)
					ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
				res = 0;
				mres = 1;
			}
			front = back;
		}
		if (mres)
			pbx_builtin_setvar_helper(chan, "PLAYBACKSTATUS", "FAILED");
		else
			pbx_builtin_setvar_helper(chan, "PLAYBACKSTATUS", "SUCCESS");
	}
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
	return ast_register_application(app, playback_exec, synopsis, descrip);
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
