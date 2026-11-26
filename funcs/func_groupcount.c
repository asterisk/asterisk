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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/strings.h"
#include "asterisk/cli.h"
#include "asterisk/test.h"

/*** DOCUMENTATION
	<function name="GROUP_COUNT" language="en_US">
		<since>
			<version>1.2.0</version>
		</since>
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
			channel's current group if not specified (and non-empty).</para>
		</description>
	</function>
	<function name="GROUP_MATCH_LIST" language="en_US">
		<synopsis>Find groups by regular expression</synopsis>
		<syntax argsep="@">
			<parameter name="group_regex"/>
			<parameter name="category_regex"/>
		</syntax>
		<description>
			<para>Search for groups matching group@category. This search will look at all known groups and filter by the regex(s) provided.</para>
			<para>If only the group_regex is specified, then return group@category results that only match the group name against the group_regex</para>
			<para>If only the category_regex is specified, then return group@category results that only match the category name against the category_regex.</para>
			<para>If the search regex parameters are entirely empty, all group@category will be returned.</para>
			<para>Uses standard regular expression matching (see regex(7)).</para>
			<example title="Find groups containing the string 'foo'">
				On Channel 1:
					Set(GROUP()=groupName)
				On Channel 2:
					Set(GROUP()=foobarbaz)
				On Channel 3:
					Set(GROUP()=foobarbill)
				On Channel 4:
					Set(GROUP()=somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(groups_found=${GROUP_MATCH_LIST(.*foo.*)})
				Given the above:
					This will find the groups 'foobarbaz' and 'foobarbill' since they both match .*foo.* regex against the group name
					Variable groups_found will be set to: foobarbaz,foobarbill
			</example>
			<example title="Find groups containing category name of 'bar'">
				On Channel 1:
					Set(GROUP()=groupName@categoryName)
				On Channel 2:
					Set(GROUP()=foo@barbaz)
				On Channel 3:
					Set(GROUP()=foo@barbill)
				On Channel 4:
					Set(GROUP()=anything@somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(groups_found=${GROUP_MATCH_LIST(@.*bar.*)})

				Given the above:
					This will find the groups 'foo@barbaz' and 'foo@barbill' since they both match .*bar.* regex against the category name
					Variable groups_found will be set to: foo@barbaz,foo@barbill
			</example>
			<example title="Find groups containing 'foo' and category name of 'bar'">
				On Channel 1:
					Set(GROUP()=groupName@categoryName)
				On Channel 2:
					Set(GROUP()=foo@barbaz)
				On Channel 3:
					Set(GROUP()=foo@barbill)
				On Channel 4:
					Set(GROUP()=anything@somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(groups_found=${GROUP_MATCH_LIST(.*foo.*@.*bar.*)})

				Given the above:
					This will find the groups 'foo@barbaz' and 'foo@barbill' since they both match .*foo.* regex against the group name and .*bar.* regex against the category name
					Variable groups_found will be set to: foo@barbaz,foo@barbill
			</example>
		</description>
		<see-also>
			<ref type="function">GROUP</ref>
			<ref type="function">GROUP_CHANNEL_LIST</ref>
		</see-also>
	</function>
	<function name="GROUP_CHANNEL_LIST" language="en_US">
		<synopsis>
			Find channels that are members of groups/categories by exact match
		</synopsis>
		<syntax argsep="@">
			<parameter name="group"/>
			<parameter name="category"/>
		</syntax>
		<description>
			<para>Search for groups matching group@category and return the channels that are members of those groups, the group/category must match exactly</para>
			<para>If only the group is specified, then return group@category results that only match against the group name</para>
			<para>If only the category is specified, then return group@category results that only match against the category name</para>
			<para>If the search parameters are entirely empty, all channels having any kind of group membership will be returned.</para>
			<para>A channel is only listed once, even if it is a member of more than one matching group.</para>
			<para>After GROUP_CHANNEL_LIST is complete.  Channel variables may be set with information related to the function call.  No variables are set when the function is used without a channel.</para>
			<variablelist>
				<variable name="GROUP_CHANNEL_LIST_ERROR">
					<value name="TRUNCATED">
						The full result is not complete and was truncated
					</value>
				</variable>
			</variablelist>
			<example title="Find channels that are members of the group 'foobarbaz'">
				On Channel PJSIP/1234-0000001:
					Set(GROUP()=groupName)
				On Channel PJSIP/1234-0000003:
					Set(GROUP()=foobarbaz)
				On Channel PJSIP/1234-0000004:
					Set(GROUP()=foobarbill)
				On Channel PJSIP/1234-0000005:
					Set(GROUP()=somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(channels_found=${GROUP_CHANNEL_LIST(foobarbaz)})

				Given the above:
					This will find the channels that are in the group 'foobarbaz'
					Variable channels_found will be set to: PJSIP/1234-0000003
			</example>
			<example title="Find channels that are in a group with the category 'barbaz'">
				On Channel PJSIP/1234-0000001:
					Set(GROUP()=groupName@categoryName)
				On Channel PJSIP/1234-0000002:
					Set(GROUP()=foo@barbaz)
				On Channel PJSIP/1234-0000003:
					Set(GROUP()=oof@barbaz)
				On Channel PJSIP/1234-0000004:
					Set(GROUP()=anything@somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(channels_found=${GROUP_CHANNEL_LIST(@barbaz)})

				Given the above:
					This will find the channels that are in the groups 'foo@barbaz' and 'oof@barbaz' since they both have the category 'barbaz'
					Variable channels_found will be set to: PJSIP/1234-0000002,PJSIP/1234-0000003
			</example>
			<example title="Find channels that are members of the group 'foo' with the category 'bar'">
				On Channel PJSIP/1234-0000001:
					Set(GROUP()=groupName@categoryName)
				On Channel PJSIP/1234-0000002:
					Set(GROUP()=foo@bar)
				On Channel PJSIP/1234-0000003:
					Set(GROUP()=foo@bar)
				On Channel PJSIP/1234-0000004:
					Set(GROUP()=foo@barbill)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(channels_found=${GROUP_CHANNEL_LIST(foo@bar)})

				Given the above:
					This will find the channels that are in the group@category assignment of foo@bar
					The category must match exactly, so the channel in 'foo@barbill' is not included
					Variable channels_found will be set to: PJSIP/1234-0000002,PJSIP/1234-0000003
			</example>
		</description>
		<see-also>
			<ref type="function">GROUP</ref>
			<ref type="function">GROUP_MATCH_CHANNEL_LIST</ref>
		</see-also>
	</function>
	<function name="GROUP_MATCH_CHANNEL_LIST" language="en_US">
		<synopsis>
			Find channels that are members of groups by regular expression
		</synopsis>
		<syntax argsep="@">
			<parameter name="group_regex"/>
			<parameter name="category_regex"/>
		</syntax>
		<description>
			<para>Search for groups matching group@category and return the channels that are members of those groups, filtering known groups by the regex(s) provided.</para>
			<para>If only the group_regex is specified, then return group@category results that only match the group name against the group_regex</para>
			<para>If only the category_regex is specified, then return group@category results that only match the category name against the category_regex.</para>
			<para>If the search regex parameters are entirely empty, all channels having any kind of group membership will be returned.</para>
			<para>Uses standard regular expression matching (see regex(7)).</para>
			<para>A channel is only listed once, even if it is a member of more than one matching group.</para>
			<para>After GROUP_MATCH_CHANNEL_LIST is complete.  Channel variables may be set with information related to the function call.  No variables are set when the function is used without a channel.</para>
			<variablelist>
				<variable name="GROUP_MATCH_CHANNEL_LIST_ERROR">
					<value name="TRUNCATED">
						The full result is not complete and was truncated
					</value>
				</variable>
			</variablelist>
			<example title="Find channels in groups containing the string 'foo'">
				On Channel PJSIP/1234-0000001:
					Set(GROUP()=groupName)
				On Channel PJSIP/1234-0000003:
					Set(GROUP()=foobarbaz)
				On Channel PJSIP/1234-0000004:
					Set(GROUP()=foobarbill)
				On Channel PJSIP/1234-0000005:
					Set(GROUP()=somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(channels_found=${GROUP_MATCH_CHANNEL_LIST(.*foo.*)})

				Given the above:
					This will find the channels that are in the groups 'foobarbaz' and 'foobarbill' since they both match .*foo.* regex against the group name
					Variable channels_found will be set to: PJSIP/1234-0000003,PJSIP/1234-0000004
			</example>
			<example title="Find channels in groups containing the category name of 'bar'">
				On Channel PJSIP/1234-0000001:
					Set(GROUP()=groupName@categoryName)
				On Channel PJSIP/1234-0000002:
					Set(GROUP()=foo@barbaz)
				On Channel PJSIP/1234-0000003:
					Set(GROUP()=foo@barbill)
				On Channel PJSIP/1234-0000004:
					Set(GROUP()=anything@somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(channels_found=${GROUP_MATCH_CHANNEL_LIST(@.*bar.*)})

				Given the above:
					This will find the channels that are in the groups 'foo@barbaz' and 'foo@barbill' since they both match .*bar.* regex against the category name
					Variable channels_found will be set to: PJSIP/1234-0000002,PJSIP/1234-0000003
			</example>
			<example title="Find channels in groups containing 'foo' and a category name containing 'bar'">
				On Channel PJSIP/1234-0000001:
					Set(GROUP()=groupName@categoryName)
				On Channel PJSIP/1234-0000002:
					Set(GROUP()=foo@bar)
				On Channel PJSIP/1234-0000003:
					Set(GROUP()=foo@barbill)
				On Channel PJSIP/1234-0000004:
					Set(GROUP()=anything@somethingelse)
				On any Channel:
					Assuming that channels still exist that have the above associated GROUP() assignents
					Set(channels_found=${GROUP_MATCH_CHANNEL_LIST(.*foo.*@.*bar.*)})

				Given the above:
					This will find the channels that are in the groups 'foo@bar' and 'foo@barbill' since they both match .*foo.* regex against the group name and .*bar.* regex against the category name
					Variable channels_found will be set to: PJSIP/1234-0000002,PJSIP/1234-0000003
			</example>
		</description>
		<see-also>
			<ref type="function">GROUP</ref>
			<ref type="function">GROUP_CHANNEL_LIST</ref>
		</see-also>
	</function>
	<function name="GROUP_MATCH_COUNT" language="en_US">
		<since>
			<version>1.2.0</version>
		</since>
		<synopsis>
			Counts the number of channels in the groups matching the specified pattern.
		</synopsis>
		<syntax argsep="@">
			<parameter name="group_match">
				<para>A standard regular expression used to match a group name.</para>
			</parameter>
			<parameter name="category_match">
				<para>A standard regular expression used to match a category name.</para>
			</parameter>
		</syntax>
		<description>
			<para>Calculates the group count for all groups that match the specified pattern.
			Note: category matching is applied after matching based on group.
			Uses standard regular expression matching on both parameters (see regex(7)).</para>
		</description>
	</function>
	<function name="GROUP" language="en_US">
		<since>
			<version>1.2.0</version>
		</since>
		<synopsis>
			Gets or sets the channel group.
		</synopsis>
		<syntax argsep="@">
			<parameter name="group">
				<para>Group name.</para>
			</parameter>
			<parameter name="category">
				<para>Category name.</para>
			</parameter>
		</syntax>
		<description>
			<para><replaceable>category</replaceable> can be employed for more fine grained group management. Each channel
			can only be member of exactly one group per <replaceable>category</replaceable>.  Once a group is assigned to
			a channel, per-group variables can then be assigned via GROUP_VAR()</para>
		</description>
		<see-also>
			<ref type="function">GROUP_VAR</ref>
		</see-also>
	</function>
	<function name="GROUP_VAR" language="en_US">
		<synopsis>
			Gets or sets a variable on a channel group.
		</synopsis>
		<syntax>
			<parameter name="category">
				<para>Category name.</para>
			</parameter>
		</syntax>
		<description>
			<para>
				Once a GROUP() is assinged to a channel, variables can then be attached to that group.	As long as the
				group exists, these variables will exist. When the group no longer exists, the variables will be cleaned up
				automatically.
			</para>
			<para>
				These variables can be considered 'globals for a group of channels'.  These variables are
				like a SHARED() variable but for a collection of channels.
			</para>
			<example>
				On Channel 1
				Set(GROUP()=foo)
				Set(GROUP_VAR(foo,savethis)=123)

				On Channel 2 -- Assuming Channel1 is still up and Channel1 is still a member of GROUP(foo)
                                We can now join the same Group and read a Group Variable
				Set(GROUP()=foo)
				NoOp(${GROUP_VAR(foo,savethis) &lt;-- This prints '123'

			        On Channel 3 -- Assuming Channel1 is still up and Channel1 is still a member of GROUP(foo)
                                We do not need to join the group in order to read a Group Variable
				NoOp(${GROUP_VAR(foo,savethis) &lt;-- This prints '123'
			</example>
		</description>
		<see-also>
			<ref type="function">GROUP</ref>
			<ref type="function">SHARED</ref>
		</see-also>
	</function>
	<function name="GROUP_LIST" language="en_US">
		<since>
			<version>1.2.0</version>
		</since>
		<synopsis>
			Gets a list of the groups set on a channel.
		</synopsis>
		<syntax />
		<description>
			<para>Gets a list of the groups set on a channel.</para>
		</description>
	</function>
	<manager name="GroupSet" language="en_US">
		<synopsis>
			Add channel group assignments
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="channel">
				<para>Channel to operate on.</para>
			</parameter>
			<parameter name="group">
				<para>Group name to set.</para>
			</parameter>
			<parameter name="category">
				<para>Category name to set.</para>
			</parameter>
		</syntax>
		<description>
			<para>For more information, see the dialplan function GROUP()</para>
		</description>
	</manager>
	<manager name="GroupRemove" language="en_US">
		<synopsis>
			Remove channel group assignments
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="channel">
				<para>Channel to operate on.</para>
			</parameter>
			<parameter name="group">
				<para>Group name to remove.</para>
			</parameter>
			<parameter name="category">
				<para>Category name to remove.</para>
			</parameter>
		</syntax>
		<description>
			<para>For more information, see the dialplan function GROUP()</para>
		</description>
	</manager>
	<application name="DumpGroups" language="en_US">
		<synopsis>
			Dump all group information to the console
		</synopsis>
		<description>
			<para>When executed, this will show all group assignments and group variables
			in the console</para>
		</description>
	</application>
	<manager name="GroupVarGet" language="en_US">
		<synopsis>
			Get channel group variables.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Group" required="false" />
			<parameter name="Category" required="false" />
			<parameter name="Variable" required="true" />
		</syntax>
		<description>
			<para>
				At a minimum, either group or category must be provided.
				For more information, see the dialplan function GROUP_VAR().
			</para>
		</description>
	</manager>
	<manager name="GroupVarSet" language="en_US">
		<synopsis>
			Set channel group variables.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Group" required="false" />
			<parameter name="Category" required="false" />
			<parameter name="Variable" required="true" />
			<parameter name="Value" required="false" />
		</syntax>
		<description>
			<para>
				At a minimum, either group or category must be provided.
				For more information, see the dialplan function GROUP_VAR().
			</para>
		</description>
	</manager>
	<manager name="GroupsShow" language="en_US">
		<synopsis>
			Show channel groups.  With optional filtering
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Group" required="false">
				<para>The exact group name to find, or to use a regular expression: set this parameter to: /regex/</para>
			</parameter>
			<parameter name="Category" required="false">
				<para>The exact category to find, or to use a regular expression: set this parameter to: /regex/</para>
			</parameter>
		</syntax>
		<description>
			<para>
				This will return a list of channel groups that are in use.
				For more information, see the dialplan function GROUP().
			</para>
		</description>
	</manager>
	<manager name="GroupsShowChannels" language="en_US">
		<synopsis>
			Show group channel assignments. With optional filtering
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Group" required="false">
				<para>The exact group name to find, or to use a regular expression: set this parameter to: /regex/</para>
			</parameter>
			<parameter name="Category" required="false">
				<para>The exact category to find, or to use a regular expression: set this parameter to: /regex/</para>
			</parameter>
		</syntax>
		<description>
			<para>
				This will return a list the channels that are within each group.
				For more information, see the dialplan function GROUP().
			</para>
		</description>
	</manager>
	<manager name="GroupsShowVariables" language="en_US">
		<synopsis>
			Show group channel variable assignments.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>
				This will return a list of groups and the variables assigned in each group.
				For more information, see the dialplan function GROUP_VAR().
			</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="GroupVarGetResponse">
		<managerEventInstance class="EVENT_FLAG_REPORTING">
			<synopsis>
				Raised in response to a a GroupVarGet command
			</synopsis>
			<syntax>
				<parameter name="ActionID">
					<para>ActionID (if any) that was passed into the GroupVarGet request</para>
				</parameter>
				<parameter name="Group">
					<para>Name of the group that the variable is a part of</para>
				</parameter>
				<parameter name="Category">
					<para>Name of the category that the variable is a part of</para>
				</parameter>
				<parameter name="Variable">
					<para>The variable that was requested</para>
				</parameter>
				<parameter name="Value">
					<para>The value of the variable</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>

 ***/

