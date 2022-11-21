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
	<depend>curl</depend>
	<depend>res_curl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <openssl/evp.h>

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/time.h"
#include "asterisk/json.h"
#include "asterisk/astdb.h"
#include "asterisk/paths.h"
#include "asterisk/conversions.h"
#include "asterisk/pbx.h"
#include "asterisk/global_datastores.h"
#include "asterisk/app.h"
#include "asterisk/test.h"
#include "asterisk/acl.h"

#include "asterisk/res_stir_shaken.h"
#include "res_stir_shaken/stir_shaken.h"
#include "res_stir_shaken/general.h"
#include "res_stir_shaken/store.h"
#include "res_stir_shaken/certificate.h"
#include "res_stir_shaken/curl.h"
#include "res_stir_shaken/profile.h"

/*** DOCUMENTATION
	<configInfo name="res_stir_shaken" language="en_US">
		<synopsis>STIR/SHAKEN module for Asterisk</synopsis>
		<configFile name="stir_shaken.conf">
			<configObject name="general">
				<synopsis>STIR/SHAKEN general options</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'general'.</synopsis>
				</configOption>
				<configOption name="ca_file" default="">
					<synopsis>File path to the certificate authority certificate</synopsis>
				</configOption>
				<configOption name="ca_path" default="">
					<synopsis>File path to a chain of trust</synopsis>
				</configOption>
				<configOption name="cache_max_size" default="1000">
					<synopsis>Maximum size to use for caching public keys</synopsis>
				</configOption>
				<configOption name="curl_timeout" default="2">
					<synopsis>Maximum time to wait to CURL certificates</synopsis>
				</configOption>
				<configOption name="signature_timeout" default="15">
					<synopsis>Amount of time a signature is valid for</synopsis>
				</configOption>
			</configObject>
			<configObject name="store">
				<synopsis>STIR/SHAKEN certificate store options</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'store'.</synopsis>
				</configOption>
				<configOption name="path" default="">
					<synopsis>Path to a directory containing certificates</synopsis>
				</configOption>
				<configOption name="public_cert_url" default="">
					<synopsis>URL to the public certificate(s)</synopsis>
					<description><para>
					 Must be a valid http, or https, URL. The URL must also contain the ${CERTIFICATE} variable, which is used for public key name substitution.
					 For example: http://mycompany.com/${CERTIFICATE}.pub
					</para></description>
				</configOption>
			</configObject>
			<configObject name="certificate">
				<synopsis>STIR/SHAKEN certificate options</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'certificate'.</synopsis>
				</configOption>
				<configOption name="path" default="">
					<synopsis>File path to a certificate</synopsis>
				</configOption>
				<configOption name="public_cert_url" default="">
					<synopsis>URL to the public certificate</synopsis>
					<description><para>
					 Must be a valid http, or https, URL.
					</para></description>
				</configOption>
				<configOption name="attestation">
					<synopsis>Attestation level</synopsis>
				</configOption>
				<configOption name="caller_id_number" default="">
					<synopsis>The caller ID number to match on.</synopsis>
				</configOption>
			</configObject>
			<configObject name="profile">
				<synopsis>STIR/SHAKEN profile configuration options</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'profile'.</synopsis>
				</configOption>
				<configOption name="stir_shaken" default="on">
					<synopsis>STIR/SHAKEN configuration settings</synopsis>
					<description><para>
					        Attest, verify, or do both STIR/SHAKEN operations. On incoming
						INVITEs, the Identity header will be checked for validity. On
						outgoing INVITEs, an Identity header will be added.</para>
					</description>
				</configOption>
				<configOption name="acllist" default="">
					<synopsis>An existing ACL from acl.conf to use</synopsis>
				</configOption>
				<configOption name="permit" default="">
					<synopsis>An IP or subnet to permit</synopsis>
				</configOption>
				<configOption name="deny" default="">
					<synopsis>An IP or subnet to deny</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
	<function name="STIR_SHAKEN" language="en_US">
		<synopsis>
			Gets the number of STIR/SHAKEN results or a specific STIR/SHAKEN value from a result on the channel.
		</synopsis>
		<syntax>
			<parameter name="index" required="true">
				<para>The index of the STIR/SHAKEN result to get. If only 'count' is passed in, gets the number of STIR/SHAKEN results instead.</para>
			</parameter>
			<parameter name="value" required="false">
				<para>The value to get from the STIR/SHAKEN result. Only used when an index is passed in (instead of 'count'). Allowable values:</para>
				<enumlist>
					<enum name = "identity" />
					<enum name = "attestation" />
					<enum name = "verify_result" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>This function will either return the number of STIR/SHAKEN identities, or return information on the specified identity.
			To get the number of identities, just pass 'count' as the only parameter to the function. If you want to get information on a
			specific STIR/SHAKEN identity, you can get the number of identities and then pass an index as the first parameter and one of
			the values you would like to retrieve as the second parameter.
			</para>
			<example title="Get count and retrieve value">
			same => n,NoOp(Number of STIR/SHAKEN identities: ${STIR_SHAKEN(count)})
			same => n,NoOp(Identity ${STIR_SHAKEN(0, identity)} has attestation level ${STIR_SHAKEN(0, attestation)})
			</example>
		</description>
	</function>
 ***/

static struct ast_sorcery *stir_shaken_sorcery;

/* Used for AstDB entries */
#define AST_DB_FAMILY "STIR_SHAKEN"

/* The directory name to store keys in. Appended to ast_config_DATA_DIR */
#define STIR_SHAKEN_DIR_NAME "stir_shaken"

/* The maximum length for path storage */
#define MAX_PATH_LEN 256

/* The default amount of time (in seconds) to use for certificate expiration
 * if no cache data is available
 */
#define EXPIRATION_BUFFER 15

struct ast_stir_shaken_payload {
	/*! The JWT header */
	struct ast_json *header;
	/*! The JWT payload */
	struct ast_json *payload;
	/*! Signature for the payload */
	unsigned char *signature;
	/*! The algorithm used */
	char *algorithm;
	/*! THe URL to the public certificate */
	char *public_cert_url;
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
	ast_free(payload->public_cert_url);
	ast_free(payload->signature);

