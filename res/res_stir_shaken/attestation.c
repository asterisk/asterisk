/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#include <jwt.h>

#define _TRACE_PREFIX_ "a",__LINE__, ""

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/uuid.h"
#include "asterisk/json.h"
#include "asterisk/channel.h"

#include "stir_shaken.h"

static const char *as_rc_map[] = {
	[AST_STIR_SHAKEN_AS_SUCCESS] = "success",
	[AST_STIR_SHAKEN_AS_DISABLED] = "disabled",
	[AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS] = "invalid_arguments",
	[AST_STIR_SHAKEN_AS_MISSING_PARAMETERS] = "missing_parameters",
	[AST_STIR_SHAKEN_AS_INTERNAL_ERROR] = "internal_error",
	[AST_STIR_SHAKEN_AS_NO_TN_FOR_CALLERID] = "no_tn_for_callerid",
	[AST_STIR_SHAKEN_AS_NO_PRIVATE_KEY_AVAIL] = "no_private_key_avail",
	[AST_STIR_SHAKEN_AS_NO_PUBLIC_CERT_URL_AVAIL] = "no_public_cert_url_avail",
	[AST_STIR_SHAKEN_AS_NO_ATTEST_LEVEL] = "no_attest_level",
	[AST_STIR_SHAKEN_AS_IDENTITY_HDR_EXISTS] = "identity_header_exists",
	[AST_STIR_SHAKEN_AS_NO_TO_HDR] = "no_to_hdr",
	[AST_STIR_SHAKEN_AS_TO_HDR_BAD_URI] = "to_hdr_bad_uri",
	[AST_STIR_SHAKEN_AS_SIGN_ENCODE_FAILURE] "sign_encode_failure",
};

const char *as_response_code_to_str(
	enum ast_stir_shaken_as_response_code as_rc)
{
	return ARRAY_IN_BOUNDS(as_rc, as_rc_map) ?
		as_rc_map[as_rc] : NULL;
}

static void ctx_destructor(void *obj)
{
	struct ast_stir_shaken_as_ctx *ctx = obj;

	ao2_cleanup(ctx->etn);
	ast_channel_cleanup(ctx->chan);
	ast_string_field_free_memory(ctx);
	AST_VECTOR_RESET(&ctx->fingerprints, ast_free);
	AST_VECTOR_FREE(&ctx->fingerprints);
}

