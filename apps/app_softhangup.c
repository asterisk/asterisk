/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * SoftHangup application
 * 
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"

static char *synopsis = "Soft Hangup Application";

static char *tdesc = "Hangs up the requested channel";

static char *desc = "  SoftHangup(Technology/resource|options)\n"
"Hangs up the requested channel.  Always returns 0\n"
"- 'options' may contain the following letter:\n"
"     'a' : hang up all channels on a specified device instead of a single resource\n";

static char *app = "SoftHangup";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int softhangup_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	struct ast_channel *c=NULL;
	char *options, *cut, *cdata, *match;
	char name[AST_CHANNEL_NAME] = "";
	int all = 0;

	if (!data) {
                ast_log(LOG_WARNING, "SoftHangup requires an argument (Technology/resource)\n");
		return 0;
	}
	
	cdata = ast_strdupa(data);
	match = strsep(&cdata, "|");
	options = strsep(&cdata, "|");
	all = options && strchr(options,'a');
	LOCAL_USER_ADD(u);
	c = ast_channel_walk_locked(NULL);
	while (c) {
		strncpy(name, c->name, sizeof(name)-1);
		ast_mutex_unlock(&c->lock);
		/* XXX watch out, i think it is wrong to access c-> after unlocking! */
		if (all) {
			/* CAPI is set up like CAPI[foo/bar]/clcnt */ 
			if (!strcmp(c->type,"CAPI")) 
				cut = strrchr(name,'/');
			/* Basically everything else is Foo/Bar-Z */
			else
				cut = strchr(name,'-');
			/* Get rid of what we've cut */
			if (cut)
				*cut = 0;
		}
		if (!strcasecmp(name, match)) {
			ast_log(LOG_WARNING, "Soft hanging %s up.\n",c->name);
			ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
			if(!all)
				break;
		}
		c = ast_channel_walk_locked(c);
	}
	LOCAL_USER_REMOVE(u);

	return 0;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, softhangup_exec, synopsis, desc);
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