static const char *dumpgroups_app = "DumpGroups";

static int group_count_function_read(struct ast_channel *chan, const char *cmd,
				     char *data, char *buf, size_t len)
{
	int ret = -1;
	int count = -1;
	char group[MAX_GROUP_LEN] = "", category[MAX_CATEGORY_LEN] = "";

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_app_group_split_group(data, group, sizeof(group), category,
				  sizeof(category));

	/* If no group has been provided let's find one */
	if (ast_strlen_zero(group)) {
		struct ast_group_info *gi = NULL;

		ast_app_group_list_rdlock();
		for (gi = ast_app_group_list_head(); gi; gi = AST_LIST_NEXT(gi, group_list)) {
			if (gi->chan != chan) {
				continue;
			}
			if (ast_strlen_zero(category) || (!ast_strlen_zero(gi->category) && !strcasecmp(gi->category, category))) {
				break;
			}
		}
		if (gi) {
			ast_copy_string(group, gi->group, sizeof(group));
			if (!ast_strlen_zero(gi->category)) {
				ast_copy_string(category, gi->category, sizeof(category));
			}
		}
		ast_app_group_list_unlock();
	}

	if ((count = ast_app_group_get_count(group, category)) == -1) {
		ast_log(LOG_NOTICE, "No group could be found for channel '%s'\n", ast_channel_name(chan));
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
	char group[MAX_GROUP_LEN] = "";
	char category[MAX_CATEGORY_LEN] = "";

	ast_app_group_split_group(data, group, sizeof(group), category,
				  sizeof(category));

	if (!ast_strlen_zero(group)) {
		int count;
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

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_app_group_list_rdlock();

	for (gi = ast_app_group_list_head(); gi; gi = AST_LIST_NEXT(gi, group_list)) {
		if (gi->chan != chan) {
			continue;
		}
		if (ast_strlen_zero(data)) {
			break;
		}
		if (!ast_strlen_zero(gi->category) && !strcasecmp(gi->category, data)) {
			break;
		}
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

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (!value) {
		return -1;
	}

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


/* GROUP_VAR and related */

static int group_var_function_read(struct ast_channel *chan, const char *cmd,
			       char *data, char *buf, size_t len)
{
	char group[MAX_GROUP_LEN] = "";
	char category[MAX_CATEGORY_LEN] = "";
	char *variable_value;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(groupcategory);
		AST_APP_ARG(varname);
	);
	AST_STANDARD_APP_ARGS(args, data);

	buf[0] = '\0';

	if (ast_strlen_zero(args.groupcategory) || ast_strlen_zero(args.varname)) {
		ast_log(LOG_WARNING, "Syntax GROUP_VAR(group[@category],<varname>)\n");
		return -1;
	}

	if (ast_app_group_split_group(args.groupcategory, group, sizeof(group), category, sizeof(category))) {
		return -1;
	}

	variable_value = ast_app_group_get_var(group, category, args.varname);

	if (variable_value) {
		ast_copy_string(buf, variable_value, len);
		ast_free(variable_value);
	}

	return 0;
}

static int group_var_function_write(struct ast_channel *chan, const char *cmd,
				char *data, const char *value)
{
	char group[MAX_GROUP_LEN] = "";
	char category[MAX_CATEGORY_LEN] = "";
	int result;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(groupcategory);
		AST_APP_ARG(varname);
	);
	AST_STANDARD_APP_ARGS(args, data);

	if (!value) {
		value = "";
	}

	if (ast_strlen_zero(args.groupcategory) || ast_strlen_zero(args.varname)) {
		ast_log(LOG_WARNING, "Syntax GROUP_VAR(group[@category],<varname>)=<value>)\n");
		return -1;
	}

	if (ast_app_group_split_group(args.groupcategory, group, sizeof(group), category, sizeof(category))) {
		ast_log(LOG_WARNING, "GROUP_VAR() Unable to parse group@category '%s'\n", args.groupcategory);
		return -1;
	}

	result = ast_app_group_set_var(chan, group, category, args.varname, value);

	if (result != 0) {
		/* We know we're not passing any NULL values, so our error has to be 'non-exist' */
		ast_log(LOG_WARNING, "GROUP_VAR() Variable set failed (group doesn't exist)\n");
		return -1;
	}

	return 0;
}

static int manager_group_set(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *group = astman_get_header(m, "Group");
	const char *category = astman_get_header(m, "Category");

	struct ast_channel *chan = NULL;
	struct ast_str *group_category = ast_str_create(MAX_GROUP_LEN);

	if (!group_category) {
		astman_send_error(s, m, "Unable to allocate new variable.  Cannot proceed with GroupSet\n");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "Channel not specified.");
		return AMI_SUCCESS;
	}

	chan = ast_channel_get_by_name(channel);
	if (!chan) {
		astman_send_error(s, m, "Channel not found.");
		return AMI_SUCCESS;
	}

	if (!group) {
		group = "";
	}

	if (!category) {
		category = "";
	}

	if (ast_str_set(&group_category, 0, "%s@%s", group, category) == AST_DYNSTR_BUILD_FAILED) {
		astman_send_error(s, m, "Unable to allocate new variable.  Cannot proceed with GroupSet\n");
		goto done;
        }

	if (ast_app_group_set_channel(chan, ast_str_buffer(group_category)) == 0) {
		astman_send_ack(s, m, "Group Set");
		ast_channel_unref(chan);
		return AMI_SUCCESS;
	}

	astman_send_error(s, m, "Group set failed.");

       done:
	ast_channel_unref(chan);
        ast_free(group_category);

	return AMI_SUCCESS;
}

static int manager_group_remove(struct mansession *s, const struct message *m)
{
	const char *group    = astman_get_header(m, "Group");
	const char *category = astman_get_header(m, "Category");

	if (ast_app_group_remove_all_channels(group, category) == 0) {
		astman_send_ack(s, m, "Group Removed");
		return AMI_SUCCESS;
	}

	astman_send_error(s, m, "Group remove failed.");

	return AMI_SUCCESS;
}

static int manager_group_var_get(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *group = astman_get_header(m, "Group");
	const char *category = astman_get_header(m, "Category");
	const char *variable = astman_get_header(m, "Variable");
	char *variable_value;
	char idText[256] = "";

	if (ast_strlen_zero(group)) {
		astman_send_error(s, m, "No group specified.");
		return 0;
	}

	if (ast_strlen_zero(variable)) {
		astman_send_error(s, m, "No variable specified.");
		return 0;
	}

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	}

	if (!category) {
		category = "";
	}

	variable_value = ast_app_group_get_var(group, category, variable);

	if (!variable_value) {
		astman_send_error(s, m, "Group variable not found");
		return 0;
	}

	astman_send_ack(s, m, "Result will follow");
	astman_append(s, "Event: GroupVarGetResponse\r\n"
			"Group: %s\r\n"
			"Category: %s\r\n"
			"Variable: %s\r\n"
			"Value: %s\r\n"
			"%s"
			"\r\n",
			group, category, variable, variable_value, idText);

        ast_free(variable_value);

	return AMI_SUCCESS;
}

