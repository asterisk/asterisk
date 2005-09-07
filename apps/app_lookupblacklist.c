/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to lookup the callerid number, and see if it is blacklisted
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
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

static char *tdesc = "Look up Caller*ID name/number from blacklist database";

static char *app = "LookupBlacklist";

static char *synopsis = "Look up Caller*ID name/number from blacklist database";

static char *descrip =
  "  LookupBlacklist: Looks up the Caller*ID number on the active\n"
  "channel in the Asterisk database (family 'blacklist').  If the\n"
  "number is found, and if there exists a priority n + 101,\n"
  "where 'n' is the priority of the current instance, then  the\n"
  "channel  will  be  setup  to continue at that priority level.\n"
  "Otherwise, it returns 0.  Does nothing if no Caller*ID was received on the\n"
  "channel.\n"
  "Example: database put blacklist <name/number> 1\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int
lookupblacklist_exec (struct ast_channel *chan, void *data)
{
	char blacklist[1];
	struct localuser *u;
	int bl = 0;

	LOCAL_USER_ADD (u);
	if (chan->cid.cid_num)
	{
		if (!ast_db_get ("blacklist", chan->cid.cid_num, blacklist, sizeof (blacklist)))
		{
			if (option_verbose > 2)
				ast_log(LOG_NOTICE, "Blacklisted number %s found\n",chan->cid.cid_num);
			bl = 1;
		}
	}
	if (chan->cid.cid_name) {
		if (!ast_db_get ("blacklist", chan->cid.cid_name, blacklist, sizeof (blacklist))) 
		{
			if (option_verbose > 2)
				ast_log (LOG_NOTICE,"Blacklisted name \"%s\" found\n",chan->cid.cid_name);
			bl = 1;
		}
	}
	
	if (bl)
		ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);

	LOCAL_USER_REMOVE (u);
	return 0;
}

int unload_module (void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application (app);
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
