/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Transfer a caller
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
#include <asterisk/module.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Transfer";

static char *app = "Transfer";

static char *synopsis = "Transfer caller to remote extension";

static char *descrip = 
"  Transfer([Tech/]dest):  Requests the remote caller be transferred\n"
"a given extension. Returns -1 on hangup, or 0 on completion\n"
"regardless of whether the transfer was successful.  If the transfer\n"
"was *not* supported or successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int transfer_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	int len;
	struct localuser *u;
	char *slash;
	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "Transfer requires an argument ([Tech/]destination)\n");
		res = 1;
	}
	if ((slash = strchr((char *)data, '/')) && (len = (slash - (char *)data))) {
		/* Allow execution only if the Tech/destination agrees with the type of the channel */
		if (strncasecmp(chan->type, (char *)data, len))
			return 0;
	}
	LOCAL_USER_ADD(u);
	if (!res) {
		res = ast_transfer(chan, data + strlen(chan->type) + 1);
	}
	if (!res) {
		/* Look for a "busy" place */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
	}
	if (res > 0)
		res = 0;
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
	return ast_register_application(app, transfer_exec, synopsis, descrip);
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
