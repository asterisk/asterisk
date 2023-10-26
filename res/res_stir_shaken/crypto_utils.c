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

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/x509v3.h>

#include "crypto_utils.h"

#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/stringfields.h"
#include "asterisk/utils.h"
#include "asterisk/vector.h"

static AST_VECTOR_RW(ast_X509_Extensions, struct ast_X509_Extension *) x509_extensions;

static void ast_X509_Extension_free(struct ast_X509_Extension *ext)
{
	ast_string_field_free_memory(ext);
	ast_free((struct ast_X509_Extension *)ext);
}

static void ast_X509_Extensions_free(void)
{
	AST_VECTOR_RW_RDLOCK(&x509_extensions);
	AST_VECTOR_RESET(&x509_extensions, ast_X509_Extension_free);
	AST_VECTOR_RW_UNLOCK(&x509_extensions);
	AST_VECTOR_RW_FREE(&x509_extensions);
}

void __attribute__((format(printf, 5, 6)))
ast_log_openssl(int level, char *file, int line, const char *function,
	const char *fmt, ...)
{
	FILE *fp;
	char *buffer;
	size_t length;
	va_list ap;
	char *tmp_fmt;

	fp = open_memstream(&buffer, &length);
	if (!fp) {
		return;
	}

	va_start(ap, fmt);
	if (!ast_strlen_zero(fmt)) {
		size_t fmt_len = strlen(fmt);
		if (fmt[fmt_len - 1] == '\n') {
			tmp_fmt = ast_strdupa(fmt);
			tmp_fmt[fmt_len - 1] = '\0';
			fmt = tmp_fmt;
		}
	}
	vfprintf(fp, fmt, ap);
	fputs(": ", fp);
	ERR_print_errors_fp(fp);
	fclose(fp);

	if (length) {
		ast_log(level, file, line, function, "%s\n", buffer);
	}

	ast_std_free(buffer);
}

static int ext_sn_comparator(struct ast_X509_Extension *ext, const char *short_name)
{
	return strcasecmp(ext->short_name, short_name) == 0;
}

static int ext_nid_comparator(struct ast_X509_Extension *ext, int nid)
{
	return ext->nid == nid;
}

struct ast_X509_Extension *ast_crypto_get_registered_extension(
	int nid, const char *short_name)
{
	struct ast_X509_Extension *ext = NULL;

	AST_VECTOR_RW_RDLOCK(&x509_extensions);
	if (nid) {
		ext = AST_VECTOR_GET_CMP(&x509_extensions, nid, ext_nid_comparator);
	} else if (!ast_strlen_zero(short_name)) {
		ext = AST_VECTOR_GET_CMP(&x509_extensions, short_name, ext_sn_comparator);
	}
	AST_VECTOR_RW_UNLOCK(&x509_extensions);

	return ext;
}

int ast_crypto_is_extension_registered(int nid, const char *short_name)
{
	struct ast_X509_Extension *ext = ast_crypto_get_registered_extension(
		nid, short_name);

	return (ext != NULL);
}

int ast_crypto_register_x509_extension(const char *oid, const char *short_name,
	const char *long_name)
{
	struct ast_X509_Extension *ext;
	int rc = 0;

	if (ast_strlen_zero(oid) || ast_strlen_zero(short_name) ||
		ast_strlen_zero(long_name)) {
		ast_log(LOG_ERROR, "One or more of oid, short_name or long_name are NULL or empty\n");
		return -1;
	}

	ext = ast_crypto_get_registered_extension(0, short_name);
	if (ext) {
		ast_log(LOG_ERROR, "An extension with the namne '%s' is already registered\n", short_name);
		return -1;
	}

	ext = ast_malloc(sizeof(*ext));
	if (!ext) {
		ast_log(LOG_ERROR, "Unable to allocate memory for extension '%s'\n", short_name);
		return -1;
	}
	if (ast_string_field_init(ext, 256)) {
		ast_log(LOG_ERROR, "Unable to initialize stringfields for extension '%s'\n", short_name);
		ast_X509_Extension_free(ext);
		return -1;
	}

	ast_string_field_set(ext, oid, oid);
	if (!ext->oid) {
		ast_log(LOG_ERROR, "Unable to set oid for extension '%s' OID '%s'\n", short_name, oid);
		ast_X509_Extension_free(ext);
		return -1;
	}

	ast_string_field_set(ext, short_name, short_name);
	if (!ext->short_name) {
		ast_log(LOG_ERROR, "Unable to set short name for extension '%s' short_name '%s'\n", short_name, short_name);
		ast_X509_Extension_free(ext);
		return -1;
	}

	ast_string_field_set(ext, long_name, long_name);
	if (!ext->long_name) {
		ast_log(LOG_ERROR, "Unable to set long name for extension '%s' short_name '%s'\n", short_name, short_name);
		ast_X509_Extension_free(ext);
		return -1;
	}

	ext->nid = OBJ_create(oid, short_name, long_name);
	if (ext->nid == NID_undef) {
		ast_X509_Extension_free(ext);
		ast_log_openssl(LOG_ERROR, "Couldn't register %s X509 extension\n", short_name);
		return -1;
	}
	ast_log(LOG_NOTICE, "Registering %s NID %d\n", ext->short_name, ext->nid);

	AST_VECTOR_RW_WRLOCK(&x509_extensions);
	rc = AST_VECTOR_APPEND(&x509_extensions, ext);
	AST_VECTOR_RW_UNLOCK(&x509_extensions);
	if (rc != 0) {
		ast_X509_Extension_free(ext);
		ast_log(LOG_ERROR, "Unable to register %s X509 extension\n", short_name);
		return -1;
	}

	return ext->nid;
}

