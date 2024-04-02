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
#include <openssl/obj_mac.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

#include "crypto_utils.h"

#include "asterisk.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/stringfields.h"
#include "asterisk/utils.h"
#include "asterisk/vector.h"
#include "asterisk/cli.h"

void __attribute__((format(printf, 5, 6)))
crypto_log_openssl(int level, char *file, int line, const char *function,
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

int crypto_register_x509_extension(const char *oid, const char *short_name,
	const char *long_name)
{
	int nid = 0;

	if (ast_strlen_zero(oid) || ast_strlen_zero(short_name) ||
		ast_strlen_zero(long_name)) {
		ast_log(LOG_ERROR, "One or more of oid, short_name or long_name are NULL or empty\n");
		return -1;
	}

	nid = OBJ_sn2nid(short_name);
	if (nid != NID_undef) {
		ast_log(LOG_NOTICE, "NID %d, object %s already registered\n", nid, short_name);
		return nid;
	}

	nid = OBJ_create(oid, short_name, long_name);
	if (nid == NID_undef) {
		crypto_log_openssl(LOG_ERROR, "Couldn't register %s X509 extension\n", short_name);
		return -1;
	}
	ast_log(LOG_NOTICE, "Registered object %s as NID %d\n", short_name, nid);

	return nid;
}

ASN1_OCTET_STRING *crypto_get_cert_extension_data(X509 *cert,
	int nid, const char *short_name)
{
	int ex_idx;
	X509_EXTENSION *ex;

	if (nid <= 0) {
		nid = OBJ_sn2nid(short_name);
		if (nid == NID_undef) {
			ast_log(LOG_ERROR, "Extension object for %s not found\n", short_name);
			return NULL;
		}
	} else {
		const char *tmp = OBJ_nid2sn(nid);
		if (!tmp) {
			ast_log(LOG_ERROR, "Extension object for NID %d not found\n", nid);
			return NULL;
		}
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

EVP_PKEY *crypto_load_privkey_from_file(const char *filename)
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
		crypto_log_openssl(LOG_ERROR, "Failed to load private key from %s\n", filename);
	}
	return key;
}

X509 *crypto_load_cert_from_file(const char *filename)
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
		crypto_log_openssl(LOG_ERROR, "Failed to create cert from %s\n", filename);
	}
	return cert;
}

X509 *crypto_load_cert_from_memory(const char *buffer, size_t size)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);
	X509 *cert = NULL;

	if (ast_strlen_zero(buffer) || size <= 0) {
		ast_log(LOG_ERROR, "buffer was null or empty\n");
		return NULL;
	}

	bio = BIO_new_mem_buf(buffer, size);
	if (!bio) {
		crypto_log_openssl(LOG_ERROR, "Unable to create memory BIO\n");
		return NULL;
	}

	cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	if (!cert) {
		crypto_log_openssl(LOG_ERROR, "Failed to create cert from BIO\n");
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
		crypto_log_openssl(LOG_ERROR, "Unable to create memory BIO\n");
		return NULL;
	}

	key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);

	return key;
}

EVP_PKEY *crypto_load_private_key_from_memory(const char *buffer, size_t size)
{
	EVP_PKEY *key = load_private_key_from_memory(buffer, size);
	if (!key) {
		crypto_log_openssl(LOG_ERROR, "Unable to load private key from memory\n");
	}
	return key;
}

int crypto_has_private_key_from_memory(const char *buffer, size_t size)
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
		crypto_log_openssl(LOG_ERROR, "Unable to extract raw public key\n");
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

int crypto_extract_raw_pubkey(EVP_PKEY *key, unsigned char **buffer)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);

	bio = BIO_new(BIO_s_mem());

	if (!bio || (PEM_write_bio_PUBKEY(bio, key) <= 0)) {
		crypto_log_openssl(LOG_ERROR, "Unable to write pubkey to BIO\n");
		return -1;
	}

	return dump_mem_bio(bio, buffer);
}

int crypto_get_raw_pubkey_from_cert(X509 *cert,
	unsigned char **buffer)
{
	RAII_VAR(EVP_PKEY *, public_key, X509_get_pubkey(cert), EVP_PKEY_free);

	if (!public_key) {
		crypto_log_openssl(LOG_ERROR, "Unable to retrieve pubkey from cert\n");
		return -1;
	}

	return crypto_extract_raw_pubkey(public_key, buffer);
}

int crypto_extract_raw_privkey(EVP_PKEY *key, unsigned char **buffer)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);

	bio = BIO_new(BIO_s_mem());

	if (!bio || (PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL) <= 0)) {
		crypto_log_openssl(LOG_ERROR, "Unable to write privkey to BIO\n");
		return -1;
	}

	return dump_mem_bio(bio, buffer);
}

static void crypto_cert_store_destructor(void *obj)
{
	struct crypto_cert_store *store = obj;

	if (store->store) {
		X509_STORE_free(store->store);
	}
}

struct crypto_cert_store *crypto_create_cert_store(void)
{
	struct crypto_cert_store *store = ao2_alloc(sizeof(*store), crypto_cert_store_destructor);
	if (!store) {
		ast_log(LOG_ERROR, "Failed to create crypto_cert_store\n");
		return NULL;
	}
	store->store = X509_STORE_new();

	if (!store->store) {
		crypto_log_openssl(LOG_ERROR, "Failed to create X509_STORE\n");
		ao2_ref(store, -1);
		return NULL;
	}

