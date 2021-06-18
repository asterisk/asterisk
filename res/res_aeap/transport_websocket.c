/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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

#include "asterisk/http_websocket.h"
#include "asterisk/utils.h"

#include "logger.h"
#include "transport.h"
#include "transport_websocket.h"

#define log_error(obj, fmt, ...) aeap_error(obj, "websocket", fmt, ##__VA_ARGS__)

struct aeap_transport_websocket {
	/*! Derive from base transport (must be first attribute) */
	struct aeap_transport base;
	/*! The underlying websocket */
	struct ast_websocket *ws;
};

static int websocket_connect(struct aeap_transport *self, const char *url,
	const char *protocol, int timeout)
{
	struct aeap_transport_websocket *transport = (struct aeap_transport_websocket *)self;
	enum ast_websocket_result ws_result;
	struct ast_websocket_client_options ws_options = {
		.uri = url,
		.protocols = protocol,
		.timeout = timeout,
		.tls_cfg = NULL,
	};

	transport->ws = ast_websocket_client_create_with_options(&ws_options, &ws_result);
	if (ws_result != WS_OK) {
		log_error(self, "connect failure (%d)", (int)ws_result);
		return -1;
	}

	return 0;
}

static int websocket_disconnect(struct aeap_transport *self)
{
	struct aeap_transport_websocket *transport = (struct aeap_transport_websocket *)self;

	if (transport->ws) {
		ast_websocket_unref(transport->ws);
		transport->ws = NULL;
	}

	return 0;
}

static void websocket_destroy(struct aeap_transport *self)
{
	/*
	 * Disconnect takes care of cleaning up the websocket. Note, disconnect
	 * was called by the base/dispatch interface prior to calling this
	 * function so nothing to do here.
	 */
}

static intmax_t websocket_read(struct aeap_transport *self, void *buf, intmax_t size,
	enum AST_AEAP_DATA_TYPE *rtype)
{
	struct aeap_transport_websocket *transport = (struct aeap_transport_websocket *)self;

	char *payload;
	uint64_t bytes_read = 0;
	uint64_t total_bytes_read = 0;
	enum ast_websocket_opcode opcode;
	int fragmented = 0;

	*rtype = AST_AEAP_DATA_TYPE_NONE;

	if (ast_websocket_fd(transport->ws) < 0) {
		log_error(self, "unavailable for reading");
		/* Ensure this transport is in a disconnected state */
		aeap_transport_disconnect(self);
		return -1;
	}

	/*
	 * This function is called with the read_lock locked. However, the lock needs to be
	 * unlocked while waiting for input otherwise a deadlock can occur during disconnect
	 * (disconnect attempts to grab the lock but can't because read holds it here). So
	 * unlock it prior to waiting.
	 */
	ast_mutex_unlock(&transport->base.read_lock);
	if (ast_websocket_wait_for_input(transport->ws, -1) <= 0) {
		ast_mutex_lock(&transport->base.read_lock);
		log_error(self, "poll failure: %s", strerror(errno));
		/* Ensure this transport is in a disconnected state */
		aeap_transport_disconnect(self);
		return -1;
	}
	ast_mutex_lock(&transport->base.read_lock);

	if (!transport->ws) {
		/*
		 * It's possible the transport was told to disconnect while waiting for input.
		 * If so then the websocket will be NULL, so we don't want to continue.
		 */
		return 0;
	}

	do {
		if (ast_websocket_read(transport->ws, &payload, &bytes_read, &opcode,
				&fragmented) != 0) {
			log_error(self, "read failure (%d): %s", opcode, strerror(errno));
			return -1;
		}

		if (!bytes_read) {
			continue;
		}

		if (total_bytes_read + bytes_read > size) {
			log_error(self, "attempted to read too many bytes into (%jd) sized buffer", size);
			return -1;
		}

		memcpy(buf + total_bytes_read, payload, bytes_read);
		total_bytes_read += bytes_read;

	} while (opcode == AST_WEBSOCKET_OPCODE_CONTINUATION);

	switch (opcode) {
	case AST_WEBSOCKET_OPCODE_CLOSE:
		log_error(self, "closed");
		return -1;
	case AST_WEBSOCKET_OPCODE_BINARY:
		*rtype = AST_AEAP_DATA_TYPE_BINARY;
		break;
	case AST_WEBSOCKET_OPCODE_TEXT:
		*rtype = AST_AEAP_DATA_TYPE_STRING;

		/* Append terminator, but check for overflow first */
		if (total_bytes_read == size) {
			log_error(self, "unable to write string terminator");
			return -1;
		}

		*((char *)(buf + total_bytes_read)) = '\0';
		break;
	default:
		/* Ignore all other message types */
		return 0;
	}

	return total_bytes_read;
}

static intmax_t websocket_write(struct aeap_transport *self, const void *buf, intmax_t size,
	enum AST_AEAP_DATA_TYPE wtype)
{
	struct aeap_transport_websocket *transport = (struct aeap_transport_websocket *)self;
	intmax_t res = 0;

	switch (wtype) {
	case AST_AEAP_DATA_TYPE_BINARY:
		res = ast_websocket_write(transport->ws, AST_WEBSOCKET_OPCODE_BINARY,
			(char *)buf, size);
		break;
	case AST_AEAP_DATA_TYPE_STRING:
		res = ast_websocket_write(transport->ws, AST_WEBSOCKET_OPCODE_TEXT,
			(char *)buf, size);
		break;
	default:
		break;
	}

	if (res < 0) {
		log_error(self, "problem writing to websocket (closed)");

		/*
		 * If the underlying socket is closed then ensure the
		 * transport is in a disconnected state as well.
		 */
		aeap_transport_disconnect(self);

		return res;
	}

	return size;
}

static struct aeap_transport_vtable *transport_websocket_vtable(void)
{
	static struct aeap_transport_vtable websocket_vtable = {
		.connect = websocket_connect,
		.disconnect = websocket_disconnect,
		.destroy = websocket_destroy,
		.read = websocket_read,
		.write = websocket_write,
	};

	return &websocket_vtable;
}

/*!
 * \brief Initialize a transport websocket object, and set its virtual table
 *
 * \param transport The transport to initialize
 *
 * \returns 0 on success, -1 on error
 */
static int transport_websocket_init(struct aeap_transport_websocket *transport)
{
	transport->ws = NULL;

	((struct aeap_transport *)transport)->vtable = transport_websocket_vtable();

	return 0;
}

struct aeap_transport_websocket *aeap_transport_websocket_create(void)
{
	struct aeap_transport_websocket *transport;

	transport = ast_calloc(1, sizeof(*transport));
	if (!transport) {
		ast_log(LOG_ERROR, "AEAP websocket: unable to create transport websocket");
		return NULL;
	}

	if (transport_websocket_init(transport)) {
		ast_free(transport);
		return NULL;
	}

	return transport;
}
