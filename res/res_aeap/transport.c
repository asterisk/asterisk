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

#include "asterisk/utils.h"

#include "logger.h"
#include "transport.h"
#include "transport_websocket.h"

struct aeap_transport *aeap_transport_create(const char *type)
{
	struct aeap_transport *transport = NULL;

	if (!strncasecmp(type, "ws", 2)) {
		transport = (struct aeap_transport *)aeap_transport_websocket_create();
	}

	if (!transport) {
		ast_log(LOG_ERROR, "AEAP transport: failed to create for type '%s'\n", type);
		return NULL;
	}

	ast_mutex_init(&transport->read_lock);
	ast_mutex_init(&transport->write_lock);

	transport->connected = 0;

	return transport;
}

int aeap_transport_connect(struct aeap_transport *transport, const char *url,
	const char *protocol, int timeout)
{
	int res;

	SCOPED_MUTEX(rlock, &transport->read_lock);
	SCOPED_MUTEX(wlock, &transport->write_lock);

	if (aeap_transport_is_connected(transport)) {
		return 0;
	}

	res = transport->vtable->connect(transport, url, protocol, timeout);
	if (!res) {
		transport->connected = 1;
	}

	return res;
}

struct aeap_transport *aeap_transport_create_and_connect(const char *type,
	const char *url, const char *protocol, int timeout)
{
	struct aeap_transport *transport = aeap_transport_create(type);

	if (!transport) {
		return NULL;
	}

	if (aeap_transport_connect(transport, url, protocol, timeout)) {
		aeap_transport_destroy(transport);
		return NULL;
	}

	return transport;
}

int aeap_transport_is_connected(struct aeap_transport *transport)
{
	/*
	 * Avoid using a lock to 'read' the 'connected' variable in order to
	 * keep things slightly more efficient.
	 */
	return ast_atomic_fetch_add(&transport->connected, 0, __ATOMIC_RELAXED);
}

int aeap_transport_disconnect(struct aeap_transport *transport)
{
	int res;

	SCOPED_MUTEX(rlock, &transport->read_lock);
	SCOPED_MUTEX(wlock, &transport->write_lock);

	if (!aeap_transport_is_connected(transport)) {
		return 0;
	}

	res = transport->vtable->disconnect(transport);

	/*
	 * Even though the transport is locked here use atomics to set the value of
	 * 'connected' since it's possible the variable is being 'read' by another
	 * thread via the 'is_connected' call.
	 */
	ast_atomic_fetch_sub(&transport->connected, 1, __ATOMIC_RELAXED);

	return res;
}

void aeap_transport_destroy(struct aeap_transport *transport)
{
	if (!transport) {
		return;
	}

	/* Ensure an orderly disconnect occurs before final destruction */
	aeap_transport_disconnect(transport);

	transport->vtable->destroy(transport);

	ast_mutex_destroy(&transport->read_lock);
	ast_mutex_destroy(&transport->write_lock);

	ast_free(transport);
}

intmax_t aeap_transport_read(struct aeap_transport *transport, void *buf, intmax_t size,
	enum AST_AEAP_DATA_TYPE *rtype)
{
	SCOPED_MUTEX(lock, &transport->read_lock);

	if (!aeap_transport_is_connected(transport)) {
		return 0;
	}

	return transport->vtable->read(transport, buf, size, rtype);
}

intmax_t aeap_transport_write(struct aeap_transport *transport, const void *buf, intmax_t size,
	enum AST_AEAP_DATA_TYPE wtype)
{
	SCOPED_MUTEX(lock, &transport->write_lock);

	if (!aeap_transport_is_connected(transport)) {
		return 0;
	}

	return transport->vtable->write(transport, buf, size, wtype);
}
