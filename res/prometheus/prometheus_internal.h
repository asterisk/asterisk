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
 * \file prometheus_internal
 *
 * \brief Prometheus Metric Internal API
 *
 * This module provides internal APIs for \file res_prometheus.
 * It should not be used outsize of that module, and should
 * typically only provide intialization functions for units that
 * want to register metrics / handlers with the core API.
 */

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

#endif /* #define PROMETHEUS_INTERNAL_H__ */
