/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Caller*id name lookup - Look up the caller's name via DNS
 * 
 * Copyright (C) 1999-2004, Digium
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
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/module.h>
#include <asterisk/enum.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <pthread.h>


static char *tdesc = "TXTCIDName";

static char *app = "TXTCIDName";

static char *synopsis = "Lookup caller name from TXT record";

static char *descrip = 
"  TXTLookup(CallerID):  Looks up a Caller Name via DNS and sets\n"
"the variable 'TXTCIDNAME'. TXTCIDName will either be blank\n"
"or return the value found in the TXT record in DNS.\n" ;

#define ENUM_CONFIG "enum.conf"

static char h323driver[80];
#define H323DRIVERDEFAULT "H323"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int txtcidname_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	char tech[80];
	char txt[256] = "";
	char dest[80];

	struct localuser *u;
	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "TXTCIDName requires an argument (extension)\n");
		res = 1;
	}
	LOCAL_USER_ADD(u);
	if (!res) {
		res = ast_get_txt(chan, data, dest, sizeof(dest), tech, sizeof(tech), txt, sizeof(txt));
	}
	LOCAL_USER_REMOVE(u);
	/* Parse it out */
	if (res > 0) {
		if(strlen(txt) > 0)
		{
			pbx_builtin_setvar_helper(chan, "TXTCIDNAME", txt);
#if 0
	printf("TXTCIDNAME got '%s'\n", txt);
#endif
		}
	}
	if (!res) {
		/* Look for a "busy" place */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
			chan->priority += 100;
	} else if (res > 0)
		res = 0;
	return res;
}

static int load_config(void)
{
	struct ast_config *cfg;
	char *s;

	cfg = ast_load(ENUM_CONFIG);
	if (cfg) {
		if (!(s=ast_variable_retrieve(cfg, "general", "h323driver"))) {
			strcpy(h323driver, H323DRIVERDEFAULT);
		} else {
			strcpy(h323driver, s);
		}
		ast_destroy(cfg);
		return 0;
	}
	ast_log(LOG_NOTICE, "No ENUM Config file, using defaults\n");
	return 0;
}


int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	int res;
	res = ast_register_application(app, txtcidname_exec, synopsis, descrip);
	if (res)
		return(res);
	if ((res=load_config())) {
		return(res);
	}
	return(0);
}

int reload(void)
{
	return(load_config());
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

