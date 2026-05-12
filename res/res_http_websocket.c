/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief WebSocket support for the Asterisk internal HTTP server
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/http.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/file.h"
#include "asterisk/sched.h"
#include "asterisk/unaligned.h"
#include "asterisk/uri.h"
#include "asterisk/uuid.h"

#define AST_API_MODULE
#include "res_http_websocket/websocket_private.h"


static struct ast_sched_context *ping_scheduler;

static struct client_vtbl client_vtbls[backend_last] = { { 0,}, };

static enum ws_client_backend client_backend = backend_tcptls;

const char *ast_websocket_type_to_str(enum ast_websocket_type type)
{
	switch (type) {
	case AST_WS_TYPE_CLIENT_PERSISTENT:
		return "persistent";
	case AST_WS_TYPE_CLIENT_PER_CALL:
		return "per_call";
	case AST_WS_TYPE_CLIENT_PER_CALL_CONFIG:
		return "per_call_config";
	case AST_WS_TYPE_CLIENT:
		return "client";
	case AST_WS_TYPE_INBOUND:
		return "inbound";
	case AST_WS_TYPE_SERVER:
		return "server";
	case AST_WS_TYPE_ANY:
		return "any";
	default:
		return "unknown";
	}
}

/*! \brief Destructor function for sessions */
void session_destroy_fn(void *obj)
{
	struct ast_websocket *session = obj;
	char *id = ast_strdupa(WS_SESSION_REMOTE(session));
	SCOPE_ENTER(2, "%s: Session destructor %p\n", id, session);

	ast_ws_close(session, session->close_status_code, 0, 0);

	ao2_cleanup(session->client);
	ast_free(session->payload);
	SCOPE_EXIT_RTN("%s: Session destructor complete %p\n", id, session);
}

int ast_ws_handled_pong_or_close(struct ast_websocket *session, char *payload, uint64_t payload_len,
	enum ast_websocket_opcode opcode)
{
	if (opcode == AST_WEBSOCKET_OPCODE_PONG) {
		/*
		 * If it's from our own PING, reset the missed count.
		 */
		if (session->missed_pong_count
			&& payload_len == WS_PING_PAYLOAD_LEN
			&& strncmp(payload, WS_PING_PAYLOAD, payload_len) == 0) {
			ast_debug(4, "%s: Received PONG from our own PING. Missed count was: %d.  Cleared.\n", WS_SESSION_REMOTE(session),
				session->missed_pong_count);
			session->missed_pong_count = 0;
			return 1;
		}
	}

	if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
		if (payload_len >= 2) {
			session->close_status_code = ntohs(get_unaligned_uint16(payload));
		}
		if (!session->closing) {
			ast_debug(2, "%s: Received CLOSE opcode reason: %d\n", WS_SESSION_REMOTE(session),
				session->close_status_code);
			ast_ws_close(session, session->close_status_code, 1, 0);
		} else {
			ast_debug(2, "%s: Received CLOSE opcode reason: %d (response to our close)\n",
				WS_SESSION_REMOTE(session), session->close_status_code);
			ast_ws_close(session, session->close_status_code, 0, 1);
		}
		return 1;
	}

	return 0;
}

void ping_scheduler_cancel(struct ast_websocket *session)
{
	SCOPE_ENTER(2, "%s: Stopping PING/PONG keepalives\n", WS_SESSION_REMOTE(session));
	if (session->ping_sched_timer >= 0) {
		AST_SCHED_DEL(ping_scheduler, session->ping_sched_timer);
		ao2_ref(session, -1);
		SCOPE_EXIT_RTN("%s: Stopped PING/PONG keepalives\n", WS_SESSION_REMOTE(session));
	}
	SCOPE_EXIT_RTN("%s: PING/PONG keepalives already stopped\n", WS_SESSION_REMOTE(session));
}

int ast_ws_close(struct ast_websocket *session, uint16_t reason, int is_reply, int force)
{
	int res = 0;
	SCOPE_ENTER(2, "%s: Close requested. Reason: %d is_reply: %s  force: %s\n",
		WS_SESSION_REMOTE(session), reason, AST_YESNO(is_reply), AST_YESNO(force));

	if (session->ping_sched_timer >= 0) {
		ping_scheduler_cancel(session);
	}

	if (session->closing) {
		SCOPE_EXIT_RTN_VALUE(0, "%s: Close already sent\n", WS_SESSION_REMOTE(session));
	}

	session->closing = 1;

	if (session->client) {
		res = session->client->vtbl->close(session, reason, is_reply, force);
	} else {
		res = tcptls_close(session, reason, is_reply, force);
	}
	SCOPE_EXIT_RTN_VALUE(res, "%s: Close done\n", WS_SESSION_REMOTE(session));
}

