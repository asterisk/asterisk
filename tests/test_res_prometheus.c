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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_prometheus</depend>
	<depend>curl</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <curl/curl.h>

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/res_prometheus.h"

#define CATEGORY "/res/prometheus/"

static char server_uri[512];

struct prometheus_general_config *module_config;

static void curl_free_wrapper(void *ptr)
{
	if (!ptr) {
		return;
	}

	curl_easy_cleanup(ptr);
}

static void prometheus_metric_free_wrapper(void *ptr)
{
	if (prometheus_metric_unregister(ptr)) {
		prometheus_metric_free(ptr);
	}
}

static struct prometheus_general_config *config_alloc(void)
{
	struct prometheus_general_config *config;

	config = prometheus_general_config_alloc();
	if (!config) {
		return NULL;
	}

	/* Set what we need on the config for most tests */
	ast_string_field_set(config, uri, "test_metrics");
	config->enabled = 1;
	config->core_metrics_enabled = 0;

	return config;
}

static CURL *get_curl_instance(void)
{
	CURL *curl;

	curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_URL, server_uri);

	return curl;
}

static size_t curl_write_string_callback(void *contents, size_t size, size_t nmemb, void *userdata)
{
	struct ast_str **buffer = userdata;
	size_t realsize = size * nmemb;
	char *rawdata;

	rawdata = ast_malloc(realsize + 1);
	if (!rawdata) {
		return 0;
	}
	memcpy(rawdata, contents, realsize);
	rawdata[realsize] = 0;
	ast_str_append(buffer, 0, "%s", rawdata);
	ast_free(rawdata);

	return realsize;
}

static void metric_values_get_counter_value_cb(struct prometheus_metric *metric)
{
	strcpy(metric->value, "2");
}

AST_TEST_DEFINE(metric_values)
{
	RAII_VAR(CURL *, curl, NULL, curl_free_wrapper);
	RAII_VAR(struct ast_str *, buffer, NULL, ast_free);
	int res;
	struct prometheus_metric test_counter_one = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"test_counter_one",
		"A test counter",
		NULL);
	struct prometheus_metric test_counter_two = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"test_counter_two",
		"A test counter",
		metric_values_get_counter_value_cb);
	enum ast_test_result_state result = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test value generation/respecting in metrics";
		info->description =
			"Metrics have two ways to provide values when the HTTP callback\n"
			"is invoked:\n"
			"1. By using the direct value that resides in the metric\n"
			"2. By providing a callback function to specify the value\n"
			"This test verifies that both function appropriately when the\n"
			"HTTP callback is called.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_str_create(128);
	if (!buffer) {
		return AST_TEST_FAIL;
	}

	curl = get_curl_instance();
	if (!curl) {
		return AST_TEST_FAIL;
	}

	ast_test_validate_cleanup(test, prometheus_metric_register(&test_counter_one) == 0, result, metric_values_cleanup);
	ast_test_validate_cleanup(test, prometheus_metric_register(&test_counter_two) == 0, result, metric_values_cleanup);
	strcpy(test_counter_one.value, "1");

	ast_test_status_update(test, " -> CURLing request...\n");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_test_status_update(test, "Failed to execute CURL: %d\n", res);
		result = AST_TEST_FAIL;
		goto metric_values_cleanup;
	}

	ast_test_status_update(test, " -> Retrieved: %s\n", ast_str_buffer(buffer));
	ast_test_validate_cleanup(test, strstr(ast_str_buffer(buffer),
		"# HELP test_counter_one A test counter\n"
		"# TYPE test_counter_one counter\n"
		"test_counter_one 1\n"
		"# HELP test_counter_two A test counter\n"
		"# TYPE test_counter_two counter\n"
		"test_counter_two 2\n") != NULL, result, metric_values_cleanup);

metric_values_cleanup:
	prometheus_metric_unregister(&test_counter_one);
	prometheus_metric_unregister(&test_counter_two);

	return result;
}

static void prometheus_metric_callback(struct ast_str **output)
{
	struct prometheus_metric test_counter = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"test_counter",
		"A test counter",
		NULL);

	prometheus_metric_to_string(&test_counter, output);
}

