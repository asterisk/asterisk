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
 * \brief Core Prometheus metrics API
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*** MODULEINFO
	<use>pjproject</use>
	<use type="module">res_pjsip</use>
	<use type="module">res_pjsip_outbound_registration</use>
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_prometheus" language="en_US">
		<synopsis>Resource for integration with Prometheus</synopsis>
		<configFile name="prometheus.conf">
			<configObject name="general">
				<synopsis>General settings.</synopsis>
				<description>
					<para>
					The <emphasis>general</emphasis> settings section contains information
					to configure Asterisk to serve up statistics for a Prometheus server.
					</para>
					<note>
						<para>You must enable Asterisk's HTTP server in <filename>http.conf</filename>
						for this module to function properly!
						</para>
					</note>
				</description>
				<configOption name="enabled" default="no">
					<synopsis>Enable or disable Prometheus statistics.</synopsis>
					<description>
						<enumlist>
							<enum name="no" />
							<enum name="yes" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="core_metrics_enabled" default="yes">
					<synopsis>Enable or disable core metrics.</synopsis>
					<description>
						<para>
						Core metrics show various properties of the Asterisk system, including
						how the binary was built, the version, uptime, last reload time, etc.
						Generally, these options are harmless and should always be enabled.
						This option mostly exists to disable output of all options for testing
						purposes, as well as for those foolish souls who really don't care
						what version of Asterisk they're running.
						</para>
						<enumlist>
							<enum name="no" />
							<enum name="yes" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="uri" default="metrics">
					<synopsis>The HTTP URI to serve metrics up on.</synopsis>
				</configOption>
				<configOption name="auth_username">
					<synopsis>Username to use for Basic Auth.</synopsis>
					<description>
						<para>
						If set, use Basic Auth to authenticate requests to the route
						specified by <replaceable>uri</replaceable>. Note that you
						will need to configure your Prometheus server with the
						appropriate auth credentials.
						</para>
						<para>
						If set, <replaceable>auth_password</replaceable> must also
						be set appropriately.
						</para>
						<warning>
							<para>
							It is highly recommended to set up Basic Auth. Failure
							to do so may result in useful information about your
							Asterisk system being made easily scrapable by the
							wide world. Consider yourself duly warned.
							</para>
						</warning>
					</description>
				</configOption>
				<configOption name="auth_password">
					<synopsis>Password to use for Basic Auth.</synopsis>
					<description>
						<para>
						If set, this is used in conjunction with <replaceable>auth_username</replaceable>
						to require Basic Auth for all requests to the Prometheus metrics. Note that
						setting this without <replaceable>auth_username</replaceable> will not
						do anything.
						</para>
					</description>
				</configOption>
				<configOption name="auth_realm" default="Asterisk Prometheus Metrics">
					<synopsis>Auth realm used in challenge responses</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

#define AST_MODULE_SELF_SYM __internal_res_prometheus_self

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/vector.h"
#include "asterisk/http.h"
#include "asterisk/config_options.h"
#include "asterisk/ast_version.h"
#include "asterisk/buildinfo.h"
#include "asterisk/res_prometheus.h"

#include "prometheus/prometheus_internal.h"

/*! \brief Lock that protects data structures during an HTTP scrape */
AST_MUTEX_DEFINE_STATIC(scrape_lock);

AST_VECTOR(, struct prometheus_metric *) metrics;

AST_VECTOR(, struct prometheus_callback *) callbacks;

AST_VECTOR(, const struct prometheus_metrics_provider *) providers;

static struct timeval last_scrape;

/*! \brief The actual module config */
struct module_config {
	/*! \brief General settings */
	struct prometheus_general_config *general;
};

static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "general",
	.item_offset = offsetof(struct module_config, general),
	.category_match = ACO_WHITELIST_EXACT,
	.category = "general",
};

struct aco_type *global_options[] = ACO_TYPES(&global_option);

struct aco_file prometheus_conf = {
	.filename = "prometheus.conf",
	.types = ACO_TYPES(&global_option),
};

/*! \brief The module configuration container */
static AO2_GLOBAL_OBJ_STATIC(global_config);

