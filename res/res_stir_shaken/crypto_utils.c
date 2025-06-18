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

#include <sys/stat.h>

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
#include "asterisk/cli.h"
#include "asterisk/file.h"
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

X509_CRL *crypto_load_crl_from_file(const char *filename)
{
	FILE *fp;
	X509_CRL *crl = NULL;

	if (ast_strlen_zero(filename)) {
		ast_log(LOG_ERROR, "filename was null or empty\n");
		return NULL;
	}

	fp = fopen(filename, "r");
	if (!fp) {
		ast_log(LOG_ERROR, "Failed to open %s: %s\n", filename, strerror(errno));
		return NULL;
	}

	crl = PEM_read_X509_CRL(fp, &crl, NULL, NULL);
	fclose(fp);
	if (!crl) {
		crypto_log_openssl(LOG_ERROR, "Failed to create CRL from %s\n", filename);
	}
	return crl;
}

#define debug_cert_chain(level, cert_chain) \
({ \
	int i; \
	char subj[1024]; \
	if (cert_chain && sk_X509_num(cert_chain) > 0) { \
		for (i = 0; i < sk_X509_num(cert_chain); i++) { \
			X509 *cert = sk_X509_value(cert_chain, i); \
			subj[0] = '\0'; \
			X509_NAME_oneline(X509_get_subject_name(cert), subj, 1024); \
			ast_debug(level, "Chain cert %d: '%s'\n", i, subj); \
		} \
	} \
})

X509 *crypto_load_cert_chain_from_file(const char *filename, STACK_OF(X509) **cert_chain)
{
	FILE *fp;
	X509 *end_cert = NULL;

	if (ast_strlen_zero(filename)) {
		ast_log(LOG_ERROR, "filename was null or empty\n");
		return NULL;
	}

	fp = fopen(filename, "r");
	if (!fp) {
		ast_log(LOG_ERROR, "Failed to open %s: %s\n", filename, strerror(errno));
		return NULL;
	}

	end_cert = PEM_read_X509(fp, &end_cert, NULL, NULL);
	if (!end_cert) {
		crypto_log_openssl(LOG_ERROR, "Failed to create end_cert from %s\n", filename);
		fclose(fp);
		return NULL;
	}

	/*
	 * If the caller provided a stack, we will read the chain certs
	 * (if any) into it.
	 */
	if (cert_chain) {
		X509 *chain_cert = NULL;

		*cert_chain = sk_X509_new_null();
		while ((chain_cert = PEM_read_X509(fp, &chain_cert, NULL, NULL)) != NULL) {
			if (sk_X509_push(*cert_chain, chain_cert) <= 0) {
				crypto_log_openssl(LOG_ERROR, "Failed to add chain cert from %s to list\n",
					filename);
				fclose(fp);
				X509_free(end_cert);
				sk_X509_pop_free(*cert_chain, X509_free);
				return NULL;
			}
			/* chain_cert needs to be reset to NULL after every call to PEM_read_X509 */
			chain_cert = NULL;
		}
	}

	if (DEBUG_ATLEAST(4)) {
		char subj[1024];

		X509_NAME_oneline(X509_get_subject_name(end_cert), subj, 1024);
		ast_debug(4, "Opened end cert '%s' from '%s'\n", subj, filename);

		if (cert_chain && *cert_chain) {
			debug_cert_chain(4, *cert_chain);
		} else {
			ast_debug(4, "No chain certs found in '%s'\n", filename);
		}
	}

	fclose(fp);

	return end_cert;
}

