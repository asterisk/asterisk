/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel group related dialplan functions
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

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
		grp = pbx_builtin_getvar_helper(chan, category);
		strncpy(group, grp, sizeof(group) - 1);
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