static void *module_config_alloc(void);
static int prometheus_config_pre_apply(void);
static void prometheus_config_post_apply(void);
/*! \brief Register information about the configs being processed by this module */
CONFIG_INFO_STANDARD(cfg_info, global_config, module_config_alloc,
	.files = ACO_FILES(&prometheus_conf),
	.pre_apply_config = prometheus_config_pre_apply,
	.post_apply_config = prometheus_config_post_apply,
);

#define CORE_PROPERTIES_HELP "Asterisk instance properties. The value of this will always be 1."

#define CORE_UPTIME_HELP "Asterisk instance uptime in seconds."

#define CORE_LAST_RELOAD_HELP "Time since last Asterisk reload in seconds."

#define CORE_METRICS_SCRAPE_TIME_HELP "Total time taken to collect metrics, in milliseconds"

static void get_core_uptime_cb(struct prometheus_metric *metric)
{
	struct timeval now = ast_tvnow();
	int64_t duration = ast_tvdiff_sec(now, ast_startuptime);

	snprintf(metric->value, sizeof(metric->value), "%" PRIu64, duration);
}

static void get_last_reload_cb(struct prometheus_metric *metric)
{
	struct timeval now = ast_tvnow();
	int64_t duration = ast_tvdiff_sec(now, ast_lastreloadtime);

	snprintf(metric->value, sizeof(metric->value), "%" PRIu64, duration);
}

/*!
 * \brief The scrape duration metric
 *
 * \details
 * This metric is special in that it should never be registered.
 * Instead, the HTTP callback function that walks the metrics will
 * always populate this metric explicitly if core metrics
 * are enabled.
 */
static struct prometheus_metric core_scrape_metric =
	PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"asterisk_core_scrape_time_ms",
		CORE_METRICS_SCRAPE_TIME_HELP,
		NULL);

#define METRIC_CORE_PROPS_ARRAY_INDEX 0
/*!
 * \brief Core metrics to scrape
 */
static struct prometheus_metric core_metrics[] = {
	PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"asterisk_core_properties",
		CORE_PROPERTIES_HELP,
		NULL),
	PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"asterisk_core_uptime_seconds",
		CORE_UPTIME_HELP,
		get_core_uptime_cb),
	PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"asterisk_core_last_reload_seconds",
		CORE_LAST_RELOAD_HELP,
		get_last_reload_cb),
};

/*!
 * \internal
 * \brief Compare two metrics to see if their name / labels / values match
 *
 * \param left The first metric to compare
 * \param right The second metric to compare
 *
 * \retval 0 The metrics are not the same
 * \retval 1 The metrics are the same
 */
static int prometheus_metric_cmp(struct prometheus_metric *left,
	struct prometheus_metric *right)
{
	int i;
	ast_debug(5, "Comparison: Names %s == %s\n", left->name, right->name);
	if (strcmp(left->name, right->name)) {
		return 0;
	}

	for (i = 0; i < PROMETHEUS_MAX_LABELS; i++) {
		ast_debug(5, "Comparison: Label %d Names %s == %s\n", i,
			left->labels[i].name, right->labels[i].name);
		if (strcmp(left->labels[i].name, right->labels[i].name)) {
			return 0;
		}

		ast_debug(5, "Comparison: Label %d Values %s == %s\n", i,
			left->labels[i].value, right->labels[i].value);
		if (strcmp(left->labels[i].value, right->labels[i].value)) {
			return 0;
		}
	}

	ast_debug(5, "Copmarison: %s (%p) is equal to %s (%p)\n",
		left->name, left, right->name, right);
	return 1;
}

int prometheus_metric_registered_count(void)
{
	SCOPED_MUTEX(lock, &scrape_lock);

	return AST_VECTOR_SIZE(&metrics);
}

