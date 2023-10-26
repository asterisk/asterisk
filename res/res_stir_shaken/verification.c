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
#include <curl/curl.h>
#include <sys/stat.h>

#include <jwt.h>
#include <jansson.h>

#include "asterisk.h"

#define _TRACE_PREFIX_ "v",__LINE__, ""

#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astdb.h"
#include "asterisk/conversions.h"
#include "asterisk/utils.h"
#include "asterisk/paths.h"
#include "asterisk/logger.h"
#include "asterisk/acl.h"
#include "asterisk/time.h"
#include "asterisk/localtime.h"
#include "asterisk/crypto.h"
#include "asterisk/json.h"

#include "stir_shaken.h"

#define AST_DB_FAMILY "STIR_SHAKEN"

struct ss_trusted_cert_store trusted_cert_store;

struct ss_trusted_cert_store *ss_get_trusted_cert_store(void)
{
	return &trusted_cert_store;
}

/* Certificates should begin with this */
#define BEGIN_CERTIFICATE_STR "-----BEGIN CERTIFICATE-----"

static const char *vs_rc_map[] = {
	[AST_STIR_SHAKEN_VS_SUCCESS] = "success",
	[AST_STIR_SHAKEN_VS_DISABLED] = "disabled",
	[AST_STIR_SHAKEN_VS_INVALID_ARGUMENTS] = "invalid_arguments",
	[AST_STIR_SHAKEN_VS_INTERNAL_ERROR] = "internal_error",
	[AST_STIR_SHAKEN_VS_NO_IDENTITY_HDR] = "missing_identity_hdr",
	[AST_STIR_SHAKEN_VS_NO_DATE_HDR] = "missing_date_hdr",
	[AST_STIR_SHAKEN_VS_DATE_HDR_PARSE_FAILURE] = "date_hdr_parse_failure",
	[AST_STIR_SHAKEN_VS_DATE_HDR_EXPIRED] = "date_hdr_range_error",
	[AST_STIR_SHAKEN_VS_NO_JWT_HDR] = "missing_jwt_hdr",
	[AST_STIR_SHAKEN_VS_CERT_CACHE_MISS] = "cert_cache_miss",
	[AST_STIR_SHAKEN_VS_CERT_CACHE_INVALID] = "cert_cache_invalid",
	[AST_STIR_SHAKEN_VS_CERT_CACHE_EXPIRED] = "cert_cache_expired",
	[AST_STIR_SHAKEN_VS_CERT_RETRIEVAL_FAILURE] = "cert_retrieval_failure",
	[AST_STIR_SHAKEN_VS_CERT_CONTENTS_INVALID] = "cert_contents_invalid",
	[AST_STIR_SHAKEN_VS_CERT_NOT_TRUSTED] = "cert_not_trusted",
	[AST_STIR_SHAKEN_VS_CERT_DATE_INVALID] = "cert_date_failure",
	[AST_STIR_SHAKEN_VS_CERT_NO_TN_AUTH_EXT] = "cert_no_tn_auth_ext",
	[AST_STIR_SHAKEN_VS_NO_RAW_KEY] = "no_raw_key",
	[AST_STIR_SHAKEN_VS_SIGNATURE_VALIDATION] = "signature_validation",
	[AST_STIR_SHAKEN_VS_NO_IAT] = "missing_iat",
	[AST_STIR_SHAKEN_VS_IAT_EXPIRED] = "iat_range_error",
	[AST_STIR_SHAKEN_VS_INVALID_OR_NO_PPT] = "invalid_or_no_ppt",
	[AST_STIR_SHAKEN_VS_INVALID_OR_NO_ALG] = "invalid_or_no_alg",
	[AST_STIR_SHAKEN_VS_INVALID_OR_NO_TYP] = "invalid_or_no_typ",
	[AST_STIR_SHAKEN_VS_INVALID_OR_NO_GRANTS] = "invalid_or_no_grants",
	[AST_STIR_SHAKEN_VS_INVALID_OR_NO_ATTEST] = "invalid_or_no_attest",
	[AST_STIR_SHAKEN_VS_NO_ORIGID] = "missing_origid",
	[AST_STIR_SHAKEN_VS_NO_ORIG_TN] = "missing_orig_tn",
	[AST_STIR_SHAKEN_VS_CID_ORIG_TN_MISMATCH] = "cid_orig_tn_mismatch",
	[AST_STIR_SHAKEN_VS_NO_DEST_TN] = "missing_dest_tn",
	[AST_STIR_SHAKEN_VS_INVALID_HEADER] = "invalid_header",
	[AST_STIR_SHAKEN_VS_INVALID_GRANT] = "invalid_grant",
};

const char *ast_stir_shaken_vs_response_code_to_str(
	enum ast_stir_shaken_vs_response_code vs_rc)
{
	return ARRAY_IN_BOUNDS(vs_rc, vs_rc_map) ?
		vs_rc_map[vs_rc] : NULL;
}

static void cleanup_cert_from_astdb_and_fs(
	struct ast_stir_shaken_vs_ctx *ctx)
{
	if (ast_db_exists(ctx->hash_family, "path") || ast_db_exists(ctx->hash_family, "expiration")) {
		ast_db_deltree(ctx->hash_family, NULL);
	}

