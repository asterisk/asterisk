/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Channel group related dialplan functions
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

/* ASTERISK_FILE_VERSION(__FILE__, "$Revision$") */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static char *group_count_function_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	int count;
	char group[80] = "";
	char category[80] = "";
	char *grp;

	ast_app_group_split_group(data, group, sizeof(group), category, sizeof(category));

	if (ast_strlen_zero(group)) {
		if ((grp = pbx_builtin_getvar_helper(chan, category)))
			ast_copy_string(group, grp, sizeof(group));
		else
			ast_log(LOG_NOTICE, "No group could be found for channel '%s'\n", chan->name);	
	}

	count = ast_app_group_get_count(group, category);
	snprintf(buf, len, "%d", count);

	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function group_count_function = {
	.name = "GROUP_COUNT",
	.syntax = "GROUP_COUNT([groupname][@category])",
	.synopsis = "Counts the number of channels in the specified group",
	.desc = "Calculates the group count for the specified group, or uses the\n"
	"channel's current group if not specifed (and non-empty).\n",
	.read = group_count_function_read,
};

static char *group_match_count_function_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	int count;
	char group[80] = "";
	char category[80] = "";

	ast_app_group_split_group(data, group, sizeof(group), category, sizeof(category));

	if (!ast_strlen_zero(group)) {
		count = ast_app_group_match_get_count(group, category);
		snprintf(buf, len, "%d", count);
	}

	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function group_match_count_function = {
	.name = "GROUP_MATCH_COUNT",
	.syntax = "GROUP_MATCH_COUNT(groupmatch[@category])",
	.synopsis = "Counts the number of channels in the groups matching the specified pattern",
	.desc = "Calculates the group count for all groups that match the specified pattern.\n"
	"Uses standard regular expression matching (see regex(7)).\n",
	.read = group_match_count_function_read,
	.write = NULL,
};

static char *group_function_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	char varname[256];
	char *group;

	if (!ast_strlen_zero(data)) {
		snprintf(varname, sizeof(varname), "%s_%s", GROUP_CATEGORY_PREFIX, data);
	} else {
		ast_copy_string(varname, GROUP_CATEGORY_PREFIX, sizeof(varname));
	}

	group = pbx_builtin_getvar_helper(chan, varname);
	if (group)
		ast_copy_string(buf, group, len);

	return buf;
}

static void group_function_write(struct ast_channel *chan, char *cmd, char *data, const char *value)
{
	char grpcat[256];

	if (!ast_strlen_zero(data)) {
		snprintf(grpcat, sizeof(grpcat), "%s@%s", value, data);
	} else {
		ast_copy_string(grpcat, value, sizeof(grpcat));
	}

        if (ast_app_group_set_channel(chan, grpcat))
                ast_log(LOG_WARNING, "Setting a group requires an argument (group name)\n");
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function group_function = {
	.name = "GROUP",
	.syntax = "GROUP([category])",
	.synopsis = "Gets or sets the channel group.",
	.desc = "Gets or sets the channel group.\n",
	.read = group_function_read,
	.write = group_function_write,
};

static char *group_list_function_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct ast_var_t *current;
	struct varshead *headp;
	char tmp1[1024] = "";
	char tmp2[1024] = "";

	headp=&chan->varshead;
	AST_LIST_TRAVERSE(headp,current,entries) {
		if (!strncmp(ast_var_name(current), GROUP_CATEGORY_PREFIX "_", strlen(GROUP_CATEGORY_PREFIX) + 1)) {
			if (!ast_strlen_zero(tmp1)) {
				ast_copy_string(tmp2, tmp1, sizeof(tmp2));
				snprintf(tmp1, sizeof(tmp1), "%s %s@%s", tmp2, ast_var_value(current), (ast_var_name(current) + strlen(GROUP_CATEGORY_PREFIX) + 1));
			} else {
				snprintf(tmp1, sizeof(tmp1), "%s@%s", ast_var_value(current), (ast_var_name(current) + strlen(GROUP_CATEGORY_PREFIX) + 1));
			}
		} else if (!strcmp(ast_var_name(current), GROUP_CATEGORY_PREFIX)) {
			if (!ast_strlen_zero(tmp1)) {
				ast_copy_string(tmp2, tmp1, sizeof(tmp2));
				snprintf(tmp1, sizeof(tmp1), "%s %s", tmp2, ast_var_value(current));
			} else {
				snprintf(tmp1, sizeof(tmp1), "%s", ast_var_value(current));
			}
		}
	}
	ast_copy_string(buf, tmp1, len);
	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function group_list_function = {
	.name = "GROUP_LIST",
	.syntax = "GROUP_LIST()",
	.synopsis = "Gets a list of the groups set on a channel.",
	.desc = "Gets a list of the groups set on a channel.\n",
	.read = group_list_function_read,
	.write = NULL,
};

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