/*! \brief Close function for websocket session */
int AST_OPTIONAL_API_NAME(ast_websocket_close)(struct ast_websocket *session, uint16_t reason)
{
	return ast_ws_close(session, reason, 0, 0);
}

static const char *opcode_map[] = {
	[AST_WEBSOCKET_OPCODE_CONTINUATION] = "continuation",
	[AST_WEBSOCKET_OPCODE_TEXT] = "text",
	[AST_WEBSOCKET_OPCODE_BINARY] = "binary",
	[AST_WEBSOCKET_OPCODE_CLOSE] = "close",
	[AST_WEBSOCKET_OPCODE_PING] = "ping",
	[AST_WEBSOCKET_OPCODE_PONG] = "pong",
};

const char *ast_ws_opcode2str(enum ast_websocket_opcode opcode)
{
	if (opcode < AST_WEBSOCKET_OPCODE_CONTINUATION ||
			opcode > AST_WEBSOCKET_OPCODE_PONG) {
		return "<unknown>";
	} else {
		return opcode_map[opcode];
	}
}

int websocket_debug_level_for_opcode(enum ast_websocket_opcode opcode)
{
	int level = 0;
	switch (opcode) {
	case AST_WEBSOCKET_OPCODE_CLOSE:
	case AST_WEBSOCKET_OPCODE_UNKNOWN:
		level = 2;
		break;
	case AST_WEBSOCKET_OPCODE_TEXT:
		level = 3;
		break;
	case AST_WEBSOCKET_OPCODE_PING:
	case AST_WEBSOCKET_OPCODE_PONG:
		level = 4;
		break;
	case AST_WEBSOCKET_OPCODE_BINARY:
	case AST_WEBSOCKET_OPCODE_CONTINUATION:
		level = 5;
		break;
	}

	return level;
}

/*! \brief Write function for websocket traffic */
int AST_OPTIONAL_API_NAME(ast_websocket_write)(struct ast_websocket *session,
	enum ast_websocket_opcode opcode, char *payload, uint64_t payload_size)
{
	int res = 0;

	if (session->client) {
		res = session->client->vtbl->write(session, opcode, payload, payload_size);
	} else {
		res = tcptls_write(session, opcode, payload, payload_size);
	}

	ast_debug(websocket_debug_level_for_opcode(opcode), "%s: Wrote %s frame, length %d, result: %d\n",
		WS_SESSION_REMOTE(session), ast_ws_opcode2str(opcode), (int)payload_size, res);

	return res;
}

void AST_OPTIONAL_API_NAME(ast_websocket_reconstruct_enable)(struct ast_websocket *session, size_t bytes)
{
	session->reconstruct = MIN(bytes, MAXIMUM_RECONSTRUCTION_CEILING);
}

void AST_OPTIONAL_API_NAME(ast_websocket_reconstruct_disable)(struct ast_websocket *session)
{
	session->reconstruct = 0;
}

void AST_OPTIONAL_API_NAME(ast_websocket_ref)(struct ast_websocket *session)
{
	ao2_ref(session, +1);
}

void AST_OPTIONAL_API_NAME(ast_websocket_unref)(struct ast_websocket *session)
{
	ast_debug(2, "%s: Session: unref %p\n", WS_SESSION_REMOTE(session), session);

	ao2_cleanup(session);
}

int AST_OPTIONAL_API_NAME(ast_websocket_fd)(struct ast_websocket *session)
{
	if (session->client) {
		return session->client->vtbl->get_fd(session);
	}
	return tcptls_get_fd(session);
}

int AST_OPTIONAL_API_NAME(ast_websocket_wait_for_input)(struct ast_websocket *session, int timeout)
{
	if (session->client) {
		return session->client->vtbl->wait_for_input(session, timeout);
	}
	return tcptls_wait_for_input(session, timeout);
}

struct ast_sockaddr * AST_OPTIONAL_API_NAME(ast_websocket_remote_address)(struct ast_websocket *session)
{
	return &session->remote_address;
}

