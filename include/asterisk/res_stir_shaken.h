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

#include "asterisk/sorcery.h"

enum ast_stir_shaken_vs_response_code {
	AST_STIR_SHAKEN_VS_SUCCESS = 0,
	AST_STIR_SHAKEN_VS_DISABLED,
	AST_STIR_SHAKEN_VS_INVALID_ARGUMENTS,
	AST_STIR_SHAKEN_VS_INTERNAL_ERROR,
	AST_STIR_SHAKEN_VS_NO_IDENTITY_HDR,
	AST_STIR_SHAKEN_VS_NO_DATE_HDR,
	AST_STIR_SHAKEN_VS_DATE_HDR_PARSE_FAILURE,
	AST_STIR_SHAKEN_VS_DATE_HDR_EXPIRED,
	AST_STIR_SHAKEN_VS_NO_JWT_HDR,
	AST_STIR_SHAKEN_VS_INVALID_OR_NO_X5U,
	AST_STIR_SHAKEN_VS_CERT_CACHE_MISS,
	AST_STIR_SHAKEN_VS_CERT_CACHE_INVALID,
	AST_STIR_SHAKEN_VS_CERT_CACHE_EXPIRED,
	AST_STIR_SHAKEN_VS_CERT_RETRIEVAL_FAILURE,
	AST_STIR_SHAKEN_VS_CERT_CONTENTS_INVALID,
	AST_STIR_SHAKEN_VS_CERT_NOT_TRUSTED,
	AST_STIR_SHAKEN_VS_CERT_DATE_INVALID,
	AST_STIR_SHAKEN_VS_CERT_NO_TN_AUTH_EXT,
	AST_STIR_SHAKEN_VS_CERT_NO_SPC_IN_TN_AUTH_EXT,
	AST_STIR_SHAKEN_VS_NO_RAW_KEY,
	AST_STIR_SHAKEN_VS_SIGNATURE_VALIDATION,
	AST_STIR_SHAKEN_VS_NO_IAT,
	AST_STIR_SHAKEN_VS_IAT_EXPIRED,
	AST_STIR_SHAKEN_VS_INVALID_OR_NO_PPT,
	AST_STIR_SHAKEN_VS_INVALID_OR_NO_ALG,
	AST_STIR_SHAKEN_VS_INVALID_OR_NO_TYP,
	AST_STIR_SHAKEN_VS_INVALID_OR_NO_GRANTS,
	AST_STIR_SHAKEN_VS_INVALID_OR_NO_ATTEST,
	AST_STIR_SHAKEN_VS_NO_ORIGID,
	AST_STIR_SHAKEN_VS_NO_ORIG_TN,
	AST_STIR_SHAKEN_VS_CID_ORIG_TN_MISMATCH,
	AST_STIR_SHAKEN_VS_NO_DEST_TN,
	AST_STIR_SHAKEN_VS_INVALID_HEADER,
	AST_STIR_SHAKEN_VS_INVALID_GRANT,
	AST_STIR_SHAKEN_VS_INVALID_OR_NO_CID,
	AST_STIR_SHAKEN_VS_RESPONSE_CODE_MAX
};

enum ast_stir_shaken_as_response_code {
	AST_STIR_SHAKEN_AS_SUCCESS = 0,
	AST_STIR_SHAKEN_AS_DISABLED,
	AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS,
	AST_STIR_SHAKEN_AS_MISSING_PARAMETERS,
	AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
	AST_STIR_SHAKEN_AS_NO_TN_FOR_CALLERID,
	AST_STIR_SHAKEN_AS_NO_PRIVATE_KEY_AVAIL,
	AST_STIR_SHAKEN_AS_NO_PUBLIC_CERT_URL_AVAIL,
	AST_STIR_SHAKEN_AS_NO_ATTEST_LEVEL,
	AST_STIR_SHAKEN_AS_IDENTITY_HDR_EXISTS,
	AST_STIR_SHAKEN_AS_NO_TO_HDR,
	AST_STIR_SHAKEN_AS_TO_HDR_BAD_URI,
	AST_STIR_SHAKEN_AS_SIGN_ENCODE_FAILURE,
	AST_STIR_SHAKEN_AS_RESPONSE_CODE_MAX
};

enum stir_shaken_failure_action_enum {
	/*! Unknown value */
	stir_shaken_failure_action_UNKNOWN = -1,
	/*! Continue and let dialplan decide action */
	stir_shaken_failure_action_CONTINUE = 0,
	/*! Reject request with respone codes defined in RFC8224 */
	stir_shaken_failure_action_REJECT_REQUEST,
	/*! Continue but return a Reason header in next provisional response  */
	stir_shaken_failure_action_CONTINUE_RETURN_REASON,
	/*! Not set in config */
	stir_shaken_failure_action_NOT_SET,
};

struct ast_stir_shaken_as_ctx;

/*!
 * \brief Create Attestation Service Context
 *
 * \param caller_id The caller_id for the outgoing call
 * \param dest_tn Canonicalized destination tn
 * \param chan The outgoing channel
 * \param profile_name The profile name on the endpoint
 *                     May be NULL.
 * \param tag Identifying string to output in log and trace messages.
 * \param ctxout Receives a pointer to the newly created context
 *               The caller must release with ao2_ref or ao2_cleanup.

 * \retval AST_STIR_SHAKEN_AS_SUCCESS if successful.
 * \retval AST_STIR_SHAKEN_AS_DISABLED if attestation is disabled
 *         by the endpoint itself, the profile or globally.
 * \retval Other AST_STIR_SHAKEN_AS errors.
 */
