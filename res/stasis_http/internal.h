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

#ifndef STASIS_HTTP_INTERNAL_H_
#define STASIS_HTTP_INTERNAL_H_

/*! \file
 *
 * \brief Internal API's for res_stasis_http.
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk/json.h"

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

struct ari_conf_general;

/*! \brief All configuration options for stasis http. */
struct ari_conf {
	/*! The general section configuration options. */
	struct ari_conf_general *general;
	/*! Configured users */
	struct ao2_container *users;
};

/*! Max length for auth_realm field */
#define ARI_AUTH_REALM_LEN 80

/*! \brief Global configuration options for stasis http. */
struct ari_conf_general {
	/*! Enabled by default, disabled if false. */
	int enabled;
	/*! Encoding format used during output (default compact). */
	enum ast_json_encoding_format format;
	/*! Authentication realm */
	char auth_realm[ARI_AUTH_REALM_LEN];

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(allowed_origins);
	);
};

/*! \brief Password format */
enum ari_password_format {
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
struct ari_conf_user {
	/*! Username for authentication */
	char *username;
	/*! User's password. */
	char password[ARI_PASSWORD_LEN];
	/*! Format for the password field */
	enum ari_password_format password_format;
	/*! If true, user cannot execute change operations */
	int read_only;
};

/*!
 * \brief Initialize the ARI configuration
 */
int ari_config_init(void);

/*!
 * \brief Reload the ARI configuration
 */
int ari_config_reload(void);

/*!
 * \brief Destroy the ARI configuration
 */
void ari_config_destroy(void);

/*!
 * \brief Get the current ARI configuration.
 *
 * This is an immutable object, so don't modify it. It is AO2 managed, so
 * ao2_cleanup() when you're done with it.
 *
 * \return ARI configuration object.
 * \return \c NULL on error.
 */
struct ari_conf *ari_config_get(void);

/*!
 * \brief Validated a user's credentials.
 *
 * \param username Name of the user.
 * \param password User's password.
 * \return User object.
 * \return \c NULL if username or password is invalid.
 */
struct ari_conf_user *ari_config_validate_user(const char *username,
	const char *password);

/*! @} */


#endif /* STASIS_HTTP_INTERNAL_H_ */