struct ast_sockaddr * AST_OPTIONAL_API_NAME(ast_websocket_local_address)(struct ast_websocket *session)
{
	return &session->local_address;
}

int AST_OPTIONAL_API_NAME(ast_websocket_is_secure)(struct ast_websocket *session)
{
	return session->secure;
}

int AST_OPTIONAL_API_NAME(ast_websocket_set_nonblock)(struct ast_websocket *session)
{
	session->non_blocking = 1;

	ast_debug(4, "%s: Set non-blocking\n", WS_SESSION_REMOTE(session));
	if (session->client) {
		return session->client->vtbl->set_nonblock(session);
	}
	return tcptls_set_nonblock(session);
}

int AST_OPTIONAL_API_NAME(ast_websocket_set_timeout)(struct ast_websocket *session, int timeout)
{
	ast_debug(2, "%s: Setting timeout to %dms\n", WS_SESSION_REMOTE(session), timeout);
	if (session->client) {
		return session->client->vtbl->set_timeout(session, timeout);
	}
	return tcptls_set_timeout(session, timeout);
}

const char * AST_OPTIONAL_API_NAME(ast_websocket_session_id)(struct ast_websocket *session)
{
	return session->session_id;
}

int AST_OPTIONAL_API_NAME(ast_websocket_read)(struct ast_websocket *session, char **payload,
	uint64_t *payload_len, enum ast_websocket_opcode *opcode, int *fragmented)
{
	int res = 0;

	*payload = NULL;
	*payload_len = 0;
	*fragmented = 0;

	if (session->client) {
		res = session->client->vtbl->read(session, payload, payload_len, opcode, fragmented);
	} else {
		res = tcptls_read(session, payload, payload_len, opcode, fragmented);
	}

	ast_debug(websocket_debug_level_for_opcode(*opcode), "%s: Read %s frame, length %d, result: %d\n",
		WS_SESSION_REMOTE(session), ast_ws_opcode2str(*opcode), (int)*payload_len, res);

	return res;
}

char *websocket_combine_key(const char *key, char *res, int res_size)
{
	char *combined;
	unsigned combined_length = strlen(key) + strlen(WEBSOCKET_GUID) + 1;
	uint8_t sha[20];

	combined = ast_alloca(combined_length);
	snprintf(combined, combined_length, "%s%s", key, WEBSOCKET_GUID);
	ast_sha1_hash_uint(sha, combined);
	ast_base64encode(res, (const unsigned char*)sha, 20, res_size);
	return res;
}

static void websocket_client_destroy(void *obj)
{
	struct websocket_client *client = obj;
	char *id = ast_strdupa(client->options->uri);
	SCOPE_ENTER(2, "%s: Client destructor %p\n", id, obj);

	if (client->vtbl->data_cleanup) {
		client->vtbl->data_cleanup(client->data);
	}

	ao2_cleanup(client->options);
	ao2_cleanup(client->ser);
	ao2_cleanup(client->args);

	ast_free(client->accept_protocol);
	ast_free(client->protocols);
	ast_free(client->key);
	ast_free(client->resource_name);
	ast_free(client->host);
	ast_free(client->userinfo);
	ast_module_unref(client->vtbl->module);

	SCOPE_EXIT_RTN("%s: Client destructor complete\n", id);
}

static void client_options_destroy(void *obj)
{
	struct ast_websocket_client_options *clone = obj;
	ast_free((char *)clone->uri);
	ast_free((char *)clone->protocols);
	ast_free((char *)clone->username);
	ast_free((char *)clone->password);
	ast_free((char *)clone->proxy_uri);
	ast_free((char *)clone->proxy_username);
	ast_free((char *)clone->proxy_password);
	ast_free(clone->tls_cfg);
}

#define SAFE_STRDUP_WITH_ERROR_RTN(_clone, _str) \
({ \
	char *_duped = NULL; \
	if (_str) { \
		_duped = ast_strdup(_str); \
		if (!_duped) { \
			ao2_cleanup(_clone); \
			return NULL; \
		} \
	} \
	_duped; \
})

static struct ast_websocket_client_options *client_options_clone(
	struct ast_websocket_client_options *options)
{
	struct ast_websocket_client_options *clone = NULL;

	clone = ao2_alloc(sizeof(*clone), client_options_destroy);
	if (!clone) {
		ast_log(LOG_ERROR, "Unable to clone client options\n");
		return NULL;
	}

