/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Verbose application
 * 
 * Copyright (c) 2004 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_verbose_v001@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <asterisk/options.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>


static char *tdesc = "Send verbose output";

static char *app_verbose = "Verbose";

static char *verbose_synopsis = "Send arbitrary text to verbose output";

static char *verbose_descrip =
"Verbose([<level>|]<message>)\n"
"  level must be an integer value.  If not specified, defaults to 0."
"  Always returns 0.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int verbose_exec(struct ast_channel *chan, void *data)
{
	char *vtext;
	int vsize;

	if (data) {
		vtext = ast_strdupa((char *)data);
		if (vtext) {
			char *tmp = strsep(&vtext, "|,");
			if (vtext) {
				if (sscanf(tmp, "%d", &vsize) != 1) {
					vsize = 0;
					ast_log(LOG_WARNING, "'%s' is not a verboser number\n", vtext);
				}
			} else {
				vtext = tmp;
				vsize = 0;
			}
			if (option_verbose >= vsize) {
				switch (vsize) {
				case 1:
					ast_verbose(VERBOSE_PREFIX_1 "%s\n", vtext);
					break;
				case 2:
					ast_verbose(VERBOSE_PREFIX_2 "%s\n", vtext);
					break;
				case 3:
					ast_verbose(VERBOSE_PREFIX_3 "%s\n", vtext);
					break;
				default:
					ast_verbose(VERBOSE_PREFIX_4 "%s\n", vtext);
				}
			}
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
		}
	}

	return 0;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app_verbose);
}

int load_module(void)
{
	return ast_register_application(app_verbose, verbose_exec, verbose_synopsis, verbose_descrip);
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
