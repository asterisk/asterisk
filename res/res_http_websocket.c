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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/http.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/file.h"
#include "asterisk/unaligned.h"
#include "asterisk/uri.h"

#define AST_API_MODULE
#include "asterisk/http_websocket.h"

/*! \brief GUID used to compute the accept key, defined in the specifications */
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*! \brief Length of a websocket's client key */
#define CLIENT_KEY_SIZE 16

/*! \brief Number of buckets for registered protocols */
#define MAX_PROTOCOL_BUCKETS 7

#ifdef LOW_MEMORY
/*! \brief Size of the pre-determined buffer for WebSocket frames */
#define MAXIMUM_FRAME_SIZE 8192

/*! \brief Default reconstruction size for multi-frame payload reconstruction. If exceeded the next frame will start a
 *         payload.
 */
#define DEFAULT_RECONSTRUCTION_CEILING 8192

/*! \brief Maximum reconstruction size for multi-frame payload reconstruction. */
#define MAXIMUM_RECONSTRUCTION_CEILING 8192
#else
/*! \brief Size of the pre-determined buffer for WebSocket frames */
#define MAXIMUM_FRAME_SIZE 32768

/*! \brief Default reconstruction size for multi-frame payload reconstruction. If exceeded the next frame will start a
 *         payload.
 */
#define DEFAULT_RECONSTRUCTION_CEILING 32768

/*! \brief Maximum reconstruction size for multi-frame payload reconstruction. */
#define MAXIMUM_RECONSTRUCTION_CEILING 32768
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

/*! \brief Structure definition for session */
struct ast_websocket {
	FILE *f;                            /*!< Pointer to the file instance used for writing and reading */
	int fd;                             /*!< File descriptor for the session, only used for polling */
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
	struct websocket_client *client;    /*!< Client object when connected as a client websocket */
};

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

static void websocket_server_internal_dtor(void *obj)
{
	struct ast_websocket_server *server = obj;
	ao2_cleanup(server->protocols);
	server->protocols = NULL;
}

static void websocket_server_dtor(void *obj)
{
	websocket_server_internal_dtor(obj);
	ast_module_unref(ast_module_info->self);
}

static struct ast_websocket_server *websocket_server_create_impl(void (*dtor)(void *))
{
	RAII_VAR(struct ast_websocket_server *, server, NULL, ao2_cleanup);

	server = ao2_alloc(sizeof(*server), dtor);
	if (!server) {
		return NULL;
	}

	server->protocols = ao2_container_alloc(MAX_PROTOCOL_BUCKETS, protocol_hash_fn, protocol_cmp_fn);
	if (!server->protocols) {
		return NULL;
	}

	ao2_ref(server, +1);
	return server;
}

static struct ast_websocket_server *websocket_server_internal_create(void)
{
	return websocket_server_create_impl(websocket_server_internal_dtor);
}

struct ast_websocket_server *AST_OPTIONAL_API_NAME(ast_websocket_server_create)(void)
{
	ast_module_ref(ast_module_info->self);
	return websocket_server_create_impl(websocket_server_dtor);
}

/*! \brief Destructor function for sessions */
static void session_destroy_fn(void *obj)
{
	struct ast_websocket *session = obj;

	if (session->f) {
		ast_websocket_close(session, 0);
		if (session->f) {
			fclose(session->f);
			ast_verb(2, "WebSocket connection %s '%s' closed\n", session->client ? "to" : "from",
				ast_sockaddr_stringify(&session->remote_address));
		}
	}

	ao2_cleanup(session->client);
	ast_free(session->payload);
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

	ast_verb(2, "WebSocket registered sub-protocol '%s'\n", protocol->name);
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

	ast_verb(2, "WebSocket unregistered sub-protocol '%s'\n", name);

	return 0;
}

