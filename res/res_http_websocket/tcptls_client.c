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

#include "websocket_private.h"

#include "asterisk/module.h"
#include "asterisk/http.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/file.h"
#include "asterisk/sched.h"
#include "asterisk/unaligned.h"
#include "asterisk/uri.h"
#include "asterisk/uuid.h"

/*! \brief Length of a websocket's client key */
#define CLIENT_KEY_SIZE 16

/*
 * Declarations for the original tcptls client-only backend functions.
 */
static struct ast_websocket *tcptls_client_create(struct ast_websocket *ws,
	struct ast_websocket_client_options *options, enum ast_websocket_result *result);
static enum ast_websocket_result tcptls_client_connect(struct ast_websocket *ws,
	struct ast_websocket_client_options *options);

static struct client_vtbl tcptls_client_vtbl = {
	.supports_proxy = 0,
	.create = tcptls_client_create,
	.connect = tcptls_client_connect,
	.close = tcptls_close,
	.read = tcptls_read,
	.write = tcptls_write,
	.wait_for_input = tcptls_wait_for_input,
	.set_timeout = tcptls_set_timeout,
	.set_nonblock = tcptls_set_nonblock,
	.get_fd = tcptls_get_fd,
};

struct client_vtbl tcptls_get_client_vtbl(void)
{
	return tcptls_client_vtbl;
}

static enum ast_websocket_result websocket_client_handle_response_code(
	struct websocket_client *client, int response_code)
{
	if (response_code <= 0) {
		return WS_INVALID_RESPONSE;
	}

	switch (response_code) {
	case 101:
		return 0;
	case 400:
		if (!client->suppress_connection_msgs) {
			ast_log(LOG_ERROR, "Received response 400 - Bad Request "
				"- from %s\n", client->host);
		}
		return WS_BAD_REQUEST;
	case 401:
		if (!client->suppress_connection_msgs) {
			ast_log(LOG_ERROR, "Received response 401 - Unauthorized "
				"- from %s\n", client->host);
		}
		return WS_UNAUTHORIZED;
	case 404:
		if (!client->suppress_connection_msgs) {
			ast_log(LOG_ERROR, "Received response 404 - Request URL not "
				"found - from %s\n", client->host);
		}
		return WS_URL_NOT_FOUND;
	}

	if (!client->suppress_connection_msgs) {
		ast_log(LOG_ERROR, "Invalid HTTP response code %d from %s\n",
			response_code, client->host);
	}
	return WS_INVALID_RESPONSE;
}

static enum ast_websocket_result websocket_client_handshake_get_response(
	struct websocket_client *client)
{
	enum ast_websocket_result res;
	char buf[4096];
	char base64[64];
	int has_upgrade = 0;
	int has_connection = 0;
	int has_accept = 0;
	int has_protocol = 0;

	while (ast_iostream_gets(client->ser->stream, buf, sizeof(buf)) <= 0) {
		if (errno == EINTR || errno == EAGAIN) {
			continue;
		}

		ast_log(LOG_ERROR, "Unable to retrieve HTTP status line.");
		return WS_BAD_STATUS;
	}

	if ((res = websocket_client_handle_response_code(client,
		    ast_http_response_status_line(
			    buf, "HTTP/1.1", 101))) != WS_OK) {
		return res;
	}

