/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#ifndef _ASTERISK_STATSD_H
#define _ASTERISK_STATSD_H

/*!
 * \brief Support for publishing to a statsd server.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

#include "asterisk/optional_api.h"

/*! An instantaneous measurement of a value. */
#define AST_STATSD_GAUGE "g"
/*!
 * Embarrassingly, gauge was misspelled for quite some time.
 * \deprecated You should spell gauge correctly.
 */
#define AST_STATSD_GUAGE AST_STATSD_GAUGE
/*! A change in a value. */
#define AST_STATSD_COUNTER "c"
/*! Measure of milliseconds. */
#define AST_STATSD_TIMER "ms"
/*! Distribution of values over time. */
#define AST_STATSD_HISTOGRAM "h"
/*!
 * Meters are non-standard and poorly supported by StatsD servers
 * \deprecated You should switch to counter or stateful counters for a similar effect.
 */
#define AST_STATSD_METER "m"


/*!
 * \brief Send a stat to the configured statsd server.
 *
 * This function uses a character argument for value instead of
 * an intmax_t argument. This is designed to be simpler to use for
 * updating a current value rather than resetting it.
 *
 * \param metric_name String (UTF-8) name of the metric.
 * \param metric_type Type of metric to send.
 * \param value Value to send.
 * \param sample_rate Percentage of samples to send.
 * \since 13
 */
AST_OPTIONAL_API(void, ast_statsd_log_string, (const char *metric_name,
	const char *metric_type, const char *value, double sample_rate), {});

/*!
 * \brief Send a stat to the configured statsd server.
 * \since 13.7.0
 *
 * This is the most flexible function for sending a message to the statsd
 * server. In addition to allowing the string value and sample rate to be specified,
 * the metric_name can be formed as a printf style string with variable
 * arguments.
 *
 * \param metric_name Format string (UTF-8) specifying the name of the metric.
 * \param metric_type Type of metric to send.
 * \param value Value to send.
 * \param sample_rate Percentage of samples to send.
 *
 * Example Usage:
 * \code
 *     ast_statsd_log_string_va(AST_STATSD_GAUGE, "+1", 1.0, "endpoints.states.%s", state_name);
 * \endcode
 */
AST_OPTIONAL_API_ATTR(void, format(printf, 1, 5), ast_statsd_log_string_va,
	(const char *metric_name, const char *metric_type, const char *value, double sample_rate, ...), {});

/*!
 * \brief Send a stat to the configured statsd server.
 *
 * The is nearly the most flexible function for sending a message to the statsd
 * server, but also the least easy to use. See ast_statsd_log() or
 * ast_statsd_log_sample() for a slightly more convenient interface.
 *
 * \param metric_name String (UTF-8) name of the metric.
 * \param metric_type Type of metric to send.
 * \param value Value to send.
 * \param sample_rate Percentage of samples to send.
 * \since 12
 */
AST_OPTIONAL_API(void, ast_statsd_log_full, (const char *metric_name,
	const char *metric_type, intmax_t value, double sample_rate), {});

/*!
 * \brief Send a stat to the configured statsd server.
 * \since 13.7.0
 *
 * This is the most flexible function for sending a message to the statsd
 * server. In addition to allowing the value and sample rate to be specified,
 * the metric_name can be formed as a printf style string with variable
 * arguments.
 *
 * \param metric_name Format string (UTF-8) specifying the name of the metric.
 * \param metric_type Type of metric to send.
 * \param value Value to send.
 * \param sample_rate Percentage of samples to send.
 *
 * Example Usage:
 * \code
 *     ast_statsd_log_full_va(AST_STATSD_TIMER, rtt, 1.0, "endpoint.%s.rtt", endpoint_name);
 * \endcode
 */
AST_OPTIONAL_API_ATTR(void, format(printf, 1, 5), ast_statsd_log_full_va,
	(const char *metric_name, const char *metric_type, intmax_t value, double sample_rate, ...), {});

/*!
 * \brief Send a stat to the configured statsd server.
 * \param metric_name String (UTF-8) name of the metric.
 * \param metric_type Type of metric to send.
 * \param value Value to send.
 * \since 12
 */
AST_OPTIONAL_API(void, ast_statsd_log, (const char *metric_name,
	const char *metric_type, intmax_t value), {});

/*!
 * \brief Send a random sampling of a stat to the configured statsd server.
 *
 * The type of sampled metrics is always \ref AST_STATSD_COUNTER. The given
 * \a sample_rate should be a percentage between 0.0 and 1.0. If it's <= 0.0,
 * then no samples will be sent. If it's >= 1.0, then all samples will be sent.
 *
 * \param metric_name String (UTF-8) name of the metric.
 * \param value Value to send.
 * \param sample_rate Percentage of samples to send.
 * \since 12
 */
AST_OPTIONAL_API(void, ast_statsd_log_sample, (const char *metric_name,
		intmax_t value, double sample_rate), {});


#endif /* _ASTERISK_STATSD_H */
