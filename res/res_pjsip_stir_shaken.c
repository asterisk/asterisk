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

#define _TRACE_PREFIX_ "pjss",__LINE__, ""

#include "asterisk/callerid.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"

#include "asterisk/res_stir_shaken.h"

static const pj_str_t identity_hdr_str = { "Identity", 8 };
static const pj_str_t date_hdr_str = { "Date", 4 };

/* Response codes from RFC8224 */
enum sip_response_code {
	SIP_RESPONSE_CODE_OK = 200,
	SIP_RESPONSE_CODE_STALE_DATE = 403,
	SIP_RESPONSE_CODE_USE_IDENTITY_HEADER = 428,
	SIP_RESPONSE_CODE_ANONYMITY_DISALLOWED = 433,
	SIP_RESPONSE_CODE_BAD_IDENTITY_INFO = 436,
	SIP_RESPONSE_CODE_UNSUPPORTED_CREDENTIAL = 437,
	SIP_RESPONSE_CODE_INVALID_IDENTITY_HEADER = 438,
	SIP_RESPONSE_CODE_INTERNAL_ERROR = 500,
};

#define SIP_RESPONSE_CODE_OK_STR "OK"
/* Response strings from RFC8224 */
#define SIP_RESPONSE_CODE_STALE_DATE_STR "Stale Date"
#define SIP_RESPONSE_CODE_USE_IDENTITY_HEADER_STR "Use Identity Header"
#define SIP_RESPONSE_CODE_ANONYMITY_DISALLOWED_STR "Anonymity Disallowed"
#define SIP_RESPONSE_CODE_BAD_IDENTITY_INFO_STR "Bad Identity Info"
#define SIP_RESPONSE_CODE_UNSUPPORTED_CREDENTIAL_STR "Unsupported Credential"
#define SIP_RESPONSE_CODE_INVALID_IDENTITY_HEADER_STR "Invalid Identity Header"
#define SIP_RESPONSE_CODE_INTERNAL_ERROR_STR "Internal Error"

#define response_to_str(_code) \
case _code: \
	return _code ## _STR;

static const char *sip_response_code_to_str(enum sip_response_code code)
{
	switch (code) {
	response_to_str(SIP_RESPONSE_CODE_OK)
	response_to_str(SIP_RESPONSE_CODE_STALE_DATE)
	response_to_str(SIP_RESPONSE_CODE_USE_IDENTITY_HEADER)
	response_to_str(SIP_RESPONSE_CODE_ANONYMITY_DISALLOWED)
	response_to_str(SIP_RESPONSE_CODE_BAD_IDENTITY_INFO)
	response_to_str(SIP_RESPONSE_CODE_UNSUPPORTED_CREDENTIAL)
	response_to_str(SIP_RESPONSE_CODE_INVALID_IDENTITY_HEADER)
	default:
		break;
	}
	return "";
}

#define translate_code(_vs_rc, _sip_rc) \
case AST_STIR_SHAKEN_VS_ ## _vs_rc: \
	return SIP_RESPONSE_CODE_ ## _sip_rc;