	/* Ignoring line folding - assuming header field values are contained
	   within a single line */
	while (1) {
		ssize_t len = ast_iostream_gets(client->ser->stream, buf, sizeof(buf));
		char *name, *value;
		int parsed;

		if (len <= 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			break;
		}

		parsed = ast_http_header_parse(buf, &name, &value);
		if (parsed < 0) {
			break;
		}

		if (parsed > 0) {
			continue;
		}

		if (!has_upgrade &&
		    (has_upgrade = ast_http_header_match(
			    name, "upgrade", value, "websocket")) < 0) {
			return WS_HEADER_MISMATCH;
		} else if (!has_connection &&
			   (has_connection = ast_http_header_match(
				   name, "connection", value, "upgrade")) < 0) {
			return WS_HEADER_MISMATCH;
		} else if (!has_accept &&
			   (has_accept = ast_http_header_match(
				   name, "sec-websocket-accept", value,
			    websocket_combine_key(
				    client->key, base64, sizeof(base64)))) < 0) {
			return WS_HEADER_MISMATCH;
		} else if (!has_protocol &&
			   (has_protocol = ast_http_header_match_in(
				   name, "sec-websocket-protocol", value, client->protocols))) {
			if (has_protocol < 0) {
				return WS_HEADER_MISMATCH;
			}
			client->accept_protocol = ast_strdup(value);
		} else if (!strcasecmp(name, "sec-websocket-extensions")) {
			ast_log(LOG_ERROR, "Extensions received, but not "
				"supported by client\n");
			return WS_NOT_SUPPORTED;
		}
	}

	return has_upgrade && has_connection && has_accept ?
		WS_OK : WS_HEADER_MISSING;
}

#define optional_header_spec "%s%s%s"
#define print_optional_header(test, name, value) \
	test ? name : "", \
	test ? value : "", \
	test ? "\r\n" : ""

static enum ast_websocket_result websocket_client_handshake(
	struct websocket_client *client)
{
	size_t protocols_len = 0;
	struct ast_variable *auth_header = NULL;
	size_t res;

	if (!ast_strlen_zero(client->userinfo)) {
		auth_header = ast_http_create_basic_auth_header(client->userinfo, NULL);
		if (!auth_header) {
			ast_log(LOG_ERROR, "Unable to allocate client websocket userinfo\n");
			return WS_ALLOCATE_ERROR;
		}
	}

	protocols_len = client->protocols ? strlen(client->protocols) : 0;

	res = ast_iostream_printf(client->ser->stream,
		"GET /%s HTTP/1.1\r\n"
		"Sec-WebSocket-Version: %d\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Host: %s\r\n"
		optional_header_spec
		optional_header_spec
		"Sec-WebSocket-Key: %s\r\n"
		"\r\n",
		client->resource_name ? ast_str_buffer(client->resource_name) : "",
		client->version,
		client->host,
		print_optional_header(auth_header, "Authorization: ", auth_header->value),
		print_optional_header(protocols_len, "Sec-WebSocket-Protocol: ", client->protocols),
		client->key
	);

	ast_variables_destroy(auth_header);
	if (res < 0) {
		ast_log(LOG_ERROR, "Failed to send handshake.\n");
		return WS_WRITE_ERROR;
	}
	/* wait for a response before doing anything else */
	return websocket_client_handshake_get_response(client);
}

static enum ast_websocket_result tcptls_client_connect(struct ast_websocket *ws,
	struct ast_websocket_client_options *options)
{
	enum ast_websocket_result res;

	/* create and connect the client - note client_start
	   releases the session instance on failure */
	if (!(ws->client->ser = ast_tcptls_client_start_timeout(
			ast_tcptls_client_create(ws->client->args), options->timeout))) {
		return WS_CLIENT_START_ERROR;
	}

	if ((res = websocket_client_handshake(ws->client)) != WS_OK) {
		ao2_ref(ws->client->ser, -1);
		ws->client->ser = NULL;
		return res;
	}

	ws->stream = ws->client->ser->stream;
	ws->secure = ast_iostream_get_ssl(ws->stream) ? 1 : 0;
	ws->client->ser->stream = NULL;
	ast_sockaddr_copy(&ws->remote_address, &ws->client->ser->remote_address);

	return WS_OK;
}

static void websocket_client_args_destroy(void *obj)
{
	struct ast_tcptls_session_args *args = obj;

	if (args->tls_cfg) {
		ast_free(args->tls_cfg->certfile);
		ast_free(args->tls_cfg->pvtfile);
		ast_free(args->tls_cfg->cipher);
		ast_free(args->tls_cfg->cafile);
		ast_free(args->tls_cfg->capath);

		ast_ssl_teardown(args->tls_cfg);
	}
	ast_free(args->tls_cfg);
}

