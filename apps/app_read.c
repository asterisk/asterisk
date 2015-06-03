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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"

/*** DOCUMENTATION
	<application name="Read" language="en_US">
		<synopsis>
			Read a variable.
		</synopsis>
		<syntax>
			<parameter name="variable" required="true">
				<para>The input digits will be stored in the given <replaceable>variable</replaceable>
				name.</para>
			</parameter>
			<parameter name="filenames" argsep="&amp;">
				<argument name="filename" required="true">
					<para>file(s) to play before reading digits or tone with option i</para>
				</argument>
				<argument name="filename2" multiple="true" />
			</parameter>
			<parameter name="maxdigits">
				<para>Maximum acceptable number of digits. Stops reading after
				<replaceable>maxdigits</replaceable> have been entered (without
				requiring the user to press the <literal>#</literal> key).</para>
				<para>Defaults to <literal>0</literal> - no limit - wait for the
				user press the <literal>#</literal> key. Any value below
				<literal>0</literal> means the same. Max accepted value is
				<literal>255</literal>.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="s">
						<para>to return immediately if the line is not up.</para>
					</option>
					<option name="i">
						<para>to play  filename as an indication tone from your
						<filename>indications.conf</filename>.</para>
					</option>
					<option name="n">
						<para>to read digits even if the line is not up.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="attempts">
				<para>If greater than <literal>1</literal>, that many
				<replaceable>attempts</replaceable> will be made in the
				event no data is entered.</para>
			</parameter>
			<parameter name="timeout">
				<para>The number of seconds to wait for a digit response. If greater
				than <literal>0</literal>, that value will override the default timeout.
				Can be floating point.</para>
			</parameter>
		</syntax>
		<description>
			<para>Reads a #-terminated string of digits a certain number of times from the
			user in to the given <replaceable>variable</replaceable>.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="READSTATUS">
					<para>This is the status of the read operation.</para>
					<value name="OK" />
					<value name="ERROR" />
					<value name="HANGUP" />
					<value name="INTERRUPTED" />
					<value name="SKIPPED" />
					<value name="TIMEOUT" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">SendDTMF</ref>
		</see-also>
	</application>
 ***/

enum read_option_flags {
	OPT_SKIP = (1 << 0),
	OPT_INDICATION = (1 << 1),
	OPT_NOANSWER = (1 << 2),
};

AST_APP_OPTIONS(read_app_options, {
	AST_APP_OPTION('s', OPT_SKIP),
	AST_APP_OPTION('i', OPT_INDICATION),
	AST_APP_OPTION('n', OPT_NOANSWER),
});

static char *app = "Read";

static int read_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char tmp[256] = "";
	int maxdigits = 255;
	int tries = 1, to = 0, x = 0;
	double tosec;
	char *argcopy = NULL;
	struct ast_tone_zone_sound *ts = NULL;
	struct ast_flags flags = {0};
	const char *status = "ERROR";

	AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(variable);
		AST_APP_ARG(filename);
		AST_APP_ARG(maxdigits);
		AST_APP_ARG(options);
		AST_APP_ARG(attempts);
		AST_APP_ARG(timeout);
	);

	pbx_builtin_setvar_helper(chan, "READSTATUS", status);
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Read requires an argument (variable)\n");
		return 0;
	}

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
		tosec = atof(arglist.timeout);
		if (tosec <= 0)
			to = 0;
		else
			to = tosec * 1000.0;
	}

	if (ast_strlen_zero(arglist.filename)) {
		arglist.filename = NULL;
	}
	if (!ast_strlen_zero(arglist.maxdigits)) {
		maxdigits = atoi(arglist.maxdigits);
		if ((maxdigits < 1) || (maxdigits > 255)) {
			maxdigits = 255;
		} else
			ast_verb(3, "Accepting a maximum of %d digits.\n", maxdigits);
	}
	if (ast_strlen_zero(arglist.variable)) {
		ast_log(LOG_WARNING, "Invalid! Usage: Read(variable[,filename][,maxdigits][,option][,attempts][,timeout])\n\n");
		return 0;
	}
	if (ast_test_flag(&flags, OPT_INDICATION)) {
		if (!ast_strlen_zero(arglist.filename)) {
			ts = ast_get_indication_tone(ast_channel_zone(chan), arglist.filename);
		}
	}
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_test_flag(&flags, OPT_SKIP)) {
			/* At the user's option, skip if the line is not up */
			pbx_builtin_setvar_helper(chan, arglist.variable, "");
			pbx_builtin_setvar_helper(chan, "READSTATUS", "SKIPPED");
			return 0;
		} else if (!ast_test_flag(&flags, OPT_NOANSWER)) {
			/* Otherwise answer unless we're supposed to read while on-hook */
			res = ast_answer(chan);
		}
	}
	if (!res) {
		while (tries && !res) {
			ast_stopstream(chan);
			if (ts && ts->data[0]) {
				if (!to)
					to = ast_channel_pbx(chan) ? ast_channel_pbx(chan)->rtimeoutms : 6000;
				res = ast_playtones_start(chan, 0, ts->data, 0);
				for (x = 0; x < maxdigits; ) {
					res = ast_waitfordigit(chan, to);
					ast_playtones_stop(chan);
					if (res < 1) {
						if (res == 0)
							status = "TIMEOUT";
						tmp[x]='\0';
						break;
					}
					tmp[x++] = res;
					if (tmp[x-1] == '#') {
						tmp[x-1] = '\0';
						status = "OK";
						break;
					}
					if (x >= maxdigits) {
						status = "OK";
					}
				}
			} else {
				res = ast_app_getdata(chan, arglist.filename, tmp, maxdigits, to);
				if (res == AST_GETDATA_COMPLETE || res == AST_GETDATA_EMPTY_END_TERMINATED)
					status = "OK";
				else if (res == AST_GETDATA_TIMEOUT)
					status = "TIMEOUT";
				else if (res == AST_GETDATA_INTERRUPTED)
					status = "INTERRUPTED";
			}
			if (res > -1) {
				pbx_builtin_setvar_helper(chan, arglist.variable, tmp);
				if (!ast_strlen_zero(tmp)) {
					ast_verb(3, "User entered '%s'\n", tmp);
					tries = 0;
				} else {
					tries--;
					if (tries)
						ast_verb(3, "User entered nothing, %d chance%s left\n", tries, (tries != 1) ? "s" : "");
					else
						ast_verb(3, "User entered nothing.\n");
				}
				res = 0;
			} else {
				pbx_builtin_setvar_helper(chan, arglist.variable, tmp);
				ast_verb(3, "User disconnected\n");
			}
		}
	}

	if (ts) {
		ts = ast_tone_zone_sound_unref(ts);
	}

	if (ast_check_hangup(chan))
		status = "HANGUP";
	pbx_builtin_setvar_helper(chan, "READSTATUS", status);
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, read_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Read Variable Application");
