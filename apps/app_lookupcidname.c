/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to set callerid name from database, based on directory number
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

static char *tdesc = "Look up CallerID Name from local database";

static char *app = "LookupCIDName";

static char *synopsis = "Look up CallerID Name from local database";

static char *descrip =
  "  LookupCIDName: Looks up the Caller*ID number on the active\n"
  "channel in the Asterisk database (family 'cidname') and sets the\n"
  "Caller*ID name.  Does nothing if no Caller*ID was received on the\n"
  "channel.  This is useful if you do not subscribe to Caller*ID\n"
  "name delivery, or if you want to change the names on some incoming\n"
  "calls.  Always returns 0.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int
lookupcidname_exec (struct ast_channel *chan, void *data)
{
  char old_cid[144] = "", *num, *name;
  char new_cid[144];
  char dbname[64];
  char shrunknum[64] = "";
  struct localuser *u;

  LOCAL_USER_ADD (u);
  if (chan->callerid)
    {
      strncpy (old_cid, chan->callerid, sizeof (old_cid) - 1);
      ast_callerid_parse (old_cid, &name, &num);	/* this destroys the original string */
      if (num)			/* It's possible to get an empty number */
	strncpy (shrunknum, num, sizeof (shrunknum) - 1);
      else
	num = shrunknum;
      ast_shrink_phone_number (shrunknum);
      if (!ast_db_get ("cidname", shrunknum, dbname, sizeof (dbname)))
	{
	  snprintf (new_cid, sizeof (new_cid), "\"%s\" <%s>", dbname, num);
	  ast_set_callerid (chan, new_cid, 0);
	  if (option_verbose > 2)
	    ast_verbose (VERBOSE_PREFIX_3 "Changed Caller*ID to %s\n",
			 new_cid);
	}

    }
  LOCAL_USER_REMOVE (u);
  return 0;
}

int
unload_module (void)
{
  STANDARD_HANGUP_LOCALUSERS;
  return ast_unregister_application (app);
}

int
load_module (void)
{
  return ast_register_application (app, lookupcidname_exec, synopsis,
				   descrip);
}

char *
description (void)
{
  return tdesc;
}

int
usecount (void)
{
  int res;
  STANDARD_USECOUNT (res);
  return res;
}

char *
key ()
{
  return ASTERISK_GPL_KEY;
}
