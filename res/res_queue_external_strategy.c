/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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

/*! \file
 *
 * \brief External Queue Strategy Provider
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/*** MODULEINFO
	<depend>app_queue</depend>
	<depend>res_curl</depend>
	<depend>curl</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <curl/curl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/json.h"
#include "asterisk/conversions.h"

#include "asterisk/app_queue.h"

/*** DOCUMENTATION
	<configInfo name="res_queue_external_strategy" language="en_US">
		<synopsis>External queue strategy interface</synopsis>
		<configFile name="res_queue_external_strategy.conf">
			<configObject name="general">
			</configObject>
			<configObject name="curl">
				<synopsis>HTTP endpoints to which to send POST requests</synopsis>
				<configOption name="enter_queue">
					<synopsis>HTTP endpoint to call when a call enters a queue</synopsis>
				</configOption>
				<configOption name="is_our_turn">
					<synopsis>HTTP endpoint to call to determine if it's a call's turn. Should return 2 to expire call, 1 if our turn, 0 if not our turn, or -1 to let the default algorithm decide.</synopsis>
				</configOption>
				<configOption name="calc_metric">
					<synopsis>HTTP endpoint to call to calculate agent metric. Should return positive metric, 0 to ignore agent for now, or -1 to let default algorithm compute.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

#define CONFIG_FILE "res_queue_external_strategy.conf"

static char url_enter_queue[256] = "";
static char url_is_our_turn[256] = "";
static char url_calc_metric[256] = "";

static size_t curl_write_string_callback(char *rawdata, size_t size, size_t nmemb, void *userdata)
{
	struct ast_str **buffer = userdata;
	return ast_str_append(buffer, 0, "%.*s", (int) (size * nmemb), rawdata);
}

static struct ast_str *curl_post(const char *url, const char *header, const char *data)
{
	CURL **curl;
	struct ast_str *str;
	long int http_code;
	struct curl_slist *slist = NULL;
	char curl_errbuf[CURL_ERROR_SIZE + 1] = "";

	str = ast_str_create(512);
	if (!str) {
		return NULL;
	}

	curl = curl_easy_init();
	if (!curl) {
		ast_free(str);
		return NULL;
	}

	slist = curl_slist_append(slist, header);

	curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	curl_easy_setopt(curl, CURLOPT_POST, 1L); /* CURLOPT_HEADER and CURLOPT_NOBODY are implicit */
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);

	if (curl_easy_perform(curl) != CURLE_OK) {
		curl_slist_free_all(slist);
		if (*curl_errbuf) {
			ast_log(LOG_WARNING, "%s\n", curl_errbuf);
		}
		ast_log(LOG_WARNING, "Failed to curl URL '%s'\n", url);
		curl_easy_cleanup(curl);
		ast_free(str);
		return NULL;
	}

	curl_slist_free_all(slist);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);

	if (http_code / 100 != 2) {
		ast_log(LOG_ERROR, "Failed to retrieve URL '%s': HTTP response code %ld\n", url, http_code);
		ast_free(str);
		return NULL;
	}

	ast_debug(3, "Response: %s\n", ast_str_buffer(str));
	return str;
}

static struct ast_json *build_base_json(struct ast_queue_caller_info *caller)
{
	struct ast_json *root;

	ast_channel_lock(caller->chan);
	root = ast_json_pack(
		"{s: s, s: s, s: s, s: s, s: s, s: i, s: i, s: i, s: i}",
		"channel", ast_channel_name(caller->chan),
		"uniqueid", ast_channel_uniqueid(caller->chan),
		"context", ast_channel_context(caller->chan),
		"queue_name", caller->queue_name,
		"digits", S_OR(caller->digits, ""),
		"prio", caller->prio,
		"pos", caller->pos,
		"start", caller->start,
		"expire", caller->expire);
	ast_channel_unlock(caller->chan);

	return root;
}

static char *build_base_str(struct ast_queue_caller_info *caller)
{
	char *str = NULL;
	struct ast_json *root = build_base_json(caller);
	if (root) {
		str = ast_json_dump_string(root);
		ast_json_unref(root);
	}
	return str;
}

static struct ast_json *build_calc_metric_json(struct ast_queue_caller_info *caller, struct ast_queue_agent_info *agent)
{
	struct ast_json *root;

	ast_channel_lock(caller->chan);
	root = ast_json_pack(
		"{s: s, s: s, s: s, s: s, s: s, s: i, s: i, s: i, s: i,"
		", s: s, s: s, s: s, s: i, s: i, s: i, s: i, s: b, s: b, s: b}",
		"channel", ast_channel_name(caller->chan),
		"uniqueid", ast_channel_uniqueid(caller->chan),
		"context", ast_channel_context(caller->chan),
		"queue_name", caller->queue_name,
		"digits", S_OR(caller->digits, ""),
		"prio", caller->prio,
		"pos", caller->pos,
		"start", caller->start,
		"expire", caller->expire,
		/* Agent */
		"interface", agent->interface,
		"state_interface", agent->state_interface,
		"member_name", agent->member_name,
		"queuepos", agent->queuepos,
		"penalty", agent->penalty,
		"calls", agent->calls,
		"status", agent->status,
		"paused", agent->paused,
		"dynamic", agent->dynamic,
		"available", agent->available);
	ast_channel_unlock(caller->chan);