static struct ast_tcptls_session_args *websocket_client_args_create(
	const char *host, struct ast_tls_config *tls_cfg,
	enum ast_websocket_result *result)
{
	struct ast_sockaddr *addr;
	struct ast_tcptls_session_args *args = ao2_alloc(
		sizeof(*args), websocket_client_args_destroy);

	if (!args) {
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	args->accept_fd = -1;
	args->tls_cfg = tls_cfg;
	args->name = "websocket client";

	if (!ast_sockaddr_resolve(&addr, host, 0, 0)) {
		ast_log(LOG_ERROR, "Unable to resolve address %s\n",
			host);
		ao2_ref(args, -1);
		*result = WS_URI_RESOLVE_ERROR;
		return NULL;
	}
	ast_sockaddr_copy(&args->remote_address, addr);
	ast_free(addr);

	/* We need to save off the hostname but it may contain a port spec */
	snprintf(args->hostname, sizeof(args->hostname),
		"%.*s",
		(int) strcspn(host, ":"), host);

	return args;
}

static char *websocket_client_create_key(void)
{
	static int encoded_size = CLIENT_KEY_SIZE * 2 * sizeof(char) + 1;
	/* key is randomly selected 16-byte base64 encoded value */
	unsigned char key[CLIENT_KEY_SIZE + sizeof(long) - 1];
	char *encoded = ast_malloc(encoded_size);
	long i = 0;

	if (!encoded) {
		ast_log(LOG_ERROR, "Unable to allocate client websocket key\n");
		return NULL;
	}

	while (i < CLIENT_KEY_SIZE) {
		long num = ast_random();
		memcpy(key + i, &num, sizeof(long));
		i += sizeof(long);
	}

	ast_base64encode(encoded, key, CLIENT_KEY_SIZE, encoded_size);
	return encoded;
}

/*! \brief Parse the given uri into a path and remote address.
 *
 * Expected uri form:
 * \verbatim [ws[s]]://<host>[:port][/<path>] \endverbatim
 *
 * The returned host will contain the address and optional port while
 * path will contain everything after the address/port if included.
 */
static int websocket_client_parse_uri(const char *uri, char **host,
	struct ast_str **path, char **userinfo)
{
	struct ast_uri *parsed_uri = ast_uri_parse_websocket(uri);

	if (!parsed_uri) {
		return -1;
	}

	*host = ast_uri_make_host_with_port(parsed_uri);
	*userinfo = ast_strdup(ast_uri_user_info(parsed_uri));

	if (ast_uri_path(parsed_uri) || ast_uri_query(parsed_uri)) {
		*path = ast_str_create(64);
		if (!*path) {
			ao2_ref(parsed_uri, -1);
			return -1;
		}

		if (ast_uri_path(parsed_uri)) {
			ast_str_set(path, 0, "%s", ast_uri_path(parsed_uri));
		}

		if (ast_uri_query(parsed_uri)) {
			ast_str_append(path, 0, "?%s", ast_uri_query(parsed_uri));
		}
	}

	ao2_ref(parsed_uri, -1);
	return 0;
}

static struct ast_websocket *tcptls_client_create(struct ast_websocket *ws,
	struct ast_websocket_client_options *options, enum ast_websocket_result *result)
{
	if (!(ws->client->key = websocket_client_create_key())) {
		ao2_ref(ws, -1);
		*result = WS_KEY_ERROR;
		return NULL;
	}

	if (websocket_client_parse_uri(
			options->uri, &ws->client->host, &ws->client->resource_name,
			&ws->client->userinfo)) {
		ao2_ref(ws, -1);
		*result = WS_URI_PARSE_ERROR;
		return NULL;
	}

	if (ast_strlen_zero(ws->client->userinfo)
		&& !ast_strlen_zero(options->username)
		&& !ast_strlen_zero(options->password)) {
		ast_asprintf(&ws->client->userinfo, "%s:%s", options->username,
			options->password);
	}

