/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Applictions connected with CDR engine
 * 
 * Copyright (C) 2003, Digium
 *
 * Justin Huff <jjhuff@mspin.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/channel.h>
#include <asterisk/cdr.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/logger.h>

#include <stdlib.h>
#include <string.h>


static char *tdesc = "CDR user field apps";

static char *setcdruserfield_descrip = 
               "[Synopsis]\n"
               "SetCDRUserField(value)\n\n"
               "[Description]\n"
               "SetCDRUserField(value): Set the CDR 'user field' to value\n"
               "       The Call Data Record (CDR) user field is an extra field you\n"
               "       can use for data not stored anywhere else in the record.\n"
               "       CDR records can be used for billing or storing other arbitrary data\n"
               "       (I.E. telephone survey responses)\n"
               "       Also see AppendCDRUserField().\n"
               "       Always returns 0\n";

		
static char *setcdruserfield_app = "SetCDRUserField";
static char *setcdruserfield_synopsis = "Set the CDR user field";

static char *appendcdruserfield_descrip = 
               "[Synopsis]\n"
               "AppendCDRUserField(value)\n\n"
               "[Description]\n"
               "AppendCDRUserField(value): Append value to the CDR user field\n"
               "       The Call Data Record (CDR) user field is an extra field you\n"
               "       can use for data not stored anywhere else in the record.\n"
               "       CDR records can be used for billing or storing other arbitrary data\n"
               "       (I.E. telephone survey responses)\n"
               "       Also see SetCDRUserField().\n"
               "       Always returns 0\n";
		
static char *appendcdruserfield_app = "AppendCDRUserField";
static char *appendcdruserfield_synopsis = "Append to the CDR user field";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int setcdruserfield_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int res = 0;
	
	LOCAL_USER_ADD(u)
	if (chan->cdr && data) 
	{
		ast_cdr_setuserfield(chan, (char*)data);
	}

	LOCAL_USER_REMOVE(u);
	
	return res;
}

static int appendcdruserfield_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int res = 0;
	
	LOCAL_USER_ADD(u)
	if (chan->cdr && data) 
	{
		ast_cdr_appenduserfield(chan, (char*)data);
	}

	LOCAL_USER_REMOVE(u);
	
	return res;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(setcdruserfield_app);
	res |= ast_unregister_application(appendcdruserfield_app);
	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(setcdruserfield_app, setcdruserfield_exec, setcdruserfield_synopsis, setcdruserfield_descrip);
	res |= ast_register_application(appendcdruserfield_app, appendcdruserfield_exec, appendcdruserfield_synopsis, appendcdruserfield_descrip);

	return res;
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