	if (ast_db_exists(ctx->url_family, ctx->public_url)) {
		ast_db_del(ctx->url_family, ctx->public_url);
	}

	/* Remove the actual file from the system */
	remove(ctx->filename);
}

static int add_cert_expiration_to_astdb(struct ast_stir_shaken_vs_ctx *cert,
	const char *cache_control_header, const char *expires_header)
{
	RAII_VAR(struct ss_vs_cfg *, cfg, ss_get_vs_cfg(), ao2_cleanup);

	char time_buf[32];
	time_t current_time = time(NULL);
	time_t max_age_hdr = 0;
	time_t expires_hdr = 0;
	ASN1_TIME *notAfter = NULL;
	time_t cert_expires = 0;
	time_t config_expires = 0;
	time_t expires = 0;
	int rc = 0;

	config_expires = current_time + cfg->max_cache_entry_age;

	if (!ast_strlen_zero(cache_control_header)) {
		char *str_max_age;

		str_max_age = strstr(cache_control_header, "s-maxage");
		if (!str_max_age) {
			str_max_age = strstr(cache_control_header, "max-age");
		}

		if (str_max_age) {
			unsigned int m;
			char *equal = strchr(str_max_age, '=');
			if (equal && !ast_str_to_uint(equal + 1, &m)) {
				max_age_hdr = current_time + m;
			}
		}
	}

	if (!ast_strlen_zero(expires_header)) {
		struct ast_tm expires_time;

		ast_strptime(expires_header, "%a, %d %b %Y %T %z", &expires_time);
		expires_time.tm_isdst = -1;
		expires_hdr = ast_mktime(&expires_time, "GMT").tv_sec;
	}

	notAfter = X509_get_notAfter(cert->xcert);
	cert_expires = ast_crypto_asn_time_as_time_t(notAfter);

	/*
	 * We got all the possible values of expires so
	 * let's find the lowest non-zero value.
	 */

	/* The default */
	expires = config_expires;

	if (max_age_hdr && max_age_hdr < expires) {
		expires = max_age_hdr;
	}

	if (expires_hdr && expires_hdr < expires) {
		expires = expires_hdr;
	}

	if (cert_expires && cert_expires < expires) {
		expires = cert_expires;
	}

	snprintf(time_buf, sizeof(time_buf), "%ld", expires);

	rc = ast_db_put(cert->hash_family, "expiration", time_buf);
	if (rc == 0) {
		strcpy(cert->expiration, time_buf); /* safe */
	}

	return rc;
}

static int add_cert_key_to_astdb(struct ast_stir_shaken_vs_ctx *cert,
	const char *cache_control_hdr, const char *expires_hdr)
{
	int rc = 0;

	rc = ast_db_put(cert->url_family, cert->public_url, cert->hash);
	if (rc) {
		return rc;
	}
	rc = ast_db_put(cert->hash_family, "path", cert->filename);
	if (rc) {
		ast_db_del(cert->url_family, cert->public_url);
		return rc;
	}

	rc = add_cert_expiration_to_astdb(cert, cache_control_hdr, expires_hdr);
	if (rc) {
		ast_db_del(cert->url_family, cert->public_url);
		ast_db_del(cert->hash_family, "path");
	}

	return rc;
}

static int is_cert_cache_entry_expired(char *expiration)
{
	struct timeval current_time = ast_tvnow();
	struct timeval expires = { .tv_sec = 0, .tv_usec = 0 };
	int res = 0;
	SCOPE_ENTER(3, "Checking for cache expiration: %s\n", expiration);

	if (ast_strlen_zero(expiration)) {
		SCOPE_EXIT_RTN_VALUE(1, "No expiration date provided\n");
	}

	if (ast_str_to_ulong(expiration, (unsigned long *)&expires.tv_sec)) {
		SCOPE_EXIT_RTN_VALUE(1, "Couldn't convert expiration string '%s' to ulong",
			expiration);
	}
	ast_trace(2, "Expiration comparison: exp: %" PRIu64 "  curr: %" PRIu64 "  Diff: %" PRIu64 ".\n",
		expires.tv_sec, current_time.tv_sec, expires.tv_sec - current_time.tv_sec);

	res = (ast_tvcmp(current_time, expires) == -1 ? 0 : 1);
	SCOPE_EXIT_RTN_VALUE(res , "entry was %sexpired\n", res ? "" : "not ");
}

static enum ast_stir_shaken_vs_response_code check_cert(
	struct ast_stir_shaken_vs_ctx * ctx)
{
	ASN1_OCTET_STRING *tn_exten;
	int res = 0;
	SCOPE_ENTER(3, "%s: Validating cert '%s'\n", ctx->tag, ctx->public_url);