	if (!(ws->client->args = websocket_client_args_create(
		      ws->client->host, options->tls_cfg, result))) {
		ao2_ref(ws, -1);
		return NULL;
	}

	ws->client->suppress_connection_msgs = options->suppress_connection_msgs;
	ws->client->args->suppress_connection_msgs = options->suppress_connection_msgs;
	ws->client->protocols = ast_strdup(options->protocols);
	ws->client->version = 13;
	ws->opcode = -1;
	ws->reconstruct = DEFAULT_RECONSTRUCTION_CEILING;
	ws->timeout = options->timeout;

	return ws;
}

/*! \brief Perform payload masking for client sessions */
static void websocket_mask_payload(struct ast_websocket *session, char *frame, char *payload, uint64_t payload_size)
{
	/* RFC 6455 5.1 - clients MUST mask frame data */
	if (session->client) {
		uint64_t i;
		uint8_t mask_key_idx;
		uint32_t mask_key = ast_random();
		uint8_t length = frame[1] & 0x7f;
		frame[1] |= 0x80; /* set mask bit to 1 */
		/* The mask key octet position depends on the length */
		mask_key_idx = length == 126 ? 4 : length == 127 ? 10 : 2;
		put_unaligned_uint32(&frame[mask_key_idx], mask_key);
		for (i = 0; i < payload_size; i++) {
			payload[i] ^= ((char *)&mask_key)[i % 4];
		}
	}
}

int tcptls_close(struct ast_websocket *session, uint16_t reason, int is_reply, int force)
{
	enum ast_websocket_opcode opcode = AST_WEBSOCKET_OPCODE_CLOSE;
	/* The header is either 2 or 6 bytes and the
	 * reason code takes up another 2 bytes */
	char frame[8] = { 0, };
	int header_size, fsize, res = 0;
	int fd = ast_iostream_get_fd(session->stream);
	SCOPE_ENTER(2, "%s: Close requested.  Reason: %d Is reply: %s Force: %s\n", WS_SESSION_REMOTE(session),
		reason, AST_YESNO(is_reply), AST_YESNO(force));

	ao2_lock(session);
	if (!session->stream) {
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(-1, "%s: WebSocket connection already closed\n", WS_SESSION_REMOTE(session));
	}

	if (force) {
		ast_trace(-1, "%s: Forcing close. Handle: %p FD: %d\n", WS_SESSION_REMOTE(session),
			session->stream, fd);
		ast_iostream_close(session->stream);
		session->stream = NULL;
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Forced close\n", WS_SESSION_REMOTE(session));
		return -1;
	}

	/* clients need space for an additional 4 byte masking key */
	header_size = session->client ? 6 : 2;
	fsize = header_size + 2;
	session->close_sent = 1;

	frame[0] = opcode | 0x80;
	frame[1] = 2; /* The reason code is always 2 bytes */

	/* If no reason has been specified assume 1000 which is normal closure */
	put_unaligned_uint16(&frame[header_size], htons(reason ? reason : 1000));

	websocket_mask_payload(session, frame, &frame[header_size], 2);
	ast_trace(-1, "%s: Writing %sCLOSE frame with reason %d.  fd: %d\n", WS_SESSION_REMOTE(session),
		is_reply ? "reply " : "", reason, fd);

	ast_iostream_set_timeout_inactivity(session->stream, session->timeout);
	res = ast_iostream_write(session->stream, frame, fsize);
	ast_iostream_set_timeout_disable(session->stream);

	if (is_reply || res != fsize) {
		if (is_reply) {
			ast_trace(-1, "%s: Writing CLOSE reply. Closing socket.  fd: %d\n",
				WS_SESSION_REMOTE(session), fd);
		} else {
			ast_trace(-1, "%s: Writing CLOSE failed.  Forcing close.  fd: %d\n",
				WS_SESSION_REMOTE(session), fd);
		}
		tcptls_close(session, reason, 0, 1);
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Socket closed after %s\n", WS_SESSION_REMOTE(session),
			is_reply ? "reply" : "write failure");
	}

