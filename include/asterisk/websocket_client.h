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

#ifndef _RES_WEBSOCKET_CLIENT_H
#define _RES_WEBSOCKET_CLIENT_H

#include "asterisk/http_websocket.h"
#include "asterisk/sorcery.h"

enum ast_ws_client_fields {
	AST_WS_CLIENT_FIELD_NONE =                   0,
	AST_WS_CLIENT_FIELD_URI =                    (1 << 0),
	AST_WS_CLIENT_FIELD_PROTOCOLS =              (1 << 1),
	AST_WS_CLIENT_FIELD_USERNAME =               (1 << 3),
	AST_WS_CLIENT_FIELD_PASSWORD =               (1 << 4),
	AST_WS_CLIENT_FIELD_TLS_ENABLED =            (1 << 7),
	AST_WS_CLIENT_FIELD_CA_LIST_FILE =           (1 << 8),
	AST_WS_CLIENT_FIELD_CA_LIST_PATH =           (1 << 9),
	AST_WS_CLIENT_FIELD_CERT_FILE =              (1 << 10),
	AST_WS_CLIENT_FIELD_PRIV_KEY_FILE =          (1 << 11),
	AST_WS_CLIENT_FIELD_CONNECTION_TYPE =        (1 << 13),
	AST_WS_CLIENT_FIELD_RECONNECT_INTERVAL =     (1 << 14),
	AST_WS_CLIENT_FIELD_RECONNECT_ATTEMPTS =     (1 << 15),
	AST_WS_CLIENT_FIELD_CONNECTION_TIMEOUT =     (1 << 16),
	AST_WS_CLIENT_FIELD_VERIFY_SERVER_CERT =     (1 << 17),
	AST_WS_CLIENT_FIELD_VERIFY_SERVER_HOSTNAME = (1 << 18),
	AST_WS_CLIENT_NEEDS_RECONNECT = AST_WS_CLIENT_FIELD_URI | AST_WS_CLIENT_FIELD_PROTOCOLS
		| AST_WS_CLIENT_FIELD_CONNECTION_TYPE
		| AST_WS_CLIENT_FIELD_USERNAME | AST_WS_CLIENT_FIELD_PASSWORD
		| AST_WS_CLIENT_FIELD_TLS_ENABLED | AST_WS_CLIENT_FIELD_CA_LIST_FILE
		| AST_WS_CLIENT_FIELD_CA_LIST_PATH | AST_WS_CLIENT_FIELD_CERT_FILE
		| AST_WS_CLIENT_FIELD_PRIV_KEY_FILE | AST_WS_CLIENT_FIELD_VERIFY_SERVER_CERT
		| AST_WS_CLIENT_FIELD_VERIFY_SERVER_HOSTNAME,
};

/*
 * The first 23 fields are reserved for the websocket client core.
 */
#define AST_WS_CLIENT_FIELD_USER_START 24

struct ast_websocket_client {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(uri);           /*!< Server URI */
		AST_STRING_FIELD(protocols);     /*!< Websocket protocols to use with server */
		AST_STRING_FIELD(username);      /*!< Auth user name */
		AST_STRING_FIELD(password);      /*!< Auth password */
		AST_STRING_FIELD(ca_list_file);  /*!< CA file */
		AST_STRING_FIELD(ca_list_path);  /*!< CA path */
		AST_STRING_FIELD(cert_file);     /*!< Certificate file */
		AST_STRING_FIELD(priv_key_file); /*!< Private key file */
	);
	int invalid;                         /*!< Invalid configuration */
	enum ast_ws_client_fields invalid_fields;  /*!< Invalid fields */
	enum ast_websocket_type connection_type; /*!< Connection type */
	int connect_timeout;                 /*!< Connection timeout (ms) */
	unsigned int reconnect_attempts;     /*!< How many attempts before returning an error */
	unsigned int reconnect_interval;     /*!< How often to attempt a reconnect (ms) */
	int tls_enabled;                     /*!< TLS enabled */
	int verify_server_cert;              /*!< Verify server certificate */
	int verify_server_hostname;          /*!< Verify server hostname */
	AST_STRING_FIELD_EXTENDED(uri_params); /*!< Additional URI parameters */
};

/*!
 * \brief Retrieve a container of all websocket client objects.
 *
 * \return The container. It may be empty but must always be cleaned up by the caller.
 */
struct ao2_container *ast_websocket_client_retrieve_all(void);

/*!
 * \brief Retrieve a websocket client object by ID.
 *
 * \param id The ID of the websocket client object.
 * \return The websocket client ao2 object or NULL if not found. The reference
 *         must be cleaned up by the caller.
 */
struct ast_websocket_client *ast_websocket_client_retrieve_by_id(const char *id);

/*!
 * \brief Detect changes between two websocket client configurations.
 *
 * \param old_ow The old websocket configuration.
 * \param new_ow The new websocket configuration.
 * \return A bitmask of changed fields.
 */
enum ast_ws_client_fields ast_websocket_client_get_field_diff(
	struct ast_websocket_client *old_wc,
	struct ast_websocket_client *new_wc);

/*!
 * \brief Add sorcery observers for websocket client events.
 *
 * \param callbacks The observer callbacks to add.
 * \return	 0 on success, -1 on failure.
 */
int ast_websocket_client_observer_add(
	const struct ast_sorcery_observer *callbacks);

/*!
 * \brief Remove sorcery observers for websocket client events.
 *
 * \param callbacks The observer callbacks to remove.
 */
void ast_websocket_client_observer_remove(
	const struct ast_sorcery_observer *callbacks);

/*!
 * \brief Connect to a websocket server using the configured authentication,
 *        retry and TLS options.
 *
 * \param wc A pointer to the ast_websocket_structure
 * \param lock_obj A pointer to an ao2 object to lock while the
 *                 connection is being attempted or NULL if no locking is needed.
 * \param display_name An id string to use for logging messages.
 *                     If NULL or empty the connection's ID will be used.
 * \param result A pointer to an enum ast_websocket_result to store the
 *               result of the connection attempt.
 *
 * \return A pointer to the ast_websocket structure on success, or NULL on failure.
 */
struct ast_websocket *ast_websocket_client_connect(struct ast_websocket_client *wc,
	void *lock_obj, const char *display_name, enum ast_websocket_result *result);

/*!
 * \brief Add additional parameters to the URI.
 *
 * \param wc A pointer to the ast_websocket_structure
 * \param uri_params A string containing URLENCODED parameters to append to the URI.
 */
void ast_websocket_client_add_uri_params(struct ast_websocket_client *wc,
	const char *uri_params);

/*!
 * \brief Force res_websocket_client to reload its configuration.
 * \return	 0 on success, -1 on failure.
 */
int ast_websocket_client_reload(void);

#endif /* _RES_WEBSOCKET_CLIENT_H */
