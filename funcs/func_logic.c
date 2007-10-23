/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 * Portions Copyright (C) 2005, Anthony Minessale II
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
 * \brief Conditional logic dialplan functions
 * 
 * \author Anthony Minessale II
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static int isnull(struct ast_channel *chan, char *cmd, char *data,
		  char *buf, size_t len)
{
	strcpy(buf, data && *data ? "0" : "1");

	return 0;
}

static int exists(struct ast_channel *chan, char *cmd, char *data, char *buf,
		  size_t len)
{
	strcpy(buf, data && *data ? "1" : "0");

	return 0;
}

static int iftime(struct ast_channel *chan, char *cmd, char *data, char *buf,
		  size_t len)
{
	struct ast_timing timing;
	char *expr;
	char *iftrue;
	char *iffalse;

	data = ast_strip_quoted(data, "\"", "\"");
	expr = strsep(&data, "?");
	iftrue = strsep(&data, ":");
	iffalse = data;

	if (ast_strlen_zero(expr) || !(iftrue || iffalse)) {
		ast_log(LOG_WARNING,
				"Syntax IFTIME(<timespec>?[<true>][:<false>])\n");
		return -1;
	}

	if (!ast_build_timing(&timing, expr)) {
		ast_log(LOG_WARNING, "Invalid Time Spec.\n");
		return -1;
	}

	if (iftrue)
		iftrue = ast_strip_quoted(iftrue, "\"", "\"");
	if (iffalse)
		iffalse = ast_strip_quoted(iffalse, "\"", "\"");

	ast_copy_string(buf, ast_check_timing(&timing) ? iftrue : iffalse, len);

	return 0;
}

static int acf_if(struct ast_channel *chan, char *cmd, char *data, char *buf,
		  size_t len)
{
	AST_DECLARE_APP_ARGS(args1,
		AST_APP_ARG(expr);
		AST_APP_ARG(remainder);
	);
	AST_DECLARE_APP_ARGS(args2,
		AST_APP_ARG(iftrue);
		AST_APP_ARG(iffalse);
	);
	args2.iftrue = args2.iffalse = NULL; /* you have to set these, because if there is nothing after the '?',
											then args1.remainder will be NULL, not a pointer to a null string, and
											then any garbage in args2.iffalse will not be cleared, and you'll crash.
										    -- and if you mod the ast_app_separate_args func instead, you'll really
											mess things up badly, because the rest of everything depends on null args
											for non-specified stuff. */
	
	AST_NONSTANDARD_APP_ARGS(args1, data, '?');
	AST_NONSTANDARD_APP_ARGS(args2, args1.remainder, ':');

	if (ast_strlen_zero(args1.expr) || !(args2.iftrue || args2.iffalse)) {
		ast_log(LOG_WARNING, "Syntax IF(<expr>?[<true>][:<false>])  (expr must be non-null, and either <true> or <false> must be non-null)\n");
		ast_log(LOG_WARNING, "      In this case, <expr>='%s', <true>='%s', and <false>='%s'\n", args1.expr, args2.iftrue, args2.iffalse);
		return -1;
	}

	args1.expr = ast_strip(args1.expr);
	if (args2.iftrue)
		args2.iftrue = ast_strip(args2.iftrue);
	if (args2.iffalse)
		args2.iffalse = ast_strip(args2.iffalse);

	ast_copy_string(buf, pbx_checkcondition(args1.expr) ? (S_OR(args2.iftrue, "")) : (S_OR(args2.iffalse, "")), len);

	return 0;
}

static int set(struct ast_channel *chan, char *cmd, char *data, char *buf,
	       size_t len)
{
	char *varname;
	char *val;

	varname = strsep(&data, "=");
	val = data;

	if (ast_strlen_zero(varname) || !val) {
		ast_log(LOG_WARNING, "Syntax SET(<varname>=[<value>])\n");
		return -1;
	}

	varname = ast_strip(varname);
	val = ast_strip(val);
	pbx_builtin_setvar_helper(chan, varname, val);
	ast_copy_string(buf, val, len);

	return 0;
}

static struct ast_custom_function isnull_function = {
	.name = "ISNULL",
	.synopsis = "NULL Test: Returns 1 if NULL or 0 otherwise",
	.syntax = "ISNULL(<data>)",
	.read = isnull,
};

static struct ast_custom_function set_function = {
	.name = "SET",
	.synopsis = "SET assigns a value to a channel variable",
	.syntax = "SET(<varname>=[<value>])",
	.read = set,
};

static struct ast_custom_function exists_function = {
	.name = "EXISTS",
	.synopsis = "Existence Test: Returns 1 if exists, 0 otherwise",
	.syntax = "EXISTS(<data>)",
	.read = exists,
};

static struct ast_custom_function if_function = {
	.name = "IF",
	.synopsis =
		"Conditional: Returns the data following '?' if true else the data following ':'",
	.syntax = "IF(<expr>?[<true>][:<false>])",
	.read = acf_if,
};

static struct ast_custom_function if_time_function = {
	.name = "IFTIME",
	.synopsis =
		"Temporal Conditional: Returns the data following '?' if true else the data following ':'",
	.syntax = "IFTIME(<timespec>?[<true>][:<false>])",
	.read = iftime,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&isnull_function);
	res |= ast_custom_function_unregister(&set_function);
	res |= ast_custom_function_unregister(&exists_function);
	res |= ast_custom_function_unregister(&if_function);
	res |= ast_custom_function_unregister(&if_time_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&isnull_function);
	res |= ast_custom_function_register(&set_function);
	res |= ast_custom_function_register(&exists_function);
	res |= ast_custom_function_register(&if_function);
	res |= ast_custom_function_register(&if_time_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Logical dialplan functions");
