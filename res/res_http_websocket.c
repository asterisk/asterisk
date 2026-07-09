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
#include "asterisk/http_websocket.h"

/*! \brief GUID used to compute the accept key, defined in the specifications */
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*! \brief Length of a websocket's client key */
#define CLIENT_KEY_SIZE 16

/*! \brief Number of buckets for registered protocols */
#define MAX_PROTOCOL_BUCKETS 7

#ifdef LOW_MEMORY
/*! \brief Default reconstruction size for multi-frame payload reconstruction. If exceeded the next frame will start a
 *         payload.
 */
#define DEFAULT_RECONSTRUCTION_CEILING AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE

/*! \brief Maximum reconstruction size for multi-frame payload reconstruction. */
#define MAXIMUM_RECONSTRUCTION_CEILING AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE
#else
/*! \brief Default reconstruction size for multi-frame payload reconstruction. If exceeded the next frame will start a
 *         payload.
 */
#define DEFAULT_RECONSTRUCTION_CEILING AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE

/*! \brief Maximum reconstruction size for multi-frame payload reconstruction. */
#define MAXIMUM_RECONSTRUCTION_CEILING AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE
#endif

/*! \brief Maximum size of a websocket frame header
 * 1 byte flags and opcode
 * 1 byte mask flag + payload len
 * 8 bytes max extended length
 * 4 bytes optional masking key
 * ... payload follows ...
 * */
#define MAX_WS_HDR_SZ 14
#define MIN_WS_HDR_SZ 2

/*! \brief WS_PING_PAYLOAD
 * It's possible that a user of this API could be sending their own PINGs
 * and expecting to see PONGs so we use the PING_PAYLOAD in the PINGs we
 * send so we can detect that any PONGs we receive are from our own PINGs.
 */
#define WS_PING_PAYLOAD "WS_CLIENT_PING"
#define WS_PING_PAYLOAD_LEN 14

static struct ast_sched_context *ping_scheduler;

enum ws_closed_by {
	WS_NOT_CLOSED = 0,
	WS_CLOSED_BY_REMOTE,
	WS_CLOSED_BY_US,
};

struct websocket_client {
	/*! Options used to create the client */
	struct ast_websocket_client_options *options;
	/*! host portion of client uri */
	char *host;
	/*! path for logical websocket connection */
	struct ast_str *resource_name;
	/*! unique key used during server handshaking */
	char *key;
	/*! container for registered protocols */
	char *protocols;
	/*! the protocol accepted by the server */
	char *accept_protocol;
	/*! websocket protocol version */
	int version;
	/*! tcptls connection arguments */
	struct ast_tcptls_session_args *args;
	/*! tcptls connection instance */
	struct ast_tcptls_session_instance *ser;
	/*! Authentication userid:password */
	char *userinfo;
	/*! Suppress connection log messages */
	int suppress_connection_msgs;
	/*! Proxy-Authentication userid:password */
	char *proxy_userinfo;
	/*! The ping scheduler timer id */
	int ping_sched_timer;
	/*! How many missed pong responses currently */
	int missed_pong_count;
};

/*! \brief Structure definition for session */
struct ast_websocket {
	struct ast_iostream *stream;        /*!< iostream of the connection */
	struct ast_sockaddr remote_address; /*!< Address of the remote client */
	struct ast_sockaddr local_address;  /*!< Our local address */
	enum ast_websocket_opcode opcode;   /*!< Cached opcode for multi-frame messages */
	size_t payload_len;                 /*!< Length of the payload */
	char *payload;                      /*!< Pointer to the payload */
	size_t reconstruct;                 /*!< Number of bytes before a reconstructed payload will be returned and a new one started */
	int timeout;                        /*!< The timeout for operations on the socket */
	unsigned int secure:1;              /*!< Bit to indicate that the transport is secure */
	unsigned int closing:1;             /*!< Bit to indicate that the session is in the process of being closed */
	unsigned int close_sent:1;          /*!< Bit to indicate that the session close opcode has been sent and no further data will be sent */
	unsigned int non_blocking:1;        /*!< Bit to indicate that the socket is non-blocking */
	struct websocket_client *client;    /*!< Client object when connected as a client websocket */
	char session_id[AST_UUID_STR_LEN];  /*!< The identifier for the websocket session */
	uint16_t close_status_code;         /*!< Status code sent in a CLOSE frame upon shutdown */
	enum ws_closed_by closed_by;        /*!< Who's closing the websocket? */
	char buf[AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE];	    /*!< Fixed buffer for reading data into */
};

#define WS_SESSION_REMOTE(_session) (_session ? (_session->client ? _session->client->options->uri : ast_sockaddr_stringify(&_session->remote_address)) : "NULL")
#define ARE_PINGPONGS_ENABLED(_session) (_session && _session->client && _session->client->options->pingpongs && _session->client->ping_sched_timer >= 0)

static const char *closed_by_str[] = {
	[WS_NOT_CLOSED] = "not closed",
	[WS_CLOSED_BY_REMOTE] = "remote",
	[WS_CLOSED_BY_US] = "local"
};

static const char *closed_by_to_str(enum ws_closed_by closed_by)
{
	if (!ARRAY_IN_BOUNDS(closed_by, closed_by_str)) {
		return "unknown";
	}
	return closed_by_str[closed_by];
}

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

/*! \brief Destructor function for protocols */
static void protocol_destroy_fn(void *obj)
{
	struct ast_websocket_protocol *protocol = obj;
	ast_free(protocol->name);
}

/*! \brief Structure for a WebSocket server */
struct ast_websocket_server {
	struct ao2_container *protocols; /*!< Container for registered protocols */
};

