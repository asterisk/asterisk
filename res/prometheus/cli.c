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
 * \brief Prometheus CLI Commands
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */
#include "asterisk.h"

#include "asterisk/cli.h"
#include "asterisk/localtime.h"
#include "asterisk/res_prometheus.h"
#include "prometheus_internal.h"

static char *prometheus_show_metrics(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_str *response;

	if (cmd == CLI_INIT) {
		e->command = "prometheus show metrics";
		e->usage =
			"Usage: prometheus show metrics\n"
			"       Displays the current metrics and their values,\n"
			"       without counting as an actual scrape.\n";
			return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	response = prometheus_scrape_to_string();
	if (!response) {
		ast_cli(a->fd, "Egads! An unknown error occurred getting the metrics\n");
		return CLI_FAILURE;
	}
	ast_cli(a->fd, "%s\n", ast_str_buffer(response));
	ast_free(response);

	return CLI_SUCCESS;
}

static char *prometheus_show_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct prometheus_general_config *config;
	char time_buffer[64];
	struct ast_tm last_scrape_local;
	struct timeval last_scrape_time;
	int64_t scrape_duration;

	if (cmd == CLI_INIT) {
		e->command = "prometheus show status";
		e->usage =
			"Usage: prometheus show status\n"
			"       Displays the status of metrics collection.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	config = prometheus_general_config_get();

	ast_cli(a->fd, "Prometheus Metrics Status:\n");
	ast_cli(a->fd, "\tEnabled: %s\n", config->enabled ? "Yes" : "No");
	ast_cli(a->fd, "\tURI: %s\n", config->uri);
	ast_cli(a->fd, "\tBasic Auth: %s\n", ast_strlen_zero(config->auth_username) ? "No": "Yes");
	ast_cli(a->fd, "\tLast Scrape Time: ");
	last_scrape_time = prometheus_last_scrape_time_get();
	if (last_scrape_time.tv_sec == 0 && last_scrape_time.tv_usec == 0) {
		snprintf(time_buffer, sizeof(time_buffer), "%s", "(N/A)");
	} else {
		ast_localtime(&last_scrape_time, &last_scrape_local, NULL);
		ast_strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &last_scrape_local);
	}
	ast_cli(a->fd, "%s\n", time_buffer);

	ast_cli(a->fd, "\tLast Scrape Duration: ");
	scrape_duration = prometheus_last_scrape_duration_get();
	if (scrape_duration < 0) {
		ast_cli(a->fd, "(N/A)\n");
	} else {
		ast_cli(a->fd, "%" PRIu64 " ms\n", scrape_duration);
	}

	ao2_ref(config, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_prometheus[] = {
	AST_CLI_DEFINE(prometheus_show_metrics, "Display the current metrics and their values"),
	AST_CLI_DEFINE(prometheus_show_status, "Display the status of Prometheus metrics collection"),
};

/*!
 * \internal
 * \brief Callback invoked when the core module is unloaded
 */
static void cli_unload_cb(void)
{
	ast_cli_unregister_multiple(cli_prometheus, ARRAY_LEN(cli_prometheus));
}

/*!
 * \internal
 * \brief Provider definition
 */
static struct prometheus_metrics_provider provider = {
	.name = "cli",
	.unload_cb = cli_unload_cb,
};

int cli_init(void)
{
	prometheus_metrics_provider_register(&provider);
	ast_cli_register_multiple(cli_prometheus, ARRAY_LEN(cli_prometheus));

	return 0;
}