enum ast_stir_shaken_as_response_code
	ast_stir_shaken_as_ctx_create(const char *orig_tn,
		const char *dest_tn, struct ast_channel *chan,
		const char *profile_name,
		const char *tag, struct ast_stir_shaken_as_ctx **ctxout)
{
	RAII_VAR(struct ast_stir_shaken_as_ctx *, ctx, NULL, ao2_cleanup);
	RAII_VAR(struct profile_cfg *, eprofile, NULL, ao2_cleanup);
	RAII_VAR(struct attestation_cfg *, as_cfg, NULL, ao2_cleanup);
	RAII_VAR(struct tn_cfg *, etn, NULL, ao2_cleanup);
	RAII_VAR(char *, canon_dest_tn , canonicalize_tn_alloc(dest_tn), ast_free);
	RAII_VAR(char *, canon_orig_tn , canonicalize_tn_alloc(orig_tn), ast_free);

	const char *t = S_OR(tag, S_COR(chan, ast_channel_name(chan), ""));
	SCOPE_ENTER(3, "%s: Enter\n", t);

	as_cfg = as_get_cfg();
	if (as_cfg->global_disable) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_DISABLED,
			"%s: Globally disabled\n", t);
	}

	if (ast_strlen_zero(profile_name)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_DISABLED,
			"%s: Disabled due to missing profile name\n", t);
	}

	eprofile = eprofile_get_cfg(profile_name);
	if (!eprofile) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_DISABLED,
		LOG_ERROR, "%s: No profile for profile name '%s'.  Call will continue\n", tag,
			profile_name);
	}

	if (!PROFILE_ALLOW_ATTEST(eprofile)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_DISABLED,
			"%s: Disabled by profile '%s'\n", t, profile_name);
	}

	if (ast_strlen_zero(tag)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS,
			LOG_ERROR, "%s: Must provide tag\n", t);
	}

	if (!canon_orig_tn) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS,
			LOG_ERROR, "%s: Must provide caller_id/orig_tn\n", tag);
	}

	if (!canon_dest_tn) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS,
			LOG_ERROR, "%s: Must provide dest_tn\n", tag);
	}

	if (!ctxout) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS,
			LOG_ERROR, "%s: Must provide ctxout\n", tag);
	}

	etn = tn_get_etn(canon_orig_tn, eprofile);
	if (!etn) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_DISABLED,
			"%s: No tn for orig_tn '%s'\n", tag, canon_orig_tn);
	}

	/* We don't need eprofile or as_cfg anymore so let's clean em up */
	ao2_cleanup(as_cfg);
	as_cfg = NULL;
	ao2_cleanup(eprofile);
	eprofile = NULL;


	if (etn->acfg_common.attest_level == attest_level_NOT_SET) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_MISSING_PARAMETERS,
			LOG_ERROR,
			"'%s': No attest_level specified in tn, profile or attestation objects\n",
			tag);
	}

	if (ast_strlen_zero(etn->acfg_common.public_cert_url)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_NO_PUBLIC_CERT_URL_AVAIL,
			LOG_ERROR, "%s: No public cert url in tn %s, profile or attestation objects\n",
			tag, canon_orig_tn);
	}

	if (etn->acfg_common.raw_key_length == 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_NO_PRIVATE_KEY_AVAIL,
			LOG_ERROR, "%s: No private key in tn %s, profile or attestation objects\n",
			canon_orig_tn, tag);
	}

	ctx = ao2_alloc_options(sizeof(*ctx), ctx_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!ctx) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to allocate memory for ctx\n", tag);
	}

	if (ast_string_field_init(ctx, 1024) != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to allocate memory for ctx\n", tag);
	}

	if (ast_string_field_set(ctx, tag, tag) != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to allocate memory for ctx\n", tag);
	}

	if (ast_string_field_set(ctx, orig_tn, canon_orig_tn) != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to allocate memory for ctx\n", tag);
	}

	if (ast_string_field_set(ctx, dest_tn, canon_dest_tn)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to allocate memory for ctx\n", tag);
	}

	ctx->chan = chan;
	ast_channel_ref(ctx->chan);

	if (AST_VECTOR_INIT(&ctx->fingerprints, 1) != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to allocate memory for ctx\n", tag);
	}

	/* Transfer the references */
	ctx->etn = etn;
	etn = NULL;
	*ctxout = ctx;
	ctx = NULL;

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_SUCCESS, "%s: Done\n", tag);
}

int ast_stir_shaken_as_ctx_wants_fingerprints(struct ast_stir_shaken_as_ctx *ctx)
{
	return ENUM_BOOL(ctx->etn->acfg_common.send_mky, send_mky);
}

enum ast_stir_shaken_as_response_code
	ast_stir_shaken_as_ctx_add_fingerprint(
	struct ast_stir_shaken_as_ctx *ctx, const char *alg, const char *fingerprint)
{
	char *compacted_fp = ast_alloca(strlen(fingerprint) + 1);
	const char *f = fingerprint;
	char *fp = compacted_fp;
	char *combined;
	int rc;
	SCOPE_ENTER(4, "%s: Add fingerprint %s:%s\n", ctx ? ctx->tag : "",
		alg, fingerprint);