/*! \brief Close function for websocket session */
int AST_OPTIONAL_API_NAME(ast_websocket_close)(struct ast_websocket *session, uint16_t reason)
{
	enum ast_websocket_opcode opcode = AST_WEBSOCKET_OPCODE_CLOSE;
	char frame[4] = { 0, }; /* The header is 2 bytes and the reason code takes up another 2 bytes */
	int res;

	if (session->close_sent) {
		return 0;
	}

	frame[0] = opcode | 0x80;
	frame[1] = 2; /* The reason code is always 2 bytes */

	/* If no reason has been specified assume 1000 which is normal closure */
	put_unaligned_uint16(&frame[2], htons(reason ? reason : 1000));

	session->closing = 1;
	session->close_sent = 1;

	ao2_lock(session);
	res = ast_careful_fwrite(session->f, session->fd, frame, 4, session->timeout);

	/* If an error occurred when trying to close this connection explicitly terminate it now.
	 * Doing so will cause the thread polling on it to wake up and terminate.
	 */
	if (res) {
		fclose(session->f);
		session->f = NULL;
		ast_verb(2, "WebSocket connection %s '%s' forcefully closed due to fatal write error\n",
			session->client ? "to" : "from", ast_sockaddr_stringify(&session->remote_address));
	}

	ao2_unlock(session);
	return res;
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
	if (opcode < AST_WEBSOCKET_OPCODE_CONTINUATION ||
			opcode > AST_WEBSOCKET_OPCODE_PONG) {
		return "<unknown>";
	} else {
		return opcode_map[opcode];
	}
}

/*! \brief Write function for websocket traffic */
int AST_OPTIONAL_API_NAME(ast_websocket_write)(struct ast_websocket *session, enum ast_websocket_opcode opcode, char *payload, uint64_t payload_size)
{
	size_t header_size = 2; /* The minimum size of a websocket frame is 2 bytes */
	char *frame;
	uint64_t length;
	uint64_t frame_size;

	ast_debug(3, "Writing websocket %s frame, length %" PRIu64 "\n",
			websocket_opcode2str(opcode), payload_size);

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

	ao2_lock(session);
	if (session->closing) {
		ao2_unlock(session);
		return -1;
	}

	if (ast_careful_fwrite(session->f, session->fd, frame, frame_size, session->timeout)) {
		ao2_unlock(session);
		/* 1011 - server terminating connection due to not being able to fulfill the request */
		ast_debug(1, "Closing WS with 1011 because we can't fulfill a write request\n");
		ast_websocket_close(session, 1011);
		return -1;
	}

	fflush(session->f);
	ao2_unlock(session);

	return 0;
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
	ao2_cleanup(session);
}

int AST_OPTIONAL_API_NAME(ast_websocket_fd)(struct ast_websocket *session)
{
	return session->closing ? -1 : session->fd;
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
	return ast_fd_set_flags(session->fd, O_NONBLOCK);
}

