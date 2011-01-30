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

/*** DOCUMENTATION
	<function name="REALTIME" language="en_US">
		<synopsis>
			RealTime Read/Write Functions.
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="fieldmatch" required="true" />
			<parameter name="value" />
			<parameter name="delim1|field">
				<para>Use <replaceable>delim1</replaceable> with <replaceable>delim2</replaceable> on
				read and <replaceable>field</replaceable> without <replaceable>delim2</replaceable> on
				write</para>
				<para>If we are reading and <replaceable>delim1</replaceable> is not specified, defaults
				to <literal>,</literal></para>
			</parameter>
			<parameter name="delim2">
				<para>Parameter only used when reading, if not specified defaults to <literal>=</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>This function will read or write values from/to a RealTime repository.
			REALTIME(....) will read names/values from the repository, and 
			REALTIME(....)= will write a new value/field to the repository. On a
			read, this function returns a delimited text string. The name/value
			pairs are delimited by <replaceable>delim1</replaceable>, and the name and value are delimited
			between each other with delim2. 
			If there is no match, NULL will be returned by the function.
			On a write, this function will always return NULL.</para>
		</description>
		<see-also>
			<ref type="function">REALTIME_STORE</ref>
			<ref type="function">REALTIME_DESTROY</ref>
			<ref type="function">REALTIME_FIELD</ref>
			<ref type="function">REALTIME_HASH</ref>
		</see-also>
	</function>
	<function name="REALTIME_STORE" language="en_US">
		<synopsis>
			RealTime Store Function.
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="field1" required="true" />
			<parameter name="fieldN" required="true" multiple="true" />
			<parameter name="field30" required="true" />
		</syntax>
		<description>
			<para>This function will insert a new set of values into the RealTime repository.
			If RT engine provides an unique ID of the stored record, REALTIME_STORE(...)=..
			creates channel variable named RTSTOREID, which contains value of unique ID.
			Currently, a maximum of 30 field/value pairs is supported.</para>
		</description>
		<see-also>
			<ref type="function">REALTIME</ref>
			<ref type="function">REALTIME_DESTROY</ref>
			<ref type="function">REALTIME_FIELD</ref>
			<ref type="function">REALTIME_HASH</ref>
		</see-also>
	</function>
	<function name="REALTIME_DESTROY" language="en_US">
		<synopsis>
			RealTime Destroy Function.
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="fieldmatch" required="true" />
			<parameter name="value" />
			<parameter name="delim1" />
			<parameter name="delim2" />
		</syntax>
		<description>
			<para>This function acts in the same way as REALTIME(....) does, except that
			it destroys the matched record in the RT engine.</para>
		</description>
		<see-also>
			<ref type="function">REALTIME</ref>
			<ref type="function">REALTIME_STORE</ref>
			<ref type="function">REALTIME_FIELD</ref>
			<ref type="function">REALTIME_HASH</ref>
		</see-also>
	</function>
	<function name="REALTIME_FIELD" language="en_US">
		<synopsis>
			RealTime query function.
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="fieldmatch" required="true" />
			<parameter name="value" required="true" />
			<parameter name="fieldname" required="true" />
		</syntax>
		<description>
			<para>This function retrieves a single item, <replaceable>fieldname</replaceable>
			from the RT engine, where <replaceable>fieldmatch</replaceable> contains the value
			<replaceable>value</replaceable>.  When written to, the REALTIME_FIELD() function
			performs identically to the REALTIME() function.</para>
		</description>
		<see-also>
			<ref type="function">REALTIME</ref>
			<ref type="function">REALTIME_STORE</ref>
			<ref type="function">REALTIME_DESTROY</ref>
			<ref type="function">REALTIME_HASH</ref>
		</see-also>
	</function>
	<function name="REALTIME_HASH" language="en_US">
		<synopsis>
			RealTime query function.
		</synopsis>
		<syntax>
			<parameter name="family" required="true" />
			<parameter name="fieldmatch" required="true" />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>This function retrieves a single record from the RT engine, where
			<replaceable>fieldmatch</replaceable> contains the value
			<replaceable>value</replaceable> and formats the output suitably, such that
			it can be assigned to the HASH() function.  The HASH() function then provides
			a suitable method for retrieving each field value of the record.</para>
		</description>
		<see-also>
			<ref type="function">REALTIME</ref>
			<ref type="function">REALTIME_STORE</ref>
			<ref type="function">REALTIME_DESTROY</ref>
			<ref type="function">REALTIME_FIELD</ref>
		</see-also>
	</function>
 ***/

