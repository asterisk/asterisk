/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#include "asterisk.h"

#include "ari_websockets.h"
#include "asterisk/ari.h"
#include "asterisk/json.h"
#include "asterisk/stasis_app.h"

struct rest_request {
	char *uuid;
	enum ast_http_method method;
	char *path;
	char *content_type;
	struct ast_variable *headers;
	struct ast_variable *get_params;
	struct ast_json *body;
};

struct rest_request_msg {
	char *identity;
	size_t request_count;
	struct rest_request *requests;
};

static void rest_request_msg_destroy(struct rest_request_msg *msg)
{
	int i = 0;
	if (!msg) {
		return;
	}

	ast_free(msg->identity);
	for (i = 0; i < msg->request_count; i++) {
		ast_free(msg->requests[i].uuid);
		ast_free(msg->requests[i].path);
		ast_free(msg->requests[i].content_type);
		ast_variables_destroy(msg->requests[i].headers);
		ast_variables_destroy(msg->requests[i].get_params);
		ast_json_unref(msg->requests[i].body);
	}
	ast_free(msg->requests);

	ast_free(msg);
}

static struct rest_request_msg *parse_rest_request_msg(
	const char *remote_addr, struct ast_json *msg,
	struct ast_ari_response *response)
{
	RAII_VAR(struct rest_request_msg *, request_msg,
		ast_calloc(1, sizeof(*request_msg)), rest_request_msg_destroy);
	RAII_VAR(char *, msg_str, NULL, ast_json_free);
	struct rest_request_msg *rtn_request_msg = NULL;
	struct ast_json *requests_array = NULL;
	int i = 0;
	int res = -1;
	SCOPE_ENTER(4, "%s: Parsing RESTRequest message\n", remote_addr);

	if (!msg) {
		response->response_code = 500;
		response->response_text = "Server error.  No message to parse";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: No message to parse\n", remote_addr);
	}

	if (!request_msg) {
		response->response_code = 500;
		response->response_text = "Server error.  Out of memory";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: Unable to allocate request_msg\n", remote_addr);
	}

	msg_str = ast_json_dump_string_format(msg, ast_ari_json_format());
	if (!msg) {
		response->response_code = 500;
		response->response_text = "Server error.  Out of memory";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: Unable to dump request msg\n", remote_addr);
	}