int prometheus_metric_register(struct prometheus_metric *metric)
{
	SCOPED_MUTEX(lock, &scrape_lock);
	int i;

	if (!metric) {
		return -1;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&metrics); i++) {
		struct prometheus_metric *existing = AST_VECTOR_GET(&metrics, i);
		struct prometheus_metric *child;

		if (prometheus_metric_cmp(existing, metric)) {
			ast_log(AST_LOG_NOTICE,
				"Refusing registration of existing Prometheus metric: %s\n",
				metric->name);
			return -1;
		}

		AST_LIST_TRAVERSE(&existing->children, child, entry) {
			if (prometheus_metric_cmp(child, metric)) {
				ast_log(AST_LOG_NOTICE,
					"Refusing registration of existing Prometheus metric: %s\n",
					metric->name);
				return -1;
			}
		}

		if (!strcmp(metric->name, existing->name)) {
			ast_debug(3, "Nesting metric '%s' as child (%p) under existing (%p)\n",
				metric->name, metric, existing);
			AST_LIST_INSERT_TAIL(&existing->children, metric, entry);
			return 0;
		}
	}

	ast_debug(3, "Tracking new root metric '%s'\n", metric->name);
	if (AST_VECTOR_APPEND(&metrics, metric)) {
		ast_log(AST_LOG_WARNING, "Failed to grow vector to make room for Prometheus metric: %s\n",
			metric->name);
		return -1;
	}

	return 0;
}

int prometheus_metric_unregister(struct prometheus_metric *metric)
{
	if (!metric) {
		return -1;
	}

	{
		SCOPED_MUTEX(lock, &scrape_lock);
		int i;

		ast_debug(3, "Removing metric '%s'\n", metric->name);
		for (i = 0; i < AST_VECTOR_SIZE(&metrics); i++) {
			struct prometheus_metric *existing = AST_VECTOR_GET(&metrics, i);

			/*
			 * If this is a complete match, remove the matching metric
			 * and place its children back into the list
			 */
			if (prometheus_metric_cmp(existing, metric)) {
				struct prometheus_metric *root;

				AST_VECTOR_REMOVE(&metrics, i, 1);
				root = AST_LIST_REMOVE_HEAD(&existing->children, entry);
				if (root) {
					struct prometheus_metric *child;
					AST_LIST_TRAVERSE_SAFE_BEGIN(&existing->children, child, entry) {
						AST_LIST_REMOVE_CURRENT(entry);
						AST_LIST_INSERT_TAIL(&root->children, child, entry);
					}
					AST_LIST_TRAVERSE_SAFE_END;
					AST_VECTOR_INSERT_AT(&metrics, i, root);
				}
				prometheus_metric_free(existing);
				return 0;
			}

			/*
			 * Name match, but labels don't match. Find the matching entry with
			 * labels and remove it along with all of its children
			 */
			if (!strcmp(existing->name, metric->name)) {
				struct prometheus_metric *child;

				AST_LIST_TRAVERSE_SAFE_BEGIN(&existing->children, child, entry) {
					if (prometheus_metric_cmp(child, metric)) {
						AST_LIST_REMOVE_CURRENT(entry);
						prometheus_metric_free(child);
						return 0;
					}
				}
				AST_LIST_TRAVERSE_SAFE_END;
			}
		}
	}

	return -1;
}

void prometheus_metric_free(struct prometheus_metric *metric)
{
	struct prometheus_metric *child;

	if (!metric) {
		return;
	}

	while ((child = AST_LIST_REMOVE_HEAD(&metric->children, entry))) {
		prometheus_metric_free(child);
	}
	ast_mutex_destroy(&metric->lock);

	if (metric->allocation_strategy == PROMETHEUS_METRIC_ALLOCD) {
		return;
	} else if (metric->allocation_strategy == PROMETHEUS_METRIC_MALLOCD) {
		ast_free(metric);
	}
}

/*!
 * \internal
 * \brief Common code for creating a metric
 *
 * \param name The name of the metric
 * \param help Help string to output when rendered. This must be static.
 *
 * \retval NULL on failure
 */
static struct prometheus_metric *prometheus_metric_create(const char *name, const char *help)
{
	struct prometheus_metric *metric = NULL;

	metric = ast_calloc(1, sizeof(*metric));
	if (!metric) {
		return NULL;
	}
	metric->allocation_strategy = PROMETHEUS_METRIC_MALLOCD;
	ast_mutex_init(&metric->lock);

	ast_copy_string(metric->name, name, sizeof(metric->name));
	metric->help = help;

