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
	<defaultenabled>no</defaultenabled>
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
#define MAXIMUM_FRAME_SIZE 8192

/*! \brief Default reconstruction size for multi-frame payload reconstruction. If exceeded the next frame will start a
 *         payload.
 */
#define DEFAULT_RECONSTRUCTION_CEILING 16384

/*! \brief Maximum reconstruction size for multi-frame payload reconstruction. */
#define MAXIMUM_RECONSTRUCTION_CEILING 16384

/*! \brief Structure definition for session */
struct ast_websocket {
	FILE *f;                          /*!< Pointer to the file instance used for writing and reading */
	int fd;                           /*!< File descriptor for the session, only used for polling */
	struct ast_sockaddr address;      /*!< Address of the remote client */
	enum ast_websocket_opcode opcode; /*!< Cached opcode for multi-frame messages */
	size_t payload_len;               /*!< Length of the payload */
	char *payload;                    /*!< Pointer to the payload */
	size_t reconstruct;               /*!< Number of bytes before a reconstructed payload will be returned and a new one started */
	unsigned int secure:1;            /*!< Bit to indicate that the transport is secure */
	unsigned int closing:1;           /*!< Bit to indicate that the session is in the process of being closed */
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

	frame[0] = AST_WEBSOCKET_OPCODE_CLOSE | 0x80;
	frame[1] = 2; /* The reason code is always 2 bytes */

	/* If no reason has been specified assume 1000 which is normal closure */
	put_unaligned_uint16(&frame[2], htons(reason ? reason : 1000));

	session->closing = 1;

	return (fwrite(frame, 1, 4, session->f) == 4) ? 0 : -1;
}


