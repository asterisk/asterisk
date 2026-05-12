/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
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
#include "websocket_private.h"

/*! \brief Number of buckets for registered protocols */
#define MAX_PROTOCOL_BUCKETS 7

static void websocket_server_dtor(void *obj)
{
	struct ast_websocket_server *server = obj;
	ao2_cleanup(server->protocols);
	server->protocols = NULL;
}

/*! \brief Hashing function for protocols */
static int protocol_hash_fn(const void *obj, const int flags)
{
	const struct ast_websocket_protocol *protocol = obj;
	const char *name = obj;

	return ast_str_case_hash(flags & OBJ_KEY ? name : protocol->name);
}

/*! \brief Comparison function for protocols */
static int protocol_cmp_fn(void *obj, void *arg, int flags)
{
	const struct ast_websocket_protocol *protocol1 = obj, *protocol2 = arg;
	const char *protocol = arg;

	return !strcasecmp(protocol1->name, flags & OBJ_KEY ? protocol : protocol2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static struct ast_websocket_server *websocket_server_create_impl(void)
{
	RAII_VAR(struct ast_websocket_server *, server, NULL, ao2_cleanup);

	server = ao2_alloc(sizeof(*server), websocket_server_dtor);
	if (!server) {
		return NULL;
	}

	server->protocols = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		MAX_PROTOCOL_BUCKETS, protocol_hash_fn, NULL, protocol_cmp_fn);
	if (!server->protocols) {
		return NULL;
	}

	ao2_ref(server, +1);
	return server;
}

struct ast_websocket_server *websocket_server_internal_create(void)
{
	return websocket_server_create_impl();
}

struct ast_websocket_server *AST_OPTIONAL_API_NAME(ast_websocket_server_create)(void)
{
	return websocket_server_create_impl();
}

/*! \brief Destructor function for protocols */
static void protocol_destroy_fn(void *obj)
{
	struct ast_websocket_protocol *protocol = obj;
	ast_free(protocol->name);
}

struct ast_websocket_protocol *AST_OPTIONAL_API_NAME(ast_websocket_sub_protocol_alloc)(const char *name)
{
	struct ast_websocket_protocol *protocol;

	protocol = ao2_alloc(sizeof(*protocol), protocol_destroy_fn);
	if (!protocol) {
		return NULL;
	}

	protocol->name = ast_strdup(name);
	if (!protocol->name) {
		ao2_ref(protocol, -1);
		return NULL;
	}
	protocol->version = AST_WEBSOCKET_PROTOCOL_VERSION;

	return protocol;
}

int AST_OPTIONAL_API_NAME(ast_websocket_server_add_protocol)(struct ast_websocket_server *server, const char *name, ast_websocket_callback callback)
{
	struct ast_websocket_protocol *protocol;

	if (!server->protocols) {
		return -1;
	}

	protocol = ast_websocket_sub_protocol_alloc(name);
	if (!protocol) {
		return -1;
	}
	protocol->session_established = callback;

	if (ast_websocket_server_add_protocol2(server, protocol)) {
		ao2_ref(protocol, -1);
		return -1;
	}

	return 0;
}

int AST_OPTIONAL_API_NAME(ast_websocket_server_add_protocol2)(struct ast_websocket_server *server, struct ast_websocket_protocol *protocol)
{
	struct ast_websocket_protocol *existing;

	if (!server->protocols) {
		return -1;
	}

	if (protocol->version != AST_WEBSOCKET_PROTOCOL_VERSION) {
		ast_log(LOG_WARNING, "WebSocket could not register sub-protocol '%s': "
			"expected version '%u', got version '%u'\n",
			protocol->name, AST_WEBSOCKET_PROTOCOL_VERSION, protocol->version);
		return -1;
	}

	ao2_lock(server->protocols);

	/* Ensure a second protocol handler is not registered for the same protocol */
	existing = ao2_find(server->protocols, protocol->name, OBJ_KEY | OBJ_NOLOCK);
	if (existing) {
		ao2_ref(existing, -1);
		ao2_unlock(server->protocols);
		return -1;
	}

	ao2_link_flags(server->protocols, protocol, OBJ_NOLOCK);
	ao2_unlock(server->protocols);

	ast_debug(1, "WebSocket registered sub-protocol '%s'\n", protocol->name);
	ao2_ref(protocol, -1);

	return 0;
}

int AST_OPTIONAL_API_NAME(ast_websocket_server_remove_protocol)(struct ast_websocket_server *server, const char *name, ast_websocket_callback callback)
{
	struct ast_websocket_protocol *protocol;

	if (!(protocol = ao2_find(server->protocols, name, OBJ_KEY))) {
		return -1;
	}

	if (protocol->session_established != callback) {
		ao2_ref(protocol, -1);
		return -1;
	}

	ao2_unlink(server->protocols, protocol);
	ao2_ref(protocol, -1);

	ast_debug(1, "WebSocket unregistered sub-protocol '%s'\n", name);

	return 0;
}

/*!
 * \brief If the server has exactly one configured protocol, return it.
 */
static struct ast_websocket_protocol *one_protocol(
	struct ast_websocket_server *server)
{
	SCOPED_AO2LOCK(lock, server->protocols);

	if (ao2_container_count(server->protocols) != 1) {
		return NULL;
	}

	return ao2_callback(server->protocols, OBJ_NOLOCK, NULL, NULL);
}

static void websocket_bad_request(struct ast_tcptls_session_instance *ser)
{
	struct ast_str *http_header = ast_str_create(64);

	if (!http_header) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Out of memory");
		return;
	}
	ast_str_set(&http_header, 0, "Sec-WebSocket-Version: 7, 8, 13\r\n");
	ast_http_send(ser, AST_HTTP_UNKNOWN, 400, "Bad Request", http_header, NULL, 0, 0);
}

