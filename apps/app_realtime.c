/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to transmit a URL
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/options.h>
#include <asterisk/pbx.h>
#include <asterisk/config.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#define next_one(var) var = var->next
#define crop_data(str) { *(str) = '\0' ; (str)++; }


static char *tdesc = "Realtime Data Lookup";
static char *app = "RealTime";
static char *synopsis = "Realtime Data Lookup";
static char *USAGE = "RealTime(<family>|<colmatch>|<value>[|<prefix>])";
static char *desc = "Use the RealTime config handler system to read data into channel variables.\n"
"RealTime(<family>|<colmatch>|<value>[|<prefix>])\n\n"
"All unique column names will be set as channel variables with optional prefix to the name.\n"
"e.g. prefix of 'var_' would make the column 'name' become the variable ${var_name}\n\n";




STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int realtime_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	struct ast_variable *var, *itt;
	char *family=NULL, *colmatch=NULL, *value=NULL, *prefix=NULL, *vname=NULL;
	size_t len;

	if (!data) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		return -1;
	}
	LOCAL_USER_ADD(u);
	if ((family = ast_strdupa(data))) {
		if ((colmatch = strchr(family,'|'))) {
			crop_data(colmatch);
			if ((value = strchr(colmatch,'|'))) {
				crop_data(value);
				if ((prefix = strchr(value,'|')))
					crop_data(prefix);
			}
		}
	}
	if (! (family && value && colmatch) ) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		res = -1;
	} else {
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4"Realtime Lookup: family:'%s' colmatch:'%s' value:'%s'\n",family,colmatch,value);
		if ((var = ast_load_realtime(family, colmatch, value))) {
			for (itt = var; itt; itt = itt->next) {
				if(prefix) {
					len = strlen(prefix) + strlen(itt->name) + 2;
					vname = alloca(len);
					snprintf(vname,len,"%s%s",prefix,itt->name);
					
				} else 
					vname = itt->name;

				pbx_builtin_setvar_helper(chan, vname, itt->value);
			}
			ast_destroy_realtime(var);
		} else if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4"No Realtime Matches Found.\n");
	}
	
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, realtime_exec, synopsis, desc);
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