	return metric;
}

struct prometheus_metric *prometheus_gauge_create(const char *name, const char *help)
{
	struct prometheus_metric *metric;

	metric = prometheus_metric_create(name, help);
	if (!metric) {
		return NULL;
	}
	metric->type = PROMETHEUS_METRIC_GAUGE;

	return metric;
}

struct prometheus_metric *prometheus_counter_create(const char *name, const char *help)
{
	struct prometheus_metric *metric;

	metric = prometheus_metric_create(name, help);
	if (!metric) {
		return NULL;
	}
	metric->type = PROMETHEUS_METRIC_COUNTER;

	return metric;
}

static const char *prometheus_metric_type_to_string(enum prometheus_metric_type type)
{
	switch (type) {
	case PROMETHEUS_METRIC_COUNTER:
		return "counter";
	case PROMETHEUS_METRIC_GAUGE:
		return "gauge";
	default:
		ast_assert(0);
		return "unknown";
	}
}

/*!
 * \internal
 * \brief Render a metric to text
 *
 * \param metric The metric to render
 * \param output The string buffer to append the text to
 */
static void prometheus_metric_full_to_string(struct prometheus_metric *metric,
	struct ast_str **output)
{
	int i;
	int labels_exist = 0;

	ast_str_append(output, 0, "%s", metric->name);

	for (i = 0; i < PROMETHEUS_MAX_LABELS; i++) {
		if (!ast_strlen_zero(metric->labels[i].name)) {
			labels_exist = 1;
			if (i == 0) {
				ast_str_append(output, 0, "%s", "{");
			} else {
				ast_str_append(output, 0, "%s", ",");
			}
			ast_str_append(output, 0, "%s=\"%s\"",
				metric->labels[i].name,
				metric->labels[i].value);
		}
	}

	if (labels_exist) {
		ast_str_append(output, 0, "%s", "}");
	}

	/*
	 * If no value exists, put in a 0. That ensures we don't anger Prometheus.
	 */
	if (ast_strlen_zero(metric->value)) {
		ast_str_append(output, 0, " 0\n");
	} else {
		ast_str_append(output, 0, " %s\n", metric->value);
	}
}

void prometheus_metric_to_string(struct prometheus_metric *metric,
	struct ast_str **output)
{
	struct prometheus_metric *child;

	ast_str_append(output, 0, "# HELP %s %s\n", metric->name, metric->help);
	ast_str_append(output, 0, "# TYPE %s %s\n", metric->name,
		prometheus_metric_type_to_string(metric->type));
	prometheus_metric_full_to_string(metric, output);
	AST_LIST_TRAVERSE(&metric->children, child, entry) {
		prometheus_metric_full_to_string(child, output);
	}
}

int prometheus_callback_register(struct prometheus_callback *callback)
{
	SCOPED_MUTEX(lock, &scrape_lock);

	if (!callback || !callback->callback_fn || ast_strlen_zero(callback->name)) {
		return -1;
	}

	AST_VECTOR_APPEND(&callbacks, callback);

	return 0;
}

void prometheus_callback_unregister(struct prometheus_callback *callback)
{
	SCOPED_MUTEX(lock, &scrape_lock);
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&callbacks); i++) {
		struct prometheus_callback *entry = AST_VECTOR_GET(&callbacks, i);

		if (!strcmp(callback->name, entry->name)) {
			AST_VECTOR_REMOVE(&callbacks, i, 1);
			return;
		}
	}
}

static void scrape_metrics(struct ast_str **response)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&callbacks); i++) {
		struct prometheus_callback *callback = AST_VECTOR_GET(&callbacks, i);

		if (!callback) {
			continue;
		}

		callback->callback_fn(response);
	}

	for (i = 0; i < AST_VECTOR_SIZE(&metrics); i++) {
		struct prometheus_metric *metric = AST_VECTOR_GET(&metrics, i);

		if (!metric) {
			continue;
		}

		ast_mutex_lock(&metric->lock);
		if (metric->get_metric_value) {
			metric->get_metric_value(metric);
		}
		prometheus_metric_to_string(metric, response);
		ast_mutex_unlock(&metric->lock);
	}
}

