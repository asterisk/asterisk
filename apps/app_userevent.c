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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/app.h"

static char *tdesc = "Custom User Event Application";

static char *app = "UserEvent";

static char *synopsis = "Send an arbitrary event to the manager interface";

static char *descrip = 
"  UserEvent(eventname[|body]): Sends an arbitrary event to the manager\n"
"interface, with an optional body representing additional arguments.  The\n"
"body may be specified as a | delimeted list of headers. Each additional\n"
"argument will be placed on a new line in the event. The format of the\n"
"event will be:\n"
"    Event: UserEvent\n"
"    UserEvent: <specified event name>\n"
"    [body]\n"
"If no body is specified, only Event and UserEvent headers will be present.\n";

LOCAL_USER_DECL;

static int userevent_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	char *parse, buf[2048] = "";
	int x, buflen = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(eventname);
		AST_APP_ARG(extra)[100];
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "UserEvent requires an argument (eventname|optional event body)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	for (x = 0; x < args.argc - 1; x++) {
		ast_copy_string(buf + buflen, args.extra[x], sizeof(buf) - buflen - 2);
		buflen += strlen(args.extra[x]);
		ast_copy_string(buf + buflen, "\r\n", 3);
		buflen += 2;
	}

	manager_event(EVENT_FLAG_USER, "UserEvent", "UserEvent: %s\r\n%s\r\n", args.eventname, buf);

	LOCAL_USER_REMOVE(u);
	return 0;
}

static int unload_module(void *mod)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

static int load_module(void *mod)
{
	return ast_register_application(app, userevent_exec, synopsis, descrip);
}

static const char *description(void)
{
	return tdesc;
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD1;
