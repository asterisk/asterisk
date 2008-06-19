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

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static int function_realtime_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len) 
{
	struct ast_variable *var, *head;
	struct ast_str *out;
	size_t resultslen;
	int n;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(family);
		AST_APP_ARG(fieldmatch);
		AST_APP_ARG(value);
		AST_APP_ARG(delim1);
		AST_APP_ARG(delim2);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: REALTIME(family,fieldmatch[,value[,delim1[,delim2]]]) - missing argument!\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (!args.delim1)
		args.delim1 = ",";
	if (!args.delim2)
		args.delim2 = "=";

	if (chan)
		ast_autoservice_start(chan);

	head = ast_load_realtime_all(args.family, args.fieldmatch, args.value, SENTINEL);

	if (!head) {
		if (chan)
			ast_autoservice_stop(chan);
		return -1;
	}

	resultslen = 0;
	n = 0;
	for (var = head; var; n++, var = var->next)
		resultslen += strlen(var->name) + strlen(var->value);
	/* add space for delimiters and final '\0' */
	resultslen += n * (strlen(args.delim1) + strlen(args.delim2)) + 1;

	out = ast_str_alloca(resultslen);
	for (var = head; var; var = var->next)
		ast_str_append(&out, 0, "%s%s%s%s", var->name, args.delim2, var->value, args.delim1);
	ast_copy_string(buf, out->str, len);

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static int function_realtime_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	int res = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(family);
		AST_APP_ARG(fieldmatch);
		AST_APP_ARG(value);
		AST_APP_ARG(field);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: REALTIME(family,fieldmatch,value,newcol) - missing argument!\n");
		return -1;
	}

	if (chan)
		ast_autoservice_start(chan);

	AST_STANDARD_APP_ARGS(args, data);

	res = ast_update_realtime(args.family, args.fieldmatch, args.value, args.field, (char *)value, SENTINEL);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to update. Check the debug log for possible data repository related entries.\n");
	}

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static int function_realtime_store(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	int res = 0;
	char storeid[32];
	char *valcopy;
	AST_DECLARE_APP_ARGS(a,
		AST_APP_ARG(family);
		AST_APP_ARG(f)[30]; /* fields */
	);

	AST_DECLARE_APP_ARGS(v,
		AST_APP_ARG(v)[30]; /* values */
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: REALTIME_STORE(family,field1,field2,...,field30) - missing argument!\n");
		return -1;
	}

	if (chan)
		ast_autoservice_start(chan);

	valcopy = ast_strdupa(value);
	AST_STANDARD_APP_ARGS(a, data);
	AST_STANDARD_APP_ARGS(v, valcopy);

	res = ast_store_realtime(a.family, 
		a.f[0], v.v[0], a.f[1], v.v[1], a.f[2], v.v[2], a.f[3], v.v[3], a.f[4], v.v[4],
		a.f[5], v.v[5], a.f[6], v.v[6], a.f[7], v.v[7], a.f[8], v.v[8], a.f[9], v.v[9],
		a.f[10], v.v[10], a.f[11], v.v[11], a.f[12], v.v[12], a.f[13], v.v[13], a.f[14], v.v[14],
		a.f[15], v.v[15], a.f[16], v.v[16], a.f[17], v.v[17], a.f[18], v.v[18], a.f[19], v.v[19],
		a.f[20], v.v[20], a.f[21], v.v[21], a.f[22], v.v[22], a.f[23], v.v[23], a.f[24], v.v[24],
		a.f[25], v.v[25], a.f[26], v.v[26], a.f[27], v.v[27], a.f[28], v.v[28], a.f[29], v.v[29], SENTINEL
	);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to store. Check the debug log for possible data repository related entries.\n");
	} else {
		snprintf(storeid, sizeof(storeid), "%d", res);
		pbx_builtin_setvar_helper(chan, "RTSTOREID", storeid);
	}

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static int function_realtime_readdestroy(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len) 
{
	struct ast_variable *var, *head;
	struct ast_str *out;
	size_t resultslen;
	int n;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(family);
		AST_APP_ARG(fieldmatch);
		AST_APP_ARG(value);
		AST_APP_ARG(delim1);
		AST_APP_ARG(delim2);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: REALTIME_DESTROY(family,fieldmatch[,value[,delim1[,delim2]]]) - missing argument!\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (!args.delim1)
		args.delim1 = ",";
	if (!args.delim2)
		args.delim2 = "=";

	if (chan)
		ast_autoservice_start(chan);

	head = ast_load_realtime_all(args.family, args.fieldmatch, args.value, SENTINEL);

	if (!head) {
		if (chan)
			ast_autoservice_stop(chan);
		return -1;
	}

	resultslen = 0;
	n = 0;
	for (var = head; var; n++, var = var->next)
		resultslen += strlen(var->name) + strlen(var->value);
	/* add space for delimiters and final '\0' */
	resultslen += n * (strlen(args.delim1) + strlen(args.delim2)) + 1;

	out = ast_str_alloca(resultslen);
	for (var = head; var; var = var->next) {
		ast_str_append(&out, 0, "%s%s%s%s", var->name, args.delim2, var->value, args.delim1);
	}
	ast_copy_string(buf, out->str, len);

	ast_destroy_realtime(args.family, args.fieldmatch, args.value, SENTINEL);

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

struct ast_custom_function realtime_function = {
	.name = "REALTIME",
	.synopsis = "RealTime Read/Write Functions",
	.syntax = "REALTIME(family,fieldmatch[,value[,delim1[,delim2]]]) on read\n"
		  "REALTIME(family,fieldmatch,value,field) on write",
	.desc = "This function will read or write values from/to a RealTime repository.\n"
		"REALTIME(....) will read names/values from the repository, and \n"
		"REALTIME(....)= will write a new value/field to the repository. On a\n"
		"read, this function returns a delimited text string. The name/value \n"
		"pairs are delimited by delim1, and the name and value are delimited \n"
		"between each other with delim2. The default for delim1 is ',' and   \n"
		"the default for delim2 is '='. If there is no match, NULL will be   \n"
		"returned by the function. On a write, this function will always     \n"
		"return NULL. \n",
	.read = function_realtime_read,
	.write = function_realtime_write,
};

struct ast_custom_function realtime_store_function = {
	.name = "REALTIME_STORE",
	.synopsis = "RealTime Store Function",
	.syntax = "REALTIME_STORE(family,field1,field2,...,field30) = value1,value2,...,value30",
	.desc = "This function will insert a new set of values into the RealTime repository.\n"
		"If RT engine provides an unique ID of the stored record, REALTIME_STORE(...)=..\n"
		"creates channel variable named RTSTOREID, which contains value of unique ID.\n"
		"Currently, a maximum of 30 field/value pairs is supported.\n",
	.write = function_realtime_store,
};

struct ast_custom_function realtime_destroy_function = {
	.name = "REALTIME_DESTROY",
	.synopsis = "RealTime Destroy Function",
	.syntax = "REALTIME_DESTROY(family,fieldmatch[,value[,delim1[,delim2]]])\n",
	.desc = "This function acts in the same way as REALTIME(....) does, except that\n"
		"it destroys matched record in RT engine.\n",
	.read = function_realtime_readdestroy,
};

static int unload_module(void)
{
	int res = 0;
	res |= ast_custom_function_unregister(&realtime_function);
	res |= ast_custom_function_unregister(&realtime_store_function);
	res |= ast_custom_function_unregister(&realtime_destroy_function);
	return res;
}

static int load_module(void)
{
	int res = 0;
	res |= ast_custom_function_register(&realtime_function);
	res |= ast_custom_function_register(&realtime_store_function);
	res |= ast_custom_function_register(&realtime_destroy_function);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Read/Write/Store/Destroy values from a RealTime repository");
