/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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

/*** MODULEINFO
	<depend>crypto</depend>
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<depend>res_stir_shaken</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"

#include "asterisk/res_stir_shaken.h"

/*!
 * \brief Get the attestation from the payload
 *
 * \param json_str The JSON string representation of the payload
 *
 * \retval Empty string on failure
 * \retval The attestation on success
 */
static char *get_attestation_from_payload(const char *json_str)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_free);
	char *attestation;

	json = ast_json_load_string(json_str, NULL);
	attestation = (char *)ast_json_string_get(ast_json_object_get(json, "attest"));

	if (!ast_strlen_zero(attestation)) {
		return attestation;
	}

	return "";
}

/*!
 * \brief Compare the caller ID from the INVITE with the one in the payload
 *
 * \param json_str The JSON string represntation of the payload
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
static int compare_caller_id(char *caller_id, const char *json_str)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_free);
	char *caller_id_other;

	json = ast_json_load_string(json_str, NULL);
	caller_id_other = (char *)ast_json_string_get(ast_json_object_get(
		ast_json_object_get(json, "orig"), "tn"));

	if (strcmp(caller_id, caller_id_other)) {
		return -1;
	}

	return 0;
}

/*!
 * \brief Compare the current timestamp with the one in the payload. If the difference
 * is greater than the signature timeout, it's not valid anymore
 *
 * \param json_str The JSON string representation of the payload
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
static int compare_timestamp(const char *json_str)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_free);
	long int timestamp;
	struct timeval now = ast_tvnow();

	json = ast_json_load_string(json_str, NULL);
	timestamp = ast_json_integer_get(ast_json_object_get(json, "iat"));

	if (now.tv_sec - timestamp > ast_stir_shaken_get_signature_timeout()) {
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Session supplement callback on an incoming INVITE request
 *
 * When we receive an INVITE, check it for STIR/SHAKEN information and
 * decide what to do from there
 *
 * \param session The session that has received an INVITE
 * \param rdata The incoming INVITE
 */
static int stir_shaken_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	static const pj_str_t identity_str = { "Identity", 8 };
	char *identity_hdr_val;
	char *encoded_val;
	struct ast_channel *chan = session->channel;
	char *caller_id = session->id.number.str;
	RAII_VAR(char *, header, NULL, ast_free);
	RAII_VAR(char *, payload, NULL, ast_free);
	char *signature;
	char *algorithm;
	char *public_key_url;
	char *attestation;
	int mismatch = 0;
	struct ast_stir_shaken_payload *ss_payload;

	identity_hdr_val = ast_sip_rdata_get_header_value(rdata, identity_str);
	if (ast_strlen_zero(identity_hdr_val)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_NOT_PRESENT);
		return 0;
	}

	encoded_val = strtok_r(identity_hdr_val, ".", &identity_hdr_val);
	header = ast_base64decode_string(encoded_val);
	if (ast_strlen_zero(header)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	encoded_val = strtok_r(identity_hdr_val, ".", &identity_hdr_val);
	payload = ast_base64decode_string(encoded_val);
	if (ast_strlen_zero(payload)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	/* It's fine to leave the signature encoded */
	signature = strtok_r(identity_hdr_val, ";", &identity_hdr_val);
	if (ast_strlen_zero(signature)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	/* Trim "info=<" to get public key URL */
	strtok_r(identity_hdr_val, "<", &identity_hdr_val);
	public_key_url = strtok_r(identity_hdr_val, ">", &identity_hdr_val);
	if (ast_strlen_zero(public_key_url)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	algorithm = strtok_r(identity_hdr_val, ";", &identity_hdr_val);
	if (ast_strlen_zero(algorithm)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	attestation = get_attestation_from_payload(payload);

	ss_payload = ast_stir_shaken_verify(header, payload, signature, algorithm, public_key_url);
	if (!ss_payload) {
		ast_stir_shaken_add_verification(chan, caller_id, attestation, AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}
	ast_stir_shaken_payload_free(ss_payload);

	mismatch |= compare_caller_id(caller_id, payload);
	mismatch |= compare_timestamp(payload);

	if (mismatch) {
		ast_stir_shaken_add_verification(chan, caller_id, attestation, AST_STIR_SHAKEN_VERIFY_MISMATCH);
		return 0;
	}

	ast_stir_shaken_add_verification(chan, caller_id, attestation, AST_STIR_SHAKEN_VERIFY_PASSED);

	return 0;
}

static struct ast_sip_session_supplement stir_shaken_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 1, /* Run AFTER channel creation */
	.incoming_request = stir_shaken_incoming_request,
};

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&stir_shaken_supplement);
	return 0;
}

static int load_module(void)
{
	ast_sip_session_register_supplement(&stir_shaken_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

#undef AST_BUILDOPT_SUM
#define AST_BUILDOPT_SUM ""

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER,
				"PSIP STIR/SHAKEN Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEFAULT,
	.requires = "res_pjsip,res_stir_shaken",
);
