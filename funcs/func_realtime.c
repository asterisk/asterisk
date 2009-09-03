/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, BJ Weschke. All rights reserved.
 * 
 * BJ Weschke <bweschke@btwtech.com>
 * 
 * This code is released by the author with no restrictions on usage. 
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief REALTIME dialplan function
 * 
 * \author BJ Weschke <bweschke@btwtech.com>
 * 
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static int function_realtime_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	struct ast_variable *var, *head;
        struct ast_module_user *u;
	char *results, *result_begin;
	size_t resultslen = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(family);
		AST_APP_ARG(fieldmatch);
		AST_APP_ARG(value);
		AST_APP_ARG(delim1);
		AST_APP_ARG(delim2);
	);


	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: REALTIME(family|fieldmatch[|value[|delim1[|delim2]]]) - missing argument!\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	AST_STANDARD_APP_ARGS(args, data);

	if (!args.delim1)
		args.delim1 = "|";
	if (!args.delim2)
		args.delim2 = "=";

	if (chan)
		ast_autoservice_start(chan);

	head = ast_load_realtime(args.family, args.fieldmatch, args.value, NULL);

	if (!head) {
		ast_module_user_remove(u);
		if (chan)
			ast_autoservice_stop(chan);
		return -1;
	}
	for (var = head; var; var = var->next)
		resultslen += strlen(var->name) + strlen(var->value) + strlen(args.delim1) + strlen(args.delim2);

	result_begin = results = alloca(resultslen);
	for (var = head; var; var = var->next)
		ast_build_string(&results, &resultslen, "%s%s%s%s", var->name, args.delim2, var->value, args.delim1);
	ast_copy_string(buf, result_begin, len);

	ast_variables_destroy(head);

	ast_module_user_remove(u);

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static int function_realtime_write(struct ast_channel *chan, char *cmd, char *data, const char *value)
{
        struct ast_module_user *u = NULL;
	int res = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(family);
		AST_APP_ARG(fieldmatch);
		AST_APP_ARG(value);
		AST_APP_ARG(field);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: REALTIME(family|fieldmatch|value|newcol) - missing argument!\n");
		return -1;
	}

	if (chan) {
		ast_autoservice_start(chan);
		u = ast_module_user_add(chan);
	}

	AST_STANDARD_APP_ARGS(args, data);

	res = ast_update_realtime(args.family, args.fieldmatch, args.value, args.field, (char *)value, NULL);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to update. Check the debug log for possible data repository related entries.\n");
	}

	if (chan) {
		ast_module_user_remove(u);
		ast_autoservice_stop(chan);
	}

	return 0;
}

struct ast_custom_function realtime_function = {
	.name = "REALTIME",
	.synopsis = "RealTime Read/Write Functions",
	.syntax = "REALTIME(family|fieldmatch[|value[|delim1[|delim2]]]) on read\n"
		  "REALTIME(family|fieldmatch|value|field) on write\n",
	.desc = "This function will read or write values from/to a RealTime repository.\n"
		"REALTIME(....) will read names/values from the repository, and \n"
		"REALTIME(....)= will write a new value/field to the repository. On a\n"
		"read, this function returns a delimited text string. The name/value \n"
		"pairs are delimited by delim1, and the name and value are delimited \n"
		"between each other with delim2. The default for delim1 is '|' and   \n"
		"the default for delim2 is '='. If there is no match, NULL will be   \n"
		"returned by the function. On a write, this function will always     \n"
		"return NULL. \n",
	.read = function_realtime_read,
	.write = function_realtime_write,
};

static int unload_module(void)
{
        int res = ast_custom_function_unregister(&realtime_function);

	ast_module_user_hangup_all();

        return res;
}

static int load_module(void)
{
        int res = ast_custom_function_register(&realtime_function);

        return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Read/Write values from a RealTime repository");