int tcptls_server_uri_cb(struct ast_tcptls_session_instance *ser,
	const struct ast_http_uri *urih, const char *uri, enum ast_http_method method,
	struct ast_variable *get_vars, struct ast_variable *headers)
{
	struct ast_variable *v;
	const char *upgrade = NULL, *key = NULL, *key1 = NULL, *key2 = NULL, *protos = NULL;
	char *requested_protocols = NULL, *protocol = NULL;
	int version = 0, flags = 1;
	struct ast_websocket_protocol *protocol_handler = NULL;
	struct ast_websocket *session;
	struct ast_websocket_server *server;


	/* Upgrade requests are only permitted on GET methods */
	if (method != AST_HTTP_GET) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return 0;
	}

	server = urih->data;

	/* Get the minimum headers required to satisfy our needs */
	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "Upgrade")) {
			upgrade = v->value;
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Key")) {
			key = v->value;
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Key1")) {
			key1 = v->value;
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Key2")) {
			key2 = v->value;
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Protocol")) {
			protos = v->value;
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Version")) {
			if (sscanf(v->value, "%30d", &version) != 1) {
				version = 0;
			}
		}
	}

	/* If this is not a websocket upgrade abort */
	if (!upgrade || strcasecmp(upgrade, "websocket")) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - did not request WebSocket\n",
			ast_sockaddr_stringify(&ser->remote_address));
		ast_http_error(ser, 426, "Upgrade Required", NULL);
		return 0;
	} else if (ast_strlen_zero(protos)) {
		/* If there's only a single protocol registered, and the
		 * client doesn't specify what protocol it's using, go ahead
		 * and accept the connection */
		protocol_handler = one_protocol(server);
		if (!protocol_handler) {
			/* Multiple registered subprotocols; client must specify */
			ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - no protocols requested\n",
				ast_sockaddr_stringify(&ser->remote_address));
			websocket_bad_request(ser);
			return 0;
		}
	} else if (key1 && key2) {
		/* Specification defined in http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-76 and
		 * http://tools.ietf.org/html/draft-ietf-hybi-thewebsocketprotocol-00 -- not currently supported*/
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - unsupported version '00/76' chosen\n",
			ast_sockaddr_stringify(&ser->remote_address));
		websocket_bad_request(ser);
		return 0;
	}

	if (!protocol_handler && protos) {
		requested_protocols = ast_strdupa(protos);
		/* Iterate through the requested protocols trying to find one that we have a handler for */
		while (!protocol_handler && (protocol = strsep(&requested_protocols, ","))) {
			protocol_handler = ao2_find(server->protocols, ast_strip(protocol), OBJ_KEY);
		}
	}

	/* If no protocol handler exists bump this back to the requester */
	if (!protocol_handler) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - no protocols out of '%s' supported\n",
			ast_sockaddr_stringify(&ser->remote_address), protos);
		websocket_bad_request(ser);
		return 0;
	}

	/* Determine how to respond depending on the version */
	if (version == 7 || version == 8 || version == 13) {
		char base64[64];

		if (!key || strlen(key) + strlen(WEBSOCKET_GUID) + 1 > 8192) { /* no stack overflows please */
			websocket_bad_request(ser);
			ao2_ref(protocol_handler, -1);
			return 0;
		}

		if (ast_http_body_discard(ser)) {
			websocket_bad_request(ser);
			ao2_ref(protocol_handler, -1);
			return 0;
		}

		if (!(session = ao2_alloc(sizeof(*session) + AST_UUID_STR_LEN + 1, session_destroy_fn))) {
			ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted\n",
				ast_sockaddr_stringify(&ser->remote_address));
			websocket_bad_request(ser);
			ao2_ref(protocol_handler, -1);
			return 0;
		}
		session->timeout = AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT;
		session->ping_sched_timer = -1;

		/* Generate the session id */
		if (!ast_uuid_generate_str(session->session_id, sizeof(session->session_id))) {
			ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - failed to generate a session id\n",
				ast_sockaddr_stringify(&ser->remote_address));
			ast_http_error(ser, 500, "Internal Server Error", "Allocation failed");
			ao2_ref(protocol_handler, -1);
			return 0;
		}

		if (protocol_handler->session_attempted
		    && protocol_handler->session_attempted(ser, get_vars, headers, session->session_id)) {
			ast_debug(3, "WebSocket connection from '%s' rejected by protocol handler '%s'\n",
				ast_sockaddr_stringify(&ser->remote_address), protocol_handler->name);
			websocket_bad_request(ser);
			ao2_ref(protocol_handler, -1);
			return 0;
		}

		/* RFC 6455, Section 4.1:
		 *
		 * 6. If the response includes a |Sec-WebSocket-Protocol| header
		 *    field and this header field indicates the use of a
		 *    subprotocol that was not present in the client's handshake
		 *    (the server has indicated a subprotocol not requested by
		 *    the client), the client MUST _Fail the WebSocket
		 *    Connection_.
		 */
		if (protocol) {
			ast_iostream_printf(ser->stream,
				"HTTP/1.1 101 Switching Protocols\r\n"
				"Upgrade: %s\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Accept: %s\r\n"
				"Sec-WebSocket-Protocol: %s\r\n\r\n",
				upgrade,
				websocket_combine_key(key, base64, sizeof(base64)),
				protocol);
		} else {
			ast_iostream_printf(ser->stream,
				"HTTP/1.1 101 Switching Protocols\r\n"
				"Upgrade: %s\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Accept: %s\r\n\r\n",
				upgrade,
				websocket_combine_key(key, base64, sizeof(base64)));
		}
	} else {

		/* Specification defined in http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-75 or completely unknown */
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - unsupported version '%d' chosen\n",
			ast_sockaddr_stringify(&ser->remote_address), version ? version : 75);
		websocket_bad_request(ser);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	/* Enable keepalive on all sessions so the underlying user does not have to */
	if (setsockopt(ast_iostream_get_fd(ser->stream), SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags))) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - failed to enable keepalive\n",
			ast_sockaddr_stringify(&ser->remote_address));
		websocket_bad_request(ser);
		ao2_ref(session, -1);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	/* Get our local address for the connected socket */
	if (ast_getsockname(ast_iostream_get_fd(ser->stream), &session->local_address)) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - failed to get local address\n",
			ast_sockaddr_stringify(&ser->remote_address));
		websocket_bad_request(ser);
		ao2_ref(session, -1);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	ast_debug(3, "WebSocket connection from '%s' for protocol '%s' accepted using version '%d'\n", ast_sockaddr_stringify(&ser->remote_address), protocol ? : "", version);

	/* Populate the session with all the needed details */
	session->stream = ser->stream;
	ast_sockaddr_copy(&session->remote_address, &ser->remote_address);
	session->opcode = -1;
	session->reconstruct = DEFAULT_RECONSTRUCTION_CEILING;
	session->secure = ast_iostream_get_ssl(ser->stream) ? 1 : 0;

	/* Give up ownership of the socket and pass it to the protocol handler */
	ast_iostream_set_exclusive_input(session->stream, 0);
	protocol_handler->session_established(session, get_vars, headers);
	ao2_ref(protocol_handler, -1);

	/*
	 * By dropping the stream from the session the connection
	 * won't get closed when the HTTP server cleans up because we
	 * passed the connection to the protocol handler.
	 */
	ser->stream = NULL;

	return 0;
}

