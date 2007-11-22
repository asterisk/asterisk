/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 Dave Chappell
 *
 * David Chappell <David.Chappell@trincoll.edu>
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
 * \brief Trivial application to read an extension into a variable
 *
 * \author David Chappell <David.Chappell@trincoll.edu>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"
#include "asterisk/channel.h"

enum {
	OPT_SKIP = (1 << 0),
	OPT_INDICATION = (1 << 1),
	OPT_NOANSWER = (1 << 2),
} readexten_option_flags;

AST_APP_OPTIONS(readexten_app_options, {
	AST_APP_OPTION('s', OPT_SKIP),
	AST_APP_OPTION('i', OPT_INDICATION),
	AST_APP_OPTION('n', OPT_NOANSWER),
});

static char *app = "ReadExten";

static char *synopsis = "Read an extension into a variable";

static char *descrip = 
"  ReadExten(<variable>[,[<filename>][,[<context>][,[<option>][,<timeout>]]]])\n\n"
"Reads a #-terminated string of digits from the user into the given variable.\n"
"  filename  file to play before reading digits or tone with option i\n"
"  context   context in which to match extensions\n"
"  option    options are:\n"
"              s - Return immediately if the channel is not answered,\n"
"              i - Play filename as an indication tone from your\n"
"                  indications.conf\n"
"              n - Read digits even if the channel is not answered.\n"
"  timeout   An integer number of seconds to wait for a digit response. If\n"
"            greater than 0, that value will override the default timeout.\n\n"
"ReadExten will set READEXTENSTATUS on exit with one of the following statuses:\n"
"  OK        A valid extension exists in ${variable}\n"
"  TIMEOUT   No extension was entered in the specified time\n"
"  INVALID   An invalid extension, ${INVALID_EXTEN}, was entered\n"
"  SKIP      Line was not up and the option 's' was specified\n"
"  ERROR     Invalid arguments were passed\n";


static int readexten_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char exten[256] = "";
	int maxdigits = sizeof(exten) - 1;
	int timeout = 0, digit_timeout = 0, x = 0;
	char *argcopy = NULL, *status = "";
	struct ind_tone_zone_sound *ts = NULL;
	struct ast_flags flags = {0};

	 AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(variable);
		AST_APP_ARG(filename);
		AST_APP_ARG(context);
		AST_APP_ARG(options);
		AST_APP_ARG(timeout);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ReadExten requires at least one argument\n");
		pbx_builtin_setvar_helper(chan, "READEXTENSTATUS", "ERROR");
		return 0;
	}

	argcopy = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(arglist, argcopy);

	if (ast_strlen_zero(arglist.variable)) {
		ast_log(LOG_WARNING, "Invalid! Usage: ReadExten(variable[|filename][|context][|options][|timeout])\n\n");
		pbx_builtin_setvar_helper(chan, "READEXTENSTATUS", "ERROR");
		return 0;
	}

	if (ast_strlen_zero(arglist.filename))
		arglist.filename = NULL;

	if (ast_strlen_zero(arglist.context))
		arglist.context = chan->context;

	if (!ast_strlen_zero(arglist.options))
		ast_app_parse_options(readexten_app_options, &flags, NULL, arglist.options);

	if (!ast_strlen_zero(arglist.timeout)) {
		timeout = atoi(arglist.timeout);
		if (timeout > 0)
			timeout *= 1000;
	}

	if (timeout <= 0)
		timeout = chan->pbx ? chan->pbx->rtimeout * 1000 : 10000;

	if (digit_timeout <= 0)
		digit_timeout = chan->pbx ? chan->pbx->dtimeout * 1000 : 5000;

	if (ast_test_flag(&flags, OPT_INDICATION) && !ast_strlen_zero(arglist.filename))
		ts = ast_get_indication_tone(chan->zone, arglist.filename);

	do {
		if (chan->_state != AST_STATE_UP) {
			if (ast_test_flag(&flags, OPT_SKIP)) {
				/* At the user's option, skip if the line is not up */
				pbx_builtin_setvar_helper(chan, arglist.variable, "");
				status = "SKIP";
				break;
			} else if (!ast_test_flag(&flags, OPT_NOANSWER)) {
				/* Otherwise answer unless we're supposed to read while on-hook */
				res = ast_answer(chan);
			}
		}

		if (res < 0) {
			status = "HANGUP";
			break;
		}

		ast_playtones_stop(chan);
		ast_stopstream(chan);

		if (ts && ts->data[0])
			res = ast_playtones_start(chan, 0, ts->data, 0);
		else if (arglist.filename)
			res = ast_streamfile(chan, arglist.filename, chan->language);

		for (x = 0; x < maxdigits; x++) {
			ast_debug(3, "extension so far: '%s', timeout: %d\n", exten, timeout);
			res = ast_waitfordigit(chan, timeout);

			ast_playtones_stop(chan);
			ast_stopstream(chan);
			timeout = digit_timeout;

			if (res < 1) {		/* timeout expired or hangup */
				if (ast_check_hangup(chan))
					status = "HANGUP";
				else
					status = "TIMEOUT";
				break;
			} else if (res == '#') {
				break;
			}

			exten[x] = res;
			if (!ast_matchmore_extension(chan, arglist.context, exten, 1 /* priority */, chan->cid.cid_num)) {
				break;
			}
		}

		if (!ast_strlen_zero(status))
			break;

		if (ast_exists_extension(chan, arglist.context, exten, 1, chan->cid.cid_num)) {
			ast_debug(3, "User entered valid extension '%s'\n", exten);
			pbx_builtin_setvar_helper(chan, arglist.variable, exten);
			status = "OK";
		} else {
			ast_debug(3, "User dialed invalid extension '%s' in context '%s' on %s\n", exten, arglist.context, chan->name);
			pbx_builtin_setvar_helper(chan, "INVALID_EXTEN", exten);
			status = "INVALID";
		}
	} while (0);

	pbx_builtin_setvar_helper(chan, "READEXTENSTATUS", status);

	return status[0] == 'H' ? -1 : 0;
}

static int acf_isexten_exec(struct ast_channel *chan, const char *cmd, char *parse, char *buffer, size_t buflen)
{
	int priority_int;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(context);
		AST_APP_ARG(extension);
		AST_APP_ARG(priority);
	);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.context))
		args.context = chan->context;

	if (ast_strlen_zero(args.extension)) {
		ast_log(LOG_WARNING, "Syntax: VALID_EXTEN([<context>],<extension>[,<priority>]) - missing argument <extension>!\n");
		return -1;
	}

	if (ast_strlen_zero(args.priority))
		priority_int = 1;
	else
		priority_int = atoi(args.priority);

	if (ast_exists_extension(chan, args.context, args.extension, priority_int, chan->cid.cid_num))
	    ast_copy_string(buffer, "1", buflen);
	else
	    ast_copy_string(buffer, "0", buflen);

	return 0;
}

static struct ast_custom_function acf_isexten = {
	.name = "VALID_EXTEN",
	.synopsis = "Determine whether an extension exists or not",
	.syntax = "VALID_EXTEN([<context>],<extension>[,<priority>])",
	.desc =
"Returns a true value if the indicated context, extension, and priority exist.\n"
"Context defaults to the current context, priority defaults to 1.\n",
	.read = acf_isexten_exec,
};

static int unload_module(void)
{
	int res = ast_unregister_application(app);
	res |= ast_custom_function_unregister(&acf_isexten);

	return res;	
}

static int load_module(void)
{
	int res = ast_register_application(app, readexten_exec, synopsis, descrip);
	res |= ast_custom_function_register(&acf_isexten);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Read and evaluate extension validity");
