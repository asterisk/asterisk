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

/*
 *
 * App to set callerid
 * 
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

static char *app2 = "SetCallerPres";

static char *synopsis2 = "Set CallerID Presentation";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *descrip2 = 
"  SetCallerPres(presentation): Set Caller*ID presentation on a call.\n"
"  Always returns 0.  Valid presentations are:\n"
"\n"
"      allowed_not_screened    : Presentation Allowed, Not Screened\n"
"      allowed_passed_screen   : Presentation Allowed, Passed Screen\n" 
"      allowed_failed_screen   : Presentation Allowed, Failed Screen\n" 
"      allowed                 : Presentation Allowed, Network Number\n"
"      prohib_not_screened     : Presentation Prohibited, Not Screened\n" 
"      prohib_passed_screen    : Presentation Prohibited, Passed Screen\n"
"      prohib_failed_screen    : Presentation Prohibited, Failed Screen\n"
"      prohib                  : Presentation Prohibited, Network Number\n"
"      unavailable             : Number Unavailable\n"
"\n"
;

static int setcallerid_pres_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int pres = -1;

	pres = ast_parse_caller_presentation(data);

	if (pres < 0) {
		ast_log(LOG_WARNING, "'%s' is not a valid presentation (see 'show application SetCallerPres')\n",
			(char *) data);
		return 0;
	}

	LOCAL_USER_ADD(u);
	chan->cid.cid_pres = pres;
	LOCAL_USER_REMOVE(u);
	return 0;
}



static char *tdesc = "Set CallerID Application";

static char *app = "SetCallerID";

static char *synopsis = "Set CallerID";

static char *descrip = 
"  SetCallerID(clid[|a]): Set Caller*ID on a call to a new\n"
"value.  Sets ANI as well if a flag is used.  Always returns 0\n";

static int setcallerid_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char tmp[256] = "";
	char name[256];
	char num[256];
	struct localuser *u;
	char *opt;
	int anitoo = 0;
	if (data)
		ast_copy_string(tmp, (char *)data, sizeof(tmp));
	opt = strchr(tmp, '|');
	if (opt) {
		*opt = '\0';
		opt++;
		if (*opt == 'a')
			anitoo = 1;
	}
	LOCAL_USER_ADD(u);
	ast_callerid_split(tmp, name, sizeof(name), num, sizeof(num));
	ast_set_callerid(chan, num, name, anitoo ? num : NULL);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_unregister_application(app2);
	return ast_unregister_application(app);
}

int load_module(void)
{
	ast_register_application(app2, setcallerid_pres_exec, synopsis2, descrip2);
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
