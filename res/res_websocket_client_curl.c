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

/*** MODULEINFO
	<depend>HAVE_CURL_WEBSOCKETS</depend>
	<depend>curl</depend>
	<depend>res_curl</depend>
	<depend>res_http_websocket</depend>
	<support_level>core</support_level>
 ***/


#include "asterisk.h"

#include "res_http_websocket/websocket_private.h"

#include "asterisk/module.h"
#include "asterisk/http.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/file.h"
#include "asterisk/sched.h"
#include "asterisk/unaligned.h"
#include "asterisk/uri.h"
#include "asterisk/uuid.h"

static struct ast_websocket *curl_client_create(struct ast_websocket *session,
	struct ast_websocket_client_options *options, enum ast_websocket_result *result);
static enum ast_websocket_result curl_client_connect(struct ast_websocket *ws,
	struct ast_websocket_client_options *options);
static int curl_close(struct ast_websocket *session, uint16_t reason, int is_reply, int force);
static int curl_read(struct ast_websocket *session, char **payload, uint64_t *payload_len,
	enum ast_websocket_opcode *opcode, int *fragmented);
static int curl_write(struct ast_websocket *session, enum ast_websocket_opcode opcode,
	char *payload, uint64_t payload_size);
static int curl_wait_for_input(struct ast_websocket *session, int timeout);
static int curl_set_timeout(struct ast_websocket *session, int timeout);
static int curl_set_nonblock(struct ast_websocket *session);
static int curl_get_fd(struct ast_websocket *session);
static void curl_data_cleanup(void *data);

struct curl_data {
	CURL *handle;
	curl_socket_t fd;
};

/*
 * Curl functions for the client virtual function table
 */

struct client_vtbl curl_client_vtbl = {
	.supports_proxy = 1,
	.create = curl_client_create,
	.connect = curl_client_connect,
	.close = curl_close,
	.read = curl_read,
	.write = curl_write,
	.wait_for_input = curl_wait_for_input,
	.set_timeout = curl_set_timeout,
	.set_nonblock = curl_set_nonblock,
	.get_fd = curl_get_fd,
	.data_cleanup = curl_data_cleanup,
};

static void curl_data_cleanup(void *data)
{
	struct curl_data *cd = data;
	if (!cd) {
		return;
	}
	if (cd->handle) {
		curl_easy_cleanup(cd->handle);
	}
	cd->fd = -1;
	ast_free(cd);
}

#define CURL_SETOPT_STRING(_handle, _opt, _value) \
({ \
	CURLcode result = CURLE_OK; \
	if (!ast_strlen_zero(_value)) { \
		ast_debug(3, "Setting %s = %s\n", __stringify(_opt), _value); \
		result = curl_easy_setopt(_handle, _opt, _value); \
		if (result != CURLE_OK) { \
			ast_log(LOG_ERROR, "Unable to set option %s:%s %s\n", __stringify(_opt), _value, curl_easy_strerror(result)); \
		} \
	} \
	result; \
})

#define CURL_SETOPT_LONG(_handle, _opt, _value) \
({ \
	CURLcode result = CURLE_OK; \
	ast_debug(3, "Setting %s = %ld\n", __stringify(_opt), (long)_value); \
	result = curl_easy_setopt(_handle, _opt, _value); \
	if (result != CURLE_OK) { \
		ast_log(LOG_ERROR, "Unable to set option %s:%s %s\n", __stringify(_opt),  __stringify(_value), curl_easy_strerror(result)); \
	} \
	result; \
})

#define CURL_SETOPT_PTR(_handle, _opt, _value) \
({ \
	CURLcode result = CURLE_OK; \
	ast_debug(3, "Setting %s = %s\n", __stringify(_opt), __stringify(_value)); \
	result = curl_easy_setopt(_handle, _opt, _value); \
	if (result != CURLE_OK) { \
		ast_log(LOG_ERROR, "Unable to set option %s:%s %s\n", __stringify(_opt),  __stringify(_value), curl_easy_strerror(result)); \
	} \
	result; \
})

static struct ast_websocket * curl_client_create(struct ast_websocket *session,
	struct ast_websocket_client_options *options, enum ast_websocket_result *result)
{
	struct curl_data *cd = NULL;
	SCOPE_ENTER(2, "%s: Client creating\n", options->uri);