	request_msg->identity = ast_strdup(
		ast_json_string_get(ast_json_object_get(msg, "identity")));
	if (!request_msg->identity) {
		response->response_code = 400;
		response->response_text = "Bad request.  No 'identity' property.";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: Failed to get 'identity' from msg %s\n",
			remote_addr, msg_str);
	}

	requests_array = ast_json_object_get(msg, "requests");
	if (!requests_array) {
		response->response_code = 400;
		response->response_text = "Bad request.  No 'requests' array.";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: Failed to get 'requests' array from msg %s\n",
			remote_addr, msg_str);
	}
	if (!ast_json_is_array(requests_array)) {
		response->response_code = 400;
		response->response_text = "Bad request.  'requests' must be an array.";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: 'requests' must be an array in msg %s\n",
			remote_addr, msg_str);
	}
	request_msg->request_count = ast_json_array_size(requests_array);
	if (request_msg->request_count <= 0) {
		response->response_code = 400;
		response->response_text = "Bad request.  'requests' array is empty.";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: 'requests' can't be an empty array in msg %s\n",
			remote_addr, msg_str);
	}

	request_msg->requests = ast_calloc(request_msg->request_count,
		sizeof(*request_msg->requests));
	if (!request_msg->requests) {
		response->response_code = 500;
		response->response_text = "Server error.  Out of memory";
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING,
			"%s: Unable to allocate requests array\n",
			remote_addr);
	}

	for (i = 0; i < request_msg->request_count; i++) {
		struct ast_json *request = ast_json_array_get(requests_array, i);
		RAII_VAR(char *, body, NULL, ast_free);
		enum ast_json_nvp_ast_vars_code nvp_code;
		SCOPE_ENTER(4, "%s: Parsing request %d\n", remote_addr, i);

		res = -1;
		if (!request) {
			response->response_code = 500;
			response->response_text = "Server error.  Failed to get request from array.";
			SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
				"%s: Failed to get request %d from msg %s\n",
				remote_addr, i, msg_str);
		}

		request_msg->requests[i].uuid = ast_strdup(
			ast_json_string_get(ast_json_object_get(request, "uuid")));
		if (!request_msg->requests[i].uuid) {
			response->response_code = 400;
			response->response_text = "Bad request.  No 'uuid' property.";
			SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
				"%s: Failed to get 'uuid' from request %d in msg %s\n",
				remote_addr, i, msg_str);
		}

		request_msg->requests[i].method = ast_get_http_method_from_string(
			ast_json_string_get(ast_json_object_get(request, "method")));
		if (request_msg->requests[i].method == AST_HTTP_UNKNOWN) {
			response->response_code = 400;
			response->response_text = "Bad request.  Unknown or missing 'method' property.";
			SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
				"%s: Failed to get 'method' from request %d in msg %s\n",
				remote_addr, i, msg_str);
		}

		request_msg->requests[i].path = ast_strdup(
			ast_json_string_get(ast_json_object_get(request, "path")));
		if (!request_msg->requests[i].path) {
			response->response_code = 400;
			response->response_text = "Bad request.  No 'path' property.";
			SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
				"%s: Failed to get 'path' from request %d in msg %s\n",
				remote_addr, i, msg_str);
		}

		request_msg->requests[i].content_type = ast_strdup(
			ast_json_string_get(ast_json_object_get(request, "content_type")));

		nvp_code = ast_json_nvp_array_to_ast_variables(
			ast_json_object_get(request, "headers"),
			&request_msg->requests[i].headers);
		if (nvp_code != AST_JSON_NVP_AST_VARS_CODE_SUCCESS &&
			nvp_code != AST_JSON_NVP_AST_VARS_CODE_NO_INPUT) {
			response->response_code = 400;
			response->response_text = "Bad request.  Unable to parse 'headers' array.";
			SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
				"%s: Failed to parse 'headers' from request %d in msg %s\n",
				remote_addr, i, msg_str);
		}

		if (ast_strlen_zero(request_msg->requests[i].content_type)) {
			const char *ct_header = ast_variable_find_in_list(
				request_msg->requests[i].headers, "Content-Type");
			if (ct_header) {
				request_msg->requests[i].content_type = ast_strdup(ct_header);
				if (!request_msg->requests[i].content_type) {
					response->response_code = 400;
					response->response_text = "Bad request.  Unable to parse 'Content-Type' header.";
					SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
						"%s: Failed to get 'Content-Type' from headers in request %d in msg %s\n",
						remote_addr, i, msg_str);
				}
			}
		}

		nvp_code = ast_json_nvp_array_to_ast_variables(ast_json_object_get(request, "query_strings"),
			&request_msg->requests[i].get_params);
		if (nvp_code != AST_JSON_NVP_AST_VARS_CODE_SUCCESS &&
			nvp_code != AST_JSON_NVP_AST_VARS_CODE_NO_INPUT) {
			response->response_code = 400;
			response->response_text = "Bad request.  Unable to parse 'query_strings' array.";
			SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
				"%s: Failed to parse 'query_strings' from request %d in msg %s\n",
				remote_addr, i, msg_str);
		}

		body = ast_strdup(ast_json_string_get(ast_json_object_get(request, "message_body")));
		if (!ast_strlen_zero(body)) {
			if (request_msg->requests[i].content_type) {
				if (ast_strings_equal(request_msg->requests[i].content_type, "application/x-www-form-urlencoded")) {
					struct ast_variable *vars = ast_http_parse_post_form(body, strlen(body),
						request_msg->requests[i].content_type);

					if (!vars) {
						response->response_code = 400;
						response->response_text = "Bad request.  Unable to parse 'message_body' as 'application/x-www-form-urlencoded'.";
						SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
							"%s: Failed to parse 'body' from request %d as 'application/x-www-form-urlencoded' in msg %s\n",
							remote_addr, i, msg_str);
					}
					ast_variable_list_append(&request_msg->requests[i].get_params, vars);
				} else if (ast_strings_equal(request_msg->requests[i].content_type, "application/json")) {
					struct ast_json_error error;
					request_msg->requests[i].body = ast_json_load_buf(
						body, strlen(body), &error);
					if (!request_msg->requests[i].body) {
						response->response_code = 400;
						response->response_text = "Bad request.  Unable to parse 'message_body' as 'application/json'.";
						SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
							"%s: Failed to parse 'message_body' from request %d as JSON (%s) in msg %s\n",
							remote_addr, i, error.text, msg_str);
					}
				} else {
					response->response_code = 400;
					response->response_text = "Bad request.  Unknown content type.";
					SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
						"%s: Failed to parse 'message_body' from request %d. Unexpected content type '%s' in msg %s\n",
						remote_addr, i, request_msg->requests[i].content_type, msg_str);
				}
			} else {
				response->response_code = 400;
				response->response_text = "Bad request.  No 'content_type' for message body.";
				SCOPE_EXIT_LOG_EXPR(break, LOG_WARNING,
					"%s: Failed to parse 'message_body' from request %d. No content type in msg %s\n",
					remote_addr, i, msg_str);
			}
		}
		res = 0;
		SCOPE_EXIT("%s: Done parsing request %d\n", remote_addr, i);
	}

	if (res == 0) {
		/* Don't let RAII destroy the good request_msg */
		rtn_request_msg = request_msg;
		request_msg = NULL;
	}
	SCOPE_EXIT_RTN_VALUE(rtn_request_msg,
		"%s: Done parsing RESTRequest message. RC: %d\n", remote_addr, res);
}

