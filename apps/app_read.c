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
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"

static char *tdesc = "Read Variable Application";

static char *app = "Read";

static char *synopsis = "Read a variable";

static char *descrip = 
"  Read(variable[|filename][|maxdigits][|option][|attempts][|timeout])\n\n"
"Reads a #-terminated string of digits a certain number of times from the\n"
"user in to the given variable.\n"
"  filename   -- file to play before reading digits.\n"
"  maxdigits  -- maximum acceptable number of digits. Stops reading after\n"
"                maxdigits have been entered (without requiring the user to\n"
"                press the '#' key).\n"
"                Defaults to 0 - no limit - wait for the user press the '#' key.\n"
"                Any value below 0 means the same. Max accepted value is 255.\n"
"  option     -- may be 'skip' to return immediately if the line is not up,\n"
"                or 'noanswer' to read digits even if the line is not up.\n"
"  attempts   -- if greater than 1, that many attempts will be made in the \n"
"                event no data is entered.\n"
"  timeout    -- if greater than 0, that value will override the default timeout.\n\n"
"Returns -1 on hangup or error and 0 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define ast_next_data(instr,ptr,delim) if((ptr=strchr(instr,delim))) { *(ptr) = '\0' ; ptr++;}

static int read_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	char *timeout = NULL;
	char *varname = NULL;
	char *filename = NULL;
	char *loops;
	char *maxdigitstr=NULL;
	char *options=NULL;
	int option_skip = 0;
	int option_noanswer = 0;
	int maxdigits=255;
	int tries = 1;
	int to = 0;
	int x = 0;
	char *argcopy = NULL;
	char *args[8];

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Read requires an argument (variable)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);
	
	argcopy = ast_strdupa(data);
	if (!argcopy) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (ast_separate_app_args(argcopy, '|', args, sizeof(args) / sizeof(args[0])) < 1) {
		ast_log(LOG_WARNING, "Cannot Parse Arguments.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	varname = args[x++];
	filename = args[x++];
	maxdigitstr = args[x++];
	options = args[x++];
	loops = args[x++];
	timeout = args[x++];
	
	if (options) { 
		if (!strcasecmp(options, "skip"))
			option_skip = 1;
		else if (!strcasecmp(options, "noanswer"))
			option_noanswer = 1;
		else {
			if (strchr(options, 's'))
				option_skip = 1;
			if (strchr(options, 'n'))
				option_noanswer = 1;
		}
	}

	if(loops) {
		tries = atoi(loops);
		if(tries <= 0)
			tries = 1;
	}

	if(timeout) {
		to = atoi(timeout);
		if (to <= 0)
			to = 0;
		else
			to *= 1000;
	}

	if (ast_strlen_zero(filename)) 
		filename = NULL;
	if (maxdigitstr) {
		maxdigits = atoi(maxdigitstr);
		if ((maxdigits<1) || (maxdigits>255)) {
    			maxdigits = 255;
		} else if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Accepting a maximum of %d digits.\n", maxdigits);
	}
	if (ast_strlen_zero(varname)) {
		ast_log(LOG_WARNING, "Invalid! Usage: Read(variable[|filename][|maxdigits][|option][|attempts][|timeout])\n\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	
	if (chan->_state != AST_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
			pbx_builtin_setvar_helper(chan, varname, "\0");
			LOCAL_USER_REMOVE(u);
			return 0;
		} else if (!option_noanswer) {
			/* Otherwise answer unless we're supposed to read while on-hook */
			res = ast_answer(chan);
		}
	}
	if (!res) {
		while(tries && !res) {
			ast_stopstream(chan);
			res = ast_app_getdata(chan, filename, tmp, maxdigits, to);
			if (res > -1) {
				pbx_builtin_setvar_helper(chan, varname, tmp);
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
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "User disconnected\n");
			}
		}
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
	return ast_register_application(app, read_exec, synopsis, descrip);
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
