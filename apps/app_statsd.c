/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Tyler Cambron <tcambron@digium.com>
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

 /*** MODULEINFO
	<depend>res_statsd</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/strings.h"

/*** DOCUMENTATION
	<application name="Statsd" language="en_US">
		<synopsis>
			Allow statistics to be passed to the StatsD server from the dialplan.
		</synopsis>
		<syntax>
			<parameter name="metric_type" required="true">
				<para>The metric type to be sent to StatsD.</para>
			</parameter>
			<parameter name="statistic_name" required="true">
				<para>The name of the variable to be sent to StatsD.</para>
			</parameter>
			<parameter name="value" required="true">
				<para>The value of the variable to be sent to StatsD.</para>
			</parameter>
		</syntax>
		<description>
<<<<<<< HEAD
			<para>This dialplan application sends statistics to the StatsD server
			specified inside of <literal>statsd.conf</literal>.</para>
=======
			<para>This dialplan application sends statistics to the StatsD
			server specified inside of <literal>statsd.conf</literal>.</para>
			<variablelist>
				<variable name="STATSDSTATUS">
					<para>This indicates the status of the execution of the
					Stasis application.</para>
					<value name="PASSED">
						All parameters passed to the StatsD application have
						been verified.
					</value>
					<value name="INVALIDPARAMS">
						A failure occurred from examining the parameters passed
						to the StatsD application. This could result from an
						empty metric_type, statistic_name or value field, or
						from an invalid metric_type, statistic_name, or value
						argument.
					</value>
				</variable>
			</variablelist>
>>>>>>> 3bcdac4... StatsD: Add user input validation to the application
		</description>
	</application>
 ***/

static const char app[] = "Statsd";

/*Prototype for the validate_metric method.*/
static int validate_metric(char* metric);

/*Prototype for the validate_name method.*/
static int validate_name(char* name);

/*Prototype for the validate_value method.*/
static int validate_value(char* value);

static int statsd_exec(struct ast_channel *chan, const char *data)
{
	AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(metric_type);
			AST_APP_ARG(statistic_name);
			AST_APP_ARG(value);
	);

	char *stats = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, stats);

	/*Channel variable to check for emitting the proper user event.*/
	pbx_builtin_setvar_helper(chan, "STATSDSTATUS", "");

	/*If any of the validations fail, set the channel variable to FAILED.*/
	if (validate_metric(args.metric_type) || validate_name(args.statistic_name)
		|| validate_value(args.value)) {
		pbx_builtin_setvar_helper(chan, "STATSDSTATUS", "INVALIDPARAMS");

		return 1;
	}

	pbx_builtin_setvar_helper(chan, "STATSDSTATUS", "PASSED");

	return 0;
}

/*Check to ensure the metric type is a valid metric type.*/
static int validate_metric(char* metric)
{
	const char *valid_metrics[] = {"gauge","set","timer","counter"};

	/*Check if metric field is blank*/
	if (ast_strlen_zero(metric)) {
		return 1;
	} else {
		int index;

		for (index = 0; index < 4; index++)
		{
			/*If none of the valid metrics matched the given metric and the 
			  entire list has been scanned, return a failure.*/
			if((strcmp(valid_metrics[index], metric) == 0)) {
				break;
			} else if (index == 3) {
				return 1;
			}
		}
	}

	return 0;
}

/*Check to ensure the statistic name is valid.*/
static int validate_name(char* name) {
	/*Check for an empty statistic name and the pipe (|) character, which is
	  the only invalid character.*/
	if ((ast_strlen_zero(name)) || (strstr(name, "|") != NULL)) {
		return 1;
	}

	return 0;
}

/*Check to ensure the value is valid.*/
static int validate_value(char* value) {
	/*Check to ensure the value field is not empty and is a digit.*/
	if (ast_strlen_zero(value)) {
		return 1;
	} else {
		const char *num = value;
		while (*num) {
			if (isdigit(*num++) == 0) {
				return 1;
			}
		}
	}

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, statsd_exec);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Statsd Dialplan Application");