static void send_status_response(
	struct ari_ws_session *ari_ws_session,
	const char *remote_addr,
	const char *identity, const char *app_name, char *path,
	int status_code, const char *reason_phrase, int debug_app)
{
	struct ast_json *app_resp_json = NULL;
	SCOPE_ENTER(4, "%s: Sending status response %d:%s for path %s\n",
		remote_addr, status_code, reason_phrase, S_OR(path, "N/A"));

	app_resp_json = ast_json_pack("{s: s, s: s, s: {s:i, s:s}}",
		"type", "RESTStatusResponse",
		"identity", identity,
		"status", "status_code", status_code, "reason_phrase", reason_phrase);

	if (!app_resp_json || ast_json_is_null(app_resp_json)) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Failed to pack JSON response for request %s\n",
			remote_addr, S_OR(path, "N/A"));
	}

	SCOPE_CALL(-1, ari_websocket_send_event, ari_ws_session, app_name,
		app_resp_json, debug_app);

	ast_json_unref(app_resp_json);
	SCOPE_EXIT("%s: Done.  response: %d : %s\n",
		remote_addr,
		status_code,
		reason_phrase);
}

static struct ast_json* get_headers_as_json_array(
	struct ast_str *headers)
{
	struct ast_json *json_headers = NULL;
	char *buffer = NULL;
	char *token = NULL;

	if (!headers) {
		return NULL;
	}

	json_headers = ast_json_array_create();
	if (!json_headers) {
		return NULL;
	}

	buffer = ast_strdupa(ast_str_buffer(headers));
	while((token = ast_strsep(&buffer, '\n', AST_STRSEP_ALL))) {
		char *name = NULL;
		char *value = NULL;
		char *header = ast_strdupa(token);
		name = ast_strsep(&header, ':', AST_STRSEP_ALL);
		value = ast_strsep(&header, ':', AST_STRSEP_ALL);
		if (name && value) {
			struct ast_json *obj = ast_json_pack("{s: s, s: s}", "name", name, "value", value);
			if (!obj) {
				ast_json_unref(json_headers);
				return NULL;
			}
			if (ast_json_array_append(json_headers, obj)) {
				ast_json_unref(obj);
				ast_json_unref(json_headers);
				return NULL;
			}
		}
	}

