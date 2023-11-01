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

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CRYPTO_H */
