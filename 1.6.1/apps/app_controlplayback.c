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
 * \brief Trivial application to control playback of a sound file
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"

static const char *app = "ControlPlayback";

static const char *synopsis = "Play a file with fast forward and rewind";

static const char *descrip =
"  ControlPlayback(file[,skipms[,ff[,rew[,stop[,pause[,restart,options]]]]]]]):\n"
"This application will play back the given filename. By default, the '*' key\n"
"can be used to rewind, and the '#' key can be used to fast-forward.\n"
"Parameters:\n"
"  skipms  - This is number of milliseconds to skip when rewinding or\n"
"            fast-forwarding.\n"
"  ff      - Fast-forward when this DTMF digit is received.\n"
"  rew     - Rewind when this DTMF digit is received.\n"
"  stop    - Stop playback when this DTMF digit is received.\n"
"  pause   - Pause playback when this DTMF digit is received.\n"
"  restart - Restart playback when this DTMF digit is received.\n"
"Options:\n"
"  o(#) - Start at # ms from the beginning of the file.\n"
"This application sets the following channel variables upon completion:\n"
"  CPLAYBACKSTATUS -  This variable contains the status of the attempt as a text\n"
"                     string, one of: SUCCESS | USERSTOPPED | ERROR\n"
"  CPLAYBACKOFFSET -  This contains the offset in ms into the file where\n"
"                     playback was at when it stopped.  -1 is end of file.\n"
"  CPLAYBACKSTOPKEY - If the playback is stopped by the user this variable contains\n"
"                     the key that was pressed.\n";

enum {
	OPT_OFFSET = (1 << 1),
};

enum {
	OPT_ARG_OFFSET = 0,
	/* must stay as the last entry ... */
	OPT_ARG_ARRAY_LEN,
};

AST_APP_OPTIONS(cpb_opts, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('o', OPT_OFFSET, OPT_ARG_OFFSET),
	END_OPTIONS
);

static int is_on_phonepad(char key)
{
	return key == 35 || key == 42 || (key >= 48 && key <= 57);
}

static int is_argument(const char *haystack, int needle)
{
	if (ast_strlen_zero(haystack))
		return 0;

	if (strchr(haystack, needle))
		return -1;

	return 0;
}

static int controlplayback_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int skipms = 0;
	long offsetms = 0;
	char offsetbuf[20];
	char stopkeybuf[2];
	char *tmp;
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_LEN];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(skip);
		AST_APP_ARG(fwd);
		AST_APP_ARG(rev);
		AST_APP_ARG(stop);
		AST_APP_ARG(pause);
		AST_APP_ARG(restart);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ControlPlayback requires an argument (filename)\n");
		return -1;
	}
	
	tmp = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, tmp);

	if (args.argc < 1) {
		ast_log(LOG_WARNING, "ControlPlayback requires an argument (filename)\n");
		return -1;
	}

	skipms = args.skip ? (atoi(args.skip) ? atoi(args.skip) : 3000) : 3000;

	if (!args.fwd || !is_on_phonepad(*args.fwd)) {
		char *digit = "#";
		if (!is_argument(args.rev, *digit) && !is_argument(args.stop, *digit) && !is_argument(args.pause, *digit) && !is_argument(args.restart, *digit))
			args.fwd = digit;
		else
			args.fwd = NULL;
	}
	if (!args.rev || !is_on_phonepad(*args.rev)) {
		char *digit = "*";
		if (!is_argument(args.fwd, *digit) && !is_argument(args.stop, *digit) && !is_argument(args.pause, *digit) && !is_argument(args.restart, *digit))
			args.rev = digit;
		else
			args.rev = NULL;
	}
	ast_log(LOG_WARNING, "args.fwd = %s, args.rew = %s\n", args.fwd, args.rev);
	if (args.stop && !is_on_phonepad(*args.stop))
		args.stop = NULL;
	if (args.pause && !is_on_phonepad(*args.pause))
		args.pause = NULL;
	if (args.restart && !is_on_phonepad(*args.restart))
		args.restart = NULL;

	if (args.options) {
		ast_app_parse_options(cpb_opts, &opts, opt_args, args.options);
		if (ast_test_flag(&opts, OPT_OFFSET))
			offsetms = atol(opt_args[OPT_ARG_OFFSET]);
	}

	res = ast_control_streamfile(chan, args.filename, args.fwd, args.rev, args.stop, args.pause, args.restart, skipms, &offsetms);

	/* If we stopped on one of our stop keys, return 0  */
	if (res > 0 && args.stop && strchr(args.stop, res)) {
		pbx_builtin_setvar_helper(chan, "CPLAYBACKSTATUS", "USERSTOPPED");
		snprintf(stopkeybuf, sizeof(stopkeybuf), "%c", res);
		pbx_builtin_setvar_helper(chan, "CPLAYBACKSTOPKEY", stopkeybuf);
		res = 0;
	} else {
		if (res < 0) {
			res = 0;
			pbx_builtin_setvar_helper(chan, "CPLAYBACKSTATUS", "ERROR");
		} else
			pbx_builtin_setvar_helper(chan, "CPLAYBACKSTATUS", "SUCCESS");
	}

	snprintf(offsetbuf, sizeof(offsetbuf), "%ld", offsetms);
	pbx_builtin_setvar_helper(chan, "CPLAYBACKOFFSET", offsetbuf);

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
	return ast_register_application(app, controlplayback_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Control Playback Application");