static int manager_group_var_set(struct mansession *s, const struct message *m)
{
	const char *group = astman_get_header(m, "Group");
	const char *category = astman_get_header(m, "Category");
	const char *variable = astman_get_header(m, "Variable");
	const char *value = astman_get_header(m, "Value");
	int result = 0;

	if (ast_strlen_zero(group)) {
		astman_send_error(s, m, "No group specified.");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(variable)) {
		astman_send_error(s, m, "No variable specified.");
		return AMI_SUCCESS;
	}

	if (!category) {
		category = "";
	}

	if (!value) {
		value = "";
	}

	result = ast_app_group_set_var(NULL, group, category, variable, value);

	if (result != 0) {
		/* We know we're not passing any NULL values, so our error has to be 'non-exist' */
		astman_send_error(s, m, "Variable set failed (group doesn't exist)");
		return 0;
	}

	astman_send_ack(s, m, "Variable Set");

	return 0;
}

static int manager_groups_show(struct mansession *s, const struct message *m)
{
	const char *id			= astman_get_header(m, "ActionID");
	const char *group_or_regex	= astman_get_header(m, "Group");    /* could be a regex, if starting with '/' */
	const char *category_or_regex	= astman_get_header(m, "Category"); /* could be a regex, if starting with '/' */

	const char *group_match		= NULL;
	const char *category_match	= NULL;

	struct ast_str *group_regex_string	= NULL;
	struct ast_str *category_regex_string	= NULL;

	regex_t regexbuf_group;
	regex_t regexbuf_category;

	struct ast_group_meta *gmi = NULL;
	char idText[256] = "";
	int groups = 0;

	if (!ast_strlen_zero(group_or_regex)) {
		if (group_or_regex[0] == '/') {
			group_regex_string = ast_str_create(strlen(group_or_regex));
			if (!group_regex_string) {
				astman_send_error(s, m, "Memory Allocation Failure");
				return 0; /* Nothing to clean up */
			}

			/* Make "/regex/" into "regex" */
			if (ast_regex_string_to_regex_pattern(group_or_regex, &group_regex_string) != 0) {
				astman_send_error(s, m, "Regex format invalid, Group param should be /regex/");
				ast_free(group_regex_string);
				return AMI_SUCCESS; /* Nothing else to clean up */
			}

			/* if regex compilation fails, whole command fails */
			if (regcomp(&regexbuf_group, ast_str_buffer(group_regex_string), REG_EXTENDED | REG_NOSUB)) {
				astman_send_error_va(s, m, "Regex compile failed on: %s", group_or_regex);
				ast_free(group_regex_string);
				return AMI_SUCCESS; /* Nothing else to clean up */
			}
		} else {
			group_match = group_or_regex;
		}
	}

	if (!ast_strlen_zero(category_or_regex)) {
		if (category_or_regex[0] == '/') {
			category_regex_string = ast_str_create(strlen(category_or_regex));
			if (!category_regex_string) {
				astman_send_error(s, m, "Memory Allocation Failure");
				goto done;
			}

			/* Make "/regex/" into "regex" */
			if (ast_regex_string_to_regex_pattern(category_or_regex, &category_regex_string) != 0) {
				astman_send_error(s, m, "Regex format invalid, Category param should be /regex/");
				ast_free(category_regex_string);
				category_regex_string = NULL;
				goto done;
			}

			/* if regex compilation fails, whole command fails */
			if (regcomp(&regexbuf_category, ast_str_buffer(category_regex_string), REG_EXTENDED | REG_NOSUB)) {
				astman_send_error_va(s, m, "Regex compile failed on: %s", category_or_regex);
				ast_free(category_regex_string);
				category_regex_string = NULL;
				goto done;
			}
		} else {
			category_match = category_or_regex;
		}
	}

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	}

	astman_send_listack(s, m, "Groups will follow", "start");

	ast_app_group_meta_rdlock();
	for (gmi = ast_app_group_meta_head(); gmi; gmi = AST_LIST_NEXT(gmi, group_meta_list)) {
		if (group_regex_string && regexec(&regexbuf_group, gmi->group, 0, NULL, 0)) {
			continue;
		}

		if (category_regex_string && regexec(&regexbuf_category, gmi->category, 0, NULL, 0)) {
			continue;
		}

		if (group_match && strcmp(group_match, gmi->group)) {
			continue;
		}

		if (category_match && strcmp(category_match, gmi->category)) {
			continue;
		}

		astman_append(s,
			"Event: GroupsShow\r\n"
			"Group: %s\r\n"
			"Category: %s\r\n"
			"%s"
			"\r\n", gmi->group, gmi->category, idText);

		groups++;
	}
	ast_app_group_meta_unlock();

	astman_append(s,
		"Event: GroupsShowComplete\r\n"
		"EventList: Complete\r\n"
		"ListItems: %d\r\n"
		"%s"
		"\r\n", groups, idText);