AST_THREADSTORAGE(buf1);
AST_THREADSTORAGE(buf2);
AST_THREADSTORAGE(buf3);

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
	ast_copy_string(buf, ast_str_buffer(out), len);

	ast_variables_destroy(head);

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
		ast_log(LOG_WARNING, "Syntax: %s(family,fieldmatch,value,newcol) - missing argument!\n", cmd);
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

static int realtimefield_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len) 
{
	struct ast_variable *var, *head;
	struct ast_str *escapebuf = ast_str_thread_get(&buf1, 16);
	struct ast_str *fields = ast_str_thread_get(&buf2, 16);
	struct ast_str *values = ast_str_thread_get(&buf3, 16);
	int first = 0;
	enum { rtfield, rthash } which;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(family);
		AST_APP_ARG(fieldmatch);
		AST_APP_ARG(value);
		AST_APP_ARG(fieldname);
	);

	if (!strcmp(cmd, "REALTIME_FIELD")) {
		which = rtfield;
	} else {
		which = rthash;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: %s(family,fieldmatch,value%s) - missing argument!\n", cmd, which == rtfield ? ",fieldname" : "");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if ((which == rtfield && args.argc != 4) || (which == rthash && args.argc != 3)) {
		ast_log(LOG_WARNING, "Syntax: %s(family,fieldmatch,value%s) - missing argument!\n", cmd, which == rtfield ? ",fieldname" : "");
		return -1;
	}

	if (chan) {
		ast_autoservice_start(chan);
	}

	if (!(head = ast_load_realtime_all(args.family, args.fieldmatch, args.value, SENTINEL))) {
		if (chan) {
			ast_autoservice_stop(chan);
		}
		return -1;
	}

	ast_str_reset(fields);
	ast_str_reset(values);

	for (var = head; var; var = var->next) {
		if (which == rtfield) {
			ast_debug(1, "Comparing %s to %s\n", var->name, args.fieldname);
			if (!strcasecmp(var->name, args.fieldname)) {
				ast_debug(1, "Match! Value is %s\n", var->value);
				ast_copy_string(buf, var->value, len);
				break;
			}
		} else if (which == rthash) {
			ast_debug(1, "Setting hash key %s to value %s\n", var->name, var->value);
			ast_str_append(&fields, 0, "%s%s", first ? "" : ",", ast_str_set_escapecommas(&escapebuf, 0, var->name, INT_MAX));
			ast_str_append(&values, 0, "%s%s", first ? "" : ",", ast_str_set_escapecommas(&escapebuf, 0, var->value, INT_MAX));
			first = 0;
		}
	}
	ast_variables_destroy(head);

	if (which == rthash) {
		pbx_builtin_setvar_helper(chan, "~ODBCFIELDS~", ast_str_buffer(fields));
		ast_copy_string(buf, ast_str_buffer(values), len);
	}

	if (chan) {
		ast_autoservice_stop(chan);
	}

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
	ast_copy_string(buf, ast_str_buffer(out), len);

	ast_destroy_realtime(args.family, args.fieldmatch, args.value, SENTINEL);
	ast_variables_destroy(head);

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static struct ast_custom_function realtime_function = {
	.name = "REALTIME",
	.read = function_realtime_read,
	.write = function_realtime_write,
};

static struct ast_custom_function realtimefield_function = {
	.name = "REALTIME_FIELD",
	.read = realtimefield_read,
	.write = function_realtime_write,
};

static struct ast_custom_function realtimehash_function = {
	.name = "REALTIME_HASH",
	.read = realtimefield_read,
};

static struct ast_custom_function realtime_store_function = {
	.name = "REALTIME_STORE",
	.write = function_realtime_store,
};

static struct ast_custom_function realtime_destroy_function = {
	.name = "REALTIME_DESTROY",
	.read = function_realtime_readdestroy,
};

static int unload_module(void)
{
	int res = 0;
	res |= ast_custom_function_unregister(&realtime_function);
	res |= ast_custom_function_unregister(&realtime_store_function);
	res |= ast_custom_function_unregister(&realtime_destroy_function);
	res |= ast_custom_function_unregister(&realtimefield_function);
	res |= ast_custom_function_unregister(&realtimehash_function);
	return res;
}

static int load_module(void)
{
	int res = 0;
	res |= ast_custom_function_register(&realtime_function);
	res |= ast_custom_function_register(&realtime_store_function);
	res |= ast_custom_function_register(&realtime_destroy_function);
	res |= ast_custom_function_register(&realtimefield_function);
	res |= ast_custom_function_register(&realtimehash_function);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Read/Write/Store/Destroy values from a RealTime repository");