	ast_rwlock_rdlock(&trusted_cert_store.store_lock);
	ast_trace(3,"%s: Checking ctx against CA ctx\n", ctx->tag);
	res = ast_crypto_is_cert_trusted(trusted_cert_store.store, ctx->xcert);
	ast_rwlock_unlock(&trusted_cert_store.store_lock);
	if (!res) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_NOT_TRUSTED,
			LOG_ERROR, "%s: Cert '%s' not trusted\n",
			ctx->tag, ctx->public_url);
	}

	ast_trace(3,"%s: Attempting to get the raw pubkey\n", ctx->tag);
	ctx->raw_key_len = ast_crypto_get_raw_pubkey_from_cert(ctx->xcert,
		&ctx->raw_key);
	if (ctx->raw_key_len <= 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_NO_RAW_KEY,
			LOG_ERROR, "%s: Unable to extract raw public key from '%s'\n",
			ctx->tag, ctx->public_url);
	}

	ast_trace(3,"%s: Checking cert '%s' validity dates\n",
		ctx->tag, ctx->public_url);
	if (!ast_crypto_is_cert_time_valid(ctx->xcert, ctx->validity_check_time)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_DATE_INVALID,
			LOG_ERROR, "%s: Cert '%s' dates not valid\n",
			ctx->tag, ctx->public_url);
	}

	ast_trace(3,"%s: Checking cert '%s' TNAuth extensions\n",
		ctx->tag, ctx->public_url);
	tn_exten = ast_crypto_get_cert_extension_data(ctx->xcert, ss_get_tn_auth_nid(), NULL);
	if (!tn_exten) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_NO_TN_AUTH_EXT,
			LOG_ERROR, "%s: Cert '%s' doesn't have a TNAuthList extension\n",
			ctx->tag, ctx->public_url);
		return 0;
	}

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_SUCCESS, "%s: Cert '%s' is valid\n",
		ctx->tag, ctx->public_url);
}


static enum ast_stir_shaken_vs_response_code retrieve_cert_from_url(
		struct ast_stir_shaken_vs_ctx *ctx)
{
	FILE *cert_file;
	long http_code;
	int rc = 0;
	enum ast_stir_shaken_vs_response_code vs_rc;
	RAII_VAR(struct curl_header_data *, header_data,
		ast_calloc(1, sizeof(*header_data)), curl_header_data_free);
	RAII_VAR(struct curl_write_data *, write_data,
		ast_calloc(1, sizeof(*write_data)), curl_write_data_free);
	RAII_VAR(struct curl_open_socket_data *, open_socket_data,
		ast_calloc(1, sizeof(*open_socket_data)), curl_open_socket_data_free);

	const char *cache_control;
	const char *expires;
	SCOPE_ENTER(2, "%s: Attempting to retrieve '%s' from net\n",
		ctx->tag, ctx->public_url);

	header_data->debug_info = ast_strdup(ctx->public_url);
	write_data->debug_info = ast_strdup(ctx->public_url);
	write_data->max_download_bytes = 8192;
	write_data->stream_buffer = NULL;
	open_socket_data->debug_info = ast_strdup(ctx->public_url);
	open_socket_data->acl = ctx->acl;

	http_code = curler(ctx->public_url, ctx->vs->curl_timeout,
		write_data, header_data, open_socket_data);

	if (http_code / 100 != 2) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_RETRIEVAL_FAILURE,
			LOG_ERROR, "%s: Failed to retrieve cert %s: code %ld\n",
			ctx->tag, ctx->public_url, http_code);
	}

	if (!ast_begins_with(write_data->stream_buffer, BEGIN_CERTIFICATE_STR)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_CONTENTS_INVALID,
			LOG_ERROR, "%s: Cert '%s' contains invalid data\n",
			ctx->tag, ctx->public_url);
	}

	ctx->xcert = ast_crypto_load_cert_from_memory(write_data->stream_buffer,
		write_data->stream_bytes_downloaded);
	if (!ctx->xcert) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_CONTENTS_INVALID,
			LOG_ERROR, "%s: Cert '%s' was not parseable as an X509 certificate\n",
			ctx->tag, ctx->public_url);
	}

	vs_rc = check_cert(ctx);
	if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		X509_free(ctx->xcert);
		ctx->xcert = NULL;
		SCOPE_EXIT_RTN_VALUE(vs_rc, "%s: Cert '%s' failed validity checks\n",
			ctx->tag, ctx->public_url);
	}

	cert_file = fopen(ctx->filename, "w");
	if (!cert_file) {
		X509_free(ctx->xcert);
		ctx->xcert = NULL;
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Failed to write cert %s: file '%s' %s (%d)\n",
			ctx->tag, ctx->public_url, ctx->filename, strerror(errno), errno);
	}

	rc = fputs(write_data->stream_buffer, cert_file);
	fclose(cert_file);
	if (rc == EOF) {
		X509_free(ctx->xcert);
		ctx->xcert = NULL;
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Failed to write cert %s: file '%s' %s (%d)\n",
			ctx->tag, ctx->public_url, ctx->filename, strerror(errno), errno);
	}

	ast_trace(2, "%s: Cert '%s' written to file '%s'\n",
		ctx->tag, ctx->public_url, ctx->filename);

	ast_trace(2, "%s: Adding cert '%s' to astdb",
		ctx->tag, ctx->public_url);
	cache_control = ast_variable_find_in_list(header_data->headers, "cache-control");
	expires = ast_variable_find_in_list(header_data->headers, "expires");

	rc = add_cert_key_to_astdb(ctx, cache_control, expires);
	if (rc) {
		X509_free(ctx->xcert);
		ctx->xcert = NULL;
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR,
			LOG_ERROR, "%s: Unable to add cert '%s' to ASTDB\n",
			ctx->tag, ctx->public_url);
	}

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_SUCCESS,
		"%s: Cert '%s' successfully retrieved from internet and cached\n",
		ctx->tag, ctx->public_url);
}