static void websocket_server_dtor(void *obj)
{
	struct ast_websocket_server *server = obj;
	ao2_cleanup(server->protocols);
	server->protocols = NULL;
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

static struct ast_websocket_server *websocket_server_internal_create(void)
{
	return websocket_server_create_impl();
}

struct ast_websocket_server *AST_OPTIONAL_API_NAME(ast_websocket_server_create)(void)
{
	return websocket_server_create_impl();
}

/*! \brief Destructor function for sessions */
static void session_destroy_fn(void *obj)
{
	struct ast_websocket *session = obj;
	char *id = ast_strdupa(WS_SESSION_REMOTE(session));
	SCOPE_ENTER(2, "%s: Session %p destructor\n", id, obj);

	if (session->stream) {
		ast_websocket_close(session, session->close_status_code);
		if (session->stream) {
			ast_iostream_close(session->stream);
			session->stream = NULL;
			ast_trace(-1, "%s: WebSocket connection closed\n", WS_SESSION_REMOTE(session));
		}
	}

	ao2_cleanup(session->client);
	ast_free(session->payload);
	SCOPE_EXIT_RTN("%s; Session %p destructor complete\n", id, obj);
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

static const char *opcode_map[] = {
	[AST_WEBSOCKET_OPCODE_CONTINUATION] = "continuation",
	[AST_WEBSOCKET_OPCODE_TEXT] = "text",
	[AST_WEBSOCKET_OPCODE_BINARY] = "binary",
	[AST_WEBSOCKET_OPCODE_CLOSE] = "close",
	[AST_WEBSOCKET_OPCODE_PING] = "ping",
	[AST_WEBSOCKET_OPCODE_PONG] = "pong",
};

static const char *websocket_opcode2str(enum ast_websocket_opcode opcode)
{
	if (!ARRAY_IN_BOUNDS(opcode, opcode_map)) {
		return "<unknown>";
	}
	return opcode_map[opcode];
}

static void ping_scheduler_cancel(struct ast_websocket *session)
{
	int enabled = ARE_PINGPONGS_ENABLED(session);
	SCOPE_ENTER(2, "%s: Cancelling PING/PONG keepalives\n", WS_SESSION_REMOTE(session));

	if (!enabled) {
		SCOPE_EXIT_RTN("%s: Not enabled, cancel not needed\n", WS_SESSION_REMOTE(session));
	}
	AST_SCHED_DEL(ping_scheduler, session->client->ping_sched_timer);
	ao2_ref(session, -1);
	SCOPE_EXIT_RTN("%s: Cancelled PING/PONG keepalives\n", WS_SESSION_REMOTE(session));
}

static int websocket_close(struct ast_websocket *session, uint16_t reason, int force)
{
	enum ast_websocket_opcode opcode = AST_WEBSOCKET_OPCODE_CLOSE;
	/* The header is either 2 or 6 bytes and the
	 * reason code takes up another 2 bytes */
	char frame[8] = { 0, };
	int header_size, fsize, res = 0;
	int fd = session->stream ? ast_iostream_get_fd(session->stream) : -1;
	SCOPE_ENTER(2, "%s: Close requested.  Reason: %s (%d) Closed by: %s Force: %s\n", WS_SESSION_REMOTE(session),
		ast_websocket_status_to_str(reason), reason, closed_by_to_str(session->closed_by), AST_YESNO(force));

	ping_scheduler_cancel(session);

	ao2_lock(session);
	if (session->closing) {
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Close already sent\n", WS_SESSION_REMOTE(session));
	}

	session->closing = 1;

	if (!session->stream) {
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(-1, "%s: WebSocket stream already closed\n", WS_SESSION_REMOTE(session));
	}

	if (force) {
		ast_trace(-1, "%s: Forcing close. Handle: %p FD: %d\n", WS_SESSION_REMOTE(session),
			session->stream, fd);
		ast_iostream_close(session->stream);
		session->stream = NULL;
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Forced close\n", WS_SESSION_REMOTE(session));
	}

	/* clients need space for an additional 4 byte masking key */
	header_size = session->client ? 6 : 2;
	fsize = header_size + 2;
	session->close_sent = 1;

	frame[0] = opcode | 0x80;
	frame[1] = 2; /* The reason code is always 2 bytes */

	/*
	 * If the remote initiated the close we should respond with the same
	 * reason code they sent.
	 */
	if (session->closed_by == WS_CLOSED_BY_REMOTE) {
		reason = session->close_status_code;
	}

	/* If no reason has been specified assume 1000 which is normal closure */
	put_unaligned_uint16(&frame[header_size], htons(reason ? reason : 1000));

	websocket_mask_payload(session, frame, &frame[header_size], 2);
	ast_trace(-1, "%s: Writing %sCLOSE frame with reason %s (%d).  fd: %d\n", WS_SESSION_REMOTE(session),
		session->closed_by == WS_CLOSED_BY_REMOTE ? "reply " : "", ast_websocket_status_to_str(reason), reason, fd);

	ast_iostream_set_timeout_inactivity(session->stream, session->timeout);
	res = ast_iostream_write(session->stream, frame, fsize);
	ast_iostream_set_timeout_disable(session->stream);

	/*
	 * If the remote initiated the close or we failed to send,
	 * we can close the socket.  If we just sent a CLOSE of our own, we need to wait
	 * until we get the close reply from the remote.
	 */
	if (session->closed_by == WS_CLOSED_BY_REMOTE || res != fsize) {
		ast_trace(-1, "%s: %s Closing socket.  fd: %d\n",
			session->closed_by == WS_CLOSED_BY_REMOTE ? "Wrote CLOSE reply." : "Writing CLOSE failed.",
			WS_SESSION_REMOTE(session), fd);

		ast_iostream_close(session->stream);
		session->stream = NULL;
		ao2_unlock(session);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Socket closed after %s\n", WS_SESSION_REMOTE(session),
			session->closed_by == WS_CLOSED_BY_REMOTE ? "reply" : "write failure");
	}

	ao2_unlock(session);
	SCOPE_EXIT_RTN_VALUE(res == sizeof(frame), "%s: Close done\n", WS_SESSION_REMOTE(session));
}

static int websocket_handled_pong_or_close(struct ast_websocket *session, char *payload, uint64_t payload_len,
	enum ast_websocket_opcode opcode)
{
	SCOPE_ENTER(4, "%s: Opcode: %s\n", WS_SESSION_REMOTE(session), websocket_opcode2str(opcode));

	if (opcode == AST_WEBSOCKET_OPCODE_PONG) {
		/*
		 * If it's from our own PING, reset the missed count.
		 */
		if (session->client->missed_pong_count
			&& payload_len == WS_PING_PAYLOAD_LEN
			&& strncmp(payload, WS_PING_PAYLOAD, payload_len) == 0) {
			int mpc = session->client->missed_pong_count;

			session->client->missed_pong_count = 0;
			SCOPE_EXIT_RTN_VALUE(1, "%s: Received PONG from our own PING. Missed count was: %d.  Cleared.\n",
				WS_SESSION_REMOTE(session), mpc);
		} else {
			SCOPE_EXIT_RTN_VALUE(0, "%s: Received PONG.  Passing up to client.\n", WS_SESSION_REMOTE(session));

		}
	}

	if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
		if (payload_len >= 2) {
			session->close_status_code = ntohs(get_unaligned_uint16(payload));
		}
		if (session->closed_by == WS_NOT_CLOSED) {
			session->closed_by = WS_CLOSED_BY_REMOTE;
			SCOPE_EXIT_RTN_VALUE(1, "%s: Handled CLOSE request by remote with reason %s (%d)\n", WS_SESSION_REMOTE(session),
				ast_websocket_status_to_str(session->close_status_code), session->close_status_code);
		}

		ast_trace(-1, "%s: Received CLOSE response from remote with reason: %s (%d)\n",
				WS_SESSION_REMOTE(session), ast_websocket_status_to_str(session->close_status_code),
				session->close_status_code);
		/*
		 * We got the close response so we can now clean up the socket.
		 */
		websocket_close(session, session->close_status_code, 1);


		SCOPE_EXIT_RTN_VALUE(1, "%s: Handled CLOSE\n", WS_SESSION_REMOTE(session));
	}

	SCOPE_EXIT_RTN_VALUE(0, "%s: Unhandled %s opcode\n", WS_SESSION_REMOTE(session), websocket_opcode2str(opcode));
}

