/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Tilghman Lesher
 *
 * Tilghman Lesher <func_global__200605@the-tilghman.com>
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
 * \brief Global variable dialplan functions
 *
 * \author Tilghman Lesher <func_global__200605@the-tilghman.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"

/*** DOCUMENTATION
	<function name="GLOBAL" language="en_US">
		<synopsis>
			Gets or sets the global variable specified.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true">
				<para>Global variable name</para>
			</parameter>
		</syntax>
		<description>
			<para>Set or get the value of a global variable specified in <replaceable>varname</replaceable></para>
		</description>
	</function>
	<function name="SHARED" language="en_US">
		<synopsis>
			Gets or sets the shared variable specified.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true">
				<para>Variable name</para>
			</parameter>
			<parameter name="channel">
				<para>If not specified will default to current channel. It is the complete
				channel name: <literal>SIP/12-abcd1234</literal> or the prefix only <literal>SIP/12</literal>.</para>
			</parameter>
		</syntax>
		<description>
			<para>Implements a shared variable area, in which you may share variables between
			channels.</para>
			<para>The variables used in this space are separate from the general namespace of
			the channel and thus <variable>SHARED(foo)</variable> and <variable>foo</variable> 
			represent two completely different variables, despite sharing the same name.</para>
			<para>Finally, realize that there is an inherent race between channels operating
			at the same time, fiddling with each others' internal variables, which is why
			this special variable namespace exists; it is to remind you that variables in
			the SHARED namespace may change at any time, without warning.  You should
			therefore take special care to ensure that when using the SHARED namespace,
			you retrieve the variable and store it in a regular channel variable before
			using it in a set of calculations (or you might be surprised by the result).</para>
		</description>
	</function>

 ***/

static void shared_variable_free(void *data);

static struct ast_datastore_info shared_variable_info = {
	.type = "SHARED_VARIABLES",
	.destroy = shared_variable_free,
};

static void shared_variable_free(void *data)
{
	struct varshead *varshead = data;
	struct ast_var_t *var;

	while ((var = AST_LIST_REMOVE_HEAD(varshead, entries))) {
		ast_var_delete(var);
	}
	ast_free(varshead);
}

static int global_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	const char *var = pbx_builtin_getvar_helper(NULL, data);

	*buf = '\0';

	if (var)
		ast_copy_string(buf, var, len);

	return 0;
}

static int global_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	pbx_builtin_setvar_helper(NULL, data, value);

	return 0;
}

static struct ast_custom_function global_function = {
	.name = "GLOBAL",
	.read = global_read,
	.write = global_write,
};

static int shared_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *varstore;
	struct varshead *varshead;
	struct ast_var_t *var;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(var);
		AST_APP_ARG(chan);
	);
	struct ast_channel *c_ref = NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SHARED() requires an argument: SHARED(<var>[,<chan>])\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (!ast_strlen_zero(args.chan)) {
		char *prefix = alloca(strlen(args.chan) + 2);
		sprintf(prefix, "%s-", args.chan);
		if (!(c_ref = ast_channel_get_by_name(args.chan)) && !(c_ref = ast_channel_get_by_name_prefix(prefix, strlen(prefix)))) {
			ast_log(LOG_ERROR, "Channel '%s' not found!  Variable '%s' will be blank.\n", args.chan, args.var);
			return -1;
		}
		chan = c_ref;
	}

	ast_channel_lock(chan);

	if (!(varstore = ast_channel_datastore_find(chan, &shared_variable_info, NULL))) {
		ast_channel_unlock(chan);
		if (c_ref) {
			c_ref = ast_channel_unref(c_ref);
		}
		return -1;
	}

	varshead = varstore->data;
	*buf = '\0';

	/* Protected by the channel lock */
	AST_LIST_TRAVERSE(varshead, var, entries) {
		if (!strcmp(args.var, ast_var_name(var))) {
			ast_copy_string(buf, ast_var_value(var), len);
			break;
		}
	}

	ast_channel_unlock(chan);

	if (c_ref) {
		c_ref = ast_channel_unref(c_ref);
	}

	return 0;
}

static int shared_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *varstore;
	struct varshead *varshead;
	struct ast_var_t *var;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(var);
		AST_APP_ARG(chan);
	);
	struct ast_channel *c_ref = NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SHARED() requires an argument: SHARED(<var>[,<chan>])\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (!ast_strlen_zero(args.chan)) {
		char *prefix = alloca(strlen(args.chan) + 2);
		sprintf(prefix, "%s-", args.chan);
		if (!(c_ref = ast_channel_get_by_name(args.chan)) && !(c_ref = ast_channel_get_by_name_prefix(prefix, strlen(prefix)))) {
			ast_log(LOG_ERROR, "Channel '%s' not found!  Variable '%s' not set to '%s'.\n", args.chan, args.var, value);
			return -1;
		}
		chan = c_ref;
	}

	ast_channel_lock(chan);

	if (!(varstore = ast_channel_datastore_find(chan, &shared_variable_info, NULL))) {
		if (!(varstore = ast_datastore_alloc(&shared_variable_info, NULL))) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  Shared variable not set.\n");
			ast_channel_unlock(chan);
			if (c_ref) {
				c_ref = ast_channel_unref(c_ref);
			}
			return -1;
		}

		if (!(varshead = ast_calloc(1, sizeof(*varshead)))) {
			ast_log(LOG_ERROR, "Unable to allocate variable structure.  Shared variable not set.\n");
			ast_datastore_free(varstore);
			ast_channel_unlock(chan);
			if (c_ref) {
				c_ref = ast_channel_unref(c_ref);
			}
			return -1;
		}

		varstore->data = varshead;
		ast_channel_datastore_add(chan, varstore);
	}
	varshead = varstore->data;

	/* Protected by the channel lock */
	AST_LIST_TRAVERSE(varshead, var, entries) {
		/* If there's a previous value, remove it */
		if (!strcmp(args.var, ast_var_name(var))) {
			AST_LIST_REMOVE(varshead, var, entries);
			ast_var_delete(var);
			break;
		}
	}

	var = ast_var_assign(args.var, S_OR(value, ""));
	AST_LIST_INSERT_HEAD(varshead, var, entries);
	manager_event(EVENT_FLAG_DIALPLAN, "VarSet", 
		"Channel: %s\r\n"
		"Variable: SHARED(%s)\r\n"
		"Value: %s\r\n"
		"Uniqueid: %s\r\n", 
		chan ? chan->name : "none", args.var, value, 
		chan ? chan->uniqueid : "none");

	ast_channel_unlock(chan);

	if (c_ref) {
		c_ref = ast_channel_unref(c_ref);
	}

	return 0;
}

static struct ast_custom_function shared_function = {
	.name = "SHARED",
	.read = shared_read,
	.write = shared_write,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&global_function);
	res |= ast_custom_function_unregister(&shared_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&global_function);
	res |= ast_custom_function_register(&shared_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Variable dialplan functions");
