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

/*! \file
 *
 * \brief Internal stir/shaken utilities
 */

#include "asterisk.h"

#include <openssl/evp.h>
#include <openssl/pem.h>

#include "asterisk/cli.h"
#include "asterisk/sorcery.h"

#include "stir_shaken.h"
#include "asterisk/res_stir_shaken.h"

int stir_shaken_cli_show(void *obj, void *arg, int flags)
{
	struct ast_cli_args *a = arg;
	struct ast_variable *options;
	struct ast_variable *i;

	if (!obj) {
		ast_cli(a->fd, "No stir/shaken configuration found\n");
		return 0;
	}

	options = ast_variable_list_sort(ast_sorcery_objectset_create2(
		ast_stir_shaken_sorcery(), obj, AST_HANDLER_ONLY_STRING));
	if (!options) {
		return 0;
	}

	ast_cli(a->fd, "%s: %s\n", ast_sorcery_object_get_type(obj),
		ast_sorcery_object_get_id(obj));

	for (i = options; i; i = i->next) {
		ast_cli(a->fd, "\t%s: %s\n", i->name, i->value);
	}

	ast_cli(a->fd, "\n");

	ast_variables_destroy(options);

	return 0;
}

char *stir_shaken_tab_complete_name(const char *word, struct ao2_container *container)
{
	void *obj;
	struct ao2_iterator it;
	int wordlen = strlen(word);
	int ret;

	it = ao2_iterator_init(container, 0);
	while ((obj = ao2_iterator_next(&it))) {
		if (!strncasecmp(word, ast_sorcery_object_get_id(obj), wordlen)) {
			ret = ast_cli_completion_add(ast_strdup(ast_sorcery_object_get_id(obj)));
			if (ret) {
				ao2_ref(obj, -1);
				break;
			}
		}
		ao2_ref(obj, -1);
	}
	ao2_iterator_destroy(&it);

	return NULL;
}

EVP_PKEY *stir_shaken_read_key(const char *path, int priv)
{
	EVP_PKEY *key = NULL;
	FILE *fp;
	X509 *cert = NULL;

	fp = fopen(path, "r");
	if (!fp) {
		ast_log(LOG_ERROR, "Failed to read %s key file '%s'\n", priv ? "private" : "public", path);
		return NULL;
	}

	/* If this is to get the private key, the file will be ECDSA or RSA, with the former eventually
	 * replacing the latter. For the public key, the file will be X.509.
	 */
	if (priv) {
		key = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
	} else {
		cert = PEM_read_X509(fp, NULL, NULL, NULL);
		if (!cert) {
			ast_log(LOG_ERROR, "Failed to read X.509 cert from file '%s'\n", path);
			fclose(fp);
			return NULL;
		}
		key = X509_get_pubkey(cert);
		/* It's fine to free the cert after we get the key because they are 2
		 * independent objects; you don't need a X509 object to be in memory
		 * in order to have an EVP_PKEY, and it doesn't rely on it being there.
		 */
		X509_free(cert);
	}

	if (!key) {
		ast_log(LOG_ERROR, "Failed to read %s key from file '%s'\n", priv ? "private" : "public", path);
		fclose(fp);
		return NULL;
	}

	if (EVP_PKEY_id(key) != EVP_PKEY_EC && EVP_PKEY_id(key) != EVP_PKEY_RSA) {
		ast_log(LOG_ERROR, "%s key from '%s' must be of type EVP_PKEY_EC or EVP_PKEY_RSA\n",
			priv ? "Private" : "Public", path);
		fclose(fp);
		EVP_PKEY_free(key);
		return NULL;
	}

	fclose(fp);

	return key;
}

char *stir_shaken_get_serial_number_x509(const char *buf, size_t buf_size)
{
	BIO *certBIO;
	X509 *cert;
	ASN1_INTEGER *serial;
	BIGNUM *bignum;
	char *serial_hex;
	char *ret;

	certBIO = BIO_new(BIO_s_mem());
	BIO_write(certBIO, buf, buf_size);
	cert = PEM_read_bio_X509(certBIO, NULL, NULL, NULL);
	BIO_free(certBIO);
	if (!cert) {
		ast_log(LOG_ERROR, "Failed to read X.509 cert from buffer\n");
		return NULL;
	}

	serial = X509_get_serialNumber(cert);
	if (!serial) {
		ast_log(LOG_ERROR, "Failed to get serial number from certificate\n");
		X509_free(cert);
		return NULL;
	}

	bignum = ASN1_INTEGER_to_BN(serial, NULL);
	if (bignum == NULL) {
		ast_log(LOG_ERROR, "Failed to convert serial to bignum for certificate\n");
		X509_free(cert);
		return NULL;
	}

	/* This will return a string with memory allocated. After we get the string,
	 * we don't need the cert, file, or bignum references anymore, so free them
	 * and return the string, if BN_bn2hex was a success.
	 */
	serial_hex = BN_bn2hex(bignum);
	X509_free(cert);
	BN_free(bignum);

	if (!serial_hex) {
		ast_log(LOG_ERROR, "Failed to convert bignum to hex for certificate\n");
		return NULL;
	}

	ret = ast_strdup(serial_hex);
	OPENSSL_free(serial_hex);
	if (!ret) {
		ast_log(LOG_ERROR, "Failed to dup serial from openssl for certificate\n");
		return NULL;
	}

	return ret;
}
