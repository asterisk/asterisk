/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Provide cryptographic signature routines
 */

#ifndef _ASTERISK_CRYPTO_H
#define _ASTERISK_CRYPTO_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#ifdef HAVE_OPENSSL
#include "openssl/x509.h"
#include "openssl/x509_vfy.h"
#endif

#include "asterisk/optional_api.h"
#include "asterisk/logger.h"

/* We previously used the key length explicitly; replace with constant.
 * For now, Asterisk is limited to 1024 bit (128 byte) RSA keys.
 */
#define AST_CRYPTO_RSA_KEY_BITS		1024
#define AST_CRYPTO_AES_BLOCKSIZE	128

struct aes_key {
	unsigned char raw[AST_CRYPTO_AES_BLOCKSIZE / 8];
};

typedef struct aes_key ast_aes_encrypt_key;
typedef struct aes_key ast_aes_decrypt_key;

#define AST_KEY_PUBLIC	(1 << 0)
#define AST_KEY_PRIVATE	(1 << 1)

/*!
 * \brief Retrieve a key
 * \param kname Name of the key we are retrieving
 * \param ktype Intger type of key (AST_KEY_PUBLIC or AST_KEY_PRIVATE)
 *
 * \retval the key on success.
 * \retval NULL on failure.
 */
AST_OPTIONAL_API(struct ast_key *, ast_key_get, (const char *kname, int ktype), { return NULL; });

/*!
 * \brief Check the authenticity of a message signature using a given public key
 * \param key a public key to use to verify
 * \param msg the message that has been signed
 * \param sig the proposed valid signature in mime64-like encoding
 *
 * \retval 0 if the signature is valid.
 * \retval -1 otherwise.
 *
 */
AST_OPTIONAL_API(int, ast_check_signature, (struct ast_key *key, const char *msg, const char *sig), { return -1; });

/*!
 * \brief Check the authenticity of a message signature using a given public key
 * \param key a public key to use to verify
 * \param msg the message that has been signed
 * \param msglen
 * \param dsig the proposed valid signature in raw binary representation
 *
 * \retval 0 if the signature is valid.
 * \retval -1 otherwise.
 *
 */
AST_OPTIONAL_API(int, ast_check_signature_bin, (struct ast_key *key, const char *msg, int msglen, const unsigned char *dsig), { return -1; });

/*!
 * \brief Sign a message signature using a given private key
 * \param key a private key to use to create the signature
 * \param msg the message to sign
 * \param sig a pointer to a buffer of at least 256 bytes in which the
 * mime64-like encoded signature will be stored
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 *
 */
AST_OPTIONAL_API(int, ast_sign, (struct ast_key *key, char *msg, char *sig), { return -1; });

/*!
 * \brief Sign a message signature using a given private key
 * \param key a private key to use to create the signature
 * \param msg the message to sign
 * \param msglen
 * \param dsig a pointer to a buffer of at least 128 bytes in which the
 * raw encoded signature will be stored
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 *
 */
AST_OPTIONAL_API(int, ast_sign_bin, (struct ast_key *key, const char *msg, int msglen, unsigned char *dsig), { return -1; });

/*!
 * \brief Encrypt a message using a given private key
 * \param dst a pointer to a buffer of at least srclen * 1.5 bytes in which the encrypted
 * \param src the message to encrypt
 * \param srclen the length of the message to encrypt
 * \param key a private key to use to encrypt
 * answer will be stored
 *
 * \retval length of encrypted data on success.
 * \retval -1 on failure.
 *
 */
AST_OPTIONAL_API(int, ast_encrypt_bin, (unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key), { return -1; });

/*!
 * \brief Decrypt a message using a given private key
 * \param dst a pointer to a buffer of at least srclen bytes in which the decrypted
 * \param src the message to decrypt
 * \param srclen the length of the message to decrypt
 * \param key a private key to use to decrypt
 * answer will be stored
 *
 * \retval length of decrypted data on success.
 * \retval -1 on failure.
 *
 */
AST_OPTIONAL_API(int, ast_decrypt_bin, (unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key), { return -1; });

/*!
 * \brief Set an encryption key
 * \param key a 16 char key
 * \param ctx address of an aes encryption context
 *
 * \retval 0 success
 * \retval nonzero failure
 */
AST_OPTIONAL_API(int, ast_aes_set_encrypt_key,
	(const unsigned char *key, ast_aes_encrypt_key *ctx),
	{ ast_log(LOG_WARNING, "AES encryption disabled. Install OpenSSL.\n"); return -1; });

/*!
 * \brief Set a decryption key
 * \param key a 16 char key
 * \param ctx address of an aes encryption context
 *
 * \retval 0 success
 * \retval nonzero failure
 */
AST_OPTIONAL_API(int, ast_aes_set_decrypt_key,
	(const unsigned char *key, ast_aes_decrypt_key *ctx),
	{ ast_log(LOG_WARNING, "AES encryption disabled. Install OpenSSL.\n"); return -1; });

/*!
 * \brief AES encrypt data
 * \param in data to be encrypted
 * \param out pointer to a buffer to hold the encrypted output
 * \param key pointer to the ast_aes_encrypt_key to use for encryption
 * \retval <= 0 failure
 * \retval otherwise number of bytes in output buffer
 */
