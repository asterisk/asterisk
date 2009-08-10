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
 * \brief App to set callerid
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
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/callerid.h"

static char *app2 = "SetCallerPres";

static char *synopsis2 = "Set CallerID Presentation";


static char *descrip2 = 
"  SetCallerPres(presentation): Set Caller*ID presentation on a call.\n"
"  Valid presentations are:\n"
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
	struct ast_module_user *u;
	int pres = -1;

	u = ast_module_user_add(chan);

	/* For interface consistency, permit the argument to be specified as a number */
	if (sscanf(data, "%30d", &pres) != 1 || pres < 0 || pres > 255 || (pres & 0x9c)) {
		pres = ast_parse_caller_presentation(data);
	}

	if (pres < 0) {
		ast_log(LOG_WARNING, "'%s' is not a valid presentation (see 'show application SetCallerPres')\n",
			(char *) data);
		ast_module_user_remove(u);
		return 0;
	}
	
	chan->cid.cid_pres = pres;
	ast_module_user_remove(u);
	return 0;
}

static char *app = "SetCallerID";

static char *synopsis = "Set CallerID";

static char *descrip = 
"  SetCallerID(clid[|a]): Set Caller*ID on a call to a new\n"
"value.  Sets ANI as well if a flag is used. \n";

static int setcallerid_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *tmp = NULL;
	char name[256];
	char num[256];
	struct ast_module_user *u;
	char *opt;
	int anitoo = 0;
	static int dep_warning = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SetCallerID requires an argument!\n");
		return 0;
	}
	
	u = ast_module_user_add(chan);

	if (!dep_warning) {
		dep_warning = 1;
		ast_log(LOG_WARNING, "SetCallerID is deprecated.  Please use Set(CALLERID(all)=...) or Set(CALLERID(ani)=...) instead.\n");
	}

	tmp = ast_strdupa(data);
	
	opt = strchr(tmp, '|');
	if (opt) {
		*opt = '\0';
		opt++;
		if (*opt == 'a')
			anitoo = 1;
	}
	
	ast_callerid_split(tmp, name, sizeof(name), num, sizeof(num));
	ast_set_callerid(chan, num, name, anitoo ? num : NULL);

	ast_module_user_remove(u);
	
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app2);
	res |= ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;
	
	res = ast_register_application(app2, setcallerid_pres_exec, synopsis2, descrip2);
	res |= ast_register_application(app, setcallerid_exec, synopsis, descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Set CallerID Application");
