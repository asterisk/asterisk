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

#ifdef TEST_FRAMEWORK
	ast_debug(3, "Ignoring STIR/SHAKEN timestamp\n");
	return 0;
#endif

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
	char *public_cert_url;
	char *attestation;
	int mismatch = 0;
	struct ast_stir_shaken_payload *ss_payload;

	if (!session->endpoint->stir_shaken) {
		return 0;
	}

	identity_hdr_val = ast_sip_rdata_get_header_value(rdata, identity_str);
	if (ast_strlen_zero(identity_hdr_val)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_NOT_PRESENT);
		return 0;
	}

	encoded_val = strtok_r(identity_hdr_val, ".", &identity_hdr_val);
	header = ast_base64url_decode_string(encoded_val);
	if (ast_strlen_zero(header)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	encoded_val = strtok_r(identity_hdr_val, ".", &identity_hdr_val);
	payload = ast_base64url_decode_string(encoded_val);
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

	/* Trim "info=<" to get public cert URL */
	strtok_r(identity_hdr_val, "<", &identity_hdr_val);
	public_cert_url = strtok_r(identity_hdr_val, ">", &identity_hdr_val);
	if (ast_strlen_zero(public_cert_url)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	/* Make sure the public URL is actually a URL */
	if (!ast_begins_with(public_cert_url, "http")) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	algorithm = strtok_r(identity_hdr_val, ";", &identity_hdr_val);
	if (ast_strlen_zero(algorithm)) {
		ast_stir_shaken_add_verification(chan, caller_id, "", AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED);
		return 0;
	}

	attestation = get_attestation_from_payload(payload);

	ss_payload = ast_stir_shaken_verify(header, payload, signature, algorithm, public_cert_url);
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

static int add_identity_header(const struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	static const pj_str_t identity_str = { "Identity", 8 };
	pjsip_generic_string_hdr *identity_hdr;
	pj_str_t identity_val;
	pjsip_fromto_hdr *old_identity;
	pjsip_fromto_hdr *to;
	pjsip_sip_uri *uri;
	char *signature;
	char *public_cert_url;
	struct ast_json *header;
	struct ast_json *payload;
	char *dumped_string;
	RAII_VAR(char *, dest_tn, NULL, ast_free);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_free);
	RAII_VAR(struct ast_stir_shaken_payload *, ss_payload, NULL, ast_stir_shaken_payload_free);
	RAII_VAR(char *, encoded_header, NULL, ast_free);
	RAII_VAR(char *, encoded_payload, NULL, ast_free);
	RAII_VAR(char *, combined_str, NULL, ast_free);
	size_t combined_size;

	old_identity = pjsip_msg_find_hdr_by_name(tdata->msg, &identity_str, NULL);
	if (old_identity) {
		return 0;
	}

	to = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_TO, NULL);
	if (!to) {
		ast_log(LOG_ERROR, "Failed to find To header while adding STIR/SHAKEN Identity header\n");
		return -1;
	}

	uri = pjsip_uri_get_uri(to->uri);
	if (!uri) {
		ast_log(LOG_ERROR, "Failed to retrieve URI from To header while adding STIR/SHAKEN Identity header\n");
		return -1;
	}

	dest_tn = ast_malloc(uri->user.slen + 1);
	if (!dest_tn) {
		ast_log(LOG_ERROR, "Failed to allocate memory for STIR/SHAKEN dest->tn\n");
		return -1;
	}

	ast_copy_pj_str(dest_tn, &uri->user, uri->user.slen + 1);

	/* x5u (public key URL), attestation, and origid will be added by ast_stir_shaken_sign */
	json = ast_json_pack("{s: {s: s, s: s, s: s}, s: {s: {s: s}, s: {s: s}}}",
		"header", "alg", "ES256", "ppt", "shaken", "typ", "passport",
		"payload", "dest", "tn", dest_tn, "orig", "tn",
		session->id.number.str);
	if (!json) {
		ast_log(LOG_ERROR, "Failed to allocate memory for STIR/SHAKEN JSON\n");
		return -1;
	}

	ss_payload = ast_stir_shaken_sign(json);
	if (!ss_payload) {
		ast_log(LOG_ERROR, "Failed to allocate memory for STIR/SHAKEN payload\n");
		return -1;
	}

	header = ast_json_object_get(json, "header");
	dumped_string = ast_json_dump_string(header);
	encoded_header = ast_base64url_encode_string(dumped_string);
	ast_json_free(dumped_string);
	if (!encoded_header) {
		ast_log(LOG_ERROR, "Failed to encode STIR/SHAKEN header\n");
		return -1;
	}

	payload = ast_json_object_get(json, "payload");
	dumped_string = ast_json_dump_string(payload);
	encoded_payload = ast_base64url_encode_string(dumped_string);
	ast_json_free(dumped_string);
	if (!encoded_payload) {
		ast_log(LOG_ERROR, "Failed to encode STIR/SHAKEN payload\n");
		return -1;
	}

	signature = (char *)ast_stir_shaken_payload_get_signature(ss_payload);
	public_cert_url = ast_stir_shaken_payload_get_public_cert_url(ss_payload);

	/* The format for the identity header:
	 * header.payload.signature;info=<public_cert_url>alg=STIR_SHAKEN_ENCRYPTION_ALGORITHM;ppt=STIR_SHAKEN_PPT
	 */
	combined_size = strlen(encoded_header) + 1 + strlen(encoded_payload) + 1
		+ strlen(signature) + strlen(";info=<>alg=;ppt=") + strlen(public_cert_url)
		+ strlen(STIR_SHAKEN_ENCRYPTION_ALGORITHM) + strlen(STIR_SHAKEN_PPT) + 1;
	combined_str = ast_calloc(1, combined_size);
	if (!combined_str) {
		ast_log(LOG_ERROR, "Failed to allocate memory for STIR/SHAKEN identity string\n");
		return -1;
	}
	snprintf(combined_str, combined_size, "%s.%s.%s;info=<%s>alg=%s;ppt=%s", encoded_header,
		encoded_payload, signature, public_cert_url, STIR_SHAKEN_ENCRYPTION_ALGORITHM, STIR_SHAKEN_PPT);

	identity_val = pj_str(combined_str);
	identity_hdr = pjsip_generic_string_hdr_create(tdata->pool, &identity_str, &identity_val);
	if (!identity_hdr) {
		ast_log(LOG_ERROR, "Failed to create STIR/SHAKEN Identity header\n");
		return -1;
	}

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)identity_hdr);

	return 0;
}

static void add_date_header(const struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	static const pj_str_t date_str = { "Date", 4 };
	pjsip_fromto_hdr *old_date;

	old_date = pjsip_msg_find_hdr_by_name(tdata->msg, &date_str, NULL);
	if (old_date) {
		ast_debug(3, "Found old STIR/SHAKEN date header, no need to add one\n");
		return;
	}

	ast_sip_add_date_header(tdata);
}

static void stir_shaken_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	if (!session->endpoint->stir_shaken) {
		return;
	}

	if (ast_strlen_zero(session->id.number.str) && session->id.number.valid) {
		return;
	}

	/* If adding the Identity header fails for some reason, there's no point
	 * adding the Date header.
	 */
	if ((add_identity_header(session, tdata)) != 0) {
		return;
	}
	add_date_header(session, tdata);
}

static struct ast_sip_session_supplement stir_shaken_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 1, /* Run AFTER channel creation */
	.incoming_request = stir_shaken_incoming_request,
	.outgoing_request = stir_shaken_outgoing_request,
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP STIR/SHAKEN Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEFAULT,
	.requires = "res_pjsip,res_pjsip_session,res_stir_shaken",
);