/*! \brief Write function for websocket traffic */
int AST_OPTIONAL_API_NAME(ast_websocket_write)(struct ast_websocket *session, enum ast_websocket_opcode opcode, char *payload, uint64_t actual_length)
{
	size_t header_size = 2; /* The minimum size of a websocket frame is 2 bytes */
	char *frame;
	uint64_t length = 0;

	if (actual_length < 126) {
		length = actual_length;
	} else if (actual_length < (1 << 16)) {
		length = 126;
		/* We need an additional 2 bytes to store the extended length */
		header_size += 2;
	} else {
		length = 127;
		/* We need an additional 8 bytes to store the really really extended length */
		header_size += 8;
	}

	frame = ast_alloca(header_size);
	memset(frame, 0, sizeof(*frame));

	frame[0] = opcode | 0x80;
	frame[1] = length;

	/* Use the additional available bytes to store the length */
	if (length == 126) {
		put_unaligned_uint16(&frame[2], htons(actual_length));
	} else if (length == 127) {
		put_unaligned_uint64(&frame[2], htonl(actual_length));
	}

	if (fwrite(frame, 1, header_size, session->f) != header_size) {
		return -1;
	}

	if (fwrite(payload, 1, actual_length, session->f) != actual_length) {
		return -1;
	}

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

int AST_OPTIONAL_API_NAME(ast_websocket_read)(struct ast_websocket *session, char **payload, uint64_t *payload_len, enum ast_websocket_opcode *opcode, int *fragmented)
{
	char buf[MAXIMUM_FRAME_SIZE] = "";
	size_t frame_size, expected = 2;

	*payload = NULL;
	*payload_len = 0;
	*fragmented = 0;

	/* We try to read in 14 bytes, which is the largest possible WebSocket header */
	if ((frame_size = fread(&buf, 1, 14, session->f)) < 1) {
		return -1;
	}

	/* The minimum size for a WebSocket frame is 2 bytes */
	if (frame_size < expected) {
		return -1;
	}

	*opcode = buf[0] & 0xf;

	if (*opcode == AST_WEBSOCKET_OPCODE_TEXT || *opcode == AST_WEBSOCKET_OPCODE_BINARY || *opcode == AST_WEBSOCKET_OPCODE_CONTINUATION ||
	    *opcode == AST_WEBSOCKET_OPCODE_PING || *opcode == AST_WEBSOCKET_OPCODE_PONG) {
		int fin = (buf[0] >> 7) & 1;
		int mask_present = (buf[1] >> 7) & 1;
		char *mask = NULL, *new_payload;
		size_t remaining;

		if (mask_present) {
			/* The mask should take up 4 bytes */
			expected += 4;

			if (frame_size < expected) {
				/* Per the RFC 1009 means we received a message that was too large for us to process */
				ast_websocket_close(session, 1009);
				return 0;
			}
		}

		/* Assume no extended length and no masking at the beginning */
		*payload_len = buf[1] & 0x7f;
		*payload = &buf[2];

		/* Determine if extended length is being used */
		if (*payload_len == 126) {
			/* Use the next 2 bytes to get a uint16_t */
			expected += 2;
			*payload += 2;

			if (frame_size < expected) {
				ast_websocket_close(session, 1009);
				return 0;
			}

			*payload_len = ntohs(get_unaligned_uint16(&buf[2]));
		} else if (*payload_len == 127) {
			/* Use the next 8 bytes to get a uint64_t */
			expected += 8;
			*payload += 8;

			if (frame_size < expected) {
				ast_websocket_close(session, 1009);
				return 0;
			}

			*payload_len = ntohl(get_unaligned_uint64(&buf[2]));
		}

		/* If masking is present the payload currently points to the mask, so move it over 4 bytes to the actual payload */
		if (mask_present) {
			mask = *payload;
			*payload += 4;
		}

		/* Determine how much payload we need to read in as we may have already read some in */
		remaining = *payload_len - (frame_size - expected);

		/* If how much payload they want us to read in exceeds what we are capable of close the session, things
		 * will fail no matter what most likely */
		if (remaining > (MAXIMUM_FRAME_SIZE - frame_size)) {
			ast_websocket_close(session, 1009);
			return 0;
		}

		new_payload = *payload + (frame_size - expected);

		/* Read in the remaining payload */
		while (remaining > 0) {
			size_t payload_read;

			/* Wait for data to come in */
			if (ast_wait_for_input(session->fd, -1) <= 0) {
				*opcode = AST_WEBSOCKET_OPCODE_CLOSE;
				*payload = NULL;
				session->closing = 1;
				return 0;
			}

			/* If some sort of failure occurs notify the caller */
			if ((payload_read = fread(new_payload, 1, remaining, session->f)) < 1) {
				return -1;
			}

			remaining -= payload_read;
			new_payload += payload_read;
		}

		/* If a mask is present unmask the payload */
		if (mask_present) {
			unsigned int pos;
			for (pos = 0; pos < *payload_len; pos++) {
				(*payload)[pos] ^= mask[pos % 4];
			}
		}

		if (!(new_payload = ast_realloc(session->payload, session->payload_len + *payload_len))) {
			*payload_len = 0;
			ast_websocket_close(session, 1009);
			return 0;
		}

		/* Per the RFC for PING we need to send back an opcode with the application data as received */
		if (*opcode == AST_WEBSOCKET_OPCODE_PING) {
			ast_websocket_write(session, AST_WEBSOCKET_OPCODE_PONG, *payload, *payload_len);
		}

		session->payload = new_payload;
		memcpy(session->payload + session->payload_len, *payload, *payload_len);
		session->payload_len += *payload_len;

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
		char *new_payload;

		*payload_len = buf[1] & 0x7f;

		/* Make the payload available so the user can look at the reason code if they so desire */
		if ((*payload_len) && (new_payload = ast_realloc(session->payload, *payload_len))) {
			session->payload = new_payload;
			memcpy(session->payload, &buf[2], *payload_len);
			*payload = session->payload;
		}

		if (!session->closing) {
			ast_websocket_close(session, 0);
		}

		fclose(session->f);
		session->f = NULL;
		ast_verb(2, "WebSocket connection from '%s' closed\n", ast_sockaddr_stringify(&session->address));
	} else {
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
			break;
		}

		if (opcode == AST_WEBSOCKET_OPCODE_TEXT || opcode == AST_WEBSOCKET_OPCODE_BINARY) {
			ast_websocket_write(session, opcode, payload, payload_len);
		} else if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
			break;
		}
	}

end:
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
