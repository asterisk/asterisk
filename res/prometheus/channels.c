
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
 * \brief Prometheus Channel Metrics
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

#include "asterisk.h"
#include "asterisk/res_prometheus.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/pbx.h"
#include "prometheus_internal.h"

#define CHANNELS_STATE_HELP "Individual channel states. 0=down; 1=reserved; 2=offhook; 3=dialing; 4=ring; 5=ringing; 6=up; 7=busy; 8=dialing_offhook; 9=prering."

#define CHANNELS_DURATION_HELP "Individual channel durations (in seconds)."

/*!
 * \internal
 * \brief Callback function to get a channel's current state
 *
 * \param metric The metric to populate
 * \param snapshot Channel snapshot
 */
static void get_channel_state(struct prometheus_metric *metric, struct ast_channel_snapshot *snapshot)
{
	snprintf(metric->value, sizeof(metric->value), "%d", snapshot->state);
}

/*!
 * \internal
 * \brief Callback function to get a channel's current duration
 *
 * \param metric The metric to populate
 * \param snapshot Channel snapshot
 */
static void get_channel_duration(struct prometheus_metric *metric, struct ast_channel_snapshot *snapshot)
{
	struct timeval now = ast_tvnow();
	int64_t duration = ast_tvdiff_sec(now, snapshot->base->creationtime);

	snprintf(metric->value, sizeof(metric->value), "%" PRIu64, duration);
}

/*!
 * \internal
 * \brief Helper struct for generating individual channel stats
 */
struct channel_metric_defs {
	/*!
	 * \brief Help text to display
	 */
	const char *help;
	/*!
	 * \brief Name of the metric
	 */
	const char *name;
	/*!
	 * \brief Callback function to generate a metric value for a given channel
	 */
	void (* const get_value)(struct prometheus_metric *metric, struct ast_channel_snapshot *snapshot);
} channel_metric_defs[] = {
	{
		.help = CHANNELS_STATE_HELP,
		.name = "asterisk_channels_state",
		.get_value = get_channel_state,
	},
	{
		.help = CHANNELS_DURATION_HELP,
		.name = "asterisk_channels_duration_seconds",
		.get_value = get_channel_duration,
	},
};

static void get_total_call_count(struct prometheus_metric *metric)
{
	snprintf(metric->value, sizeof(metric->value), "%d", ast_processed_calls());
}

static void get_current_call_count(struct prometheus_metric *metric)
{
	snprintf(metric->value, sizeof(metric->value), "%d", ast_active_calls());
}

/*!
 * \internal
 * \brief Channel based metrics that are always available
 */
static struct prometheus_metric global_channel_metrics[] = {
	PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"asterisk_calls_sum",
		"Total call count.",
		&get_total_call_count
	),
	PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"asterisk_calls_count",
		"Current call count.",
		&get_current_call_count
	),
};

/*!
 * \internal
 * \brief Callback invoked when Prometheus scrapes the server
 *
 * \param response The response to populate with formatted metrics
 */
static void channels_scrape_cb(struct ast_str **response)
{
	struct ao2_container *channel_cache;
	struct ao2_container *channels;
	struct ao2_iterator it_chans;
	struct ast_channel_snapshot *snapshot;
	struct prometheus_metric *channel_metrics;
	char eid_str[32];
	int num_channels;
	int i, j;
	struct prometheus_metric channel_count = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"asterisk_channels_count",
		"Current channel count.",
		NULL
	);

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);

	channel_cache = ast_channel_cache_all();
	if (!channel_cache) {
		return;
	}

	channels = ao2_container_clone(channel_cache, 0);
	ao2_ref(channel_cache, -1);
	if (!channels) {
		return;
	}

	num_channels = ao2_container_count(channels);

	/* Channel count */
	PROMETHEUS_METRIC_SET_LABEL(&channel_count, 0, "eid", eid_str);
	snprintf(channel_count.value, sizeof(channel_count.value), "%d", num_channels);
	prometheus_metric_to_string(&channel_count, response);

	/* Global call values */
	for (i = 0; i < ARRAY_LEN(global_channel_metrics); i++) {
		PROMETHEUS_METRIC_SET_LABEL(&global_channel_metrics[i], 0, "eid", eid_str);
		global_channel_metrics[i].get_metric_value(&global_channel_metrics[i]);
		prometheus_metric_to_string(&global_channel_metrics[i], response);
	}

	if (num_channels == 0) {
		ao2_ref(channels, -1);
		return;
	}

	/* Channel dependent values */
	channel_metrics = ast_calloc(ARRAY_LEN(channel_metric_defs) * num_channels, sizeof(*channel_metrics));
	if (!channel_metrics) {
		ao2_ref(channels, -1);
		return;
	}

	it_chans = ao2_iterator_init(channels, 0);
	for (i = 0; (snapshot = ao2_iterator_next(&it_chans)); ao2_ref(snapshot, -1), i++) {
		for (j = 0; j < ARRAY_LEN(channel_metric_defs); j++) {
			int index = i * ARRAY_LEN(channel_metric_defs) + j;

			channel_metrics[index].type = PROMETHEUS_METRIC_GAUGE;
			ast_copy_string(channel_metrics[index].name, channel_metric_defs[j].name, sizeof(channel_metrics[index].name));
			channel_metrics[index].help = channel_metric_defs[j].help;
			PROMETHEUS_METRIC_SET_LABEL(&channel_metrics[index], 0, "eid", eid_str);
			PROMETHEUS_METRIC_SET_LABEL(&channel_metrics[index], 1, "name", (snapshot->base->name));
			PROMETHEUS_METRIC_SET_LABEL(&channel_metrics[index], 2, "id", (snapshot->base->uniqueid));
			PROMETHEUS_METRIC_SET_LABEL(&channel_metrics[index], 3, "type", (snapshot->base->type));
			if (snapshot->peer) {
				PROMETHEUS_METRIC_SET_LABEL(&channel_metrics[index], 4, "linkedid", (snapshot->peer->linkedid));
			}
			channel_metric_defs[j].get_value(&channel_metrics[index], snapshot);

			if (i > 0) {
				AST_LIST_INSERT_TAIL(&channel_metrics[j].children, &channel_metrics[index], entry);
			}
		}
	}
	ao2_iterator_destroy(&it_chans);

	for (j = 0; j < ARRAY_LEN(channel_metric_defs); j++) {
		prometheus_metric_to_string(&channel_metrics[j], response);
	}

	ast_free(channel_metrics);
	ao2_ref(channels, -1);
}

struct prometheus_callback channels_callback = {
	.name = "Channels callback",
	.callback_fn = channels_scrape_cb,
};

/*!
 * \internal
 * \brief Callback invoked when the core module is unloaded
 */
static void channel_metrics_unload_cb(void)
{
	prometheus_callback_unregister(&channels_callback);
}

/*!
 * \internal
 * \brief Metrics provider definition
 */
static struct prometheus_metrics_provider provider = {
	.name = "channels",
	.unload_cb = channel_metrics_unload_cb,
};

int channel_metrics_init(void)
{
	prometheus_metrics_provider_register(&provider);
	prometheus_callback_register(&channels_callback);

	return 0;
}
