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
 * \brief Playback the special information tone to get rid of telemarketers
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="Zapateller" language="en_US">
		<synopsis>
			Block telemarketers with SIT.
		</synopsis>
		<syntax>
			<parameter name="options" required="true">
				<para>Comma delimited list of options.</para>
				<optionlist>
					<option name="answer">
						<para>Causes the line to be answered before playing the tone.</para>
					</option>
					<option name="nocallerid">
						<para>Causes Zapateller to only play the tone if there is no
						callerid information available.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Generates special information tone to block telemarketers from calling you.</para>
			<para>This application will set the following channel variable upon completion:</para>
			<variablelist>
				<variable name="ZAPATELLERSTATUS">
					<para>This will contain the last action accomplished by the
					Zapateller application. Possible values include:</para>
					<value name="NOTHING" />
					<value name="ANSWERED" />
					<value name="ZAPPED" />
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static char *app = "Zapateller";

static int zapateller_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int i, answer = 0, nocallerid = 0;
	char *parse = ast_strdupa((char *)data);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options)[2];
	);

	AST_STANDARD_APP_ARGS(args, parse);

	for (i = 0; i < args.argc; i++) {
		if (!strcasecmp(args.options[i], "answer"))
			answer = 1;
		else if (!strcasecmp(args.options[i], "nocallerid"))
			nocallerid = 1;
	}

	pbx_builtin_setvar_helper(chan, "ZAPATELLERSTATUS", "NOTHING");
	ast_stopstream(chan);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (answer) {
			res = ast_answer(chan);
			pbx_builtin_setvar_helper(chan, "ZAPATELLERSTATUS", "ANSWERED");
		}
		if (!res)
			res = ast_safe_sleep(chan, 500);
	}

	if (nocallerid	/* Zap caller if no caller id. */
		&& ast_channel_caller(chan)->id.number.valid
		&& !ast_strlen_zero(ast_channel_caller(chan)->id.number.str)) {
		/* We have caller id. */
		return res;
	}

	if (!res) 
		res = ast_tonepair(chan, 950, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 1400, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 1800, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 0, 0, 1000, 0);
	
	pbx_builtin_setvar_helper(chan, "ZAPATELLERSTATUS", "ZAPPED");
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ((ast_register_application_xml(app, zapateller_exec)) ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Block Telemarketers with Special Information Tone");

