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

/*! \file
 * 
 * \brief Conditional logic dialplan functions
 * 
 * \author Anthony Minessale II
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static char *isnull(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	return data && *data ? "0" : "1";
}

static char *exists(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	return data && *data ? "1" : "0";
}

static char *iftime(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	struct ast_timing timing;
	char *ret;
	char *expr;
	char *iftrue;
	char *iffalse;

	if (!(data = ast_strdupa(data)))
		return NULL;

	data = ast_strip_quoted(data, "\"", "\"");
	expr = strsep(&data, "?");
	iftrue = strsep(&data, ":");
	iffalse = data;

	if (ast_strlen_zero(expr) || !(iftrue || iffalse)) {
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

static char *acf_if(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *ret;
	char *expr;
	char *iftrue;
	char *iffalse;

	if (!(data = ast_strdupa(data)))
		return NULL;

	data = ast_strip_quoted(data, "\"", "\"");
	expr = strsep(&data, "?");
	iftrue = strsep(&data, ":");
	iffalse = data;

	if (ast_strlen_zero(expr) || !(iftrue || iffalse)) {
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

static char *set(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *varname;
	char *val;

	if (!(data = ast_strdupa(data)))
		return NULL;

	varname = strsep(&data, "=");
	val = data;

	if (ast_strlen_zero(varname) || !val) {
		ast_log(LOG_WARNING, "Syntax SET(<varname>=[<value>])\n");
		return NULL;
	}

	varname = ast_strip(varname);
	val = ast_strip(val);
	pbx_builtin_setvar_helper(chan, varname, val);
	ast_copy_string(buf, val, len);

	return buf;
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
	.synopsis = "Conditional: Returns the data following '?' if true else the data following ':'",
	.syntax = "IF(<expr>?[<true>][:<false>])",
	.read = acf_if,
};

static struct ast_custom_function if_time_function = {
	.name = "IFTIME",
	.synopsis = "Temporal Conditional: Returns the data following '?' if true else the data following ':'",
	.syntax = "IFTIME(<timespec>?[<true>][:<false>])",
	.read = iftime,
};

static char *tdesc = "Logical dialplan functions";

int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&isnull_function);
	res |= ast_custom_function_unregister(&set_function);
	res |= ast_custom_function_unregister(&exists_function);
	res |= ast_custom_function_unregister(&if_function);
	res |= ast_custom_function_unregister(&if_time_function);

	return res;
}

int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&isnull_function);
	res |= ast_custom_function_register(&set_function);
	res |= ast_custom_function_register(&exists_function);
	res |= ast_custom_function_register(&if_function);
	res |= ast_custom_function_register(&if_time_function);

	return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