/*! \brief Close function for websocket session */
int AST_OPTIONAL_API_NAME(ast_websocket_close)(struct ast_websocket *session, uint16_t reason)
{
	if (session->closed_by == WS_NOT_CLOSED) {
		session->closed_by = WS_CLOSED_BY_US;
	}

	return websocket_close(session, reason, 0);
}

/*! \brief Write function for websocket traffic */
int AST_OPTIONAL_API_NAME(ast_websocket_write)(struct ast_websocket *session, enum ast_websocket_opcode opcode,
	char *payload, uint64_t payload_size)
{
	size_t header_size = 2; /* The minimum size of a websocket frame is 2 bytes */
	char *frame;
	uint64_t length;
	uint64_t frame_size;
	SCOPE_ENTER(4, "%s: Opcode: %s Length: %"PRIu64"\n",
		WS_SESSION_REMOTE(session), websocket_opcode2str(opcode), payload_size);

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
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Websocket already closing\n", WS_SESSION_REMOTE(session));
	}

	ast_iostream_set_timeout_sequence(session->stream, ast_tvnow(), session->timeout);
	if (ast_iostream_write(session->stream, frame, frame_size) != frame_size) {
		ao2_unlock(session);
		/* 1011 - server terminating connection due to not being able to fulfill the request */
		ast_trace(-1, "%s: Closing WS with 1011 because we can't fulfill a write request\n",
			WS_SESSION_REMOTE(session));
		websocket_close(session, 1011, 1);
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Closed WS with 1011 because we couldn't fulfill a write request\n",
			WS_SESSION_REMOTE(session));
	}

	ast_iostream_set_timeout_disable(session->stream);
	ao2_unlock(session);

	SCOPE_EXIT_RTN_VALUE(0, "%s: Wrote opcode: %s length: %"PRIu64"\n",
		WS_SESSION_REMOTE(session), websocket_opcode2str(opcode), payload_size);
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
	char *id = ast_strdupa(WS_SESSION_REMOTE(session));
	int refcount = session ? ao2_ref(session, 0) : 0;
	SCOPE_ENTER(2, "%s: Reffing.  Refcount: %d\n", id, refcount);
	ao2_ref(session, +1);
	SCOPE_EXIT("%s: Reffed.  Refcount: %d\n", id, session ? refcount - 1 : 0);
}

void AST_OPTIONAL_API_NAME(ast_websocket_unref)(struct ast_websocket *session)
{
	char *id = ast_strdupa(WS_SESSION_REMOTE(session));
	int refcount = session ? ao2_ref(session, 0) : 0;
	SCOPE_ENTER(2, "%s: Unreffing.  Refcount: %d\n", id, refcount);

	ao2_cleanup(session);
	SCOPE_EXIT("%s: Unreffed.  Refcount: %d\n", id, session ? refcount - 1 : 0);
}

int AST_OPTIONAL_API_NAME(ast_websocket_fd)(struct ast_websocket *session)
{
	return session->closing ? -1 : ast_iostream_get_fd(session->stream);
}

