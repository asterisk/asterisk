/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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
 * \brief Dialplan group functions check if a dialplan entry exists
 *
 * \author Gregory Nietsky AKA irroot <gregory@networksentry.co.za>
 * \author Russell Bryant <russell@digium.com>
 * 
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="DIALPLAN_EXISTS" language="en_US">
		<synopsis>
			Checks the existence of a dialplan target.
		</synopsis>
		<syntax>
			<parameter name="context" required="true" />
			<parameter name="extension" />
			<parameter name="priority" />
		</syntax>
		<description>
			<para>This function returns <literal>1</literal> if the target exits. Otherwise, it returns <literal>0</literal>.</para>
		</description>
	</function>
	<function name="VALID_EXTEN" language="en_US">
		<synopsis>
			Determine whether an extension exists or not.
		</synopsis>
		<syntax>
			<parameter name="context">
				<para>Defaults to the current context</para>
			</parameter>
			<parameter name="extension" required="true" />
			<parameter name="priority">
				<para>Priority defaults to <literal>1</literal>.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns a true value if the indicated <replaceable>context</replaceable>,
			<replaceable>extension</replaceable>, and <replaceable>priority</replaceable> exist.</para>
			<warning><para>This function has been deprecated in favor of the <literal>DIALPLAN_EXISTS()</literal> function</para></warning>
		</description>
	</function>
 ***/

static int isexten_function_read(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(context);
		AST_APP_ARG(exten);
		AST_APP_ARG(priority);
	);

	strcpy(buf, "0");

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "DIALPLAN_EXISTS() requires an argument\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.priority)) {
		int priority_num;
		if (sscanf(args.priority, "%30d", &priority_num) == 1 && priority_num > 0) {
			int res;
			res = ast_exists_extension(chan, args.context, args.exten, priority_num, 
				S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL));
			if (res)
				strcpy(buf, "1");
		} else {
			int res;
			res = ast_findlabel_extension(chan, args.context, args.exten, args.priority,
				S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL));
			if (res > 0)
				strcpy(buf, "1");
		}
	} else if (!ast_strlen_zero(args.exten)) {
		int res;
		res = ast_exists_extension(chan, args.context, args.exten, 1, 
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL));
		if (res)
			strcpy(buf, "1");
	} else if (!ast_strlen_zero(args.context)) {
		if (ast_context_find(args.context))
			strcpy(buf, "1");
	} else {
		ast_log(LOG_ERROR, "Invalid arguments provided to DIALPLAN_EXISTS\n");
		return -1;
	}

	return 0;
}

static int acf_isexten_exec(struct ast_channel *chan, const char *cmd, char *parse, char *buffer, size_t buflen)
{
	int priority_int;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(context);
		AST_APP_ARG(extension);
		AST_APP_ARG(priority);
	);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.context)) {
		args.context = chan->context;
	}

	if (ast_strlen_zero(args.extension)) {
		ast_log(LOG_WARNING, "Syntax: VALID_EXTEN([<context>],<extension>[,<priority>]) - missing argument <extension>!\n");
		return -1;
	}

	if (ast_strlen_zero(args.priority)) {
		priority_int = 1;
	} else {
		priority_int = atoi(args.priority);
	}

	if (ast_exists_extension(chan, args.context, args.extension, priority_int,
		S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))) {
	    ast_copy_string(buffer, "1", buflen);
	} else {
	    ast_copy_string(buffer, "0", buflen);
	}

	return 0;
}

static struct ast_custom_function isexten_function = {
	.name = "DIALPLAN_EXISTS",
	.read = isexten_function_read,
	.read_max = 2,
};

static struct ast_custom_function acf_isexten = {
	.name = "VALID_EXTEN",
	.read = acf_isexten_exec,
};

static int unload_module(void)
{
	int res = ast_custom_function_unregister(&isexten_function);
	res |= ast_custom_function_unregister(&acf_isexten);
	return res;
}

static int load_module(void)
{
	int res = ast_custom_function_register(&isexten_function);
	res |= ast_custom_function_register(&acf_isexten);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialplan Context/Extension/Priority Checking Functions");
