/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Eval application
 *
 * Copyright (c) 2004 - 2005, Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_eval__v001@the-tilghman.com>
 *
 * $Id$
 *
 * This code is released by the author with no restrictions on usage.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"

/* Maximum length of any variable */
#define MAXRESULT	1024

static char *tdesc = "Reevaluates strings";

static char *app_eval = "Eval";

static char *eval_synopsis = "Evaluates a string";

static char *eval_descrip =
"Usage: Eval(newvar=somestring)\n"
"  Normally Asterisk evaluates variables inline.  But what if you want to\n"
"store variable offsets in a database, to be evaluated later?  Eval is\n"
"the answer, by allowing a string to be evaluated twice in the dialplan,\n"
"the first time as part of the normal dialplan, and the second using Eval.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int eval_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *s, *newvar=NULL, tmp[MAXRESULT];

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (data) {
		s = ast_strdupa((char *)data);
		if (s) {
			newvar = strsep(&s, "=");
			if (newvar && (newvar[0] != '\0')) {
				memset(tmp, 0, MAXRESULT);
				pbx_substitute_variables_helper(chan, s, tmp, MAXRESULT - 1);
				pbx_builtin_setvar_helper(chan, newvar, tmp);
			}
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
			res = -1;
		}
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app_eval);
}

int load_module(void)
{
	return ast_register_application(app_eval, eval_exec, eval_synopsis, eval_descrip);
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
