/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/time.h"
#include "asterisk/json.h"

#include "asterisk/res_stir_shaken.h"
#include "res_stir_shaken/stir_shaken.h"
#include "res_stir_shaken/general.h"
#include "res_stir_shaken/store.h"
#include "res_stir_shaken/certificate.h"

#define STIR_SHAKEN_ENCRYPTION_ALGORITHM "ES256"
#define STIR_SHAKEN_PPT "shaken"
#define STIR_SHAKEN_TYPE "passport"

static struct ast_sorcery *stir_shaken_sorcery;

struct ast_stir_shaken_payload {
	/*! The JWT header */
	struct ast_json *header;
	/*! The JWT payload */
	struct ast_json *payload;
	/*! Signature for the payload */
	unsigned char *signature;
	/*! The algorithm used */
	char *algorithm;
	/*! THe URL to the public key for the certificate */
	char *public_key_url;
};

struct ast_sorcery *ast_stir_shaken_sorcery(void)
{
	return stir_shaken_sorcery;
}

void ast_stir_shaken_payload_free(struct ast_stir_shaken_payload *payload)
{
	if (!payload) {
		return;
	}

	ast_json_unref(payload->header);
	ast_json_unref(payload->payload);
	ast_free(payload->algorithm);
	ast_free(payload->public_key_url);
	ast_free(payload->signature);

	ast_free(payload);
}

/*!
 * \brief Verifies the necessary contents are in the JSON and returns a
 * ast_stir_shaken_payload with the extracted values.
 *
 * \param json The JSON to verify
 *
 * \return ast_stir_shaken_payload on success
 * \return NULL on failure
 */
static struct ast_stir_shaken_payload *stir_shaken_verify_json(struct ast_json *json)
{
	struct ast_stir_shaken_payload *payload;
	struct ast_json *obj;
	const char *val;

	payload = ast_calloc(1, sizeof(*payload));
	if (!payload) {
		ast_log(LOG_ERROR, "Failed to allocate STIR_SHAKEN payload\n");
		goto cleanup;
	}

	/* Look through the header first */
	obj = ast_json_object_get(json, "header");
	if (!obj) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT did not have the required field 'header'\n");
		goto cleanup;
	}

	payload->header = ast_json_deep_copy(obj);
	if (!payload->header) {
		ast_log(LOG_ERROR, "STIR_SHAKEN payload failed to copy 'header'\n");
		goto cleanup;
	}

	/* Check the ppt value for "shaken" */
	val = ast_json_string_get(ast_json_object_get(obj, "ppt"));
	if (ast_strlen_zero(val)) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT did not have the required field 'ppt'\n");
		goto cleanup;
	}
	if (strcmp(val, STIR_SHAKEN_PPT)) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT field 'ppt' did not have "
			"required value '%s' (was '%s')\n", STIR_SHAKEN_PPT, val);
		goto cleanup;
	}

	/* Check the typ value for "passport" */
	val = ast_json_string_get(ast_json_object_get(obj, "typ"));
	if (ast_strlen_zero(val)) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT did not have the required field 'typ'\n");
		goto cleanup;
	}
	if (strcmp(val, STIR_SHAKEN_TYPE)) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT field 'typ' did not have "
			"required value '%s' (was '%s')\n", STIR_SHAKEN_TYPE, val);
		goto cleanup;
	}

	/* Check the alg value for "ES256" */
	val = ast_json_string_get(ast_json_object_get(obj, "alg"));
	if (ast_strlen_zero(val)) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT did not have required field 'alg'\n");
		goto cleanup;
	}
	if (strcmp(val, STIR_SHAKEN_ENCRYPTION_ALGORITHM)) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT field 'alg' did not have "
			"required value '%s' (was '%s')\n", STIR_SHAKEN_ENCRYPTION_ALGORITHM, val);
		goto cleanup;
	}

	payload->algorithm = ast_strdup(val);
	if (!payload->algorithm) {
		ast_log(LOG_ERROR, "STIR/SHAKEN payload failed to copy 'algorithm'\n");
		goto cleanup;
	}

	/* Now let's check the payload section */
	obj = ast_json_object_get(json, "payload");
	if (!obj) {
		ast_log(LOG_ERROR, "STIR/SHAKEN payload JWT did not have required field 'payload'\n");
		goto cleanup;
	}

	/* Check the orig tn value for not NULL */
	val = ast_json_string_get(ast_json_object_get(ast_json_object_get(obj, "orig"), "tn"));
	if (ast_strlen_zero(val)) {
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT did not have required field 'orig->tn'\n");
		goto cleanup;
	}

	/* Payload seems sane. Copy it and return on success */
	payload->payload = ast_json_deep_copy(obj);
	if (!payload->payload) {
		ast_log(LOG_ERROR, "STIR/SHAKEN payload failed to copy 'payload'\n");
		goto cleanup;
	}

	return payload;