ASN1_OCTET_STRING *ast_crypto_get_cert_extension_data(X509 *cert,
	int nid, const char *short_name)
{
	int ex_idx;
	X509_EXTENSION *ex;

	if (nid <= 0) {
		struct ast_X509_Extension *ext;

		AST_VECTOR_RW_RDLOCK(&x509_extensions);
		ext = AST_VECTOR_GET_CMP(&x509_extensions, short_name, ext_sn_comparator);
		AST_VECTOR_RW_UNLOCK(&x509_extensions);

		if (!ext) {
			ast_log(LOG_ERROR, "Unable to find registered extension '%s'\n", short_name);
			return NULL;
		}

		nid = ext->nid;
	}

	ex_idx = X509_get_ext_by_NID(cert, nid, -1);
	if (ex_idx < 0) {
		ast_log(LOG_ERROR, "Extension index not found in certificate\n");
		return NULL;
	}
	ex = X509_get_ext(cert, ex_idx);
	if (!ex) {
		ast_log(LOG_ERROR, "Extension not found in certificate\n");
		return NULL;
	}

	return X509_EXTENSION_get_data(ex);
}

EVP_PKEY *ast_crypto_load_privkey_from_file(const char *filename)
{
	EVP_PKEY *key = NULL;
	FILE *fp;

	if (ast_strlen_zero(filename)) {
		ast_log(LOG_ERROR, "filename was null or empty\n");
		return NULL;
	}

	fp = fopen(filename, "r");
	if (!fp) {
		ast_log(LOG_ERROR, "Failed to open %s: %s\n", filename, strerror(errno));
		return NULL;
	}

	key = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
	fclose(fp);
	if (!key) {
		ast_log_openssl(LOG_ERROR, "Failed to load private key from %s\n", filename);
	}
	return key;
}

X509 *ast_crypto_load_cert_from_file(const char *filename)
{
	FILE *fp;
	X509 *cert = NULL;

	if (ast_strlen_zero(filename)) {
		ast_log(LOG_ERROR, "filename was null or empty\n");
		return NULL;
	}

	fp = fopen(filename, "r");
	if (!fp) {
		ast_log(LOG_ERROR, "Failed to open %s: %s\n", filename, strerror(errno));
		return NULL;
	}

	cert = PEM_read_X509(fp, &cert, NULL, NULL);
	fclose(fp);
	if (!cert) {
		ast_log_openssl(LOG_ERROR, "Failed to create cert from %s\n", filename);
	}
	return cert;
}

X509 *ast_crypto_load_cert_from_memory(const char *buffer, size_t size)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);
	X509 *cert = NULL;

	if (ast_strlen_zero(buffer) || size <= 0) {
		ast_log(LOG_ERROR, "buffer was null or empty\n");
		return NULL;
	}

	bio = BIO_new_mem_buf(buffer, size);
	if (!bio) {
		ast_log_openssl(LOG_ERROR, "Unable to create memory BIO\n");
		return NULL;
	}

	cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	if (!cert) {
		ast_log_openssl(LOG_ERROR, "Failed to create cert from BIO\n");
	}
	return cert;
}

static EVP_PKEY *load_private_key_from_memory(const char *buffer, size_t size)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);
	EVP_PKEY *key = NULL;

	if (ast_strlen_zero(buffer) || size <= 0) {
		ast_log(LOG_ERROR, "buffer was null or empty\n");
		return NULL;
	}

	bio = BIO_new_mem_buf(buffer, size);
	if (!bio) {
		ast_log_openssl(LOG_ERROR, "Unable to create memory BIO\n");
		return NULL;
	}

	key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);

	return key;
}

EVP_PKEY *ast_crypto_load_private_key_from_memory(const char *buffer, size_t size)
{
	EVP_PKEY *key = load_private_key_from_memory(buffer, size);
	if (!key) {
		ast_log_openssl(LOG_ERROR, "Unable to load private key from memory\n");
	}
	return key;
}

int ast_crypto_has_private_key_from_memory(const char *buffer, size_t size)
{
	RAII_VAR(EVP_PKEY *, key, load_private_key_from_memory(buffer, size), EVP_PKEY_free);

	return key ? 1 : 0;
}