static struct ast_http_uri websocketuri = {
	.callback = AST_OPTIONAL_API_NAME(ast_websocket_uri_cb),
	.description = "Asterisk HTTP WebSocket",
	.uri = "ws",
	.has_subtree = 0,
	.data = NULL,
	.key = __FILE__,
};

static int websocket_add_protocol_internal(const char *name, ast_websocket_callback callback)
{
	struct ast_websocket_server *ws_server = websocketuri.data;
	if (!ws_server) {
		return -1;
	}
	return ast_websocket_server_add_protocol(ws_server, name, callback);
}

int AST_OPTIONAL_API_NAME(ast_websocket_add_protocol)(const char *name, ast_websocket_callback callback)
{
	return websocket_add_protocol_internal(name, callback);
}

int AST_OPTIONAL_API_NAME(ast_websocket_add_protocol2)(struct ast_websocket_protocol *protocol)
{
	struct ast_websocket_server *ws_server = websocketuri.data;

	if (!ws_server) {
		return -1;
	}

	if (ast_websocket_server_add_protocol2(ws_server, protocol)) {
		return -1;
	}

	return 0;
}

static int websocket_remove_protocol_internal(const char *name, ast_websocket_callback callback)
{
	struct ast_websocket_server *ws_server = websocketuri.data;
	if (!ws_server) {
		return -1;
	}
	return ast_websocket_server_remove_protocol(ws_server, name, callback);
}