	ast_free(payload);
}

unsigned char *ast_stir_shaken_payload_get_signature(const struct ast_stir_shaken_payload *payload)
{
	return payload ? payload->signature : NULL;
}

char *ast_stir_shaken_payload_get_public_cert_url(const struct ast_stir_shaken_payload *payload)
{
	return payload ? payload->public_cert_url : NULL;
}

unsigned int ast_stir_shaken_get_signature_timeout(void)
{
	return ast_stir_shaken_signature_timeout(stir_shaken_general_get());
}

struct stir_shaken_profile *ast_stir_shaken_get_profile(const char *id)
{
	if (ast_strlen_zero(id)) {
		return NULL;
	}

	return ast_stir_shaken_get_profile_by_name(id);
}

unsigned int ast_stir_shaken_profile_supports_attestation(const struct stir_shaken_profile *profile)
{
	if (!profile) {
		return 0;
	}

	return (profile->stir_shaken & STIR_SHAKEN_ATTEST);
}

unsigned int ast_stir_shaken_profile_supports_verification(const struct stir_shaken_profile *profile)
{
	if (!profile) {
		return 0;
	}

	return (profile->stir_shaken & STIR_SHAKEN_VERIFY);
}

/*!
 * \brief Convert an ast_stir_shaken_verification_result to string representation
 *
 * \param result The result to convert
 *
 * \retval empty string if not a valid enum value
 * \retval string representation of result otherwise
 */
static const char *stir_shaken_verification_result_to_string(enum ast_stir_shaken_verification_result result)
{
	switch (result) {
		case AST_STIR_SHAKEN_VERIFY_NOT_PRESENT:
			return "Verification not present";
		case AST_STIR_SHAKEN_VERIFY_SIGNATURE_FAILED:
			return "Signature failed";
		case AST_STIR_SHAKEN_VERIFY_MISMATCH:
			return "Verification mismatch";
		case AST_STIR_SHAKEN_VERIFY_PASSED:
			return "Verification passed";
		default:
			break;
	}

	return "";
}

/* The datastore struct holding verification information for the channel */
struct stir_shaken_datastore {
	/* The identitifier for the STIR/SHAKEN verification */
	char *identity;
	/* The attestation value */
	char *attestation;
	/* The actual verification result */
	enum ast_stir_shaken_verification_result verify_result;
};

/*!
 * \brief Frees a stir_shaken_datastore structure
 *
 * \param datastore The datastore to free
 */
static void stir_shaken_datastore_free(struct stir_shaken_datastore *datastore)
{
	if (!datastore) {
		return;
	}

	ast_free(datastore->identity);
	ast_free(datastore->attestation);
	ast_free(datastore);
}

/*!
 * \brief The callback to destroy a stir_shaken_datastore
 *
 * \param data The stir_shaken_datastore
 */
static void stir_shaken_datastore_destroy_cb(void *data)
{
	struct stir_shaken_datastore *datastore = data;
	stir_shaken_datastore_free(datastore);
}

/* The stir_shaken_datastore info used to add and compare stir_shaken_datastores on the channel */
static const struct ast_datastore_info stir_shaken_datastore_info = {
	.type = "STIR/SHAKEN VERIFICATION",
	.destroy = stir_shaken_datastore_destroy_cb,
};

int ast_stir_shaken_add_verification(struct ast_channel *chan, const char *identity, const char *attestation,
	enum ast_stir_shaken_verification_result result)
{
	struct stir_shaken_datastore *ss_datastore;
	struct ast_datastore *datastore;
	const char *chan_name;

	if (!chan) {
		ast_log(LOG_ERROR, "Channel is required to add STIR/SHAKEN verification\n");
		return -1;
	}

	chan_name = ast_channel_name(chan);

	if (!identity) {
		ast_log(LOG_ERROR, "No identity to add STIR/SHAKEN verification to channel "
			"%s\n", chan_name);
		return -1;
	}

	if (!attestation) {
		ast_log(LOG_ERROR, "Attestation cannot be NULL to add STIR/SHAKEN verification to "
			"channel %s\n", chan_name);
		return -1;
	}

	ss_datastore = ast_calloc(1, sizeof(*ss_datastore));
	if (!ss_datastore) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore for "
			"channel %s\n", chan_name);
		return -1;
	}

	ss_datastore->identity = ast_strdup(identity);
	if (!ss_datastore->identity) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore "
			"identity for channel %s\n", chan_name);
		stir_shaken_datastore_free(ss_datastore);
		return -1;
	}

	ss_datastore->attestation = ast_strdup(attestation);
	if (!ss_datastore->attestation) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore "
			"attestation for channel %s\n", chan_name);
		stir_shaken_datastore_free(ss_datastore);
		return -1;
	}

	ss_datastore->verify_result = result;

	datastore = ast_datastore_alloc(&stir_shaken_datastore_info, NULL);
	if (!datastore) {
		ast_log(LOG_ERROR, "Failed to allocate space for datastore for channel "
			"%s\n", chan_name);
		stir_shaken_datastore_free(ss_datastore);
		return -1;
	}

	datastore->data = ss_datastore;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	return 0;
}

/*!
 * \brief Sets the expiration for the public key based on the provided fields.
 * If Cache-Control is present, use it. Otherwise, use Expires.
 *
 * \param public_cert_url The URL to the public certificate
 * \param data The CURL callback data containing expiration data
 */