	if (!ctx || ast_strlen_zero(alg) || ast_strlen_zero(fingerprint)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS,
			"%s: Missing arguments\n", ctx->tag);
	}

	if (!ENUM_BOOL(ctx->etn->acfg_common.send_mky, send_mky)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_DISABLED,
			"%s: Not needed\n", ctx->tag);
	}

	/* De-colonize */
	while (*f != '\0') {
		if (*f != ':') {
			*fp++ = *f;
		}
		f++;
	}
	*fp = '\0';
	rc = ast_asprintf(&combined, "%s:%s", alg, compacted_fp);
	if (rc < 0) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			"%s: Can't allocate memory for comobined string\n", ctx->tag);
	}

	rc = AST_VECTOR_ADD_SORTED(&ctx->fingerprints, combined, strcasecmp);
	if (rc < 0) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			"%s: Can't add entry to vector\n", ctx->tag);
	}

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_SUCCESS,
		"%s: Done\n", ctx->tag);
}

/*
 * We have to construct the PASSporT payload manually instead of
 * using ast_json_pack.  These macros help make sure nothing
 * leaks if there are errors creating the individual objects.
 */
#define CREATE_JSON_SET_OBJ(__val, __obj, __name) \
({ \
	struct ast_json *__var; \
	if (!(__var = __val)) {\
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR, \
			LOG_ERROR, "%s: Cannot allocate one of the JSON objects\n", \
			ctx->tag); \
	} else { \
		if (ast_json_object_set(__obj, __name, __var)) { \
			SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR, \
				LOG_ERROR, "%s: Cannot set one of the JSON objects\n", \
				ctx->tag); \
		} \
	} \
	(__var); \
})

#define CREATE_JSON_APPEND_ARRAY(__val, __obj) \
({ \
	struct ast_json *__var; \
	if (!(__var = __val)) {\
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR, \
			LOG_ERROR, "%s: Cannot allocate one of the JSON objects\n", \
			ctx->tag); \
	} else { \
		if (ast_json_array_append(__obj, __var)) { \
			SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR, \
				LOG_ERROR, "%s: Cannot set one of the JSON objects\n", \
				ctx->tag); \
		} \
	} \
	(__var); \
})

static enum ast_stir_shaken_as_response_code pack_payload(
	struct ast_stir_shaken_as_ctx *ctx, jwt_t *jwt)
{
	RAII_VAR(struct ast_json *, payload, ast_json_object_create(), ast_json_unref);
	/*
	 * These don't need RAII because once they're added to payload,
	 * they'll get destroyed when payload gets unreffed.
	 */
	struct ast_json *dest;
	struct ast_json *tns;
	struct ast_json *orig;
	char origid[AST_UUID_STR_LEN];
	char *payload_str = NULL;
	SCOPE_ENTER(3, "%s: Enter\n", ctx->tag);

	/*
	 * All fields added need to be in alphabetical order
	 * and there must be no whitespace in the result.
	 *
	 * We can't use ast_json_pack here because the entries
	 * need to be kept in order and the "mky" array may
	 * not be present.
	 */

	/*
	 * The order of the calls matters.  We want to add an object
	 * to its parent as soon as it's created, then add things
	 * to it.  This way if something later fails, the whole thing
	 * will get destroyed when its parent gets destroyed.
	 */
	CREATE_JSON_SET_OBJ(ast_json_string_create(
		attest_level_to_str(ctx->etn->acfg_common.attest_level)),
		payload, "attest");

	dest = CREATE_JSON_SET_OBJ(ast_json_object_create(), payload, "dest");
	tns = CREATE_JSON_SET_OBJ(ast_json_array_create(), dest, "tn");
	CREATE_JSON_APPEND_ARRAY(ast_json_string_create(ctx->dest_tn), tns);

	CREATE_JSON_SET_OBJ(ast_json_integer_create(time(NULL)), payload, "iat");