	if (ast_json_array_size(json_headers) == 0) {
		ast_json_unref(json_headers);
		json_headers = NULL;
	}

	return json_headers;
}

static void send_rest_response(
	struct ari_ws_session *ari_ws_session,
	const char *remote_addr, const char *app_name,
	struct rest_request_msg *request_msg,
	struct ast_ari_response *response, int debug_app)
{
	struct ast_json *app_resp_json = NULL;
	char *message = NULL;
	SCOPE_ENTER(4, "%s: Sending REST response %d:%s for path %s\n",
		remote_addr, response->response_code, response->response_text,
		request_msg ? request_msg->requests[0].path : "N/A");

	if (response->fd >= 0) {
		close(response->fd);
		response->response_code = 406;
		response->response_text = "Not Acceptable.  Use HTTP GET";
		ast_free(response->headers);
		response->headers = NULL;
	} else if (response->message && !ast_json_is_null(response->message)) {
		message = ast_json_dump_string_format(response->message, AST_JSON_COMPACT);
		ast_str_append(&response->headers, 0, "Content-type: application/json\r\n");
		ast_json_unref(response->message);
	}

	app_resp_json = ast_json_pack(
		"{s:s, s:s, s:[ { s:s, s:i, s:s, s:s, s:o*, s:s* } ] }",
		"type", "RESTResponseMsg",
		"identity", ari_ws_session->session_id,
		"responses",
			"uuid", request_msg ? request_msg->requests[0].uuid : "N/A",
			"status_code", response->response_code,
			"reason_phrase", response->response_text,
			"path", request_msg ? request_msg->requests[0].path : "N/A",
			"headers", get_headers_as_json_array(response->headers),
			"message_body", message);

	ast_json_free(message);
	if (!app_resp_json || ast_json_is_null(app_resp_json)) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Failed to pack JSON response for request %s\n",
			remote_addr, request_msg ? request_msg->requests[0].path : "N/A");
	}

	SCOPE_CALL(-1, ari_websocket_send_event, ari_ws_session,
		app_name, app_resp_json, debug_app);

	ast_json_unref(app_resp_json);

	SCOPE_EXIT("%s: Done.  response: %d : %s\n",
		remote_addr,
		response->response_code,
		response->response_text);
}

static int process_handshake_request(
	struct ari_ws_session *ari_ws_session,
	const char *remote_addr, struct ast_json *msg, const char *app_name, int debug_app)
{
	const char *protocol_version = NULL;
	const char *protocol_name = NULL;
	SCOPE_ENTER(3, "%s: Processing handshake request for %s\n",
		remote_addr, app_name);

	protocol_version = ast_json_string_get(ast_json_object_get(msg, "protocol_version"));
	if (ast_strlen_zero(protocol_version)) {
		send_status_response(ari_ws_session, remote_addr, ari_ws_session->session_id,
			app_name, NULL, 400, "Bad request. Missing protocol_version field in message", 0);
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING,
			"%s: Missing 'protocol_version' field in JSON request.\n",
			remote_addr);
	}

	protocol_name = ast_json_string_get(ast_json_object_get(msg, "protocol_name"));
	if (ast_strlen_zero(protocol_name)) {
		send_status_response(ari_ws_session, remote_addr, ari_ws_session->session_id,
			app_name, NULL, 400, "Bad request. Missing protocol_name field in message", 0);
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING,
			"%s: Missing 'protocol_name' field in JSON request.\n",
			remote_addr);
	}

	send_status_response(ari_ws_session, remote_addr, ari_ws_session->session_id,
		app_name, NULL, 200, "OK", debug_app);

	SCOPE_EXIT_RTN_VALUE(0, "%s: Sent handshake response\n", remote_addr);
}