static enum ast_stir_shaken_vs_response_code
	retrieve_cert_from_cache(struct ast_stir_shaken_vs_ctx *ctx)
{
	int rc = 0;
	enum ast_stir_shaken_vs_response_code vs_rc;

	SCOPE_ENTER(2, "%s: Attempting to retrieve cert '%s' from cache\n",
		ctx->tag, ctx->public_url);

	if (!ast_db_exists(ctx->hash_family, "path")) {
		cleanup_cert_from_astdb_and_fs(ctx);
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_CACHE_MISS,
			"%s: No cert found in astdb matching %s/path for '%s'\n",
			ctx->tag, ctx->hash_family, ctx->public_url);
	}

	rc = ast_db_get(ctx->hash_family, "expiration", ctx->expiration, sizeof(ctx->expiration));
	if (rc) {
		cleanup_cert_from_astdb_and_fs(ctx);
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_CACHE_MISS,
			"%s: No cert found in astdb matching %s/expiration for '%s'\n",
			ctx->tag, ctx->hash_family, ctx->public_url);
	}

	if (!ast_file_is_readable(ctx->filename)) {
		cleanup_cert_from_astdb_and_fs(ctx);
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_CACHE_MISS,
			"%s: Cert file '%s' was not found or was not readable for '%s'\n",
			ctx->tag, ctx->filename, ctx->public_url);
	}

	if (is_cert_cache_entry_expired(ctx->expiration)) {
		cleanup_cert_from_astdb_and_fs(ctx);
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_CACHE_EXPIRED,
			"%s: Cert file '%s' cache entry was expired for '%s'\n",
			ctx->tag, ctx->filename, ctx->public_url);
	}

	ctx->xcert = ast_crypto_load_cert_from_file(ctx->filename);
	if (!ctx->xcert) {
		cleanup_cert_from_astdb_and_fs(ctx);
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_CERT_CONTENTS_INVALID,
			"%s: Cert file '%s' was not parseable as an X509 certificate for '%s'\n",
			ctx->tag, ctx->filename, ctx->public_url);
	}

	vs_rc = check_cert(ctx);
	if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		X509_free(ctx->xcert);
		ctx->xcert = NULL;
		SCOPE_EXIT_RTN_VALUE(vs_rc, "%s: Cert '%s' failed validity checks\n",
			ctx->tag, ctx->public_url);
	}

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_SUCCESS,
		"%s: Cert '%s' successfully retrieved from cache\n",
		ctx->tag, ctx->public_url);
}

static enum ast_stir_shaken_vs_response_code ctx_populate(
	struct ast_stir_shaken_vs_ctx *ctx)
{
	char hash[41];

	ast_sha1_hash(hash, ctx->public_url);
	if (ast_string_field_set(ctx, hash, hash) != 0) {
		return AST_STIR_SHAKEN_VS_INTERNAL_ERROR;
	}

	if (ast_string_field_build(ctx, filename, "%s/%s.pem",
		ctx->vs->cert_cache_dir, hash) != 0) {
		return AST_STIR_SHAKEN_VS_INTERNAL_ERROR;
	}

	if (ast_string_field_build(ctx, hash_family, "%s/hash/%s",
		AST_DB_FAMILY, hash) != 0) {
		return AST_STIR_SHAKEN_VS_INTERNAL_ERROR;
	}

	if (ast_string_field_build(ctx, url_family, "%s/url", AST_DB_FAMILY) != 0) {
		return AST_STIR_SHAKEN_VS_INTERNAL_ERROR;
	}

	return AST_STIR_SHAKEN_VS_SUCCESS;
}

static enum ast_stir_shaken_vs_response_code
	retrieve_verification_cert(struct ast_stir_shaken_vs_ctx *ctx)
{
	enum ast_stir_shaken_vs_response_code rc = AST_STIR_SHAKEN_VS_SUCCESS;
	SCOPE_ENTER(3, "%s: Retrieving cert '%s'\n", ctx->tag, ctx->public_url);

	ast_trace(1, "%s: Checking cache for cert '%s'\n", ctx->tag, ctx->public_url);
	rc = retrieve_cert_from_cache(ctx);
	if (rc == AST_STIR_SHAKEN_VS_SUCCESS) {
		SCOPE_EXIT_RTN_VALUE(rc, "%s: Using cert '%s' from cache\n",
			ctx->tag, ctx->public_url);;
	}

	ast_trace(1, "%s: No valid cert for '%s' available in cache\n",
		ctx->tag, ctx->public_url);
	ast_trace(1, "%s: Retrieving cert directly from url '%s'\n",
		ctx->tag, ctx->public_url);

	rc = retrieve_cert_from_url(ctx);
	if (rc == AST_STIR_SHAKEN_VS_SUCCESS) {
		SCOPE_EXIT_RTN_VALUE(rc, "%s: Using cert '%s' from internet\n",
			ctx->tag, ctx->public_url);
	}

	SCOPE_EXIT_LOG_RTN_VALUE(rc, LOG_ERROR,
		"%s: Unable to retrieve cert '%s' from cache or internet\n",
		ctx->tag, ctx->public_url);
}

enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_ctx_add_identity_hdr(
	struct ast_stir_shaken_vs_ctx * ctx, const char *identity_hdr)
{
	return ast_string_field_set(ctx, identity_hdr, identity_hdr) == 0 ?
		AST_STIR_SHAKEN_VS_SUCCESS : AST_STIR_SHAKEN_VS_INTERNAL_ERROR;
}

enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_ctx_add_date_hdr(struct ast_stir_shaken_vs_ctx * ctx,
	const char *date_hdr)
{
	return ast_string_field_set(ctx, date_hdr, date_hdr) == 0 ?
		AST_STIR_SHAKEN_VS_SUCCESS : AST_STIR_SHAKEN_VS_INTERNAL_ERROR;
}

enum ast_stir_shaken_failure_action
	ast_stir_shaken_vs_get_failure_action(
		struct ast_stir_shaken_vs_ctx *ctx)
{
	return ctx->failure_action;
}

int	ast_stir_shaken_vs_get_use_rfc9410_responses(
		struct ast_stir_shaken_vs_ctx *ctx)
{
	return ctx->use_rfc9410_responses;
}

void ast_stir_shaken_vs_ctx_set_response_code(
	struct ast_stir_shaken_vs_ctx *ctx,
	enum ast_stir_shaken_vs_response_code vs_rc)
{
	ctx->failure_reason = vs_rc;
}

static void ctx_destructor(void *obj)
{
	struct ast_stir_shaken_vs_ctx *ctx = obj;

	ao2_cleanup(ctx->vs);
	ast_free(ctx->raw_key);
	ast_string_field_free_memory(ctx);
	X509_free(ctx->xcert);
}

enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_ctx_create(const char *caller_id,
		struct ast_channel *chan, const char *profile_name,
		const char *tag, struct ast_stir_shaken_vs_ctx **ctxout)
{
	RAII_VAR(struct ast_stir_shaken_vs_ctx *, ctx, NULL, ao2_cleanup);
	RAII_VAR(struct ss_profile *, profile, NULL, ao2_cleanup);
	RAII_VAR(struct ss_vs_cfg *, vs, NULL, ao2_cleanup);
	const char *t = S_OR(tag, S_COR(chan, ast_channel_name(chan), ""));
	SCOPE_ENTER(3, "%s: Enter\n", t);

	if (ast_strlen_zero(tag)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_ARGUMENTS,
			LOG_ERROR, "%s: Must provide tag\n", t);
	}

	if (ast_strlen_zero(caller_id)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_ARGUMENTS,
		LOG_ERROR, "%s: Must provide caller_id\n", t);
	}

	if (!profile_name) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_DISABLED,
			"%s: Disabled due to missing profile name\n", t);
	}

	vs = ss_get_vs_cfg();
	if (vs->global_disable) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_DISABLED,
			"%s: Globally disabled\n", t);
	}

	profile = ss_get_profile(profile_name);
	if (!profile) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_ARGUMENTS,
		LOG_ERROR, "%s: No profile for profile name '%s'\n", tag,
			profile_name);
	}

	if (profile->behavior != AST_STIR_SHAKEN_BEHAVIOR_ON &&
		profile->behavior != AST_STIR_SHAKEN_BEHAVIOR_VERIFY) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_DISABLED,
			"%s: Disabled by profile\n", t);
	}

	ctx = ao2_alloc_options(sizeof(*ctx), ctx_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!ctx) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR);
	}
	if (ast_string_field_init(ctx, 1024) != 0) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR);
	}

	if (ast_string_field_set(ctx, tag, tag) != 0) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR);
	}

	ctx->acl = (const struct ast_acl_list *)profile->acl;

	ctx->chan = chan;
	if (ast_string_field_set(ctx, caller_id, caller_id) != 0) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR);
	}

	ctx->failure_action = EFFECTIVE_ENUM(profile, vs, failure_action,
		AST_STIR_SHAKEN_VS_FAILURE, AST_STIR_SHAKEN_VS_FAILURE_CONTINUE);

	ctx->use_rfc9410_responses = EFFECTIVE_ENUM_BOOL(profile, vs,
		use_rfc9410_responses, AST_STIR_SHAKEN_VS_RFC9410, AST_STIR_SHAKEN_VS_RFC9410_NO);

	/* Transfer reference to ctx */
	ctx->vs = vs;
	vs = NULL;

	ao2_ref(ctx, +1);
	*ctxout = ctx;
	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_SUCCESS, "%s: Done\n", t);
}