static int http_callback(struct ast_tcptls_session_instance *ser,
	const struct ast_http_uri *urih, const char *uri, enum ast_http_method method,
	struct ast_variable *get_params, struct ast_variable *headers)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(global_config), ao2_cleanup);
	struct ast_str *response = NULL;
	struct timeval start;
	struct timeval end;

	/* If there is no module config or we're not enabled, we can't handle requests */
	if (!mod_cfg || !mod_cfg->general->enabled) {
		goto err503;
	}

	if (!ast_strlen_zero(mod_cfg->general->auth_username)) {
		struct ast_http_auth *http_auth;

		http_auth = ast_http_get_auth(headers);
		if (!http_auth) {
			goto err401;
		}

		if (strcmp(http_auth->userid, mod_cfg->general->auth_username)) {
			ast_debug(5, "Invalid username provided for auth request: %s\n", http_auth->userid);
			ao2_ref(http_auth, -1);
			goto err401;
		}

		if (strcmp(http_auth->password, mod_cfg->general->auth_password)) {
			ast_debug(5, "Invalid password provided for auth request: %s\n", http_auth->password);
			ao2_ref(http_auth, -1);
			goto err401;
		}

		ao2_ref(http_auth, -1);
	}

	response = ast_str_create(512);
	if (!response) {
		goto err500;
	}

	start = ast_tvnow();

	ast_mutex_lock(&scrape_lock);

	last_scrape = start;
	scrape_metrics(&response);

	if (mod_cfg->general->core_metrics_enabled) {
		int64_t duration;

		end = ast_tvnow();
		duration = ast_tvdiff_ms(end, start);
		snprintf(core_scrape_metric.value,
			sizeof(core_scrape_metric.value),
			"%" PRIu64,
			duration);
		prometheus_metric_to_string(&core_scrape_metric, &response);
	}
	ast_mutex_unlock(&scrape_lock);

	ast_http_send(ser, method, 200, "OK", NULL, response, 0, 0);

	return 0;

err401:
	{
		struct ast_str *auth_challenge_headers;

		auth_challenge_headers = ast_str_create(128);
		if (!auth_challenge_headers) {
			goto err500;
		}
		ast_str_append(&auth_challenge_headers, 0,
			"WWW-Authenticate: Basic realm=\"%s\"\r\n",
			mod_cfg->general->auth_realm);
		/* ast_http_send takes ownership of the ast_str */
		ast_http_send(ser, method, 401, "Unauthorized", auth_challenge_headers, NULL, 0, 1);
	}
	ast_free(response);
	return 0;
err503:
	ast_http_send(ser, method, 503, "Service Unavailable", NULL, NULL, 0, 1);
	ast_free(response);
	return 0;
err500:
	ast_http_send(ser, method, 500, "Server Error", NULL, NULL, 0, 1);
	ast_free(response);
	return 0;
}

struct ast_str *prometheus_scrape_to_string(void)
{
	struct ast_str *response;

	response = ast_str_create(512);
	if (!response) {
		return NULL;
	}

	ast_mutex_lock(&scrape_lock);
	scrape_metrics(&response);
	ast_mutex_unlock(&scrape_lock);

	return response;
}

int64_t prometheus_last_scrape_duration_get(void)
{
	int64_t duration;

	if (sscanf(core_scrape_metric.value, "%" PRIu64, &duration) != 1) {
		return -1;
	}

	return duration;
}

struct timeval prometheus_last_scrape_time_get(void)
{
	SCOPED_MUTEX(lock, &scrape_lock);

	return last_scrape;
}

static void prometheus_general_config_dtor(void *obj)
{
	struct prometheus_general_config *config = obj;

	ast_string_field_free_memory(config);
}

void *prometheus_general_config_alloc(void)
{
	struct prometheus_general_config *config;

	config = ao2_alloc(sizeof(*config), prometheus_general_config_dtor);
	if (!config || ast_string_field_init(config, 32)) {
		return NULL;
	}

	return config;
}

struct prometheus_general_config *prometheus_general_config_get(void)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(global_config), ao2_cleanup);

	if (!mod_cfg) {
		return NULL;
	}
	ao2_bump(mod_cfg->general);

	return mod_cfg->general;
}