static enum sip_response_code vs_code_to_sip_code(
	enum ast_stir_shaken_vs_response_code vs_rc)
{
	/*
	 * We want to use a switch/case statement here because
	 * it'll spit out an error if VS codes are added to the
	 * enum but aren't present here.
	 */
	switch (vs_rc) {
	translate_code(SUCCESS,					OK)
	translate_code(DISABLED, 				OK)
	translate_code(INVALID_ARGUMENTS, 		INTERNAL_ERROR)
	translate_code(INTERNAL_ERROR, 			INTERNAL_ERROR)
	translate_code(NO_IDENTITY_HDR, 		USE_IDENTITY_HEADER)
	translate_code(NO_DATE_HDR, 			STALE_DATE)
	translate_code(DATE_HDR_PARSE_FAILURE, 	STALE_DATE)
	translate_code(DATE_HDR_EXPIRED, 		STALE_DATE)
	translate_code(NO_JWT_HDR, 				INVALID_IDENTITY_HEADER)
	translate_code(INVALID_OR_NO_X5U, 		INVALID_IDENTITY_HEADER)
	translate_code(CERT_CACHE_MISS, 		INVALID_IDENTITY_HEADER)
	translate_code(CERT_CACHE_INVALID, 		INVALID_IDENTITY_HEADER)
	translate_code(CERT_CACHE_EXPIRED, 		INVALID_IDENTITY_HEADER)
	translate_code(CERT_RETRIEVAL_FAILURE, 	BAD_IDENTITY_INFO)
	translate_code(CERT_CONTENTS_INVALID, 	UNSUPPORTED_CREDENTIAL)
	translate_code(CERT_NOT_TRUSTED, 		UNSUPPORTED_CREDENTIAL)
	translate_code(CERT_DATE_INVALID, 		UNSUPPORTED_CREDENTIAL)
	translate_code(CERT_NO_TN_AUTH_EXT, 	UNSUPPORTED_CREDENTIAL)
	translate_code(CERT_NO_SPC_IN_TN_AUTH_EXT, 	UNSUPPORTED_CREDENTIAL)
	translate_code(NO_RAW_KEY, 				UNSUPPORTED_CREDENTIAL)
	translate_code(SIGNATURE_VALIDATION, 	INVALID_IDENTITY_HEADER)
	translate_code(NO_IAT, 					INVALID_IDENTITY_HEADER)
	translate_code(IAT_EXPIRED, 			STALE_DATE)
	translate_code(INVALID_OR_NO_PPT, 		INVALID_IDENTITY_HEADER)
	translate_code(INVALID_OR_NO_ALG, 		INVALID_IDENTITY_HEADER)
	translate_code(INVALID_OR_NO_TYP, 		INVALID_IDENTITY_HEADER)
	translate_code(INVALID_OR_NO_ATTEST, 	INVALID_IDENTITY_HEADER)
	translate_code(NO_ORIGID, 				INVALID_IDENTITY_HEADER)
	translate_code(NO_ORIG_TN, 				INVALID_IDENTITY_HEADER)
	translate_code(NO_DEST_TN, 				INVALID_IDENTITY_HEADER)
	translate_code(INVALID_HEADER, 			INVALID_IDENTITY_HEADER)
	translate_code(INVALID_GRANT, 			INVALID_IDENTITY_HEADER)
	translate_code(INVALID_OR_NO_GRANTS, 	INVALID_IDENTITY_HEADER)
	translate_code(CID_ORIG_TN_MISMATCH, 	INVALID_IDENTITY_HEADER)
	translate_code(INVALID_OR_NO_CID, 		ANONYMITY_DISALLOWED)
	translate_code(RESPONSE_CODE_MAX, 		INVALID_IDENTITY_HEADER)
	}

	return 500;
}

enum process_failure_rc {
	PROCESS_FAILURE_CONTINUE = 0,
	PROCESS_FAILURE_REJECT,
	PROCESS_FAILURE_SYSTEM_FAILURE,
};

static void reject_incoming_call(struct ast_sip_session *session,
	enum sip_response_code response_code)
{
	ast_sip_session_terminate(session, response_code);
	ast_hangup(session->channel);
}

