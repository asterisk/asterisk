/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * IVR Demo application
 * 
 * Copyright (C) 2005, Mark Spencer
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <asterisk/app.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static char *tdesc = "IVR Demo Application";
static char *app = "IVRDemo";
static char *synopsis = 
"  This is a skeleton application that shows you the basic structure to create your\n"
"own asterisk applications and demonstrates the IVR demo.\n";

static int ivr_demo_func(struct ast_channel *chan, void *data)
{
	ast_verbose("IVR Demo, data is %s!\n", (char *)data);
	return 0;
}

AST_IVR_DECLARE_MENU(ivr_submenu, "IVR Demo Sub Menu", 0, 
{
	{ "s", AST_ACTION_BACKGROUND, "demo-abouttotry" },
	{ "s", AST_ACTION_WAITOPTION },
	{ "1", AST_ACTION_PLAYBACK, "digits/1" },
	{ "1", AST_ACTION_PLAYBACK, "digits/1" },
	{ "1", AST_ACTION_RESTART },
	{ "2", AST_ACTION_PLAYLIST, "digits/2;digits/3" },
	{ "3", AST_ACTION_CALLBACK, ivr_demo_func },
	{ "4", AST_ACTION_TRANSFER, "demo|s|1" },
	{ "*", AST_ACTION_REPEAT },
	{ "#", AST_ACTION_UPONE  },
	{ NULL }
});

AST_IVR_DECLARE_MENU(ivr_demo, "IVR Demo Main Menu", 0, 
{
	{ "s", AST_ACTION_BACKGROUND, "demo-congrats" },
	{ "g", AST_ACTION_BACKGROUND, "demo-instruct" },
	{ "g", AST_ACTION_WAITOPTION },
	{ "1", AST_ACTION_PLAYBACK, "digits/1" },
	{ "1", AST_ACTION_RESTART },
	{ "2", AST_ACTION_MENU, &ivr_submenu },
	{ "2", AST_ACTION_RESTART },
	{ "i", AST_ACTION_PLAYBACK, "invalid" },
	{ "i", AST_ACTION_REPEAT, (void *)(unsigned long)2 },
	{ "#", AST_ACTION_EXIT },
	{ NULL },
});

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int skel_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	if (!data) {
		ast_log(LOG_WARNING, "skel requires an argument (filename)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	/* Do our thing here */
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res)
		res = ast_ivr_menu_run(chan, &ivr_demo, data);
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
	return ast_register_application(app, skel_exec, tdesc, synopsis);
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
