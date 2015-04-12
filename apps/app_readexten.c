/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2008, Trinity College Computing Center
 * Written by David Chappell <David.Chappell@trincoll.edu>
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"
#include "asterisk/channel.h"

/*** DOCUMENTATION
	<application name="ReadExten" language="en_US">
		<synopsis>
			Read an extension into a variable.
		</synopsis>
		<syntax>
			<parameter name="variable" required="true" />
			<parameter name="filename">
				<para>File to play before reading digits or tone with option <literal>i</literal></para>
			</parameter>
			<parameter name="context">
				<para>Context in which to match extensions.</para>
			</parameter>
			<parameter name="option">
				<optionlist>
					<option name="s">
						<para>Return immediately if the channel is not answered.</para>
					</option>
					<option name="i">
						<para>Play <replaceable>filename</replaceable> as an indication tone from your
						<filename>indications.conf</filename> or a directly specified list of
						frequencies and durations.</para>
					</option>
					<option name="n">
						<para>Read digits even if the channel is not answered.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="timeout">
				<para>An integer number of seconds to wait for a digit response. If
				greater than <literal>0</literal>, that value will override the default timeout.</para>
			</parameter>
		</syntax>
		<description>
			<para>Reads a <literal>#</literal> terminated string of digits from the user into the given variable.</para>
			<para>Will set READEXTENSTATUS on exit with one of the following statuses:</para>
			<variablelist>
				<variable name="READEXTENSTATUS">
					<value name="OK">
						A valid extension exists in ${variable}.
					</value>
					<value name="TIMEOUT">
						No extension was entered in the specified time.  Also sets ${variable} to "t".
					</value>
					<value name="INVALID">
						An invalid extension, ${INVALID_EXTEN}, was entered.  Also sets ${variable} to "i".
					</value>
					<value name="SKIP">
						Line was not up and the option 's' was specified.
					</value>
					<value name="ERROR">
						Invalid arguments were passed.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

enum readexten_option_flags {
	OPT_SKIP = (1 << 0),
	OPT_INDICATION = (1 << 1),
	OPT_NOANSWER = (1 << 2),
};

AST_APP_OPTIONS(readexten_app_options, {
	AST_APP_OPTION('s', OPT_SKIP),
	AST_APP_OPTION('i', OPT_INDICATION),
	AST_APP_OPTION('n', OPT_NOANSWER),
});

static char *app = "ReadExten";

static int readexten_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char exten[256] = "";
	int maxdigits = sizeof(exten) - 1;
	int timeout = 0, digit_timeout = 0, x = 0;
	char *argcopy = NULL, *status = "";
	struct ast_tone_zone_sound *ts = NULL;
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
		ast_log(LOG_WARNING, "Usage: ReadExten(variable[,filename[,context[,options[,timeout]]]])\n");
		pbx_builtin_setvar_helper(chan, "READEXTENSTATUS", "ERROR");
		return 0;
	}

	if (ast_strlen_zero(arglist.filename)) {
		arglist.filename = NULL;
	}

	if (ast_strlen_zero(arglist.context)) {
		arglist.context = ast_strdupa(ast_channel_context(chan));
	}

	if (!ast_strlen_zero(arglist.options)) {
		ast_app_parse_options(readexten_app_options, &flags, NULL, arglist.options);
	}

	if (!ast_strlen_zero(arglist.timeout)) {
		timeout = atoi(arglist.timeout);
		if (timeout > 0)
			timeout *= 1000;
	}

	if (timeout <= 0)
		timeout = ast_channel_pbx(chan) ? ast_channel_pbx(chan)->rtimeoutms : 10000;

	if (digit_timeout <= 0)
		digit_timeout = ast_channel_pbx(chan) ? ast_channel_pbx(chan)->dtimeoutms : 5000;

	if (ast_test_flag(&flags, OPT_INDICATION) && !ast_strlen_zero(arglist.filename)) {
		ts = ast_get_indication_tone(ast_channel_zone(chan), arglist.filename);
	}

	do {
		if (ast_channel_state(chan) != AST_STATE_UP) {
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

		if (ts && ts->data[0]) {
			res = ast_playtones_start(chan, 0, ts->data, 0);
		} else if (arglist.filename) {
			if (ast_test_flag(&flags, OPT_INDICATION) && ast_fileexists(arglist.filename, NULL, ast_channel_language(chan)) <= 0) {
				/*
				 * We were asked to play an indication that did not exist in the config.
				 * If no such file exists, play it as a tonelist.  With any luck they won't
				 * have a file named "350+440.ulaw"
				 * (but honestly, who would do something so silly?)
				 */
				res = ast_playtones_start(chan, 0, arglist.filename, 0);
			} else {
				res = ast_streamfile(chan, arglist.filename, ast_channel_language(chan));
			}
		}

		for (x = 0; x < maxdigits; x++) {
			ast_debug(3, "extension so far: '%s', timeout: %d\n", exten, timeout);
			res = ast_waitfordigit(chan, timeout);

			ast_playtones_stop(chan);
			ast_stopstream(chan);
			timeout = digit_timeout;

			if (res < 1) {		/* timeout expired or hangup */
				if (ast_check_hangup(chan)) {
					status = "HANGUP";
				} else if (x == 0) {
					pbx_builtin_setvar_helper(chan, arglist.variable, "t");
					status = "TIMEOUT";
				}
				break;
			}

			exten[x] = res;
			if (!ast_matchmore_extension(chan, arglist.context, exten, 1 /* priority */,
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
				if (!ast_exists_extension(chan, arglist.context, exten, 1,
					S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))
					&& res == '#') {
					exten[x] = '\0';
				}
				break;
			}
		}

		if (!ast_strlen_zero(status))
			break;

		if (ast_exists_extension(chan, arglist.context, exten, 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_debug(3, "User entered valid extension '%s'\n", exten);
			pbx_builtin_setvar_helper(chan, arglist.variable, exten);
			status = "OK";
		} else {
			ast_debug(3, "User dialed invalid extension '%s' in context '%s' on %s\n", exten, arglist.context, ast_channel_name(chan));
			pbx_builtin_setvar_helper(chan, arglist.variable, "i");
			pbx_builtin_setvar_helper(chan, "INVALID_EXTEN", exten);
			status = "INVALID";
		}
	} while (0);

	if (ts) {
		ts = ast_tone_zone_sound_unref(ts);
	}

	pbx_builtin_setvar_helper(chan, "READEXTENSTATUS", status);

	return status[0] == 'H' ? -1 : 0;
}

static int unload_module(void)
{
	int res = ast_unregister_application(app);
	return res;
}

static int load_module(void)
{
	int res = ast_register_application_xml(app, readexten_exec);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Read and evaluate extension validity");
