/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Martin Pycko <martinp@digium.com>
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
 * \brief Applications connected with CDR engine
 *
 * \author Martin Pycko <martinp@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="NoCDR" language="en_US">
		<synopsis>
			Tell Asterisk to not maintain a CDR for this channel.
		</synopsis>
		<syntax />
		<description>
			<para>This application will tell Asterisk not to maintain a CDR for
			the current channel. This does <emphasis>NOT</emphasis> mean that
			information is not tracked; rather, if the channel is hung up no
			CDRs will be created for that channel.</para>
			<para>If a subsequent call to ResetCDR occurs, all non-finalized
			CDRs created for the channel will be enabled.</para>
			<note><para>This application is deprecated. Please use the CDR_PROP
			function to disable CDRs on a channel.</para></note>
		</description>
		<see-also>
			<ref type="application">ResetCDR</ref>
			<ref type="function">CDR_PROP</ref>
		</see-also>
	</application>
	<application name="ResetCDR" language="en_US">
		<synopsis>
			Resets the Call Data Record.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="v">
						<para>Save the CDR variables during the reset.</para>
					</option>
					<option name="e">
						<para>Enable the CDRs for this channel only (negate
						effects of NoCDR).</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application causes the Call Data Record to be reset.
			Depending on the flags passed in, this can have several effects.
			With no options, a reset does the following:</para>
			<para>1. The <literal>start</literal> time is set to the current time.</para>
			<para>2. If the channel is answered, the <literal>answer</literal> time is set to the
			current time.</para>
			<para>3. All variables are wiped from the CDR. Note that this step
			can be prevented with the <literal>v</literal> option.</para>
			<para>On the other hand, if the <literal>e</literal> option is
			specified, the effects of the NoCDR application will be lifted. CDRs
			will be re-enabled for this channel.</para>
			<note><para>The <literal>e</literal> option is deprecated. Please
			use the CDR_PROP function instead.</para></note>
		</description>
		<see-also>
			<ref type="application">ForkCDR</ref>
			<ref type="application">NoCDR</ref>
			<ref type="function">CDR_PROP</ref>
		</see-also>
	</application>
 ***/

static const char nocdr_app[] = "NoCDR";
static const char resetcdr_app[] = "ResetCDR";

enum reset_cdr_options {
	OPT_DISABLE_DISPATCH = (1 << 0),
	OPT_KEEP_VARS = (1 << 1),
	OPT_ENABLE = (1 << 2),
};

AST_APP_OPTIONS(resetcdr_opts, {
	AST_APP_OPTION('v', AST_CDR_FLAG_KEEP_VARS),
	AST_APP_OPTION('e', AST_CDR_FLAG_DISABLE_ALL),
});

static int resetcdr_exec(struct ast_channel *chan, const char *data)
{
	char *args;
	struct ast_flags flags = { 0 };
	int res = 0;

	if (!ast_strlen_zero(data)) {
		args = ast_strdupa(data);
		ast_app_parse_options(resetcdr_opts, &flags, NULL, args);
	}

	if (ast_test_flag(&flags, AST_CDR_FLAG_DISABLE_ALL)) {
		if (ast_cdr_clear_property(ast_channel_name(chan), AST_CDR_FLAG_DISABLE_ALL)) {
			res = 1;
		}
	}
	if (ast_cdr_reset(ast_channel_name(chan), &flags)) {
		res = 1;
	}

	if (res) {
		ast_log(AST_LOG_WARNING, "Failed to reset CDR for channel %s\n", ast_channel_name(chan));
	}
	return res;
}

static int nocdr_exec(struct ast_channel *chan, const char *data)
{
	if (ast_cdr_set_property(ast_channel_name(chan), AST_CDR_FLAG_DISABLE_ALL)) {
		ast_log(AST_LOG_WARNING, "Failed to disable CDR for channel %s\n", ast_channel_name(chan));
	}

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(nocdr_app);
}

static int load_module(void)
{
	int res = 0;

	res |= ast_register_application_xml(nocdr_app, nocdr_exec);
	res |= ast_register_application_xml(resetcdr_app, resetcdr_exec);

	if (res) {
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Tell Asterisk to not maintain a CDR for the current call");
