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
 * \brief App to lookup the callerid number, and see if it is blacklisted
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 * 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/callerid.h"
#include "asterisk/astdb.h"
#include "asterisk/options.h"

static char *app = "LookupBlacklist";

static char *synopsis = "Look up Caller*ID name/number from blacklist database";

static char *descrip =
  "  LookupBlacklist(options): Looks up the Caller*ID number on the active\n"
  "channel in the Asterisk database (family 'blacklist').  \n"
  "The option string may contain the following character:\n"
  "	'j' -- jump to n+101 priority if the number/name is found in the blacklist\n"
  "This application sets the following channel variable upon completion:\n"
  "	LOOKUPBLSTATUS		The status of the Blacklist lookup as a text string, one of\n"
  "		FOUND | NOTFOUND\n"
  "Example: exten => 1234,1,LookupBlacklist()\n\n"
  "This application is deprecated and may be removed from a future release.\n"
  "Please use the dialplan function BLACKLIST() instead.\n";


static int blacklist_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	char blacklist[1];
	int bl = 0;

	if (chan->cid.cid_num) {
		if (!ast_db_get("blacklist", chan->cid.cid_num, blacklist, sizeof (blacklist)))
			bl = 1;
	}
	if (chan->cid.cid_name) {
		if (!ast_db_get("blacklist", chan->cid.cid_name, blacklist, sizeof (blacklist)))
			bl = 1;
	}

	snprintf(buf, len, "%d", bl);
	return 0;
}

static struct ast_custom_function blacklist_function = {
	.name = "BLACKLIST",
	.synopsis = "Check if the callerid is on the blacklist",
	.desc = "Uses astdb to check if the Caller*ID is in family 'blacklist'.  Returns 1 or 0.\n",
	.syntax = "BLACKLIST()",
	.read = blacklist_read,
};

static int
lookupblacklist_exec (struct ast_channel *chan, void *data)
{
	char blacklist[1];
	struct ast_module_user *u;
	int bl = 0;
	int priority_jump = 0;
	static int dep_warning = 0;

	u = ast_module_user_add(chan);

	if (!dep_warning) {
		dep_warning = 1;
		ast_log(LOG_WARNING, "LookupBlacklist is deprecated.  Please use ${BLACKLIST()} instead.\n");
	}

	if (!ast_strlen_zero(data)) {
		if (strchr(data, 'j'))
			priority_jump = 1;
	}

	if (chan->cid.cid_num) {
		if (!ast_db_get("blacklist", chan->cid.cid_num, blacklist, sizeof (blacklist))) {
			if (option_verbose > 2)
				ast_log(LOG_NOTICE, "Blacklisted number %s found\n",chan->cid.cid_num);
			bl = 1;
		}
	}
	if (chan->cid.cid_name) {
		if (!ast_db_get("blacklist", chan->cid.cid_name, blacklist, sizeof (blacklist))) {
			if (option_verbose > 2)
				ast_log (LOG_NOTICE,"Blacklisted name \"%s\" found\n",chan->cid.cid_name);
			bl = 1;
		}
	}

	if (bl) {
		if (priority_jump || ast_opt_priority_jumping) 
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "LOOKUPBLSTATUS", "FOUND");
	} else
		pbx_builtin_setvar_helper(chan, "LOOKUPBLSTATUS", "NOTFOUND");	

	ast_module_user_remove(u);

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_custom_function_unregister(&blacklist_function);

	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	int res = ast_custom_function_register(&blacklist_function);
	res |= ast_register_application (app, lookupblacklist_exec, synopsis,descrip);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Look up Caller*ID name/number from blacklist database");