int AST_OPTIONAL_API_NAME(ast_websocket_set_timeout)(struct ast_websocket *session, int timeout)
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
static inline int ws_safe_read(struct ast_websocket *session, char *buf, int len, enum ast_websocket_opcode *opcode)
{
	size_t rlen;
	int xlen = len;
	char *rbuf = buf;
	int sanity = 10;

	ao2_lock(session);
	if (!session->f) {
		ao2_unlock(session);
		errno = ECONNABORTED;
		return -1;
	}

	for (;;) {
		clearerr(session->f);
		rlen = fread(rbuf, 1, xlen, session->f);
		if (!rlen) {
			if (feof(session->f)) {
				ast_log(LOG_WARNING, "Web socket closed abruptly\n");
				*opcode = AST_WEBSOCKET_OPCODE_CLOSE;
				session->closing = 1;
				ao2_unlock(session);
				return -1;
			}

			if (ferror(session->f) && errno != EAGAIN) {
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
		xlen = xlen - rlen;
		rbuf = rbuf + rlen;
		if (!xlen) {
			break;
		}
		if (ast_wait_for_input(session->fd, 1000) < 0) {
			ast_log(LOG_ERROR, "ast_wait_for_input returned err: %s\n", strerror(errno));
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
	char buf[MAXIMUM_FRAME_SIZE] = "";
	int fin = 0;
	int mask_present = 0;
	char *mask = NULL, *new_payload = NULL;
	size_t options_len = 0, frame_size = 0;

	*payload = NULL;
	*payload_len = 0;
	*fragmented = 0;

	if (ws_safe_read(session, &buf[0], MIN_WS_HDR_SZ, opcode)) {
		return -1;
	}
	frame_size += MIN_WS_HDR_SZ;

	/* ok, now we have the first 2 bytes, so we know some flags, opcode and payload length (or whether payload length extension will be required) */
	*opcode = buf[0] & 0xf;
	*payload_len = buf[1] & 0x7f;
	if (*opcode == AST_WEBSOCKET_OPCODE_TEXT || *opcode == AST_WEBSOCKET_OPCODE_BINARY || *opcode == AST_WEBSOCKET_OPCODE_CONTINUATION ||
	    *opcode == AST_WEBSOCKET_OPCODE_PING || *opcode == AST_WEBSOCKET_OPCODE_PONG) {
		fin = (buf[0] >> 7) & 1;
		mask_present = (buf[1] >> 7) & 1;

		/* Based on the mask flag and payload length, determine how much more we need to read before start parsing the rest of the header */
		options_len += mask_present ? 4 : 0;
		options_len += (*payload_len == 126) ? 2 : (*payload_len == 127) ? 8 : 0;
		if (options_len) {
			/* read the rest of the header options */
			if (ws_safe_read(session, &buf[frame_size], options_len, opcode)) {
				return -1;
			}
			frame_size += options_len;
		}

		if (*payload_len == 126) {
			/* Grab the 2-byte payload length  */
			*payload_len = ntohs(get_unaligned_uint16(&buf[2]));
			mask = &buf[4];
		} else if (*payload_len == 127) {
			/* Grab the 8-byte payload length  */
			*payload_len = ntohl(get_unaligned_uint64(&buf[2]));
			mask = &buf[10];
		} else {
			/* Just set the mask after the small 2-byte header */
			mask = &buf[2];
		}

		/* Now read the rest of the payload */
		*payload = &buf[frame_size]; /* payload will start here, at the end of the options, if any */
		frame_size = frame_size + (*payload_len); /* final frame size is header + optional headers + payload data */
		if (frame_size > MAXIMUM_FRAME_SIZE) {
			ast_log(LOG_WARNING, "Cannot fit huge websocket frame of %zu bytes\n", frame_size);
			/* The frame won't fit :-( */
			ast_websocket_close(session, 1009);
			return -1;
		}

		if (ws_safe_read(session, *payload, *payload_len, opcode)) {
			return -1;
		}
		/* If a mask is present unmask the payload */
		if (mask_present) {
			unsigned int pos;
			for (pos = 0; pos < *payload_len; pos++) {
				(*payload)[pos] ^= mask[pos % 4];
			}
		}

		/* Per the RFC for PING we need to send back an opcode with the application data as received */
		if ((*opcode == AST_WEBSOCKET_OPCODE_PING) && (ast_websocket_write(session, AST_WEBSOCKET_OPCODE_PONG, *payload, *payload_len))) {
			*payload_len = 0;
			ast_websocket_close(session, 1009);
			return 0;
		}

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
	} else if (*opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
		session->closing = 1;

		/* Make the payload available so the user can look at the reason code if they so desire */
		if (!*payload_len) {
			return 0;
		}

		if (!(new_payload = ast_realloc(session->payload, *payload_len))) {
			ast_log(LOG_WARNING, "Failed allocation: %p, %"PRIu64"\n",
					session->payload, *payload_len);
			*payload_len = 0;
			return -1;
		}

		session->payload = new_payload;
		if (ws_safe_read(session, &buf[frame_size], *payload_len, opcode)) {
			return -1;
		}
		memcpy(session->payload, &buf[frame_size], *payload_len);
		*payload = session->payload;
		frame_size += *payload_len;
	} else {
		ast_log(LOG_WARNING, "WebSocket unknown opcode %u\n", *opcode);
		/* We received an opcode that we don't understand, the RFC states that 1003 is for a type of data that can't be accepted... opcodes
		 * fit that, I think. */
		ast_websocket_close(session, 1003);
	}

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

		if (!(session = ao2_alloc(sizeof(*session), session_destroy_fn))) {
			ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted\n",
				ast_sockaddr_stringify(&ser->remote_address));
			websocket_bad_request(ser);
			ao2_ref(protocol_handler, -1);
			return 0;
		}
		session->timeout =  AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT;

		if (protocol_handler->session_attempted
		    && protocol_handler->session_attempted(ser, get_vars, headers)) {
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
			fprintf(ser->f, "HTTP/1.1 101 Switching Protocols\r\n"
				"Upgrade: %s\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Accept: %s\r\n"
				"Sec-WebSocket-Protocol: %s\r\n\r\n",
				upgrade,
				websocket_combine_key(key, base64, sizeof(base64)),
				protocol);
		} else {
			fprintf(ser->f, "HTTP/1.1 101 Switching Protocols\r\n"
				"Upgrade: %s\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Accept: %s\r\n\r\n",
				upgrade,
				websocket_combine_key(key, base64, sizeof(base64)));
		}

		fflush(ser->f);
	} else {

		/* Specification defined in http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-75 or completely unknown */
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - unsupported version '%d' chosen\n",
			ast_sockaddr_stringify(&ser->remote_address), version ? version : 75);
		websocket_bad_request(ser);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	/* Enable keepalive on all sessions so the underlying user does not have to */
	if (setsockopt(ser->fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags))) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - failed to enable keepalive\n",
			ast_sockaddr_stringify(&ser->remote_address));
		websocket_bad_request(ser);
		ao2_ref(session, -1);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	/* Get our local address for the connected socket */
	if (ast_getsockname(ser->fd, &session->local_address)) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - failed to get local address\n",
			ast_sockaddr_stringify(&ser->remote_address));
		websocket_bad_request(ser);
		ao2_ref(session, -1);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	ast_verb(2, "WebSocket connection from '%s' for protocol '%s' accepted using version '%d'\n", ast_sockaddr_stringify(&ser->remote_address), protocol ? : "", version);

	/* Populate the session with all the needed details */
	session->f = ser->f;
	session->fd = ser->fd;
	ast_sockaddr_copy(&session->remote_address, &ser->remote_address);
	session->opcode = -1;
	session->reconstruct = DEFAULT_RECONSTRUCTION_CEILING;
	session->secure = ser->ssl ? 1 : 0;

	/* Give up ownership of the socket and pass it to the protocol handler */
	ast_tcptls_stream_set_exclusive_input(ser->stream_cookie, 0);
	protocol_handler->session_established(session, get_vars, headers);
	ao2_ref(protocol_handler, -1);

	/*
	 * By dropping the FILE* and fd from the session the connection
	 * won't get closed when the HTTP server cleans up because we
	 * passed the connection to the protocol handler.
	 */
	ser->f = NULL;
	ser->fd = -1;

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

	while ((res = ast_wait_for_input(ast_websocket_fd(session), -1)) > 0) {
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
	int res = websocket_add_protocol_internal(name, callback);
	if (res == 0) {
		ast_module_ref(ast_module_info->self);
	}
	return res;
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

	ast_module_ref(ast_module_info->self);
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
	int res = websocket_remove_protocol_internal(name, callback);
	if (res == 0) {
		ast_module_unref(ast_module_info->self);
	}
	return res;
}