done:
	if (group_regex_string) {
		regfree(&regexbuf_group);
		ast_free(group_regex_string);
	}

	if (category_regex_string) {
		regfree(&regexbuf_category);
		ast_free(category_regex_string);
	}

	return AMI_SUCCESS;
}

static int manager_groups_show_channels(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *group_or_regex    = astman_get_header(m, "Group");	  /* could be a regex, if starting with '/' */
	const char *category_or_regex = astman_get_header(m, "Category"); /* could be a regex, if starting with '/' */

	const char *group_match	   = NULL;
	const char *category_match = NULL;

	struct ast_str *group_regex_string    = NULL;
	struct ast_str *category_regex_string = NULL;

	regex_t regexbuf_group;
	regex_t regexbuf_category;

	struct ast_group_info *gi = NULL;
	char idText[256] = "";
	int channels = 0;

	if (!ast_strlen_zero(group_or_regex)) {
		if (group_or_regex[0] == '/') {
			group_regex_string = ast_str_create(strlen(group_or_regex));
			if (!group_regex_string) {
				astman_send_error(s, m, "Memory Allocation Failure");
				return 0; /* Nothing to clean up */
			}

			/* Make "/regex/" into "regex" */
			if (ast_regex_string_to_regex_pattern(group_or_regex, &group_regex_string) != 0) {
				astman_send_error(s, m, "Regex format invalid, Group param should be /regex/");
				ast_free(group_regex_string);
				return AMI_SUCCESS; /* Nothing else to clean up */
			}

			/* if regex compilation fails, whole command fails */
			if (regcomp(&regexbuf_group, ast_str_buffer(group_regex_string), REG_EXTENDED | REG_NOSUB)) {
				astman_send_error_va(s, m, "Regex compile failed on: %s", group_or_regex);
				ast_free(group_regex_string);
				return AMI_SUCCESS; /* Nothing else to clean up */
			}
		} else {
			group_match = group_or_regex;
		}
	}

	if (!ast_strlen_zero(category_or_regex)) {
		if (category_or_regex[0] == '/') {
			category_regex_string = ast_str_create(strlen(category_or_regex));
			if (!category_regex_string) {
				astman_send_error(s, m, "Memory Allocation Failure");
				goto done;
			}

			/* Make "/regex/" into "regex" */
			if (ast_regex_string_to_regex_pattern(category_or_regex, &category_regex_string) != 0) {
				astman_send_error(s, m, "Regex format invalid, Category param should be /regex/");
				ast_free(category_regex_string);
				category_regex_string = NULL;
				goto done;
			}

			/* if regex compilation fails, whole command fails */
			if (regcomp(&regexbuf_category, ast_str_buffer(category_regex_string), REG_EXTENDED | REG_NOSUB)) {
				astman_send_error_va(s, m, "Regex compile failed on: %s", category_or_regex);
				ast_free(category_regex_string);
				category_regex_string = NULL;
				goto done;
			}
		} else {
			category_match = category_or_regex;
		}
	}

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	}

	astman_send_listack(s, m, "Group channels will follow", "start");

	ast_app_group_list_rdlock();
	for (gi = ast_app_group_list_head(); gi; gi = AST_LIST_NEXT(gi, group_list)) {
		if (group_regex_string && regexec(&regexbuf_group, gi->group, 0, NULL, 0)) {
			continue;
		}

		if (category_regex_string && regexec(&regexbuf_category, gi->category, 0, NULL, 0)) {
			continue;
		}

		if (group_match && strcmp(group_match, gi->group)) {
			continue;
		}

		if (category_match && strcmp(category_match, gi->category)) {
			continue;
		}

		astman_append(s,
			"Event: GroupsShowChannels\r\n"
			"Group: %s\r\n"
			"Category: %s\r\n"
			"Channel: %s\r\n"
			"%s"
			"\r\n", gi->group, gi->category, ast_channel_name(gi->chan), idText);

		channels++;
	}
	ast_app_group_list_unlock();

	astman_append(s,
		"Event: GroupsShowChannelsComplete\r\n"
		"EventList: Complete\r\n"
		"ListItems: %d\r\n"
		"%s"
		"\r\n", channels, idText);