	memcpy(clone, options, sizeof(*options));
	clone->uri = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->uri);
	clone->protocols = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->protocols);
	clone->username = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->username);
	clone->password = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->password);
	clone->proxy_uri = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->proxy_uri);
	clone->proxy_username = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->proxy_username);
	clone->proxy_password = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->proxy_password);
	if (options->tls_cfg) {
		clone->tls_cfg = ast_calloc(1, sizeof(*options->tls_cfg));
		if (!clone->tls_cfg) {
			ao2_cleanup(clone);
			return NULL;
		}
		memcpy(clone->tls_cfg, options->tls_cfg, sizeof(*options->tls_cfg));
	}

	return clone;
}

static struct ast_websocket *websocket_client_create(
	struct ast_websocket_client_options *options, enum ast_websocket_result *result)
{
	struct ast_websocket *ws = NULL;

	ast_debug(2, "%s: Creating client\n", options->uri);

	ws = ao2_alloc(sizeof(*ws), session_destroy_fn);
	if (!ws) {
		ast_log(LOG_ERROR, "Unable to allocate websocket\n");
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}
	ws->ping_sched_timer = -1;

	if (!ast_uuid_generate_str(ws->session_id, sizeof(ws->session_id))) {
		ast_log(LOG_ERROR, "Unable to allocate websocket session_id\n");
		ao2_ref(ws, -1);
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	if (!(ws->client = ao2_alloc(
		      sizeof(*ws->client), websocket_client_destroy))) {
		ast_log(LOG_ERROR, "Unable to allocate websocket client\n");
		ao2_ref(ws, -1);
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	ws->client->options = client_options_clone(options);
	if (!ws->client->options) {
		ast_log(LOG_ERROR, "Unable to clone client options\n");
		ao2_ref(ws, -1);
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	ws->client->backend_type = client_backend;
	ws->client->vtbl = &client_vtbls[client_backend];

	ws->client->vtbl->create(ws, options, result);
	if (*result != WS_OK) {
		return NULL;
	}
	ast_module_ref(ws->client->vtbl->module);

	return ws;
}

const char * AST_OPTIONAL_API_NAME(
	ast_websocket_client_accept_protocol)(struct ast_websocket *ws)
{
	return ws->client->accept_protocol;
}


struct ast_websocket *AST_OPTIONAL_API_NAME(ast_websocket_client_create)
	(const char *uri, const char *protocols, struct ast_tls_config *tls_cfg,
	 enum ast_websocket_result *result)
{
	struct ast_websocket_client_options options = {
		.uri = uri,
		.protocols = protocols,
		.timeout = -1,
		.tls_cfg = tls_cfg,
	};

	return ast_websocket_client_create_with_options(&options, result);
}

static int ping_scheduler_callback(const void *obj)
{
	struct ast_websocket *session = (struct ast_websocket *)obj;

	if (!session->client) {
		/*
		 * We should never get here because we can only start pingpongs from a client
		 * but just in case...
		 */
		return 0;
	}

	ao2_lock(session);
	if (session->closing) {
		ao2_unlock(session);
		return 0;
	}

	if (session->missed_pong_count > 1) {
		ast_debug(2, "%s: Missed PONG count is now %d\n", WS_SESSION_REMOTE(session),
			session->missed_pong_count);
	}

	if (session->missed_pong_count >= session->client->options->pingpong_probes) {
		ao2_unlock(session);
		ast_log(LOG_WARNING, "%s: %d missed PONGs. Closing connection.\n",
			WS_SESSION_REMOTE(session), session->missed_pong_count);
		ast_ws_close(session, AST_WEBSOCKET_STATUS_GOING_AWAY, 0, 1);
		return 0;
	}

	ast_websocket_write(session, AST_WEBSOCKET_OPCODE_PING, WS_PING_PAYLOAD, WS_PING_PAYLOAD_LEN);
	session->missed_pong_count++;

	ao2_unlock(session);
	return session->client->options->pingpong_intvl * 1000;
}

struct ast_websocket *AST_OPTIONAL_API_NAME(ast_websocket_client_create_with_options)
	(struct ast_websocket_client_options *options, enum ast_websocket_result *result)
{
	struct ast_websocket *ws = NULL;
	SCOPE_ENTER(2, "%s: Creating client\n", options->uri);

	ws = websocket_client_create(options, result);
	if (!ws) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Failed to create: %s\n", options->uri, ast_websocket_result_to_str(*result));
	}

	ast_trace(-1, "%s: Connecting\n", ws->client->options->uri);

	if ((*result = ws->client->vtbl->connect(ws, options)) != WS_OK) {
		ao2_ref(ws, -1);
 		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Failed to connect: %s\n", options->uri, ast_websocket_result_to_str(*result));
	}
	ast_trace(-1, "%s: Connected\n", ws->client->options->uri);

	if (ws->client->options->tcp_keepalives) {
		int sockfd = ws->client->vtbl->get_fd(ws);
		int enabled = 1;

		setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &options->tcp_keepalive_time, sizeof(options->tcp_keepalive_time));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &options->tcp_keepalive_intvl, sizeof(&options->tcp_keepalive_intvl));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &options->tcp_keepalive_probes, sizeof(&options->tcp_keepalive_probes));
		ast_trace(-1, "%s: Enabled TCP keepalives\n", ws->client->options->uri);
	}

	if (ws->client->options->pingpongs) {
		ws->ping_sched_timer = ast_sched_add(ping_scheduler, ws->client->options->pingpong_intvl * 1000,
			ping_scheduler_callback, ao2_bump(ws));
		if (ws->ping_sched_timer < 0) {
			ast_log(LOG_WARNING, "%s: Unable to schedule PING/PONG keepalives\n", ws->client->options->uri);
		} else {
			ast_trace(-1, "%s: Enabled PING/PONG keepalives\n", ws->client->options->uri);
		}
	}
	SCOPE_EXIT_RTN_VALUE(ws, "%s: Client created and connected %p %p\n", options->uri, ws, ws->client);
}