int AST_OPTIONAL_API_NAME(ast_websocket_remove_protocol)(const char *name, ast_websocket_callback callback)
{
	return websocket_remove_protocol_internal(name, callback);
}

/*! \brief Simple echo implementation which echoes received text and binary frames */
static void websocket_echo_callback(struct ast_websocket *session, struct ast_variable *parameters,
	struct ast_variable *headers)
{
	int res;

	ast_debug(1, "Entering WebSocket echo loop\n");

	if (ast_fd_set_flags(ast_websocket_fd(session), O_NONBLOCK)) {
		goto end;
	}

	while ((res = ast_websocket_wait_for_input(session, -1)) > 0) {
		char *payload;
		uint64_t payload_len;
		enum ast_websocket_opcode opcode;
		int fragmented;

		if (ast_websocket_read(session, &payload, &payload_len, &opcode, &fragmented)) {
			/* We err on the side of caution and terminate the session if any error occurs */
			ast_log(LOG_WARNING, "Read failure during WebSocket echo loop\n");
			break;
		}

		if (opcode == AST_WEBSOCKET_OPCODE_TEXT || opcode == AST_WEBSOCKET_OPCODE_BINARY) {
			ast_websocket_write(session, opcode, payload, payload_len);
		} else if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
			break;
		} else {
			ast_debug(1, "Ignored WebSocket opcode %u\n", opcode);
		}
	}

end:
	ast_debug(1, "Exiting WebSocket echo loop\n");
	ast_websocket_unref(session);
}

int tcptls_unregister_server(void)
{
	websocket_remove_protocol_internal("echo", websocket_echo_callback);
	ast_http_uri_unlink(&websocketuri);
	ao2_ref(websocketuri.data, -1);
	websocketuri.data = NULL;

	return 0;
}

int tcptls_register_server(void)
{
	websocketuri.data = websocket_server_internal_create();
	if (!websocketuri.data) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_http_uri_link(&websocketuri);
	websocket_add_protocol_internal("echo", websocket_echo_callback);

	return AST_MODULE_LOAD_SUCCESS;
}