done:
	if (group_regex_string) {
		regfree(&regexbuf_group);
		ast_free(group_regex_string);
	}

	if (category_regex_string) {
		regfree(&regexbuf_category);
		ast_free(category_regex_string);
	}

	return AMI_SUCCESS;
}

static int manager_groups_show_variables(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	struct ast_group_meta *gmi = NULL;
	char action_id[256] = "";
	int groups = 0;

	struct varshead *headp;
	struct ast_var_t *vardata;
	struct ast_str *variables = ast_str_create(100);

	if (!variables) {
		ast_log(LOG_ERROR, "Unable to allocate new variable.  Cannot proceed with GroupsShowVariables().\n");
		return AMI_SUCCESS;
	}

	if (!ast_strlen_zero(id)) {
		snprintf(action_id, sizeof(action_id), "ActionID: %s\r\n", id);
	}

	astman_send_listack(s, m, "Group variables will follow", "start");

	ast_app_group_meta_rdlock();
	for (gmi = ast_app_group_meta_head(); gmi; gmi = AST_LIST_NEXT(gmi, group_meta_list)) {
		headp = &gmi->varshead;
		ast_str_reset(variables);

		AST_LIST_TRAVERSE(headp, vardata, entries) {
			ast_str_append(&variables, 0, "Variable(%s): %s\r\n", ast_var_name(vardata), ast_var_value(vardata));
		}

		astman_append(s,
			"Event: GroupsShowVariables\r\n"
			"Group: %s\r\n"
			"Category: %s\r\n"
			"%s"
			"%s"
			"\r\n", gmi->group, gmi->category, action_id, ast_str_buffer((variables)));

		groups++;
	}
	ast_app_group_meta_unlock();

	astman_append(s,
		"Event: GroupsShowVariablesComplete\r\n"
		"EventList: Complete\r\n"
		"ListItems: %d\r\n"
		"%s"
		"\r\n", groups, action_id);

	ast_free(variables);

	return AMI_SUCCESS;
}

static struct ast_custom_function group_var_function = {
	.name = "GROUP_VAR",
	.syntax = "GROUP_VAR(groupname[@category],var)",
	.synopsis = "Gets or sets a channel group variable.",
	.read = group_var_function_read,
	.write = group_var_function_write,
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
		if (gi->chan != chan) {
			continue;
		}

		if (!ast_strlen_zero(tmp1)) {
			ast_copy_string(tmp2, tmp1, sizeof(tmp2));
			if (!ast_strlen_zero(gi->category)) {
				snprintf(tmp1, sizeof(tmp1), "%s %s@%s", tmp2, gi->group, gi->category);
			} else {
				snprintf(tmp1, sizeof(tmp1), "%s %s", tmp2, gi->group);
			}
		} else {
			if (!ast_strlen_zero(gi->category)) {
				snprintf(tmp1, sizeof(tmp1), "%s@%s", gi->group, gi->category);
			} else {
				snprintf(tmp1, sizeof(tmp1), "%s", gi->group);
			}
		}
	}

	ast_app_group_list_unlock();

	ast_copy_string(buf, tmp1, len);

	return 0;
}