	cd = ast_calloc(1, sizeof(*cd));
	if (!cd) {
		*result = WS_ALLOCATE_ERROR;
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Failed to initialize curl data\n", options->uri);
	}
	session->client->data = cd;

	cd->handle = curl_easy_init();
	if (!cd->handle) {
		*result = WS_ALLOCATE_ERROR;
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Failed to initialize curl handle\n", options->uri);
	}

	CURL_SETOPT_STRING(cd->handle, CURLOPT_URL, options->uri);

	CURL_SETOPT_LONG(cd->handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
	CURL_SETOPT_STRING(cd->handle, CURLOPT_USERNAME, options->username);
	CURL_SETOPT_STRING(cd->handle, CURLOPT_PASSWORD, options->password);

	CURL_SETOPT_LONG(cd->handle, CURLOPT_PROXYAUTH, (long)CURLAUTH_BASIC);
	CURL_SETOPT_STRING(cd->handle, CURLOPT_PROXY, options->proxy_uri);
	CURL_SETOPT_STRING(cd->handle, CURLOPT_PROXYUSERNAME, options->proxy_username);
	CURL_SETOPT_STRING(cd->handle, CURLOPT_PROXYPASSWORD, options->proxy_password);
	CURL_SETOPT_LONG(cd->handle, CURLOPT_HTTPPROXYTUNNEL, options->proxy_force_tunnel);

	if (options->tls_cfg) {
		session->secure = 1;
		CURL_SETOPT_LONG(cd->handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);

		CURL_SETOPT_STRING(cd->handle, CURLOPT_CAINFO, options->tls_cfg->cafile);
		CURL_SETOPT_STRING(cd->handle, CURLOPT_CAPATH, options->tls_cfg->capath);

		CURL_SETOPT_STRING(cd->handle, CURLOPT_SSLCERT, options->tls_cfg->certfile);
		CURL_SETOPT_STRING(cd->handle, CURLOPT_SSLKEY, options->tls_cfg->pvtfile);

		CURL_SETOPT_LONG(cd->handle, CURLOPT_SSL_VERIFYHOST,
			ast_test_flag(&options->tls_cfg->flags, AST_SSL_IGNORE_COMMON_NAME) ? 0L : 2L);

		CURL_SETOPT_LONG(cd->handle, CURLOPT_SSL_VERIFYPEER,
			ast_test_flag(&options->tls_cfg->flags, AST_SSL_DONT_VERIFY_SERVER) ? 0L : 2L);

	}

	if (options->timeout > 0) {
		CURL_SETOPT_LONG(cd->handle, CURLOPT_CONNECTTIMEOUT_MS, options->timeout);
		CURL_SETOPT_LONG(cd->handle, CURLOPT_TIMEOUT_MS, options->timeout);
	}

	/* This tells curl we'll be managing the data transfer after the negotiation */
	CURL_SETOPT_LONG(cd->handle, CURLOPT_CONNECT_ONLY, 2L);
	*result = WS_OK;

	SCOPE_EXIT_RTN_VALUE(session, "%s: Client created\n", options->uri);
}

static size_t curl_header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
	size_t total = size * nitems;
	struct ast_websocket *ws = userdata;

	if (strncmp(buffer, "Sec-WebSocket-Protocol:", 23) == 0) {
		int value_len = total - 23;
		char value[value_len + 1];

		ast_copy_string(value, buffer + 23, value_len);
		ws->client->accept_protocol = ast_strdup(ast_strip(value));
	}

	return total;
}

static enum ast_websocket_result curl_client_connect(struct ast_websocket *session,
	struct ast_websocket_client_options *options)
{
	struct curl_data *cd = session->client->data;
	CURLcode res;
	char *remote_addr = NULL;
	struct curl_slist *slist = NULL;
	SCOPE_ENTER(2, "%s: Client connecting\n", options->uri);

	cd->fd = -1;
	if (!ast_strlen_zero(options->protocols)) {
		const char *header_name = "Sec-WebSocket-Protocol: ";
		char header[strlen(header_name) + strlen(options->protocols) + 1];

		sprintf(header, "%s%s", header_name, options->protocols); /* Safe */
		slist = curl_slist_append(NULL, header);
		CURL_SETOPT_PTR(cd->handle, CURLOPT_HTTPHEADER, slist);
	}

