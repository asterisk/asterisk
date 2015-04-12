/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2003, 2006 Tilghman Lesher.  All rights reserved.
 * Copyright (c) 2006 Digium, Inc.
 *
 * Tilghman Lesher <app_sayunixtime__200309@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief SayUnixTime application
 *
 * \author Tilghman Lesher <app_sayunixtime__200309@the-tilghman.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/say.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="SayUnixTime" language="en_US">
		<synopsis>
			Says a specified time in a custom format.
		</synopsis>
		<syntax>
			<parameter name="unixtime" required="false">
				<para>time, in seconds since Jan 1, 1970.  May be negative. Defaults to now.</para>
			</parameter>
			<parameter name="timezone" required="false" >
				<para>timezone, see <directory>/usr/share/zoneinfo</directory> for a list. Defaults to machine default.</para>
			</parameter>
			<parameter name="format" required="false" >
				<para>a format the time is to be said in.  See <filename>voicemail.conf</filename>.
				Defaults to <literal>ABdY "digits/at" IMp</literal></para>
			</parameter>
			<parameter name="options" required="false">
				 <optionlist>
					<option name="j">
						<para>Allow the calling user to dial digits to jump to that extension.
						This option is automatically enabled if
						<variable>SAY_DTMF_INTERRUPT</variable> is present on the channel and
						set to 'true' (case insensitive)</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Uses some of the sound files stored in <directory>/var/lib/asterisk/sounds</directory> to construct a phrase 
			saying the specified date and/or time in the specified format. </para>
		</description>
		<see-also>
			<ref type="function">STRFTIME</ref>
			<ref type="function">STRPTIME</ref>
			<ref type="function">IFTIME</ref>
		</see-also>
	</application>
	<application name="DateTime" language="en_US">
		<synopsis>
			Says a specified time in a custom format.
		</synopsis>
		<syntax>
			<parameter name="unixtime">
				<para>time, in seconds since Jan 1, 1970.  May be negative. Defaults to now.</para>
			</parameter>
			<parameter name="timezone">
				<para>timezone, see <filename>/usr/share/zoneinfo</filename> for a list. Defaults to machine default.</para>
			</parameter>
			<parameter name="format">
				<para>a format the time is to be said in.  See <filename>voicemail.conf</filename>.
				Defaults to <literal>ABdY "digits/at" IMp</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>Say the date and time in a specified format.</para>
		</description>
	</application>

 ***/

enum {
	OPT_JUMP =          (1 << 0),
};

enum {
	OPT_ARG_JUMP = 0,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(sayunixtime_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('j', OPT_JUMP, OPT_ARG_JUMP),
END_OPTIONS );

static char *app_sayunixtime = "SayUnixTime";
static char *app_datetime = "DateTime";

static int sayunixtime_exec(struct ast_channel *chan, const char *data)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timeval);
		AST_APP_ARG(timezone);
		AST_APP_ARG(format);
		AST_APP_ARG(options);
	);
	char *parse;
	int res = 0;
	time_t unixtime;
	/* New default behavior is do not jump on key pressed */
	const char * haltondigits = AST_DIGIT_NONE;
	struct ast_flags64 opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	const char *interrupt_string;

	if (!data) {
		return 0;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	/* check if we had the 'j' jump flag in option list */
	if (!ast_strlen_zero(args.options))	{
		ast_app_parse_options64(sayunixtime_exec_options, &opts, opt_args, args.options);
		if (ast_test_flag64(&opts, OPT_JUMP)){
			haltondigits = AST_DIGIT_ANY;
		}
	}

	/* Check if 'SAY_DTMF_INTERRUPT' is true and apply the same behavior as the j flag. */
	ast_channel_lock(chan);
	interrupt_string = pbx_builtin_getvar_helper(chan, "SAY_DTMF_INTERRUPT");
	if (ast_true(interrupt_string)) {
		haltondigits = AST_DIGIT_ANY;
	}
	ast_channel_unlock(chan);

	ast_get_time_t(ast_strlen_zero(args.timeval) ? NULL : args.timeval, &unixtime, time(NULL), NULL);

	if (ast_channel_state(chan) != AST_STATE_UP) {
		res = ast_answer(chan);
	}

	if (!res) {
		res = ast_say_date_with_format(chan, unixtime, haltondigits,
					       ast_channel_language(chan), ast_strlen_zero(args.format) ? NULL : args.format, ast_strlen_zero(args.timezone) ? NULL : args.timezone);
	}

	return res;
}

static int unload_module(void)
{
	int res;
	
	res = ast_unregister_application(app_sayunixtime);
	res |= ast_unregister_application(app_datetime);
	
	return res;
}

static int load_module(void)
{
	int res;
	
	res = ast_register_application_xml(app_sayunixtime, sayunixtime_exec);
	res |= ast_register_application_xml(app_datetime, sayunixtime_exec);
	
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Say time");