static int group_match_list_function_read(struct ast_channel *chan, const char *cmd,
				    char *data, char *buf, size_t len)
{
	struct ast_group_meta *gmi = NULL;
	struct ast_str *foundgroup_str = ast_str_create(MAX_GROUP_LEN + MAX_CATEGORY_LEN);

	char groupmatch[MAX_GROUP_LEN] = "";
	char categorymatch[MAX_CATEGORY_LEN] = "";
	int groups_found = 0;
	regex_t regexbuf_group;
	regex_t regexbuf_category;

	buf[0] = '\0';

	if (!foundgroup_str) {
		ast_log(LOG_ERROR, "Unable to allocate new variable.  Cannot proceed with GROUP_MATCH_LIST()\n");
		return -1;
	}

	if (ast_app_group_split_group(data, groupmatch, sizeof(groupmatch), categorymatch, sizeof(categorymatch))) {
		ast_log(LOG_WARNING, "GROUP_MATCH_LIST() Unable to parse group@category '%s'\n", S_OR(data, ""));
		ast_free(foundgroup_str);
		return -1;
	}

	if (regcomp(&regexbuf_group, groupmatch, REG_EXTENDED | REG_NOSUB)) {
		ast_log(LOG_ERROR, "Regex compile failed on: %s\n", groupmatch);
		ast_free(foundgroup_str);
		return -1;
	}

	if (regcomp(&regexbuf_category, categorymatch, REG_EXTENDED | REG_NOSUB)) {
		ast_log(LOG_ERROR, "Regex compile failed on: %s\n", categorymatch);
		regfree(&regexbuf_group);
		ast_free(foundgroup_str);
		return -1;
	}

	/* Traverse all groups, and keep track of what we find */
	ast_app_group_meta_rdlock();
	for (gmi = ast_app_group_meta_head(); gmi; gmi = AST_LIST_NEXT(gmi, group_meta_list)) {
		if (!regexec(&regexbuf_group, gmi->group, 0, NULL, 0) && (ast_strlen_zero(categorymatch) || (!ast_strlen_zero(gmi->category) && !regexec(&regexbuf_category, gmi->category, 0, NULL, 0)))) {
			if (groups_found > 0) {
				ast_str_append(&foundgroup_str, 0, ",");
			}

			if (ast_strlen_zero(gmi->category)) {
				ast_str_append(&foundgroup_str, 0, "%s", gmi->group);
			} else {
				ast_str_append(&foundgroup_str, 0, "%s@%s", gmi->group, gmi->category);
			}

			groups_found++;
		}
	}
	ast_app_group_meta_unlock();

	snprintf(buf, len, "%s", ast_str_buffer(foundgroup_str));

	ast_free(foundgroup_str);
	regfree(&regexbuf_group);
	regfree(&regexbuf_category);

	return 0;
}

static int dumpgroups_exec(struct ast_channel *chan, const char *data)
{
#define FORMAT_STRING_CHANNELS	"%-25s	%-20s  %-20s\n"
#define FORMAT_STRING_GROUPS	 "%-20s	 %-20s\n"
#define FORMAT_STRING_VAR "	%s=%s\n"

	static char *line     = "================================================================================";
	static char *thinline = "-----------------------------------";

	struct ast_group_info *gi = NULL;
	struct ast_group_meta *gmi = NULL;
	struct varshead *headp;
	struct ast_var_t *variable = NULL;
	int numchans = 0;
	struct ast_str *out = ast_str_create(4096);

	if (!out) {
		ast_log(LOG_ERROR, "Unable to allocate new variable.  Cannot proceed with DumpGroups().\n");
		return -1;
	}

	ast_verbose("%s\n", line);
	ast_verbose(FORMAT_STRING_CHANNELS, "Channel", "Group", "Category\n");

	ast_app_group_list_rdlock();
	gi = ast_app_group_list_head();
	while (gi) {
		ast_str_append(&out, 0, FORMAT_STRING_CHANNELS, ast_channel_name(gi->chan), gi->group, (ast_strlen_zero(gi->category) ? "(default)" : gi->category));
		numchans++;
		gi = AST_LIST_NEXT(gi, group_list);
	}

	ast_app_group_list_unlock();

	ast_str_append(&out, 0, "%d active group assignment%s\n", numchans, ESS(numchans));

	ast_str_append(&out, 0, "%s\n", thinline);
	/****************** Group Variables ******************/

	ast_str_append(&out, 0, "Group	  Variables    Category\n");

	/* Print group variables */
	ast_app_group_meta_rdlock();
	gmi = ast_app_group_meta_head();
	while (gmi) {
		ast_str_append(&out, 0, FORMAT_STRING_GROUPS, gmi->group, (strcmp(gmi->category, "") ? gmi->category : "(Default)"));
		headp = &gmi->varshead;

		AST_LIST_TRAVERSE(headp, variable, entries) {
			ast_str_append(&out, 0, FORMAT_STRING_VAR, ast_var_name(variable), ast_var_value(variable));
		}

		gmi = AST_LIST_NEXT(gmi, group_meta_list);
	}
	ast_app_group_meta_unlock();

	ast_str_append(&out, 0, "%s\n", line);

	ast_verbose("%s\n", ast_str_buffer(out));

	ast_free(out);

	return 0;

#undef FORMAT_STRING_CHANNELS
#undef FORMAT_STRING_GROUPS
#undef FORMAT_STRING_VAR
}

static struct ast_custom_function group_match_list_function = {
	.name = "GROUP_MATCH_LIST",
	.read = group_match_list_function_read,
	.write = NULL,
};

static struct ast_custom_function group_list_function = {
	.name = "GROUP_LIST",
	.read = group_list_function_read,
	.write = NULL,
};

/*!
 * \internal
 * \brief Check whether a channel name is already present in a comma separated list
 */