	if (AST_VECTOR_SIZE(&ctx->fingerprints)
		&& ENUM_BOOL(ctx->etn->acfg_common.send_mky, send_mky)) {
		struct ast_json *mky;
		int i;

		mky = CREATE_JSON_SET_OBJ(ast_json_array_create(), payload, "mky");

		for (i = 0; i < AST_VECTOR_SIZE(&ctx->fingerprints); i++) {
			struct ast_json *mk;
			char *afp = AST_VECTOR_GET(&ctx->fingerprints, i);
			char *fp = strchr(afp, ':');
			*fp++ = '\0';

			mk = CREATE_JSON_APPEND_ARRAY(ast_json_object_create(), mky);
			CREATE_JSON_SET_OBJ(ast_json_string_create(afp), mk, "alg");
			CREATE_JSON_SET_OBJ(ast_json_string_create(fp), mk, "dig");
		}
	}

	orig = CREATE_JSON_SET_OBJ(ast_json_object_create(), payload, "orig");
	CREATE_JSON_SET_OBJ(ast_json_string_create(ctx->orig_tn), orig, "tn");

	ast_uuid_generate_str(origid, sizeof(origid));
	CREATE_JSON_SET_OBJ(ast_json_string_create(origid), payload, "origid");

	payload_str = ast_json_dump_string_format(payload, AST_JSON_COMPACT);
	ast_trace(2, "Payload: %s\n", payload_str);
	jwt_add_grants_json(jwt, payload_str);
	ast_json_free(payload_str);

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_SUCCESS, "Done\n");

}

enum ast_stir_shaken_as_response_code ast_stir_shaken_attest(
	struct ast_stir_shaken_as_ctx *ctx, char **header)
{
	RAII_VAR(jwt_t *, jwt, NULL, jwt_free);
	jwt_alg_t alg;
	char *encoded = NULL;
	enum ast_stir_shaken_as_response_code as_rc;
	int rc = 0;
	SCOPE_ENTER(3, "%s: Attestation: orig: %s dest: %s\n",
		ctx ? ctx->tag : "NULL", ctx ? ctx->orig_tn : "NULL",
		ctx ? ctx->dest_tn : "NULL");

	if (!ctx) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR, LOG_ERROR,
			"%s: No context object!\n", "NULL");
	}

	if (header == NULL) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INVALID_ARGUMENTS,
			LOG_ERROR, "%s: Header buffer was NULL\n", ctx->tag);
	}

	rc = jwt_new(&jwt);
	if (rc != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Cannot create JWT\n", ctx->tag);
	}

	/*
	 * All headers added need to be in alphabetical order!
	 */
	alg = jwt_str_alg(STIR_SHAKEN_ENCRYPTION_ALGORITHM);
	jwt_set_alg(jwt, alg, (const unsigned char *)ctx->etn->acfg_common.raw_key,
		ctx->etn->acfg_common.raw_key_length);
	jwt_add_header(jwt, "ppt", STIR_SHAKEN_PPT);
	jwt_add_header(jwt, "typ", STIR_SHAKEN_TYPE);
	jwt_add_header(jwt, "x5u", ctx->etn->acfg_common.public_cert_url);

	as_rc = pack_payload(ctx, jwt);
	if (as_rc != AST_STIR_SHAKEN_AS_SUCCESS) {
		SCOPE_EXIT_LOG_RTN_VALUE(as_rc,
			LOG_ERROR, "%s: Cannot pack payload\n", ctx->tag);
	}

	encoded = jwt_encode_str(jwt);
	if (!encoded) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_SIGN_ENCODE_FAILURE,
			LOG_ERROR, "%s: Unable to sign/encode JWT\n", ctx->tag);
	}

	rc = ast_asprintf(header, "%s;info=<%s>;alg=%s;ppt=%s",
		encoded, ctx->etn->acfg_common.public_cert_url, jwt_alg_str(alg),
		STIR_SHAKEN_PPT);
	ast_std_free(encoded);
	if (rc < 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_AS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to allocate memory for identity header\n",
			ctx->tag);
	}

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_AS_SUCCESS, "%s: Done\n", ctx->tag);
}

int as_reload()
{
	as_config_reload();

	return 0;
}

int as_unload()
{
	as_config_unload();
	return 0;
}

int as_load()
{
	if (as_config_load()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}
