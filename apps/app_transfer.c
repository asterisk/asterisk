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
 * \brief Transfer a caller
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * Requires transfer support from channel driver
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"

/*** DOCUMENTATION
	<application name="Transfer" language="en_US">
		<synopsis>
			Transfer caller to remote extension.
		</synopsis>
		<syntax>
			<parameter name="dest" required="true" argsep="">
				<argument name="Tech/" />
				<argument name="destination" required="true" />
			</parameter>
		</syntax>
		<description>
			<para>Requests the remote caller be transferred
			to a given destination. If TECH (SIP, IAX2, etc) is used, only
			an incoming call with the same channel technology will be transferred.
			Note that for SIP, if you transfer before call is setup, a 302 redirect
			SIP message will be returned to the caller.</para>
			<para>The result of the application will be reported in the <variable>TRANSFERSTATUS</variable>
			channel variable:</para>
			<variablelist>
				<variable name="TRANSFERSTATUS">
					<value name="SUCCESS">
						Transfer succeeded.
					</value>
					<value name="FAILURE">
						Transfer failed.
					</value>
					<value name="UNSUPPORTED">
						Transfer unsupported by channel driver.
					</value>
				</variable>
				<variable name="TRANSFERSTATUSPROTOCOL">
					<value name="0">
						No error.
					</value>
					<value name="3xx-6xx">
						SIP example - Error result code.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static const char * const app = "Transfer";

static int transfer_exec(struct ast_channel *chan, const char *data)
{
	int res;
	int len;
	char *slash;
	char *tech = NULL;
	char *dest = NULL;
	char *status;
	char *parse;
	int protocol = 0;
	char status_protocol[20];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dest);
	);

	if (ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "Transfer requires an argument ([Tech/]destination)\n");
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "FAILURE");
		snprintf(status_protocol, sizeof(status_protocol), "%d", protocol);
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUSPROTOCOL", status_protocol);
		return 0;
	} else
		parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	dest = args.dest;

	if ((slash = strchr(dest, '/')) && (len = (slash - dest))) {
		tech = dest;
		dest = slash + 1;
		/* Allow execution only if the Tech/destination agrees with the type of the channel */
		if (strncasecmp(ast_channel_tech(chan)->type, tech, len)) {
			pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "FAILURE");
			snprintf(status_protocol, sizeof(status_protocol), "%d", protocol);
			pbx_builtin_setvar_helper(chan, "TRANSFERSTATUSPROTOCOL", status_protocol);
			return 0;
		}
	}

	/* Check if the channel supports transfer before we try it */
	if (!ast_channel_tech(chan)->transfer) {
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "UNSUPPORTED");
		snprintf(status_protocol, sizeof(status_protocol), "%d", protocol);
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUSPROTOCOL", status_protocol);
		return 0;
	}

	/* New transfer API returns a protocol code
	   SIP example, 0 = success, 3xx-6xx are sip error codes for the REFER */
	res = ast_transfer_protocol(chan, dest, &protocol);

	if (res < 0) {
		status = "FAILURE";
		res = 0;
	} else {
		status = "SUCCESS";
		res = 0;
	}

	snprintf(status_protocol, sizeof(status_protocol), "%d", protocol);
	ast_debug(1, "ast_transfer channel %s TRANSFERSTATUS=%s, TRANSFERSTATUSPROTOCOL=%s\n",
		  ast_channel_name(chan), status, status_protocol);
	pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", status);
	pbx_builtin_setvar_helper(chan, "TRANSFERSTATUSPROTOCOL", status_protocol);

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, transfer_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Transfers a caller to another extension");