X509 *crypto_load_cert_chain_from_memory(const char *buffer, size_t size,
	STACK_OF(X509) **cert_chain)
{
	RAII_VAR(BIO *, bio, NULL, BIO_free_all);
	X509 *end_cert = NULL;

	if (ast_strlen_zero(buffer) || size <= 0) {
		ast_log(LOG_ERROR, "buffer was null or empty\n");
		return NULL;
	}

	bio = BIO_new_mem_buf(buffer, size);
	if (!bio) {
		crypto_log_openssl(LOG_ERROR, "Unable to create memory BIO\n");
		return NULL;
	}

	end_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	if (!end_cert) {
		crypto_log_openssl(LOG_ERROR, "Failed to create end_cert from BIO\n");
		return NULL;
	}

	/*
	 * If the caller provided a stack, we will read the chain certs
	 * (if any) into it.
	 */
	if (cert_chain) {
		X509 *chain_cert = NULL;

		*cert_chain = sk_X509_new_null();
		while ((chain_cert = PEM_read_bio_X509(bio, &chain_cert, NULL, NULL)) != NULL) {
			if (sk_X509_push(*cert_chain, chain_cert) <= 0) {
				crypto_log_openssl(LOG_ERROR, "Failed to add chain cert from BIO to list\n");
				X509_free(end_cert);
				sk_X509_pop_free(*cert_chain, X509_free);
				return NULL;
			}
			/* chain_cert needs to be reset to NULL after every call to PEM_read_X509 */
			chain_cert = NULL;
		}
	}

	if (DEBUG_ATLEAST(4)) {
		char subj[1024];

		X509_NAME_oneline(X509_get_subject_name(end_cert), subj, 1024);
		ast_debug(4, "Opened end cert '%s' from BIO\n", subj);

		if (cert_chain && *cert_chain) {
			debug_cert_chain(4, *cert_chain);
		} else {
			ast_debug(4, "No chain certs found in BIO\n");
		}
	}

	return end_cert;
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

/*
 * Notes on the crypto_cert_store object:
 *
 * We've discoverd a few issues with the X509_STORE object in OpenSSL
 * that requires us to a bit more work to get the desired behavior.
 *
 * Basically, although X509_STORE_load_locations() and X509_STORE_load_path()
 * work file for trusted certs, they refuse to load either CRLs or
 * untrusted certs from directories, which is needed to support the
 * crl_path and untrusted_cert_path options.  So we have to brute force
 * it a bit.  We now use PEM_read_X509() and PEM_read_X509_CRL() to load
 * the objects from files and then use X509_STORE_add_cert() and
 * X509_STORE_add_crl() to add them to the store.  This is a bit more
 * work but it gets the job done.  To load from directories, we
 * simply use ast_file_read_dirs() with a callback that calls
 * those functions.  This also fixes an issue where certificates
 * loaded using ca_path don't show up when displaying the
 * verification or profile objects from the CLI.
 *
 * NOTE: X509_STORE_load_file() could have been used instead of
 * PEM_read_X509()/PEM_read_X509_CRL() and
 * X509_STORE_add_cert()/X509_STORE_add_crl() but X509_STORE_load_file()
 * didn't appear in OpenSSL until version 1.1.1. :(
 *
 * Another issue we have is that, while X509_verify_cert() can use
 * an X509_STORE of CA certificates directly, it can't use X509_STOREs
 * of untrusted certs or CRLs.  Instead, it needs a stack of X509
 * objects for untrusted certs and a stack of X509_CRL objects for CRLs.
 * So we need to extract the untrusted certs and CRLs from their
 * stores and push them onto the stacks when the configuration is
 * loaded.  We still use the stores as intermediaries because they
 * make it easy to load the certs and CRLs from files and directories
 * and they handle freeing the objects when the store is freed.
 */

static void crypto_cert_store_destructor(void *obj)
{
	struct crypto_cert_store *store = obj;

	if (store->certs) {
		X509_STORE_free(store->certs);
	}
	if (store->untrusted) {
		X509_STORE_free(store->untrusted);
	}
	if (store->untrusted_stack) {
		sk_X509_free(store->untrusted_stack);
	}
	if (store->crls) {
		X509_STORE_free(store->crls);
	}
	if (store->crl_stack) {
		sk_X509_CRL_free(store->crl_stack);
	}
}

struct crypto_cert_store *crypto_create_cert_store(void)
{
	struct crypto_cert_store *store = ao2_alloc(sizeof(*store), crypto_cert_store_destructor);
	if (!store) {
		ast_log(LOG_ERROR, "Failed to create crypto_cert_store\n");
		return NULL;
	}

	store->certs = X509_STORE_new();
	if (!store->certs) {
		crypto_log_openssl(LOG_ERROR, "Failed to create X509_STORE\n");
		ao2_ref(store, -1);
		return NULL;
	}

	store->untrusted = X509_STORE_new();
	if (!store->untrusted) {
		crypto_log_openssl(LOG_ERROR, "Failed to create untrusted X509_STORE\n");
		ao2_ref(store, -1);
		return NULL;
	}
	store->untrusted_stack = sk_X509_new_null();
	if (!store->untrusted_stack) {
		crypto_log_openssl(LOG_ERROR, "Failed to create untrusted stack\n");
		ao2_ref(store, -1);
		return NULL;
	}

	store->crls = X509_STORE_new();
	if (!store->crls) {
		crypto_log_openssl(LOG_ERROR, "Failed to create CRL X509_STORE\n");
		ao2_ref(store, -1);
		return NULL;
	}
	store->crl_stack = sk_X509_CRL_new_null();
	if (!store->crl_stack) {
		crypto_log_openssl(LOG_ERROR, "Failed to create CRL stack\n");
		ao2_ref(store, -1);
		return NULL;
	}

	return store;
}

static int crypto_load_store_from_cert_file(X509_STORE *store, const char *file)
{
	X509 *cert;
	int rc = 0;

	if (ast_strlen_zero(file)) {
		ast_log(LOG_ERROR, "file was null or empty\n");
		return -1;
	}

	cert = crypto_load_cert_chain_from_file(file, NULL);
	if (!cert) {
		return -1;
	}
	rc = X509_STORE_add_cert(store, cert);
	X509_free(cert);
	if (!rc) {
		crypto_log_openssl(LOG_ERROR, "Failed to load store from file '%s'\n", file);
		return -1;
	}

	return 0;
}

static int crypto_load_store_from_crl_file(X509_STORE *store, const char *file)
{
	X509_CRL *crl;
	int rc = 0;

	if (ast_strlen_zero(file)) {
		ast_log(LOG_ERROR, "file was null or empty\n");
		return -1;
	}

	crl = crypto_load_crl_from_file(file);
	if (!crl) {
		return -1;
	}
	rc = X509_STORE_add_crl(store, crl);
	X509_CRL_free(crl);
	if (!rc) {
		crypto_log_openssl(LOG_ERROR, "Failed to load store from file '%s'\n", file);
		return -1;
	}

	return 0;
}

struct pem_file_cb_data {
	X509_STORE *store;
	int is_crl;
};

static int pem_file_cb(const char *dir_name, const char *filename, void *obj)
{
	struct pem_file_cb_data* data = obj;
	char *filename_merged = NULL;
	struct stat statbuf;
	int rc = 0;

	if (ast_asprintf(&filename_merged, "%s/%s", dir_name, filename) < 0) {
		return -1;
	}

	if (lstat(filename_merged, &statbuf)) {
		printf("Error reading path stats - %s: %s\n",
					filename_merged, strerror(errno));
		ast_free(filename_merged);
		return -1;
	}

	/* We only want the symlinks from the directory */
	if (!S_ISLNK(statbuf.st_mode)) {
		ast_free(filename_merged);
		return 0;
	}

	if (data->is_crl) {
		rc = crypto_load_store_from_crl_file(data->store, filename_merged);
	} else {
		rc = crypto_load_store_from_cert_file(data->store, filename_merged);
	}

	ast_free(filename_merged);
	return rc;
}

static int _crypto_load_cert_store(X509_STORE *store, const char *file, const char *path)
{
	int rc = 0;

	if (!ast_strlen_zero(file)) {
		rc = crypto_load_store_from_cert_file(store, file);
		if (rc != 0) {
			return -1;
		}
	}

	if (!ast_strlen_zero(path)) {
		struct pem_file_cb_data data = { .store = store, .is_crl = 0 };
		if (ast_file_read_dirs(path, pem_file_cb, &data, 0)) {
			return -1;
		}
	}

	return 0;
}

static int _crypto_load_crl_store(X509_STORE *store, const char *file, const char *path)
{
	int rc = 0;

	if (!ast_strlen_zero(file)) {
		rc = crypto_load_store_from_crl_file(store, file);
		if (rc != 0) {
			return -1;
		}
	}

	if (!ast_strlen_zero(path)) {
		struct pem_file_cb_data data = { .store = store, .is_crl = 1 };
		if (ast_file_read_dirs(path, pem_file_cb, &data, 0)) {
			return -1;
		}
	}

	return 0;
}

int crypto_load_cert_store(struct crypto_cert_store *store, const char *file,
	const char *path)
{
	if (ast_strlen_zero(file) && ast_strlen_zero(path)) {
		ast_log(LOG_ERROR, "Both file and path can't be NULL\n");
		return -1;
	}

	if (!store || !store->certs) {
		ast_log(LOG_ERROR, "store or store->certs is NULL\n");
		return -1;
	}

	return _crypto_load_cert_store(store->certs, file, path);
}

int crypto_load_untrusted_cert_store(struct crypto_cert_store *store, const char *file,
	const char *path)
{
	int rc = 0;
	STACK_OF(X509_OBJECT) *objs = NULL;
	int count = 0;
	int i = 0;

	if (ast_strlen_zero(file) && ast_strlen_zero(path)) {
		ast_log(LOG_ERROR, "Both file and path can't be NULL\n");
		return -1;
	}

	if (!store || !store->untrusted || !store->untrusted_stack) {
		ast_log(LOG_ERROR, "store wasn't initialized properly\n");
		return -1;
	}

	rc = _crypto_load_cert_store(store->untrusted, file, path);
	if (rc != 0) {
		return rc;
	}

	/*
	 * We need to extract the certs from the store and push them onto the
	 * untrusted stack.  This is because the verification context needs
	 * a stack of untrusted certs and not the store.
	 * The store holds the references to the certs so we can't
	 * free it.
	 */
	objs = X509_STORE_get0_objects(store->untrusted);
	count = sk_X509_OBJECT_num(objs);
	for (i = 0; i < count ; i++) {
		X509_OBJECT *o = sk_X509_OBJECT_value(objs, i);
		if (X509_OBJECT_get_type(o) == X509_LU_X509) {
			X509 *c = X509_OBJECT_get0_X509(o);
			sk_X509_push(store->untrusted_stack, c);
		}
	}

	return 0;
}

int crypto_load_crl_store(struct crypto_cert_store *store, const char *file,
	const char *path)
{
	int rc = 0;
	STACK_OF(X509_OBJECT) *objs = NULL;
	int count = 0;
	int i = 0;

	if (ast_strlen_zero(file) && ast_strlen_zero(path)) {
		ast_log(LOG_ERROR, "Both file and path can't be NULL\n");
		return -1;
	}

	if (!store || !store->untrusted || !store->untrusted_stack) {
		ast_log(LOG_ERROR, "store wasn't initialized properly\n");
		return -1;
	}

	rc = _crypto_load_crl_store(store->crls, file, path);
	if (rc != 0) {
		return rc;
	}

	/*
	 * We need to extract the CRLs from the store and push them onto the
	 * crl stack.  This is because the verification context needs
	 * a stack of CRLs and not the store.
	 * The store holds the references to the CRLs so we can't
	 * free it.
	 */
	objs = X509_STORE_get0_objects(store->crls);
	count = sk_X509_OBJECT_num(objs);
	for (i = 0; i < count ; i++) {
		X509_OBJECT *o = sk_X509_OBJECT_value(objs, i);
		if (X509_OBJECT_get_type(o) == X509_LU_CRL) {
			X509_CRL *c = X509_OBJECT_get0_X509_CRL(o);
			sk_X509_CRL_push(store->crl_stack, c);
		}
	}

	return 0;
}

int crypto_show_cli_store(struct crypto_cert_store *store, int fd)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
	STACK_OF(X509_OBJECT) *objs = NULL;
	int count = 0;
	int untrusted_count = 0;
	int crl_count = 0;
	int i = 0;
	char subj[1024];

	/*
	 * The CA certificates are stored in the certs store.
	 */
	objs = X509_STORE_get0_objects(store->certs);
	count = sk_X509_OBJECT_num(objs);

	for (i = 0; i < count ; i++) {
		X509_OBJECT *o = sk_X509_OBJECT_value(objs, i);
		if (X509_OBJECT_get_type(o) == X509_LU_X509) {
			X509 *c = X509_OBJECT_get0_X509(o);
			X509_NAME_oneline(X509_get_subject_name(c), subj, 1024);
			ast_cli(fd, "Cert: %s\n", subj);
		} else {
			ast_log(LOG_ERROR, "CRLs are not allowed in the CA cert store\n");
		}
	}

	/*
	 * Although the untrusted certs are stored in the untrusted store,
	 * we already have the stack of certificates so we can just
	 * list them directly.
	 */
	untrusted_count = sk_X509_num(store->untrusted_stack);
	for (i = 0; i < untrusted_count ; i++) {
		X509 *c = sk_X509_value(store->untrusted_stack, i);
		X509_NAME_oneline(X509_get_subject_name(c), subj, 1024);
		ast_cli(fd, "Untrusted: %s\n", subj);
	}

	/*
	 * Same for the CRLs.
	 */
	crl_count = sk_X509_CRL_num(store->crl_stack);
	for (i = 0; i < crl_count ; i++) {
		X509_CRL *crl = sk_X509_CRL_value(store->crl_stack, i);
		X509_NAME_oneline(X509_CRL_get_issuer(crl), subj, 1024);
		ast_cli(fd, "CRL: %s\n", subj);
	}

	return count + untrusted_count + crl_count;
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

int crypto_is_cert_trusted(struct crypto_cert_store *store, X509 *cert,
	STACK_OF(X509) *cert_chain, const char **err_msg)
{
	X509_STORE_CTX *verify_ctx = NULL;
	RAII_VAR(STACK_OF(X509) *, untrusted_stack, NULL, sk_X509_free);
	int rc = 0;

	if (!(verify_ctx = X509_STORE_CTX_new())) {
		crypto_log_openssl(LOG_ERROR, "Unable to create verify_ctx\n");
		return 0;
	}

	if (cert_chain && sk_X509_num(cert_chain) > 0) {
		int untrusted_count = store->untrusted_stack ? sk_X509_num(store->untrusted_stack) : 0;
		int i = 0;

		untrusted_stack = sk_X509_dup(cert_chain);
		if (!untrusted_stack) {
			crypto_log_openssl(LOG_ERROR, "Unable to duplicate untrusted stack\n");
			X509_STORE_CTX_free(verify_ctx);
			return 0;
		}
		/*
		 * If store->untrusted_stack was NULL for some reason then
		 * untrusted_count will be 0 so the loop will never run.
		 */
		for (i = 0; i < untrusted_count; i++) {
			X509 *c = sk_X509_value(store->untrusted_stack, i);
			if (sk_X509_push(untrusted_stack, c) <= 0) {
				crypto_log_openssl(LOG_ERROR, "Unable to push untrusted cert onto stack\n");
				sk_X509_free(untrusted_stack);
				X509_STORE_CTX_free(verify_ctx);
				return 0;
			}
		}
	/*
	 * store->untrusted_stack should always be allocated even if empty
	 * but we'll make sure.
	 */
	} else if (store->untrusted_stack){
		/* This is a dead simple shallow clone */
		ast_debug(4, "cert_chain had no certs\n");
		untrusted_stack = sk_X509_dup(store->untrusted_stack);
		if (!untrusted_stack) {
			crypto_log_openssl(LOG_ERROR, "Unable to duplicate untrusted stack\n");
			X509_STORE_CTX_free(verify_ctx);
			return 0;
		}
	}

	if (X509_STORE_CTX_init(verify_ctx, store->certs, cert, untrusted_stack) != 1) {
		X509_STORE_CTX_cleanup(verify_ctx);
		X509_STORE_CTX_free(verify_ctx);
		crypto_log_openssl(LOG_ERROR, "Unable to initialize verify_ctx\n");
		return 0;
	}
	X509_STORE_CTX_set0_crls(verify_ctx, store->crl_stack);

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