/*! \brief Parse the given uri into a path and remote address.
 *
 * Expected uri form: [ws[s]]://<host>[:port][/<path>]
 *
 * The returned host will contain the address and optional port while
 * path will contain everything after the address/port if included.
 */
static int websocket_client_parse_uri(const char *uri, char **host, struct ast_str **path)
{
	struct ast_uri *parsed_uri = ast_uri_parse_websocket(uri);

	if (!parsed_uri) {
		return -1;
	}

	*host = ast_uri_make_host_with_port(parsed_uri);

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

struct websocket_client {
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
};

static void websocket_client_destroy(void *obj)
{
	struct websocket_client *client = obj;

	ao2_cleanup(client->ser);
	ao2_cleanup(client->args);

	ast_free(client->accept_protocol);
	ast_free(client->protocols);
	ast_free(client->key);
	ast_free(client->resource_name);
	ast_free(client->host);
}

static struct ast_websocket * websocket_client_create(
	const char *uri, const char *protocols,	struct ast_tls_config *tls_cfg,
	enum ast_websocket_result *result)
{
	struct ast_websocket *ws = ao2_alloc(sizeof(*ws), session_destroy_fn);

	if (!ws) {
		ast_log(LOG_ERROR, "Unable to allocate websocket\n");
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	if (!(ws->client = ao2_alloc(
		      sizeof(*ws->client), websocket_client_destroy))) {
		ast_log(LOG_ERROR, "Unable to allocate websocket client\n");
		*result = WS_ALLOCATE_ERROR;
		return NULL;
	}

	if (!(ws->client->key = websocket_client_create_key())) {
		ao2_ref(ws, -1);
		*result = WS_KEY_ERROR;
		return NULL;
	}

	if (websocket_client_parse_uri(
		    uri, &ws->client->host, &ws->client->resource_name)) {
		ao2_ref(ws, -1);
		*result = WS_URI_PARSE_ERROR;
		return NULL;
	}

	if (!(ws->client->args = websocket_client_args_create(
		      ws->client->host, tls_cfg, result))) {
		ao2_ref(ws, -1);
		return NULL;
	}
	ws->client->protocols = ast_strdup(protocols);

	ws->client->version = 13;
	ws->opcode = -1;
	ws->reconstruct = DEFAULT_RECONSTRUCTION_CEILING;
	return ws;
}

const char * AST_OPTIONAL_API_NAME(
	ast_websocket_client_accept_protocol)(struct ast_websocket *ws)
{
	return ws->client->accept_protocol;
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
		ast_log(LOG_ERROR, "Received response 400 - Bad Request "
			"- from %s\n", client->host);
		return WS_BAD_REQUEST;
	case 404:
		ast_log(LOG_ERROR, "Received response 404 - Request URL not "
			"found - from %s\n", client->host);
		return WS_URL_NOT_FOUND;
	}

	ast_log(LOG_ERROR, "Invalid HTTP response code %d from %s\n",
		response_code, client->host);
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

	if (!fgets(buf, sizeof(buf), client->ser->f)) {
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
	while (fgets(buf, sizeof(buf), client->ser->f)) {
		char *name, *value;
		int parsed = ast_http_header_parse(buf, &name, &value);

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

static enum ast_websocket_result websocket_client_handshake(
	struct websocket_client *client)
{
	char protocols[100] = "";

	if (!ast_strlen_zero(client->protocols)) {
		sprintf(protocols, "Sec-WebSocket-Protocol: %s\r\n",
			client->protocols);
	}

	if (fprintf(client->ser->f,
		    "GET /%s HTTP/1.1\r\n"
		    "Sec-WebSocket-Version: %d\r\n"
		    "Upgrade: websocket\r\n"
		    "Connection: Upgrade\r\n"
		    "Host: %s\r\n"
		    "Sec-WebSocket-Key: %s\r\n"
		    "%s\r\n",
		    client->resource_name ? ast_str_buffer(client->resource_name) : "",
		    client->version,
		    client->host,
		    client->key,
		    protocols) < 0) {
		ast_log(LOG_ERROR, "Failed to send handshake.\n");
		return WS_WRITE_ERROR;
	}
	/* wait for a response before doing anything else */
	return websocket_client_handshake_get_response(client);
}

static enum ast_websocket_result websocket_client_connect(struct ast_websocket *ws)
{
	enum ast_websocket_result res;
	/* create and connect the client - note client_start
	   releases the session instance on failure */
	if (!(ws->client->ser = ast_tcptls_client_start(
		      ast_tcptls_client_create(ws->client->args)))) {
		return WS_CLIENT_START_ERROR;
	}

	if ((res = websocket_client_handshake(ws->client)) != WS_OK) {
		ao2_ref(ws->client->ser, -1);
		ws->client->ser = NULL;
		return res;
	}

	ws->f = ws->client->ser->f;
	ws->fd = ws->client->ser->fd;
	ws->secure = ws->client->ser->ssl ? 1 : 0;
	ast_sockaddr_copy(&ws->remote_address, &ws->client->ser->remote_address);
	return WS_OK;
}

struct ast_websocket *AST_OPTIONAL_API_NAME(ast_websocket_client_create)
	(const char *uri, const char *protocols, struct ast_tls_config *tls_cfg,
	 enum ast_websocket_result *result)
{
	struct ast_websocket *ws = websocket_client_create(
		uri, protocols, tls_cfg, result);

	if (!ws) {
		return NULL;
	}

	if ((*result = websocket_client_connect(ws)) != WS_OK) {
		ao2_ref(ws, -1);
		return NULL;
	}

	return ws;
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

	if (!(*buf = ast_malloc(payload_len + 1))) {
		return -1;
	}

	ast_copy_string(*buf, payload, payload_len + 1);
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

static int load_module(void)
{
	websocketuri.data = websocket_server_internal_create();
	if (!websocketuri.data) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_http_uri_link(&websocketuri);
	websocket_add_protocol_internal("echo", websocket_echo_callback);

	/* For Optional API. */
	ast_module_shutdown_ref(ast_module_info->self);

	return 0;
}

static int unload_module(void)
{
	websocket_remove_protocol_internal("echo", websocket_echo_callback);
	ast_http_uri_unlink(&websocketuri);
	ao2_ref(websocketuri.data, -1);
	websocketuri.data = NULL;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "HTTP WebSocket Support",
		.support_level = AST_MODULE_SUPPORT_EXTENDED,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	);
