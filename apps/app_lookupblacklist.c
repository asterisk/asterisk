/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to lookup the callerid number, and see if it is blacklisted
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/image.h>
#include <asterisk/callerid.h>
#include <asterisk/astdb.h>
#include <string.h>
#include <stdlib.h>

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
	char old_cid[144] = "", *num, *name;
	char blacklist[1];
	char shrunknum[64] = "";
	struct localuser *u;
	int bl = 0;

	LOCAL_USER_ADD (u);
	if (chan->callerid)
	{
		strncpy (old_cid, chan->callerid, sizeof (old_cid) - 1);
		ast_callerid_parse (old_cid, &name, &num);
		if (num)
			strncpy (shrunknum, num, sizeof (shrunknum) - 1);
		else
			num = shrunknum;
		
		ast_shrink_phone_number (shrunknum);
		if (!ast_db_get ("blacklist", shrunknum, blacklist, sizeof (blacklist)))
		{
			if (option_verbose > 2)
				ast_log(LOG_NOTICE, "Blacklisted number %s found\n",shrunknum);
			bl = 1;
		}
		else if (!ast_db_get ("blacklist", name, blacklist, sizeof (blacklist)))
		{
			if (option_verbose > 2)
				ast_log (LOG_NOTICE,"Blacklisted name \"%s\" found\n",name);
			bl = 1;
		}
	}
	
	if (bl && ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
		chan->priority+=100;
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
