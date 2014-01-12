/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
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

/*! \file
 *
 * \brief /api-docs/endpoints.{format} implementation- Endpoint resources
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "resource_endpoints.h"

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/channel.h"

void ast_ari_endpoints_list(struct ast_variable *headers,
	struct ast_ari_endpoints_list_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, snapshots, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	void *obj;

	cache = ast_endpoint_cache();
	if (!cache) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Message bus not initialized");
		return;
	}
	ao2_ref(cache, +1);

	snapshots = stasis_cache_dump(cache, ast_endpoint_snapshot_type());
	if (!snapshots) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	json = ast_json_array_create();
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	i = ao2_iterator_init(snapshots, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, msg, obj, ao2_cleanup);
		struct ast_endpoint_snapshot *snapshot = stasis_message_data(msg);
		struct ast_json *json_endpoint = ast_endpoint_snapshot_to_json(snapshot, stasis_app_get_sanitizer());
		int r;

		if (!json_endpoint) {
			ao2_iterator_destroy(&i);
			return;
		}

		r = ast_json_array_append(
			json, json_endpoint);
		if (r != 0) {
			ao2_iterator_destroy(&i);
			ast_ari_response_alloc_failed(response);
			return;
		}
	}
	ao2_iterator_destroy(&i);

	ast_ari_response_ok(response, ast_json_ref(json));
}
void ast_ari_endpoints_list_by_tech(struct ast_variable *headers,
	struct ast_ari_endpoints_list_by_tech_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_cache *, cache, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, snapshots, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ao2_iterator i;
	void *obj;

	if (!ast_get_channel_tech(args->tech)) {
		ast_ari_response_error(response, 404, "Not Found",
				       "No Endpoints found - invalid tech %s", args->tech);
		return;
	}

	cache = ast_endpoint_cache();
	if (!cache) {
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Message bus not initialized");
		return;
	}
	ao2_ref(cache, +1);

	snapshots = stasis_cache_dump(cache, ast_endpoint_snapshot_type());
	if (!snapshots) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	json = ast_json_array_create();
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	i = ao2_iterator_init(snapshots, 0);
	while ((obj = ao2_iterator_next(&i))) {
		RAII_VAR(struct stasis_message *, msg, obj, ao2_cleanup);
		struct ast_endpoint_snapshot *snapshot = stasis_message_data(msg);
		struct ast_json *json_endpoint;
		int r;

		if (strcasecmp(args->tech, snapshot->tech) != 0) {
			continue;
		}

		json_endpoint = ast_endpoint_snapshot_to_json(snapshot, stasis_app_get_sanitizer());
		if (!json_endpoint) {
			continue;
		}

		r = ast_json_array_append(
			json, json_endpoint);
		if (r != 0) {
			ao2_iterator_destroy(&i);
			ast_ari_response_alloc_failed(response);
			return;
		}
	}
	ao2_iterator_destroy(&i);
	ast_ari_response_ok(response, ast_json_ref(json));
}
void ast_ari_endpoints_get(struct ast_variable *headers,
	struct ast_ari_endpoints_get_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;
	RAII_VAR(struct ast_endpoint_snapshot *, snapshot, NULL, ao2_cleanup);

	snapshot = ast_endpoint_latest_snapshot(args->tech, args->resource);
	if (!snapshot) {
		ast_ari_response_error(response, 404, "Not Found",
			"Endpoint not found");
		return;
	}

	json = ast_endpoint_snapshot_to_json(snapshot, stasis_app_get_sanitizer());
	if (!json) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, json);
}
