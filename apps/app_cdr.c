/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Applictions connected with CDR engine
 * 
 * Copyright (C) 2003, Digium
 *
 * Martin Pycko <martinp@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include <stdlib.h>


static char *tdesc = "Make sure asterisk doesn't save CDR for a certain call";

static char *nocdr_descrip = "NoCDR(): makes sure there won't be any CDR written for a certain call";
static char *nocdr_app = "NoCDR";
static char *nocdr_synopsis = "Make sure asterisk doesn't save CDR for a certain call";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int nocdr_exec(struct ast_channel *chan, void *data)
{
	if (chan->cdr) {
		ast_cdr_free(chan->cdr);
		chan->cdr = NULL;
	}
	return 0;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(nocdr_app);
}

int load_module(void)
{
	return ast_register_application(nocdr_app, nocdr_exec, nocdr_synopsis, nocdr_descrip);
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