static int dump_mem_bio(BIO *bio, unsigned char **buffer)
{
	char *temp_ptr;
	int raw_key_len;

	raw_key_len = BIO_get_mem_data(bio, &temp_ptr);
	if (raw_key_len <= 0) {
		ast_log_openssl(LOG_ERROR, "Unable to extract raw public key\n");
		return -1;
	}
	*buffer = ast_malloc(raw_key_len);
	if (!*buffer) {
		ast_log(LOG_ERROR, "Unable to allocate memory for raw public key\n");
		return -1;
	}
	memcpy(*buffer, temp_ptr, raw_key_len);

	return raw_key_len;
}

int ast_crypto_extract_raw_pubkey(EVP_PKEY *key, unsigned char **buffer)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);

	bio = BIO_new(BIO_s_mem());

	if (!bio || (PEM_write_bio_PUBKEY(bio, key) <= 0)) {
		ast_log_openssl(LOG_ERROR, "Unable to write pubkey to BIO\n");
		return -1;
	}

	return dump_mem_bio(bio, buffer);
}

int ast_crypto_get_raw_pubkey_from_cert(X509 *cert,
	unsigned char **buffer)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);
	EVP_PKEY *public_key;

	public_key = X509_get0_pubkey(cert);
	if (!public_key) {
		ast_log_openssl(LOG_ERROR, "Unable to retrieve pubkey from cert\n");
		return -1;
	}

	return ast_crypto_extract_raw_pubkey(public_key, buffer);
}

int ast_crypto_extract_raw_privkey(EVP_PKEY *key, unsigned char **buffer)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);

	bio = BIO_new(BIO_s_mem());

	if (!bio || (PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL) <= 0)) {
		ast_log_openssl(LOG_ERROR, "Unable to write privkey to BIO\n");
		return -1;
	}

	return dump_mem_bio(bio, buffer);
}

X509_STORE *ast_crypto_create_cert_store(void)
{
	X509_STORE *store = X509_STORE_new();

	if (!store) {
		ast_log_openssl(LOG_ERROR, "Failed to create X509_STORE\n");
		return NULL;
	}

	return store;
}

int ast_crypto_load_cert_store(X509_STORE *store, const char *file,
	const char *path)
{
	if (ast_strlen_zero(file) && ast_strlen_zero(path)) {
		ast_log(LOG_ERROR, "Both file and path can't be NULL");
		return -1;
	}

	if (!store) {
		ast_log(LOG_ERROR, "store is NULL");
		return -1;
	}

	/*
	 * If the file or path are empty strings, we need to pass NULL
	 * so openssl ignores it otherwise it'll try to open a file or
	 * path named ''.
	 */
	if (!X509_STORE_load_locations(store, S_OR(file, NULL), S_OR(path, NULL))) {
		ast_log_openssl(LOG_ERROR, "Failed to load store from file '%s' or path '%s'\n",
			S_OR(file, "N/A"), S_OR(path, "N/A"));
		return -1;
	}

	return 0;
}

int ast_crypto_is_cert_time_valid(X509*cert, time_t reftime)
{
	ASN1_STRING *notbefore;
	ASN1_STRING *notafter;

	if (!reftime) {
		reftime = time(NULL);
	}
	notbefore = X509_get_notBefore(cert);
	notafter = X509_get_notAfter(cert);
	if (!notbefore || !notafter) {
		ast_log(LOG_ERROR, "Either notbefore or notafter were not present in the cert\n");
		return 0;
	}

	return (X509_cmp_time(notbefore, &reftime) < 0 &&
		X509_cmp_time(notafter, &reftime) > 0);
}

int ast_crypto_is_cert_trusted(X509_STORE *store, X509 *cert)
{
	X509_STORE_CTX *verify_ctx = NULL;
	int rc = 0;

	if (!(verify_ctx = X509_STORE_CTX_new())) {
		ast_log_openssl(LOG_ERROR, "Unable to create verify_ctx\n");
		return 0;
	}

	if (X509_STORE_CTX_init(verify_ctx, store, cert, NULL) != 1) {
		X509_STORE_CTX_cleanup(verify_ctx);
		X509_STORE_CTX_free(verify_ctx);
		ast_log_openssl(LOG_ERROR, "Unable to initialize verify_ctx\n");
		return 0;
	}

	rc = X509_verify_cert(verify_ctx);
	X509_STORE_CTX_cleanup(verify_ctx);
	X509_STORE_CTX_free(verify_ctx);

	return rc;
}

#define SECS_PER_DAY 86400
time_t ast_crypto_asn_time_as_time_t(ASN1_TIME *at)
{
	int pday;
	int psec;
	time_t rt = time(NULL);

	if (!ASN1_TIME_diff(&pday, &psec, NULL, at)) {
		ast_log_openssl(LOG_ERROR, "Unable to calculate time diff\n");
		return 0;
	}

	rt += ((pday * SECS_PER_DAY) + psec);

	return rt;
}
#undef SECS_PER_DAY

int ss_crypto_load(void)
{
	int res = 0;

	res = AST_VECTOR_RW_INIT(&x509_extensions, 5);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

int ss_crypto_unload(void)
{
	ast_X509_Extensions_free();

	return 0;
}