	CURL_SETOPT_PTR(cd->handle, CURLOPT_HEADERFUNCTION, curl_header_callback);
	CURL_SETOPT_PTR(cd->handle, CURLOPT_HEADERDATA , session);

	res = curl_easy_perform(cd->handle);

	curl_slist_free_all(slist); /* This function is NULL safe. */

	if(res != CURLE_OK) {
		cd->fd = -1;
		SCOPE_EXIT_LOG_RTN_VALUE(WS_CLIENT_START_ERROR, LOG_ERROR,
			"%s: Failed to connect: %s\n", options->uri, curl_easy_strerror(res));
	}

	curl_easy_getinfo(cd->handle, CURLINFO_ACTIVESOCKET, &cd->fd);
	curl_easy_getinfo(cd->handle, CURLINFO_PRIMARY_IP, &remote_addr);
	if (!ast_strlen_zero(remote_addr)) {
		ast_sockaddr_parse(&session->remote_address, remote_addr, 0);
	}

	SCOPE_EXIT_RTN_VALUE(WS_OK, "%s: Client connected.  FD: %d\n", options->uri, cd->fd);
}

static unsigned int ast_opcode_to_curl_map[] = {
	[AST_WEBSOCKET_OPCODE_CONTINUATION] = CURLWS_CONT,
	[AST_WEBSOCKET_OPCODE_TEXT] = CURLWS_TEXT,
	[AST_WEBSOCKET_OPCODE_BINARY] = CURLWS_BINARY,
	[AST_WEBSOCKET_OPCODE_CLOSE] = CURLWS_CLOSE,
	[AST_WEBSOCKET_OPCODE_PING] = CURLWS_PING,
	[AST_WEBSOCKET_OPCODE_PONG] = CURLWS_PONG,
};

static unsigned int ast_opcode_to_curl(enum ast_websocket_opcode opcode)
{
	if (opcode < AST_WEBSOCKET_OPCODE_CONTINUATION || opcode > AST_WEBSOCKET_OPCODE_PONG) {
		ast_log(LOG_WARNING, "Unknown ast opcode '0x%02x'\n", opcode);
		return CURLWS_BINARY;
	}
	return ast_opcode_to_curl_map[opcode];
}

static enum ast_websocket_opcode curl_opcode_to_ast(unsigned int curl_opcode, int *continuation)
{
	/*
	 * The type flags are mutually excludive but the CURLWS_CONT
	 * bit can also be set on TEXT and BINARY frames.
	 */

	/* Coerce continuation into a simple 0/1 */
	*continuation = !!(curl_opcode & CURLWS_CONT);

	/* Mask out the CURLWS_CONT bit */
	curl_opcode &= ~(CURLWS_CONT);

	switch(curl_opcode) {
	case CURLWS_TEXT:
		return AST_WEBSOCKET_OPCODE_TEXT;
	case CURLWS_BINARY:
		return AST_WEBSOCKET_OPCODE_BINARY;
	case CURLWS_CLOSE:
		return AST_WEBSOCKET_OPCODE_CLOSE;
	case CURLWS_PING:
		return AST_WEBSOCKET_OPCODE_PING;
	case CURLWS_PONG:
		return AST_WEBSOCKET_OPCODE_PONG;
	default:
		ast_log(LOG_WARNING, "Unknown curl opcode '0x%02x'\n", curl_opcode);
		return AST_WEBSOCKET_OPCODE_UNKNOWN;
	}
}