static int channel_list_contains(const char *list, size_t list_len, const char *name, size_t name_len)
{
	const char *pos = list;
	const char *end = list + list_len;

	while (pos < end) {
		const char *comma = memchr(pos, ',', end - pos);
		size_t token_len = comma ? (size_t) (comma - pos) : (size_t) (end - pos);

		if (token_len == name_len && !strncmp(pos, name, name_len)) {
			return 1;
		}
		if (!comma) {
			break;
		}
		pos = comma + 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Build a comma separated list of the channels that are members of matching groups
 *
 * An empty group or category matches anything.  When group_regex is NULL the group
 * and category are compared exactly (case insensitive), otherwise group_regex and
 * category_regex (which must both be valid) are used.
 *
 * A channel is only listed once, even if it is a member of several matching groups.
 * The list never contains a partial channel name.
 *
 * \retval 0 The complete list was written to buf
 * \retval 1 The list did not fit in buf and was truncated
 */
static int group_channel_list_build(char *buf, size_t len, const char *group, const char *category,
	const regex_t *group_regex, const regex_t *category_regex)
{
	struct ast_group_info *gi;
	size_t out_len = 0;
	int truncated = 0;

	buf[0] = '\0';

	ast_app_group_list_rdlock();
	for (gi = ast_app_group_list_head(); gi; gi = AST_LIST_NEXT(gi, group_list)) {
		const char *name;
		size_t name_len;
		size_t needed;

		if (group_regex) {
			if (regexec(group_regex, gi->group, 0, NULL, 0)) {
				continue;
			}
			if (!ast_strlen_zero(category) && (ast_strlen_zero(gi->category) || regexec(category_regex, gi->category, 0, NULL, 0))) {
				continue;
			}
		} else {
			if (!ast_strlen_zero(group) && strcasecmp(gi->group, group)) {
				continue;
			}
			if (!ast_strlen_zero(category) && (ast_strlen_zero(gi->category) || strcasecmp(gi->category, category))) {
				continue;
			}
		}

		name = ast_channel_name(gi->chan);
		name_len = strlen(name);

		if (channel_list_contains(buf, out_len, name, name_len)) {
			continue;
		}

		/* Optional separator, the name and the terminating NULL */
		needed = (out_len ? 1 : 0) + name_len + 1;
		if (out_len + needed > len) {
			truncated = 1;
			break;
		}

		if (out_len) {
			buf[out_len++] = ',';
		}
		memcpy(buf + out_len, name, name_len);
		out_len += name_len;
		buf[out_len] = '\0';
	}
	ast_app_group_list_unlock();

	return truncated;
}

/*!
 * \internal
 * \brief Set, or clear, the variable that reports a truncated channel list
 *
 * Only a channel has variables to set.  With no channel pbx_builtin_setvar_helper()
 * would modify a global variable instead, so do nothing.
 */
static void channel_list_set_error(struct ast_channel *chan, const char *variable, int truncated)
{
	if (chan) {
		pbx_builtin_setvar_helper(chan, variable, truncated ? "TRUNCATED" : NULL);
	}
}

static int group_channel_list_function_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char group[MAX_GROUP_LEN] = "";
	char category[MAX_CATEGORY_LEN] = "";
	int truncated;

	buf[0] = '\0';

	if (ast_app_group_split_group(data, group, sizeof(group), category, sizeof(category))) {
		ast_log(LOG_WARNING, "GROUP_CHANNEL_LIST() Unable to parse group@category '%s'\n", S_OR(data, ""));
		return -1;
	}

	truncated = group_channel_list_build(buf, len, group, category, NULL, NULL);
	if (truncated) {
		ast_log(LOG_WARNING, "GROUP_CHANNEL_LIST(%s) Channel membership list too large for buffer, truncating.\n", S_OR(data, ""));
	}

	/* Done after the group list lock is released, setting a variable locks the channel */
	channel_list_set_error(chan, "GROUP_CHANNEL_LIST_ERROR", truncated);

	return 0;
}

static struct ast_custom_function group_channel_list_function = {
	.name = "GROUP_CHANNEL_LIST",
	.read = group_channel_list_function_read,
};

static int group_match_channel_list_function_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char groupmatch[MAX_GROUP_LEN] = "";
	char categorymatch[MAX_CATEGORY_LEN] = "";
	regex_t regexbuf_group;
	regex_t regexbuf_category;
	int truncated;

	buf[0] = '\0';

	if (ast_app_group_split_group(data, groupmatch, sizeof(groupmatch), categorymatch, sizeof(categorymatch))) {
		ast_log(LOG_WARNING, "GROUP_MATCH_CHANNEL_LIST() Unable to parse group@category '%s'\n", S_OR(data, ""));
		return -1;
	}

	if (regcomp(&regexbuf_group, groupmatch, REG_EXTENDED | REG_NOSUB)) {
		ast_log(LOG_ERROR, "Regex compile failed on: %s\n", groupmatch);
		return -1;
	}

	if (regcomp(&regexbuf_category, categorymatch, REG_EXTENDED | REG_NOSUB)) {
		ast_log(LOG_ERROR, "Regex compile failed on: %s\n", categorymatch);
		regfree(&regexbuf_group);
		return -1;
	}

	truncated = group_channel_list_build(buf, len, groupmatch, categorymatch, &regexbuf_group, &regexbuf_category);
	if (truncated) {
		ast_log(LOG_WARNING, "GROUP_MATCH_CHANNEL_LIST(%s) Channel membership list too large for buffer, truncating.\n", S_OR(data, ""));
	}

	regfree(&regexbuf_group);
	regfree(&regexbuf_category);

	channel_list_set_error(chan, "GROUP_MATCH_CHANNEL_LIST_ERROR", truncated);

	return 0;
}

static struct ast_custom_function group_match_channel_list_function = {
	.name = "GROUP_MATCH_CHANNEL_LIST",
	.read = group_match_channel_list_function_read,
};

#ifdef TEST_FRAMEWORK

/*!
 * \internal
 * \brief Run GROUP_CHANNEL_LIST or GROUP_MATCH_CHANNEL_LIST and compare the result
 *
 * \retval 1 The result matched the expected value
 */
static int check_channel_list(struct ast_test *test, int match, const char *data, const char *expected_fmt, ...)
	__attribute__((format(printf, 4, 5)));

static int check_channel_list(struct ast_test *test, int match, const char *data, const char *expected_fmt, ...)
{
	char expected[512];
	char buf[512];
	char *args = ast_strdupa(data);
	va_list ap;
	int res;

	va_start(ap, expected_fmt);
	vsnprintf(expected, sizeof(expected), expected_fmt, ap);
	va_end(ap);

	if (match) {
		res = group_match_channel_list_function_read(NULL, "GROUP_MATCH_CHANNEL_LIST", args, buf, sizeof(buf));
	} else {
		res = group_channel_list_function_read(NULL, "GROUP_CHANNEL_LIST", args, buf, sizeof(buf));
	}

	if (res || strcmp(buf, expected)) {
		ast_test_status_update(test, "%s(%s): expected '%s', got '%s' (result %d)\n",
			match ? "GROUP_MATCH_CHANNEL_LIST" : "GROUP_CHANNEL_LIST", data, expected, buf, res);
		return 0;
	}

	return 1;
}