	ao2_unlock(session);
	SCOPE_EXIT_RTN_VALUE(res == sizeof(frame), "%s: Close done\n", WS_SESSION_REMOTE(session));
}

int tcptls_get_fd(struct ast_websocket *session)
{
	return session->closing ? -1 : ast_iostream_get_fd(session->stream);
}

int tcptls_wait_for_input(struct ast_websocket *session, int timeout)
{
	return session->closing ? -1 : ast_iostream_wait_for_input(session->stream, timeout);
}

int tcptls_set_timeout(struct ast_websocket *session, int timeout)
{
	session->timeout = timeout;

	return 0;
}

/* MAINTENANCE WARNING on ast_websocket_read()!
 *
 * We have to keep in mind during this function that the fact that session->fd seems ready
 * (via poll) does not necessarily mean we have application data ready, because in the case
 * of an SSL socket, there is some encryption data overhead that needs to be read from the
 * TCP socket, so poll() may say there are bytes to be read, but whether it is just 1 byte
 * or N bytes we do not know that, and we do not know how many of those bytes (if any) are
 * for application data (for us) and not just for the SSL protocol consumption
 *
 * There used to be a couple of nasty bugs here that were fixed in last refactoring but I
 * want to document them so the constraints are clear and we do not re-introduce them:
 *
 * - This function would incorrectly assume that fread() would necessarily return more than
 *   1 byte of data, just because a websocket frame is always >= 2 bytes, but the thing
 *   is we're dealing with a TCP bitstream here, we could read just one byte and that's normal.
 *   The problem before was that if just one byte was read, the function bailed out and returned
 *   an error, effectively dropping the first byte of a websocket frame header!
 *
 * - Another subtle bug was that it would just read up to MAX_WS_HDR_SZ (14 bytes) via fread()
 *   then assume that executing poll() would tell you if there is more to read, but since
 *   we're dealing with a buffered stream (session->f is a FILE*), poll would say there is
 *   nothing else to read (in the real tcp socket session->fd) and we would get stuck here
 *   without processing the rest of the data in session->f internal buffers until another packet
 *   came on the network to unblock us!
 *
 * Note during the header parsing stage we try to read in small chunks just what we need, this
 * is buffered data anyways, no expensive syscall required most of the time ...
 */
static inline int ws_safe_read(struct ast_websocket *session, char *buf, size_t len, enum ast_websocket_opcode *opcode)
{
	ssize_t rlen;
	int xlen = len;
	char *rbuf = buf;
	int sanity = 10;

	ast_assert(len > 0);

	if (!len) {
		errno = EINVAL;
		return -1;
	}

	ao2_lock(session);
	if (!session->stream) {
		ao2_unlock(session);
		errno = ECONNABORTED;
		return -1;
	}

	for (;;) {
		rlen = ast_iostream_read(session->stream, rbuf, xlen);
		if (rlen != xlen) {
			if (rlen == 0) {
				ast_log(LOG_WARNING, "Web socket closed abruptly\n");
				*opcode = AST_WEBSOCKET_OPCODE_CLOSE;
				session->closing = 1;
				ao2_unlock(session);
				return -1;
			}

			if (rlen < 0 && errno != EAGAIN) {
				ast_log(LOG_ERROR, "Error reading from web socket: %s\n", strerror(errno));
				*opcode = AST_WEBSOCKET_OPCODE_CLOSE;
				session->closing = 1;
				ao2_unlock(session);
				return -1;
			}

			if (!--sanity) {
				ast_log(LOG_WARNING, "Websocket seems unresponsive, disconnecting ...\n");
				*opcode = AST_WEBSOCKET_OPCODE_CLOSE;
				session->closing = 1;
				ao2_unlock(session);
				return -1;
			}
		}
		if (rlen > 0) {
			xlen = xlen - rlen;
			rbuf = rbuf + rlen;
			if (!xlen) {
				break;
			}
		}
		if (ast_iostream_wait_for_input(session->stream, 1000) < 0) {
			ast_log(LOG_ERROR, "ast_iostream_wait_for_input returned err: %s\n", strerror(errno));
			*opcode = AST_WEBSOCKET_OPCODE_CLOSE;
			session->closing = 1;
			ao2_unlock(session);
			return -1;
		}
	}

	ao2_unlock(session);
	return 0;
}

