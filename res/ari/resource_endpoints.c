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

#include "resource_endpoints.h"

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/channel.h"
#include "asterisk/message.h"

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

		if (!json_endpoint || ast_json_array_append(json, json_endpoint)) {
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
	struct ast_endpoint *tech_endpoint;
	struct ao2_iterator i;
	void *obj;

	tech_endpoint = ast_endpoint_find_by_id(args->tech);
	if (!tech_endpoint) {
		ast_ari_response_error(response, 404, "Not Found",
				       "No Endpoints found - invalid tech %s", args->tech);
		return;
	}
	ao2_ref(tech_endpoint, -1);

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

static void send_message(const char *to, const char *from, const char *body, struct ast_variable *variables, struct ast_ari_response *response)
{
	struct ast_variable *current;
	struct ast_msg *msg;
	int res = 0;

	if (ast_strlen_zero(to)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"To must be specified");
		return;
	}

	msg = ast_msg_alloc();
	if (!msg) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	res |= ast_msg_set_from(msg, "%s", from);
	res |= ast_msg_set_to(msg, "%s", to);

	if (!ast_strlen_zero(body)) {
		res |= ast_msg_set_body(msg, "%s", body);
	}

	for (current = variables; current; current = current->next) {
		res |= ast_msg_set_var_outbound(msg, current->name, current->value);
	}

	if (res) {
		ast_ari_response_alloc_failed(response);
		ast_msg_destroy(msg);
		return;
	}

	if (ast_msg_send(msg, to, from)) {
		ast_ari_response_error(response, 404, "Not Found",
			"Endpoint not found");
	}

	response->message = ast_json_null();
	response->response_code = 202;
	response->response_text = "Accepted";
}

/*!
 * \internal
 * \brief Convert a \c ast_json list of key/value pair tuples into a \c ast_variable list
 * \since 13.3.0
 *
 * \param[out] response HTTP response if error
 * \param json_variables The JSON blob containing the variable
 * \param[out] variables An out reference to the variables to populate.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int json_to_ast_variables(struct ast_ari_response *response, struct ast_json *json_variables, struct ast_variable **variables)
{
	enum ast_json_to_ast_vars_code res;

	res = ast_json_to_ast_variables(json_variables, variables);
	switch (res) {
	case AST_JSON_TO_AST_VARS_CODE_SUCCESS:
		return 0;
	case AST_JSON_TO_AST_VARS_CODE_INVALID_TYPE:
		ast_ari_response_error(response, 400, "Bad Request",
			"Only string values in the 'variables' object allowed");
		break;
	case AST_JSON_TO_AST_VARS_CODE_OOM:
		ast_ari_response_alloc_failed(response);
		break;
	}
	ast_log(AST_LOG_ERROR, "Unable to convert 'variables' in JSON body to Asterisk variables\n");

	return -1;
}

void ast_ari_endpoints_send_message(struct ast_variable *headers,
	struct ast_ari_endpoints_send_message_args *args,
	struct ast_ari_response *response)
{
	struct ast_variable *variables = NULL;

	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_endpoints_send_message_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables
			&& json_to_ast_variables(response, json_variables, &variables)) {
			return;
		}
	}

	send_message(args->to, args->from, args->body, variables, response);
	ast_variables_destroy(variables);
}

void ast_ari_endpoints_send_message_to_endpoint(struct ast_variable *headers,
	struct ast_ari_endpoints_send_message_to_endpoint_args *args,
	struct ast_ari_response *response)
{
	struct ast_variable *variables = NULL;
	struct ast_endpoint_snapshot *snapshot;
	char msg_to[128];
	char *tech = ast_strdupa(args->tech);

	/* Really, we just want to know if this thing exists */
	snapshot = ast_endpoint_latest_snapshot(args->tech, args->resource);
	if (!snapshot) {
		ast_ari_response_error(response, 404, "Not Found",
			"Endpoint not found");
		return;
	}
	ao2_ref(snapshot, -1);

	if (args->variables) {
		struct ast_json *json_variables;

		ast_ari_endpoints_send_message_to_endpoint_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
		if (json_variables
			&& json_to_ast_variables(response, json_variables, &variables)) {
			return;
		}
	}

	snprintf(msg_to, sizeof(msg_to), "%s:%s", ast_str_to_lower(tech), args->resource);

	send_message(msg_to, args->from, args->body, variables, response);
	ast_variables_destroy(variables);
}
