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
				rates less than or equal to 0 will never be sent and sample rates
				greater than or equal to 1 will always be sent. Any rate
				between 1 and 0 will be compared to a randomly generated value,
				and if it is greater than the random value, it will be sent.</para>
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
 *
 * This function checks to see if the value given to the StatsD dailplan
 * application is within the allowed range of [-2^63, 2^63] as specified by StatsD.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int value_in_range(const char *value) {
	double numerical_value = strtod(value, NULL);

	if (numerical_value < pow(-2, 63) || numerical_value > pow(2, 63)) {
		ast_log(AST_LOG_WARNING, "Value %lf out of range!\n", numerical_value);
		return 1;
	}

	return 0;
}

/*!
 * \brief Check to ensure the value is within the allowed range.
 *
 * \param value The value of the statistic to be sent to StatsD.
 *
 * This function checks to see if the value given to the StatsD dailplan
 * application is within the allowed range of [0, 2^64] as specified by StatsD.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int non_neg_value_range(const char *value) {
	double numerical_value = strtod(value, NULL);

	if (numerical_value < 0 || numerical_value > pow(2, 64)) {
		ast_log(AST_LOG_WARNING, "Value %lf out of range!\n", numerical_value);
		return 1;
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
 * \brief Check to ensure that a numeric value is valid.
 *
 * \param numeric_value The numeric value to be sent to StatsD.
 *
 * This function checks to see if a number to be sent to StatsD is actually
 * a valid number. One decimal is allowed.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_numeric(const char *numeric_value) {
	const char *num;
	int decimal_counter = 0;

	num = numeric_value;
	while (*num) {
		if (!isdigit(*num++)) {
			if (strstr(numeric_value, ".") != NULL && decimal_counter == 0) {
				decimal_counter++;
				continue;
			}
			ast_log(AST_LOG_ERROR, "%s is not a number!\n", numeric_value);
			return 1;
		}
	}

	return 0;
}

/*!
 * \brief Determines the actual value of a number by looking for a leading + or -.
 *
 * \param raw_value The entire numeric string to be sent to StatsD.
 *
 * This function checks to see if the numeric string contains valid characters
 * and then isolates the actual number to be sent for validation. Returns the
 * result of the numeric validation.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int determine_actual_value(const char *raw_value) {
	const char *actual_value;

	if ((raw_value[0] == '+') || (raw_value[0] == '-')) {
		actual_value = &raw_value[1];
		if (ast_strlen_zero(actual_value)) {
			ast_log(AST_LOG_ERROR, "Value argument %s only contains a sign"
				" operator.\n", raw_value);
			return 1;
		}
	} else {
		actual_value = &raw_value[0];
	}

	return validate_numeric(actual_value);
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
 * \brief Calls the appropriate functions to validate a gauge metric.
 *
 * \param statistic_name The statistic name to be sent to StatsD.
 * \param value The value to be sent to StatsD.
 *
 * This function calls other validating functions to correctly validate each
 * input based on allowable input for a gauge metric.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_metric_type_gauge(const char *statistic_name, const char *value) {

	if (ast_strlen_zero(value)) {
		ast_log(AST_LOG_ERROR, "Missing value argument.\n");
		return 1;
	}

	if (validate_name(statistic_name) || determine_actual_value(value)
		|| value_in_range(value)) {
		return 1;
	}

	return 0;
}

/*!
 * \brief Calls the appropriate functions to validate a counter metric.
 *
 * \param statistic_name The statistic name to be sent to StatsD.
 * \param value The value to be sent to StatsD.
 *
 * This function calls other validating functions to correctly validate each
 * input based on allowable input for a counter metric.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_metric_type_counter(const char *statistic_name, const char *value) {

	if (ast_strlen_zero(value)) {
		ast_log(AST_LOG_ERROR, "Missing value argument.\n");
		return 1;
	}

	if (validate_name(statistic_name) || determine_actual_value(value)
		|| value_in_range(value)) {
		return 1;
	}

	return 0;
}

/*!
 * \brief Calls the appropriate functions to validate a timer metric.
 *
 * \param statistic_name The statistic name to be sent to StatsD.
 * \param value The value to be sent to StatsD.
 *
 * This function calls other validating functions to correctly validate each
 * input based on allowable input for a timer metric.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_metric_type_timer(const char *statistic_name, const char *value) {

	if (ast_strlen_zero(value)) {
		ast_log(AST_LOG_ERROR, "Missing value argument.\n");
		return 1;
	}

	if (validate_name(statistic_name) || validate_numeric(value)
		|| non_neg_value_range(value)) {
		return 1;
	}

	return 0;
}

/*!
 * \brief Calls the appropriate functions to validate a set metric.
 *
 * \param statistic_name The statistic name to be sent to StatsD.
 * \param value The value to be sent to StatsD.
 *
 * This function calls other validating functions to correctly validate each
 * input based on allowable input for a set metric.
 *
 * \retval zero on success.
 * \retval 1 on error.
 */
static int validate_metric_type_set(const char *statistic_name, const char *value) {
	if (ast_strlen_zero(value)) {
		ast_log(AST_LOG_ERROR, "Missing value argument.\n");
		return 1;
	}

	if (validate_name(statistic_name)) {
		return 1;
	}

	if (strstr(value, "|") != NULL) {
		ast_log(AST_LOG_ERROR, "Pipe (|) character is not allowed for value %s"
			" in a set metric.\n", value);
		return 1;
	}

	return 0;
}

static int statsd_exec(struct ast_channel *chan, const char *data)
{
	char *stats;
	double numerical_rate = 1.0;

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

	if (validate_metric(args.metric_type)) {
		return 1;
	}

	if (!strcmp(args.metric_type, "g")) {
		if (validate_metric_type_gauge(args.statistic_name, args.value)) {
			ast_log(AST_LOG_ERROR, "Invalid input for a gauge metric.\n");
			return 1;
		}
	}
	else if (!strcmp(args.metric_type, "c")) {
		if (validate_metric_type_counter(args.statistic_name, args.value)) {
			ast_log(AST_LOG_ERROR, "Invalid input for a counter metric.\n");
			return 1;
		}
	}
	else if (!strcmp(args.metric_type, "ms")) {
		if (validate_metric_type_timer(args.statistic_name, args.value)) {
			ast_log(AST_LOG_ERROR, "Invalid input for a timer metric.\n");
			return 1;
		}
	}
	else if (!strcmp(args.metric_type, "s")) {
		if (validate_metric_type_set(args.statistic_name, args.value)) {
			ast_log(AST_LOG_ERROR, "Invalid input for a set metric.\n");
			return 1;
		}
	}

	if (args.sample_rate) {

		if (validate_numeric(args.sample_rate)) {
			return 1;
		}

		numerical_rate = strtod(args.sample_rate, NULL);
	}

	ast_statsd_log_string(args.statistic_name, args.metric_type, args.value,
		numerical_rate);

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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "StatsD Dialplan Application",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_statsd",
);
