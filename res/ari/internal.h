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
#include "asterisk/md5.h"
#include "asterisk/sorcery.h"
#include "asterisk/stringfields.h"
#include "asterisk/websocket_client.h"
#include "ari_websockets.h"


/*! @{ */

/*!
 * \brief Register CLI commands for ARI.
 *
 * \return 0 on success.
 * \return Non-zero on error.
 */
int ari_cli_register(void);

/*!
 * \brief Unregister CLI commands for ARI.
 */
void ari_cli_unregister(void);

/*! @} */

/*! @{ */

/*! \brief Global configuration options for ARI. */
struct ari_conf_general {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! Allowed CORS origins */
		AST_STRING_FIELD(allowed_origins);
		/*! Authentication realm */
		AST_STRING_FIELD(auth_realm);
		/*! Channel variables */
		AST_STRING_FIELD(channelvars);
	);
	/*! Enabled by default, disabled if false. */
	int enabled;
	/*! Write timeout for websocket connections */
	int write_timeout;
	/*! Encoding format used during output (default compact). */
	enum ast_json_encoding_format format;
};

/*! \brief Password format */
enum ari_user_password_format {
	/*! \brief Plaintext password */
	ARI_PASSWORD_FORMAT_PLAIN,
	/*! crypt(3) password */
	ARI_PASSWORD_FORMAT_CRYPT,
};

/*! \brief Per-user configuration options */
struct ari_conf_user {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! User's password. */
		AST_STRING_FIELD(password);
	);
	/*! Format for the password field */
	enum ari_user_password_format password_format;
	/*! If true, user cannot execute change operations */
	int read_only;
};

enum ari_conf_owc_fields {
	ARI_OWC_FIELD_NONE =                    0,
	ARI_OWC_FIELD_WEBSOCKET_CONNECTION_ID = (1 << AST_WS_CLIENT_FIELD_USER_START),
	ARI_OWC_FIELD_APPS =                    (1 << (AST_WS_CLIENT_FIELD_USER_START + 1)),
	ARI_OWC_FIELD_LOCAL_ARI_USER =          (1 << (AST_WS_CLIENT_FIELD_USER_START + 2)),
	ARI_OWC_FIELD_LOCAL_ARI_PASSWORD =      (1 << (AST_WS_CLIENT_FIELD_USER_START + 3)),
	ARI_OWC_FIELD_SUBSCRIBE_ALL =           (1 << (AST_WS_CLIENT_FIELD_USER_START + 4)),
	ARI_OWC_NEEDS_RECONNECT = AST_WS_CLIENT_NEEDS_RECONNECT
	| ARI_OWC_FIELD_WEBSOCKET_CONNECTION_ID | ARI_OWC_FIELD_LOCAL_ARI_USER
	| ARI_OWC_FIELD_LOCAL_ARI_PASSWORD,
	ARI_OWC_NEEDS_REREGISTER = ARI_OWC_FIELD_APPS | ARI_OWC_FIELD_SUBSCRIBE_ALL,
};

struct ari_conf_outbound_websocket {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(websocket_client_id);  /*!< The ID of the websocket client to use */
		AST_STRING_FIELD(apps);          /*!< Stasis apps using this connection */
		AST_STRING_FIELD(local_ari_user);/*!< The ARI user to act as */
		AST_STRING_FIELD(local_ari_password);   /*!< The password for the ARI user */
	);
	int invalid;                         /*!< Invalid configuration */
	int subscribe_all;                   /*!< Subscribe to all events */
	struct ast_websocket_client *websocket_client; /*!< The websocket client */
};

/*!
 * \brief Detect changes between two outbound websocket configurations.
 *
 * \param old_owc The old outbound websocket configuration.
 * \param new_owc The new outbound websocket configuration.
 * \return A bitmask of changed fields.
 */
enum ari_conf_owc_fields ari_conf_owc_detect_changes(
	struct ari_conf_outbound_websocket *old_owc,
	struct ari_conf_outbound_websocket *new_owc);

/*!
 * \brief Get the outbound websocket configuration for a Stasis app.
 *
 * \param app_name The application name to search for.
 * \param ws_type An OR'd list of ari_websocket_types or ARI_WS_TYPE_ANY.
 *
 * \retval ARI outbound websocket configuration object.
 * \retval NULL if not found.
 */
struct ari_conf_outbound_websocket *ari_conf_get_owc_for_app(
	const char *app_name, unsigned int ws_type);

enum ari_conf_load_flags {
	ARI_CONF_INIT =              (1 << 0), /*!< Initialize sorcery */
	ARI_CONF_RELOAD =            (1 << 1), /*!< Reload sorcery */
	ARI_CONF_LOAD_GENERAL = (1 << 2), /*!< Load general config */
	ARI_CONF_LOAD_USER =    (1 << 3), /*!< Load user config */
	ARI_CONF_LOAD_OWC =     (1 << 4), /*!< Load outbound websocket config */
	ARI_CONF_LOAD_ALL =     (         /*!< Load all configs */
		ARI_CONF_LOAD_GENERAL
		| ARI_CONF_LOAD_USER
		| ARI_CONF_LOAD_OWC),
};

/*!
 * \brief (Re)load the ARI configuration
 */
int ari_conf_load(enum ari_conf_load_flags flags);

/*!
 * \brief Destroy the ARI configuration
 */
void ari_conf_destroy(void);

struct ari_conf_general* ari_conf_get_general(void);
struct ao2_container *ari_conf_get_users(void);
struct ari_conf_user *ari_conf_get_user(const char *username);
struct ao2_container *ari_conf_get_owcs(void);
struct ari_conf_outbound_websocket *ari_conf_get_owc(const char *id);
enum ari_conf_owc_fields ari_conf_owc_get_invalid_fields(const char *id);
const char *ari_websocket_type_to_str(enum ast_websocket_type type);
int ari_sorcery_observer_add(const char *object_type,
	const struct ast_sorcery_observer *callbacks);
int ari_sorcery_observer_remove(const char *object_type,
	const struct ast_sorcery_observer *callbacks);

/*!
 * \brief Validated a user's credentials.
 *
 * \param username Name of the user.
 * \param password User's password.
 * \return User object.
 * \retval NULL if username or password is invalid.
 */
struct ari_conf_user *ari_conf_validate_user(const char *username,
	const char *password);

/*! @} */

#endif /* ARI_INTERNAL_H_ */
