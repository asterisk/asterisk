/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to read a variable
 * 
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/app.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Read Variable Application";

static char *app = "Read";

static char *synopsis = "Read a variable";

static char *descrip = 
"  Read(variable[|filename][|maxdigits][|option])\n\n"
"Reads a #-terminated string of digits from the user in to the given variable,\n"
"optionally playing a given filename first.\n"
"  maxdigits  -- maximum acceptable number of digits. Stops reading after\n"
"                maxdigits have been entered (without requiring the user to\n"
"                press the '#' key).\n"
"                Defaults to 0 - no limit - wait for the user press the '#' key.\n"
"                Any value below 0 means the same. Max accepted value is 255.\n"
"  option     -- may be 'skip' to return immediately if the line is not up,\n"
"                or 'noanswer' to read digits even if the line is not up.\n\n"
"Returns -1 on hangup or error and 0 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int read_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	char argdata[256] = "";
	char *varname;
	char *filename;
	char *stringp;
	char *maxdigitstr;
	char *options;
	int option_skip = 0;
	int option_noanswer = 0;
	int maxdigits=255;
	if (!data || ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "Read requires an argument (variable)\n");
		return -1;
	}
	strncpy(argdata, (char *)data, sizeof(argdata)-1);
	stringp=argdata;
	varname = strsep(&stringp, "|");
	filename = strsep(&stringp, "|");
	maxdigitstr = strsep(&stringp,"|");
	options = strsep(&stringp, "|");
	if (options && !strcasecmp(options, "skip"))
		option_skip = 1;
	if (options && !strcasecmp(options, "noanswer"))
		option_noanswer = 1;
	if (!(filename) || ast_strlen_zero(filename)) 
		filename = NULL;
	if (maxdigitstr) {
	    maxdigits = atoi(maxdigitstr);
	    if ((maxdigits<1) || (maxdigits>255)) {
    		maxdigits = 255;
	    } else
		ast_verbose(VERBOSE_PREFIX_3 "Accepting a maximum of %i digits.\n", maxdigits);
	}	
	if (!(varname) || ast_strlen_zero(varname)) {
		ast_log(LOG_WARNING, "Read requires an variable name\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
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
		ast_stopstream(chan);
		res = ast_app_getdata(chan, filename, tmp, maxdigits, 0);
		if (res > -1) {
			pbx_builtin_setvar_helper(chan, varname, tmp);
			ast_verbose(VERBOSE_PREFIX_3 "User entered '%s'\n", tmp);
		} else {
			ast_verbose(VERBOSE_PREFIX_3 "User entered nothing\n");
		}
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
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
