/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Time of day - Report the time of day
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
#include <asterisk/utils.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static char *tdesc = "ENUM Lookup";

static char *app = "EnumLookup";

static char *synopsis = "Lookup number in ENUM";

static char *descrip = 
"  EnumLookup(exten):  Looks up an extension via ENUM and sets\n"
"the variable 'ENUM'. For VoIP URIs this variable will \n"
"look like 'TECHNOLOGY/URI' with the appropriate technology.\n"
"Returns -1 on hangup, or 0 on completion regardless of whether the \n"
"lookup was successful. \n"
"Currently, the enumservices SIP, H323, IAX, IAX2 and TEL are recognized. \n"
"A good SIP, H323, IAX or IAX2 entry will result in normal priority handling, \n"
"whereas a good TEL entry will increase the priority by 51 (if existing).\n"
"If the lookup was *not* successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

#define ENUM_CONFIG "enum.conf"

static char h323driver[80] = "";
#define H323DRIVERDEFAULT "H323"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int enumlookup_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	char tech[80];
	char dest[80];
	char tmp[256];
	char *c,*t;
	struct localuser *u;
	if (!data || ast_strlen_zero(data)) {
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
		} else if (!strcasecmp(tech, "h323")) {
			c = dest;
			if (!strncmp(c, "h323:", 5))
				c += 5;
			snprintf(tmp, sizeof(tmp), "%s/%s", h323driver, c);
/* do a s!;.*!! on the H323 URI */
			t = strchr(c,';');
			if (t) 
				*t = 0;
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (!strcasecmp(tech, "iax")) {
			c = dest;
			if (!strncmp(c, "iax:", 4))
				c += 4;
			snprintf(tmp, sizeof(tmp), "IAX/%s", c);
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (!strcasecmp(tech, "iax2")) {
			c = dest;
			if (!strncmp(c, "iax2:", 5))
				c += 5;
			snprintf(tmp, sizeof(tmp), "IAX2/%s", c);
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (!strcasecmp(tech, "tel")) {
			c = dest;
			if (!strncmp(c, "tel:", 4))
				c += 4;

			if (c[0] != '+') {
				ast_log(LOG_NOTICE, "tel: uri must start with a \"+\" (got '%s')\n", c);
				res = 0;
			} else {
/* now copy over the number, skipping all non-digits and stop at ; or NULL */
				t = tmp;	
				while( *c && (*c != ';') && (t - tmp < (sizeof(tmp) - 1))) {
					if (isdigit(*c))
						*t++ = *c;
					c++;
				}
				*t = 0;
				pbx_builtin_setvar_helper(chan, "ENUM", tmp);
				ast_log(LOG_NOTICE, "tel: ENUM set to \"%s\"\n", tmp);
				if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 51, chan->cid.cid_num))
					chan->priority += 50;
				else
					res = 0;
			}
		} else if (!ast_strlen_zero(tech)) {
			ast_log(LOG_NOTICE, "Don't know how to handle technology '%s'\n", tech);
			res = 0;
		}
	}
	if (!res) {
		/* Look for a "busy" place */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
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
			strncpy(h323driver, H323DRIVERDEFAULT, sizeof(h323driver) - 1);
		} else {
			strncpy(h323driver, s, sizeof(h323driver) - 1);
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
	res = ast_register_application(app, enumlookup_exec, synopsis, descrip);
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

