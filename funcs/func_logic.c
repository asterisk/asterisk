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
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="ISNULL" language="en_US">
		<synopsis>
			Check if a value is NULL.
		</synopsis>
		<syntax>
			<parameter name="data" required="true" />
		</syntax>
		<description>
			<para>Returns <literal>1</literal> if NULL or <literal>0</literal> otherwise.</para>
		</description>
	</function>
	<function name="SET" language="en_US">
		<synopsis>
			SET assigns a value to a channel variable.
		</synopsis>
		<syntax argsep="=">
			<parameter name="varname" required="true" />
			<parameter name="value" />
		</syntax>
		<description>
		</description>
	</function>
	<function name="EXISTS" language="en_US">
		<synopsis>
			Test the existence of a value.
		</synopsis>
		<syntax>
			<parameter name="data" required="true" />
		</syntax>
		<description>
			<para>Returns <literal>1</literal> if exists, <literal>0</literal> otherwise.</para>
		</description>
	</function>
	<function name="IF" language="en_US">
		<synopsis>
			Check for an expresion.
		</synopsis>
		<syntax argsep="?">
			<parameter name="expresion" required="true" />
			<parameter name="retvalue" argsep=":" required="true">
				<argument name="true" />
				<argument name="false" />
			</parameter>
		</syntax>
		<description>
			<para>Returns the data following <literal>?</literal> if true, else the data following <literal>:</literal></para>
		</description>	
	</function>
	<function name="IFTIME" language="en_US">
		<synopsis>
			Temporal Conditional.
		</synopsis>
		<syntax argsep="?">
			<parameter name="timespec" required="true" />
			<parameter name="retvalue" required="true" argsep=":">
				<argument name="true" />
				<argument name="false" />
			</parameter>
		</syntax>
		<description>
			<para>Returns the data following <literal>?</literal> if true, else the data following <literal>:</literal></para>
		</description>
	</function>
	<function name="IMPORT" language="en_US">
		<synopsis>
			Retrieve the value of a variable from another channel.
		</synopsis>
		<syntax>
			<parameter name="channel" required="true" />
			<parameter name="variable" required="true" />
		</syntax>
		<description>
		</description>
	</function>
 ***/

static int isnull(struct ast_channel *chan, const char *cmd, char *data,
		  char *buf, size_t len)
{
	strcpy(buf, data && *data ? "0" : "1");

	return 0;
}

static int exists(struct ast_channel *chan, const char *cmd, char *data, char *buf,
		  size_t len)
{
	strcpy(buf, data && *data ? "1" : "0");

	return 0;
}

static int iftime(struct ast_channel *chan, const char *cmd, char *data, char *buf,
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
		ast_destroy_timing(&timing);
		return -1;
	}

	if (iftrue)
		iftrue = ast_strip_quoted(iftrue, "\"", "\"");
	if (iffalse)
		iffalse = ast_strip_quoted(iffalse, "\"", "\"");

	ast_copy_string(buf, ast_check_timing(&timing) ? S_OR(iftrue, "") : S_OR(iffalse, ""), len);
	ast_destroy_timing(&timing);

	return 0;
}

static int acf_if(struct ast_channel *chan, const char *cmd, char *data, char *buf,
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

static int set(struct ast_channel *chan, const char *cmd, char *data, char *buf,
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

static int set2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **str, ssize_t len)
{
	if (len > -1) {
		ast_str_make_space(str, len == 0 ? strlen(data) : len);
	}
	return set(chan, cmd, data, ast_str_buffer(*str), ast_str_size(*str));
}

static int import_helper(struct ast_channel *chan, const char *cmd, char *data, char *buf, struct ast_str **str, ssize_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(varname);
	);
	AST_STANDARD_APP_ARGS(args, data);
	if (buf) {
		*buf = '\0';
	}

	if (!ast_strlen_zero(args.varname)) {
		struct ast_channel *chan2;

		if ((chan2 = ast_channel_get_by_name(args.channel))) {
			char *s = ast_alloca(strlen(args.varname) + 4);
			sprintf(s, "${%s}", args.varname);
			ast_channel_lock(chan2);
			if (buf) {
				pbx_substitute_variables_helper(chan2, s, buf, len);
			} else {
				ast_str_substitute_variables(str, len, chan2, s);
			}
			ast_channel_unlock(chan2);
			chan2 = ast_channel_unref(chan2);
		}
	}

	return 0;
}

static int import_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	return import_helper(chan, cmd, data, buf, NULL, len);
}

static int import_read2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **str, ssize_t len)
{
	return import_helper(chan, cmd, data, NULL, str, len);
}

static struct ast_custom_function isnull_function = {
	.name = "ISNULL",
	.read = isnull,
	.read_max = 2,
};

static struct ast_custom_function set_function = {
	.name = "SET",
	.read = set,
	.read2 = set2,
};

static struct ast_custom_function exists_function = {
	.name = "EXISTS",
	.read = exists,
	.read_max = 2,
};

static struct ast_custom_function if_function = {
	.name = "IF",
	.read = acf_if,
};

static struct ast_custom_function if_time_function = {
	.name = "IFTIME",
	.read = iftime,
};

static struct ast_custom_function import_function = {
	.name = "IMPORT",
	.read = import_read,
	.read2 = import_read2,
};

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&isnull_function);
	res |= ast_custom_function_register(&set_function);
	res |= ast_custom_function_register(&exists_function);
	res |= ast_custom_function_register(&if_function);
	res |= ast_custom_function_register(&if_time_function);
	res |= ast_custom_function_register(&import_function);

	return res;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Logical dialplan functions");