int AST_OPTIONAL_API_NAME(ast_websocket_client_is_proxy_supported)(void)
{
	return client_vtbls[client_backend].supports_proxy;
}

int AST_OPTIONAL_API_NAME(ast_websocket_read_string)
	(struct ast_websocket *ws, char **buf)
{
	char *payload;
	uint64_t payload_len;
	enum ast_websocket_opcode opcode;
	int fragmented = 1;

	while (fragmented) {
		if (ast_websocket_read(ws, &payload, &payload_len, &opcode, &fragmented)) {
			ast_log(LOG_ERROR, "Client WebSocket string read - "
				"error reading string data\n");
			return -1;
		}

		if (opcode == AST_WEBSOCKET_OPCODE_PING) {
			/* Try read again, we have sent pong already */
			fragmented = 1;
			continue;
		}

		if (opcode == AST_WEBSOCKET_OPCODE_CONTINUATION) {
			continue;
		}

		if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
			return -1;
		}

		if (opcode != AST_WEBSOCKET_OPCODE_TEXT) {
			ast_log(LOG_ERROR, "Client WebSocket string read - "
				"non string data received\n");
			return -1;
		}
	}

	if (!(*buf = ast_strndup(payload, payload_len))) {
		return -1;
	}

	return payload_len + 1;
}

int AST_OPTIONAL_API_NAME(ast_websocket_write_string)
	(struct ast_websocket *ws, const char *buf)
{
	uint64_t len = strlen(buf);

	ast_debug(4, "Writing websocket string of length %" PRIu64 "\n", len);

	/* We do not pass strlen(buf) to ast_websocket_write() directly because the
	 * size_t returned by strlen() may not require the same storage size
	 * as the uint64_t that ast_websocket_write() uses. This normally
	 * would not cause a problem, but since ast_websocket_write() uses
	 * the optional API, this function call goes through a series of macros
	 * that may cause a 32-bit to 64-bit conversion to go awry.
	 */
	return ast_websocket_write(ws, AST_WEBSOCKET_OPCODE_TEXT,
				   (char *)buf, len);
}

const char *websocket_result_string_map[] = {
	[WS_OK] = "OK",
	[WS_ALLOCATE_ERROR] = "Allocation error",
	[WS_KEY_ERROR] = "Key error",
	[WS_URI_PARSE_ERROR] = "URI parse error",
	[WS_URI_RESOLVE_ERROR] = "URI resolve error",
	[WS_BAD_STATUS] = "Bad status line",
	[WS_INVALID_RESPONSE] = "Invalid response code",
	[WS_BAD_REQUEST] = "Bad request",
	[WS_URL_NOT_FOUND] = "URL not found",
	[WS_HEADER_MISMATCH] = "Header mismatch",
	[WS_HEADER_MISSING] = "Header missing",
	[WS_NOT_SUPPORTED] = "Not supported",
	[WS_WRITE_ERROR] = "Write error",
	[WS_CLIENT_START_ERROR] = "Client start error",
	[WS_UNAUTHORIZED] = "Unauthorized"
};