static int curl_close(struct ast_websocket *session, uint16_t reason, int is_reply, int force)
{
	struct curl_data *cd = session->client->data;
	CURLcode res = CURLE_OK;
	size_t bytes_sent = 0;
	char frame[2] = { 0, };
	SCOPE_ENTER(2, "%s: Close requested.  Reason: %d Is reply: %s  Force: %s\n", WS_SESSION_REMOTE(session),
		reason, AST_YESNO(is_reply), AST_YESNO(force));

	ao2_lock(session);

	if (!cd->handle || cd->fd < 0) {
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(0, "%s: WebSocket connection already closed\n", WS_SESSION_REMOTE(session));
	}

	if (force) {
		ast_trace(-1, "%s: Forcing close. Handle: %p FD: %d\n", WS_SESSION_REMOTE(session),
			cd->handle, cd->fd);
		if (cd->fd >= 0) {
			ast_trace(-1, "%s: Shutting down and closing socket FD: %d\n", WS_SESSION_REMOTE(session),
				cd->fd);
			shutdown(cd->fd, SHUT_RDWR);
			close(cd->fd);
			cd->fd = -1;
		}
		if (cd->handle) {
			ast_trace(-1, "%s: Cleaning up curl handle: %p\n", WS_SESSION_REMOTE(session),
				cd->handle);
			curl_easy_cleanup(cd->handle);
			cd->handle = NULL;
		}
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Forced close\n", WS_SESSION_REMOTE(session));
	}

	put_unaligned_uint16(&frame, htons(reason ? reason : 1000));

	ast_trace(-1, "%s: Writing %sCLOSE frame with reason %d.  fd: %d\n", WS_SESSION_REMOTE(session),
		is_reply ? "reply " : "", reason, cd->fd);
	res = curl_ws_send(cd->handle, frame, 2, &bytes_sent, 0, CURLWS_CLOSE);
	session->close_sent = 1;
	if (is_reply || res != CURLE_OK || bytes_sent != 2) {
		if (is_reply) {
			ast_trace(-1, "%s: Writing CLOSE reply. Closing socket.  fd: %d\n",
				WS_SESSION_REMOTE(session), cd->fd);
		} else {
			ast_trace(-1, "%s: Writing CLOSE failed.  Forcing close.  fd: %d\n",
				WS_SESSION_REMOTE(session), cd->fd);
		}
		curl_close(session, reason, 0, 1);
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Socket closed after %s\n", WS_SESSION_REMOTE(session),
			is_reply ? "reply" : "write failure");
	}
	ao2_unlock(session);
	SCOPE_EXIT_RTN_VALUE(1, "%s: Close done\n", session->client->options->uri);
}

static int curl_write(struct ast_websocket *session, enum ast_websocket_opcode opcode,
	char *payload, uint64_t payload_size)
{
	struct curl_data *cd = session->client->data;
	CURLcode res;
	size_t bytes_sent  =0;
	unsigned int flags = 0;

	flags = ast_opcode_to_curl(opcode);

	ao2_lock(session);
	if (session->closing || !cd->handle || cd->fd < 0) {
		ao2_unlock(session);
		return -1;
	}

	res = curl_ws_send(cd->handle, payload, payload_size, &bytes_sent, 0, flags);
	if (res != CURLE_OK) {
		/* 1011 - server terminating connection due to not being able to fulfill the request */
		ast_log(LOG_WARNING, "%s: Write failed: '%s' Closing websocket\n",
			session->client->options->uri, curl_easy_strerror(res));
		ast_ws_close(session, 1011, 1, 1);
	}
	ao2_unlock(session);

	return res == CURLE_OK ? 0 : -1;
}

static int curl_read(struct ast_websocket *session, char **payload, uint64_t *payload_len,
	enum ast_websocket_opcode *opcode, int *fragmented)
{
	struct curl_data *cd = session->client->data;
	CURLcode res = CURLE_OK;
	size_t bytes_read = 0;
	size_t frame_length = 0;
	int continuation = 0;
#ifdef HAVE_CURL_CONST_META
	const struct curl_ws_frame *meta = NULL;
#else
	struct curl_ws_frame *meta = NULL;
#endif
	ast_debug(4, "%s: Starting read\n", session->client->options->uri);

	if (!cd->handle || cd->fd < 0) {
		ast_debug(4, "%s: Socket closed\n", session->client->options->uri);
		return -1;
	}

	if (!session->non_blocking) {
		ast_debug(4, "%s: Waiting for input\n", session->client->options->uri);
		ast_wait_for_input(cd->fd, -1);
		ast_debug(4, "%s: Wait returned\n", session->client->options->uri);
	}
	if (!cd->handle || cd->fd < 0) {
		ast_debug(4, "%s: Socket closed\n", session->client->options->uri);
		return -1;
	}

	/* Find out what's available */
	ast_debug(4, "%s: Checking for availability\n", session->client->options->uri);
	res = curl_ws_recv(cd->handle, NULL, 0, &bytes_read, &meta);
	ast_debug(4, "%s: Read result: %s\n", session->client->options->uri, curl_easy_strerror(res));
	if (res == CURLE_AGAIN) {
		ast_debug(4, "%s: Returning AGAIN\n", session->client->options->uri);
		return 0;
	}
	if (res != CURLE_OK || !meta) {
		ast_log(LOG_WARNING, "%s: Read failed: %s\n", session->client->options->uri, curl_easy_strerror(res));
		return -1;
	}
	*opcode = curl_opcode_to_ast(meta->flags, &continuation);
	*fragmented = continuation;

