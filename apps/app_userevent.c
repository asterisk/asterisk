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

ASTERISK_REGISTER_FILE()

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/app.h"
#include "asterisk/json.h"
#include "asterisk/stasis_channels.h"

/*** DOCUMENTATION
	<application name="UserEvent" language="en_US">
		<synopsis>
			Send an arbitrary user-defined event to parties interested in a channel (AMI users and relevant res_stasis applications).
		</synopsis>
		<syntax>
			<parameter name="eventname" required="true" />
			<parameter name="body" />
		</syntax>
		<description>
			<para>Sends an arbitrary event to interested parties, with an optional
			<replaceable>body</replaceable> representing additional arguments. The
			<replaceable>body</replaceable> may be specified as
			a <literal>,</literal> delimited list of key:value pairs.</para>
			<para>For AMI, each additional argument will be placed on a new line in
			the event and the format of the event will be:</para>
			<para>    Event: UserEvent</para>
			<para>    UserEvent: &lt;specified event name&gt;</para>
			<para>    [body]</para>
			<para>If no <replaceable>body</replaceable> is specified, only Event and
			UserEvent headers will be present.</para>
			<para>For res_stasis applications, the event will be provided as a JSON
			blob with additional arguments appearing as keys in the object and the
			<replaceable>eventname</replaceable> under the
			<literal>eventname</literal> key.</para>
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
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "UserEvent requires an argument (eventname,optional event body)\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	blob = ast_json_pack("{s: s}",
			     "eventname", args.eventname);
	if (!blob) {
		return -1;
	}

	for (x = 0; x < args.argc - 1; x++) {
		char *key, *value = args.extra[x];
		struct ast_json *json_value;

		key = strsep(&value, ":");
		if (!value) {
			/* no ':' in string? */
			continue;
		}

		value = ast_strip(value);
		json_value = ast_json_string_create(value);
		if (!json_value) {
			return -1;
		}

		/* ref stolen by ast_json_object_set */
		if (ast_json_object_set(blob, key, json_value)) {
			return -1;
		}
	}

	ast_channel_lock(chan);
	ast_multi_object_blob_single_channel_publish(chan, ast_multi_user_event_type(), blob);
	ast_channel_unlock(chan);
	return 0;
}

static int load_module(void)
{
	return ast_register_application_xml(app, userevent_exec);
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Custom User Event Application");