AST_TEST_DEFINE(metric_callback_register)
{
	RAII_VAR(CURL *, curl, NULL, curl_free_wrapper);
	RAII_VAR(struct ast_str *, buffer, NULL, ast_free);
	int res;
	struct prometheus_callback callback = {
		.name = "test_callback",
		.callback_fn = &prometheus_metric_callback,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test registration of callbacks";
		info->description =
			"This test covers callback registration. It registers\n"
			"a callback that is invoked when an HTTP request is made,\n"
			"and it verifies that during said callback the output to\n"
			"the response string is correctly appended to. It also verifies\n"
			"that unregistered callbacks are not invoked.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_str_create(128);
	if (!buffer) {
		return AST_TEST_FAIL;
	}

	ast_test_validate(test, prometheus_callback_register(&callback) == 0);

	curl = get_curl_instance();
	if (!curl) {
		return AST_TEST_NOT_RUN;
	}

	ast_test_status_update(test, " -> CURLing request...\n");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_test_status_update(test, "Failed to execute CURL: %d\n", res);
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, " -> Retrieved: %s\n", ast_str_buffer(buffer));
	ast_test_validate(test, strstr(ast_str_buffer(buffer),
		"# HELP test_counter A test counter\n"
		"# TYPE test_counter counter\n"
		"test_counter 0\n") != NULL);

	prometheus_callback_unregister(&callback);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(metric_register)
{
	struct prometheus_metric test_counter = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"test_counter",
		"A test counter",
		NULL);
	RAII_VAR(struct prometheus_metric *, test_gauge, NULL, prometheus_metric_free_wrapper);
	RAII_VAR(struct prometheus_metric *, test_gauge_child_one, NULL, prometheus_metric_free_wrapper);
	RAII_VAR(struct prometheus_metric *, test_gauge_child_two, NULL, prometheus_metric_free_wrapper);
	RAII_VAR(struct prometheus_metric *, bad_metric, NULL, prometheus_metric_free_wrapper);
	enum ast_test_result_state result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test registration of metrics";
		info->description =
			"This test covers the following registration scenarios:\n"
			"- Nominal registration of simple metrics\n"
			"- Registration of metrics with different allocation strategies\n"
			"- Nested metrics with label families\n"
			"- Off nominal registration with simple name collisions\n"
			"- Off nominal registration with label collisions";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing nominal registration\n");
	ast_test_status_update(test, "-> Static metric\n");
	ast_test_validate_cleanup(test, prometheus_metric_register(&test_counter) == 0, result, metric_register_cleanup);
	ast_test_status_update(test, "-> Malloc'd metric\n");
	test_gauge = prometheus_gauge_create("test_gauge", "A test gauge");
	ast_test_validate(test, test_gauge != NULL);
	ast_test_validate_cleanup(test, prometheus_metric_register(test_gauge) == 0, result, metric_register_cleanup);
	ast_test_validate_cleanup(test, prometheus_metric_registered_count() == 2, result, metric_register_cleanup);

	ast_test_status_update(test, "Testing nominal registration of child metrics\n");
	test_gauge_child_one = prometheus_gauge_create("test_gauge", "A test gauge");
	ast_test_validate_cleanup(test, test_gauge_child_one != NULL, result, metric_register_cleanup);
	PROMETHEUS_METRIC_SET_LABEL(test_gauge_child_one, 0, "key_one", "value_one");
	PROMETHEUS_METRIC_SET_LABEL(test_gauge_child_one, 1, "key_two", "value_one");
	test_gauge_child_two = prometheus_gauge_create("test_gauge", "A test gauge");
	ast_test_validate_cleanup(test, test_gauge_child_two != NULL, result, metric_register_cleanup);
	PROMETHEUS_METRIC_SET_LABEL(test_gauge_child_two, 0, "key_one", "value_two");
	PROMETHEUS_METRIC_SET_LABEL(test_gauge_child_two, 1, "key_two", "value_two");
	ast_test_validate_cleanup(test, prometheus_metric_register(test_gauge_child_one) == 0, result, metric_register_cleanup);
	ast_test_validate_cleanup(test, prometheus_metric_register(test_gauge_child_two) == 0, result, metric_register_cleanup);
	ast_test_validate_cleanup(test, prometheus_metric_registered_count() == 2, result, metric_register_cleanup);
	ast_test_validate_cleanup(test, test_gauge->children.first == test_gauge_child_one, result, metric_register_cleanup);
	ast_test_validate_cleanup(test, test_gauge->children.last == test_gauge_child_two, result, metric_register_cleanup);

	ast_test_status_update(test, "Testing name collisions\n");
	bad_metric = prometheus_counter_create("test_counter", "A test counter");
	ast_test_validate_cleanup(test, bad_metric != NULL, result, metric_register_cleanup);
	ast_test_validate_cleanup(test, prometheus_metric_register(bad_metric) != 0, result, metric_register_cleanup);
	prometheus_metric_free(bad_metric);
	bad_metric = NULL;

	ast_test_status_update(test, "Testing label collisions\n");
	bad_metric = prometheus_gauge_create("test_gauge", "A test gauge");
	ast_test_validate_cleanup(test, bad_metric != NULL, result, metric_register_cleanup);
	PROMETHEUS_METRIC_SET_LABEL(bad_metric, 0, "key_one", "value_one");
	PROMETHEUS_METRIC_SET_LABEL(bad_metric, 1, "key_two", "value_one");
	ast_test_validate_cleanup(test, prometheus_metric_register(bad_metric) != 0, result, metric_register_cleanup);
	prometheus_metric_free(bad_metric);
	bad_metric = NULL;

	ast_test_status_update(test, "Testing removal of metrics\n");
	prometheus_metric_unregister(test_gauge_child_two);
	test_gauge_child_two = NULL;

	ast_test_validate_cleanup(test, prometheus_metric_registered_count() == 2, result, metric_register_cleanup);
	prometheus_metric_unregister(test_gauge);
	test_gauge = NULL;

	ast_test_validate_cleanup(test, prometheus_metric_registered_count() == 2, result, metric_register_cleanup);
	prometheus_metric_unregister(test_gauge_child_one);
	test_gauge_child_one = NULL;

	ast_test_validate_cleanup(test, prometheus_metric_registered_count() == 1, result, metric_register_cleanup);
	prometheus_metric_unregister(&test_counter);

	ast_test_validate_cleanup(test, prometheus_metric_registered_count() == 0, result, metric_register_cleanup);

	return AST_TEST_PASS;

metric_register_cleanup:
	prometheus_metric_unregister(&test_counter);
	return result;
}