static enum ast_stir_shaken_vs_response_code check_date_header(
	struct ast_stir_shaken_vs_ctx * ctx)
{
	struct ast_tm date_hdr_tm;
	struct timeval date_hdr_timeval;
	struct timeval current_timeval;
	char *remainder;
	char timezone[80] = { 0 };
	int64_t time_diff;
	SCOPE_ENTER(3, "%s: Checking date header: '%s'\n",
		ctx->tag, ctx->date_hdr);

	if (!(remainder = ast_strptime(ctx->date_hdr, "%a, %d %b %Y %T", &date_hdr_tm))) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_DATE_HDR_PARSE_FAILURE,
			LOG_ERROR, "%s: Failed to parse: '%s'\n",
			ctx->tag, ctx->date_hdr);
	}

	sscanf(remainder, "%79s", timezone);

	if (ast_strlen_zero(timezone)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_DATE_HDR_PARSE_FAILURE,
			LOG_ERROR, "%s: A timezone is required: '%s'\n",
			ctx->tag, ctx->date_hdr);
	}

	date_hdr_timeval = ast_mktime(&date_hdr_tm, timezone);
	ctx->date_hdr_time = date_hdr_timeval.tv_sec;
	current_timeval = ast_tvnow();

	time_diff = ast_tvdiff_ms(current_timeval, date_hdr_timeval);
	ast_trace(3, "%zu  %zu  %zu %d\n", current_timeval.tv_sec,
		date_hdr_timeval.tv_sec,
		(current_timeval.tv_sec - date_hdr_timeval.tv_sec), (int)time_diff)
	if (time_diff < 0) {
		/* An INVITE from the future! */
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_DATE_HDR_EXPIRED,
			LOG_ERROR, "%s: Future date: '%s'\n",
			ctx->tag, ctx->date_hdr);
	} else if (time_diff > (ctx->vs->max_date_header_age * 1000)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_DATE_HDR_EXPIRED,
			LOG_ERROR, "%s: More than %u seconds old: '%s'\n",
			ctx->tag, ctx->vs->max_date_header_age, ctx->date_hdr);
	}

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_SUCCESS,
		"%s: Success: '%s'\n", ctx->tag, ctx->date_hdr);
}

#define URL_INVALID_CHAR_SET "?@#&="