enum ast_stir_shaken_as_response_code
	ast_stir_shaken_as_ctx_create(const char *caller_id,
		const char *dest_tn, struct ast_channel *chan,
		const char *profile_name,
		const char *tag, struct ast_stir_shaken_as_ctx **ctxout);

/*!
 * \brief Indicates if the AS context needs DTLS fingerprints
 *
 * \param ctx AS Context
 *
 * \retval 0 Not needed
 * \retval 1 Needed
 */
int ast_stir_shaken_as_ctx_wants_fingerprints(struct ast_stir_shaken_as_ctx *ctx);

/*!
 * \brief Add DTLS fingerprints to AS context
 *
 * \param ctx AS context
 * \param alg Fingerprint algorithm ("sha-1" or "sha-256")
 * \param fingerprint Fingerprint
 *
 * \retval AST_STIR_SHAKEN_AS_SUCCESS if successful
 * \retval Other AST_STIR_SHAKEN_AS errors.
 */
enum ast_stir_shaken_as_response_code ast_stir_shaken_as_ctx_add_fingerprint(
	struct ast_stir_shaken_as_ctx *ctx, const char *alg, const char *fingerprint);

/*!
 * \brief Attest and return Identity header value
 *
 * \param ctx AS Context
 * \param header Pointer to buffer to receive the header value
 *               Must be freed with ast_free when done
 *
 * \retval AST_STIR_SHAKEN_AS_SUCCESS if successful
 * \retval Other AST_STIR_SHAKEN_AS errors.
 */
enum ast_stir_shaken_as_response_code ast_stir_shaken_attest(
	struct ast_stir_shaken_as_ctx *ctx, char **header);


struct ast_stir_shaken_vs_ctx;

/*!
 * \brief Create Verification Service context
 *
 * \param caller_id Incoming caller id
 * \param chan Incoming channel
 * \param profile_name The profile name on the endpoint
 *                     May be NULL.
 * \param endpoint_behavior Behavior associated to the specific
 *                          endpoint
 * \param tag Identifying string to output in log and trace messages.
 * \param ctxout Receives a pointer to the newly created context
 *               The caller must release with ao2_ref or ao2_cleanup.
 *
 * \retval AST_STIR_SHAKEN_VS_SUCCESS if successful.
 * \retval AST_STIR_SHAKEN_VS_DISABLED if verification is disabled
 *         by the endpoint itself, the profile or globally.
 * \retval Other AST_STIR_SHAKEN_VS errors.
 */
enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_ctx_create(const char *caller_id,
		struct ast_channel *chan, const char *profile_name,
		const char *tag, struct ast_stir_shaken_vs_ctx **ctxout);

/*!
 * \brief Sets response code on VS context
 *
 * \param ctx VS context
 * \param vs_rc ast_stir_shaken_vs_response_code to set
 */
void ast_stir_shaken_vs_ctx_set_response_code(
	struct ast_stir_shaken_vs_ctx *ctx,
	enum ast_stir_shaken_vs_response_code vs_rc);

/*!
 * \brief Add the received Identity header value to the VS context
 *
 * \param ctx VS context
 * \param identity_hdr Identity header value
 *
 * \retval AST_STIR_SHAKEN_VS_SUCCESS if successful
 * \retval Other AST_STIR_SHAKEN_VS errors.
 */
enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_ctx_add_identity_hdr(struct ast_stir_shaken_vs_ctx * ctx,
	const char *identity_hdr);

/*!
 * \brief Add the received Date header value to the VS context
 *
 * \param ctx VS context
 * \param date_hdr Date header value
 *
 * \retval AST_STIR_SHAKEN_VS_SUCCESS if successful
 * \retval Other AST_STIR_SHAKEN_VS errors.
 */
enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_ctx_add_date_hdr(struct ast_stir_shaken_vs_ctx * ctx,
	const char *date_hdr);

/*!
 * \brief Get failure_action from context
 *
 * \param ctx VS context
 *
 * \retval ast_stir_shaken_failure_action
 */
enum stir_shaken_failure_action_enum
	ast_stir_shaken_vs_get_failure_action(
		struct ast_stir_shaken_vs_ctx *ctx);

/*!
 * \brief Get use_rfc9410_responses from context
 *
 * \param ctx VS context
 *
 * \retval 1 if true
 * \retval 0 if false
 */
int	ast_stir_shaken_vs_get_use_rfc9410_responses(
		struct ast_stir_shaken_vs_ctx *ctx);

/*!
 * \brief Get caller_id from context
 *
 * \param ctx VS context
 *
 * \retval Caller ID or NULL
 */
const char *ast_stir_shaken_vs_get_caller_id(
		struct ast_stir_shaken_vs_ctx *ctx);

/*!
 * \brief Add a STIR/SHAKEN verification result to a channel
 *
 * \param ctx VS context
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
int ast_stir_shaken_add_result_to_channel(
	struct ast_stir_shaken_vs_ctx *ctx);

/*!
 * \brief Perform incoming call verification
 *
 * \param ctx VS context
 *
 * \retval AST_STIR_SHAKEN_AS_SUCCESS if successful
 * \retval Other AST_STIR_SHAKEN_AS errors.
 */
enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_verify(struct ast_stir_shaken_vs_ctx * ctx);

#endif /* _RES_STIR_SHAKEN_H */
