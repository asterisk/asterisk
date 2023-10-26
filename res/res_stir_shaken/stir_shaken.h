/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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
#ifndef _STIR_SHAKEN_H
#define _STIR_SHAKEN_H

#include "asterisk/res_stir_shaken.h"
#include "attestation.h"
#include "common_config.h"
#include "crypto_utils.h"
#include "curl_utils.h"
#include "verification.h"

/*!
 * \brief Retrieve the stir/shaken sorcery context
 *
 * \retval The stir/shaken sorcery context
 */
struct ast_sorcery *ss_sorcery(void);

/*!
 * \brief Return string version of VS response code
 *
 * \param vs_rc
 * \return Response string
 */
const char *ast_stir_shaken_vs_response_code_to_str(
	enum ast_stir_shaken_vs_response_code vs_rc);

/*!
 * \brief Return string version of AS response code
 *
 * \param as_rc
 * \return Response string
 */
const char *ast_stir_shaken_as_response_code_to_str(
	enum ast_stir_shaken_as_response_code as_rc);

/*!
 * \brief Output configuration settings to the Asterisk CLI
 *
 * \param obj A sorcery object containing configuration data
 * \param arg Asterisk CLI argument object
 * \param flags ao2 container flags
 *
 * \retval 0
 */
int stir_shaken_cli_show(void *obj, void *arg, int flags);

/*!
 * \brief Tab completion for name matching with STIR/SHAKEN CLI commands
 *
 * \param word The word to tab complete on
 * \param container The sorcery container to iterate through
 *
 * \retval The tab completion options
 */
char *stir_shaken_tab_complete_name(const char *word, struct ao2_container *container);

/*!
 * \brief Retrieves the OpenSSL NID for the TN Auth list extension
 * \retval The NID
 */
int ss_get_tn_auth_nid(void);

struct ss_trusted_cert_store {
	X509_STORE *store;
	ast_rwlock_t store_lock;
};

/*!
 * \brief Retrieves the OpenSSL trusted cert store
 * \retval The store
 */
struct ss_trusted_cert_store *ss_get_trusted_cert_store(void);


#endif /* _STIR_SHAKEN_H */