enum ast_stir_shaken_vs_response_code
	ast_stir_shaken_vs_verify(struct ast_stir_shaken_vs_ctx * ctx)
{
	RAII_VAR(char *, jwt_encoded, NULL, ast_free);
	RAII_VAR(jwt_t *, jwt, NULL, jwt_free);
	RAII_VAR(struct ast_json *, grants, NULL, ast_json_unref);
	char *p = NULL;
	char *grants_str = NULL;
	const char *x5u;
	const char *ppt_header = NULL;
	const char *grant = NULL;
	time_t now_s = time(NULL);
	time_t iat;
	struct ast_json *grant_obj = NULL;
	int len;
	int rc;
	enum ast_stir_shaken_vs_response_code vs_rc;
	SCOPE_ENTER(3, "%s: Verifying\n", ctx->tag);

	p = strchr(ctx->identity_hdr, ';');
	len = p - ctx->identity_hdr + 1;
	jwt_encoded = ast_malloc(len);
	if (!jwt_encoded) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR, LOG_ERROR,
			"%s: Failed to allocate memory for encoded jwt\n", ctx->tag);
	}

	memcpy(jwt_encoded, ctx->identity_hdr, len);
	jwt_encoded[len - 1] = '\0';

	jwt_decode(&jwt, jwt_encoded, NULL, 0);

	ppt_header = jwt_get_header(jwt, "ppt");
	if (!ppt_header || strcmp(ppt_header, STIR_SHAKEN_PPT)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_PPT, "%s: %s\n",
			ctx->tag, ast_stir_shaken_vs_response_code_to_str(AST_STIR_SHAKEN_VS_INVALID_OR_NO_PPT));
	}

	vs_rc = check_date_header(ctx);
	if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		SCOPE_EXIT_LOG_RTN_VALUE(vs_rc, LOG_ERROR,
			"%s: Date header verification failed\n", ctx->tag);
	}

	x5u = jwt_get_header(jwt, "x5u");
	if (ast_strlen_zero(x5u)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_X5U, LOG_ERROR,
			"%s: No x5u in Identity header\n", ctx->tag);
	}
	if (!ast_begins_with(x5u, "http") || !ast_begins_with(x5u, "https")) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_X5U, LOG_ERROR,
			"%s: x5u '%s' not http or https\n", ctx->tag, x5u);
	}

	if (strpbrk(x5u, URL_INVALID_CHAR_SET)) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_X5U, LOG_ERROR,
			"%s: x5u '%s' can't contain user:password, query parameters or path parameters\n",
			ctx->tag, x5u);
	}

	ast_trace(3, "%s: Decoded enough to get x5u: '%s'\n", ctx->tag, x5u);
	if (ast_string_field_set(ctx, public_url, x5u) != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_INTERNAL_ERROR, LOG_ERROR,
			"%s: Failed to set public_url '%s'\n", ctx->tag, x5u);
	}

	iat = jwt_get_grant_int(jwt, "iat");
	if (iat == 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_NO_IAT, LOG_ERROR,
			"%s: No 'iat' in Identity header\n", ctx->tag);
	}
	ast_trace(1, "date_hdr: %zu  iat: %zu  diff: %zu\n",
		ctx->date_hdr_time, iat, ctx->date_hdr_time - iat);
	if (iat + ctx->vs->max_iat_age < now_s) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_IAT_EXPIRED,
			"%s: iat %ld older than %u seconds\n", ctx->tag,
			iat, ctx->vs->max_iat_age);
	}
	ctx->validity_check_time = iat;

	vs_rc = ctx_populate(ctx);
	if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		SCOPE_EXIT_LOG_RTN_VALUE(vs_rc, LOG_ERROR,
			"%s: Unable to populate ctx\n", ctx->tag);
	}

	vs_rc = retrieve_verification_cert(ctx);
	if (vs_rc != AST_STIR_SHAKEN_VS_SUCCESS) {
		SCOPE_EXIT_LOG_RTN_VALUE(vs_rc, LOG_ERROR,
			"%s: Could not get valid cert from '%s'\n", ctx->tag, ctx->public_url);
	}

	jwt_free(jwt);
	jwt = NULL;

	rc = jwt_decode(&jwt, jwt_encoded, ctx->raw_key, ctx->raw_key_len);
	if (rc != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(AST_STIR_SHAKEN_VS_SIGNATURE_VALIDATION,
			LOG_ERROR, "%s: Signature validation failed for '%s'\n",
			ctx->tag, ctx->public_url);
	}

	ast_trace(1, "%s: Decoding succeeded\n", ctx->tag);

	ppt_header = jwt_get_header(jwt, "alg");
	if (!ppt_header || strcmp(ppt_header, STIR_SHAKEN_ENCRYPTION_ALGORITHM)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_ALG,
			"%s: %s\n", ctx->tag,
			ast_stir_shaken_vs_response_code_to_str(AST_STIR_SHAKEN_VS_INVALID_OR_NO_ALG));
	}

	ppt_header = jwt_get_header(jwt, "ppt");
	if (!ppt_header || strcmp(ppt_header, STIR_SHAKEN_PPT)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_PPT,
			"%s: %s\n", ctx->tag,
			ast_stir_shaken_vs_response_code_to_str(AST_STIR_SHAKEN_VS_INVALID_OR_NO_PPT));
	}

	ppt_header = jwt_get_header(jwt, "typ");
	if (!ppt_header || strcmp(ppt_header, STIR_SHAKEN_TYPE)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_TYP,
			"%s: %s\n", ctx->tag,
			ast_stir_shaken_vs_response_code_to_str(AST_STIR_SHAKEN_VS_INVALID_OR_NO_TYP));
	}

	grants_str = jwt_get_grants_json(jwt, NULL);
	if (ast_strlen_zero(grants_str)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_GRANTS,
			"%s: %s\n", ctx->tag,
			ast_stir_shaken_vs_response_code_to_str(AST_STIR_SHAKEN_VS_INVALID_OR_NO_GRANTS));
	}
	ast_trace(1, "grants: %s\n", grants_str);
	grants = ast_json_load_string(grants_str, NULL);
	ast_std_free(grants_str);
	if (!grants) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_GRANTS,
			"%s: %s\n", ctx->tag,
			ast_stir_shaken_vs_response_code_to_str(AST_STIR_SHAKEN_VS_INVALID_OR_NO_GRANTS));
	}

	grant = ast_json_object_string_get(grants, "attest");
	if (ast_strlen_zero(grant)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_ATTEST,
			"%s: No 'attest' in Identity header\n", ctx->tag);
	}
	if (grant[0] < 'A' || grant[0] > 'C') {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_INVALID_OR_NO_ATTEST,
			"%s: Invalid attest value '%s'\n", ctx->tag, grant);
	}
	ast_string_field_set(ctx, attestation, grant);
	ast_trace(1, "got attest: %s\n", grant);

	grant_obj = ast_json_object_get(grants, "dest");
	if (!grant_obj) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_NO_DEST_TN,
			"%s: No 'dest' in Identity header\n", ctx->tag);
	}
	if (TRACE_ATLEAST(3)) {
		char *otn = ast_json_dump_string(grant_obj);
		ast_trace(1, "got dest: %s\n", otn);
		ast_json_free(otn);
	}

	grant_obj = ast_json_object_get(grants, "orig");
	if (!grant_obj) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_NO_ORIG_TN,
			"%s: No 'orig' in Identity header\n", ctx->tag);
	}
	if (TRACE_ATLEAST(3)) {
		char *otn = ast_json_dump_string(grant_obj);
		ast_trace(1, "got orig: %s\n", otn);
		ast_json_free(otn);
	}
	grant = ast_json_object_string_get(grant_obj, "tn");
	if (!grant) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_NO_ORIG_TN,
			"%s: No 'orig.tn' in Indentity header\n", ctx->tag);
	}
	ast_string_field_set(ctx, orig_tn, grant);
	if (strcmp(ctx->caller_id, ctx->orig_tn) != 0) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_CID_ORIG_TN_MISMATCH,
			"%s: Mismatched cid '%s' and orig_tn '%s'\n", ctx->tag,
			ctx->caller_id, grant);
	}

	grant = ast_json_object_string_get(grants, "origid");
	if (ast_strlen_zero(grant)) {
		SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_NO_ORIGID,
			"%s: No 'origid' in Identity header\n", ctx->tag);
	}

	SCOPE_EXIT_RTN_VALUE(AST_STIR_SHAKEN_VS_SUCCESS,
		"%s: verification succeeded\n", ctx->tag);
}