	return root;
}

static char *build_calc_metric_str(struct ast_queue_caller_info *caller, struct ast_queue_agent_info *agent)
{
	char *str = NULL;
	struct ast_json *root = build_calc_metric_json(caller, agent);
	if (root) {
		str = ast_json_dump_string(root);
		ast_json_unref(root);
	}
	return str;
}

static int return_result(const char *url, char *data, int expect_response)
{
	struct ast_str *str;

	if (!data) {
		return -1;
	}

	ast_debug(7, "CURL POST %s: %s", url, data);
	str = curl_post(url, "Content-Type: application/json", data);
	ast_json_free(data);

	if (!expect_response) {
		ast_free(str);
		return -1;
	}

	if (str) {
		int result;
		int res = ast_str_to_int(ast_str_buffer(str), &result);
		if (res) {
			ast_log(LOG_WARNING, "Endpoint did not return numeric response ('%s')\n", ast_str_buffer(str));
			ast_free(str);
			return -1; /* Not valid int, so fall back to defaults */
		}
		ast_free(str);
		return result;
	}

	return -1;
}

static void curlstrat_enter_queue(struct ast_queue_caller_info *caller)
{
	ast_autoservice_start(caller->chan);
	if (!ast_strlen_zero(url_enter_queue)) {
		return_result(url_enter_queue, build_base_str(caller), 0);
	}
	ast_autoservice_stop(caller->chan);
}

static int curlstrat_is_our_turn(struct ast_queue_caller_info *caller)
{
	int res;

	if (ast_strlen_zero(url_is_our_turn)) {
		return -1;
	}

	ast_autoservice_start(caller->chan);
	res = return_result(url_is_our_turn, build_base_str(caller), 1);
	ast_autoservice_stop(caller->chan);

	return res;
}

static int curlstrat_calc_metric(struct ast_queue_caller_info *caller, struct ast_queue_agent_info *agent)
{
	int res;

	if (agent->paused) {
		/* If the agent is paused, then obviously not available */
		return 0;
	}

	if (ast_strlen_zero(url_calc_metric)) {
		return -1;
	}

	ast_autoservice_start(caller->chan);
	res = return_result(url_calc_metric, build_calc_metric_str(caller, agent), 1);
	ast_autoservice_stop(caller->chan);

	return res;
}

static int load_config(int reload)
{
	const char *cat = NULL;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_variable *var;

	if (!(cfg = ast_config_load(CONFIG_FILE, config_flags))) {
		ast_log(LOG_WARNING, "Config file %s not found, declining to load\n", CONFIG_FILE);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(1, "Config file %s unchanged, skipping\n", CONFIG_FILE);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format. Aborting.\n", CONFIG_FILE);
		return -1;
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			continue;
		}
		if (strcasecmp(cat, "curl")) {
			ast_log(LOG_WARNING, "Invalid config section: %s\n", cat);
			continue;
		}
		/* curl strategy */
		var = ast_variable_browse(cfg, cat);
		while (var) {
			if (!strcasecmp(var->name, "url_enter_queue") && !ast_strlen_zero(var->value)) {
				ast_copy_string(url_enter_queue, var->value, sizeof(url_enter_queue));
			} else if (!strcasecmp(var->name, "url_is_our_turn") && !ast_strlen_zero(var->value)) {
				ast_copy_string(url_is_our_turn, var->value, sizeof(url_is_our_turn));
			} else if (!strcasecmp(var->name, "url_calc_metric") && !ast_strlen_zero(var->value)) {
				ast_copy_string(url_calc_metric, var->value, sizeof(url_calc_metric));
			} else {
				ast_log(LOG_WARNING, "Unknown setting at line %d: '%s'\n", var->lineno, var->name);
			}
			var = var->next;
		}
	}

	ast_config_destroy(cfg);
	return 0;
}

struct ast_queue_strategy_callbacks curlstrat_callbacks = {
	.enter_queue = curlstrat_enter_queue,
	.is_our_turn = curlstrat_is_our_turn,
	.calc_metric = curlstrat_calc_metric,
};

static int unload_module(void)
{
	/* We must decline to unload if ast_queue_unregister_external_strategy_provider returns nonzero */
	return ast_queue_unregister_external_strategy_provider(&curlstrat_callbacks);
}

static int load_module(void)
{
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_queue_register_external_strategy_provider(&curlstrat_callbacks, "curl")) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "External Queue Strategy Provider",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "app_queue,res_curl",
);