int AST_OPTIONAL_API_NAME(ast_websocket_wait_for_input)(struct ast_websocket *session, int timeout)
{
	return session->closing ? -1 : ast_iostream_wait_for_input(session->stream, timeout);
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
	ast_iostream_nonblock(session->stream);
	ast_iostream_set_exclusive_input(session->stream, 0);
	return 0;
}

int AST_OPTIONAL_API_NAME(ast_websocket_set_timeout)(struct ast_websocket *session, int timeout)
{
	session->timeout = timeout;

	return 0;
}

const char * AST_OPTIONAL_API_NAME(ast_websocket_session_id)(struct ast_websocket *session)
{
	return session->session_id;
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

int AST_OPTIONAL_API_NAME(ast_websocket_read)(struct ast_websocket *session, char **payload, uint64_t *payload_len, enum ast_websocket_opcode *opcode, int *fragmented)
{
	int fin = 0;
	int mask_present = 0;
	char *mask = NULL, *new_payload = NULL;
	size_t options_len = 0, frame_size = 0;
	SCOPE_ENTER(4, "%s: Reading\n", WS_SESSION_REMOTE(session));

	*payload = NULL;
	*payload_len = 0;
	*fragmented = 0;

	if (ws_safe_read(session, &session->buf[0], MIN_WS_HDR_SZ, opcode)) {
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Initial ws_safe_read failed\n", WS_SESSION_REMOTE(session));
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
				SCOPE_EXIT_RTN_VALUE(-1, "%s: ws_safe_read of options failed\n", WS_SESSION_REMOTE(session));
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
			/* The frame won't fit :-( */
			ast_websocket_close(session, 1009);
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: Cannot fit huge websocket frame of %zu bytes\n",
				WS_SESSION_REMOTE(session), frame_size);
		}

		if (*payload_len) {
			if (ws_safe_read(session, *payload, *payload_len, opcode)) {
				SCOPE_EXIT_RTN_VALUE(-1, "%s: ws_safe_read of payload failed\n", WS_SESSION_REMOTE(session));
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
			SCOPE_EXIT_RTN_VALUE(0, "%s: PING received.  Sent PONG\n", WS_SESSION_REMOTE(session));
		}

		if (websocket_handled_pong_or_close(session, *payload, *payload_len, *opcode)) {
			SCOPE_EXIT_RTN_VALUE(0, "%s: Handled PONG or CLOSE\n", WS_SESSION_REMOTE(session));
		}

		/* Below this point we are handling TEXT, BINARY or CONTINUATION opcodes */
		if (*payload_len) {
			if (!(new_payload = ast_realloc(session->payload, (session->payload_len + *payload_len)))) {
				*payload_len = 0;
				ast_websocket_close(session, 1009);
				SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: Failed allocation: %p, %zu, %"PRIu64"\n",
					WS_SESSION_REMOTE(session), session->payload, session->payload_len, *payload_len);
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

	SCOPE_EXIT_RTN_VALUE(0, "%s: Read complete.  Opcode: %s  Length: %"PRIu64"\n",
		WS_SESSION_REMOTE(session), websocket_opcode2str(*opcode), *payload_len);
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

static char *websocket_combine_key(const char *key, char *res, int res_size)
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

int AST_OPTIONAL_API_NAME(ast_websocket_uri_cb)(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_vars, struct ast_variable *headers)
{
	struct ast_variable *v;
	const char *upgrade = NULL, *key = NULL, *key1 = NULL, *key2 = NULL, *protos = NULL;
	char *requested_protocols = NULL, *protocol = NULL;
	int version = 0, flags = 1;
	struct ast_websocket_protocol *protocol_handler = NULL;
	struct ast_websocket *session;
	struct ast_websocket_server *server;

	SCOPED_MODULE_USE(ast_module_info->self);

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

		/* Generate the session id */
		if (!ast_uuid_generate_str(session->session_id, sizeof(session->session_id))) {
			ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - failed to generate a session id\n",
				ast_sockaddr_stringify(&ser->remote_address));
			ast_http_error(ser, 500, "Internal Server Error", "Allocation failed");
			ao2_ref(session, -1);
			ao2_ref(protocol_handler, -1);
			return 0;
		}

		if (protocol_handler->session_attempted
		    && protocol_handler->session_attempted(ser, get_vars, headers, session->session_id)) {
			ast_debug(3, "WebSocket connection from '%s' rejected by protocol handler '%s'\n",
				ast_sockaddr_stringify(&ser->remote_address), protocol_handler->name);
			websocket_bad_request(ser);
			ao2_ref(session, -1);
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

/*! \brief Simple echo implementation which echoes received text and binary frames */
static void websocket_echo_callback(struct ast_websocket *session, struct ast_variable *parameters, struct ast_variable *headers)
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

/*! \brief Parse the given uri into a path and remote address.
 *
 * Expected uri form:
 * \verbatim [ws[s]]://<host>[:port][/<path>] \endverbatim
 *
 * The returned host will contain the address and optional port while
 * path will contain everything after the address/port if included.
 */
static int websocket_client_parse_uri(const char *uri, char **host,
	struct ast_str **path, char **userinfo, int proxy)
{
	struct ast_uri *parsed_uri = proxy ? ast_uri_parse_http(uri) : ast_uri_parse_websocket(uri);

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

static struct ast_tcptls_session_args *websocket_client_args_create(struct ast_websocket *ws,
	struct ast_websocket_client_options *options, enum ast_websocket_result *result)
{
	struct ast_sockaddr *addr;
	struct ast_tcptls_session_args *args = ao2_alloc(
		sizeof(*args), websocket_client_args_destroy);
	const char *resolve_host = NULL;

	if (!args) {
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	args->accept_fd = -1;
	args->tls_cfg = options->tls_cfg;
	args->name = "websocket client";

	if (!ast_strlen_zero(ws->client->options->proxy_host)) {
		resolve_host = ws->client->options->proxy_host;
	} else {
		resolve_host = ws->client->host;
	}
	if (!ast_sockaddr_resolve(&addr, resolve_host, 0, 0)) {
		ast_log(LOG_ERROR, "Unable to resolve address %s\n",
			resolve_host);
		ao2_ref(args, -1);
		*result = WS_URI_RESOLVE_ERROR;
		return NULL;
	}
	ast_sockaddr_copy(&args->remote_address, addr);
	ast_free(addr);

	/* We need to save off the hostname but it may contain a port spec */
	snprintf(args->hostname, sizeof(args->hostname),
		"%.*s",
		(int) strcspn(ws->client->host, ":"), ws->client->host);

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

static void websocket_client_destroy(void *obj)
{
	struct websocket_client *client = obj;
	char *id = ast_strdupa(client->options->uri);
	SCOPE_ENTER(2, "%s: Client destructor %p\n", id, obj);

	ao2_cleanup(client->options);
	ao2_cleanup(client->ser);
	ao2_cleanup(client->args);

	ast_free(client->accept_protocol);
	ast_free(client->protocols);
	ast_free(client->key);
	ast_free(client->resource_name);
	ast_free(client->host);
	ast_free(client->userinfo);
	ast_free(client->proxy_userinfo);

	SCOPE_EXIT_RTN("%s: Client destructor complete\n", id);
}

static void client_options_destroy(void *obj)
{
	struct ast_websocket_client_options *clone = obj;
	ast_free((char *)clone->uri);
	ast_free((char *)clone->protocols);
	ast_free((char *)clone->username);
	ast_free((char *)clone->password);
	ast_free((char *)clone->proxy_host);
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
	clone->proxy_host = SAFE_STRDUP_WITH_ERROR_RTN(clone, options->proxy_host);
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

static struct ast_websocket * websocket_client_create(
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
	ws->client->ping_sched_timer = -1;

	ws->client->options = client_options_clone(options);
	if (!ws->client->options) {
		ast_log(LOG_ERROR, "Unable to clone client options\n");
		ao2_ref(ws, -1);
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	if (!(ws->client->key = websocket_client_create_key())) {
		ao2_ref(ws, -1);
		*result = WS_KEY_ERROR;
		return NULL;
	}

	if (websocket_client_parse_uri(
			options->uri, &ws->client->host, &ws->client->resource_name,
			&ws->client->userinfo, 0)) {
		ao2_ref(ws, -1);
		*result = WS_URI_PARSE_ERROR;
		return NULL;
	}
	ast_debug(2, "%s: host: %s resource: %s userinfo: %s\n", options->uri, ws->client->host,
		ws->client->resource_name ? ast_str_buffer(ws->client->resource_name) : "",
		ws->client->userinfo);

	if (ast_strlen_zero(ws->client->userinfo)
		&& !ast_strlen_zero(options->username)
		&& !ast_strlen_zero(options->password)) {
		ast_asprintf(&ws->client->userinfo, "%s:%s", options->username,
			options->password);
	}

	if (!ast_strlen_zero(options->proxy_host)) {
		ast_debug(2, "%s: Proxy host: %s userinfo: %s\n", options->uri, ws->client->options->proxy_host,
			ws->client->proxy_userinfo);

		if (ast_strlen_zero(ws->client->proxy_userinfo)
			&& !ast_strlen_zero(options->proxy_username)
			&& !ast_strlen_zero(options->proxy_password)) {
			ast_asprintf(&ws->client->proxy_userinfo, "%s:%s", options->proxy_username,
				options->proxy_password);
		}

	}

	if (!(ws->client->args = websocket_client_args_create(ws, options, result))) {
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

const char * AST_OPTIONAL_API_NAME(
	ast_websocket_client_accept_protocol)(struct ast_websocket *ws)
{
	return ws->client->accept_protocol;
}

static enum ast_websocket_result websocket_client_handle_response_code(
	struct websocket_client *client, int response_code, int proxy)
{
	if (response_code <= 0) {
		return WS_INVALID_RESPONSE;
	}

	switch (response_code) {
	case 101:
		if (!proxy) {
			return WS_OK;
		}
		break;
	case 200:
		if (proxy) {
			return WS_OK;
		}
		break;
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
			response_code, proxy ? client->options->proxy_host : client->host);
	}
	return WS_INVALID_RESPONSE;
}

static enum ast_websocket_result websocket_client_handshake_get_response(
	struct websocket_client *client, int proxy)
{
	enum ast_websocket_result res;
	char buf[4096];
	char base64[64];
	int has_upgrade = 0;
	int has_connection = 0;
	int has_accept = 0;
	int has_protocol = 0;
	int status_code = 0;
	SCOPE_ENTER(2, "%s: Proxy? %s  Proxy host: %s\n", client->options->uri, AST_YESNO(proxy),
		S_OR(client->options->proxy_host, "N/A"));

	while (ast_iostream_gets(client->ser->stream, buf, sizeof(buf)) <= 0) {
		SCOPE_EXIT_LOG_RTN_VALUE((errno == EINTR || errno == EAGAIN) ? WS_CLIENT_START_ERROR : WS_BAD_STATUS,
			LOG_ERROR, "%s: %s waiting for HTTP status line", client->options->uri,
			(errno == EINTR || errno == EAGAIN) ? "Timeout" : "Error");
	}

	status_code = ast_http_response_status_line(buf, "HTTP/1.1", 101);
	ast_trace(-1, "%s: Status code: %d\n", client->options->uri, status_code);

	res = websocket_client_handle_response_code(client, status_code, proxy);
	if (res != WS_OK) {
		SCOPE_EXIT_RTN_VALUE(res, "%s: HTTP status line result: %d/%s", client->options->uri,
			res, ast_websocket_result_to_str(res));
	}

	/* Ignoring line folding - assuming header field values are contained
	   within a single line */
	while (1) {
		ssize_t len = ast_iostream_gets(client->ser->stream, buf, sizeof(buf));
		char *name, *value;
		int parsed;

		if (len <= 0) {
			if (errno == EINTR || errno == EAGAIN) {
				SCOPE_EXIT_LOG_RTN_VALUE((errno == EINTR || errno == EAGAIN) ? WS_CLIENT_START_ERROR : WS_BAD_STATUS,
					LOG_ERROR, "%s: %s waiting for HTTP header", client->options->uri,
					(errno == EINTR || errno == EAGAIN) ? "Timeout" : "Error");
			} else {
				ast_trace(-1, "%s: Blank line received\n", client->options->uri);
			}
			break;
		}

		parsed = ast_http_header_parse(buf, &name, &value);
		if (parsed < 0) {
			break;
		}

		if (proxy || parsed > 0) {
			continue;
		}

		if (!has_upgrade &&
		    (has_upgrade = ast_http_header_match(
			    name, "upgrade", value, "websocket")) < 0) {
			SCOPE_EXIT_RTN_VALUE(WS_HEADER_MISMATCH);
		} else if (!has_connection &&
			   (has_connection = ast_http_header_match(
				   name, "connection", value, "upgrade")) < 0) {
			SCOPE_EXIT_RTN_VALUE(WS_HEADER_MISMATCH);
		} else if (!has_accept &&
			   (has_accept = ast_http_header_match(
				   name, "sec-websocket-accept", value,
			    websocket_combine_key(
				    client->key, base64, sizeof(base64)))) < 0) {
			SCOPE_EXIT_RTN_VALUE(WS_HEADER_MISMATCH);
		} else if (!has_protocol &&
			   (has_protocol = ast_http_header_match_in(
				   name, "sec-websocket-protocol", value, client->protocols))) {
			if (has_protocol < 0) {
				SCOPE_EXIT_RTN_VALUE(WS_HEADER_MISMATCH);
			}
			client->accept_protocol = ast_strdup(value);
		} else if (!strcasecmp(name, "sec-websocket-extensions")) {
			ast_log(LOG_ERROR, "Extensions received, but not "
				"supported by client\n");
			SCOPE_EXIT_RTN_VALUE(WS_NOT_SUPPORTED);
		}
	}

	if (status_code == 408) {
		res = WS_CLIENT_START_ERROR;
	} else {
		if (proxy) {
			res = WS_OK;
		} else {
			res = has_upgrade && has_connection && has_accept ?
				WS_OK : WS_HEADER_MISSING;
		}
	}

	SCOPE_EXIT_RTN_VALUE(res, "%s: Status code: %d\n", client->options->uri, status_code);
}

static void websocket_client_start_handshake_timer(struct websocket_client *client)
{
	/*
	 * ast_iostream_gets (called above in websocket_client_handshake_get_response) is
	 * a blocking call which means that if the TCP/TLS connection succeeds but the remote doesn't
	 * actually respond to the proxy CONNECT (if proxy is configured) or GET requests, the process
	 * can hang indefinitely and escalate to a deadlock.  To get ast_iostream_gets to timeout,
	 * we need to make the following 3 calls on the iostream.
	 *
	 * Since the write of the CONNECT and/or GET request is included in the timeout, we'll
	 * double the timeout set in the websocket client's "connect_timeout" parameter to give
	 * the server enough time to respond.
	 */
	ast_iostream_nonblock(client->ser->stream);
	ast_iostream_set_exclusive_input(client->ser->stream, 1);
	ast_iostream_set_timeout_sequence(client->ser->stream, ast_tvnow(), client->options->timeout * 2);
}

static void websocket_client_stop_handshake_timer(struct websocket_client *client)
{
	/*
	 * Once the handshake is complete, we need to undo what we did in
	 * websocket_client_start_handshake_timer.
	 */
	ast_iostream_set_timeout_disable(client->ser->stream);
	ast_iostream_set_exclusive_input(client->ser->stream, 0);
	ast_iostream_blocking(client->ser->stream);
}

#define optional_header_spec "%s%s%s"
#define print_optional_header(test, name, value) \
	test ? name : "", \
	test ? value : "", \
	test ? "\r\n" : ""

static enum ast_websocket_result websocket_proxy_handshake(
	struct websocket_client *client)
{
	struct ast_variable *auth_header = NULL;
	enum ast_websocket_result res = WS_OK;
	size_t bytes_written = 0;
	SCOPE_ENTER(2, "%s: Handshaking with proxy %s\n", client->options->uri, client->options->proxy_host);

	if (!ast_strlen_zero(client->proxy_userinfo)) {
		auth_header = ast_http_create_basic_auth_header(client->proxy_userinfo, NULL);
		if (!auth_header) {
			SCOPE_EXIT_LOG_RTN_VALUE(WS_ALLOCATE_ERROR, LOG_ERROR, "Unable to allocate client websocket userinfo\n");
		}
	}

	websocket_client_start_handshake_timer(client);

	bytes_written = ast_iostream_printf(client->ser->stream,
		"CONNECT %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		optional_header_spec
		"Proxy-Connection: Keep-Alive\r\n"
		"\r\n",
		client->host,
		client->host,
		print_optional_header(auth_header, "Proxy-Authorization: ", auth_header->value)
	);

	ast_variables_destroy(auth_header);
	if (bytes_written < 0) {
		websocket_client_stop_handshake_timer(client);
		SCOPE_EXIT_LOG_RTN_VALUE(WS_WRITE_ERROR, LOG_ERROR, "Failed to send handshake\n");
	}

	/* wait for a response before doing anything else */
	res = websocket_client_handshake_get_response(client, 1);
	websocket_client_stop_handshake_timer(client);

	SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_websocket_result_to_str(res));
}

static enum ast_websocket_result websocket_client_handshake(
	struct websocket_client *client)
{
	size_t protocols_len = 0;
	struct ast_variable *auth_header = NULL;
	size_t res;
	SCOPE_ENTER(2, "%s: Handshaking with server\n", client->options->uri);

	if (!ast_strlen_zero(client->userinfo)) {
		auth_header = ast_http_create_basic_auth_header(client->userinfo, NULL);
		if (!auth_header) {
			SCOPE_EXIT_LOG_RTN_VALUE(WS_ALLOCATE_ERROR, LOG_ERROR, "Unable to allocate client websocket userinfo\n");
		}
	}

	protocols_len = client->protocols ? strlen(client->protocols) : 0;

	websocket_client_start_handshake_timer(client);

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
		websocket_client_stop_handshake_timer(client);
		SCOPE_EXIT_LOG_RTN_VALUE(WS_WRITE_ERROR, LOG_ERROR, "Failed to send handshake\n");
	}

	/* wait for a response before doing anything else */
	res = websocket_client_handshake_get_response(client, 0);
	websocket_client_stop_handshake_timer(client);

	SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_websocket_result_to_str(res));
}

static enum ast_websocket_result websocket_client_connect(struct ast_websocket *ws,
	struct ast_websocket_client_options *options)
{
	enum ast_websocket_result res;
	int original_tls_enabled = ws->client->args->tls_cfg ? ws->client->args->tls_cfg->enabled : 0;
	int proxy = !ast_strlen_zero(ws->client->options->proxy_host);
	SCOPE_ENTER(2, "%s: proxy: %s  tls_enabled: %s\n", options->uri, S_OR(ws->client->options->proxy_host, "N/A"),
		AST_YESNO(original_tls_enabled));


	/* create and connect the client - note client_start
	   releases the session instance on failure */

	if (proxy && original_tls_enabled ) {
		ast_trace(-1, "%s: Disabling TLS while handshaking with proxy\n", options->uri);
		if (ws->client->args->tls_cfg) {
			ws->client->args->tls_cfg->enabled = 0;
		}
	}

	ast_trace(-1, "%s: Creating tcptls client\n", options->uri);
	ws->client->ser = ast_tcptls_client_create(ws->client->args);
	if (!ws->client->ser) {
		SCOPE_EXIT_LOG_RTN_VALUE(WS_CLIENT_START_ERROR, LOG_ERROR, "%s: Unable to create tcptls client\n", options->uri);
	}

	ast_trace(-1, "%s: Connecting%s%s\n", options->uri, proxy ? " via proxy " : "", S_OR(ws->client->options->proxy_host, ""));
	if (!ast_tcptls_client_start_timeout(ws->client->ser, options->timeout)) {
		ws->client->ser = NULL;
		SCOPE_EXIT_LOG_RTN_VALUE(WS_CLIENT_START_ERROR, LOG_ERROR, "%s: Unable to connect%s%s\n", options->uri,
			proxy ? " via proxy " : "", S_OR(ws->client->options->proxy_host, ""));
	}

	ast_trace(-1, "%s: Connected%s%s\n", options->uri, proxy ? " via proxy " : "", S_OR(ws->client->options->proxy_host, ""));
	if (proxy) {
		res = websocket_proxy_handshake(ws->client);
		if (res != WS_OK) {
			ao2_ref(ws->client->ser, -1);
			ws->client->ser = NULL;
			SCOPE_EXIT_LOG_RTN_VALUE(res, LOG_ERROR, "%s: Unable to perform proxy handshake with %s\n", options->uri,
				ws->client->options->proxy_host);
		}
	}

	if (proxy && original_tls_enabled && !ws->client->args->tls_cfg->enabled) {
		int rc = 0;
		ast_trace(-1, "%s: Re-enabling TLS after handshaking with proxy %s\n", options->uri, ws->client->options->proxy_host);
		ws->client->args->tls_cfg->enabled = 1;
		rc = ast_ssl_setup_client(ws->client->args->tls_cfg);
		if (rc != 1) {
			ao2_cleanup(ws->client->ser);
			ws->client->ser = NULL;
			SCOPE_EXIT_LOG_RTN_VALUE(WS_TLS_ERROR, LOG_ERROR, "%s: TLS context setup failed after handshake with %s\n",
				options->uri, ws->client->options->proxy_host);
		}
		if (!ast_tcptls_start_tls(ws->client->ser)) {
			ws->client->ser = NULL;
			SCOPE_EXIT_LOG_RTN_VALUE(WS_TLS_ERROR, LOG_ERROR, "%s: TLS with websocket server failed after handshake with %s\n",
				options->uri, ws->client->options->proxy_host);
		}
	}

	if ((res = websocket_client_handshake(ws->client)) != WS_OK) {
		ao2_ref(ws->client->ser, -1);
		ws->client->ser = NULL;
		SCOPE_EXIT_LOG_RTN_VALUE(res, LOG_ERROR, "%s: Unable to perform websocket handshake\n", options->uri);
	}

	ws->stream = ws->client->ser->stream;
	ws->secure = ast_iostream_get_ssl(ws->stream) ? 1 : 0;
	ws->client->ser->stream = NULL;
	ast_sockaddr_copy(&ws->remote_address, &ws->client->ser->remote_address);

	SCOPE_EXIT_RTN_VALUE(WS_OK, "%s: Handshake complete\n", options->uri);
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

	if (session->client->missed_pong_count > 1) {
		ast_debug(2, "%s: Missed PONG count is now %d\n", WS_SESSION_REMOTE(session),
			session->client->missed_pong_count);
	}

	if (session->client->missed_pong_count >= session->client->options->pingpong_probes) {
		ao2_unlock(session);
		ast_log(LOG_WARNING, "%s: %d missed PONGs. Closing connection.\n",
			WS_SESSION_REMOTE(session), session->client->missed_pong_count);
		session->client->ping_sched_timer = -1;
		websocket_close(session, AST_WEBSOCKET_STATUS_GOING_AWAY, 1);
		return 0;
	}

	ast_websocket_write(session, AST_WEBSOCKET_OPCODE_PING, WS_PING_PAYLOAD, WS_PING_PAYLOAD_LEN);
	session->client->missed_pong_count++;

	ao2_unlock(session);
	return session->client->options->pingpong_interval * 1000;
}

/*
 * Notes on client session lifecycle:
 *
 * Historically, the lifecycle of a client session was fairly simple.  Ownership was
 * transferred to the caller with the return of the create and when the caller released
 * their last reference, the session was closed by the destructor.  There was one issue
 * with this however, if the remote end sent us a CLOSE opcode, we were destroying
 * everything without sending the required CLOSE reply.  This could cause issues on
 * the remote end. The addition of WebSocket PING/PONG capability also doesn't work well
 * with that pattern because it adds a scheduler and callback which means that a reference
 * must be held by this module as long as the scheduler is active. This means that a caller
 * can't just unref the websocket and expect it to be automatically closed.
 *
 * So now...
 *
 * Once the websocket is created and connected, the usual pattern is for the higher
 * level client to be in a loop that blocks waiting on a websocket frame to be
 * available.  The client owns a reference to the websocket while the loop is
 * active and if we're sending PINGs, the scheduled task also holds a reference.
 *
 * When the higher level client wants to close the websocket, it calls
 * ast_websocket_close() from another thread.  We mark the session as CLOSED_BY_US,
 * stop the scheduled PING task and release its reference, send a CLOSE opcode to the
 * remote, then return to the caller.  The client's read thread is still blocked
 * at this point.  When we receive the CLOSE reply from the remote, we use the
 * CLOSED_BY_US flag to indicate that we're done and can close the socket and stream.
 * This causes the client's read thread to unblock with a CLOSE opcode which then calls
 * ast_websocket_close() (which is a NoOp because we already did the close) and
 * ast_websocket_unref() which triggers the session destructor.
 *
 * When the remote sends us a CLOSE opcode, we mark the session as CLOSED_BY_REMOTE
 * and return the CLOSE opcode in the ast_websocket_read that the caller should have
 * been blocked on. The caller must then call ast_websocket_close() just as above.
 * In this case however, it's not a NoOp.  We stop the scheduled PING task and
 * release its reference, send a CLOSE reply to the remote and since the transaction
 * is done, we close the socket and stream and return to the caller.  The caller then
 * calls ast_websocket_unref() which triggers the session destructor.
 */

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

	if ((*result = websocket_client_connect(ws, options)) != WS_OK) {
		ao2_ref(ws, -1);
 		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Failed to connect: %s\n", options->uri, ast_websocket_result_to_str(*result));
	}
	ast_trace(-1, "%s: Connected\n", ws->client->options->uri);

	if (ws->client->options->tcp_keepalives) {
		int sockfd = ast_iostream_get_fd(ws->stream);
		int enabled = 1;

		setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &options->tcp_keepalive_time, sizeof(options->tcp_keepalive_time));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &options->tcp_keepalive_interval, sizeof(&options->tcp_keepalive_interval));
		setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &options->tcp_keepalive_probes, sizeof(&options->tcp_keepalive_probes));
		ast_trace(-1, "%s: Enabled TCP keepalives\n", ws->client->options->uri);
	}

	if (ws->client->options->pingpongs) {
		ws->client->ping_sched_timer = ast_sched_add(ping_scheduler, ws->client->options->pingpong_interval * 1000,
			ping_scheduler_callback, ao2_bump(ws));
		if (ws->client->ping_sched_timer < 0) {
			ast_log(LOG_WARNING, "%s: Unable to schedule PING/PONG keepalives\n", ws->client->options->uri);
			ao2_ref(ws, -1);
		} else {
			ast_trace(-1, "%s: Enabled PING/PONG keepalives\n", ws->client->options->uri);
		}
	}
	SCOPE_EXIT_RTN_VALUE(ws, "%s: Client created and connected %p %p\n", options->uri, ws, ws->client);
}

int AST_OPTIONAL_API_NAME(ast_websocket_read_string)
	(struct ast_websocket *ws, char **buf)
{
	char *payload;
	uint64_t payload_len;
	enum ast_websocket_opcode opcode;
	int fragmented = 1;

	while (fragmented) {
		if (ast_websocket_read(ws, &payload, &payload_len,
				       &opcode, &fragmented)) {
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

	ast_debug(3, "Writing websocket string of length %" PRIu64 "\n", len);

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

static int unload_module(void)
{
	if (ping_scheduler) {
		ast_sched_context_destroy(ping_scheduler);
		ping_scheduler = NULL;
	}

	websocket_remove_protocol_internal("echo", websocket_echo_callback);
	ast_http_uri_unlink(&websocketuri);
	ao2_ref(websocketuri.data, -1);
	websocketuri.data = NULL;

	return 0;
}

static int load_module(void)
{
	websocketuri.data = websocket_server_internal_create();
	if (!websocketuri.data) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_http_uri_link(&websocketuri);
	websocket_add_protocol_internal("echo", websocket_echo_callback);

	ping_scheduler = ast_sched_context_create();
	if (!ping_scheduler) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sched_start_thread(ping_scheduler)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "HTTP WebSocket Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "http",
);
