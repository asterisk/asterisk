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
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/http.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/file.h"
#include "asterisk/unaligned.h"

#define AST_API_MODULE
#include "asterisk/http_websocket.h"

/*! \brief GUID used to compute the accept key, defined in the specifications */
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*! \brief Number of buckets for registered protocols */
#define MAX_PROTOCOL_BUCKETS 7

/*! \brief Size of the pre-determined buffer for WebSocket frames */
#define MAXIMUM_FRAME_SIZE 16384

/*! \brief Default reconstruction size for multi-frame payload reconstruction. If exceeded the next frame will start a
 *         payload.
 */
#define DEFAULT_RECONSTRUCTION_CEILING 16384

/*! \brief Maximum reconstruction size for multi-frame payload reconstruction. */
#define MAXIMUM_RECONSTRUCTION_CEILING 16384

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
	FILE *f;                          /*!< Pointer to the file instance used for writing and reading */
	int fd;                           /*!< File descriptor for the session, only used for polling */
	struct ast_sockaddr address;      /*!< Address of the remote client */
	enum ast_websocket_opcode opcode; /*!< Cached opcode for multi-frame messages */
	size_t payload_len;               /*!< Length of the payload */
	char *payload;                    /*!< Pointer to the payload */
	size_t reconstruct;               /*!< Number of bytes before a reconstructed payload will be returned and a new one started */
	int timeout;                      /*!< The timeout for operations on the socket */
	unsigned int secure:1;            /*!< Bit to indicate that the transport is secure */
	unsigned int closing:1;           /*!< Bit to indicate that the session is in the process of being closed */
	unsigned int close_sent:1;        /*!< Bit to indicate that the session close opcode has been sent and no further data will be sent */
};

/*! \brief Structure definition for protocols */
struct websocket_protocol {
	char *name;                      /*!< Name of the protocol */
	ast_websocket_callback callback; /*!< Callback called when a new session is established */
};

/*! \brief Container for registered protocols */
static struct ao2_container *protocols;

/*! \brief Hashing function for protocols */
static int protocol_hash_fn(const void *obj, const int flags)
{
	const struct websocket_protocol *protocol = obj;
	const char *name = obj;

	return ast_str_case_hash(flags & OBJ_KEY ? name : protocol->name);
}

/*! \brief Comparison function for protocols */
static int protocol_cmp_fn(void *obj, void *arg, int flags)
{
	const struct websocket_protocol *protocol1 = obj, *protocol2 = arg;
	const char *protocol = arg;

	return !strcasecmp(protocol1->name, flags & OBJ_KEY ? protocol : protocol2->name) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Destructor function for protocols */
static void protocol_destroy_fn(void *obj)
{
	struct websocket_protocol *protocol = obj;
	ast_free(protocol->name);
}

/*! \brief Destructor function for sessions */
static void session_destroy_fn(void *obj)
{
	struct ast_websocket *session = obj;

	ast_websocket_close(session, 0);

	if (session->f) {
		fclose(session->f);
		ast_verb(2, "WebSocket connection from '%s' closed\n", ast_sockaddr_stringify(&session->address));
	}

	ast_free(session->payload);
}

int AST_OPTIONAL_API_NAME(ast_websocket_add_protocol)(const char *name, ast_websocket_callback callback)
{
	struct websocket_protocol *protocol;

	ao2_lock(protocols);

	/* Ensure a second protocol handler is not registered for the same protocol */
	if ((protocol = ao2_find(protocols, name, OBJ_KEY | OBJ_NOLOCK))) {
		ao2_ref(protocol, -1);
		ao2_unlock(protocols);
		return -1;
	}

	if (!(protocol = ao2_alloc(sizeof(*protocol), protocol_destroy_fn))) {
		ao2_unlock(protocols);
		return -1;
	}

	if (!(protocol->name = ast_strdup(name))) {
		ao2_ref(protocol, -1);
		ao2_unlock(protocols);
		return -1;
	}

	protocol->callback = callback;

	ao2_link_flags(protocols, protocol, OBJ_NOLOCK);
	ao2_unlock(protocols);
	ao2_ref(protocol, -1);

	ast_verb(2, "WebSocket registered sub-protocol '%s'\n", name);

	return 0;
}

int AST_OPTIONAL_API_NAME(ast_websocket_remove_protocol)(const char *name, ast_websocket_callback callback)
{
	struct websocket_protocol *protocol;

	if (!(protocol = ao2_find(protocols, name, OBJ_KEY))) {
		return -1;
	}

	if (protocol->callback != callback) {
		ao2_ref(protocol, -1);
		return -1;
	}

	ao2_unlink(protocols, protocol);
	ao2_ref(protocol, -1);

	ast_verb(2, "WebSocket unregistered sub-protocol '%s'\n", name);

	return 0;
}

/*! \brief Close function for websocket session */
int AST_OPTIONAL_API_NAME(ast_websocket_close)(struct ast_websocket *session, uint16_t reason)
{
	char frame[4] = { 0, }; /* The header is 2 bytes and the reason code takes up another 2 bytes */
	int res;

	if (session->close_sent) {
		return 0;
	}

	frame[0] = AST_WEBSOCKET_OPCODE_CLOSE | 0x80;
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
		ast_verb(2, "WebSocket connection from '%s' forcefully closed due to fatal write error\n",
			ast_sockaddr_stringify(&session->address));
	}

	ao2_unlock(session);

	return res;
}


