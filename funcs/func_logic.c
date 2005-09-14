/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

/*
 * 
 * Conditional logic dialplan functions
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

/* ASTERISK_FILE_VERSION(__FILE__, "$Revision$") */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/config.h"		/* for ast_true */

static char *builtin_function_isnull(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	return data && *data ? "0" : "1";
}

static char *builtin_function_exists(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	return data && *data ? "1" : "0";
}

static char *builtin_function_iftime(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	struct ast_timing timing;
	char *ret;
	char *expr;
	char *iftrue;
	char *iffalse;

	if (!(data = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		return NULL;
	}

	data = ast_strip_quoted(data, "\"", "\"");
	expr = strsep(&data, "?");
	iftrue = strsep(&data, ":");
	iffalse = data;

	if (!expr || ast_strlen_zero(expr) || !(iftrue || iffalse)) {
		ast_log(LOG_WARNING, "Syntax IFTIME(<timespec>?[<true>][:<false>])\n");
		return NULL;
	}

	if (!ast_build_timing(&timing, expr)) {
		ast_log(LOG_WARNING, "Invalid Time Spec.\n");
		return NULL;
	}

	if (iftrue)
		iftrue = ast_strip_quoted(iftrue, "\"", "\"");
	if (iffalse)
		iffalse = ast_strip_quoted(iffalse, "\"", "\"");

	if ((ret = ast_check_timing(&timing) ? iftrue : iffalse)) {
		ast_copy_string(buf, ret, len);
		ret = buf;
	} 
	
	return ret;
}

static char *builtin_function_if(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *ret;
	char *expr;
	char *iftrue;
	char *iffalse;

	if (!(data = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		return NULL;
	}

	data = ast_strip_quoted(data, "\"", "\"");
	expr = strsep(&data, "?");
	iftrue = strsep(&data, ":");
	iffalse = data;

	if (!expr || ast_strlen_zero(expr) || !(iftrue || iffalse)) {
		ast_log(LOG_WARNING, "Syntax IF(<expr>?[<true>][:<false>])\n");
		return NULL;
	}

	expr = ast_strip(expr);
	if (iftrue)
		iftrue = ast_strip_quoted(iftrue, "\"", "\"");
	if (iffalse)
		iffalse = ast_strip_quoted(iffalse, "\"", "\"");

	if ((ret = ast_true(expr) ? iftrue : iffalse)) {
		ast_copy_string(buf, ret, len);
		ret = buf;
	} 
	
	return ret;
}

static char *builtin_function_set(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *varname;
	char *val;

	if (!(data = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		return NULL;
	}

	varname = strsep(&data, "=");
	val = data;

	if (!varname || ast_strlen_zero(varname) || !val) {
		ast_log(LOG_WARNING, "Syntax SET(<varname>=[<value>])\n");
		return NULL;
	}

	varname = ast_strip(varname);
	val = ast_strip(val);
	pbx_builtin_setvar_helper(chan, varname, val);
	ast_copy_string(buf, val, len);

	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function isnull_function = {
	.name = "ISNULL",
	.synopsis = "NULL Test: Returns 1 if NULL or 0 otherwise",
	.syntax = "ISNULL(<data>)",
	.read = builtin_function_isnull,
};

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function set_function = {
	.name = "SET",
	.synopsis = "SET assigns a value to a channel variable",
	.syntax = "SET(<varname>=[<value>])",
	.read = builtin_function_set,
};

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function exists_function = {
	.name = "EXISTS",
	.synopsis = "Existence Test: Returns 1 if exists, 0 otherwise",
	.syntax = "EXISTS(<data>)",
	.read = builtin_function_exists,
};

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function if_function = {
	.name = "IF",
	.synopsis = "Conditional: Returns the data following '?' if true else the data following ':'",
	.syntax = "IF(<expr>?[<true>][:<false>])",
	.read = builtin_function_if,
};


#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function if_time_function = {
	.name = "IFTIME",
	.synopsis = "Temporal Conditional: Returns the data following '?' if true else the data following ':'",
	.syntax = "IFTIME(<timespec>?[<true>][:<false>])",
	.read = builtin_function_iftime,
};
