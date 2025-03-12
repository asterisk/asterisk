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

struct rest_request_msg {
	char *request_type;
	char *transaction_id;
	char *request_id;
	enum ast_http_method method;
	char *uri;
	char *content_type;
	struct ast_variable *query_strings;
	struct ast_json *body;
};

static void request_destroy(struct rest_request_msg *request)
{
	if (!request) {
		return;
	}

	ast_free(request->request_type);
	ast_free(request->transaction_id);
	ast_free(request->request_id);
	ast_free(request->uri);
	ast_free(request->content_type);
	ast_variables_destroy(request->query_strings);
	ast_json_unref(request->body);

	ast_free(request);
}

#define SET_RESPONSE_AND_EXIT(_reponse_code, _reponse_text, \
	_reponse_msg, _remote_addr, _request, _request_msg) \
({ \
	RAII_VAR(char *, _msg_str, NULL, ast_json_free); \
	if (_request_msg) { \
		_msg_str = ast_json_dump_string_format(_request_msg, AST_JSON_COMPACT); \
		if (!_msg_str) { \
			response->response_code = 500; \
			response->response_text = "Server error.  Out of memory"; \
		} \
	} \
	response->message = ast_json_pack("{ s:s }", \
		"message", _reponse_msg); \
	response->response_code = _reponse_code; \
	response->response_text = _reponse_text; \
	SCOPE_EXIT_LOG_RTN_VALUE(_request, LOG_WARNING, \
		"%s: %s Request: %s\n", _remote_addr, _reponse_text, S_OR(_msg_str, "<none>")); \
})

static struct rest_request_msg *parse_rest_request_msg(
	const char *remote_addr, struct ast_json *request_msg,
	struct ast_ari_response *response, int debug_app)
{
	struct rest_request_msg *request = NULL;
	RAII_VAR(char *, body, NULL, ast_free);
	enum ast_json_nvp_ast_vars_code nvp_code;
	char *query_string_start = NULL;
	SCOPE_ENTER(4, "%s: Parsing RESTRequest message\n", remote_addr);

	response->response_code = 200;
	response->response_text = "OK";

	if (!request_msg) {
		SET_RESPONSE_AND_EXIT(500,
			"Server error","No message to parse.",
			remote_addr, request, NULL);
	}

	request = ast_calloc(1, sizeof(*request));
	if (!request) {
		SET_RESPONSE_AND_EXIT(500,
			"Server error","Out of memory",
			remote_addr, request, NULL);
	}

	/* transaction_id is optional */
	request->transaction_id = ast_strdup(
		ast_json_string_get(ast_json_object_get(
			request_msg, "transaction_id")));

	/* request_id is optional */
	request->request_id = ast_strdup(
		ast_json_string_get(ast_json_object_get(
			request_msg, "request_id")));

	request->request_type = ast_strdup(
		ast_json_string_get(ast_json_object_get(request_msg, "type")));
	if (ast_strlen_zero(request->request_type)) {
		SET_RESPONSE_AND_EXIT(400,
			"Bad request","No 'type' property.",
			remote_addr, request, request_msg);
	}

	if (!ast_strings_equal(request->request_type, "RESTRequest")) {
		SET_RESPONSE_AND_EXIT(400,
			"Bad request","Unknown request type.",
			remote_addr, request, request_msg);
	}

	request->uri = ast_strdup(
		ast_json_string_get(ast_json_object_get(request_msg, "uri")));
	if (ast_strlen_zero(request->uri)) {
		SET_RESPONSE_AND_EXIT(400,
			"Bad request","Empty or missing 'uri' property.",
			remote_addr, request, request_msg);
	}
	if ((query_string_start = strchr(request->uri, '?')))
	{
		*query_string_start = '\0';
		query_string_start++;
		request->query_strings = ast_http_parse_post_form(
			query_string_start, strlen(query_string_start), "application/x-www-form-urlencoded");
	}

	request->method = ast_get_http_method_from_string(
		ast_json_string_get(ast_json_object_get(request_msg, "method")));
	if (request->method == AST_HTTP_UNKNOWN) {
		SET_RESPONSE_AND_EXIT(400,
			"Bad request","Unknown or missing 'method' property.",
			remote_addr, request, request_msg);
	}

	/* query_strings is optional */
	nvp_code = ast_json_nvp_array_to_ast_variables(
		ast_json_object_get(request_msg, "query_strings"),
		&request->query_strings);
	if (nvp_code != AST_JSON_NVP_AST_VARS_CODE_SUCCESS &&
		nvp_code != AST_JSON_NVP_AST_VARS_CODE_NO_INPUT) {
		SET_RESPONSE_AND_EXIT(400,
			"Bad request","Unable to parse 'query_strings' array.",
			remote_addr, request, request_msg);
	}

	request->body = ast_json_null();

	body = ast_strdup(ast_json_string_get(
		ast_json_object_get(request_msg, "message_body")));

	if (ast_strlen_zero(body)) {
		SCOPE_EXIT_RTN_VALUE(request,
			"%s: Done parsing RESTRequest message.\n", remote_addr);
	}

	/* content_type is optional */
	request->content_type = ast_strdup(
		ast_json_string_get(ast_json_object_get(request_msg, "content_type")));

	if (ast_strlen_zero(request->content_type)) {
		SET_RESPONSE_AND_EXIT(400,
			"Bad request","No 'content_type' for 'message_body'.",
			remote_addr, request, request_msg);
	}