/*! \brief Write function for websocket traffic */
int AST_OPTIONAL_API_NAME(ast_websocket_write)(struct ast_websocket *session, enum ast_websocket_opcode opcode, char *payload, uint64_t payload_size)
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

	frame_size = header_size+payload_size;

	frame = ast_alloca(frame_size+1);
	memset(frame, 0, frame_size+1);

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

	if (!session->f) {
		ao2_unlock(session);
		ast_debug(0, "Closing WS session with 1011 because there's no session->f\n");
		ast_websocket_close(session, 1011);
		return -1;
	}

	if (ast_careful_fwrite(session->f, session->fd, frame, frame_size, session->timeout)) {
		ao2_unlock(session);
		/* 1011 - server terminating connection due to not being able to fulfill the request */
		ast_debug(0, "Closing WS with 1011 because we can't fulfill the write request\n");
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
	ao2_ref(session, -1);
}

int AST_OPTIONAL_API_NAME(ast_websocket_fd)(struct ast_websocket *session)
{
	return session->closing ? -1 : session->fd;
}

struct ast_sockaddr * AST_OPTIONAL_API_NAME(ast_websocket_remote_address)(struct ast_websocket *session)
{
	return &session->address;
}

int AST_OPTIONAL_API_NAME(ast_websocket_is_secure)(struct ast_websocket *session)
{
	return session->secure;
}

int AST_OPTIONAL_API_NAME(ast_websocket_set_nonblock)(struct ast_websocket *session)
{
	int flags;

	if ((flags = fcntl(session->fd, F_GETFL)) == -1) {
		return -1;
	}

	flags |= O_NONBLOCK;

	if ((flags = fcntl(session->fd, F_SETFL, flags)) == -1) {
		return -1;
	}

	return 0;
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
		/* Make the payload available so the user can look at the reason code if they so desire */
		if ((*payload_len) && (new_payload = ast_realloc(session->payload, *payload_len))) {
			if (ws_safe_read(session, &buf[frame_size], (*payload_len), opcode)) {
				return -1;
			}
			session->payload = new_payload;
			memcpy(session->payload, &buf[frame_size], *payload_len);
			*payload = session->payload;
			frame_size += (*payload_len);
		}

		session->closing = 1;
	} else {
		ast_log(LOG_WARNING, "WebSocket unknown opcode %u\n", *opcode);
		/* We received an opcode that we don't understand, the RFC states that 1003 is for a type of data that can't be accepted... opcodes
		 * fit that, I think. */
		ast_websocket_close(session, 1003);
	}

	return 0;
}

