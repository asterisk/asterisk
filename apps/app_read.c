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
 * \brief Trivial application to read a variable
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/indications.h"

enum {
	OPT_SKIP = (1 << 0),
	OPT_INDICATION = (1 << 1),
	OPT_NOANSWER = (1 << 2),
} read_option_flags;

AST_APP_OPTIONS(read_app_options, {
	AST_APP_OPTION('s', OPT_SKIP),
	AST_APP_OPTION('i', OPT_INDICATION),
	AST_APP_OPTION('n', OPT_NOANSWER),
});

static char *app = "Read";

static char *synopsis = "Read a variable";

static char *descrip = 
"  Read(variable[|filename][|maxdigits][|option][|attempts][|timeout])\n\n"
"Reads a #-terminated string of digits a certain number of times from the\n"
"user in to the given variable.\n"
"  filename   -- file to play before reading digits or tone with option i\n"
"  maxdigits  -- maximum acceptable number of digits. Stops reading after\n"
"                maxdigits have been entered (without requiring the user to\n"
"                press the '#' key).\n"
"                Defaults to 0 - no limit - wait for the user press the '#' key.\n"
"                Any value below 0 means the same. Max accepted value is 255.\n"
"  option     -- options are 's' , 'i', 'n'\n"
"                's' to return immediately if the line is not up,\n"
"                'i' to play  filename as an indication tone from your indications.conf\n"
"                'n' to read digits even if the line is not up.\n"
"  attempts   -- if greater than 1, that many attempts will be made in the \n"
"                event no data is entered.\n"
"  timeout    -- An integer number of seconds to wait for a digit response. If greater\n"
"                than 0, that value will override the default timeout.\n\n"
"Read should disconnect if the function fails or errors out.\n";


#define ast_next_data(instr,ptr,delim) if((ptr=strchr(instr,delim))) { *(ptr) = '\0' ; ptr++;}

static int read_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	char tmp[256] = "";
	int maxdigits = 255;
	int tries = 1, to = 0, x = 0;
	char *argcopy = NULL;
	struct ind_tone_zone_sound *ts;
	struct ast_flags flags = {0};

	 AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(variable);
		AST_APP_ARG(filename);
		AST_APP_ARG(maxdigits);
		AST_APP_ARG(options);
		AST_APP_ARG(attempts);
		AST_APP_ARG(timeout);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Read requires an argument (variable)\n");
		return -1;
	}

	u = ast_module_user_add(chan);
	
	argcopy = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(arglist, argcopy);

	if (!ast_strlen_zero(arglist.options)) {
		ast_app_parse_options(read_app_options, &flags, NULL, arglist.options);
	}
	
	if (!ast_strlen_zero(arglist.attempts)) {
		tries = atoi(arglist.attempts);
		if (tries <= 0)
			tries = 1;
	}

	if (!ast_strlen_zero(arglist.timeout)) {
		to = atoi(arglist.timeout);
		if (to <= 0)
			to = 0;
		else
			to *= 1000;
	}

	if (ast_strlen_zero(arglist.filename)) {
		arglist.filename = NULL;
	}
	if (!ast_strlen_zero(arglist.maxdigits)) {
		maxdigits = atoi(arglist.maxdigits);
		if ((maxdigits<1) || (maxdigits>255)) {
    			maxdigits = 255;
		} else if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Accepting a maximum of %d digits.\n", maxdigits);
	}
	if (ast_strlen_zero(arglist.variable)) {
		ast_log(LOG_WARNING, "Invalid! Usage: Read(variable[|filename][|maxdigits][|option][|attempts][|timeout])\n\n");
		ast_module_user_remove(u);
		return -1;
	}
	ts=NULL;
	if (ast_test_flag(&flags,OPT_INDICATION)) {
		if (!ast_strlen_zero(arglist.filename)) {
			ts = ast_get_indication_tone(chan->zone,arglist.filename);
		}
	}
	if (chan->_state != AST_STATE_UP) {
		if (ast_test_flag(&flags,OPT_SKIP)) {
			/* At the user's option, skip if the line is not up */
			pbx_builtin_setvar_helper(chan, arglist.variable, "\0");
			ast_module_user_remove(u);
			return 0;
		} else if (!ast_test_flag(&flags,OPT_NOANSWER)) {
			/* Otherwise answer unless we're supposed to read while on-hook */
			res = ast_answer(chan);
		}
	}
	if (!res) {
		while (tries && !res) {
			ast_stopstream(chan);
			if (ts && ts->data[0]) {
				if (!to)
					to = chan->pbx ? chan->pbx->rtimeout * 1000 : 6000;
				res = ast_playtones_start(chan, 0, ts->data, 0);
				for (x = 0; x < maxdigits; ) {
					res = ast_waitfordigit(chan, to);
					ast_playtones_stop(chan);
					if (res < 1) {
						tmp[x]='\0';
						break;
					}
					tmp[x++] = res;
					if (tmp[x-1] == '#') {
						tmp[x-1] = '\0';
						break;
					}
				}
			} else {
				res = ast_app_getdata(chan, arglist.filename, tmp, maxdigits, to);
			}
			if (res > -1) {
				pbx_builtin_setvar_helper(chan, arglist.variable, tmp);
				if (!ast_strlen_zero(tmp)) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "User entered '%s'\n", tmp);
					tries = 0;
				} else {
					tries--;
					if (option_verbose > 2) {
						if (tries)
							ast_verbose(VERBOSE_PREFIX_3 "User entered nothing, %d chance%s left\n", tries, (tries != 1) ? "s" : "");
						else
							ast_verbose(VERBOSE_PREFIX_3 "User entered nothing.\n");
					}
				}
				res = 0;
			} else {
				pbx_builtin_setvar_helper(chan, arglist.variable, tmp);
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "User disconnected\n");
			}
		}
	}
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
	return ast_register_application(app, read_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Read Variable Application");