int tcptls_read(struct ast_websocket *session, char **payload, uint64_t *payload_len,
	enum ast_websocket_opcode *opcode, int *fragmented)
{
	int fin = 0;
	int mask_present = 0;
	char *mask = NULL, *new_payload = NULL;
	size_t options_len = 0, frame_size = 0;


	if (ws_safe_read(session, &session->buf[0], MIN_WS_HDR_SZ, opcode)) {
		return -1;
	}
	frame_size += MIN_WS_HDR_SZ;

	/* ok, now we have the first 2 bytes, so we know some flags, opcode and payload length (or whether payload length extension will be required) */
	*opcode = session->buf[0] & 0xf;
	*payload_len = session->buf[1] & 0x7f;
	if (*opcode == AST_WEBSOCKET_OPCODE_TEXT || *opcode == AST_WEBSOCKET_OPCODE_BINARY || *opcode == AST_WEBSOCKET_OPCODE_CONTINUATION ||
	    *opcode == AST_WEBSOCKET_OPCODE_PING || *opcode == AST_WEBSOCKET_OPCODE_PONG  || *opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
		fin = (session->buf[0] >> 7) & 1;
		mask_present = (session->buf[1] >> 7) & 1;

		/* Based on the mask flag and payload length, determine how much more we need to read before start parsing the rest of the header */
		options_len += mask_present ? 4 : 0;
		options_len += (*payload_len == 126) ? 2 : (*payload_len == 127) ? 8 : 0;
		if (options_len) {
			/* read the rest of the header options */
			if (ws_safe_read(session, &session->buf[frame_size], options_len, opcode)) {
				return -1;
			}
			frame_size += options_len;
		}

		if (*payload_len == 126) {
			/* Grab the 2-byte payload length  */
			*payload_len = ntohs(get_unaligned_uint16(&session->buf[2]));
			mask = &session->buf[4];
		} else if (*payload_len == 127) {
			/* Grab the 8-byte payload length  */
			*payload_len = ntohll(get_unaligned_uint64(&session->buf[2]));
			mask = &session->buf[10];
		} else {
			/* Just set the mask after the small 2-byte header */
			mask = &session->buf[2];
		}

		/* Now read the rest of the payload */
		*payload = &session->buf[frame_size]; /* payload will start here, at the end of the options, if any */
		frame_size = frame_size + (*payload_len); /* final frame size is header + optional headers + payload data */
		if (frame_size > AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE) {
			ast_log(LOG_WARNING, "Cannot fit huge websocket frame of %zu bytes\n", frame_size);
			/* The frame won't fit :-( */
			ast_websocket_close(session, 1009);
			return -1;
		}

		if (*payload_len) {
			if (ws_safe_read(session, *payload, *payload_len, opcode)) {
				return -1;
			}
		}

		/* If a mask is present unmask the payload */
		if (mask_present) {
			unsigned int pos;
			for (pos = 0; pos < *payload_len; pos++) {
				(*payload)[pos] ^= mask[pos % 4];
			}
		}

		/* Per the RFC for PING we need to send back an opcode with the application data as received */
		if (*opcode == AST_WEBSOCKET_OPCODE_PING) {
			if (ast_websocket_write(session, AST_WEBSOCKET_OPCODE_PONG, *payload, *payload_len)) {
				ast_websocket_close(session, 1009);
			}
			*payload_len = 0;
			return 0;
		}

		if (ast_ws_handled_pong_or_close(session, *payload, *payload_len, *opcode)) {
			return 0;
		}

		/* Below this point we are handling TEXT, BINARY or CONTINUATION opcodes */
		if (*payload_len) {
			if (!(new_payload = ast_realloc(session->payload, (session->payload_len + *payload_len)))) {
				ast_log(LOG_WARNING, "Failed allocation: %p, %zu, %"PRIu64"\n",
					session->payload, session->payload_len, *payload_len);
				*payload_len = 0;
				ast_websocket_close(session, 1009);
				return -1;
			}

			session->payload = new_payload;
			memcpy((session->payload + session->payload_len), (*payload), (*payload_len));
			session->payload_len += *payload_len;
		} else if (!session->payload_len && session->payload) {
			ast_free(session->payload);
			session->payload = NULL;
		}

		if (!fin && session->reconstruct && (session->payload_len < session->reconstruct)) {
			/* If this is not a final message we need to defer returning it until later */
			if (*opcode != AST_WEBSOCKET_OPCODE_CONTINUATION) {
				session->opcode = *opcode;
			}
			*opcode = AST_WEBSOCKET_OPCODE_CONTINUATION;
			*payload_len = 0;
			*payload = NULL;
		} else {
			if (*opcode == AST_WEBSOCKET_OPCODE_CONTINUATION) {
				if (!fin) {
					/* If this was not actually the final message tell the user it is fragmented so they can deal with it accordingly */
					*fragmented = 1;
				} else {
					/* Final frame in multi-frame so push up the actual opcode */
					*opcode = session->opcode;
				}
			}
			*payload_len = session->payload_len;
			*payload = session->payload;
			session->payload_len = 0;
		}
	} else {
		ast_log(LOG_WARNING, "WebSocket unknown opcode %u\n", *opcode);
		/* We received an opcode that we don't understand, the RFC states that 1003 is for a type of data that can't be accepted... opcodes
		 * fit that, I think. */
		ast_websocket_close(session, 1003);
	}