void prometheus_general_config_set(struct prometheus_general_config *config)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(global_config), ao2_cleanup);

	if (!mod_cfg) {
		return;
	}
	ao2_replace(mod_cfg->general, config);
	prometheus_config_post_apply();
}


/*! \brief Configuration object destructor */
static void module_config_dtor(void *obj)
{
	struct module_config *config = obj;

	if (config->general) {
		ao2_ref(config->general, -1);
	}
}

/*! \brief Module config constructor */
static void *module_config_alloc(void)
{
	struct module_config *config;

	config = ao2_alloc(sizeof(*config), module_config_dtor);
	if (!config) {
		return NULL;
	}

	config->general = prometheus_general_config_alloc();
	if (!config->general) {
		ao2_ref(config, -1);
		config = NULL;
	}

	return config;
}

static struct ast_http_uri prometheus_uri = {
	.description = "Prometheus Metrics URI",
	.callback = http_callback,
	.has_subtree = 1,
	.data = NULL,
	.key = __FILE__,
};

/*!
 * \brief Pre-apply callback for the config framework.
 *
 * This validates that required fields exist and are populated.
 */
static int prometheus_config_pre_apply(void)
{
	struct module_config *config = aco_pending_config(&cfg_info);

	if (!config->general->enabled) {
		/* If we're not enabled, we don't care about anything else */
		return 0;
	}

	if (!ast_strlen_zero(config->general->auth_username)
		&& ast_strlen_zero(config->general->auth_password)) {
		ast_log(AST_LOG_ERROR, "'auth_username' set without a corresponding 'auth_password'\n");
		return -1;
	}

	return 0;
}

/*!
 * \brief Post-apply callback for the config framework.
 *
 * This sets any run-time information derived from the configuration
 */
static void prometheus_config_post_apply(void)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(global_config), ao2_cleanup);
	int i;

	/* We can get away with this as the lifetime of the URI
	 * registered with the HTTP core is contained within
	 * the lifetime of the module configuration
	 */
	prometheus_uri.uri = mod_cfg->general->uri;

	/* Re-register the core metrics */
	for (i = 0; i < ARRAY_LEN(core_metrics); i++) {
		prometheus_metric_unregister(&core_metrics[i]);
	}
	if (mod_cfg->general->core_metrics_enabled) {
		char eid_str[32];
		ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);

		PROMETHEUS_METRIC_SET_LABEL(&core_scrape_metric, 0, "eid", eid_str);

		PROMETHEUS_METRIC_SET_LABEL(&core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX],
			1, "version", ast_get_version());
		PROMETHEUS_METRIC_SET_LABEL(&core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX],
			2, "build_options", ast_get_build_opts());
		PROMETHEUS_METRIC_SET_LABEL(&core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX],
			3, "build_date", ast_build_date);
		PROMETHEUS_METRIC_SET_LABEL(&core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX],
			4, "build_os", ast_build_os);
		PROMETHEUS_METRIC_SET_LABEL(&core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX],
			5, "build_kernel", ast_build_kernel);
		PROMETHEUS_METRIC_SET_LABEL(&core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX],
			6, "build_host", ast_build_hostname);
		snprintf(core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX].value,
			sizeof(core_metrics[METRIC_CORE_PROPS_ARRAY_INDEX].value),
			"%d", 1);

		for (i = 0; i < ARRAY_LEN(core_metrics); i++) {
			PROMETHEUS_METRIC_SET_LABEL(&core_metrics[i], 0, "eid", eid_str);
			prometheus_metric_register(&core_metrics[i]);
		}
	}
}

void prometheus_metrics_provider_register(const struct prometheus_metrics_provider *provider)
{
	AST_VECTOR_APPEND(&providers, provider);
}

