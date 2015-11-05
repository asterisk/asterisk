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
				<para>The metric type to be sent to StatsD. Valid metric types
				are 'g' for gauge, 'c' for counter, 'ms' for timer, and 's' for
				sets.</para>
			</parameter>
			<parameter name="statistic_name" required="true">
				<para>The name of the variable to be sent to StatsD. Statistic
				names cannot contain the pipe (|) character.</para>
			</parameter>
			<parameter name="value" required="true">
				<para>The value of the variable to be sent to StatsD. Values
				must be numeric. Values for gauge and counter metrics can be
				sent with a '+' or '-' to update a value after the value has
				been initialized. Only counters can be initialized as negative.
				Sets can send a string as the value parameter, but the string
				cannot contain the pipe character.</para>
			</parameter>
			<parameter name="sample_rate">
				<para>The value of the sample rate to be sent to StatsD. Sample
				rates less than or equal to 0 will never be sent, sample rates
				greater than or equal to 1 will always be sent, and any rate
				between 1 and 0 will be left up to chance.</para>
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
 * allowed at the beginning of the value if it is a counter or a gauge. A set
 * can send a string as the value, but it cannot contain a pipe character.
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

	if (!strcmp(metric, "s")) {
		if (strstr(value, "|") != NULL) {
			ast_log(AST_LOG_ERROR, "Pipe (|) character is not allowed for value %s"
				" in a set metric.\n", value);
			return 1;
		}
		return 0;
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

/*!
 * \brief Check to ensure the sample rate is valid.
 *
 * \param sample_rate The sample_rate to be sent to StatsD.
 *
 * This function checks to see if the sample rate given to the StatsD daialplan
 * application is valid by testing if it is numeric. Sample rates can be any
 * number.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_rate(const char *sample_rate)
{
	if (!isdigit(*sample_rate)) {
		ast_log(AST_LOG_ERROR, "Sample rate of %s is not a number!\n", sample_rate);
		return 1;
	}

	return 0;
}

/*!
 * \brief Calls other validate functions.
 *
 * \param value The value of the statistic to be sent to StatsD.
 * \param metric_type The metric type to be sent to StatsD.
 * \param statistic_name The variable name to be sent to StatsD.
 *
 * This function calls the other validation methods and returns a failure if a
 * failure is returned by any of them. This was placed here for cleanliness.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_arguments(const char *metric_type, const char *statistic_name,
	const char *value)
{
	if (validate_metric(metric_type) || validate_name(statistic_name)
		|| validate_value(value, metric_type)) {

		return 1;
	}

	return 0;
}

static int statsd_exec(struct ast_channel *chan, const char *data)
{
	char *stats;
	double numerical_rate;

	AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(metric_type);
			AST_APP_ARG(statistic_name);
			AST_APP_ARG(value);
			AST_APP_ARG(sample_rate);
	);

	if (!data) {
		ast_log(AST_LOG_ERROR, "No parameters were provided. Correct format is "
			"StatsD(metric_type,statistic_name,value[,sample_rate]). Sample rate is the "
			"only optional parameter.\n");
		return 1;
	}

	stats = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, stats);

	if (args.sample_rate) {

		/* If any of the validations fail, emit a warning message. */
		if (validate_arguments(args.metric_type, args.statistic_name, args.value)
			|| validate_rate(args.sample_rate)) {
			ast_log(AST_LOG_WARNING, "Invalid parameters provided. Correct format is "
				"StatsD(metric_type,statistic_name,value[,sample_rate]). Sample rate is "
				"the only optional parameter.\n");

			return 1;
		}

		numerical_rate = strtod(args.sample_rate, NULL);
		ast_statsd_log_string(args.statistic_name, args.metric_type, args.value,
			numerical_rate);

		return 0;
	}

	if (validate_arguments(args.metric_type, args.statistic_name, args.value)) {
		ast_log(AST_LOG_WARNING, "Invalid parameters provided. Correct format is "
				"StatsD(metric_type,statistic_name,value[,sample_rate]). Sample rate is "
				"the only optional parameter.\n");

		return 1;
	}

	ast_statsd_log_string(args.statistic_name, args.metric_type, args.value,
		1.0);

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
