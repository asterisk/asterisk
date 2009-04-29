/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="GROUP_COUNT" language="en_US">
		<synopsis>
			Counts the number of channels in the specified group.
		</synopsis>
		<syntax argsep="@">
			<parameter name="groupname">
				<para>Group name.</para>
			</parameter>
			<parameter name="category">
				<para>Category name</para>
			</parameter>
		</syntax>
		<description>
			<para>Calculates the group count for the specified group, or uses the
			channel's current group if not specifed (and non-empty).</para>
		</description>
	</function>
	<function name="GROUP_MATCH_COUNT" language="en_US">
		<synopsis>
			Counts the number of channels in the groups matching the specified pattern.
		</synopsis>
		<syntax argsep="@">
			<parameter name="groupmatch" required="true">
				<para>A standard regular expression used to match a group name.</para>
			</parameter>
			<parameter name="category">
				<para>Category name.</para>
			</parameter>
		</syntax>
		<description>
			<para>Calculates the group count for all groups that match the specified pattern.
			Uses standard regular expression matching (see regex(7)).</para>
		</description>
	</function>
	<function name="GROUP" language="en_US">
		<synopsis>
			Gets or sets the channel group.
		</synopsis>
		<syntax>
			<parameter name="category">
				<para>Category name.</para>
			</parameter>
		</syntax>
		<description>
			<para><replaceable>category</replaceable> can be employed for more fine grained group management. Each channel 
			can only be member of exactly one group per <replaceable>category</replaceable>.</para>
		</description>
	</function>
	<function name="GROUP_LIST" language="en_US">
		<synopsis>
			Gets a list of the groups set on a channel.
		</synopsis>
		<syntax />
		<description>
			<para>Gets a list of the groups set on a channel.</para>
		</description>
	</function>

 ***/

static int group_count_function_read(struct ast_channel *chan, const char *cmd,
				     char *data, char *buf, size_t len)
{
	int ret = -1;
	int count = -1;
	char group[80] = "", category[80] = "";

	ast_app_group_split_group(data, group, sizeof(group), category,
				  sizeof(category));

	/* If no group has been provided let's find one */
	if (ast_strlen_zero(group)) {
		struct ast_group_info *gi = NULL;

		ast_app_group_list_rdlock();
		for (gi = ast_app_group_list_head(); gi; gi = AST_LIST_NEXT(gi, group_list)) {
			if (gi->chan != chan)
				continue;
			if (ast_strlen_zero(category) || (!ast_strlen_zero(gi->category) && !strcasecmp(gi->category, category)))
				break;
		}
		if (gi) {
			ast_copy_string(group, gi->group, sizeof(group));
			if (!ast_strlen_zero(gi->category))
				ast_copy_string(category, gi->category, sizeof(category));
		}
		ast_app_group_list_unlock();
	}

	if ((count = ast_app_group_get_count(group, category)) == -1) {
		ast_log(LOG_NOTICE, "No group could be found for channel '%s'\n", chan->name);
	} else {
		snprintf(buf, len, "%d", count);
		ret = 0;
	}

	return ret;
}

static struct ast_custom_function group_count_function = {
	.name = "GROUP_COUNT",
	.read = group_count_function_read,
	.read_max = 12,
};

static int group_match_count_function_read(struct ast_channel *chan,
					   const char *cmd, char *data, char *buf,
					   size_t len)
{
	int count;
	char group[80] = "";
	char category[80] = "";

	ast_app_group_split_group(data, group, sizeof(group), category,
				  sizeof(category));

	if (!ast_strlen_zero(group)) {
		count = ast_app_group_match_get_count(group, category);
		snprintf(buf, len, "%d", count);
		return 0;
	}

	return -1;
}

static struct ast_custom_function group_match_count_function = {
	.name = "GROUP_MATCH_COUNT",
	.read = group_match_count_function_read,
	.read_max = 12,
	.write = NULL,
};

static int group_function_read(struct ast_channel *chan, const char *cmd,
			       char *data, char *buf, size_t len)
{
	int ret = -1;
	struct ast_group_info *gi = NULL;
	
	ast_app_group_list_rdlock();
	
	for (gi = ast_app_group_list_head(); gi; gi = AST_LIST_NEXT(gi, group_list)) {
		if (gi->chan != chan)
			continue;
		if (ast_strlen_zero(data))
			break;
		if (!ast_strlen_zero(gi->category) && !strcasecmp(gi->category, data))
			break;
	}
	
	if (gi) {
		ast_copy_string(buf, gi->group, len);
		ret = 0;
	}
	
	ast_app_group_list_unlock();
	
	return ret;
}

static int group_function_write(struct ast_channel *chan, const char *cmd,
				char *data, const char *value)
{
	char grpcat[256];

	if (!ast_strlen_zero(data)) {
		snprintf(grpcat, sizeof(grpcat), "%s@%s", value, data);
	} else {
		ast_copy_string(grpcat, value, sizeof(grpcat));
	}

	if (ast_app_group_set_channel(chan, grpcat))
		ast_log(LOG_WARNING,
				"Setting a group requires an argument (group name)\n");

	return 0;
}

static struct ast_custom_function group_function = {
	.name = "GROUP",
	.read = group_function_read,
	.write = group_function_write,
};

static int group_list_function_read(struct ast_channel *chan, const char *cmd,
				    char *data, char *buf, size_t len)
{
	struct ast_group_info *gi = NULL;
	char tmp1[1024] = "";
	char tmp2[1024] = "";

	if (!chan)
		return -1;

	ast_app_group_list_rdlock();

	for (gi = ast_app_group_list_head(); gi; gi = AST_LIST_NEXT(gi, group_list)) {
		if (gi->chan != chan)
			continue;
		if (!ast_strlen_zero(tmp1)) {
			ast_copy_string(tmp2, tmp1, sizeof(tmp2));
			if (!ast_strlen_zero(gi->category))
				snprintf(tmp1, sizeof(tmp1), "%s %s@%s", tmp2, gi->group, gi->category);
			else
				snprintf(tmp1, sizeof(tmp1), "%s %s", tmp2, gi->group);
		} else {
			if (!ast_strlen_zero(gi->category))
				snprintf(tmp1, sizeof(tmp1), "%s@%s", gi->group, gi->category);
			else
				snprintf(tmp1, sizeof(tmp1), "%s", gi->group);
		}
	}
	
	ast_app_group_list_unlock();

	ast_copy_string(buf, tmp1, len);

	return 0;
}

static struct ast_custom_function group_list_function = {
	.name = "GROUP_LIST",
	.read = group_list_function_read,
	.write = NULL,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&group_count_function);
	res |= ast_custom_function_unregister(&group_match_count_function);
	res |= ast_custom_function_unregister(&group_list_function);
	res |= ast_custom_function_unregister(&group_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&group_count_function);
	res |= ast_custom_function_register(&group_match_count_function);
	res |= ast_custom_function_register(&group_list_function);
	res |= ast_custom_function_register(&group_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel group dialplan functions");