static char *cli_show_trusted_certs(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
	struct ss_trusted_cert_store *trusted_cert_store;
	STACK_OF(X509_OBJECT) *certs = NULL;
	int count = 0;
	int i = 0;
	char subj[1024];

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken verification show trusted certs";
		e->usage =
			"Usage: stir_shaken verification show trusted certs\n"
			"       Show the stir/shaken verification trusted certs\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	/*
	 * OpenSSL explicitly says to NOT attempt to free
	 * any of these pointers.
	 */
	trusted_cert_store = ss_get_trusted_cert_store();
	ast_rwlock_rdlock(&trusted_cert_store->store_lock);
	certs = X509_STORE_get0_objects(trusted_cert_store->store);
	count = sk_X509_OBJECT_num(certs);
	ast_cli(a->fd, "Count: %d\n", count);
	for (i = 0; i < count ; i++) {
		X509_OBJECT *o = sk_X509_OBJECT_value(certs, i);
		X509 *c = X509_OBJECT_get0_X509(o);
		X509_NAME_oneline(X509_get_subject_name(c), subj, 1024);
		ast_cli(a->fd, "%s\n", subj);
	}
	ast_rwlock_unlock(&trusted_cert_store->store_lock);

	return CLI_SUCCESS;
}

static struct ast_cli_entry verification_cli[] = {
	AST_CLI_DEFINE(cli_show_trusted_certs, "Show stir/shaken verification trusted certs"),
};

static int load_verification_stores(void)
{
	RAII_VAR(struct ss_vs_cfg *, cfg, ss_get_vs_cfg(), ao2_cleanup);

	int rc = 0;
	SCOPED_WRLOCK(local_lock, &trusted_cert_store.store_lock);

	rc = ast_crypto_load_cert_store(trusted_cert_store.store, cfg->ca_file, cfg->ca_path);
	if (rc != 0) {
		ast_log(LOG_ERROR, "Unable to load CA cert store from '%s' or '%s'\n",
			cfg->ca_file, cfg->ca_path);
		return -1;
	}

	if (!ast_strlen_zero(cfg->crl_file) || !ast_strlen_zero(cfg->crl_path)) {
		rc = ast_crypto_load_cert_store(trusted_cert_store.store, cfg->crl_file, cfg->crl_path);
		if (rc != 0) {
			ast_log(LOG_ERROR, "Unable to load CA CRL store from '%s' or '%s'\n",
				cfg->crl_file, cfg->crl_path);
			return -1;
		}
	}

	if (cfg->load_system_certs) {
		X509_STORE_set_default_paths(trusted_cert_store.store);
	}

	X509_STORE_set_flags(trusted_cert_store.store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);

	return 0;
}

static int destroy_verification_stores(void)
{
	SCOPED_WRLOCK(local_lock, &trusted_cert_store.store_lock);
	if (trusted_cert_store.store) {
		X509_STORE_free(trusted_cert_store.store);
		trusted_cert_store.store = NULL;
	}
	return 0;
}

static int create_verification_stores(void)
{
	SCOPED_WRLOCK(local_lock, &trusted_cert_store.store_lock);
	if (trusted_cert_store.store) {
		X509_STORE_free(trusted_cert_store.store);
	}
	trusted_cert_store.store = ast_crypto_create_cert_store();
	if (!trusted_cert_store.store) {
		ast_log(LOG_ERROR, "Unable to create CA cert store\n");
		return -1;
	}
	return 0;
}

static int recreate_verification_stores(void)
{
	SCOPED_WRLOCK(local_lock, &trusted_cert_store.store_lock);
	if (trusted_cert_store.store) {
		X509_STORE_free(trusted_cert_store.store);
	}
	trusted_cert_store.store = ast_crypto_create_cert_store();
	if (!trusted_cert_store.store) {
		ast_log(LOG_ERROR, "Unable to create CA cert store\n");
		return -1;
	}
	return 0;
}

int ss_vs_reload()
{
	ss_vs_config_reload();

	recreate_verification_stores();

	if (load_verification_stores()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

int ss_vs_unload()
{
	destroy_verification_stores();
	ss_vs_config_unload();
	return 0;
}

int ss_vs_load()
{
	ast_rwlock_init_notracking(&trusted_cert_store.store_lock);
	if (create_verification_stores() != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_vs_config_load()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!ss_vs_is_config_loaded()) {
		ast_log(LOG_ERROR, "stir/shaken verification service failed to load\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (load_verification_stores()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(verification_cli,
		ARRAY_LEN(verification_cli));

	return AST_MODULE_LOAD_SUCCESS;
}
