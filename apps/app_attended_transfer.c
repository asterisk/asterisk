/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Alexei Gradinari
 *
 * Alexei Gradinari <alex2grad@gmail.com>
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
 * \brief Attended transfer by caller channel
 *
 * \author Alexei Gradinari <alex2grad@gmail.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/features_config.h"

/*** DOCUMENTATION
	<application name="AttendedTransfer" language="en_US">
		<synopsis>
			Attended transfer to the extension provided and TRANSFER_CONTEXT
		</synopsis>
		<syntax>
			<parameter name="exten" required="true">
				<para>Specify extension.</para>
			</parameter>
		</syntax>
		<description>
			<para>Queue up attended transfer to the specified extension in the <literal>TRANSFER_CONTEXT</literal>.</para>
			<para>Note that the attended transfer only work when two channels have answered and are bridged together.</para>
			<para>Make sure to set Attended Transfer DTMF feature <literal>atxfer</literal>
			and attended transfer is permitted.</para>
			<para>The result of the application will be reported in the <variable>ATTENDEDTRANSFERSTATUS</variable>
			channel variable:</para>
			<variablelist>
				<variable name="ATTENDEDTRANSFERSTATUS">
					<value name="SUCCESS">
						Transfer successfully queued.
					</value>
					<value name="FAILURE">
						Transfer failed.
					</value>
					<value name="NOTPERMITTED">
						Transfer not permitted.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static const char * const app = "AttendedTransfer";

static int attended_transfer_exec(struct ast_channel *chan, const char *data)
{
	char *exten = NULL;
	const char *context = NULL;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(exten);
	);
	char feature_code[AST_FEATURE_MAX_LEN];
	const char *digit;
	struct ast_frame f = { .frametype = AST_FRAME_DTMF };

	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "%s requires an argument (exten)\n", app);
		pbx_builtin_setvar_helper(chan, "ATTENDEDTRANSFERSTATUS", "FAILURE");
		return 0;
	}

	context = pbx_builtin_getvar_helper(chan, "TRANSFER_CONTEXT");
	if (ast_strlen_zero(context)) {
		pbx_builtin_setvar_helper(chan, "ATTENDEDTRANSFERSTATUS", "NOTPERMITTED");
		return 0;
	}

	ast_channel_lock(chan);
	if (ast_get_builtin_feature(chan, "atxfer", feature_code, sizeof(feature_code)) ||
		ast_strlen_zero(feature_code)) {
		pbx_builtin_setvar_helper(chan, "ATTENDEDTRANSFERSTATUS", "NOTPERMITTED");
		ast_channel_unlock(chan);
		return 0;
	}
	ast_channel_unlock(chan);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	exten = args.exten;

	for (digit = feature_code; *digit; ++digit) {
		f.subclass.integer = *digit;
		ast_queue_frame(chan, &f);
	}

	for (digit = exten; *digit; ++digit) {
		f.subclass.integer = *digit;
		ast_queue_frame(chan, &f);
	}

	f.subclass.integer = '#';
	ast_queue_frame(chan, &f);

	pbx_builtin_setvar_helper(chan, "ATTENDEDTRANSFERSTATUS", "SUCCESS");

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, attended_transfer_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Attended transfer to the given extension");