static void set_public_key_expiration(const char *public_cert_url, const struct curl_cb_data *data)
{
	char time_buf[32], secs[AST_TIME_T_LEN];
	char *value;
	struct timeval actual_expires = ast_tvnow();
	char hash[41];

	ast_sha1_hash(hash, public_cert_url);

	value = curl_cb_data_get_cache_control(data);
	if (!ast_strlen_zero(value)) {
		char *str_max_age;

		str_max_age = strstr(value, "s-maxage");
		if (!str_max_age) {
			str_max_age = strstr(value, "max-age");
		}

		if (str_max_age) {
			unsigned int max_age;
			char *equal = strchr(str_max_age, '=');
			if (equal && !ast_str_to_uint(equal + 1, &max_age)) {
				actual_expires.tv_sec += max_age;
			}
		}
	} else {
		value = curl_cb_data_get_expires(data);
		if (!ast_strlen_zero(value)) {
			struct tm expires_time;

			strptime(value, "%a, %d %b %Y %T %z", &expires_time);
			expires_time.tm_isdst = -1;
			actual_expires.tv_sec = mktime(&expires_time);
		}
	}

	if (ast_strlen_zero(value)) {
		actual_expires.tv_sec += EXPIRATION_BUFFER;
	}

	ast_time_t_to_string(actual_expires.tv_sec, secs, sizeof(secs));

	snprintf(time_buf, sizeof(time_buf), "%30s", secs);

	ast_db_put(hash, "expiration", time_buf);
}

/*!
 * \brief Check to see if the public key is expired
 *
 * \param public_cert_url The public cert URL
 *
 * \retval 1 if expired
 * \retval 0 if not expired
 */
static int public_key_is_expired(const char *public_cert_url)
{
	struct timeval current_time = ast_tvnow();
	struct timeval expires = { .tv_sec = 0, .tv_usec = 0 };
	char expiration[32];
	char hash[41];

	ast_sha1_hash(hash, public_cert_url);
	ast_db_get(hash, "expiration", expiration, sizeof(expiration));

	if (ast_strlen_zero(expiration)) {
		return 1;
	}

	if (ast_str_to_ulong(expiration, (unsigned long *)&expires.tv_sec)) {
		return 1;
	}

	return ast_tvcmp(current_time, expires) == -1 ? 0 : 1;
}

/*!
 * \brief Returns the path to the downloaded file for the provided URL
 *
 * \param public_cert_url The public cert URL
 *
 * \retval Empty string if not present in AstDB
 * \retval The file path if present in AstDB
 */
static char *get_path_to_public_key(const char *public_cert_url)
{
	char hash[41];
	char file_path[MAX_PATH_LEN];

	ast_sha1_hash(hash, public_cert_url);

	ast_db_get(hash, "path", file_path, sizeof(file_path));

	if (ast_strlen_zero(file_path)) {
		file_path[0] = '\0';
	}

	return ast_strdup(file_path);
}

/*!
 * \brief Add the public key details and file path to AstDB
 *
 * \param public_cert_url The public cert URL
 * \param filepath The path to the file
 */
static void add_public_key_to_astdb(const char *public_cert_url, const char *filepath)
{
	char hash[41];

	ast_sha1_hash(hash, public_cert_url);

	ast_db_put(AST_DB_FAMILY, public_cert_url, hash);
	ast_db_put(hash, "path", filepath);
}

/*!
 * \brief Remove the public key details and associated information from AstDB
 *
 * \param public_cert_url The public cert URL
 */
static void remove_public_key_from_astdb(const char *public_cert_url)
{
	char hash[41];
	char filepath[MAX_PATH_LEN];

	ast_sha1_hash(hash, public_cert_url);

	/* Remove this public key from storage */
	ast_db_get(hash, "path", filepath, sizeof(filepath));

	/* Remove the actual file from the system */
	remove(filepath);

	ast_db_del(AST_DB_FAMILY, public_cert_url);
	ast_db_deltree(hash, NULL);
}

/*!
 * \brief Verifies the signature using a public key
 *
 * \param msg The payload
 * \param signature The signature to verify
 * \param public_key The public key used for verification
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
static int stir_shaken_verify_signature(const char *msg, const char *signature, EVP_PKEY *public_key)
{
	EVP_MD_CTX *mdctx = NULL;
	int ret = 0;
	unsigned char *decoded_signature;
	size_t signature_length, decoded_signature_length;

	mdctx = EVP_MD_CTX_create();
	if (!mdctx) {
		ast_log(LOG_ERROR, "Failed to create Message Digest Context\n");
		return -1;
	}

	ret = EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, public_key);
	if (ret != 1) {
		ast_log(LOG_ERROR, "Failed to initialize Message Digest Context\n");
		EVP_MD_CTX_destroy(mdctx);
		return -1;
	}

	ret = EVP_DigestVerifyUpdate(mdctx, (unsigned char *)msg, strlen(msg));
	if (ret != 1) {
		ast_log(LOG_ERROR, "Failed to update Message Digest Context\n");
		EVP_MD_CTX_destroy(mdctx);
		return -1;
	}

	/* We need to decode the signature from base64 URL to bytes. Make sure we have
	 * at least enough characters for this check */
	signature_length = strlen(signature);
	decoded_signature_length = (signature_length * 3 / 4);
	decoded_signature = ast_calloc(1, decoded_signature_length);
	ast_base64url_decode(decoded_signature, signature, decoded_signature_length);

	ret = EVP_DigestVerifyFinal(mdctx, decoded_signature, decoded_signature_length);
	if (ret != 1) {
		ast_log(LOG_ERROR, "Failed final phase of signature verification\n");
		EVP_MD_CTX_destroy(mdctx);
		ast_free(decoded_signature);
		return -1;
	}

	EVP_MD_CTX_destroy(mdctx);
	ast_free(decoded_signature);

	return 0;
}

/*!
 * \brief CURL the file located at public_cert_url to the specified path
 *
 * \note filename will need to be freed by the caller
 *
 * \param public_cert_url The public cert URL
 * \param path The path to download the file to
 *
 * \retval NULL on failure
 * \retval full path filename on success
 */
