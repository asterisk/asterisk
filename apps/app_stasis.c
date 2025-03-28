/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Stasis dialplan application.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend>res_stasis</depend>
	<depend>res_ari</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/ari.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_app_impl.h"

/*** DOCUMENTATION
	<application name="Stasis" language="en_US">
		<since>
			<version>12.0.0</version>
		</since>
		<synopsis>Invoke an external Stasis application.</synopsis>
		<syntax>
			<parameter name="app_name" required="true">
				<para>Name of the application to invoke.</para>
			</parameter>
			<parameter name="args">
				<para>Optional comma-delimited arguments for the
				application invocation.</para>
			</parameter>
		</syntax>
		<description>
			<para>Invoke a Stasis application.</para>
			<para>This application will set the following channel variable upon
			completion:</para>
			<variablelist>
				<variable name="STASISSTATUS">
					<para>This indicates the status of the execution of the
					Stasis application.</para>
					<value name="SUCCESS">
						The channel has exited Stasis without any failures in
						Stasis.
					</value>
					<value name="FAILED">
						A failure occurred when executing the Stasis
						The app registry is not instantiated; The app
						application. Some (not all) possible reasons for this:
						requested is not registered; The app requested is not
						active; Stasis couldn't send a start message.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

/*! \brief Maximum number of arguments for the Stasis dialplan application */
#define MAX_ARGS 128

/*! \brief Dialplan application name */
static const char *stasis = "Stasis";

/*! \brief Stasis dialplan application callback */
static int app_exec(struct ast_channel *chan, const char *data)
{
	char *parse = NULL;
	char *connection_id;
	int ret = -1;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(app_name);
		AST_APP_ARG(app_argv)[MAX_ARGS];
	);

	ast_assert(chan != NULL);
	ast_assert(data != NULL);

	pbx_builtin_setvar_helper(chan, "STASISSTATUS", "");

	/* parse the arguments */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 1) {
		ast_log(LOG_WARNING, "Stasis app_name argument missing\n");
		goto done;
	}

	if (stasis_app_is_registered(args.app_name)) {
		ast_debug(3, "%s: App '%s' is already registered\n",
			ast_channel_name(chan), args.app_name);
		ret = stasis_app_exec(chan, args.app_name, args.argc - 1, args.app_argv);
		goto done;
	}
	ast_debug(3, "%s: App '%s' is NOT already registered\n",
		ast_channel_name(chan), args.app_name);

	/*
	 * The app isn't registered so we need to see if we have a
	 * per-call outbound websocket config we can use.
	 * connection_id will be freed by ast_ari_close_per_call_websocket().
	 */
	connection_id = ast_ari_create_per_call_websocket(args.app_name, chan);
	if (ast_strlen_zero(connection_id)) {
		ast_log(LOG_WARNING,
			"%s: Stasis app '%s' doesn't exist\n",
			ast_channel_name(chan), args.app_name);
		goto done;
	}

	ret = stasis_app_exec(chan, connection_id, args.argc - 1, args.app_argv);
	ast_ari_close_per_call_websocket(connection_id);

done:
	if (ret) {
		/* set ret to 0 so pbx_core doesnt hangup the channel */
		if (!ast_check_hangup(chan)) {
			ret = 0;
		} else {
			ret = -1;
		}
		pbx_builtin_setvar_helper(chan, "STASISSTATUS", "FAILED");
	} else {
		pbx_builtin_setvar_helper(chan, "STASISSTATUS", "SUCCESS");
	}

	return ret;
}

static int load_module(void)
{
	return ast_register_application_xml(stasis, app_exec);
}

static int unload_module(void)
{
	return ast_unregister_application(stasis);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Stasis dialplan application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_stasis,res_ari",
);
