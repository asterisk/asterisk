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
 * \brief Blind transfer by caller channel
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

/*** DOCUMENTATION
	<application name="BlindTransfer" language="en_US">
		<synopsis>
			Blind transfer channel(s) to the extension and context provided
		</synopsis>
		<syntax>
			<parameter name="exten" required="true">
				<para>Specify extension.</para>
			</parameter>
			<parameter name="context">
				<para>Optionally specify a context.
				By default, Asterisk will use the caller channel context.</para>
			</parameter>
		</syntax>
		<description>
			<para>Redirect all channels currently bridged to the caller channel to the
			specified destination.</para>
			<para>The result of the application will be reported in the <variable>BLINDTRANSFERSTATUS</variable>
			channel variable:</para>
			<variablelist>
				<variable name="BLINDTRANSFERSTATUS">
					<value name="SUCCESS">
						Transfer succeeded.
					</value>
					<value name="FAILURE">
						Transfer failed.
					</value>
					<value name="INVALID">
						Transfer invalid.
					</value>
					<value name="NOTPERMITTED">
						Transfer not permitted.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static const char * const app = "BlindTransfer";

static int blind_transfer_exec(struct ast_channel *chan, const char *data)
{
	char *exten = NULL;
	char *context = NULL;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(exten);
		AST_APP_ARG(context);
	);

	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "%s requires an argument (exten)\n", app);
		pbx_builtin_setvar_helper(chan, "BLINDTRANSFERSTATUS", "FAILURE");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	exten = args.exten;
	if (ast_strlen_zero(args.context)) {
		context = (char *)ast_channel_context(chan);
	} else {
		context = args.context;
	}

	switch (ast_bridge_transfer_blind(1, chan, exten, context, NULL, NULL)) {
		case AST_BRIDGE_TRANSFER_NOT_PERMITTED:
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFERSTATUS", "NOTPERMITTED");
			break;
		case AST_BRIDGE_TRANSFER_INVALID:
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFERSTATUS", "INVALID");
			break;
		case AST_BRIDGE_TRANSFER_FAIL:
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFERSTATUS", "FAILURE");
			break;
		case AST_BRIDGE_TRANSFER_SUCCESS:
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFERSTATUS", "SUCCESS");
			break;
		default:
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFERSTATUS", "FAILURE");
        }

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, blind_transfer_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Blind transfer channel to the given destination");
