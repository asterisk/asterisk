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

/*!
 * \brief Print a log message with any OpenSSL errors appended
 *
 * \param level	Type of log event
 * \param file	Will be provided by the AST_LOG_* macro
 * \param line	Will be provided by the AST_LOG_* macro
 * \param function	Will be provided by the AST_LOG_* macro
 * \param fmt	This is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
 */
void crypto_log_openssl(int level, char *file, int line,
	const char *function, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

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
int crypto_register_x509_extension(const char *oid,
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
ASN1_OCTET_STRING *crypto_get_cert_extension_data(X509 *cert, int nid,
	const char *short_name);

/*!
 * \brief Load an X509 Cert and any chained certs from a file
 *
 * \param filename PEM file
 * \param chain_stack The address of a STACK_OF(X509) pointer to receive the
 * chain of certificates if any.
 *
 * \note The caller is responsible for freeing the cert_chain stack and
 * any certs in it.
 *
 * \returns X509* or NULL on error
 */
X509 *crypto_load_cert_chain_from_file(const char *filename,
	STACK_OF(X509) **chain_stack);

/*!
 * \brief Load an X509 CRL from a PEM file
 *
 * \param filename PEM file
 *
 * \returns X509_CRL* or NULL on error
 */
X509_CRL *crypto_load_crl_from_file(const char *filename);

/*!
 * \brief Load a private key from memory
 *
 * \param buffer private key
 * \param size buffer size
 *
 * \returns EVP_PKEY* or NULL on error
 */
EVP_PKEY *crypto_load_private_key_from_memory(const char *buffer, size_t size);

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
int crypto_has_private_key_from_memory(const char *buffer, size_t size);

/*!
 * \brief Load an X509 Cert and any chained certs from a NULL terminated buffer
 *
 * \param buffer containing the cert
 * \param size size of the buffer.
 *             May be -1 if the buffer is NULL terminated.
 * \param chain_stack The address of a STACK_OF(X509) pointer to receive the
 * chain of certificates if any.
 *
 * \note The caller is responsible for freeing the cert_chain stack and
 * any certs in it.
 *
 * \returns X509* or NULL on error
 */
X509 *crypto_load_cert_chain_from_memory(const char *buffer, size_t size,
	STACK_OF(X509) **cert_chain);

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
int crypto_get_raw_pubkey_from_cert(X509 *cert,
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
int crypto_extract_raw_pubkey(EVP_PKEY *key, unsigned char **buffer);

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
int crypto_extract_raw_privkey(EVP_PKEY *key, unsigned char **buffer);

/*!
 * \brief Load a private key from a file
 *
 * \param filename File to load from
 *
 * \returns EVP_PKEY *key or NULL on error
 */
EVP_PKEY *crypto_load_privkey_from_file(const char *filename);

/*!
 * \brief ao2 object wrapper for X509_STORE that provides locking and refcounting
 */
struct crypto_cert_store {
	X509_STORE *certs;
	X509_STORE *crls;
	/*!< The verification context needs a stack of CRLs, not the store */
	STACK_OF(X509_CRL) *crl_stack;
	X509_STORE *untrusted;
	/*!< The verification context needs a stack of untrusted certs, not the store */
	STACK_OF(X509) *untrusted_stack;
};

/*!
 * \brief Free an X509 store
 *
 * \param store X509 Store to free
 *
 */
#define crypto_free_cert_store(store) ao2_cleanup(store)

/*!
 * \brief Create an empty X509 store
 *
 * \returns crypto_cert_store * or NULL on error
 */
struct crypto_cert_store *crypto_create_cert_store(void);

/*!
 * \brief Dump a cert store to the asterisk CLI
 *
 * \param store X509 Store to dump
 * \param fd The CLI fd to print to

 * \retval Count of objects printed
 */
int crypto_show_cli_store(struct crypto_cert_store *store, int fd);

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
int crypto_load_cert_store(struct crypto_cert_store *store, const char *file,
	const char *path);

/*!
 * \brief Load an X509 Store with certificate revocation lists
 *
 * \param store X509 Store to load
 * \param file CRL file to load or NULL
 * \param path Path to directory with hashed CRLs to load or NULL
 *
 * \note At least 1 file or path must be specified.
 *
 * \retval <= 0 failure
 * \retval 0 success
 */
int crypto_load_crl_store(struct crypto_cert_store *store, const char *file,
	const char *path);

/*!
 * \brief Load an X509 Store with untrusted certificates
 *
 * \param store X509 Store to load
 * \param file Certificate file to load or NULL
 * \param path Path to directory with hashed certs to load or NULL
 *
 * \note At least 1 file or path must be specified.
 *
 * \retval <= 0 failure
 * \retval 0 success
 */
int crypto_load_untrusted_cert_store(struct crypto_cert_store *store, const char *file,
	const char *path);

/*!
 * \brief Locks an X509 Store
 *
 * \param store X509 Store to lock
 *
 * \retval <= 0 failure
 * \retval 0 success
 */
#define crypto_lock_cert_store(store) ao2_lock(store)

/*!
 * \brief Unlocks an X509 Store
 *
 * \param store X509 Store to unlock
 *
 * \retval <= 0 failure
 * \retval 0 success
 */
#define crypto_unlock_cert_store(store) ao2_unlock(store)

/*!
 * \brief Check if the reftime is within the cert's valid dates
 *
 * \param cert The cert to check
 * \param reftime to use or 0 to use current time
 *
 * \retval 1 Cert is valid
 * \retval 0 Cert is not valid
 */
int crypto_is_cert_time_valid(X509 *cert, time_t reftime);

/*!
 * \brief Check if the cert is trusted
 *
 * \param store The CA store to check against
 * \param cert The cert to check
 * \param cert_chain An untrusted certificate chain that may have accompanied the cert.
 * \param err_msg Optional pointer to a const char *
 *
 * \retval 1 Cert is trusted
 * \retval 0 Cert is not trusted
 */
int crypto_is_cert_trusted(struct crypto_cert_store *store, X509 *cert,
	STACK_OF(X509) *cert_chain, const char **err_msg);

/*!
 * \brief Return a time_t for an ASN1_TIME
 *
 * \param at ASN1_TIME
 *
 * \returns time_t corresponding to the ASN1_TIME
 */
time_t crypto_asn_time_as_time_t(ASN1_TIME *at);


/*!
 * \brief Returns the Subject (or component of Subject) from a certificate
 *
 * \param cert  The X509 certificate
 * \param short_name The upper case short name of the component to extract.
 *                   May be NULL to extract the entire subject.
 * \returns Entire subject or component.  Must be freed with ast_free();
 */
char *crypto_get_cert_subject(X509 *cert, const char *short_name);

/*!
 * \brief Initialize the crypto utils
 */
int crypto_load(void);

/*!
 * \brief Clean up the crypto utils
 */
int crypto_unload(void);

#endif /* CRYPTO_UTILS */
