/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014 - Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASTERISK_PHONEPROV_H
#define _ASTERISK_PHONEPROV_H

#include "asterisk.h"
#include "asterisk/inline_api.h"

enum ast_phoneprov_std_variables {
	AST_PHONEPROV_STD_MAC = 0,
	AST_PHONEPROV_STD_PROFILE,
	AST_PHONEPROV_STD_USERNAME,
	AST_PHONEPROV_STD_DISPLAY_NAME,
	AST_PHONEPROV_STD_SECRET,
	AST_PHONEPROV_STD_LABEL,
	AST_PHONEPROV_STD_CALLERID,
	AST_PHONEPROV_STD_TIMEZONE,
	AST_PHONEPROV_STD_LINENUMBER,
	AST_PHONEPROV_STD_LINEKEYS,
	AST_PHONEPROV_STD_SERVER,
	AST_PHONEPROV_STD_SERVER_PORT,
	AST_PHONEPROV_STD_SERVER_IFACE,
	AST_PHONEPROV_STD_VOICEMAIL_EXTEN,
	AST_PHONEPROV_STD_EXTENSION_LENGTH,
	AST_PHONEPROV_STD_TZOFFSET,
	AST_PHONEPROV_STD_DST_ENABLE,
	AST_PHONEPROV_STD_DST_START_MONTH,
	AST_PHONEPROV_STD_DST_START_MDAY,
	AST_PHONEPROV_STD_DST_START_HOUR,
	AST_PHONEPROV_STD_DST_END_MONTH,
	AST_PHONEPROV_STD_DST_END_MDAY,
	AST_PHONEPROV_STD_DST_END_HOUR,
	AST_PHONEPROV_STD_VAR_LIST_LENGTH,	/* This entry must always be the last in the list */
};

/*!
 * \brief Returns the string respresentation of a phoneprov standard variable.
 * \param var One of enum ast_phoneprov_std_variables
 *
 * \return The string representation or NULL if not found.
 */
const char *ast_phoneprov_std_variable_lookup(enum ast_phoneprov_std_variables var);

/*!
 * \brief Causes the provider to load its users.
 *
 * This function is called by phoneprov in response to a
 * ast_phoneprov_provider_register call by the provider.
 * It may also be called by phoneprov to request a reload in
 * response to the res_phoneprov module being reloaded.
 *
 * \retval 0 if successful
 * \retval non-zero if failure
 */
typedef int(*ast_phoneprov_load_users_cb)(void);

/*!
 * \brief Registers a config provider to phoneprov.
 * \param provider_name The name of the provider
 * \param load_users Callback that gathers user variables then loads them by
 * calling ast_phoneprov_add_extension once for each extension.
 *
 * \retval 0 if successful
 * \retval non-zero if failure
 */
int ast_phoneprov_provider_register(char *provider_name,
	ast_phoneprov_load_users_cb load_users);

/*!
 * \brief Unegisters a config provider from phoneprov and frees its resources.
 * \param provider_name The name of the provider
 */
void ast_phoneprov_provider_unregister(char *provider_name);

/*!
 * \brief Adds an extension
 * \param provider_name The name of the provider
 * \param vars An ast_vat_t linked list of the extension's variables.
 * The list is automatically cloned and it must contain at least MACADDRESS
 * and USERNAME entries.
 *
 * \retval 0 if successful
 * \retval non-zero if failure
 */
int ast_phoneprov_add_extension(char *provider_name, struct varshead *vars);

/*!
 * \brief Deletes an extension
 * \param provider_name The name of the provider
 * \param macaddress The mac address of the extension
 */
void ast_phoneprov_delete_extension(char *provider_name, char *macaddress);

/*!
 * \brief Deletes all extensions for this provider
 * \param provider_name The name of the provider
 */
void ast_phoneprov_delete_extensions(char *provider_name);

#endif /* _ASTERISK_PHONEPROV_H */

#ifdef __cplusplus
}
#endif
