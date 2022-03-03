/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Roberto Casas.
 * Copyright (C) 2008, Digium, Inc.
 *
 * Roberto Casas <roberto.casas@diaple.com>
 * Russell Bryant <russell@digium.com>
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

/*!
 * \file
 * \brief Originate application
 *
 * \author Roberto Casas <roberto.casas@diaple.com>
 * \author Russell Bryant <russell@digium.com>
 *
 * \ingroup applications
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/format_cache.h"

static const char app_originate[] = "Originate";

/*** DOCUMENTATION
	<application name="Originate" language="en_US">
		<synopsis>
			Originate a call.
		</synopsis>
		<syntax>
			<parameter name="tech_data" required="true">
				<para>Channel technology and data for creating the outbound channel.
                      For example, SIP/1234.</para>
			</parameter>
			<parameter name="type" required="true">
				<para>This should be <literal>app</literal> or <literal>exten</literal>, depending on whether the outbound channel should be connected to an application or extension.</para>
			</parameter>
			<parameter name="arg1" required="true">
				<para>If the type is <literal>app</literal>, then this is the application name.  If the type is <literal>exten</literal>, then this is the context that the channel will be sent to.</para>
			</parameter>
			<parameter name="arg2" required="false">
				<para>If the type is <literal>app</literal>, then this is the data passed as arguments to the application.  If the type is <literal>exten</literal>, then this is the extension that the channel will be sent to.</para>
			</parameter>
			<parameter name="arg3" required="false">
				<para>If the type is <literal>exten</literal>, then this is the priority that the channel is sent to.  If the type is <literal>app</literal>, then this parameter is ignored.</para>
			</parameter>
			<parameter name="timeout" required="false">
				<para>Timeout in seconds. Default is 30 seconds.</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
				<option name="a">
					<para>Originate asynchronously.  In other words, continue in the dialplan
					without waiting for the originated channel to answer.</para>
				</option>
				<option name="b" argsep="^">
					<para>Before originating the outgoing call, Gosub to the specified
					location using the newly created channel.</para>
					<argument name="context" required="false" />
					<argument name="exten" required="false" />
					<argument name="priority" required="true" hasparams="optional" argsep="^">
						<argument name="arg1" multiple="true" required="true" />
						<argument name="argN" />
					</argument>
				</option>
				<option name="B" argsep="^">
					<para>Before originating the outgoing call, Gosub to the specified
					location using the current channel.</para>
					<argument name="context" required="false" />
					<argument name="exten" required="false" />
					<argument name="priority" required="true" hasparams="optional" argsep="^">
						<argument name="arg1" multiple="true" required="true" />
						<argument name="argN" />
					</argument>
				</option>
				<option name="C">
					<para>Comma-separated list of codecs to use for this call.
					Default is <literal>slin</literal>.</para>
				</option>
				<option name="c">
					<para>The caller ID number to use for the called channel. Default is
					the current channel's Caller ID number.</para>
				</option>
				<option name="n">
					<para>The caller ID name to use for the called channel. Default is
					the current channel's Caller ID name.</para>
				</option>
				<option name="v" argsep="^">
					<para>A series of channel variables to set on the destination channel.</para>
					<argument name="var1" multiple="true" argsep="=">
						<argument name="name" required="true" />
						<argument name="value" required="true" />
					</argument>
				</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
		<para>This application originates an outbound call and connects it to a specified extension or application.  This application will block until the outgoing call fails or gets answered, unless the async option is used.  At that point, this application will exit with the status variable set and dialplan processing will continue.</para>
		<para>This application sets the following channel variable before exiting:</para>
		<variablelist>
			<variable name="ORIGINATE_STATUS">
				<para>This indicates the result of the call origination.</para>
				<value name="FAILED"/>
				<value name="SUCCESS"/>
				<value name="BUSY"/>
				<value name="CONGESTION"/>
				<value name="HANGUP"/>
				<value name="RINGING"/>
				<value name="UNKNOWN">
				In practice, you should never see this value.  Please report it to the issue tracker if you ever see it.
				</value>
			</variable>
		</variablelist>
		</description>
	</application>
 ***/


enum {
	OPT_PREDIAL_CALLEE =    (1 << 0),
	OPT_PREDIAL_CALLER =    (1 << 1),
	OPT_ASYNC =             (1 << 2),
	OPT_CALLER_NUM =        (1 << 3),
	OPT_CALLER_NAME =       (1 << 4),
	OPT_CODECS =            (1 << 5),
	OPT_VARIABLES =         (1 << 6),
};