static char *run_curl(const char *public_cert_url, const char *path, const struct ast_acl_list *acl)
{
	struct curl_cb_data *data;
	char *filename;

	data = curl_cb_data_create();
	if (!data) {
		ast_log(LOG_ERROR, "Failed to create CURL callback data\n");
		return NULL;
	}

	filename = curl_public_key(public_cert_url, path, data, acl);
	if (!filename) {
		ast_log(LOG_ERROR, "Could not retrieve public key for '%s'\n", public_cert_url);
		curl_cb_data_free(data);
		return NULL;
	}

	set_public_key_expiration(public_cert_url, data);
	curl_cb_data_free(data);

	return filename;
}

/*!
 * \brief Downloads the public cert from public_cert_url. If curl is non-zero, that signals
 * CURL has already been run, and we should bail here. The entry is added to AstDB as well.
 *
 * \note filename will need to be freed by the caller
 *
 * \param public_cert_url The public cert URL
 * \param path The path to download the file to
 * \param curl Flag signaling if we have run CURL or not
 *
 * \retval NULL on failure
 * \retval full path filename on success
 */
static char *curl_and_check_expiration(const char *public_cert_url, const char *path, int *curl, const struct ast_acl_list *acl)
{
	char *filename;

	if (curl) {
		ast_log(LOG_ERROR, "Already downloaded public key '%s'\n", path);
		return NULL;
	}

	filename = run_curl(public_cert_url, path, acl);
	if (!filename) {
		return NULL;
	}

	if (public_key_is_expired(public_cert_url)) {
		ast_log(LOG_ERROR, "Newly downloaded public key '%s' is expired\n", path);
		ast_free(filename);
		return NULL;
	}

	*curl = 1;
	add_public_key_to_astdb(public_cert_url, filename);

	return filename;
}

/*!
 * \brief Verifies that the string parameters are not empty for STIR/SHAKEN verification
 *
 * \retval 0 on success
 * \retval 1 on failure
 */
static int stir_shaken_verify_check_empty_strings(const char *header, const char *payload, const char *signature,
	const char *algorithm, const char *public_cert_url)
{
	if (ast_strlen_zero(header)) {
		ast_log(LOG_ERROR, "'header' is required for STIR/SHAKEN verification\n");
		return 1;
	}

	if (ast_strlen_zero(payload)) {
		ast_log(LOG_ERROR, "'payload' is required for STIR/SHAKEN verification\n");
		return 1;
	}

	if (ast_strlen_zero(signature)) {
		ast_log(LOG_ERROR, "'signature' is required for STIR/SHAKEN verification\n");
		return 1;
	}

	if (ast_strlen_zero(algorithm)) {
		ast_log(LOG_ERROR, "'algorithm' is required for STIR/SHAKEN verification\n");
		return 1;
	}

	if (ast_strlen_zero(public_cert_url)) {
		ast_log(LOG_ERROR, "'public_cert_url' is required for STIR/SHAKEN verification\n");
		return 1;
	}

	return 0;
}

/*!
 * \brief Get or set up the file path for the certificate
 *
 * \note This function will allocate memory for file_path and dir_path and populate them
 *
 * \retval 0 on success
 * \retval 1 on failure
 */
static int stir_shaken_verify_setup_file_paths(const char *public_cert_url, char **file_path, char **dir_path, int *curl,
	const struct ast_acl_list *acl)
{
	*file_path = get_path_to_public_key(public_cert_url);
	if (ast_asprintf(dir_path, "%s/keys/%s", ast_config_AST_DATA_DIR, STIR_SHAKEN_DIR_NAME) < 0) {
		return 1;
	}

	/* If we don't have an entry in AstDB, CURL from the provided URL */
	if (ast_strlen_zero(*file_path)) {
		/* Remove this entry from the database, since we will be
		 * downloading a new file anyways.
		 */
		remove_public_key_from_astdb(public_cert_url);

		/* Go ahead and free file_path, in case anything was allocated above */
		ast_free(*file_path);

		/* Download to the default path */
		*file_path = run_curl(public_cert_url, *dir_path, acl);
		if (!(*file_path)) {
			return 1;
		}

		/* Signal that we have already downloaded a new file, no reason to do it again */
		*curl = 1;

		/* We should have a successful download at this point, so
		 * add an entry to the database.
		 */
		add_public_key_to_astdb(public_cert_url, *file_path);
	}

	return 0;
}

/*!
 * \brief See if the cert is expired. If it is, remove it and try downloading again if we haven't already.
 *
 * \retval 0 on success
 * \retval 1 on failure
 */
static int stir_shaken_verify_validate_cert(const char *public_cert_url, char **file_path, char *dir_path, int *curl,
	EVP_PKEY **public_key, const struct ast_acl_list *acl)
{
	if (public_key_is_expired(public_cert_url)) {

		ast_debug(3, "Public cert '%s' is expired\n", public_cert_url);

		remove_public_key_from_astdb(public_cert_url);

		/* If this fails, then there's nothing we can do */
		ast_free(*file_path);
		*file_path = curl_and_check_expiration(public_cert_url, dir_path, curl, acl);
		if (!(*file_path)) {
			return 1;
		}
	}

	/* First attempt to read the key. If it fails, try downloading the file,
	 * unless we already did. Check for expiration again */
	*public_key = stir_shaken_read_key(*file_path, 0);
	if (!(*public_key)) {

		ast_debug(3, "Failed first read of public key file '%s'\n", *file_path);

		remove_public_key_from_astdb(public_cert_url);

		ast_free(*file_path);
		*file_path = curl_and_check_expiration(public_cert_url, dir_path, curl, acl);
		if (!(*file_path)) {
			return 1;
		}

		*public_key = stir_shaken_read_key(*file_path, 0);
		if (!(*public_key)) {
			ast_log(LOG_ERROR, "Failed to read public key from '%s'\n", *file_path);
			remove_public_key_from_astdb(public_cert_url);
			return 1;
		}
	}

	return 0;
}

struct ast_stir_shaken_payload *ast_stir_shaken_verify(const char *header, const char *payload, const char *signature,
	const char *algorithm, const char *public_cert_url)
{
	int code = 0;

	return ast_stir_shaken_verify2(header, payload, signature, algorithm, public_cert_url, &code);
}

