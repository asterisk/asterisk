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

#include <math.h>

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/strings.h"
#include "asterisk/statsd.h"

/*** DOCUMENTATION
	<application name="StatsD" language="en_US">
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
			<para>This dialplan application sends statistics to the StatsD
			server specified inside of <literal>statsd.conf</literal>.</para>
		</description>
	</application>
 ***/

static const char app[] = "StatsD";

/*!
 * \brief Check to ensure the value is within the allowed range.
 *
 * \param value The value of the statistic to be sent to StatsD.
 * \param metric The metric type to be sent to StatsD.
 *
 * This function checks to see if the value given to the StatsD daialplan
 * application is within the allowed range as specified by StatsD. A counter
 * is the only metric type allowed to be initialized as a negative number.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int value_in_range(const char *value, const char *metric)
{
	double numerical_value = strtod(value, NULL);

	if (!strcmp(metric, "c")) {
		if (numerical_value < pow(-2, 63) || numerical_value > pow(2, 63)) {
			ast_log(AST_LOG_WARNING, "Value %lf out of range!\n", numerical_value);
			return 1;
		}
	} else {
		if (numerical_value < 0 || numerical_value > pow(2, 64)) {
			ast_log(AST_LOG_WARNING, "Value %lf out of range!\n", numerical_value);
			return 1;
		}
	}

	return 0;
}

/*!
 * \brief Check to ensure the metric type is a valid metric type.
 *
 * \param metric The metric type to be sent to StatsD.
 *
 * This function checks to see if the metric type given to the StatsD dialplan
 * is a valid metric type. Metric types are determined by StatsD.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_metric(const char *metric)
{
	const char *valid_metrics[] = {"g","s","ms","c"};
	int i;

	if (ast_strlen_zero(metric)) {
		ast_log(AST_LOG_ERROR, "Missing metric type argument.\n");
		return 1;
	}

	for (i = 0; i < ARRAY_LEN(valid_metrics); i++) {
		if (!strcmp(valid_metrics[i], metric)) {
			return 0;
		}
	}

	ast_log(AST_LOG_ERROR, "Invalid metric type %s.\n", metric);

	return 1;
}

/*!
 * \brief Check to ensure the statistic name is valid.
 *
 * \param name The variable name to be sent to StatsD.
 *
 * This function checks to see if the statistic name given to the StatsD
 * dialplan application is valid by ensuring that the name does not have any
 * invalid characters.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_name(const char *name)
{
	if (ast_strlen_zero(name) || (strstr(name, "|") != NULL)) {
		ast_log(AST_LOG_ERROR, "Statistic name %s is missing or contains a pipe (|)"
			" character.\n", name);
		return 1;
	}

	return 0;
}

/*!
 * \brief Check to ensure the value is valid.
 *
 * \param value The value of the statistic to be sent to StatsD.
 * \param metric The metric type to be sent to StatsD.
 *
 * This function checks to see if the value given to the StatsD daialplan
 * application is valid by testing if it is numeric. A plus or minus is only
 * allowed at the beginning of the value if it is a counter or a gauge.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_value(const char *value, const char *metric)
{
	const char *actual_value;

	if (ast_strlen_zero(value)) {
		ast_log(AST_LOG_ERROR, "Missing value argument.\n");
		return 1;
	}

	if (!strcmp(metric, "g") || !strcmp(metric, "c")) {
		if ((value[0] == '+') || (value[0] == '-')) {
			actual_value = &value[1];
			if (ast_strlen_zero(actual_value)) {
				ast_log(AST_LOG_ERROR, "Value argument %s only contains a sign"
					" operator.\n", value);
				return 1;
			}
		} else {
			actual_value = &value[0];
		}
	} else {
		actual_value = &value[0];
	}

	if (!isdigit(*actual_value)) {
		ast_log(AST_LOG_ERROR, "Value of %s is not a valid number!\n", actual_value);
		return 1;
	}

	if (value_in_range(actual_value, metric)) {
		return 1;
	}

	return 0;
}

static int statsd_exec(struct ast_channel *chan, const char *data)
{
	char *stats;
	double numerical_value;

	AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(metric_type);
			AST_APP_ARG(statistic_name);
			AST_APP_ARG(value);
	);

	if (!data) {
		ast_log(AST_LOG_ERROR, "No parameters were provided. Correct format is "
			"StatsD(metric_type,statistic_name,value). All parameters are required.\n");
		return 1;
	}

	stats = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, stats);

	/* If any of the validations fail, emit a warning message. */
	if (validate_metric(args.metric_type) || validate_name(args.statistic_name)
		|| validate_value(args.value, args.metric_type)) {
		ast_log(AST_LOG_WARNING, "Invalid parameters provided. Correct format is "
			"StatsD(metric_type,statistic_name,value). All parameters are required.\n");

		return 1;
	}

	/* Conversion to a double is safe here since the value would have been validated as a
	   number in validate_value().
	*/
	numerical_value = strtod(args.value, NULL);
	ast_statsd_log(args.statistic_name, args.metric_type, numerical_value);

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

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "StatsD Dialplan Application");
