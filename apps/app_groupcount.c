/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Group Manipulation Applications
 *
 * Copyright (c) 2004 Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>

static char *tdesc = "Group Management Routines";

static char *app_group_count = "GetGroupCount";
static char *app_group_set = "SetGroup";
static char *app_group_check = "CheckGroup";

static char *group_count_synopsis = "GetGroupCount([groupname])";
static char *group_set_synopsis = "SetGroup([groupname])";
static char *group_check_synopsis = "CheckGroup(max)";

static char *group_count_descrip =
"GetGroupCount([group])\n"
"  Calculates the group count for the specified group, or uses\n"
"the current channel's group if not specifed (and non-empty).\n"
"Stores result in GROUPCOUNT.  Always returns 0.\n";

static char *group_set_descrip =
"SetGroup(group)\n"
"  Sets the channel group to the specified value.  Equivalent to\n"
"SetVar(GROUP=group).  Always returns 0.\n";

static char *group_check_descrip =
"CheckGroup(max)\n"
"  Checks that the current number of total channels in the\n"
"current channel's group does not exceed 'max'.  If the number\n"
"does not exceed 'max', we continue to the next step. If the\n"
"number does in fact exceed max, if priority n+101 exists, then\n"
"execution continues at that step, otherwise -1 is returned.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int group_get_count(char *group)
{
	/* XXX ast_channel_walk needs to be modified to
	       prevent a race in which after we return the channel
		   is no longer valid (or ast_channel_free can be modified
		   just as well) XXX */
	struct ast_channel *chan;
	int count = 0;
	char *test;
	if (group && strlen(group)) {
		chan = ast_channel_walk(NULL);
		while(chan) {
			test = pbx_builtin_getvar_helper(chan, "GROUP");
			if (!strcasecmp(test, group))
				count++;
			chan = ast_channel_walk(chan);
		}
	}
	return count;
}

static int group_count_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	int count;
	struct localuser *u;
	char *group=NULL;
	char ret[80];

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (data && strlen(data)) {
		group = (char *)data;
	} else {
		group = pbx_builtin_getvar_helper(chan, "GROUP");
	}
	count = group_get_count(group);
	snprintf(ret, sizeof(ret), "%d", count);
	pbx_builtin_setvar_helper(chan, "GROUPCOUNT", ret);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int group_set_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;

	LOCAL_USER_ADD(u);
	/* Check and parse arguments */
	if (data && strlen(data)) {
		pbx_builtin_setvar_helper(chan, "GROUP", (char *)data);
	} else
		ast_log(LOG_WARNING, "GroupSet requires an argument (group name)\n");

	LOCAL_USER_REMOVE(u);
	return res;
}

static int group_check_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	int max, count;
	struct localuser *u;

	LOCAL_USER_ADD(u);

	if (data && (sscanf((char *)data, "%i", &max) == 1) && (max > -1)) {	
		count = group_get_count(pbx_builtin_getvar_helper(chan, "GROUP"));
		if (count > max) {
			if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
				chan->priority += 100;
			else
				res = -1;
		}
	} else
		ast_log(LOG_WARNING, "GroupCheck requires a positive integer argument (max)\n");
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(app_group_count);
	res |= ast_unregister_application(app_group_set);
	res |= ast_unregister_application(app_group_check);
	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app_group_count, group_count_exec, group_count_synopsis, group_count_descrip);
	res |= ast_register_application(app_group_set, group_set_exec, group_set_synopsis, group_set_descrip);
	res |= ast_register_application(app_group_check, group_check_exec, group_check_synopsis, group_check_descrip);
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