static enum process_failure_rc process_failure(struct ast_stir_shaken_vs_ctx *ctx,
	const char *caller_id, struct ast_sip_session *session,
	pjsip_rx_data *rdata, enum ast_stir_shaken_vs_response_code vs_rc)
{
	enum sip_response_code response_code = vs_code_to_sip_code(vs_rc);
	pj_str_t response_str;
	const char *response_string =
		sip_response_code_to_str(response_code);
	enum stir_shaken_failure_action_enum failure_action =
		ast_stir_shaken_vs_get_failure_action(ctx);
	const char *tag = ast_sip_session_get_name(session);
	SCOPE_ENTER(1, "%s: FA: %d  RC: %d\n", tag,
		failure_action, response_code);

	pj_cstr(&response_str, response_string);

	if (failure_action == stir_shaken_failure_action_REJECT_REQUEST) {
		reject_incoming_call(session, response_code);
		SCOPE_EXIT_RTN_VALUE(PROCESS_FAILURE_REJECT,
			"%s: Rejecting request and terminating session\n",
			tag);
	}

	ast_stir_shaken_vs_ctx_set_response_code(ctx, vs_rc);
	ast_stir_shaken_add_result_to_channel(ctx);

	if (failure_action == stir_shaken_failure_action_CONTINUE_RETURN_REASON) {
		int rc = ast_sip_session_add_reason_header(session,
			ast_stir_shaken_vs_get_use_rfc9410_responses(ctx) ? "STIR" : "SIP",
			response_code, response_str.ptr);
		if (rc != 0) {
			SCOPE_EXIT_RTN_VALUE(PROCESS_FAILURE_SYSTEM_FAILURE,
				"%s: Failed to add Reason header\n", tag);
		}
		SCOPE_EXIT_RTN_VALUE(PROCESS_FAILURE_CONTINUE,
			"%s: Attaching reason code to session\n", tag);
	}
	SCOPE_EXIT_RTN_VALUE(PROCESS_FAILURE_CONTINUE,
		"%s: Continuing\n", tag);
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
	RAII_VAR(struct ast_stir_shaken_vs_ctx *, ctx, NULL, ao2_cleanup);
	RAII_VAR(char *, header, NULL, ast_free);
	RAII_VAR(char *, payload, NULL, ast_free);
	char *identity_hdr_val;
	char *date_hdr_val;
	char *caller_id = session->id.number.str;
	const char *session_name = ast_sip_session_get_name(session);
	struct ast_channel *chan = session->channel;
	enum ast_stir_shaken_vs_response_code vs_rc;
	enum process_failure_rc p_rc;
	SCOPE_ENTER(1, "%s: Enter\n", session_name);

	if (!session) {
		SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "No session\n");
	}
	if (!session->channel) {
		SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "%s: No channel\n", session_name);
	}
	if (!rdata) {
		SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "%s: No rdata\n", session_name);
	}

	/* Check if this is a reinvite. If it is, we don't need to do anything */
	if (rdata->msg_info.to->tag.slen) {
		SCOPE_EXIT_RTN_VALUE(0, "%s: Reinvite. No action needed\n", session_name);
	}

	/*
	 * Shortcut:  If there's no profile name just bail now.
	 */
	if (ast_strlen_zero(session->endpoint->stir_shaken_profile)) {
		SCOPE_EXIT_RTN_VALUE(0, "%s: No profile name on endpoint. No action needed\n", session_name);
	}

	vs_rc = ast_stir_shaken_vs_ctx_create(caller_id, chan,
		session->endpoint->stir_shaken_profile,
		session_name, &ctx);
	if (vs_rc == AST_STIR_SHAKEN_VS_DISABLED) {
		SCOPE_EXIT_RTN_VALUE(0, "%s: VS Disabled\n", session_name);
	} else if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		reject_incoming_call(session, 500);
		SCOPE_EXIT_RTN_VALUE(1, "%s: Unable to create context.  Call terminated\n",
			session_name);
	}

	if (ast_strlen_zero(ast_stir_shaken_vs_get_caller_id(ctx))) {
		p_rc = process_failure(ctx, caller_id, session, rdata,
			AST_STIR_SHAKEN_VS_INVALID_OR_NO_CID);
		if (p_rc == PROCESS_FAILURE_CONTINUE) {
			SCOPE_EXIT_RTN_VALUE(0, "%s: Invalid or no callerid found.  Call continuing\n",
				session_name);
		}
		SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "%s: Invalid or no callerid found.  Call terminated\n",
			session_name);
	}

	identity_hdr_val = ast_sip_rdata_get_header_value(rdata, identity_hdr_str);
	if (ast_strlen_zero(identity_hdr_val)) {
		p_rc = process_failure(ctx, caller_id, session, rdata,
			AST_STIR_SHAKEN_VS_NO_IDENTITY_HDR);
		if (p_rc == PROCESS_FAILURE_CONTINUE) {
			SCOPE_EXIT_RTN_VALUE(0, "%s: No Identity header found.  Call continuing\n",
				session_name);
		}
		SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "%s: No Identity header found.  Call terminated\n",
			session_name);
	}

	vs_rc = ast_stir_shaken_vs_ctx_add_identity_hdr(ctx, identity_hdr_val);
	if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		reject_incoming_call(session, 500);
		SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "%s: Unable to add Identity header.  Call terminated.\n",
			session_name);
	}

	date_hdr_val = ast_sip_rdata_get_header_value(rdata, date_hdr_str);
	if (!ast_strlen_zero(date_hdr_val)) {
		vs_rc = ast_stir_shaken_vs_ctx_add_date_hdr(ctx, date_hdr_val);
		if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
			reject_incoming_call(session, 500);
			SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "%s: Unable to add Date header.  Call terminated.\n",
				session_name);
		}
	}

	vs_rc = ast_stir_shaken_vs_verify(ctx);
	if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		p_rc = process_failure(ctx, caller_id, session, rdata, vs_rc);
		if (p_rc == PROCESS_FAILURE_CONTINUE) {
			SCOPE_EXIT_RTN_VALUE(0, "%s: Verification failed.  Call continuing\n",
				session_name);
		}
		SCOPE_EXIT_LOG_RTN_VALUE(1, LOG_ERROR, "%s: Verification failed.  Call terminated\n",
			session_name);

	}

	ast_stir_shaken_add_result_to_channel(ctx);

	SCOPE_EXIT_RTN_VALUE(0, "Passed\n");
}

