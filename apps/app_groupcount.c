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
#include <asterisk/utils.h>

static char *tdesc = "Group Management Routines";

static char *app_group_count = "GetGroupCount";
static char *app_group_set = "SetGroup";
static char *app_group_check = "CheckGroup";

static char *group_count_synopsis = "GetGroupCount([groupname][@category])";
static char *group_set_synopsis = "SetGroup(groupname[@category])";
static char *group_check_synopsis = "CheckGroup(max[@category])";

static char *group_count_descrip =
"GetGroupCount([group][@category])\n"
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

#define DEFAULT_CATEGORY "GROUP"

static int group_get_count(char *group, char *category)
{
	struct ast_channel *chan;
	int count = 0;
	char *test;
	if (group && !ast_strlen_zero(group)) {
		chan = ast_channel_walk_locked(NULL);
		while(chan) {
			test = pbx_builtin_getvar_helper(chan, category);
			if (test && !strcasecmp(test, group))
				count++;
			ast_mutex_unlock(&chan->lock);
			chan = ast_channel_walk_locked(chan);
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
	char *cat = NULL;
	char ret[80]="";
	char tmp[256]="";

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (data && !ast_strlen_zero(data)) {
		strncpy(tmp, data, sizeof(tmp) - 1);
		group = tmp;
		cat = strchr(tmp, '@');
		if (cat) {
			*cat = '\0';
			cat++;
		}
	}
	if (cat)
		snprintf(ret, sizeof(ret), "GROUP_%s", cat);
	else
		strncpy(ret, DEFAULT_CATEGORY, sizeof(ret) - 1);

	if (!group || ast_strlen_zero(group)) {
		group = pbx_builtin_getvar_helper(chan, ret);
	}
	count = group_get_count(group, ret);
	snprintf(ret, sizeof(ret), "%d", count);
	pbx_builtin_setvar_helper(chan, "GROUPCOUNT", ret);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int group_set_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char ret[80] = "";
	char tmp[256] = "";
	char *cat=NULL, *group=NULL;

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (data && !ast_strlen_zero(data)) {
		strncpy(tmp, data, sizeof(tmp) - 1);
		group = tmp;
		cat = strchr(tmp, '@');
		if (cat) {
			*cat = '\0';
			cat++;
		}
	}
	if (cat)
		snprintf(ret, sizeof(ret), "GROUP_%s", cat);
	else
		strncpy(ret, DEFAULT_CATEGORY, sizeof(ret) - 1);

	if (group && !ast_strlen_zero(group)) {
		pbx_builtin_setvar_helper(chan, ret, group);
	} else
		ast_log(LOG_WARNING, "SetGroup requires an argument (group name)\n");

	LOCAL_USER_REMOVE(u);
	return res;
}

static int group_check_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	int max, count;
	struct localuser *u;
	char ret[80] = "";
	char tmp[256] = "";
	char *cat, *group;

	LOCAL_USER_ADD(u);

	if (data && !ast_strlen_zero(data)) {
		strncpy(tmp, data, sizeof(tmp) - 1);
		group = tmp;
		cat = strchr(tmp, '@');
		if (cat) {
			*cat = '\0';
			cat++;
		}
	 	if ((sscanf((char *)tmp, "%i", &max) == 1) && (max > -1)) {
			if (cat)
				snprintf(ret, sizeof(ret), "GROUP_%s", cat);
			else
				strncpy(ret, DEFAULT_CATEGORY, sizeof(ret) - 1);
			
			count = group_get_count(pbx_builtin_getvar_helper(chan, ret), ret);
			if (count > max) {
				if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
					chan->priority += 100;
				else
					res = -1;
			}
		} else
			ast_log(LOG_WARNING, "CheckGroup requires a positive integer argument (max)\n");
	} else
		ast_log(LOG_WARNING, "CheckGroup requires an argument(max)\n");
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
