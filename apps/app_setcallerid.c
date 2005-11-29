/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to set callerid
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
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/image.h>
#include <asterisk/callerid.h>
#include <string.h>
#include <stdlib.h>

static char *app2 = "SetCallerPres";

static char *synopsis2 = "Set CallerID Presentation";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct {
	int val;
	char *name;
} preses[] = {
	{  AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED, "allowed_not_screened" },
	{  AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN, "allowed_passed_screen" },
	{  AST_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN, "allowed_failed_screen" },
	{  AST_PRES_ALLOWED_NETWORK_NUMBER, "allowed" },
	{  AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED	, "prohib_not_screened" },
	{  AST_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN, "prohib_passed_screen" },
	{  AST_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN, "prohib_failed_screen" },
	{  AST_PRES_PROHIB_NETWORK_NUMBER, "prohib" },
	{  AST_PRES_NUMBER_NOT_AVAILABLE, "unavailable" },
};

static char *descrip2 = 
"  SetCallerPres(presentation): Set Caller*ID presentation on\n"
"a call to a new value.  Sets ANI as well if a flag is used.\n"
"Always returns 0.  Valid presentations are:\n"
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
	int res = 0;
	char tmp[256] = "";
	struct localuser *u;
	int x;
	char *opts;
	int pres = -1;
	if (data)
		strncpy(tmp, (char *)data, sizeof(tmp) - 1);
	opts = strchr(tmp, '|');
	if (opts) {
		*opts = '\0';
		opts++;
	}
	for (x=0;x<sizeof(preses) / sizeof(preses[0]);x++) {
		if (!strcasecmp(preses[x].name, tmp)) {
			pres = preses[x].val;
			break;
		}
	}
	if (pres < 0) {
		ast_log(LOG_WARNING, "'%s' is not a valid presentation (see 'show application SetCallerPres')\n", tmp);
		return 0;
	}
	LOCAL_USER_ADD(u);
	chan->callingpres = pres;
	LOCAL_USER_REMOVE(u);
	return res;
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
	struct localuser *u;
	char *opt;
	int anitoo = 0;
	if (data)
		strncpy(tmp, (char *)data, sizeof(tmp) - 1);
	opt = strchr(tmp, '|');
	if (opt) {
		*opt = '\0';
		opt++;
		if (*opt == 'a')
			anitoo = 1;
	}
	LOCAL_USER_ADD(u);
	ast_set_callerid(chan, strlen(tmp) ? tmp : NULL, anitoo);
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