enum {
	OPT_ARG_PREDIAL_CALLEE,
	OPT_ARG_PREDIAL_CALLER,
	OPT_ARG_CALLER_NUM,
	OPT_ARG_CALLER_NAME,
	OPT_ARG_CODECS,
	OPT_ARG_VARIABLES,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(originate_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION('a', OPT_ASYNC),
	AST_APP_OPTION_ARG('b', OPT_PREDIAL_CALLEE, OPT_ARG_PREDIAL_CALLEE),
	AST_APP_OPTION_ARG('B', OPT_PREDIAL_CALLER, OPT_ARG_PREDIAL_CALLER),
	AST_APP_OPTION_ARG('C', OPT_CODECS, OPT_ARG_CODECS),
	AST_APP_OPTION_ARG('c', OPT_CALLER_NUM, OPT_ARG_CALLER_NUM),
	AST_APP_OPTION_ARG('n', OPT_CALLER_NAME, OPT_ARG_CALLER_NAME),
	AST_APP_OPTION_ARG('v', OPT_VARIABLES, OPT_ARG_VARIABLES),
END_OPTIONS );

static int originate_exec(struct ast_channel *chan, const char *data)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(tech_data);
		AST_APP_ARG(type);
		AST_APP_ARG(arg1);
		AST_APP_ARG(arg2);
		AST_APP_ARG(arg3);
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
	);
	struct ast_flags64 opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	char *predial_callee = NULL;
	char *parse, *cnum = NULL, *cname = NULL;

	struct ast_variable *vars = NULL;
	char *chantech, *chandata;
	int res = -1;
	int continue_in_dialplan = 0;
	int outgoing_status = 0;
	unsigned int timeout = 30;
	static const char default_exten[] = "s";
	struct ast_format_cap *capabilities;
	capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	ast_autoservice_start(chan);
	if (!capabilities) {
		goto return_cleanup;
	}

	ast_format_cap_append(capabilities, ast_format_slin, 0);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Originate() requires arguments\n");
		goto return_cleanup;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 3) {
		ast_log(LOG_ERROR, "Incorrect number of arguments\n");
		goto return_cleanup;
	}

	if (!ast_strlen_zero(args.timeout)) {
		if(sscanf(args.timeout, "%u", &timeout) != 1) {
			ast_log(LOG_NOTICE, "Invalid timeout: '%s'. Setting timeout to 30 seconds\n", args.timeout);
			timeout = 30;
		}
	}

	chandata = ast_strdupa(args.tech_data);
	chantech = strsep(&chandata, "/");

	if (ast_strlen_zero(chandata) || ast_strlen_zero(chantech)) {
		ast_log(LOG_ERROR, "Channel Tech/Data invalid: '%s'\n", args.tech_data);
		goto return_cleanup;
	}

	if (!ast_strlen_zero(args.options) &&
		ast_app_parse_options64(originate_exec_options, &opts, opt_args, args.options)) {
		ast_log(LOG_ERROR, "Invalid options: '%s'\n", args.options);
		goto return_cleanup;
	}

	/* PREDIAL: Run gosub on the caller's channel */
	if (ast_test_flag64(&opts, OPT_PREDIAL_CALLER)
		&& !ast_strlen_zero(opt_args[OPT_ARG_PREDIAL_CALLER])) {
		ast_replace_subargument_delimiter(opt_args[OPT_ARG_PREDIAL_CALLER]);
		ast_app_exec_sub(NULL, chan, opt_args[OPT_ARG_PREDIAL_CALLER], 0);
	}

	if (ast_test_flag64(&opts, OPT_PREDIAL_CALLEE)
		&& !ast_strlen_zero(opt_args[OPT_ARG_PREDIAL_CALLEE])) {
		ast_replace_subargument_delimiter(opt_args[OPT_ARG_PREDIAL_CALLEE]);
		predial_callee = opt_args[OPT_ARG_PREDIAL_CALLEE];
	}

	if (strcasecmp(args.type, "exten") && strcasecmp(args.type, "app")) {
		ast_log(LOG_ERROR, "Incorrect type, it should be 'exten' or 'app': %s\n",
				args.type);
		goto return_cleanup;
	}

	if (ast_test_flag64(&opts, OPT_CODECS)) {
		if (!ast_strlen_zero(opt_args[OPT_ARG_CODECS])) {
			ast_format_cap_remove_by_type(capabilities, AST_MEDIA_TYPE_UNKNOWN);
			ast_format_cap_update_by_allow_disallow(capabilities, opt_args[OPT_ARG_CODECS], 1);
		}
	}

	if (ast_test_flag64(&opts, OPT_CALLER_NUM)) {
		if (!ast_strlen_zero(opt_args[OPT_ARG_CALLER_NUM])) {
			cnum = opt_args[OPT_ARG_CALLER_NUM];
		} else if (ast_channel_caller(chan)->id.number.str) {
			cnum = ast_channel_caller(chan)->id.number.str;
		}
	}

	if (ast_test_flag64(&opts, OPT_CALLER_NAME)) {
		if (!ast_strlen_zero(opt_args[OPT_ARG_CALLER_NAME])) {
			cname = opt_args[OPT_ARG_CALLER_NAME];
		} else if (ast_channel_caller(chan)->id.name.str) {
			cname = ast_channel_caller(chan)->id.name.str;
		}
	}

	/* Assign variables */
	if (ast_test_flag64(&opts, OPT_VARIABLES)
		&& !ast_strlen_zero(opt_args[OPT_ARG_VARIABLES])) {
		char *vartext;
		char *text = opt_args[OPT_ARG_VARIABLES];
		while ((vartext = ast_strsep(&text, '^', 0))) {
			struct ast_variable *var;
			char *varname, *varvalue;
			if (!(varname = ast_strsep(&vartext, '=', 0))) {
				ast_log(LOG_ERROR, "Variable syntax error: %s\n", vartext);
				goto return_cleanup;
			}
			if (!(varvalue = ast_strsep(&vartext, '=', 0))) {
				varvalue = ""; /* empty values are allowed */
			}
			var = ast_variable_new(varname, varvalue, "");
			if (!var) {
				ast_log(LOG_ERROR, "Failed to allocate variable: %s\n", varname);
				goto return_cleanup;
			}
			ast_debug(1, "Appending variable '%s' with value '%s'", varname, varvalue);
			ast_variable_list_append(&vars, var);
		}
	}

	if (!strcasecmp(args.type, "exten")) {
		const char *cid_num = cnum;
		const char *cid_name = cname;
		int priority = 1; /* Initialized in case priority not specified */
		const char *exten = args.arg2;

		if (args.argc == 5) {
			/* Context/Exten/Priority all specified */
			if (sscanf(args.arg3, "%30d", &priority) != 1) {
				ast_log(LOG_ERROR, "Invalid priority: '%s'\n", args.arg3);
				goto return_cleanup;
			}
		} else if (args.argc == 3) {
			/* Exten not specified */
			exten = default_exten;
		}

		ast_debug(1, "Originating call to '%s/%s' and connecting them to extension %s,%s,%d\n",
				chantech, chandata, args.arg1, exten, priority);

		res = ast_pbx_outgoing_exten_predial(chantech, capabilities, chandata,
				timeout * 1000, args.arg1, exten, priority, &outgoing_status,
				ast_test_flag64(&opts, OPT_ASYNC) ? AST_OUTGOING_NO_WAIT : AST_OUTGOING_WAIT,
				cid_num, cid_name, vars, NULL, NULL, 0, NULL,
				predial_callee);
	} else {
		const char *cid_num = cnum;
		const char *cid_name = cname;
		ast_debug(1, "Originating call to '%s/%s' and connecting them to %s(%s)\n",
				chantech, chandata, args.arg1, S_OR(args.arg2, ""));

		res = ast_pbx_outgoing_app_predial(chantech, capabilities, chandata,
				timeout * 1000, args.arg1, args.arg2, &outgoing_status,
				ast_test_flag64(&opts, OPT_ASYNC) ? AST_OUTGOING_NO_WAIT : AST_OUTGOING_WAIT,
				cid_num, cid_name, vars, NULL, NULL, NULL,
				predial_callee);
	}

	/*
	 * Getting here means that we have passed the various validation checks and
	 * have at least attempted the dial. If we have a reason (outgoing_status),
	 * we clear our error indicator so that we ultimately report the right thing
	 * to the caller.
	 */
	if (res && outgoing_status) {
		res = 0;
	}

	/* We need to exit cleanly if we've gotten this far */
	continue_in_dialplan = 1;

return_cleanup:
	if (res) {
		pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "FAILED");
	} else {
		switch (outgoing_status) {
		case 0:
		case AST_CONTROL_ANSWER:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "SUCCESS");
			break;
		case AST_CONTROL_BUSY:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "BUSY");
			break;
		case AST_CONTROL_CONGESTION:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "CONGESTION");
			break;
		case AST_CONTROL_HANGUP:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "HANGUP");
			break;
		case AST_CONTROL_RINGING:
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "RINGING");
			break;
		default:
			ast_log(LOG_WARNING, "Unknown originate status result of '%d'\n",
					outgoing_status);
			pbx_builtin_setvar_helper(chan, "ORIGINATE_STATUS", "UNKNOWN");
			break;
		}
	}
	if (vars) {
		ast_variables_destroy(vars);
	}
	ao2_cleanup(capabilities);
	ast_autoservice_stop(chan);

	return continue_in_dialplan ? 0 : -1;
}

static int unload_module(void)
{
	return ast_unregister_application(app_originate);
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app_originate, originate_exec);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Originate call");
