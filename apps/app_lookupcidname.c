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
 * \brief App to set callerid name from database, based on directory number
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
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

static char *app = "LookupCIDName";

static char *synopsis = "Look up CallerID Name from local database";

static char *descrip =
  "  LookupCIDName: Looks up the Caller*ID number on the active\n"
  "channel in the Asterisk database (family 'cidname') and sets the\n"
  "Caller*ID name.  Does nothing if no Caller*ID was received on the\n"
  "channel.  This is useful if you do not subscribe to Caller*ID\n"
  "name delivery, or if you want to change the names on some incoming\n"
  "calls.\n\n"
  "LookupCIDName is deprecated.  Please use ${DB(cidname/${CALLERID(num)})}\n"
  "instead.\n";


static int lookupcidname_exec (struct ast_channel *chan, void *data)
{
	char dbname[64];
	struct ast_module_user *u;
	static int dep_warning = 0;

	u = ast_module_user_add(chan);
	if (!dep_warning) {
		dep_warning = 1;
		ast_log(LOG_WARNING, "LookupCIDName is deprecated.  Please use ${DB(cidname/${CALLERID(num)})} instead.\n");
	}
	if (chan->cid.cid_num) {
		if (!ast_db_get ("cidname", chan->cid.cid_num, dbname, sizeof (dbname))) {
			ast_set_callerid (chan, NULL, dbname, NULL);
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "Changed Caller*ID name to %s\n",
					     dbname);
		}
	}
	ast_module_user_remove(u);

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application (app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	return ast_register_application (app, lookupcidname_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Look up CallerID Name from local database");
