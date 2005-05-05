/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Conditional logic dialplan functions
 * 
 * Copyright (C) 2005, Digium, Inc.
 * Portions Copyright (C) 2005, Anthony Minessale II
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
#include "asterisk/config.h"		/* for ast_true */

static char *builtin_function_isnull(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *ret_true = "1", *ret_false = "0";

	return data && *data ? ret_false : ret_true;
}

static char *builtin_function_exists(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *ret_true = "1", *ret_false = "0";

	return data && *data ? ret_true : ret_false;
}

static char *builtin_function_if(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *ret = NULL;
	char *mydata = NULL;
	char *expr = NULL;
	char *iftrue = NULL;
	char *iffalse = NULL;

	if((mydata = ast_strdupa(data))) {
		expr = mydata;
		if ((iftrue = strchr(mydata, '?'))) {
			*iftrue = '\0';
			iftrue++;
			if ((iffalse = strchr(iftrue, ':'))) {
				*iffalse = '\0';
				iffalse++;
			}
		} else 
			iffalse = "";
		if (expr && iftrue) {
			ret = ast_true(expr) ? iftrue : iffalse;
			strncpy(buf, ret, len);
			ret = buf;
		} else {
			ast_log(LOG_WARNING, "Syntax $(if <expr>?[<truecond>][:<falsecond>])\n");
			ret = NULL;
		}
	} else {
		ast_log(LOG_WARNING, "Memory Error!\n");
		ret = NULL;
	}

	return ret;
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
	.syntax = "IF(<expr>?<true>:<false>)",
	.read = builtin_function_if,
};
