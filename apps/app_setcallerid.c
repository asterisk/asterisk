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
 * \brief App to set callerid presentation
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
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
	int pres = -1;
	static int deprecated = 0;

	if (!deprecated) {
		deprecated = 1;
		ast_log(LOG_WARNING, "SetCallerPres is deprecated.  Please use Set(CALLERPRES()=%s) instead.\n", (char *)data);
	}
	pres = ast_parse_caller_presentation(data);

	if (pres < 0) {
		ast_log(LOG_WARNING, "'%s' is not a valid presentation (see 'show application SetCallerPres')\n",
			(char *) data);
		return 0;
	}
	
	chan->cid.cid_pres = pres;
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app2);
}

static int load_module(void)
{
	return ast_register_application(app2, setcallerid_pres_exec, synopsis2, descrip2);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Set CallerID Presentation Application");
