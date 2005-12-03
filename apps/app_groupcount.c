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

/*! \file
 *
 * \brief Group Manipulation Applications
 *
 * \ingroup applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int group_count_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int count;
	struct localuser *u;
	char group[80] = "";
	char category[80] = "";
	char ret[80] = "";
	static int deprecation_warning = 0;

	LOCAL_USER_ADD(u);

	if (!deprecation_warning) {
	        ast_log(LOG_WARNING, "The GetGroupCount application has been deprecated, please use the GROUP_COUNT function.\n");
		deprecation_warning = 1;
	}

	ast_app_group_split_group(data, group, sizeof(group), category, sizeof(category));

	if (ast_strlen_zero(group)) {
		const char *grp = pbx_builtin_getvar_helper(chan, category);
		strncpy(group, grp, sizeof(group) - 1);
	}

	count = ast_app_group_get_count(group, category);
	snprintf(ret, sizeof(ret), "%d", count);
	pbx_builtin_setvar_helper(chan, "GROUPCOUNT", ret);

	LOCAL_USER_REMOVE(u);

	return res;
}

static int group_match_count_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int count;
	struct localuser *u;
	char group[80] = "";
	char category[80] = "";
	char ret[80] = "";
	static int deprecation_warning = 0;

	LOCAL_USER_ADD(u);

	if (!deprecation_warning) {
	        ast_log(LOG_WARNING, "The GetGroupMatchCount application has been deprecated, please use the GROUP_MATCH_COUNT function.\n");
		deprecation_warning = 1;
	}

	ast_app_group_split_group(data, group, sizeof(group), category, sizeof(category));

	if (!ast_strlen_zero(group)) {
		count = ast_app_group_match_get_count(group, category);
		snprintf(ret, sizeof(ret), "%d", count);
		pbx_builtin_setvar_helper(chan, "GROUPCOUNT", ret);
	}

	LOCAL_USER_REMOVE(u);

	return res;
}

static int group_set_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	static int deprecation_warning = 0;

	LOCAL_USER_ADD(u);
	
	if (!deprecation_warning) {
	        ast_log(LOG_WARNING, "The SetGroup application has been deprecated, please use the GROUP() function.\n");
		deprecation_warning = 1;
	}

	if (ast_app_group_set_channel(chan, data))
		ast_log(LOG_WARNING, "SetGroup requires an argument (group name)\n");

	LOCAL_USER_REMOVE(u);
	return res;
}

static int group_check_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int max, count;
	struct localuser *u;
	char limit[80]="";
	char category[80]="";
	static int deprecation_warning = 0;
	char *parse;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(max);
		AST_APP_ARG(options);
	);

	LOCAL_USER_ADD(u);

	if (!deprecation_warning) {
	        ast_log(LOG_WARNING, "The CheckGroup application has been deprecated, please use a combination of the GotoIf application and the GROUP_COUNT() function.\n");
		deprecation_warning = 1;
	}

	if (!(parse = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (ast_strlen_zero(args.max)) {
		ast_log(LOG_WARNING, "CheckGroup requires an argument(max[@category][|options])\n");
		return res;
	}

  	ast_app_group_split_group(args.max, limit, sizeof(limit), category, sizeof(category));

 	if ((sscanf(limit, "%d", &max) == 1) && (max > -1)) {
		count = ast_app_group_get_count(pbx_builtin_getvar_helper(chan, category), category);
		if (count > max) {
			pbx_builtin_setvar_helper(chan, "CHECKGROUPSTATUS", "OVERMAX");
			if (priority_jump || option_priority_jumping) {
				if (!ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101))
					res = -1;
			}
		} else
			pbx_builtin_setvar_helper(chan, "CHECKGROUPSTATUS", "OK");
	} else
		ast_log(LOG_WARNING, "CheckGroup requires a positive integer argument (max)\n");

	LOCAL_USER_REMOVE(u);
	return res;
}

static int group_show_channels(int fd, int argc, char *argv[])
{
#define FORMAT_STRING  "%-25s  %-20s  %-20s\n"

	struct ast_channel *c = NULL;
	int numchans = 0;
	struct ast_var_t *current;
	struct varshead *headp;
	regex_t regexbuf;
	int havepattern = 0;

	if (argc < 3 || argc > 4)
		return RESULT_SHOWUSAGE;
	
	if (argc == 4) {
		if (regcomp(&regexbuf, argv[3], REG_EXTENDED | REG_NOSUB))
			return RESULT_SHOWUSAGE;
		havepattern = 1;
	}

	ast_cli(fd, FORMAT_STRING, "Channel", "Group", "Category");
	while ( (c = ast_channel_walk_locked(c)) != NULL) {
		headp=&c->varshead;
		AST_LIST_TRAVERSE(headp,current,entries) {
			if (!strncmp(ast_var_name(current), GROUP_CATEGORY_PREFIX "_", strlen(GROUP_CATEGORY_PREFIX) + 1)) {
				if (!havepattern || !regexec(&regexbuf, ast_var_value(current), 0, NULL, 0)) {
					ast_cli(fd, FORMAT_STRING, c->name, ast_var_value(current),
						(ast_var_name(current) + strlen(GROUP_CATEGORY_PREFIX) + 1));
					numchans++;
				}
			} else if (!strcmp(ast_var_name(current), GROUP_CATEGORY_PREFIX)) {
				if (!havepattern || !regexec(&regexbuf, ast_var_value(current), 0, NULL, 0)) {
					ast_cli(fd, FORMAT_STRING, c->name, ast_var_value(current), "(default)");
					numchans++;
				}
			}
		}
		numchans++;
		ast_mutex_unlock(&c->lock);
	}

	if (havepattern)
		regfree(&regexbuf);

	ast_cli(fd, "%d active channel%s\n", numchans, (numchans != 1) ? "s" : "");
	return RESULT_SUCCESS;
#undef FORMAT_STRING
}

static char *tdesc = "Group Management Routines";

static char *app_group_count = "GetGroupCount";
static char *app_group_set = "SetGroup";
static char *app_group_check = "CheckGroup";
static char *app_group_match_count = "GetGroupMatchCount";

static char *group_count_synopsis = "Get the channel count of a group";
static char *group_set_synopsis = "Set the channel's group";
static char *group_check_synopsis = "Check the channel count of a group against a limit";
static char *group_match_count_synopsis = "Get the channel count of all groups that match a pattern";

static char *group_count_descrip =
"Usage: GetGroupCount([groupname][@category])\n"
"  Calculates the group count for the specified group, or uses\n"
"the current channel's group if not specifed (and non-empty).\n"
"Stores result in GROUPCOUNT. \n"
"This application has been deprecated, please use the function\n"
"GroupCount.\n";

static char *group_set_descrip =
"Usage: SetGroup(groupname[@category])\n"
"  Sets the channel group to the specified value.  Equivalent to\n"
"Set(GROUP=group).  Always returns 0.\n";

static char *group_check_descrip =
"Usage: CheckGroup(max[@category][|options])\n"
"  Checks that the current number of total channels in the\n"
"current channel's group does not exceed 'max'.  If the number\n"
"does not exceed 'max', we continue to the next step. \n"
" The option string may contain zero of the following character:\n"
"	'j' -- jump to n+101 priority if the number does in fact exceed max,\n"
"              and priority n+101 exists. Execuation then continues at that\n"
"	       step, otherwise -1 is returned.\n"
" This application sets the following channel variable upon successful completion:\n"
"	CHECKGROUPSTATUS  The status of the check that the current channel's\n"
"			  group does not exceed 'max'. It's value is one of\n"
"		OK | OVERMAX \n";	

static char *group_match_count_descrip =
"Usage: GetGroupMatchCount(groupmatch[@category])\n"
"  Calculates the group count for all groups that match the specified\n"
"pattern. Uses standard regular expression matching (see regex(7)).\n"
"Stores result in GROUPCOUNT.  Always returns 0.\n"
"This application has been deprecated, please use the function\n"
"GroupMatchCount.\n";

static char show_channels_usage[] = 
"Usage: group show channels [pattern]\n"
"       Lists all currently active channels with channel group(s) specified.\n       Optional regular expression pattern is matched to group names for each channel.\n";

static struct ast_cli_entry  cli_show_channels =
	{ { "group", "show", "channels", NULL }, group_show_channels, "Show active channels with group(s)", show_channels_usage};

int unload_module(void)
{
	int res;

	res = ast_cli_unregister(&cli_show_channels);
	res |= ast_unregister_application(app_group_count);
	res |= ast_unregister_application(app_group_set);
	res |= ast_unregister_application(app_group_check);
	res |= ast_unregister_application(app_group_match_count);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_register_application(app_group_count, group_count_exec, group_count_synopsis, group_count_descrip);
	res |= ast_register_application(app_group_set, group_set_exec, group_set_synopsis, group_set_descrip);
	res |= ast_register_application(app_group_check, group_check_exec, group_check_synopsis, group_check_descrip);
	res |= ast_register_application(app_group_match_count, group_match_count_exec, group_match_count_synopsis, group_match_count_descrip);
	res |= ast_cli_register(&cli_show_channels);
	
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
