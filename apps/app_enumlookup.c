/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Time of day - Report the time of day
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
#include <asterisk/enum.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <pthread.h>


static char *tdesc = "Date and Time";

static char *app = "EnumLookup";

static char *synopsis = "Say the date and time";

static char *descrip = 
"  EnumLookup(exten):  Looks up an extension via ENUM and sets\n"
"the variable 'ENUM'.  Returns -1 on hangup, or 0 on completion\n"
"regardless of whether the lookup was successful.  If the lookup\n"
"was *not* successful and there exists a priority n + 101, then\n"
"that priority will be taken next.\n" ;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int enumlookup_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	char tech[80];
	char dest[80];
	char tmp[256];
	char *c;
	struct localuser *u;
	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "EnumLookup requires an argument (extension)\n");
		res = 1;
	}
	LOCAL_USER_ADD(u);
	if (!res) {
		res = ast_get_enum(chan, data, dest, sizeof(dest), tech, sizeof(tech));
		printf("ENUM got '%d'\n", res);
	}
	LOCAL_USER_REMOVE(u);
	/* Parse it out */
	if (res > 0) {
		if (!strcasecmp(tech, "SIP")) {
			c = dest;
			if (!strncmp(c, "sip:", 4))
				c += 4;
			snprintf(tmp, sizeof(tmp), "SIP/%s", c);
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (strlen(tech)) {
			ast_log(LOG_NOTICE, "Don't know how to handle technology '%s'\n", tech);
			res = 0;
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

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, enumlookup_exec, synopsis, descrip);
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
