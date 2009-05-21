/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Roberto Casas.
 * Copyright (C) 2008, Digium, Inc.
 *
 * Roberto Casas <roberto.casas@diaple.com>
 * Russell Bryant <russell@digium.com>
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

/*!
 * \file
 * \brief Originate application
 *
 * \author Roberto Casas <roberto.casas@diaple.com>
 * \author Russell Bryant <russell@digium.com>
 *
 * \ingroup applications
 *
 * \todo Make a way to be able to set variables (and functions) on the outbound
 *       channel, similar to the Variable headers for the AMI Originate, and the
 *       Set options for call files.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

static const char app_originate[] = "Originate";

/*** DOCUMENTATION
	<application name="Originate" language="en_US">
		<synopsis>
			Originate a call.
		</synopsis>
		<syntax>
			<parameter name="tech_data" required="true">
				<para>Channel technology and data for creating the outbound channel.
                      For example, SIP/1234.</para>
			</parameter>
			<parameter name="type" required="true">
				<para>This should be <literal>app</literal> or <literal>exten</literal>, depending on whether the outbound channel should be connected to an application or extension.</para>
			</parameter>
			<parameter name="arg1" required="true">
				<para>If the type is <literal>app</literal>, then this is the application name.  If the type is <literal>exten</literal>, then this is the context that the channel will be sent to.</para>
			</parameter>
			<parameter name="arg2" required="false">
				<para>If the type is <literal>app</literal>, then this is the data passed as arguments to the application.  If the type is <literal>exten</literal>, then this is the extension that the channel will be sent to.</para>
			</parameter>
			<parameter name="arg3" required="false">
				<para>If the type is <literal>exten</literal>, then this is the priority that the channel is sent to.  If the type is <literal>app</literal>, then this parameter is ignored.</para>
			</parameter>
		</syntax>
		<description>
		<para>This application originates an outbound call and connects it to a specified extension or application.  This application will block until the outgoing call fails or gets answered.  At that point, this application will exit with the status variable set and dialplan processing will continue.</para>

		<para>This application sets the following channel variable before exiting:</para>
		<variablelist>
			<variable name="ORIGINATE_STATUS">
				<para>This indicates the result of the call origination.</para>
				<value name="FAILED"/>
				<value name="SUCCESS"/>
				<value name="BUSY"/>
				<value name="CONGESTION"/>
				<value name="HANGUP"/>
				<value name="RINGING"/>
				<value name="UNKNOWN">
				In practice, you should never see this value.  Please report it to the issue tracker if you ever see it.
				</value>
			</variable>
		</variablelist>
		</description>
	</application>
 ***/

static int originate_exec(struct ast_channel *chan, const char *data)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(tech_data);
		AST_APP_ARG(type);
		AST_APP_ARG(arg1);
		AST_APP_ARG(arg2);
		AST_APP_ARG(arg3);
	);
	char *parse;
	char *chantech, *chandata;
	int res = -1;
	int outgoing_res = 0;
	int outgoing_status = 0;
	static const unsigned int timeout = 30;
	static const char default_exten[] = "s";

	ast_autoservice_start(chan);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Originate() requires arguments\n");
		goto return_cleanup;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 3) {
		ast_log(LOG_ERROR, "Incorrect number of arguments\n");
		goto return_cleanup;
	}

	chandata = ast_strdupa(args.tech_data);
	chantech = strsep(&chandata, "/");

	if (ast_strlen_zero(chandata) || ast_strlen_zero(chantech)) {
		ast_log(LOG_ERROR, "Channel Tech/Data invalid: '%s'\n", args.tech_data);
		goto return_cleanup;
	}

	if (!strcasecmp(args.type, "exten")) {
		int priority = 1; /* Initialized in case priority not specified */
		const char *exten = args.arg2;

		if (args.argc == 5) {
			/* Context/Exten/Priority all specified */
			if (sscanf(args.arg3, "%d", &priority) != 1) {
				ast_log(LOG_ERROR, "Invalid priority: '%s'\n", args.arg3);
				goto return_cleanup;
			}
		} else if (args.argc == 3) {
			/* Exten not specified */
			exten = default_exten;
		}

		ast_debug(1, "Originating call to '%s/%s' and connecting them to extension %s,%s,%d\n",
				chantech, chandata, args.arg1, exten, priority);

		outgoing_res = ast_pbx_outgoing_exten(chantech, AST_FORMAT_SLINEAR, chandata,
				timeout * 1000, args.arg1, exten, priority, &outgoing_status, 0, NULL,
				NULL, NULL, NULL, NULL);
	} else if (!strcasecmp(args.type, "app")) {
		ast_debug(1, "Originating call to '%s/%s' and connecting them to %s(%s)\n",
				chantech, chandata, args.arg1, S_OR(args.arg2, ""));

		outgoing_res = ast_pbx_outgoing_app(chantech, AST_FORMAT_SLINEAR, chandata,
				timeout * 1000, args.arg1, args.arg2, &outgoing_status, 0, NULL,
				NULL, NULL, NULL, NULL);
	} else {
		ast_log(LOG_ERROR, "Incorrect type, it should be 'exten' or 'app': %s\n",
				args.type);
		goto return_cleanup;
	}

	res = 0;

return_cleanup:
	if (res) {
		pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "FAILED");
	} else {
		switch (outgoing_status) {
		case 0:
		case AST_CONTROL_ANSWER:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "SUCCESS");
			break;
		case AST_CONTROL_BUSY:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "BUSY");
			break;
		case AST_CONTROL_CONGESTION:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "CONGESTION");
			break;
		case AST_CONTROL_HANGUP:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "HANGUP");
			break;
		case AST_CONTROL_RINGING:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "RINGING");
			break;
		default:
			ast_log(LOG_WARNING, "Unknown originate status result of '%d'\n",
					outgoing_status);
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "UNKNOWN");
			break;
		}
	}

	ast_autoservice_stop(chan);

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app_originate);
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app_originate, originate_exec);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Originate call");
