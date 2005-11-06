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
 * \brief striplsd: Strip trailing digits app
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"

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
	int maxbytes = 0;
	int stripcount = 0;
	int extlen = strlen(chan->exten);
	struct localuser *u;

	LOCAL_USER_ADD(u);

	maxbytes = sizeof(newexten) - 1;
	if (data) {
		stripcount = atoi(data);
	}
	if (!stripcount) {
		ast_log(LOG_DEBUG, "Ignoring, since number of digits to strip is 0\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	if (extlen > stripcount) {
		if (extlen - stripcount <= maxbytes) {
			maxbytes = extlen - stripcount;
		}
		strncpy(newexten, chan->exten, maxbytes);
	}
	strncpy(chan->exten, newexten, sizeof(chan->exten)-1);

	LOCAL_USER_REMOVE(u);

	return 0;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
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
