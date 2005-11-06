/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Oliver Daudey <traveler@xs4all.nl>
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
 * \brief App to set callerid number
 * 
 * \ingroup applications
 */
 
#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/callerid.h"
#include "asterisk/utils.h"

static char *tdesc = "Set CallerID Number";

static char *app = "SetCIDNum";

static char *synopsis = "Set CallerID Number";

static char *descrip = 
"  SetCIDNum(cnum[|a]): Set Caller*ID Number on a call to a new\n"
"value, while preserving the original Caller*ID name.  This is\n"
"useful for providing additional information to the called\n"
"party. Sets ANI as well if a flag is used.  Always returns 0\n"
"SetCIDNum has been deprecated in favor of the function\n"
"CALLERID(number)\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int setcallerid_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *opt;
	int anitoo = 0;
	char *tmp = NULL;
	static int deprecation_warning = 0;

	LOCAL_USER_ADD(u);
	
	if (!deprecation_warning) {
		ast_log(LOG_WARNING, "SetCIDNum is deprecated, please use Set(CALLERID(number)=value) instead.\n");
		deprecation_warning = 1;
	}

	if (data)
		tmp = ast_strdupa(data);
	else
		tmp = "";
	
	opt = strchr(tmp, '|');
	if (opt) {
		*opt = '\0';
		opt++;
		if (*opt == 'a')
			anitoo = 1;
	}
	
	ast_set_callerid(chan, tmp, NULL, anitoo ? tmp : NULL);

	LOCAL_USER_REMOVE(u);
	
	return res;
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
	return ast_register_application(app, setcallerid_exec, synopsis, descrip);
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
