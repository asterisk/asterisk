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
#include "common_config.h"
#include "crypto_utils.h"
#include "curl_utils.h"
#include "attestation.h"
#include "verification.h"

#define STIR_SHAKEN_ENCRYPTION_ALGORITHM "ES256"
#define STIR_SHAKEN_PPT "shaken"
#define STIR_SHAKEN_TYPE "passport"

#define STIR_SHAKEN_VERIFICATION_DS "STIR/SHAKEN/VERIFICATION"
struct stir_shaken_verification_ds {
	/*! The identitifier for the STIR/SHAKEN verification */
	char *identity;
	/*! The attestation value */
	char *attestation;
	/*! The actual verification result */
	enum ast_stir_shaken_vs_response_code verify_result;
};

#define STIR_SHAKEN_ATTESTATION_DS "STIR/SHAKEN/ATTESTATION"
struct stir_shaken_attestation_ds {
	/*! Whether to suppress attestation on outgoing call */
	int suppress;
};

struct stir_shaken_attestation_ds *ast_stir_shaken_get_attestation_datastore(
	struct ast_channel *chan);

/*!
 * \brief Retrieve the stir/shaken sorcery context
 *
 * \retval The stir/shaken sorcery context
 */
struct ast_sorcery *get_sorcery(void);


/*!
 * \brief Return string version of VS response code
 *
 * \param vs_rc
 * \return Response string
 */
const char *vs_response_code_to_str(
	enum ast_stir_shaken_vs_response_code vs_rc);

/*!
 * \brief Return string version of AS response code
 *
 * \param as_rc
 * \return Response string
 */
const char *as_response_code_to_str(
	enum ast_stir_shaken_as_response_code as_rc);

/*!
 * \brief Retrieves the OpenSSL NID for the TN Auth list extension
 * \retval The NID
 */
int get_tn_auth_nid(void);

struct trusted_cert_store {
	X509_STORE *store;
	ast_rwlock_t store_lock;
};

/*!
 * \brief Retrieves the OpenSSL trusted cert store
 * \retval The store
 */
struct trusted_cert_store *get_trusted_cert_store(void);


#endif /* _STIR_SHAKEN_H */
