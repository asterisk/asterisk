/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Random application
 * 
 * Copyright (c) 2003 - 2005 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <asterisk__app_random__20040111@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage or distribution.
 *
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


static char *tdesc = "Random goto";

static char *app_random = "Random";

static char *random_synopsis = "Conditionally branches, based upon a probability";

static char *random_descrip =
"Random([probability]:[[context|]extension|]priority)\n"
"  probability := INTEGER in the range 1 to 100\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int random_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;

	char *s;
	char *exten, *pri, *context;
	char *prob;
	int probint, priorityint;

	if (!data) {
		ast_log(LOG_WARNING, "Random requires an argument ([probability]:[[context|]extension|]priority)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	s = ast_strdupa((void *) data);

	prob = strsep(&s,":");
	if ((!prob) || (sscanf(prob, "%d", &probint) != 1))
		probint = 0;

	if ((random() % 100) + probint > 100) {
		context = strsep(&s, "|");
		exten = strsep(&s, "|");
		if (!exten) {
			/* Only a priority */
			pri = context;
			exten = NULL;
			context = NULL;
		} else {
			pri = strsep(&s, "|");
			if (!pri) {
				pri = exten;
				exten = context;
				context = NULL;
			}
		}
		if (!pri) {
			ast_log(LOG_WARNING, "No label specified\n");
			LOCAL_USER_REMOVE(u);
			return -1;
		} else if (sscanf(pri, "%d", &priorityint) != 1) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0\n", pri);
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		/* At this point we have a priority and */
		/* maybe an extension and a context     */
		chan->priority = priorityint - 1;
		if (exten && strcasecmp(exten, "BYEXTENSION"))
			strncpy(chan->exten, exten, sizeof(chan->exten)-1);
		if (context)
			strncpy(chan->context, context, sizeof(chan->context)-1);
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Random branches to (%s,%s,%d)\n",
				chan->context,chan->exten, chan->priority+1);
		LOCAL_USER_REMOVE(u);
	}
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app_random);
}

int load_module(void)
{
	srandom((unsigned int)getpid() + (unsigned int)time(NULL));
	return ast_register_application(app_random, random_exec, random_synopsis, random_descrip);
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