	if (ast_strings_equal(request->content_type, "application/x-www-form-urlencoded")) {
		struct ast_variable *vars = ast_http_parse_post_form(body, strlen(body),
			request->content_type);
		if (!vars) {
			SET_RESPONSE_AND_EXIT(400,
				"Bad request","Unable to parse 'message_body' as 'application/x-www-form-urlencoded'.",
				remote_addr, request, request_msg);
		}
		ast_variable_list_append(&request->query_strings, vars);
	} else if (ast_strings_equal(request->content_type, "application/json")) {
		struct ast_json_error error;
		request->body = ast_json_load_buf(body, strlen(body), &error);
		if (!request->body) {
			SET_RESPONSE_AND_EXIT(400,
				"Bad request","Unable to parse 'message_body' as 'application/json'.",
				remote_addr, request, request_msg);
		}
	} else {
		SET_RESPONSE_AND_EXIT(400,
			"Bad request","Unknown content type.",
			remote_addr, request, request_msg);
	}

	if (TRACE_ATLEAST(3) || debug_app) {
		struct ast_variable *v = request->query_strings;
		for (; v; v = v->next) {
			ast_trace(-1, "Query string: %s=%s\n", v->name, v->value);
		}
	}

	SCOPE_EXIT_RTN_VALUE(request,
		"%s: Done parsing RESTRequest message.\n", remote_addr);
}

static void send_rest_response(
	struct ari_ws_session *ari_ws_session,
	const char *remote_addr, const char *app_name,
	struct rest_request_msg *request,
	struct ast_ari_response *response, int debug_app)
{
	struct ast_json *app_resp_json = NULL;
	char *message = NULL;
	SCOPE_ENTER(4, "%s: Sending REST response %d:%s for uri %s\n",
		remote_addr, response->response_code, response->response_text,
		request ? request->uri : "N/A");

	if (response->fd >= 0) {
		close(response->fd);
		response->response_code = 406;
		response->response_text = "Not Acceptable.  Use HTTP GET";
	} else if (response->message && !ast_json_is_null(response->message)) {
		message = ast_json_dump_string_format(response->message, AST_JSON_COMPACT);
		ast_json_unref(response->message);
	}

	app_resp_json = ast_json_pack(
		"{s:s, s:s*, s:s*, s:i, s:s, s:s, s:s*, s:s* }",
		"type", "RESTResponse",
		"transaction_id", request ? S_OR(request->transaction_id, "") : "",
		"request_id", request ? S_OR(request->request_id, "") : "",
		"status_code", response->response_code,
		"reason_phrase", response->response_text,
		"uri", request ? S_OR(request->uri, "") : "",
		"content_type", message ? "application/json" : NULL,
		"message_body", message);

	ast_json_free(message);
	if (!app_resp_json || ast_json_is_null(app_resp_json)) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING,
			"%s: Failed to pack JSON response for request %s\n",
			remote_addr, request ? request->uri : "N/A");
	}

	SCOPE_CALL(-1, ari_websocket_send_event, ari_ws_session,
		app_name, app_resp_json, debug_app);

	ast_json_unref(app_resp_json);

	SCOPE_EXIT("%s: Done.  response: %d : %s\n",
		remote_addr,
		response->response_code,
		response->response_text);
}

int ari_websocket_process_request(struct ari_ws_session *ari_ws_session,
		const char *remote_addr, struct ast_variable *upgrade_headers,
		const char *app_name, struct ast_json *request_msg)
{
	int debug_app = stasis_app_get_debug_by_name(app_name);
	RAII_VAR(struct rest_request_msg *, request, NULL, request_destroy);
	struct ast_ari_response response = { .fd = -1, 0 };

	SCOPE_ENTER(3, "%s: New WebSocket Msg\n", remote_addr);

	if (TRACE_ATLEAST(3) || debug_app) {
		char *str = ast_json_dump_string_format(request_msg, AST_JSON_PRETTY);
		/* If we can't allocate a string, we can't respond to the client either. */
		if (!str) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Failed to dump JSON request\n",
				remote_addr);
		}
		ast_verbose("<--- Received ARI message from %s --->\n%s\n",
			remote_addr, str);
		ast_json_free(str);
	}

	request = SCOPE_CALL_WITH_RESULT(-1, struct rest_request_msg *,
		parse_rest_request_msg, remote_addr, request_msg, &response, debug_app);

	if (!request || response.response_code != 200) {
		SCOPE_CALL(-1, send_rest_response, ari_ws_session,
			remote_addr, app_name, request, &response, debug_app);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Done with message\n", remote_addr);
	}

	/*
	 * We don't actually use the headers in the response
	 * but we have to allocate it because ast_ari_invoke
	 * and the resource handlers expect it.
	 */
	response.headers = ast_str_create(80);
	if (!response.headers) {
		/* If we can't allocate a string, we can't respond to the client either. */
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Failed allocate headers string\n",
			remote_addr);
	}

	SCOPE_CALL(-1, ast_ari_invoke, NULL, ARI_INVOKE_SOURCE_WEBSOCKET,
		NULL, request->uri, request->method, request->query_strings,
		upgrade_headers, request->body, &response);

	ast_free(response.headers);

	if (response.no_response) {
		SCOPE_EXIT_RTN_VALUE(0, "No response needed\n");
	}

	SCOPE_CALL(-1, send_rest_response, ari_ws_session,
		remote_addr, app_name, request, &response, debug_app);

	SCOPE_EXIT_RTN_VALUE(0, "%s: Done with message\n", remote_addr);
}