/*! \brief Callback that is executed everytime an HTTP request is received by this module */
static int websocket_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_vars, struct ast_variable *headers)
{
	struct ast_variable *v;
	char *upgrade = NULL, *key = NULL, *key1 = NULL, *key2 = NULL, *protos = NULL, *requested_protocols = NULL, *protocol = NULL;
	int version = 0, flags = 1;
	struct websocket_protocol *protocol_handler = NULL;
	struct ast_websocket *session;

	/* Upgrade requests are only permitted on GET methods */
	if (method != AST_HTTP_GET) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return -1;
	}

	/* Get the minimum headers required to satisfy our needs */
	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "Upgrade")) {
			upgrade = ast_strip(ast_strdupa(v->value));
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Key")) {
			key = ast_strip(ast_strdupa(v->value));
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Key1")) {
			key1 = ast_strip(ast_strdupa(v->value));
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Key2")) {
			key2 = ast_strip(ast_strdupa(v->value));
		} else if (!strcasecmp(v->name, "Sec-WebSocket-Protocol")) {
			requested_protocols = ast_strip(ast_strdupa(v->value));
			protos = ast_strdupa(requested_protocols);
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
		return -1;
	} else if (ast_strlen_zero(requested_protocols)) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - no protocols requested\n",
			ast_sockaddr_stringify(&ser->remote_address));
		fputs("HTTP/1.1 400 Bad Request\r\n"
		      "Sec-WebSocket-Version: 7, 8, 13\r\n\r\n", ser->f);
		return -1;
	} else if (key1 && key2) {
		/* Specification defined in http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-76 and
		 * http://tools.ietf.org/html/draft-ietf-hybi-thewebsocketprotocol-00 -- not currently supported*/
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - unsupported version '00/76' chosen\n",
			ast_sockaddr_stringify(&ser->remote_address));
		fputs("HTTP/1.1 400 Bad Request\r\n"
		      "Sec-WebSocket-Version: 7, 8, 13\r\n\r\n", ser->f);
		return 0;
	}

	/* Iterate through the requested protocols trying to find one that we have a handler for */
	while ((protocol = strsep(&requested_protocols, ","))) {
		if ((protocol_handler = ao2_find(protocols, ast_strip(protocol), OBJ_KEY))) {
			break;
		}
	}

	/* If no protocol handler exists bump this back to the requester */
	if (!protocol_handler) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - no protocols out of '%s' supported\n",
			ast_sockaddr_stringify(&ser->remote_address), protos);
		fputs("HTTP/1.1 400 Bad Request\r\n"
		      "Sec-WebSocket-Version: 7, 8, 13\r\n\r\n", ser->f);
		return 0;
	}

	/* Determine how to respond depending on the version */
	if (version == 7 || version == 8 || version == 13) {
		/* Version 7 defined in specification http://tools.ietf.org/html/draft-ietf-hybi-thewebsocketprotocol-07 */
		/* Version 8 defined in specification http://tools.ietf.org/html/draft-ietf-hybi-thewebsocketprotocol-10 */
		/* Version 13 defined in specification http://tools.ietf.org/html/rfc6455 */
		char *combined, base64[64];
		unsigned combined_length;
		uint8_t sha[20];

		combined_length = (key ? strlen(key) : 0) + strlen(WEBSOCKET_GUID) + 1;
		if (!key || combined_length > 8192) { /* no stack overflows please */
			fputs("HTTP/1.1 400 Bad Request\r\n"
			      "Sec-WebSocket-Version: 7, 8, 13\r\n\r\n", ser->f);
			ao2_ref(protocol_handler, -1);
			return 0;
		}

		if (!(session = ao2_alloc(sizeof(*session), session_destroy_fn))) {
			ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted\n",
				ast_sockaddr_stringify(&ser->remote_address));
			fputs("HTTP/1.1 400 Bad Request\r\n"
			      "Sec-WebSocket-Version: 7, 8, 13\r\n\r\n", ser->f);
			ao2_ref(protocol_handler, -1);
			return 0;
		}
		session->timeout = AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT;

		combined = ast_alloca(combined_length);
		snprintf(combined, combined_length, "%s%s", key, WEBSOCKET_GUID);
		ast_sha1_hash_uint(sha, combined);
		ast_base64encode(base64, (const unsigned char*)sha, 20, sizeof(base64));

		fprintf(ser->f, "HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: %s\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Accept: %s\r\n"
			"Sec-WebSocket-Protocol: %s\r\n\r\n",
			upgrade,
			base64,
			protocol);
		fflush(ser->f);
	} else {

		/* Specification defined in http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-75 or completely unknown */
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - unsupported version '%d' chosen\n",
			ast_sockaddr_stringify(&ser->remote_address), version ? version : 75);
		fputs("HTTP/1.1 400 Bad Request\r\n"
		      "Sec-WebSocket-Version: 7, 8, 13\r\n\r\n", ser->f);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	/* Enable keepalive on all sessions so the underlying user does not have to */
	if (setsockopt(ser->fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags))) {
		ast_log(LOG_WARNING, "WebSocket connection from '%s' could not be accepted - failed to enable keepalive\n",
			ast_sockaddr_stringify(&ser->remote_address));
		fputs("HTTP/1.1 400 Bad Request\r\n"
		      "Sec-WebSocket-Version: 7, 8, 13\r\n\r\n", ser->f);
		ao2_ref(session, -1);
		ao2_ref(protocol_handler, -1);
		return 0;
	}

	ast_verb(2, "WebSocket connection from '%s' for protocol '%s' accepted using version '%d'\n", ast_sockaddr_stringify(&ser->remote_address), protocol, version);

	/* Populate the session with all the needed details */
	session->f = ser->f;
	session->fd = ser->fd;
	ast_sockaddr_copy(&session->address, &ser->remote_address);
	session->opcode = -1;
	session->reconstruct = DEFAULT_RECONSTRUCTION_CEILING;
	session->secure = ser->ssl ? 1 : 0;

	/* Give up ownership of the socket and pass it to the protocol handler */
	protocol_handler->callback(session, get_vars, headers);
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
	.callback = websocket_callback,
	.description = "Asterisk HTTP WebSocket",
	.uri = "ws",
	.has_subtree = 0,
	.data = NULL,
	.key = __FILE__,
};

/*! \brief Simple echo implementation which echoes received text and binary frames */
static void websocket_echo_callback(struct ast_websocket *session, struct ast_variable *parameters, struct ast_variable *headers)
{
	int flags, res;

	ast_debug(1, "Entering WebSocket echo loop\n");

	if ((flags = fcntl(ast_websocket_fd(session), F_GETFL)) == -1) {
		goto end;
	}

	flags |= O_NONBLOCK;

	if (fcntl(ast_websocket_fd(session), F_SETFL, flags) == -1) {
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

static int load_module(void)
{
	protocols = ao2_container_alloc(MAX_PROTOCOL_BUCKETS, protocol_hash_fn, protocol_cmp_fn);
	ast_http_uri_link(&websocketuri);
	ast_websocket_add_protocol("echo", websocket_echo_callback);

	return 0;
}

static int unload_module(void)
{
	ast_websocket_remove_protocol("echo", websocket_echo_callback);
	ast_http_uri_unlink(&websocketuri);
	ao2_ref(protocols, -1);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "HTTP WebSocket Support",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	);