const char *AST_OPTIONAL_API_NAME(ast_websocket_result_to_str)
	(enum ast_websocket_result result)
{
	if (!ARRAY_IN_BOUNDS(result, websocket_result_string_map)) {
		return "unknown";
	}
	return websocket_result_string_map[result];
}

struct status_map {
	enum ast_websocket_status_code code;
	const char *desc;
};

static const struct status_map websocket_status_map[] = {
	{ AST_WEBSOCKET_STATUS_NORMAL, "Normal" },
	{ AST_WEBSOCKET_STATUS_GOING_AWAY, "Going away" },
	{ AST_WEBSOCKET_STATUS_PROTOCOL_ERROR, "Protocol error" },
	{ AST_WEBSOCKET_STATUS_UNSUPPORTED_DATA, "Unsupported data" },
	{ AST_WEBSOCKET_STATUS_RESERVED_1004, "reserved 1004" },
	{ AST_WEBSOCKET_STATUS_RESERVED_1005, "reserved 1005" },
	{ AST_WEBSOCKET_STATUS_RESERVED_1006, "reserved 1006" },
	{ AST_WEBSOCKET_STATUS_INVALID_FRAME, "Invalid frame" },
	{ AST_WEBSOCKET_STATUS_POLICY_VIOLATION, "Policy violation" },
	{ AST_WEBSOCKET_STATUS_TOO_BIG, "Data too big" },
	{ AST_WEBSOCKET_STATUS_MANDATORY_EXT, "Mandatory extension" },
	{ AST_WEBSOCKET_STATUS_INTERNAL_ERROR, "Internal error" },
	{ AST_WEBSOCKET_STATUS_RESERVED_1012, "reserved 1012" },
	{ AST_WEBSOCKET_STATUS_RESERVED_1013, "reserved 1013" },
	{ AST_WEBSOCKET_STATUS_BAD_GATEWAY, "Bad gateway" },
	{ AST_WEBSOCKET_STATUS_RESERVED_1015, "reserved 1015" },
};

const char *AST_OPTIONAL_API_NAME(ast_websocket_status_to_str)
	(enum ast_websocket_status_code code)
{
	int i;

	for (i = 0; i < ARRAY_LEN(websocket_status_map); i++) {
		if (websocket_status_map[i].code == code)
			return websocket_status_map[i].desc;
	}

	return "Unknown";
}

int AST_OPTIONAL_API_NAME(ast_websocket_uri_cb)(struct ast_tcptls_session_instance *ser,
	const struct ast_http_uri *urih, const char *uri, enum ast_http_method method,
	struct ast_variable *get_vars, struct ast_variable *headers)
{
	SCOPED_MODULE_USE(ast_module_info->self);
	return tcptls_server_uri_cb(ser, urih, uri, method, get_vars, headers);
}

int _ast_ws_unregister_client_backend(struct ast_module *mod, enum ws_client_backend backend)
{
	if (backend <= backend_tcptls || backend >= backend_last) {
		return -1;
	}
	client_backend = backend_tcptls;
	return 0;
}

int _ast_ws_register_client_backend(struct ast_module *mod, enum ws_client_backend backend, struct client_vtbl vtbl)
{
	if (backend <= backend_tcptls || backend >= backend_last) {
		return -1;
	}
	client_vtbls[backend] = vtbl;
	client_vtbls[backend].module = mod;
	client_backend = backend;
	return 0;
}

static int unload_module(void)
{
	if (ping_scheduler) {
		ast_sched_context_destroy(ping_scheduler);
		ping_scheduler = NULL;
	}

	return tcptls_unregister_server();
}

static int load_module(void)
{
	client_vtbls[backend_tcptls] = tcptls_get_client_vtbl();
	client_vtbls[backend_tcptls].module = ast_module_info->self;

	if (tcptls_register_server() != AST_MODULE_LOAD_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ping_scheduler = ast_sched_context_create();
	if (!ping_scheduler) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sched_start_thread(ping_scheduler)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "HTTP WebSocket Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "http",
);
