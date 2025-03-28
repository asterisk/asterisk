/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#ifndef ARI_WEBSOCKETS_H_
#define ARI_WEBSOCKETS_H_

/*! \file
 *
 * \brief Internal API's for websockets.
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk/http.h"
#include "asterisk/json.h"
#include "asterisk/vector.h"
#include "asterisk/websocket_client.h"

struct ast_ari_events_event_websocket_args;

/* Forward-declare websocket structs. This avoids including http_websocket.h,
 * which causes optional_api stuff to happen, which makes optional_api more
 * difficult to debug. */

struct ast_websocket;

/*
 * Since we create a "stasis-<appname>" dialplan context for each
 * stasis app, we need to make sure that the total length will be
 * <= AST_MAX_CONTEXT
 */
#define STASIS_CONTEXT_PREFIX "stasis-"
#define STASIS_CONTEXT_PREFIX_LEN (sizeof(STASIS_CONTEXT_PREFIX) - 1)
#define ARI_MAX_APP_NAME_LEN (AST_MAX_CONTEXT - STASIS_CONTEXT_PREFIX_LEN)

struct ari_ws_session {
	enum ast_websocket_type type;                   /*!< The type of websocket session. */
	struct ast_websocket *ast_ws_session;           /*!< The parent websocket session. */
	int (*validator)(struct ast_json *);            /*!< The message validator. */
	struct ast_vector_string websocket_apps;        /*!< List of Stasis apps registered to
	                                                     the websocket session. */
	int subscribe_all;                              /*!< Flag indicating if all events are subscribed to. */
	AST_VECTOR(, struct ast_json *) message_queue;  /*!< Container for holding delayed messages. */
	char *app_name;                                 /*!< The name of the Stasis application. */
	char *remote_addr;                              /*!< The remote address. */
	struct ari_conf_outbound_websocket *owc;           /*!< The outbound websocket configuration. */
	pthread_t thread;                               /*!< The thread that handles the websocket. */
	char *channel_id;                               /*!< The channel id for per-call websocket. */
	char *channel_name;                             /*!< The channel name for per-call websocket. */
	int stasis_end_sent;                            /*!< Flag indicating if the StasisEnd message was sent. */
	int connected;                                  /*!< Flag indicating if the websocket is connected. */
	int closing;                                    /*!< Flag indicating if the session is closing. */
	char session_id[];                              /*!< The id for the websocket session. */
};

struct ao2_container* ari_websocket_get_sessions(void);
struct ari_ws_session *ari_websocket_get_session(const char *session_id);
struct ari_ws_session *ari_websocket_get_session_by_app(const char *app_name);
const char *ari_websocket_type_to_str(enum ast_websocket_type type);
void ari_websocket_shutdown(struct ari_ws_session *session);
void ari_websocket_shutdown_all(void);
int ari_outbound_websocket_start(struct ari_conf_outbound_websocket *owc);

/*!
 * \internal
 * \brief Send a JSON event to a websocket.
 *
 * \param ari_ws_session ARI websocket session
 * \param app_name       Application name
 * \param message        JSON message
 * \param debug_app      Debug flag for application
 */
void ari_websocket_send_event(struct ari_ws_session *ari_ws_session,
	const char *app_name, struct ast_json *message, int debug_app);

/*!
 * \internal
 * \brief Process an ARI REST over Websocket request
 *
 * \param ari_ws_session  ARI websocket session
 * \param remote_addr     Remote address for log messages
 * \param upgrade_headers HTTP headers from the upgrade request
 * \param app_name        Application name
 * \param msg JSON        Request message
 * \retval 0 on success, -1 on failure
 */
int ari_websocket_process_request(struct ari_ws_session *ast_ws_session,
		const char *remote_addr, struct ast_variable *upgrade_headers,
		const char *app_name, struct ast_json *msg);

/*!
 * \brief Wrapper for invoking the websocket code for an incoming connection.
 *
 * \param ws_server WebSocket server to invoke.
 * \param ser HTTP session.
 * \param uri Requested URI.
 * \param method Requested HTTP method.
 * \param get_params Parsed query parameters.
 * \param headers Parsed HTTP headers.
 */
void ari_handle_websocket(struct ast_tcptls_session_instance *ser,
	const char *uri, enum ast_http_method method,
	struct ast_variable *get_params,
	struct ast_variable *headers);

int ari_websocket_unload_module(void);
int ari_websocket_load_module(int is_enabled);

#endif /* ARI_WEBSOCKETS_H_ */