AST_TEST_DEFINE(counter_to_string)
{
	struct prometheus_metric test_counter = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"test_counter",
		"A test counter",
		NULL);
	struct prometheus_metric test_counter_child_one = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"test_counter",
		"A test counter",
		NULL);
	struct prometheus_metric test_counter_child_two = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_COUNTER,
		"test_counter",
		"A test counter",
		NULL);
	RAII_VAR(struct ast_str *, buffer, NULL, ast_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test formatting of counters";
		info->description =
			"This test covers the formatting of printed counters";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_str_create(128);
	if (!buffer) {
		return AST_TEST_FAIL;
	}

	PROMETHEUS_METRIC_SET_LABEL(&test_counter_child_one, 0, "key_one", "value_one");
	PROMETHEUS_METRIC_SET_LABEL(&test_counter_child_one, 1, "key_two", "value_one");
	PROMETHEUS_METRIC_SET_LABEL(&test_counter_child_two, 0, "key_one", "value_two");
	PROMETHEUS_METRIC_SET_LABEL(&test_counter_child_two, 1, "key_two", "value_two");
	AST_LIST_INSERT_TAIL(&test_counter.children, &test_counter_child_one, entry);
	AST_LIST_INSERT_TAIL(&test_counter.children, &test_counter_child_two, entry);
	prometheus_metric_to_string(&test_counter, &buffer);
	ast_test_validate(test, strcmp(ast_str_buffer(buffer),
		"# HELP test_counter A test counter\n"
		"# TYPE test_counter counter\n"
		"test_counter 0\n"
		"test_counter{key_one=\"value_one\",key_two=\"value_one\"} 0\n"
		"test_counter{key_one=\"value_two\",key_two=\"value_two\"} 0\n") == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(counter_create)
{
	RAII_VAR(struct prometheus_metric *, metric, NULL, prometheus_metric_free_wrapper);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test creation (and destruction) of malloc'd counters";
		info->description =
			"This test covers creating a counter metric and destroying\n"
			"it. The metric should be malloc'd.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	metric = prometheus_counter_create("test_counter", "A test counter");
	ast_test_validate(test, metric != NULL);
	ast_test_validate(test, metric->type == PROMETHEUS_METRIC_COUNTER);
	ast_test_validate(test, metric->allocation_strategy = PROMETHEUS_METRIC_MALLOCD);
	ast_test_validate(test, !strcmp(metric->help, "A test counter"));
	ast_test_validate(test, !strcmp(metric->name, "test_counter"));
	ast_test_validate(test, !strcmp(metric->value, ""));
	ast_test_validate(test, metric->children.first == NULL);
	ast_test_validate(test, metric->children.last == NULL);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(gauge_to_string)
{
	struct prometheus_metric test_gauge = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"test_gauge",
		"A test gauge",
		NULL);
	struct prometheus_metric test_gauge_child_one = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"test_gauge",
		"A test gauge",
		NULL);
	struct prometheus_metric test_gauge_child_two = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"test_gauge",
		"A test gauge",
		NULL);
	RAII_VAR(struct ast_str *, buffer, NULL, ast_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test formatting of gauges";
		info->description =
			"This test covers the formatting of printed gauges";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_str_create(128);
	if (!buffer) {
		return AST_TEST_FAIL;
	}

	PROMETHEUS_METRIC_SET_LABEL(&test_gauge_child_one, 0, "key_one", "value_one");
	PROMETHEUS_METRIC_SET_LABEL(&test_gauge_child_one, 1, "key_two", "value_one");
	PROMETHEUS_METRIC_SET_LABEL(&test_gauge_child_two, 0, "key_one", "value_two");
	PROMETHEUS_METRIC_SET_LABEL(&test_gauge_child_two, 1, "key_two", "value_two");
	AST_LIST_INSERT_TAIL(&test_gauge.children, &test_gauge_child_one, entry);
	AST_LIST_INSERT_TAIL(&test_gauge.children, &test_gauge_child_two, entry);
	prometheus_metric_to_string(&test_gauge, &buffer);
	ast_test_validate(test, strcmp(ast_str_buffer(buffer),
		"# HELP test_gauge A test gauge\n"
		"# TYPE test_gauge gauge\n"
		"test_gauge 0\n"
		"test_gauge{key_one=\"value_one\",key_two=\"value_one\"} 0\n"
		"test_gauge{key_one=\"value_two\",key_two=\"value_two\"} 0\n") == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(gauge_create)
{
	RAII_VAR(struct prometheus_metric *, metric, NULL, prometheus_metric_free_wrapper);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test creation (and destruction) of malloc'd gauges";
		info->description =
			"This test covers creating a gauge metric and destroying\n"
			"it. The metric should be malloc'd.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	metric = prometheus_gauge_create("test_gauge", "A test gauge");
	ast_test_validate(test, metric != NULL);
	ast_test_validate(test, metric->type == PROMETHEUS_METRIC_GAUGE);
	ast_test_validate(test, metric->allocation_strategy = PROMETHEUS_METRIC_MALLOCD);
	ast_test_validate(test, !strcmp(metric->help, "A test gauge"));
	ast_test_validate(test, !strcmp(metric->name, "test_gauge"));
	ast_test_validate(test, !strcmp(metric->value, ""));
	ast_test_validate(test, metric->children.first == NULL);
	ast_test_validate(test, metric->children.last == NULL);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(config_general_basic_auth)
{
	RAII_VAR(CURL *, curl, NULL, curl_free_wrapper);
	struct prometheus_general_config *config;
	int res;
	long response_code;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test basic auth handling";
		info->description =
			"This test covers authentication of requests";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	config = config_alloc();
	if (!config) {
		return AST_TEST_NOT_RUN;
	}
	ast_string_field_set(config, auth_username, "foo");
	ast_string_field_set(config, auth_password, "bar");
	/* Prometheus module owns the ref after this call */
	prometheus_general_config_set(config);
	ao2_ref(config, -1);

	curl = get_curl_instance();
	if (!curl) {
		return AST_TEST_NOT_RUN;
	}

	ast_test_status_update(test, "Testing without auth credentials\n");
	ast_test_status_update(test, " -> CURLing request...\n");
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_test_status_update(test, "Failed to execute CURL: %d\n", res);
		return AST_TEST_FAIL;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	ast_test_status_update(test, " -> CURL returned %ld\n", response_code);
	ast_test_validate(test, response_code == 401);

	ast_test_status_update(test, "Testing with invalid auth credentials\n");
	ast_test_status_update(test, " -> CURLing request...\n");
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERPWD, "matt:jordan");
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_test_status_update(test, "Failed to execute CURL: %d\n", res);
		return AST_TEST_FAIL;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	ast_test_status_update(test, " -> CURL returned %ld\n", response_code);
	ast_test_validate(test, response_code == 401);

	ast_test_status_update(test, "Testing with valid auth credentials\n");
	ast_test_status_update(test, " -> CURLing request...\n");
	curl_easy_setopt(curl, CURLOPT_USERPWD, "foo:bar");
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_test_status_update(test, "Failed to execute CURL: %d\n", res);
		return AST_TEST_FAIL;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	ast_test_status_update(test, " -> CURL returned %ld\n", response_code);
	ast_test_validate(test, response_code == 200);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(config_general_enabled)
{
	RAII_VAR(CURL *, curl, NULL, curl_free_wrapper);
	struct prometheus_general_config *config;
	int res;
	long response_code;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test handling of enable/disable";
		info->description =
			"When disabled, the module should return a 503.\n"
			"This test verifies that it actually occurs.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	config = config_alloc();
	if (!config) {
		return AST_TEST_NOT_RUN;
	}
	config->enabled = 0;
	/* Prometheus module owns the ref after this call */
	prometheus_general_config_set(config);
	ao2_ref(config, -1);

	curl = get_curl_instance();
	if (!curl) {
		return AST_TEST_NOT_RUN;
	}

	ast_test_status_update(test, " -> CURLing request...\n");
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_test_status_update(test, "Failed to execute CURL: %d\n", res);
		return AST_TEST_FAIL;
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	ast_test_status_update(test, " -> CURL returned %ld\n", response_code);
	ast_test_validate(test, response_code == 503);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(config_general_core_metrics)
{
	RAII_VAR(CURL *, curl, NULL, curl_free_wrapper);
	RAII_VAR(struct ast_str *, buffer, NULL, ast_free);
	struct prometheus_general_config *config;
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test producing core metrics";
		info->description =
			"This test covers the core metrics that are produced\n"
			"by the basic Prometheus module.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	buffer = ast_str_create(128);
	if (!buffer) {
		return AST_TEST_NOT_RUN;
	}

	config = config_alloc();
	if (!config) {
		return AST_TEST_NOT_RUN;
	}
	config->core_metrics_enabled = 1;
	/* Prometheus module owns the ref after this call */
	prometheus_general_config_set(config);
	ao2_ref(config, -1);

	curl = get_curl_instance();
	if (!curl) {
		return AST_TEST_NOT_RUN;
	}

	ast_test_status_update(test, " -> CURLing request...\n");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ast_test_status_update(test, "Failed to execute CURL: %d\n", res);
		return AST_TEST_FAIL;
	}
	ast_test_status_update(test, " -> Retrieved: %s\n", ast_str_buffer(buffer));

	ast_test_status_update(test, " -> Checking for core properties\n");
	ast_test_validate(test, strstr(ast_str_buffer(buffer), "asterisk_core_properties") != NULL);

	ast_test_status_update(test, " -> Checking for uptime\n");
	ast_test_validate(test, strstr(ast_str_buffer(buffer), "asterisk_core_uptime_seconds") != NULL);

	ast_test_status_update(test, " -> Checking for last reload\n");
	ast_test_validate(test, strstr(ast_str_buffer(buffer), "asterisk_core_last_reload_seconds") != NULL);

	ast_test_status_update(test, " -> Checking for scrape time\n");
	ast_test_validate(test, strstr(ast_str_buffer(buffer), "asterisk_core_scrape_time_ms") != NULL);

	return AST_TEST_PASS;
}

static int process_config(int reload)
{
	struct ast_config *config;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *bindaddr;
	const char *bindport;
	const char *prefix;
	const char *enabled;

	config = ast_config_load("http.conf", config_flags);
	if (!config || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(AST_LOG_NOTICE, "HTTP config file is invalid; declining load");
		return -1;
	} else if (config == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	enabled = ast_config_option(config, "general", "enabled");
	if (!enabled || ast_false(enabled)) {
		ast_config_destroy(config);
		ast_log(AST_LOG_NOTICE, "HTTP server is disabled; declining load");
		return -1;
	}

	/* Construct our Server URI */
	bindaddr = ast_config_option(config, "general", "bindaddr");
	if (!bindaddr) {
		ast_config_destroy(config);
		ast_log(AST_LOG_NOTICE, "HTTP config file fails to specify 'bindaddr'; declining load");
		return -1;
	}

	bindport = ast_config_option(config, "general", "bindport");
	if (!bindport) {
		bindport = "8088";
	}

	prefix = ast_config_option(config, "general", "prefix");

	snprintf(server_uri, sizeof(server_uri), "http://%s:%s%s/test_metrics", bindaddr, bindport, S_OR(prefix, ""));

	ast_config_destroy(config);

	return 0;
}

static int test_init_cb(struct ast_test_info *info, struct ast_test *test)
{
	struct prometheus_general_config *new_module_config;

	new_module_config = config_alloc();
	if (!new_module_config) {
		return -1;
	}

	module_config = prometheus_general_config_get();
	prometheus_general_config_set(new_module_config);

	/* Allow the module to own the ref */
	ao2_ref(new_module_config, -1);

	return 0;
}

static int test_cleanup_cb(struct ast_test_info *info, struct ast_test *test)
{
	prometheus_general_config_set(module_config);
	ao2_cleanup(module_config);

	return 0;
}

static int reload_module(void)
{
	return process_config(1);
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(metric_values);
	AST_TEST_UNREGISTER(metric_callback_register);
	AST_TEST_UNREGISTER(metric_register);

	AST_TEST_UNREGISTER(counter_to_string);
	AST_TEST_UNREGISTER(counter_create);
	AST_TEST_UNREGISTER(gauge_to_string);
	AST_TEST_UNREGISTER(gauge_create);

	AST_TEST_UNREGISTER(config_general_enabled);
	AST_TEST_UNREGISTER(config_general_basic_auth);
	AST_TEST_UNREGISTER(config_general_core_metrics);

	return 0;
}

static int load_module(void)
{
	if (process_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(metric_values);
	AST_TEST_REGISTER(metric_callback_register);
	AST_TEST_REGISTER(metric_register);

	AST_TEST_REGISTER(counter_to_string);
	AST_TEST_REGISTER(counter_create);
	AST_TEST_REGISTER(gauge_to_string);
	AST_TEST_REGISTER(gauge_create);

	AST_TEST_REGISTER(config_general_enabled);
	AST_TEST_REGISTER(config_general_basic_auth);
	AST_TEST_REGISTER(config_general_core_metrics);

	ast_test_register_init(CATEGORY, &test_init_cb);
	ast_test_register_cleanup(CATEGORY, &test_cleanup_cb);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Prometheus Core Unit Tests",
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.requires = "res_prometheus",
);