AST_OPTIONAL_API(int, ast_aes_encrypt,
	(const unsigned char *in, unsigned char *out, const ast_aes_encrypt_key *key),
	{ ast_log(LOG_WARNING, "AES encryption disabled. Install OpenSSL.\n");return -1; });

/*!
 * \brief AES decrypt data
 * \param in encrypted data
 * \param out pointer to a buffer to hold the decrypted output
 * \param key pointer to the ast_aes_decrypt_key to use for decryption
 * \retval <= 0 failure
 * \retval otherwise number of bytes in output buffer
 */
AST_OPTIONAL_API(int, ast_aes_decrypt,
	(const unsigned char *in, unsigned char *out, const ast_aes_decrypt_key *key),
	{ ast_log(LOG_WARNING, "AES encryption disabled. Install OpenSSL.\n");return -1; });

AST_OPTIONAL_API(int, ast_crypto_loaded, (void), { return 0; });

AST_OPTIONAL_API(int, ast_crypto_reload, (void), { return 0; });

#ifdef HAVE_OPENSSL
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
 * \param nid The NID of the extension
 *            (0 to search by short_name)
 * \param short_name The short name of the extension
 * \note Either nid or short_name may be supplied.  If both are,
 * nid takes precedence.
 * \returns The definition of the extension or NULL if not found
 * \warning Do not attempt to free or otherwise manipulate the
 * structure returned or its members.
 */
struct ast_X509_Extension *ast_crypto_get_registered_extension(
	int nid, const char *short_name);

/*!
 * \brief Check if an extension is already locally registered
 * \param nid The NID of the extension
 *            (0 to search by short_name)
 * \param short_name The short name of the extension
 * \note Either nid or short_name may be supplied.  If both are,
 * nid takes precedence.
 * \retval 0 The extension has not been registered
 * \retval 1 The extension has been registered
 */
int ast_crypto_is_extension_registered(int nid, const char *short_name);

/*!
 * \brief Register a certificate extension to openssl
 * \param oid  The OID of the extension
 * \param short_name The short name of the extension
 * \param long_name The long name of the extension
 * \retval <0 Extension was not successfully added
 * \retval >= NID of the added extension
 */
int ast_crypto_register_x509_extension(const char *oid,
	const char *short_name, const char *long_name);

/*!
 * \brief Return the data from a specific extension in a cert
 * \param cert The cert containing the extension
 * \param nid The NID of the extension
 *            (0 to search locally registered extensions by short_name)
 * \param short_name The short name of the extension
 *                   (only for locally registered extensions)
 * \note Either nid or short_name may be supplied.  If both are,
 * nid takes precedence.
 * \note The extension nid may be any of the built-in values
 * in openssl/obj_mac.h or a NID returned by
 * ast_crypto_register_x509_extension().
 * \returns The data for the extension or NULL if not found
 * \warning Do NOT attempt to free the returned buffer.
 */
ASN1_OCTET_STRING *ast_crypto_get_cert_extension_data(X509 *cert, int nid,
	const char *short_name);

/*!
 * \brief Load an X509 Cert from a file
 * \param filename PEM file
 * \returns X509* or NULL on error
 */
X509 *ast_crypto_load_cert_from_file(const char *filename);

/*!
 * \brief Load an X509 Cert from a NULL terminated buffer
 * \param buffer containing the cert
 * \param size size of the buffer.
 *             May be -1 if the buffer is NULL terminated.
 * \returns X509* or NULL on error
 */
X509 *ast_crypto_load_cert_from_memory(const char *buffer, size_t size);

/*!
 * \brief Retrieve RAW public key from cert
 * \param cert The cert containing the extension
 * \param raw_key Address of char * to place the raw key
 * \note raw_key must be freed by the caller after use
 * \retval <=0 An error has occurred
 * \retval >0 Length of raw key
 */
int ast_crypto_get_raw_pubkey_from_cert(X509 *cert,
	unsigned char **raw_key);

/*!
 * \brief Create an empty X509 store
 * \returns X509_STORE* or NULL on error
 */
X509_STORE *ast_crypto_create_cert_store(void);

/*!
 * \brief Load an X509 Store with either certificates or CRLs
 * \param store X509 Store to load
 * \param file Certificate or CRL file to load or NULL
 * \param path Path to directory with hashed certs or CRLs to load or NULL
 * \note At least 1 file or path must be specified.
 * \retval <= 0 failure
 * \retval 0 success
 */
int ast_crypto_load_cert_store(X509_STORE *store, const char *file,
	const char *path);

/*!
 * \brief Check if the reftime is within the cert's valid dates
 * \param cert The cert to check
 * \param reftime to use or 0 to use current time
 * \retval 1 Cert is valid
 * \retval 0 Cert is not valid
 */
int ast_crypto_is_cert_time_valid(X509 *cert, time_t reftime);

/*!
 * \brief Check if the cert is trusted
 * \param store The CA store to check against
 * \param cert The cert to check
 * \retval 1 Cert is trusted
 * \retval 0 Cert is not trusted
 */
int ast_crypto_is_cert_trusted(X509_STORE *store, X509 *cert);

/*!
 * \brief Return a time_t for an ASN1_TIME
 * \param at ASN1_TIME
 * \returns time_t corresponding to the ASN1_TIME
 */
time_t ast_crypto_asn_time_as_time_t(ASN1_TIME *at);

#endif /* HAVE_OPENSSL */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CRYPTO_H */
