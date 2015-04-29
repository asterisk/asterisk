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

#ifndef ARI_INTERNAL_H_
#define ARI_INTERNAL_H_

/*! \file
 *
 * \brief Internal API's for res_ari.
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk/http.h"
#include "asterisk/json.h"
#include "asterisk/stringfields.h"

/*! @{ */

/*!
 * \brief Register CLI commands for ARI.
 *
 * \return 0 on success.
 * \return Non-zero on error.
 */
int ast_ari_cli_register(void);

/*! @} */

/*! @{ */

struct ast_ari_conf_general;

/*! \brief All configuration options for ARI. */
struct ast_ari_conf {
	/*! The general section configuration options. */
	struct ast_ari_conf_general *general;
	/*! Configured users */
	struct ao2_container *users;
};

/*! Max length for auth_realm field */
#define ARI_AUTH_REALM_LEN 80

/*! \brief Global configuration options for ARI. */
struct ast_ari_conf_general {
	/*! Enabled by default, disabled if false. */
	int enabled;
	/*! Write timeout for websocket connections */
	int write_timeout;
	/*! Encoding format used during output (default compact). */
	enum ast_json_encoding_format format;
	/*! Authentication realm */
	char auth_realm[ARI_AUTH_REALM_LEN];

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(allowed_origins);
	);
};

/*! \brief Password format */
enum ast_ari_password_format {
	/*! \brief Plaintext password */
	ARI_PASSWORD_FORMAT_PLAIN,
	/*! crypt(3) password */
	ARI_PASSWORD_FORMAT_CRYPT,
};

/*!
 * \brief User's password mx length.
 *
 * If 256 seems like a lot, a crypt SHA-512 has over 106 characters.
 */
#define ARI_PASSWORD_LEN 256

/*! \brief Per-user configuration options */
struct ast_ari_conf_user {
	/*! Username for authentication */
	char *username;
	/*! User's password. */
	char password[ARI_PASSWORD_LEN];
	/*! Format for the password field */
	enum ast_ari_password_format password_format;
	/*! If true, user cannot execute change operations */
	int read_only;
};

/*!
 * \brief Initialize the ARI configuration
 */
int ast_ari_config_init(void);

/*!
 * \brief Reload the ARI configuration
 */
int ast_ari_config_reload(void);

/*!
 * \brief Destroy the ARI configuration
 */
void ast_ari_config_destroy(void);

/*!
 * \brief Get the current ARI configuration.
 *
 * This is an immutable object, so don't modify it. It is AO2 managed, so
 * ao2_cleanup() when you're done with it.
 *
 * \return ARI configuration object.
 * \return \c NULL on error.
 */
struct ast_ari_conf *ast_ari_config_get(void);

/*!
 * \brief Validated a user's credentials.
 *
 * \param username Name of the user.
 * \param password User's password.
 * \return User object.
 * \return \c NULL if username or password is invalid.
 */
struct ast_ari_conf_user *ast_ari_config_validate_user(const char *username,
	const char *password);

/*! @} */

/* Forward-declare websocket structs. This avoids including http_websocket.h,
 * which causes optional_api stuff to happen, which makes optional_api more
 * difficult to debug. */

struct ast_websocket_server;

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
void ari_handle_websocket(struct ast_websocket_server *ws_server,
	struct ast_tcptls_session_instance *ser, const char *uri,
	enum ast_http_method method, struct ast_variable *get_params,
	struct ast_variable *headers);

#endif /* ARI_INTERNAL_H_ */