static void add_fingerprints_if_present(struct ast_sip_session *session,
	struct ast_stir_shaken_as_ctx *ctx)
{
	struct ast_sip_session_media_state *ms = session->pending_media_state;
	struct ast_sip_session_media *m = NULL;
	struct ast_rtp_engine_dtls *d = NULL;
	enum ast_rtp_dtls_hash h;
	int i;
	const char *tag = ast_sip_session_get_name(session);
	size_t count = AST_VECTOR_SIZE(&ms->sessions);
	SCOPE_ENTER(4, "%s: Check %zu media sessions for fingerprints\n",
		tag, count);

	if (!ast_stir_shaken_as_ctx_wants_fingerprints(ctx)) {
		SCOPE_EXIT_RTN("%s: Fingerprints not needed\n", tag);
	}

	for (i = 0; i < count; i++) {
		const char *f;

		m = AST_VECTOR_GET(&ms->sessions, i);
		if (!m|| !m->rtp) {
			ast_trace(1, "Session: %d: No session or rtp instance\n", i);
			continue;
		}
		d = ast_rtp_instance_get_dtls(m->rtp);
		h = d->get_fingerprint_hash(m->rtp);
		f = d->get_fingerprint(m->rtp);

		ast_stir_shaken_as_ctx_add_fingerprint(ctx,
			h == AST_RTP_DTLS_HASH_SHA256 ? "sha-256" : "sha-1", f);
	}
	SCOPE_EXIT_RTN("%s: Done\n", tag);
}

static char *get_dest_tn(pjsip_tx_data *tdata, const char *tag)
{
	pjsip_fromto_hdr *to;
	pjsip_sip_uri *uri;
	char *dest_tn = NULL;
	SCOPE_ENTER(4, "%s: Enter\n", tag);

	to = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_TO, NULL);
	if (!to) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Failed to find To header\n", tag);
	}

	uri = pjsip_uri_get_uri(to->uri);
	if (!uri) {
		SCOPE_EXIT_RTN_VALUE(NULL,
			"%s: Failed to retrieve URI from To header\n", tag);
	}

	dest_tn = ast_malloc(uri->user.slen + 1);
	if (!dest_tn) {
		SCOPE_EXIT_RTN_VALUE(NULL,
			"%s: Failed to allocate memory for dest_tn\n", tag);
	}

	ast_copy_pj_str(dest_tn, &uri->user, uri->user.slen + 1);

	SCOPE_EXIT_RTN_VALUE(dest_tn, "%s: Done\n", tag);
}