cleanup:
	ast_stir_shaken_payload_free(payload);
	return NULL;
}

/*!
 * \brief Signs the payload and returns the signature.
 *
 * \param json_str The string representation of the JSON
 * \param private_key The private key used to sign the payload
 *
 * \retval signature on success
 * \retval NULL on failure
 */
static unsigned char *stir_shaken_sign(char *json_str, EVP_PKEY *private_key)
{
	EVP_MD_CTX *mdctx = NULL;
	int ret = 0;
	unsigned char *encoded_signature = NULL;
	unsigned char *signature = NULL;
	size_t encoded_length = 0;
	size_t signature_length = 0;

	mdctx = EVP_MD_CTX_create();
	if (!mdctx) {
		ast_log(LOG_ERROR, "Failed to create Message Digest Context\n");
		goto cleanup;
	}

	ret = EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, private_key);
	if (ret != 1) {
		ast_log(LOG_ERROR, "Failed to initialize Message Digest Context\n");
		goto cleanup;
	}

	ret = EVP_DigestSignUpdate(mdctx, json_str, strlen(json_str));
	if (ret != 1) {
		ast_log(LOG_ERROR, "Failed to update Message Digest Context\n");
		goto cleanup;
	}

	ret = EVP_DigestSignFinal(mdctx, NULL, &signature_length);
	if (ret != 1) {
		ast_log(LOG_ERROR, "Failed initial phase of Message Digest Context signing\n");
		goto cleanup;
	}

	signature = ast_calloc(1, sizeof(unsigned char) * signature_length);
	if (!signature) {
		ast_log(LOG_ERROR, "Failed to allocate space for signature\n");
		goto cleanup;
	}

	ret = EVP_DigestSignFinal(mdctx, signature, &signature_length);
	if (ret != 1) {
		ast_log(LOG_ERROR, "Failed final phase of Message Digest Context signing\n");
		goto cleanup;
	}

	/* There are 6 bits to 1 base64 digit, so in order to get the size of the base64 encoded
	 * signature, we need to multiply by the number of bits in a byte and divide by 6. Since
	 * there's rounding when doing base64 conversions, add 3 bytes, just in case, and account
	 * for padding. Add another byte for the NULL-terminator so we don't lose data.
	 */
	encoded_length = ((signature_length * 4 / 3 + 3) & ~3) + 1;
	encoded_signature = ast_calloc(1, encoded_length);
	if (!encoded_signature) {
		ast_log(LOG_ERROR, "Failed to allocate space for encoded signature\n");
		goto cleanup;
	}

	ast_base64encode((char *)encoded_signature, signature, signature_length, encoded_length);

cleanup:
	if (mdctx) {
		EVP_MD_CTX_destroy(mdctx);
	}
	ast_free(signature);

	return encoded_signature;
}

