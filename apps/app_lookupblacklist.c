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
 * \ingroup applications
 * 
 */

#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

static char *tdesc = "Look up Caller*ID name/number from blacklist database";

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
  "Example: exten => 1234,1,LookupBlacklist()\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int
lookupblacklist_exec (struct ast_channel *chan, void *data)
{
	char blacklist[1];
	struct localuser *u;
	int bl = 0;
	int priority_jump = 0;

	LOCAL_USER_ADD(u);

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
		if (priority_jump || option_priority_jumping) 
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "LOOKUPBLSTATUS", "FOUND");
	} else
		pbx_builtin_setvar_helper(chan, "LOOKUPBLSTATUS", "NOTFOUND");	

	LOCAL_USER_REMOVE(u);

	return 0;
}

int unload_module (void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

int load_module (void)
{
	return ast_register_application (app, lookupblacklist_exec, synopsis,descrip);
}

char *description (void)
{
	return tdesc;
}

int usecount (void)
{
	int res;
	STANDARD_USECOUNT (res);
	return res;
}

char *key ()
{
	return ASTERISK_GPL_KEY;
}
