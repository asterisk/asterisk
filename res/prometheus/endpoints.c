/*
 * Asterisk -- An open source telephony toolkit.
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

/*!
 * \file
 * \brief Prometheus Endpoint Metrics
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

#include "asterisk.h"

#include "asterisk/stasis_endpoints.h"
#include "asterisk/res_prometheus.h"
#include "prometheus_internal.h"

#define ENDPOINTS_STATE_HELP "Individual endpoint states. 0=unknown; 1=offline; 2=online."

#define ENDPOINTS_CHANNELS_COUNT_HELP "Count of the number of channels currently existing that are associated with the endpoint."

/*!
 * \internal
 * \brief Callback function to get an endpoint's current state
 *
 * \param metric The metric to populate
 * \param snapshot Endpoint snapshot
 */
static void get_endpoint_state(struct prometheus_metric *metric, struct ast_endpoint_snapshot *snapshot)
{
	snprintf(metric->value, sizeof(metric->value), "%d", snapshot->state);
}

/*!
 * \internal
 * \brief Callback function to get the current number of channel's associated with an endpoint
 *
 * \param metric The metric to populate
 * \param snapshot Endpoint snapshot
 */
static void get_endpoint_channel_count(struct prometheus_metric *metric, struct ast_endpoint_snapshot *snapshot)
{
	snprintf(metric->value, sizeof(metric->value), "%d", snapshot->num_channels);
}

/*!
 * \internal
 * \brief Helper struct for generating individual endpoint stats
 */
struct endpoint_metric_defs {
	/*!
	 * \brief Help text to display
	 */
	const char *help;
	/*!
	 * \brief Name of the metric
	 */
	const char *name;
	/*!
	 * \brief Callback function to generate a metric value for a given endpoint
	 */
	void (* const get_value)(struct prometheus_metric *metric, struct ast_endpoint_snapshot *snapshot);
} endpoint_metric_defs[] = {
	{
		.help = ENDPOINTS_STATE_HELP,
		.name = "asterisk_endpoints_state",
		.get_value = get_endpoint_state,
	},
	{
		.help = ENDPOINTS_CHANNELS_COUNT_HELP,
		.name = "asterisk_endpoints_channels_count",
		.get_value = get_endpoint_channel_count,
	},
};

/*!
 * \internal
 * \brief Callback invoked when Prometheus scrapes the server
 *
 * \param response The response to populate with formatted metrics
 */
static void endpoints_scrape_cb(struct ast_str **response)
{
	struct ao2_container *endpoint_cache;
	struct ao2_container *endpoints;
	struct ao2_iterator it_endpoints;
	struct stasis_message *message;
	struct prometheus_metric *endpoint_metrics;
	char eid_str[32];
	int i, j, num_endpoints;
	struct prometheus_metric endpoint_count = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"asterisk_endpoints_count",
		"Current endpoint count.",
		NULL
	);

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);

	endpoint_cache = stasis_cache_dump(ast_endpoint_cache(), ast_endpoint_snapshot_type());
	if (!endpoint_cache) {
		return;
	}
	endpoints = ao2_container_clone(endpoint_cache, 0);
	ao2_ref(endpoint_cache, -1);
	if (!endpoints) {
		return;
	}

	num_endpoints = ao2_container_count(endpoints);

	/* Current endpoint count */
	PROMETHEUS_METRIC_SET_LABEL(&endpoint_count, 0, "eid", eid_str);
	snprintf(endpoint_count.value, sizeof(endpoint_count.value), "%d", num_endpoints);
	prometheus_metric_to_string(&endpoint_count, response);

	if (num_endpoints == 0) {
		ao2_ref(endpoints, -1);
		return;
	}

	endpoint_metrics = ast_calloc(ARRAY_LEN(endpoint_metric_defs) * num_endpoints, sizeof(*endpoint_metrics));
	if (!endpoint_metrics) {
		ao2_ref(endpoints, -1);
		return;
	}

	/* Endpoint dependent values */
	it_endpoints = ao2_iterator_init(endpoints, 0);
	for (i = 0; (message = ao2_iterator_next(&it_endpoints)); ao2_ref(message, -1), i++) {
		struct ast_endpoint_snapshot *snapshot = stasis_message_data(message);

		for (j = 0; j < ARRAY_LEN(endpoint_metric_defs); j++) {
			int index = i * ARRAY_LEN(endpoint_metric_defs) + j;

			endpoint_metrics[index].type = PROMETHEUS_METRIC_GAUGE;
			ast_copy_string(endpoint_metrics[index].name, endpoint_metric_defs[j].name, sizeof(endpoint_metrics[index].name));
			endpoint_metrics[index].help = endpoint_metric_defs[j].help;
			PROMETHEUS_METRIC_SET_LABEL(&endpoint_metrics[index], 0, "eid", eid_str);
			PROMETHEUS_METRIC_SET_LABEL(&endpoint_metrics[index], 1, "id", (snapshot->id));
			PROMETHEUS_METRIC_SET_LABEL(&endpoint_metrics[index], 2, "tech", (snapshot->tech));
			PROMETHEUS_METRIC_SET_LABEL(&endpoint_metrics[index], 3, "resource", (snapshot->resource));
			endpoint_metric_defs[j].get_value(&endpoint_metrics[index], snapshot);

			if (i != 0) {
				AST_LIST_INSERT_TAIL(&endpoint_metrics[j].children, &endpoint_metrics[index], entry);
			}
		}
	}
	ao2_iterator_destroy(&it_endpoints);

	for (j = 0; j < ARRAY_LEN(endpoint_metric_defs); j++) {
		prometheus_metric_to_string(&endpoint_metrics[j], response);
	}

	ast_free(endpoint_metrics);
	ao2_ref(endpoints, -1);
}

struct prometheus_callback endpoints_callback = {
	.name = "Endpoints callback",
	.callback_fn = endpoints_scrape_cb,
};

/*!
 * \internal
 * \brief Callback invoked when the core module is unloaded
 */
static void endpoint_metrics_unload_cb(void)
{
	prometheus_callback_unregister(&endpoints_callback);
}

/*!
 * \internal
 * \brief Metrics provider definition
 */
static struct prometheus_metrics_provider provider = {
	.name = "endpoints",
	.unload_cb = endpoint_metrics_unload_cb,
};

int endpoint_metrics_init(void)
{
	prometheus_metrics_provider_register(&provider);
	prometheus_callback_register(&endpoints_callback);

	return 0;
}