static int process_rest_request(
	struct ari_ws_session *ari_ws_session, const char *remote_addr,
	struct ast_json *msg, const char *app_name,
	struct ast_variable *headers, int debug_app)
{
	struct rest_request_msg *request_msg;
	struct ast_ari_response response = { .fd = -1, 0 };

	SCOPE_ENTER(3, "%s: Handling REST request\n", remote_addr);

	response.headers = ast_str_create(40);
	if (!response.headers) {
		/* If we can't allocate 40 bytes we won't be able to even respond. */
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "Out of memory!\n");
	}

	request_msg = SCOPE_CALL_WITH_RESULT(-1, struct rest_request_msg *,
		parse_rest_request_msg, remote_addr, msg, &response);

	if (!request_msg) {
		SCOPE_CALL(-1, send_rest_response, ari_ws_session,
			remote_addr, app_name, request_msg, &response, debug_app);
		ast_free(response.headers);
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING,
			"%s: Failed to parse RESTRequest message\n",
			remote_addr);
	}

	SCOPE_CALL(-1, ast_ari_invoke, NULL, ARI_INVOKE_SOURCE_WEBSOCKET, NULL,
		request_msg->requests[0].path,
		request_msg->requests[0].method, request_msg->requests[0].get_params,
		headers, request_msg->requests[0].body, &response);

	if (response.no_response) {
		ast_free(response.headers);
		SCOPE_EXIT_RTN_VALUE(0, "No response needed\n");
	}

	SCOPE_CALL(-1, send_rest_response, ari_ws_session,
		remote_addr, app_name, request_msg, &response, debug_app);

	rest_request_msg_destroy(request_msg);

	ast_free(response.headers);
	SCOPE_EXIT_RTN_VALUE(0, "Done.  response: %d : %s\n",
		response.response_code,
		response.response_text);
}

void ari_websocket_process_request(
		struct ari_ws_session *ari_ws_session,
		const char *remote_addr,
		struct ast_variable *upgrade_headers,
		const char *app_name,
		struct ast_json *msg)
{
	const char *msg_type = NULL;
	int debug_app = stasis_app_get_debug_by_name(app_name);

	SCOPE_ENTER(3, "%s: New WebSocket Msg\n", remote_addr);

	if (TRACE_ATLEAST(3) || debug_app) {
		char *str = ast_json_dump_string_format(msg, AST_JSON_PRETTY);
		/* If we can't even allocate a string, we can't even respond to the client. */
		if (!str) {
			SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Failed to dump JSON request\n",
				remote_addr);
		}
		ast_verbose("<--- Received ARI message from %s --->\n%s\n",
			remote_addr, str);
		ast_json_free(str);
	}

	msg_type = ast_json_string_get(ast_json_object_get(msg, "type"));
	if (ast_strlen_zero(msg_type)) {
		send_status_response(ari_ws_session, remote_addr,
			ari_ws_session->session_id,
			app_name, NULL, 400, "Bad request. Missing type field in message", debug_app);
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Missing 'type' field in JSON request.\n",
			remote_addr);
	}
	ast_trace(-1, "%s: Message type: %s\n", remote_addr, msg_type);

	if (ast_strings_equal(msg_type, "RESTHandshakeRequest")) {
		SCOPE_CALL(-1, process_handshake_request, ari_ws_session,
			remote_addr, msg, app_name, debug_app);
		SCOPE_EXIT_RTN("%s: Done with handshake\n", remote_addr);
	}

	if (!ast_strings_equal(msg_type, "RESTRequest")) {
		send_status_response(ari_ws_session, remote_addr,
			ari_ws_session->session_id,
			app_name, NULL, 404, "Unknown message type", debug_app);
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Received unknown ARI Websocket message type '%s'\n",
			remote_addr, msg_type);
	}

	SCOPE_CALL(-1, process_rest_request, ari_ws_session, remote_addr,
		msg, app_name, upgrade_headers, debug_app);

	SCOPE_EXIT("%s: Done with message\n", remote_addr);
}