static void add_date_header(const struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	pjsip_fromto_hdr *old_date;
	const char *session_name = ast_sip_session_get_name(session);
	SCOPE_ENTER(1, "%s: Enter\n", session_name);

	old_date = pjsip_msg_find_hdr_by_name(tdata->msg, &date_hdr_str, NULL);
	if (old_date) {
		SCOPE_EXIT_RTN("Found existing Date header, no need to add one\n");
	}

	ast_sip_add_date_header(tdata);
	SCOPE_EXIT_RTN("Done\n");
}

static void stir_shaken_outgoing_request(struct ast_sip_session *session,
	pjsip_tx_data *tdata)
{
	struct ast_party_id effective_id;
	struct ast_party_id connected_id;
	pjsip_generic_string_hdr *old_identity;
	pjsip_generic_string_hdr *identity_hdr;
	pj_str_t identity_val;
	char *dest_tn;
	char *identity_str;
	struct ast_stir_shaken_as_ctx *ctx = NULL;
	enum ast_stir_shaken_as_response_code as_rc;
	const char *session_name = ast_sip_session_get_name(session);
	SCOPE_ENTER(1, "%s: Enter\n", session_name);

	if (!session) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "No session\n");
	}
	if (!session->channel) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: No channel\n", session_name);
	}
	if (!tdata) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: No tdata\n", session_name);
	}

	old_identity = pjsip_msg_find_hdr_by_name(tdata->msg, &identity_hdr_str, NULL);
	if (old_identity) {
		SCOPE_EXIT_RTN("Found an existing Identity header\n");
	}

	dest_tn = get_dest_tn(tdata, session_name);
	if (!dest_tn) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to find destination tn\n",
			session_name);
	}

	ast_party_id_init(&connected_id);
	ast_channel_lock(session->channel);
	effective_id = ast_channel_connected_effective_id(session->channel);
	ast_party_id_copy(&connected_id, &effective_id);
	ast_channel_unlock(session->channel);

	if (!ast_sip_can_present_connected_id(session, &connected_id)) {
		ast_free(dest_tn);
		ast_party_id_free(&connected_id);
		SCOPE_EXIT_RTN("Unable to get caller id\n");
	}

	as_rc = ast_stir_shaken_as_ctx_create(connected_id.number.str,
		dest_tn, session->channel,
		session->endpoint->stir_shaken_profile,
		session_name, &ctx);

	ast_free(dest_tn);
	ast_party_id_free(&connected_id);

	if (as_rc == AST_STIR_SHAKEN_AS_DISABLED) {
		SCOPE_EXIT_RTN("%s: AS Disabled\n", session_name);
	} else if (as_rc != AST_STIR_SHAKEN_AS_SUCCESS) {
		SCOPE_EXIT_RTN("%s: Unable to create context\n",
			session_name);
	}

	add_date_header(session, tdata);
	add_fingerprints_if_present(session, ctx);

	as_rc = ast_stir_shaken_attest(ctx, &identity_str);
	if (as_rc != AST_STIR_SHAKEN_AS_SUCCESS) {
		ao2_cleanup(ctx);
		SCOPE_EXIT_LOG(LOG_ERROR,
			"%s: Failed to create attestation\n", session_name);
	}

	ast_trace(1, "%s: Identity header: %s\n", session_name, identity_str);
	identity_val = pj_str(identity_str);
	identity_hdr = pjsip_generic_string_hdr_create(tdata->pool, &identity_hdr_str, &identity_val);
	ast_free(identity_str);
	if (!identity_hdr) {
		ao2_cleanup(ctx);
		SCOPE_EXIT_LOG_RTN(LOG_ERROR,
			"%s: Unable to create Identity header\n", session_name);
	}

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)identity_hdr);

	ao2_cleanup(ctx);
	SCOPE_EXIT_RTN("Done\n");
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
