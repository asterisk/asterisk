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
 * \brief Prometheus Bridge Metrics
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

#include "asterisk.h"

#include "asterisk/stasis_bridges.h"
#include "asterisk/res_prometheus.h"
#include "prometheus_internal.h"

#define BRIDGES_CHANNELS_COUNT_HELP "Number of channels in the bridge."

/*!
 * \internal
 * \brief Callback function to get the number of channels in a bridge
 *
 * \param metric The metric to populate
 * \param snapshot Bridge snapshot
 */
static void get_bridge_channel_count(struct prometheus_metric *metric, struct ast_bridge_snapshot *snapshot)
{
	snprintf(metric->value, sizeof(metric->value), "%d", snapshot->num_channels);
}

/*!
 * \internal
 * \brief Helper struct for generating individual bridge stats
 */
struct bridge_metric_defs {
	/*!
	 * \brief Help text to display
	 */
	const char *help;
	/*!
	 * \brief Name of the metric
	 */
	const char *name;
	/*!
	 * \brief Callback function to generate a metric value for a given bridge
	 */
	void (* const get_value)(struct prometheus_metric *metric, struct ast_bridge_snapshot *snapshot);
} bridge_metric_defs[] = {
	{
		.help = BRIDGES_CHANNELS_COUNT_HELP,
		.name = "asterisk_bridges_channels_count",
		.get_value = get_bridge_channel_count,
	},
};

/*!
 * \internal
 * \brief Callback invoked when Prometheus scrapes the server
 *
 * \param response The response to populate with formatted metrics
 */
static void bridges_scrape_cb(struct ast_str **response)
{
	struct ao2_container *bridge_cache;
	struct ao2_container *bridges;
	struct ao2_iterator it_bridges;
	struct ast_bridge *bridge;
	struct prometheus_metric *bridge_metrics;
	char eid_str[32];
	int i, j, num_bridges;
	struct prometheus_metric bridge_count = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"asterisk_bridges_count",
		"Current bridge count.",
		NULL
	);

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);

	bridge_cache = ast_bridges();
	if (!bridge_cache) {
		return;
	}

	bridges = ao2_container_clone(bridge_cache, 0);
	ao2_ref(bridge_cache, -1);
	if (!bridges) {
		return;
	}

	num_bridges = ao2_container_count(bridges);

	/* Current endpoint count */
	PROMETHEUS_METRIC_SET_LABEL(&bridge_count, 0, "eid", eid_str);
	snprintf(bridge_count.value, sizeof(bridge_count.value), "%d", num_bridges);
	prometheus_metric_to_string(&bridge_count, response);

	if (num_bridges == 0) {
		ao2_ref(bridges, -1);
		return;
	}

	bridge_metrics = ast_calloc(ARRAY_LEN(bridge_metric_defs) * num_bridges, sizeof(*bridge_metrics));
	if (!bridge_metrics) {
		ao2_ref(bridges, -1);
		return;
	}

	/* Bridge dependent values */
	it_bridges = ao2_iterator_init(bridges, 0);
	for (i = 0; (bridge = ao2_iterator_next(&it_bridges)); ao2_ref(bridge, -1), i++) {
		struct ast_bridge_snapshot *snapshot = ast_bridge_get_snapshot(bridge);

		for (j = 0; j < ARRAY_LEN(bridge_metric_defs); j++) {
			int index = i * ARRAY_LEN(bridge_metric_defs) + j;

			bridge_metrics[index].type = PROMETHEUS_METRIC_GAUGE;
			ast_copy_string(bridge_metrics[index].name, bridge_metric_defs[j].name, sizeof(bridge_metrics[index].name));
			bridge_metrics[index].help = bridge_metric_defs[j].help;
			PROMETHEUS_METRIC_SET_LABEL(&bridge_metrics[index], 0, "eid", eid_str);
			PROMETHEUS_METRIC_SET_LABEL(&bridge_metrics[index], 1, "id", (snapshot->uniqueid));
			PROMETHEUS_METRIC_SET_LABEL(&bridge_metrics[index], 2, "tech", (snapshot->technology));
			PROMETHEUS_METRIC_SET_LABEL(&bridge_metrics[index], 3, "subclass", (snapshot->subclass));
			PROMETHEUS_METRIC_SET_LABEL(&bridge_metrics[index], 4, "creator", (snapshot->creator));
			PROMETHEUS_METRIC_SET_LABEL(&bridge_metrics[index], 5, "name", (snapshot->name));
			bridge_metric_defs[j].get_value(&bridge_metrics[index], snapshot);

			if (i > 0) {
				AST_LIST_INSERT_TAIL(&bridge_metrics[j].children, &bridge_metrics[index], entry);
			}
		}
		ao2_ref(snapshot, -1);
	}
	ao2_iterator_destroy(&it_bridges);

	for (j = 0; j < ARRAY_LEN(bridge_metric_defs); j++) {
		prometheus_metric_to_string(&bridge_metrics[j], response);
	}

	ast_free(bridge_metrics);
	ao2_ref(bridges, -1);
}

struct prometheus_callback bridges_callback = {
	.name = "bridges callback",
	.callback_fn = bridges_scrape_cb,
};

/*!
 * \internal
 * \brief Callback invoked when the core module is unloaded
 */
static void bridge_metrics_unload_cb(void)
{
	prometheus_callback_unregister(&bridges_callback);
}

/*!
 * \internal
 * \brief Metrics provider definition
 */
static struct prometheus_metrics_provider provider = {
	.name = "bridges",
	.unload_cb = bridge_metrics_unload_cb,
};

int bridge_metrics_init(void)
{
	prometheus_metrics_provider_register(&provider);
	prometheus_callback_register(&bridges_callback);

	return 0;
}