static int unload_module(void)
{
	SCOPED_MUTEX(lock, &scrape_lock);
	int i;

	ast_http_uri_unlink(&prometheus_uri);

	for (i = 0; i < AST_VECTOR_SIZE(&providers); i++) {
		const struct prometheus_metrics_provider *provider = AST_VECTOR_GET(&providers, i);

		if (!provider->unload_cb) {
			continue;
		}

		provider->unload_cb();
	}

	for (i = 0; i < AST_VECTOR_SIZE(&metrics); i++) {
		struct prometheus_metric *metric = AST_VECTOR_GET(&metrics, i);

		prometheus_metric_free(metric);
	}
	AST_VECTOR_FREE(&metrics);

	AST_VECTOR_FREE(&callbacks);

	AST_VECTOR_FREE(&providers);

	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(global_config);

	return 0;
}

static int reload_module(void) {
	SCOPED_MUTEX(lock, &scrape_lock);
	int i;
	struct prometheus_general_config *general_config;

	ast_http_uri_unlink(&prometheus_uri);
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		return -1;
	}

	/* Our config should be all reloaded now */
	general_config = prometheus_general_config_get();
	for (i = 0; i < AST_VECTOR_SIZE(&providers); i++) {
		const struct prometheus_metrics_provider *provider = AST_VECTOR_GET(&providers, i);

		if (!provider->reload_cb) {
			continue;
		}

		if (provider->reload_cb(general_config)) {
			ast_log(AST_LOG_WARNING, "Failed to reload metrics provider %s\n", provider->name);
			ao2_ref(general_config, -1);
			return -1;
		}
	}
	ao2_ref(general_config, -1);

	if (ast_http_uri_link(&prometheus_uri)) {
		ast_log(AST_LOG_WARNING, "Failed to re-register Prometheus Metrics URI during reload\n");
		return -1;
	}

	return 0;
}

static int load_module(void)
{
	SCOPED_MUTEX(lock, &scrape_lock);

	if (AST_VECTOR_INIT(&metrics, 64)) {
		goto cleanup;
	}

	if (AST_VECTOR_INIT(&callbacks, 8)) {
		goto cleanup;
	}

	if (AST_VECTOR_INIT(&providers, 8)) {
		goto cleanup;
	}

	if (aco_info_init(&cfg_info)) {
		goto cleanup;
	}
	aco_option_register(&cfg_info, "enabled", ACO_EXACT, global_options, "no", OPT_BOOL_T, 1, FLDSET(struct prometheus_general_config, enabled));
	aco_option_register(&cfg_info, "core_metrics_enabled", ACO_EXACT, global_options, "yes", OPT_BOOL_T, 1, FLDSET(struct prometheus_general_config, core_metrics_enabled));
	aco_option_register(&cfg_info, "uri", ACO_EXACT, global_options, "", OPT_STRINGFIELD_T, 1, STRFLDSET(struct prometheus_general_config, uri));
	aco_option_register(&cfg_info, "auth_username", ACO_EXACT, global_options, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct prometheus_general_config, auth_username));
	aco_option_register(&cfg_info, "auth_password", ACO_EXACT, global_options, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct prometheus_general_config, auth_password));
	aco_option_register(&cfg_info, "auth_realm", ACO_EXACT, global_options, "Asterisk Prometheus Metrics", OPT_STRINGFIELD_T, 0, STRFLDSET(struct prometheus_general_config, auth_realm));
	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		goto cleanup;
	}

	if (cli_init()
		|| channel_metrics_init()
		|| endpoint_metrics_init()
		|| bridge_metrics_init()
		|| pjsip_outbound_registration_metrics_init()) {
		goto cleanup;
	}

	if (ast_http_uri_link(&prometheus_uri)) {
		goto cleanup;
	}

	return AST_MODULE_LOAD_SUCCESS;

cleanup:
	ast_http_uri_unlink(&prometheus_uri);
	aco_info_destroy(&cfg_info);
	AST_VECTOR_FREE(&metrics);
	AST_VECTOR_FREE(&callbacks);
	AST_VECTOR_FREE(&providers);

	return AST_MODULE_LOAD_DECLINE;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Asterisk Prometheus Module",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_DEFAULT,
#ifdef HAVE_PJPROJECT
	/* This module explicitly calls into res_pjsip if Asterisk is built with PJSIP support, so they are required. */
	.requires = "res_pjsip,res_pjsip_outbound_registration",
#endif
);