	return store;
}

int crypto_load_cert_store(struct crypto_cert_store *store, const char *file,
	const char *path)
{
	if (ast_strlen_zero(file) && ast_strlen_zero(path)) {
		ast_log(LOG_ERROR, "Both file and path can't be NULL");
		return -1;
	}

	if (!store || !store->store) {
		ast_log(LOG_ERROR, "store is NULL");
		return -1;
	}

	/*
	 * If the file or path are empty strings, we need to pass NULL
	 * so openssl ignores it otherwise it'll try to open a file or
	 * path named ''.
	 */
	if (!X509_STORE_load_locations(store->store, S_OR(file, NULL), S_OR(path, NULL))) {
		crypto_log_openssl(LOG_ERROR, "Failed to load store from file '%s' or path '%s'\n",
			S_OR(file, "N/A"), S_OR(path, "N/A"));
		return -1;
	}

	return 0;
}

int crypto_show_cli_store(struct crypto_cert_store *store, int fd)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
	STACK_OF(X509_OBJECT) *certs = NULL;
	int count = 0;
	int i = 0;
	char subj[1024];

	certs = X509_STORE_get0_objects(store->store);
	count = sk_X509_OBJECT_num(certs);
	for (i = 0; i < count ; i++) {
		X509_OBJECT *o = sk_X509_OBJECT_value(certs, i);
		X509 *c = X509_OBJECT_get0_X509(o);
		X509_NAME_oneline(X509_get_subject_name(c), subj, 1024);
		ast_cli(fd, "%s\n", subj);
	}
	return count;
#else
	ast_cli(fd, "This command is not supported until OpenSSL 1.1.0\n");
	return 0;
#endif
}

int crypto_is_cert_time_valid(X509*cert, time_t reftime)
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

int crypto_is_cert_trusted(struct crypto_cert_store *store, X509 *cert, const char **err_msg)
{
	X509_STORE_CTX *verify_ctx = NULL;
	int rc = 0;

	if (!(verify_ctx = X509_STORE_CTX_new())) {
		crypto_log_openssl(LOG_ERROR, "Unable to create verify_ctx\n");
		return 0;
	}

	if (X509_STORE_CTX_init(verify_ctx, store->store, cert, NULL) != 1) {
		X509_STORE_CTX_cleanup(verify_ctx);
		X509_STORE_CTX_free(verify_ctx);
		crypto_log_openssl(LOG_ERROR, "Unable to initialize verify_ctx\n");
		return 0;
	}

	rc = X509_verify_cert(verify_ctx);
	if (rc != 1 && err_msg != NULL) {
		int err = X509_STORE_CTX_get_error(verify_ctx);
		*err_msg = X509_verify_cert_error_string(err);
	}
	X509_STORE_CTX_cleanup(verify_ctx);
	X509_STORE_CTX_free(verify_ctx);

	return rc;
}

#define SECS_PER_DAY 86400
time_t crypto_asn_time_as_time_t(ASN1_TIME *at)
{
	int pday;
	int psec;
	time_t rt = time(NULL);

	if (!ASN1_TIME_diff(&pday, &psec, NULL, at)) {
		crypto_log_openssl(LOG_ERROR, "Unable to calculate time diff\n");
		return 0;
	}

	rt += ((pday * SECS_PER_DAY) + psec);

	return rt;
}
#undef SECS_PER_DAY

char *crypto_get_cert_subject(X509 *cert, const char *short_name)
{
	size_t len = 0;
	RAII_VAR(char *, buffer, NULL, ast_std_free);
	char *search_buff = NULL;
	char *search = NULL;
	size_t search_len = 0;
	char *rtn = NULL;
	char *line = NULL;
	/*
	 * If short_name was supplied, we want a multiline subject
	 * with each component on a separate line.  This makes it easier
	 * to iterate over the components to find the one we want.
	 * Otherwise, we just want the whole subject on one line.
	 */
	unsigned long flags =
		short_name ? XN_FLAG_FN_SN | XN_FLAG_SEP_MULTILINE : XN_FLAG_ONELINE;
	FILE *fp = open_memstream(&buffer, &len);
	BIO *bio = fp ? BIO_new_fp(fp, BIO_CLOSE) : NULL;
	X509_NAME *subject = X509_get_subject_name(cert);
	int rc = 0;

	if (!fp || !bio || !subject) {
		return NULL;
	}

	rc = X509_NAME_print_ex(bio, subject, 0, flags);
	BIO_free(bio);
	if (rc < 0) {
		return NULL;
	}

	if (!short_name) {
		rtn = ast_malloc(len + 1);
		if (rtn) {
			strcpy(rtn, buffer); /* Safe */
		}
		return rtn;
	}

	search_len = strlen(short_name) + 1;
	rc = ast_asprintf(&search, "%s=", short_name);
	if (rc != search_len) {
		return NULL;
	}

	search_buff = buffer;
	while((line = ast_read_line_from_buffer(&search_buff))) {
		if (ast_begins_with(line, search)) {
			rtn = ast_malloc(strlen(line) - search_len + 1);
			if (rtn) {
				strcpy(rtn, line + search_len); /* Safe */
			}
			break;
		}
	}

	ast_std_free(search);
	return rtn;
}

int crypto_load(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

int crypto_unload(void)
{
	return 0;
}

