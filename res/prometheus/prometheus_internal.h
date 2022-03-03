/*
 * Prometheus Internal API
 *
 * Copyright (C) 2019 Sangoma, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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

#ifndef PROMETHEUS_INTERNAL_H__
#define PROMETHEUS_INTERNAL_H__

/*!
 * \file
 *
 * \brief Prometheus Metric Internal API
 *
 * This module provides internal APIs for res_prometheus.
 * It should not be used outsize of that module, and should
 * typically only provide intialization functions for units that
 * want to register metrics / handlers with the core API.
 */

/*!
 * \brief Retrieve the amount of time it took to perform the last scrape
 *
 * \details Time returned is in milliseconds
 *
 * \retval The scrape duration, in milliseconds
 */
int64_t prometheus_last_scrape_duration_get(void);

/*!
 * \brief Retrieve the timestamp when the last scrape occurred
 *
 * \retval The time when the last scrape occurred
 */
struct timeval prometheus_last_scrape_time_get(void);

/*!
 * \brief Get the raw output of what a scrape would produce
 *
 * \details
 * It can be useful to dump what a scrape will look like.
 * This function returns the raw string representation
 * of the metrics.
 *
 * \retval NULL on error
 * \retval Malloc'd ast_str on success
 */
struct ast_str *prometheus_scrape_to_string(void);

/*!
 * \brief Initialize CLI command
 *
 * \retval 0 success
 * \retval -1 error
 */
int cli_init(void);

/*!
 * \brief Initialize channel metrics
 *
 * \retval 0 success
 * \retval -1 error
 */
int channel_metrics_init(void);

/*!
 * \brief Initialize endpoint metrics
 *
 * \retval 0 success
 * \retval -1 error
 */
int endpoint_metrics_init(void);

/*!
 * \brief Initialize bridge metrics
 *
 * \retval 0 success
 * \retval -1 error
 */
int bridge_metrics_init(void);

/*!
 * \brief Initialize PJSIP outbound registration metrics
 *
 * \retval 0 success
 * \retval -1 error
 */
int pjsip_outbound_registration_metrics_init(void);

#endif /* #define PROMETHEUS_INTERNAL_H__ */
