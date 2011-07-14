/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief UserEvent application -- send manager event
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="UserEvent" language="en_US">
		<synopsis>
			Send an arbitrary event to the manager interface.
		</synopsis>
		<syntax>
			<parameter name="eventname" required="true" />
			<parameter name="body" />
		</syntax>
		<description>
			<para>Sends an arbitrary event to the manager interface, with an optional
			<replaceable>body</replaceable> representing additional arguments. The
			<replaceable>body</replaceable> may be specified as
			a <literal>,</literal> delimited list of headers. Each additional
			argument will be placed on a new line in the event. The format of the
			event will be:</para>
			<para>    Event: UserEvent</para>
			<para>    UserEvent: &lt;specified event name&gt;</para>
			<para>    [body]</para>
			<para>If no <replaceable>body</replaceable> is specified, only Event and UserEvent headers will be present.</para>
		</description>
	</application>
 ***/

static char *app = "UserEvent";

static int userevent_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	int x;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(eventname);
		AST_APP_ARG(extra)[100];
	);
	struct ast_str *body = ast_str_create(16);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "UserEvent requires an argument (eventname,optional event body)\n");
		ast_free(body);
		return -1;
	}

	if (!body) {
		ast_log(LOG_WARNING, "Unable to allocate buffer\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	for (x = 0; x < args.argc - 1; x++) {
		ast_str_append(&body, 0, "%s\r\n", args.extra[x]);
	}

	manager_event(EVENT_FLAG_USER, "UserEvent", "UserEvent: %s\r\n%s", args.eventname, ast_str_buffer(body));
	ast_free(body);

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, userevent_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Custom User Event Application");
