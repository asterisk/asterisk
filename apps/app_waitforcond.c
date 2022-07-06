/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Sleep until a condition is true
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="WaitForCondition" language="en_US">
		<since>
			<version>16.20.0</version>
			<version>18.6.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Wait (sleep) until the given condition is true.
		</synopsis>
		<syntax>
			<parameter name="replacementchar" required="true">
				<para>Specifies the character in the expression used to replace the <literal>$</literal>
				character. This character should not be used anywhere in the expression itself.</para>
			</parameter>
			<parameter name="expression" required="true">
				<para>A modified logical expression with the <literal>$</literal> characters replaced by
				<replaceable>replacementchar</replaceable>. This is necessary to pass the expression itself
				into the application, rather than its initial evaluation.</para>
			</parameter>
			<parameter name="timeout">
				<para>The maximum amount of time, in seconds, this application should wait for a condition
				to become true before dialplan execution continues automatically to the next priority.
				By default, there is no timeout.</para>
			</parameter>
			<parameter name="interval">
				<para>The frequency, in seconds, of polling the condition, which can be adjusted depending
				on how time-sensitive execution needs to be. By default, this is 0.05.</para>
			</parameter>
		</syntax>
		<description>
			<para>Waits until <replaceable>expression</replaceable> evaluates to true, checking every
			<replaceable>interval</replaceable> seconds for up to <replaceable>timeout</replaceable>. Default
			is evaluate <replaceable>expression</replaceable> every 50 milliseconds with no timeout.</para>
			<example title="Wait for condition dialplan variable/function to become 1 for up to 40 seconds, checking every 500ms">
			 same => n,WaitForCondition(#,#["#{condition}"="1"],40,0.5)
			</example>
			<para>Sets <variable>WAITFORCONDITIONSTATUS</variable> to one of the following values:</para>
			<variablelist>
				<variable name="WAITFORCONDITIONSTATUS">
					<value name="TRUE">
						Condition evaluated to true before timeout expired.
					</value>
					<value name="FAILURE">
						Invalid argument.
					</value>
					<value name="TIMEOUT">
						Timeout elapsed without condition evaluating to true.
					</value>
					<value name="HANGUP">
						Channel hung up before condition became true.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static char *app = "WaitForCondition";

static int waitforcond_exec(struct ast_channel *chan, const char *data)
{
	int ms, i;
	double timeout = 0, poll = 0;
	int timeout_ms = 0;
	int poll_ms = 50; /* default is evaluate the condition every 50ms */
	struct timeval start = ast_tvnow();
	char dollarsignrep;
	int brackets = 0;
	char *pos, *open_bracket, *expression, *optargs = NULL;
	char condition[512];

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timeout);
		AST_APP_ARG(interval);
	);

	pos = ast_strdupa(data);

	if (ast_strlen_zero(pos)) {
		ast_log(LOG_ERROR, "WaitForCondition requires a condition\n");
		pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "FAILURE");
		return 0;
	}

	/* is there at least a [ followed by a ] somewhere ? */
	if (!(open_bracket = strchr(pos, '[')) || !strchr(open_bracket, ']')) {
		ast_log(LOG_ERROR, "No expression detected. Did you forget to replace the $ signs?\n");
		pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "FAILURE");
		return 0;
	}

	dollarsignrep = pos[0];
	if (dollarsignrep == '$' || dollarsignrep == '[' || dollarsignrep == ']'
		|| dollarsignrep == '{' || dollarsignrep == '}') {
		ast_log(LOG_ERROR, "Dollar sign replacement cannot be %c.\n", dollarsignrep);
		pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "FAILURE");
		return 0;
	}
	++pos;
	if (pos[0] != ',') {
		ast_log(LOG_ERROR, "Invalid separator: %c\n", pos[0]);
		pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "FAILURE");
		return 0;
	}
	++pos;
	if (pos[0] != dollarsignrep) {
		ast_log(LOG_ERROR, "Expression start does not match provided replacement: %c\n", pos[0]);
		pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "FAILURE");
		return 0;
	}

	expression = pos; /* we're at the start of the expression */

	/* commas may appear within the expression, so go until we've encountered as many closing brackets as opening */
	while (++pos) {
		if (pos[0] == '\0') {
			ast_log(LOG_ERROR, "Could not parse end of expression.\n");
			pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "FAILURE");
			return 0;
		}
		if (pos[0] == '[') {
			brackets++;
		} else if (pos[0] == ']') {
			brackets--;
		}
		if (brackets == 0) { /* reached end of expression */
			break;
		}
	}
	++pos;
	if (pos[0] != '\0') {
		++pos; /* eat comma separator */
		if (pos[0] != '\0') {
			optargs = ast_strdupa(pos);
			AST_STANDARD_APP_ARGS(args, optargs);
			if (!ast_strlen_zero(args.timeout)) {
				if (sscanf(args.timeout, "%30lg", &timeout) != 1) {
					ast_log(LOG_WARNING, "Invalid timeout provided: %s. No timeout set.\n", args.timeout);
					return -1;
				}
				timeout_ms = timeout * 1000.0;
			}

			if (!ast_strlen_zero(args.interval)) {
				if (sscanf(args.interval, "%30lg", &poll) != 1) {
					ast_log(LOG_WARNING, "Invalid polling interval provided: %s. Default unchanged.\n", args.interval);
					return -1;
				}
				if (poll < 0.001) {
					ast_log(LOG_WARNING, "Polling interval cannot be less than 1ms. Default unchanged.\n");
					return -1;
				}
				poll_ms = poll * 1000.0;
			}
		}
	}

	for (i = 0; expression[i] != '\0'; i++) {
		if (expression[i] == dollarsignrep) {
			expression[i] = '$'; /* replace $s back into expression for variable parsing */
		}
	}

	if (timeout_ms > 0) {
		ast_debug(1, "Waiting for condition for %f seconds: %s (checking every %d ms)", timeout, expression, poll_ms);
	} else {
		ast_debug(1, "Waiting for condition, forever: %s (checking every %d ms)", expression, poll_ms);
	}

	while (1) {
		/* Substitute variables now */
		pbx_substitute_variables_helper(chan, expression, condition, sizeof(condition) - 1);
		if (pbx_checkcondition(condition)) {
			pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "TRUE");
			return 0;
		}
		/* If a timeout was specified, check that it hasn't expired */
		if ((timeout_ms > 0) && !(ms = ast_remaining_ms(start, timeout_ms))) {
			pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "TIMEOUT");
			return 0;
		}
		if (ast_safe_sleep(chan, poll_ms)) { /* don't waste CPU, we don't need a super tight loop */
			pbx_builtin_setvar_helper(chan, "WAITFORCONDITIONSTATUS", "HANGUP");
			return -1; /* channel hung up */
		}
	}
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, waitforcond_exec);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Wait until condition is true");
