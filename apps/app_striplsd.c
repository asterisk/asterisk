/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Skeleton application
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Strip trailing digits";

static char *descrip =
"  StripLSD(count): Strips the trailing  'count'  digits  from  the  channel's\n"
"associated extension. For example, the  number  5551212 when stripped with a\n"
"count of 4 would be changed to 555.  This app always returns 0, and the PBX\n"
"will continue processing at the next priority for the *new* extension.\n"
"  So, for  example, if  priority 3 of 5551212  is  StripLSD 4, the next step\n"
"executed will be priority 4 of 555.  If you switch into an  extension which\n"
"has no first step, the PBX will treat it as though the user dialed an\n"
"invalid extension.\n";

static char *app = "StripLSD";

static char *synopsis = "Strip Least Significant Digits";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int striplsd_exec(struct ast_channel *chan, void *data)
{
  char newexten[AST_MAX_EXTENSION] = "";
  if (!data || !atoi(data)) {
    ast_log(LOG_DEBUG, "Ignoring, since number of digits to strip is 0\n");
    return 0;
  }
  if (strlen(chan->exten) > atoi(data)) {
    strncpy(newexten, chan->exten, strlen(chan->exten)-atoi(data));
  }
  strncpy(chan->exten, newexten, sizeof(chan->exten)-1);
  return 0;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, striplsd_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