AST_TEST_DEFINE(test_group_channel_lists)
{
	struct ast_channel *chans[4] = { NULL, };
	const char *names[4] = { NULL, };
	enum ast_test_result_state res = AST_TEST_PASS;
	char buf[512];
	char data[128];
	const char *var;
	size_t i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "group_channel_lists";
		info->category = "/funcs/func_groupcount/";
		info->summary = "GROUP_CHANNEL_LIST, GROUP_MATCH_CHANNEL_LIST and GROUP_MATCH_LIST";
		info->description =
			"Tests exact and regular expression matching of the group channel list\n"
			"functions, that channels are only listed once, that truncation never\n"
			"returns a partial channel name, and that the error variables are only\n"
			"touched when there is a channel to set them on.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(chans); i++) {
		chans[i] = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, 0, "TestGroupChannel%d", (int) i);
		if (!chans[i]) {
			res = AST_TEST_FAIL;
			goto cleanup;
		}
		ast_channel_unlock(chans[i]);
		names[i] = ast_channel_name(chans[i]);
	}

	/* chans[1] is in two groups, with different categories, so that matching
	 * more than one of them must not list it twice */
	ast_app_group_set_channel(chans[0], "gclgroupname@gclcategoryname");
	ast_app_group_set_channel(chans[1], "gclfoo@gclbar");
	ast_app_group_set_channel(chans[2], "gclfoo@gclbarbill");
	ast_app_group_set_channel(chans[3], "gclanything@gclsomethingelse");
	ast_app_group_set_channel(chans[1], "gclfoo2@gclbar2");

	/* Exact matching */
	if (!check_channel_list(test, 0, "gclfoo@gclbar", "%s", names[1])
		|| !check_channel_list(test, 0, "GCLFOO@GCLBAR", "%s", names[1])
		|| !check_channel_list(test, 0, "gclfoo", "%s,%s", names[1], names[2])
		|| !check_channel_list(test, 0, "@gclbar", "%s", names[1])
		|| !check_channel_list(test, 0, "gclfoo@nosuchcategory", "%s", "")
		|| !check_channel_list(test, 0, "nosuchgroup", "%s", "")
		|| !check_channel_list(test, 0, "", "%s,%s,%s,%s", names[0], names[1], names[2], names[3])) {
		res = AST_TEST_FAIL;
	}

	/* Regular expression matching.  A match on the group name or category
	 * alone is not exact, 'gclfoo' also matches 'gclfoo2' */
	if (!check_channel_list(test, 1, "gclfoo.*", "%s,%s", names[1], names[2])
		|| !check_channel_list(test, 1, "@.*gclbar.*", "%s,%s", names[1], names[2])
		|| !check_channel_list(test, 1, "gclfoo.*@.*gclbar.*", "%s,%s", names[1], names[2])
		|| !check_channel_list(test, 1, "^gclfoo$@^gclbar$", "%s", names[1])
		|| !check_channel_list(test, 1, "gclfoo.*@^gclbarbill$", "%s", names[2])
		|| !check_channel_list(test, 1, "nomatchxyz", "%s", "")
		|| !check_channel_list(test, 1, "", "%s,%s,%s,%s", names[0], names[1], names[2], names[3])) {
		res = AST_TEST_FAIL;
	}

	/* An invalid regular expression is an error, not an empty result */
	snprintf(data, sizeof(data), "[[");
	if (group_match_channel_list_function_read(NULL, "GROUP_MATCH_CHANNEL_LIST", data, buf, sizeof(buf)) != -1) {
		ast_test_status_update(test, "GROUP_MATCH_CHANNEL_LIST with an invalid regex should fail\n");
		res = AST_TEST_FAIL;
	}

	/* GROUP_MATCH_LIST lists group@category, in the order the groups were created */
	snprintf(data, sizeof(data), "gclfoo.*");
	if (group_match_list_function_read(NULL, "GROUP_MATCH_LIST", data, buf, sizeof(buf))
		|| strcmp(buf, "gclfoo@gclbar,gclfoo@gclbarbill,gclfoo2@gclbar2")) {
		ast_test_status_update(test, "GROUP_MATCH_LIST(%s): unexpected result '%s'\n", data, buf);
		res = AST_TEST_FAIL;
	}

	/* Truncation: room for the first channel name only.  The partial name of
	 * the second channel must not be written, and the caller is told. */
	snprintf(data, sizeof(data), "gclfoo.*");
	if (group_match_channel_list_function_read(chans[0], "GROUP_MATCH_CHANNEL_LIST", data, buf, strlen(names[1]) + 1)
		|| strcmp(buf, names[1])) {
		ast_test_status_update(test, "GROUP_MATCH_CHANNEL_LIST truncation: expected '%s', got '%s'\n", names[1], buf);
		res = AST_TEST_FAIL;
	}
	var = pbx_builtin_getvar_helper(chans[0], "GROUP_MATCH_CHANNEL_LIST_ERROR");
	if (ast_strlen_zero(var) || strcmp(var, "TRUNCATED")) {
		ast_test_status_update(test, "GROUP_MATCH_CHANNEL_LIST_ERROR should be TRUNCATED, got '%s'\n", S_OR(var, ""));
		res = AST_TEST_FAIL;
	}

	/* A complete result clears the error again */
	if (group_match_channel_list_function_read(chans[0], "GROUP_MATCH_CHANNEL_LIST", data, buf, sizeof(buf))
		|| pbx_builtin_getvar_helper(chans[0], "GROUP_MATCH_CHANNEL_LIST_ERROR")) {
		ast_test_status_update(test, "GROUP_MATCH_CHANNEL_LIST_ERROR should be cleared by a complete result\n");
		res = AST_TEST_FAIL;
	}

	snprintf(data, sizeof(data), "gclfoo");
	if (group_channel_list_function_read(chans[0], "GROUP_CHANNEL_LIST", data, buf, strlen(names[1]) + 1)
		|| strcmp(buf, names[1])) {
		ast_test_status_update(test, "GROUP_CHANNEL_LIST truncation: expected '%s', got '%s'\n", names[1], buf);
		res = AST_TEST_FAIL;
	}
	var = pbx_builtin_getvar_helper(chans[0], "GROUP_CHANNEL_LIST_ERROR");
	if (ast_strlen_zero(var) || strcmp(var, "TRUNCATED")) {
		ast_test_status_update(test, "GROUP_CHANNEL_LIST_ERROR should be TRUNCATED, got '%s'\n", S_OR(var, ""));
		res = AST_TEST_FAIL;
	}

	/* Without a channel there is nowhere to set the error variable.  It must
	 * not be set as a global variable instead. */
	snprintf(data, sizeof(data), "gclfoo");
	group_channel_list_function_read(NULL, "GROUP_CHANNEL_LIST", data, buf, strlen(names[1]) + 1);
	snprintf(data, sizeof(data), "gclfoo.*");
	group_match_channel_list_function_read(NULL, "GROUP_MATCH_CHANNEL_LIST", data, buf, strlen(names[1]) + 1);
	if (pbx_builtin_getvar_helper(NULL, "GROUP_CHANNEL_LIST_ERROR")
		|| pbx_builtin_getvar_helper(NULL, "GROUP_MATCH_CHANNEL_LIST_ERROR")) {
		ast_test_status_update(test, "An error variable was set as a global variable when there was no channel\n");
		pbx_builtin_setvar_helper(NULL, "GROUP_CHANNEL_LIST_ERROR", NULL);
		pbx_builtin_setvar_helper(NULL, "GROUP_MATCH_CHANNEL_LIST_ERROR", NULL);
		res = AST_TEST_FAIL;
	}

cleanup:
	for (i = 0; i < ARRAY_LEN(chans); i++) {
		if (chans[i]) {
			ast_hangup(chans[i]);
		}
	}

	return res;
}

#endif /* TEST_FRAMEWORK */

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&group_count_function);
	res |= ast_custom_function_unregister(&group_match_count_function);
	res |= ast_custom_function_unregister(&group_match_list_function);
	res |= ast_custom_function_unregister(&group_match_channel_list_function);
	res |= ast_custom_function_unregister(&group_list_function);
	res |= ast_custom_function_unregister(&group_channel_list_function);
	res |= ast_custom_function_unregister(&group_var_function);
	res |= ast_custom_function_unregister(&group_function);

	AST_TEST_UNREGISTER(test_group_channel_lists);

	res |= ast_unregister_application(dumpgroups_app);

	res |= ast_manager_unregister("GroupSet");
	res |= ast_manager_unregister("GroupRemove");
	res |= ast_manager_unregister("GroupVarGet");
	res |= ast_manager_unregister("GroupVarSet");
	res |= ast_manager_unregister("GroupsShow");
	res |= ast_manager_unregister("GroupsShowChannels");
	res |= ast_manager_unregister("GroupsShowVariables");

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&group_count_function);
	res |= ast_custom_function_register(&group_match_count_function);
	res |= ast_custom_function_register(&group_match_list_function);
	res |= ast_custom_function_register(&group_match_channel_list_function);
	res |= ast_custom_function_register(&group_list_function);
	res |= ast_custom_function_register(&group_channel_list_function);
	res |= ast_custom_function_register(&group_var_function);
	res |= ast_custom_function_register(&group_function);

	AST_TEST_REGISTER(test_group_channel_lists);

	res |= ast_register_application_xml(dumpgroups_app, dumpgroups_exec);

	res |= ast_manager_register_xml("GroupSet",    EVENT_FLAG_CALL, manager_group_set);
	res |= ast_manager_register_xml("GroupRemove", EVENT_FLAG_CALL, manager_group_remove);
	res |= ast_manager_register_xml("GroupVarGet", EVENT_FLAG_CALL, manager_group_var_get);
	res |= ast_manager_register_xml("GroupVarSet", EVENT_FLAG_CALL, manager_group_var_set);
	res |= ast_manager_register_xml("GroupsShow",  EVENT_FLAG_REPORTING, manager_groups_show);
	res |= ast_manager_register_xml("GroupsShowChannels",  EVENT_FLAG_REPORTING, manager_groups_show_channels);
	res |= ast_manager_register_xml("GroupsShowVariables", EVENT_FLAG_REPORTING, manager_groups_show_variables);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel group dialplan functions");