/*!
 * \brief Adds the 'x5u' (public key URL) field to the JWT.
 *
 * \param json The JWT
 * \param x5u The public key URL
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int stir_shaken_add_x5u(struct ast_json *json, const char *x5u)
{
	struct ast_json *value;

	value = ast_json_string_create(x5u);
	if (!value) {
		return -1;
	}

	return ast_json_object_set(ast_json_object_get(json, "header"), "x5u", value);
}

/*!
 * \brief Adds the 'attest' field to the JWT.
 *
 * \param json The JWT
 * \param attest The value to set attest to
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int stir_shaken_add_attest(struct ast_json *json, const char *attest)
{
	struct ast_json *value;

	value = ast_json_string_create(attest);
	if (!value) {
		return -1;
	}

	return ast_json_object_set(ast_json_object_get(json, "payload"), "attest", value);
}

/*!
 * \brief Adds the 'origid' field to the JWT.
 *
 * \param json The JWT
 * \param origid The value to set origid to
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int stir_shaken_add_origid(struct ast_json *json, const char *origid)
{
	struct ast_json *value;

	value = ast_json_string_create(origid);
	if (!origid) {
		return -1;
	}

	return ast_json_object_set(ast_json_object_get(json, "payload"), "origid", value);
}

/*!
 * \brief Adds the 'iat' field to the JWT.
 *
 * \param json The JWT
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int stir_shaken_add_iat(struct ast_json *json)
{
	struct ast_json *value;
	struct timeval tv;
	int timestamp;

	tv = ast_tvnow();
	timestamp = tv.tv_sec + tv.tv_usec / 1000;
	value = ast_json_integer_create(timestamp);

	return ast_json_object_set(ast_json_object_get(json, "payload"), "iat", value);
}

struct ast_stir_shaken_payload *ast_stir_shaken_sign(struct ast_json *json)
{
	struct ast_stir_shaken_payload *payload;
	unsigned char *signature;
	const char *caller_id_num;
	char *json_str = NULL;
	struct stir_shaken_certificate *cert = NULL;

	payload = stir_shaken_verify_json(json);
	if (!payload) {
		return NULL;
	}

	/* From the payload section of the JSON, get the orig section, and then get
	 * the value of tn. This will be the caller ID number */
	caller_id_num = ast_json_string_get(ast_json_object_get(ast_json_object_get(
			ast_json_object_get(json, "payload"), "orig"), "tn"));
	if (!caller_id_num) {
		ast_log(LOG_ERROR, "Failed to get caller ID number from JWT\n");
		goto cleanup;
	}

	cert = stir_shaken_certificate_get_by_caller_id_number(caller_id_num);
	if (!cert) {
		ast_log(LOG_ERROR, "Failed to retrieve certificate for caller ID "
			"'%s'\n", caller_id_num);
		goto cleanup;
	}

	if (stir_shaken_add_x5u(json, stir_shaken_certificate_get_public_key_url(cert))) {
		ast_log(LOG_ERROR, "Failed to add 'x5u' (public key URL) to payload\n");
		goto cleanup;
	}

	/* TODO: This is just a placeholder for adding 'attest', 'iat', and
	 * 'origid' to the payload. Later, additional logic will need to be
	 * added to determine what these values actually are, but the functions
	 * themselves are ready to go.
	 */
	if (stir_shaken_add_attest(json, "B")) {
		ast_log(LOG_ERROR, "Failed to add 'attest' to payload\n");
		goto cleanup;
	}

	if (stir_shaken_add_origid(json, "asterisk")) {
		ast_log(LOG_ERROR, "Failed to add 'origid' to payload\n");
		goto cleanup;
	}

	if (stir_shaken_add_iat(json)) {
		ast_log(LOG_ERROR, "Failed to add 'iat' to payload\n");
		goto cleanup;
	}

	json_str = ast_json_dump_string(json);
	if (!json_str) {
		ast_log(LOG_ERROR, "Failed to convert JSON to string\n");
		goto cleanup;
	}

	signature = stir_shaken_sign(json_str, stir_shaken_certificate_get_private_key(cert));
	if (!signature) {
		goto cleanup;
	}

	payload->signature = signature;
	ao2_cleanup(cert);
	ast_json_free(json_str);

	return payload;

cleanup:
	ao2_cleanup(cert);
	ast_stir_shaken_payload_free(payload);
	ast_json_free(json_str);
	return NULL;
}

static int reload_module(void)
{
	if (stir_shaken_sorcery) {
		ast_sorcery_reload(stir_shaken_sorcery);
	}

	return 0;
}

static int unload_module(void)
{
	stir_shaken_certificate_unload();
	stir_shaken_store_unload();
	stir_shaken_general_unload();

	ast_sorcery_unref(stir_shaken_sorcery);
	stir_shaken_sorcery = NULL;

	return 0;
}

static int load_module(void)
{
	if (!(stir_shaken_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "stir/shaken - failed to open sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stir_shaken_general_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stir_shaken_store_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (stir_shaken_certificate_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_load(ast_stir_shaken_sorcery());

	return AST_MODULE_LOAD_SUCCESS;
}

#undef AST_BUILDOPT_SUM
#define AST_BUILDOPT_SUM ""

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER,
				"STIR/SHAKEN Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 1,
);
