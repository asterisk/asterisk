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
#ifndef _RES_STIR_SHAKEN_H
#define _RES_STIR_SHAKEN_H

#define STIR_SHAKEN_ENCRYPTION_ALGORITHM "ES256"
#define STIR_SHAKEN_PPT "shaken"
#define STIR_SHAKEN_TYPE "passport"

enum ast_stir_shaken_verification_result {
	AST_STIR_SHAKEN_VERIFY_NOT_PRESENT, /*! No STIR/SHAKEN information was available */
	AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED, /*! Signature verification failed */
	AST_STIR_SHAKEN_VERIFY_MISMATCH, /*! Contents of the signaling and the STIR/SHAKEN payload did not match */
	AST_STIR_SHAKEN_VERIFY_PASSED, /*! Signature verified and contents match signaling */
};

/*! Different from ast_stir_shaken_verification_result. Used to determine why ast_stir_shaken_verify returned NULL */
enum ast_stir_shaken_verify_failure_reason {
	AST_STIR_SHAKEN_VERIFY_FAILED_MEMORY_ALLOC, /*! Memory allocation failure */
	AST_STIR_SHAKEN_VERIFY_FAILED_TO_GET_CERT, /*! Failed to get the credentials to verify */
	AST_STIR_SHAKEN_VERIFY_FAILED_SIGNATURE_VALIDATION, /*! Failed validating the signature */
};

struct ast_stir_shaken_payload;

struct ast_json;

/*!
 * \brief Retrieve the value for 'signature' from an ast_stir_shaken_payload
 *
 * \param payload The payload
 *
 * \retval The signature
 */
unsigned char *ast_stir_shaken_payload_get_signature(const struct ast_stir_shaken_payload *payload);

/*!
 * \brief Retrieve the value for 'public_cert_url' from an ast_stir_shaken_payload
 *
 * \param payload The payload
 *
 * \retval The public key URL
 */
char *ast_stir_shaken_payload_get_public_cert_url(const struct ast_stir_shaken_payload *payload);

/*!
 * \brief Retrieve the value for 'signature_timeout' from 'general' config object
 *
 * \retval The signature timeout
 */
unsigned int ast_stir_shaken_get_signature_timeout(void);

/*!
 * \brief Add a STIR/SHAKEN verification result to a channel
 *
 * \param chan The channel
 * \param identity The identity
 * \param attestation The attestation
 * \param result The verification result
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
int ast_stir_shaken_add_verification(struct ast_channel *chan, const char *identity, const char *attestation,
	enum ast_stir_shaken_verification_result result);

/*!
 * \brief Verify a JSON STIR/SHAKEN payload
 *
 * \param header The payload header
 * \param payload The payload section
 * \param signature The payload signature
 * \param algorithm The signature algorithm
 * \param public_cert_url The public key URL
 *
 * \retval ast_stir_shaken_payload on success
 * \retval NULL on failure
 */
struct ast_stir_shaken_payload *ast_stir_shaken_verify(const char *header, const char *payload, const char *signature,
	const char *algorithm, const char *public_cert_url);

/*!
 * \brief Same as ast_stir_shaken_verify, but will populate a struct with additional information on failure
 *
 * \note failure_code will be written to in this function
 *
 * \param header The payload header
 * \param payload The payload section
 * \param signature The payload signature
 * \param algorithm The signature algorithm
 * \param public_cert_url The public key URL
 * \param failure_code Additional failure information
 *
 * \retval ast_stir_shaken_payload on success
 * \retval NULL on failure
 */
struct ast_stir_shaken_payload *ast_stir_shaken_verify2(const char *header, const char *payload, const char *signature,
	const char *algorithm, const char *public_cert_url, int *failure_code);

/*!
 * \brief Retrieve the stir/shaken sorcery context
 *
 * \retval The stir/shaken sorcery context
 */
struct ast_sorcery *ast_stir_shaken_sorcery(void);

/*!
 * \brief Free a STIR/SHAKEN payload
 */
void ast_stir_shaken_payload_free(struct ast_stir_shaken_payload *payload);

/*!
 * \brief Sign a JSON STIR/SHAKEN payload
 *
 * \note This function will automatically add the "attest", "iat", and "origid" fields.
 *
 * \param json The JWT to sign
 *
 * \retval ast_stir_shaken_payload on success
 * \retval NULL on failure
 */
struct ast_stir_shaken_payload *ast_stir_shaken_sign(struct ast_json *json);

#endif /* _RES_STIR_SHAKEN_H */
