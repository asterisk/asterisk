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

#ifndef _WEBSOCKET_PRIVATE_H
#define _WEBSOCKET_PRIVATE_H

#include "asterisk/uuid.h"

#include "asterisk/http_websocket.h"

#ifdef HAVE_CURL_WEBSOCKETS
#include <curl/curl.h>
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

#define WS_PING_PAYLOAD "WS_CLIENT_PING"
#define WS_PING_PAYLOAD_LEN 14

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

/*! \brief GUID used to compute the accept key, defined in the specifications */
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

enum ws_client_backend {
	backend_tcptls = 0,
	backend_curl,
	backend_last,
};

/*
 * Typedefs for the client backend virtual functions.
 */
typedef struct ast_websocket *(*client_create)(struct ast_websocket *ws,
	struct ast_websocket_client_options *options, enum ast_websocket_result *result);
typedef enum ast_websocket_result (*client_connect)(struct ast_websocket *ws,
	struct ast_websocket_client_options *options);
typedef int (*client_close)(struct ast_websocket *session, uint16_t reason, int is_reply, int force);
typedef int (*client_read)(struct ast_websocket *session, char **payload, uint64_t *payload_len,
	enum ast_websocket_opcode *opcode, int *fragmented);
typedef int (*client_write)(struct ast_websocket *session, enum ast_websocket_opcode opcode,
	char *payload, uint64_t payload_size);
typedef int (*client_wait_for_input)(struct ast_websocket *session, int timeout);
typedef int (*client_set_timeout)(struct ast_websocket *session, int timeout);
typedef int (*client_set_nonblock)(struct ast_websocket *session);
typedef int (*client_get_fd)(struct ast_websocket *session);
typedef void (*client_data_cleanup)(void *data);

struct client_vtbl {
	struct ast_module *module;
	int supports_proxy;
	client_create create;
	client_connect connect;
	client_close close;
	client_read read;
	client_write write;
	client_wait_for_input wait_for_input;
	client_set_timeout set_timeout;
	client_set_nonblock set_nonblock;
	client_get_fd get_fd;
	client_data_cleanup data_cleanup;
};

struct websocket_client {
	/*! Which client backend is this? */
	enum ws_client_backend backend_type;
	/*! Client vtbl */
	struct client_vtbl *vtbl;
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
	/*! Client backend data */
	void *data;
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
	int ping_sched_timer;               /*!< The ping scheduler timer id */
	int missed_pong_count;              /*!< How many missed pong responses currently */
	char buf[AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE];	    /*!< Fixed buffer for reading data into */
};

#define WS_SESSION_REMOTE(_session) (_session ? (_session->client ? _session->client->options->uri : ast_sockaddr_stringify(&_session->remote_address)) : "NULL")

/*! \brief Structure for a WebSocket server */
struct ast_websocket_server {
	struct ao2_container *protocols; /*!< Container for registered protocols */
};

struct ast_websocket_server *websocket_server_internal_create(void);
/*! \brief Destructor function for sessions */
void session_destroy_fn(void *obj);

struct client_vtbl tcptls_get_client_vtbl(void);

char *websocket_combine_key(const char *key, char *res, int res_size);
int websocket_debug_level_for_opcode(enum ast_websocket_opcode opcode);
void ping_scheduler_cancel(struct ast_websocket *session);

/*
 * The close, read and write functions also get called for server connections.
 */
int tcptls_close(struct ast_websocket *session, uint16_t reason, int is_reply, int force);
int tcptls_read(struct ast_websocket *session, char **payload, uint64_t *payload_len,
	enum ast_websocket_opcode *opcode, int *fragmented);
int tcptls_write(struct ast_websocket *session, enum ast_websocket_opcode opcode,
	char *payload, uint64_t payload_size);
int tcptls_wait_for_input(struct ast_websocket *session, int timeout);
int tcptls_set_timeout(struct ast_websocket *session, int timeout);
int tcptls_set_nonblock(struct ast_websocket *session);
int tcptls_get_fd(struct ast_websocket *session);

int tcptls_register_server(void);
int tcptls_unregister_server(void);
int tcptls_server_uri_cb(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih,
	const char *uri, enum ast_http_method method, struct ast_variable *get_vars, struct ast_variable *headers);

/*
 * These functions should only be used by client backends.
 */
int ast_ws_close(struct ast_websocket *session, uint16_t reason, int is_reply, int force);
const char *ast_ws_opcode2str(enum ast_websocket_opcode opcode);
int ast_ws_handled_pong_or_close(struct ast_websocket *session, char *payload, uint64_t payload_len,
	enum ast_websocket_opcode opcode);

int _ast_ws_unregister_client_backend(struct ast_module *mod, enum ws_client_backend backend);
#define ast_ws_unregister_client_backend(_backend) \
	_ast_ws_unregister_client_backend(ast_module_info->self, _backend)

int _ast_ws_register_client_backend(struct ast_module *mod, enum ws_client_backend backend, struct client_vtbl vtbl);
#define ast_ws_register_client_backend(_backend, _vtbl) \
	_ast_ws_register_client_backend(ast_module_info->self, _backend, _vtbl)
#endif /* _WEBSOCKET_PRIVATE_H */