	ast_debug(4, "%s: OC: %s br: %d  offset: %d  bylesleft: %d  len: %d\n", session->client->options->uri,
		ast_ws_opcode2str(*opcode), (int)bytes_read, (int)meta->offset, (int)meta->bytesleft, (int)meta->len);

	if (meta->bytesleft == 0) {
		/* It's a zero-length message */
		ast_free(session->payload);
		session->payload = NULL;
		*payload = NULL;
		*payload_len = 0;
		ast_ws_handled_pong_or_close(session, NULL, 0, *opcode);
		return 0;
	}

	frame_length = meta->bytesleft;
	session->payload = ast_realloc(session->payload, frame_length);
	while (res == CURLE_OK) {
		size_t bytes_copied = 0;
		res = curl_ws_recv(cd->handle, session->payload + bytes_read,
			frame_length - bytes_read, &bytes_copied, &meta);

		if (meta) {
			ast_debug(4, "%s: OC: %s copied: %d offset: %d  bylesleft: %d  len: %d\n", session->client->options->uri,
				ast_ws_opcode2str(*opcode), (int)bytes_copied, (int)meta->offset, (int)meta->bytesleft, (int)meta->len);
		} else {
			ast_debug(4, "%s: Read result: %s\n", session->client->options->uri, curl_easy_strerror(res));
		}

		bytes_read += bytes_copied;
		if (res == CURLE_OK && meta->bytesleft == 0) {
			ast_debug(4, "%s: Read done\n", session->client->options->uri);
			break;
		}
		if (res == CURLE_AGAIN) {
			ast_debug(4, "%s: AGAIN\n", session->client->options->uri);
			ast_wait_for_input(cd->fd, -1);
			ast_debug(4, "%s: Wait returned\n", session->client->options->uri);
			if (!cd->handle || cd->fd < 0) {
				ast_debug(4, "%s: Socket closed\n", session->client->options->uri);
				res = CURLE_GOT_NOTHING;
				break;
			}
			res = CURLE_OK;
		}
	}
	if (res != CURLE_OK || bytes_read != frame_length) {
		ast_free(session->payload);
		session->payload = NULL;
		ast_log(LOG_WARNING, "%s: oops.  Bytes read: %d  Expected: %d : %s\n",
			session->client->options->uri, (int)bytes_read, (int)frame_length, curl_easy_strerror(res));
		return -1;
	}
	*payload = session->payload;
	*payload_len = frame_length;
	ast_ws_handled_pong_or_close(session, session->payload, frame_length, *opcode);

	ast_debug(4, "Read websocket %s frame, length %" PRIu64 "\n",
		ast_ws_opcode2str(*opcode), frame_length);

	return 0;
}

static int curl_get_fd(struct ast_websocket *session)
{
	struct curl_data *cd = session->client->data;
	return session->closing ? -1 : (int)cd->fd;
}

static int curl_wait_for_input(struct ast_websocket *session, int timeout)
{
	struct curl_data *cd = session->client->data;
	return session->closing ? -1 : ast_wait_for_input(cd->fd, timeout);
}

static int curl_set_timeout(struct ast_websocket *session, int timeout)
{
	struct curl_data *cd = session->client->data;
	session->timeout = timeout;
	CURL_SETOPT_LONG(cd->handle, CURLOPT_CONNECTTIMEOUT_MS, timeout);
	CURL_SETOPT_LONG(cd->handle, CURLOPT_TIMEOUT_MS, timeout);
	return 0;
}

static int curl_set_nonblock(struct ast_websocket *session)
{
	/* curl websockets are non-blocking by default */
	return 0;
}

static int unload_module(void)
{
	ast_ws_unregister_client_backend(backend_curl);
	return 0;
}

static int load_module(void)
{
	ast_debug(2, "Initializing Websocket Curl Client Configuration\n");
	ast_ws_register_client_backend(backend_curl, curl_client_vtbl);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "WebSocket Curl Client Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_curl,res_http_websocket",
);