struct ast_stir_shaken_payload *ast_stir_shaken_verify2(const char *header, const char *payload, const char *signature,
	const char *algorithm, const char *public_cert_url, int *failure_code)
{
	return ast_stir_shaken_verify_with_profile(header, payload, signature, algorithm, public_cert_url, failure_code, NULL);
}

struct ast_stir_shaken_payload *ast_stir_shaken_verify_with_profile(const char *header, const char *payload, const char *signature,
	const char *algorithm, const char *public_cert_url, int *failure_code, const struct stir_shaken_profile *profile)
{
	struct ast_stir_shaken_payload *ret_payload;
	EVP_PKEY *public_key;
	int curl = 0;
	RAII_VAR(char *, file_path, NULL, ast_free);
	RAII_VAR(char *, dir_path, NULL, ast_free);
	RAII_VAR(char *, combined_str, NULL, ast_free);
	size_t combined_size;
	const struct ast_acl_list *acl;

	if (stir_shaken_verify_check_empty_strings(header, payload, signature, algorithm, public_cert_url)) {
		return NULL;
	}

	acl = profile ? (const struct ast_acl_list *)profile->acl : NULL;

	/* Check to see if we have already downloaded this public cert. The reason we
	 * store the file path is because:
	 *
	 * 1. If, for some reason, the default directory changes, we still know where
	 * to look for the files we already have.
	 *
	 * 2. In the future, if we want to add a way to store the certs in multiple
	 * {configurable) directories, we already have the storage mechanism in place.
	 * The only thing that would be left to do is pull from the configuration.
	 */
	if (stir_shaken_verify_setup_file_paths(public_cert_url, &file_path, &dir_path, &curl, acl)) {
		return NULL;
	}

	/* Check to see if the cert we downloaded (or already had) is expired */
	if (stir_shaken_verify_validate_cert(public_cert_url, &file_path, dir_path, &curl, &public_key, acl)) {
		*failure_code = AST_STIR_SHAKEN_VERIFY_FAILED_TO_GET_CERT;
		return NULL;
	}

	/* Combine the header and payload to get the original signed message: header.payload */
	combined_size = strlen(header) + strlen(payload) + 2;
	combined_str = ast_calloc(1, combined_size);
	if (!combined_str) {
		ast_log(LOG_ERROR, "Failed to allocate space for message to verify\n");
		EVP_PKEY_free(public_key);
		*failure_code = AST_STIR_SHAKEN_VERIFY_FAILED_MEMORY_ALLOC;
		return NULL;
	}
	snprintf(combined_str, combined_size, "%s.%s", header, payload);
	if (stir_shaken_verify_signature(combined_str, signature, public_key)) {
		ast_log(LOG_ERROR, "Failed to verify signature\n");
		*failure_code = AST_STIR_SHAKEN_VERIFY_FAILED_SIGNATURE_VALIDATION;
		EVP_PKEY_free(public_key);
		return NULL;
	}

	/* We don't need the public key anymore */
	EVP_PKEY_free(public_key);

	ret_payload = ast_calloc(1, sizeof(*ret_payload));
	if (!ret_payload) {
		ast_log(LOG_ERROR, "Failed to allocate STIR/SHAKEN payload\n");
		*failure_code = AST_STIR_SHAKEN_VERIFY_FAILED_MEMORY_ALLOC;
		return NULL;
	}

	ret_payload->header = ast_json_load_string(header, NULL);
	if (!ret_payload->header) {
		ast_log(LOG_ERROR, "Failed to create JSON from header\n");
		*failure_code = AST_STIR_SHAKEN_VERIFY_FAILED_MEMORY_ALLOC;
		ast_stir_shaken_payload_free(ret_payload);
		return NULL;
	}

	ret_payload->payload = ast_json_load_string(payload, NULL);
	if (!ret_payload->payload) {
		ast_log(LOG_ERROR, "Failed to create JSON from payload\n");
		*failure_code = AST_STIR_SHAKEN_VERIFY_FAILED_MEMORY_ALLOC;
		ast_stir_shaken_payload_free(ret_payload);
		return NULL;
	}

	ret_payload->signature = (unsigned char *)ast_strdup(signature);
	ret_payload->algorithm = ast_strdup(algorithm);
	ret_payload->public_cert_url = ast_strdup(public_cert_url);

	return ret_payload;
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
		ast_log(LOG_ERROR, "Failed to allocate STIR/SHAKEN payload\n");
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

	/* Check to see if there is a value for alg */
	val = ast_json_string_get(ast_json_object_get(obj, "alg"));
	if (!ast_strlen_zero(val) && strcmp(val, STIR_SHAKEN_ENCRYPTION_ALGORITHM)) {
		/* If alg is not present that's fine; if it is and is not ES256, cleanup */
		ast_log(LOG_ERROR, "STIR/SHAKEN JWT did not have supported type for field 'alg' (was %s)\n", val);
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

	/* There are 6 bits to 1 base64 URL digit, so in order to get the size of the base64 encoded
	 * signature, we need to multiply by the number of bits in a byte and divide by 6. Since
	 * there's rounding when doing base64 conversions, add 3 bytes, just in case, and account
	 * for padding. Add another byte for the NULL-terminator.
	 */
	encoded_length = ((signature_length * 4 / 3 + 3) & ~3) + 1;
	encoded_signature = ast_calloc(1, encoded_length);
	if (!encoded_signature) {
		ast_log(LOG_ERROR, "Failed to allocate space for encoded signature\n");
		goto cleanup;
	}

	ast_base64url_encode((char *)encoded_signature, signature, signature_length, encoded_length);

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
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int stir_shaken_add_origid(struct ast_json *json)
{
	struct ast_json *value;
	char uuid_str[AST_UUID_STR_LEN];

	ast_uuid_generate_str(uuid_str, sizeof(uuid_str));
	if (strlen(uuid_str) != (AST_UUID_STR_LEN - 1)) {
		return -1;
	}

	value = ast_json_string_create(uuid_str);

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
	struct ast_stir_shaken_payload *ss_payload;
	unsigned char *signature;
	const char *public_cert_url;
	const char *caller_id_num;
	const char *header;
	const char *payload;
	struct ast_json *tmp_json;
	char *msg = NULL;
	size_t msg_len;
	struct stir_shaken_certificate *cert = NULL;

	ss_payload = stir_shaken_verify_json(json);
	if (!ss_payload) {
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

	public_cert_url = stir_shaken_certificate_get_public_cert_url(cert);
	if (stir_shaken_add_x5u(json, public_cert_url)) {
		ast_log(LOG_ERROR, "Failed to add 'x5u' (public cert URL) to payload\n");
		goto cleanup;
	}
	ss_payload->public_cert_url = ast_strdup(public_cert_url);

	if (stir_shaken_add_attest(json, stir_shaken_certificate_get_attestation(cert))) {
		ast_log(LOG_ERROR, "Failed to add 'attest' to payload\n");
		goto cleanup;
	}

	if (stir_shaken_add_origid(json)) {
		ast_log(LOG_ERROR, "Failed to add 'origid' to payload\n");
		goto cleanup;
	}

	if (stir_shaken_add_iat(json)) {
		ast_log(LOG_ERROR, "Failed to add 'iat' to payload\n");
		goto cleanup;
	}

	/* Get the header and the payload. Combine them to get the message to sign */
	tmp_json = ast_json_object_get(json, "header");
	header = ast_json_dump_string(tmp_json);
	tmp_json = ast_json_object_get(json, "payload");
	payload = ast_json_dump_string(tmp_json);
	msg_len = strlen(header) + strlen(payload) + 2;
	msg = ast_calloc(1, msg_len);
	if (!msg) {
		ast_log(LOG_ERROR, "Failed to allocate space for message to sign\n");
		goto cleanup;
	}
	snprintf(msg, msg_len, "%s.%s", header, payload);

	signature = stir_shaken_sign(msg, stir_shaken_certificate_get_private_key(cert));
	if (!signature) {
		goto cleanup;
	}

	ss_payload->signature = signature;
	ao2_cleanup(cert);
	ast_free(msg);

	return ss_payload;

cleanup:
	ao2_cleanup(cert);
	ast_stir_shaken_payload_free(ss_payload);
	ast_free(msg);
	return NULL;
}

/*!
 * \brief Retrieves STIR/SHAKEN verification information for the channel via dialplan.
 * Examples:
 *
 * STIR_SHAKEN(count)
 * STIR_SHAKEN(0, identity)
 * STIR_SHAKEN(1, attestation)
 * STIR_SHAKEN(27, verify_result)
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
static int stir_shaken_read(struct ast_channel *chan, const char *function,
	char *data, char *buf, size_t len)
{
	struct stir_shaken_datastore *ss_datastore;
	struct ast_datastore *datastore;
	char *parse;
	char *first;
	char *second;
	unsigned int target_index, current_index = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(first_param);
		AST_APP_ARG(second_param);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires at least one argument\n", function);
		return -1;
	}

	if (!chan) {
		ast_log(LOG_ERROR, "No channel for %s function\n", function);
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	first = ast_strip(args.first_param);
	if (ast_strlen_zero(first)) {
		ast_log(LOG_ERROR, "An argument must be passed to %s\n", function);
		return -1;
	}

	second = ast_strip(args.second_param);

	/* Check if we are only looking for the number of STIR/SHAKEN verification results */
	if (!strcasecmp(first, "count")) {

		size_t count = 0;

		if (!ast_strlen_zero(second)) {
			ast_log(LOG_ERROR, "%s only takes 1 paramater for 'count'\n", function);
			return -1;
		}

		ast_channel_lock(chan);
		AST_LIST_TRAVERSE(ast_channel_datastores(chan), datastore, entry) {
			if (datastore->info != &stir_shaken_datastore_info) {
				continue;
			}
			count++;
		}
		ast_channel_unlock(chan);

		snprintf(buf, len, "%zu", count);
		return 0;
	}

	/* If we aren't doing a count, then there should be two parameters. The field
	 * we are searching for will be the second parameter. The index is the first.
	 */
	if (ast_strlen_zero(second)) {
		ast_log(LOG_ERROR, "Retrieving a value using %s requires two paramaters (index, value) "
			"- only index was given\n", function);
		return -1;
	}

	if (ast_str_to_uint(first, &target_index)) {
		ast_log(LOG_ERROR, "Failed to convert index %s to integer for function %s\n",
			first, function);
		return -1;
	}

	/* We don't store by uid for the datastore, so just search for the specified index */
	ast_channel_lock(chan);
	AST_LIST_TRAVERSE(ast_channel_datastores(chan), datastore, entry) {
		if (datastore->info != &stir_shaken_datastore_info) {
			continue;
		}

		if (current_index == target_index) {
			break;
		}

		current_index++;
	}
	ast_channel_unlock(chan);
	if (current_index != target_index || !datastore) {
		ast_log(LOG_WARNING, "No STIR/SHAKEN results for index '%s'\n", first);
		return -1;
	}
	ss_datastore = datastore->data;

	if (!strcasecmp(second, "identity")) {
		ast_copy_string(buf, ss_datastore->identity, len);
	} else if (!strcasecmp(second, "attestation")) {
		ast_copy_string(buf, ss_datastore->attestation, len);
	} else if (!strcasecmp(second, "verify_result")) {
		ast_copy_string(buf, stir_shaken_verification_result_to_string(ss_datastore->verify_result), len);
	} else {
		ast_log(LOG_ERROR, "No such value '%s' for %s\n", second, function);
		return -1;
	}

	return 0;
}

static struct ast_custom_function stir_shaken_function = {
	.name = "STIR_SHAKEN",
	.read = stir_shaken_read,
};

#ifdef TEST_FRAMEWORK

static void test_stir_shaken_add_fake_astdb_entry(const char *public_cert_url, const char *file_path)
{
	struct timeval expires = ast_tvnow();
	char time_buf[32];
	char hash[41];

	ast_sha1_hash(hash, public_cert_url);
	add_public_key_to_astdb(public_cert_url, file_path);
	snprintf(time_buf, sizeof(time_buf), "%30lu", expires.tv_sec + 300);

	ast_db_put(hash, "expiration", time_buf);
}

/*!
 * \brief Create a private or public key certificate
 *
 * \param file_path The path of the file to create
 * \param private Set to 0 if public, 1 if private
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
static int test_stir_shaken_write_temp_key(char *file_path, int private)
{
	FILE *file;
	int fd;
	char *data;
	char *type = private ? "private" : "public";
	char *private_data =
		"-----BEGIN EC PRIVATE KEY-----\n"
		"MHcCAQEEIC+xv2GKNTDd81vJM8rwGAGNqgklKKxz9Qejn+pcRPC1oAoGCCqGSM49\n"
		"AwEHoUQDQgAEq12QXu8lH295ZMZ4udKy5VV8wVgE4qSOnkdofn3hEDsh6QTKTZg9\n"
		"W6PncYAVnmOFRL4cTGRbmAIShN4naZk2Yg==\n"
		"-----END EC PRIVATE KEY-----";
	char *public_data =
		"-----BEGIN CERTIFICATE-----\n"
		"MIIBzDCCAXGgAwIBAgIUXDt6EC0OixT1iRSSPV3jB/zQAlQwCgYIKoZIzj0EAwIw\n"
		"RTELMAkGA1UEBhMCQVUxEzARBgNVBAgMClNvbWUtU3RhdGUxITAfBgNVBAoMGElu\n"
		"dGVybmV0IFdpZGdpdHMgUHR5IEx0ZDAeFw0yMTA0MTMwNjM3MjRaFw0yMzA3MTcw\n"
		"NjM3MjRaMGoxCzAJBgNVBAYTAlVTMQswCQYDVQQIDAJWQTESMBAGA1UEBwwJU29t\n"
		"ZXdoZXJlMRowGAYDVQQKDBFBY21lVGVsZWNvbSwgSW5jLjENMAsGA1UECwwEVk9J\n"
		"UDEPMA0GA1UEAwwGU0hBS0VOMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEq12Q\n"
		"Xu8lH295ZMZ4udKy5VV8wVgE4qSOnkdofn3hEDsh6QTKTZg9W6PncYAVnmOFRL4c\n"
		"TGRbmAIShN4naZk2YqMaMBgwFgYIKwYBBQUHARoECjAIoAYWBDEwMDEwCgYIKoZI\n"
		"zj0EAwIDSQAwRgIhAMa9Ky38DgVaIgVm9Mgws/qN3zxjMQXfxEExAbDwyq/WAiEA\n"
		"zbC29mvtSulwbvQJ4fBdFU84cFC3Ctu1QrCeFOiZHc4=\n"
		"-----END CERTIFICATE-----";

	fd = mkstemp(file_path);
	if (fd < 0) {
		ast_log(LOG_ERROR, "Failed to create temp %s file: %s\n", type, strerror(errno));
		return -1;
	}

	file = fdopen(fd, "w");
	if (!file) {
		ast_log(LOG_ERROR, "Failed to create temp %s key file: %s\n", type, strerror(errno));
		close(fd);
		return -1;
	}

	data = private ? private_data : public_data;
	if (fputs(data, file) == EOF) {
		ast_log(LOG_ERROR, "Failed to write temp %s key file\n", type);
		fclose(file);
		return -1;
	}

	fclose(file);

	return 0;
}

AST_TEST_DEFINE(test_stir_shaken_sign)
{
	char *caller_id_number = "1234567";
	char file_path[] = "/tmp/stir_shaken_private.XXXXXX";
	RAII_VAR(char *, rm_on_exit, file_path, unlink);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_free);
	RAII_VAR(struct ast_stir_shaken_payload *, payload, NULL, ast_stir_shaken_payload_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stir_shaken_sign";
		info->category = "/res/res_stir_shaken/";
		info->summary = "STIR/SHAKEN sign unit test";
		info->description =
			"Tests signing a JWT with a private key.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* We only need a private key to sign */
	test_stir_shaken_write_temp_key(file_path, 1);
	test_stir_shaken_create_cert(caller_id_number, file_path);

	/* Test missing header section */
	json = ast_json_pack("{s: {s: {s: s}}}", "payload", "orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (missing 'header')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test missing payload section */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", STIR_SHAKEN_PPT, "typ", STIR_SHAKEN_TYPE,
		"x5u", "http://testing123");
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (missing 'payload')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test missing alg section */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "ppt",
		STIR_SHAKEN_PPT, "typ", STIR_SHAKEN_TYPE, "x5u", "http://testing123", "payload",
		"orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (missing 'alg')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test invalid alg value */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "alg",
		"invalid algorithm", "ppt", STIR_SHAKEN_PPT, "typ", STIR_SHAKEN_TYPE,
		"x5u", "http://testing123", "payload", "orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (wrong 'alg')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test missing ppt section */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "typ", STIR_SHAKEN_TYPE, "x5u", "http://testing123",
		"payload", "orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (missing 'ppt')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test invalid ppt value */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", "invalid ppt", "typ", STIR_SHAKEN_TYPE,
		"x5u", "http://testing123", "payload", "orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (wrong 'ppt')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test missing typ section */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", STIR_SHAKEN_PPT, "x5u", "http://testing123",
		"payload", "orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (missing 'typ')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test invalid typ value */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", STIR_SHAKEN_PPT, "typ", "invalid typ",
		"x5u", "http://testing123", "payload", "orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (wrong 'typ')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test missing orig section */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}, s: {s: s}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", STIR_SHAKEN_PPT, "typ", STIR_SHAKEN_TYPE,
		"x5u", "http://testing123", "payload", "filler", "filler");
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (missing 'orig')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test missing tn section */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}, s: {s: s}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", STIR_SHAKEN_PPT, "typ", STIR_SHAKEN_TYPE,
		"x5u", "http://testing123", "payload", "orig", "filler");
	payload = ast_stir_shaken_sign(json);
	if (payload) {
		ast_test_status_update(test, "Signed an invalid JWT (missing 'tn')\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test valid JWT */
	ast_json_free(json);
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", STIR_SHAKEN_PPT, "typ", STIR_SHAKEN_TYPE,
		"x5u", "http://testing123", "payload", "orig", "tn", caller_id_number);
	payload = ast_stir_shaken_sign(json);
	if (!payload) {
		ast_test_status_update(test, "Failed to sign a valid JWT\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	test_stir_shaken_cleanup_cert(caller_id_number);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_stir_shaken_verify)
{
	char *caller_id_number = "1234567";
	char *public_cert_url = "http://testing123";
	char *header;
	char *payload;
	struct ast_json *tmp_json;
	char public_path[] = "/tmp/stir_shaken_public.XXXXXX";
	char private_path[] = "/tmp/stir_shaken_public.XXXXXX";
	RAII_VAR(char *, rm_on_exit_public, public_path, unlink);
	RAII_VAR(char *, rm_on_exit_private, private_path, unlink);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_free);
	RAII_VAR(struct ast_stir_shaken_payload *, signed_payload, NULL, ast_stir_shaken_payload_free);
	RAII_VAR(struct ast_stir_shaken_payload *, returned_payload, NULL, ast_stir_shaken_payload_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stir_shaken_verify";
		info->category = "/res/res_stir_shaken/";
		info->summary = "STIR/SHAKEN verify unit test";
		info->description =
			"Tests verifying a signature with a public key";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* We need the private key to sign, but we also need the corresponding
	 * public key to verify */
	test_stir_shaken_write_temp_key(public_path, 0);
	test_stir_shaken_write_temp_key(private_path, 1);
	test_stir_shaken_create_cert(caller_id_number, private_path);

	/* Get the signature */
	json = ast_json_pack("{s: {s: s, s: s, s: s, s: s}, s: {s: {s: s}}}", "header", "alg",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "ppt", STIR_SHAKEN_PPT, "typ", STIR_SHAKEN_TYPE,
		"x5u", public_cert_url, "payload", "orig", "tn", caller_id_number);
	signed_payload = ast_stir_shaken_sign(json);
	if (!signed_payload) {
		ast_test_status_update(test, "Failed to sign a valid JWT\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Get the header and payload for ast_stir_shaken_verify */
	tmp_json = ast_json_object_get(json, "header");
	header = ast_json_dump_string(tmp_json);
	tmp_json = ast_json_object_get(json, "payload");
	payload = ast_json_dump_string(tmp_json);

	/* Test empty header parameter */
	returned_payload = ast_stir_shaken_verify("", payload, (const char *)signed_payload->signature,
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, public_cert_url);
	if (returned_payload) {
		ast_test_status_update(test, "Verified a signature with missing 'header'\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test empty payload parameter */
	returned_payload = ast_stir_shaken_verify(header, "", (const char *)signed_payload->signature,
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, public_cert_url);
	if (returned_payload) {
		ast_test_status_update(test, "Verified a signature with missing 'payload'\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test empty signature parameter */
	returned_payload = ast_stir_shaken_verify(header, payload, "",
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, public_cert_url);
	if (returned_payload) {
		ast_test_status_update(test, "Verified a signature with missing 'signature'\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test empty algorithm parameter */
	returned_payload = ast_stir_shaken_verify(header, payload, (const char *)signed_payload->signature,
		"", public_cert_url);
	if (returned_payload) {
		ast_test_status_update(test, "Verified a signature with missing 'algorithm'\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Test empty public key URL */
	returned_payload = ast_stir_shaken_verify(header, payload, (const char *)signed_payload->signature,
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, "");
	if (returned_payload) {
		ast_test_status_update(test, "Verified a signature with missing 'public key URL'\n");
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	/* Trick the function into thinking we've already downloaded the key */
	test_stir_shaken_add_fake_astdb_entry(public_cert_url, public_path);

	/* Verify a valid signature */
	returned_payload = ast_stir_shaken_verify(header, payload, (const char *)signed_payload->signature,
		STIR_SHAKEN_ENCRYPTION_ALGORITHM, public_cert_url);
	if (!returned_payload) {
		ast_test_status_update(test, "Failed to verify a valid signature\n");
		remove_public_key_from_astdb(public_cert_url);
		test_stir_shaken_cleanup_cert(caller_id_number);
		return AST_TEST_FAIL;
	}

	remove_public_key_from_astdb(public_cert_url);

	test_stir_shaken_cleanup_cert(caller_id_number);

	return AST_TEST_PASS;
}

#endif /* TEST_FRAMEWORK */

static int reload_module(void)
{
	if (stir_shaken_sorcery) {
		ast_sorcery_reload(stir_shaken_sorcery);
	}

	return 0;
}

static int unload_module(void)
{
	int res = 0;

	stir_shaken_profile_unload();
	stir_shaken_certificate_unload();
	stir_shaken_store_unload();
	stir_shaken_general_unload();

	ast_sorcery_unref(stir_shaken_sorcery);
	stir_shaken_sorcery = NULL;

	res |= ast_custom_function_unregister(&stir_shaken_function);

	AST_TEST_UNREGISTER(test_stir_shaken_sign);
	AST_TEST_UNREGISTER(test_stir_shaken_verify);

	return res;
}

static int load_module(void)
{
	int res = 0;

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

	if (stir_shaken_profile_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_load(ast_stir_shaken_sorcery());

	res |= ast_custom_function_register(&stir_shaken_function);

	AST_TEST_REGISTER(test_stir_shaken_sign);
	AST_TEST_REGISTER(test_stir_shaken_verify);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "STIR/SHAKEN Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 1,
	.requires = "res_curl",
);