	return 0;
}

int tcptls_set_nonblock(struct ast_websocket *session)
{
	ast_iostream_nonblock(session->stream);
	ast_iostream_set_exclusive_input(session->stream, 0);
	return 0;
}

int tcptls_write(struct ast_websocket *session, enum ast_websocket_opcode opcode,
	char *payload, uint64_t payload_size)
{
	size_t header_size = 2; /* The minimum size of a websocket frame is 2 bytes */
	char *frame;
	uint64_t length;
	uint64_t frame_size;

	if (payload_size < 126) {
		length = payload_size;
	} else if (payload_size < (1 << 16)) {
		length = 126;
		/* We need an additional 2 bytes to store the extended length */
		header_size += 2;
	} else {
		length = 127;
		/* We need an additional 8 bytes to store the really really extended length */
		header_size += 8;
	}

	if (session->client) {
		/* Additional 4 bytes for the client masking key */
		header_size += 4;
	}

	frame_size = header_size + payload_size;

	frame = ast_alloca(frame_size + 1);
	memset(frame, 0, frame_size + 1);

	frame[0] = opcode | 0x80;
	frame[1] = length;

	/* Use the additional available bytes to store the length */
	if (length == 126) {
		put_unaligned_uint16(&frame[2], htons(payload_size));
	} else if (length == 127) {
		put_unaligned_uint64(&frame[2], htonll(payload_size));
	}

	memcpy(&frame[header_size], payload, payload_size);

	websocket_mask_payload(session, frame, &frame[header_size], payload_size);

	ao2_lock(session);
	if (session->closing) {
		ao2_unlock(session);
		return -1;
	}

	ast_iostream_set_timeout_sequence(session->stream, ast_tvnow(), session->timeout);
	if (ast_iostream_write(session->stream, frame, frame_size) != frame_size) {
		ao2_unlock(session);
		/* 1011 - server terminating connection due to not being able to fulfill the request */
		ast_debug(1, "Closing WS with 1011 because we can't fulfill a write request\n");
		ast_ws_close(session, 1011, 0, 1);
		return -1;
	}

	ast_iostream_set_timeout_disable(session->stream);
	ao2_unlock(session);

	return 0;
}


