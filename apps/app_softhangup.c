/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * SoftHangup application
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <pthread.h>


static char *synopsis = "Soft Hangup Application";

static char *tdesc = "Hangs up the requested channel";

static char *desc = "  SoftHangup(Technology/resource)\n"
"Hangs up the requested channel.  Always returns 0\n";

static char *app = "SoftHangup";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int softhangup_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	struct ast_channel *c=NULL;
	if (!data) {
                ast_log(LOG_WARNING, "SoftHangup requires an argument (Technology/resource)\n");
		return 0;
	}
	LOCAL_USER_ADD(u);
	c = ast_channel_walk_locked(NULL);
	while (c) {
		if (!strcasecmp(c->name, data)) {
			ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
			ast_mutex_unlock(&c->lock);
			break;
		}
		ast_mutex_unlock(&c->lock);
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
