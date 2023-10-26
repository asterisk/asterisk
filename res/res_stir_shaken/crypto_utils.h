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
#ifndef _CRYPTO_UTILS_H
#define _CRYPTO_UTILS_H

#include "openssl/x509.h"
#include "openssl/x509_vfy.h"

#include "asterisk.h"
#include "asterisk/logger.h"
#include "asterisk/stringfields.h"

struct ast_X509_Extension {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(oid);
		AST_STRING_FIELD(short_name);
		AST_STRING_FIELD(long_name);
	);
	int nid;
};

/*!
 * \brief Print a log message with any OpenSSL errors appended
 *
 * \param level	Type of log event
 * \param file	Will be provided by the AST_LOG_* macro
 * \param line	Will be provided by the AST_LOG_* macro
 * \param function	Will be provided by the AST_LOG_* macro
 * \param fmt	This is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
 */
void ast_log_openssl(int level, char *file, int line,
	const char *function, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

/*!
 * \brief Return a locally registered X509 extension definition
 *
 * \param nid The NID of the extension
 *            (0 to search by short_name)
 * \param short_name The short name of the extension
 *
 * \note Either nid or short_name may be supplied.  If both are,
 * nid takes precedence.
 *
 * \returns The definition of the extension or NULL if not found
 * \warning Do not attempt to free or otherwise manipulate the
 * structure returned or its members.
 */
struct ast_X509_Extension *ast_crypto_get_registered_extension(
	int nid, const char *short_name);

/*!
 * \brief Check if an extension is already locally registered
 *
 * \param nid The NID of the extension
 *            (0 to search by short_name)
 * \param short_name The short name of the extension
 *
 * \note Either nid or short_name may be supplied.  If both are,
 * nid takes precedence.
 *
 * \retval 0 The extension has not been registered
 * \retval 1 The extension has been registered
 */
int ast_crypto_is_extension_registered(int nid, const char *short_name);

/*!
 * \brief Register a certificate extension to openssl
 *
 * \param oid  The OID of the extension
 * \param short_name The short name of the extension
 * \param long_name The long name of the extension
 *
 * \retval <0 Extension was not successfully added
 * \retval >= NID of the added extension
 */
int ast_crypto_register_x509_extension(const char *oid,
	const char *short_name, const char *long_name);

/*!
 * \brief Return the data from a specific extension in a cert
 *
 * \param cert The cert containing the extension
 * \param nid The NID of the extension
 *            (0 to search locally registered extensions by short_name)
 * \param short_name The short name of the extension
 *                   (only for locally registered extensions)
 *
 * \note Either nid or short_name may be supplied.  If both are,
 * nid takes precedence.
 * \note The extension nid may be any of the built-in values
 * in openssl/obj_mac.h or a NID returned by
 * ast_crypto_register_x509_extension().
 *
 * \returns The data for the extension or NULL if not found
 *
 * \warning Do NOT attempt to free the returned buffer.
 */
ASN1_OCTET_STRING *ast_crypto_get_cert_extension_data(X509 *cert, int nid,
	const char *short_name);

/*!
 * \brief Load an X509 Cert from a file
 *
 * \param filename PEM file
 *
 * \returns X509* or NULL on error
 */
X509 *ast_crypto_load_cert_from_file(const char *filename);

/*!
 * \brief Load a private key from memory
 *
 * \param buffer private key
 * \param size buffer size
 *
 * \returns EVP_PKEY* or NULL on error
 */
EVP_PKEY *ast_crypto_load_private_key_from_memory(const char *buffer, size_t size);

/*!
 * \brief Check if the supplied buffer has a private key
 *
 * \note This function can be used to check a certificate PEM file to
 * see if it also has a private key in it.
 *
 * \param buffer arbitrary buffer
 * \param size buffer size
 *
 * \retval 1 buffer has a private key
 * \retval 0 buffer does not have a private key
 */
int ast_crypto_has_private_key_from_memory(const char *buffer, size_t size);

/*!
 * \brief Load an X509 Cert from a NULL terminated buffer
 *
 * \param buffer containing the cert
 * \param size size of the buffer.
 *             May be -1 if the buffer is NULL terminated.
 *
 * \returns X509* or NULL on error
 */
X509 *ast_crypto_load_cert_from_memory(const char *buffer, size_t size);

/*!
 * \brief Retrieve RAW public key from cert
 *
 * \param cert The cert containing the extension
 * \param raw_key Address of char * to place the raw key.
 *                Must be freed with ast_free after use
 *
 * \retval <=0 An error has occurred
 * \retval >0 Length of raw key
 */
int ast_crypto_get_raw_pubkey_from_cert(X509 *cert,
	unsigned char **raw_key);

/*!
 * \brief Extract raw public key from EVP_PKEY
 *
 * \param key Key to extract from
 *
 * \param buffer Pointer to unsigned char * to receive raw key
 *               Must be freed with ast_free after use
 *
 * \retval <=0 An error has occurred
 * \retval >0 Length of raw key
 */
int ast_crypto_extract_raw_pubkey(EVP_PKEY *key, unsigned char **buffer);

/*!
 * \brief Extract raw private key from EVP_PKEY
 *
 * \param key Key to extract from
 * \param buffer Pointer to unsigned char * to receive raw key
 *               Must be freed with ast_free after use
 *
 * \retval <=0 An error has occurred
 * \retval >0 Length of raw key
 */
int ast_crypto_extract_raw_privkey(EVP_PKEY *key, unsigned char **buffer);

/*!
 * \brief Load a private key from a file
 *
 * \param filename File to load from
 *
 * \returns EVP_PKEY *key or NULL on error
 */
EVP_PKEY *ast_crypto_load_privkey_from_file(const char *filename);

/*!
 * \brief Create an empty X509 store
 *
 * \returns X509_STORE* or NULL on error
 */
X509_STORE *ast_crypto_create_cert_store(void);

/*!
 * \brief Load an X509 Store with either certificates or CRLs
 *
 * \param store X509 Store to load
 * \param file Certificate or CRL file to load or NULL
 * \param path Path to directory with hashed certs or CRLs to load or NULL
 *
 * \note At least 1 file or path must be specified.
 *
 * \retval <= 0 failure
 * \retval 0 success
 */
int ast_crypto_load_cert_store(X509_STORE *store, const char *file,
	const char *path);

/*!
 * \brief Check if the reftime is within the cert's valid dates
 *
 * \param cert The cert to check
 * \param reftime to use or 0 to use current time
 *
 * \retval 1 Cert is valid
 * \retval 0 Cert is not valid
 */
int ast_crypto_is_cert_time_valid(X509 *cert, time_t reftime);

/*!
 * \brief Check if the cert is trusted
 *
 * \param store The CA store to check against
 * \param cert The cert to check
 *
 * \retval 1 Cert is trusted
 * \retval 0 Cert is not trusted
 */
int ast_crypto_is_cert_trusted(X509_STORE *store, X509 *cert);

/*!
 * \brief Return a time_t for an ASN1_TIME
 *
 * \param at ASN1_TIME
 *
 * \returns time_t corresponding to the ASN1_TIME
 */
time_t ast_crypto_asn_time_as_time_t(ASN1_TIME *at);

int ss_crypto_load(void);
int ss_crypto_unload(void);

#endif /* CRYPTO_UTILS */
