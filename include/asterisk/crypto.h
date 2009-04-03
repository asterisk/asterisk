/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

#define AST_KEY_PUBLIC	(1 << 0)
#define AST_KEY_PRIVATE	(1 << 1)

struct ast_key;

/*! 
 * \brief Retrieve a key
 * \param name of the key we are retrieving
 * \param int type of key (AST_KEY_PUBLIC or AST_KEY_PRIVATE)
 *
 * \retval the key on success.
 * \retval NULL on failure.
 */
extern struct ast_key *(*ast_key_get)(const char *key, int type);

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
extern int (*ast_check_signature)(struct ast_key *key, const char *msg, const char *sig);

/*! 
 * \brief Check the authenticity of a message signature using a given public key
 * \param key a public key to use to verify
 * \param msg the message that has been signed
 * \param sig the proposed valid signature in raw binary representation
 *
 * \retval 0 if the signature is valid.
 * \retval -1 otherwise.
 *
 */
extern int (*ast_check_signature_bin)(struct ast_key *key, const char *msg, int msglen, const unsigned char *sig);

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
extern int (*ast_sign)(struct ast_key *key, char *msg, char *sig);

/*!
 * \brief Sign a message signature using a given private key
 * \param key a private key to use to create the signature
 * \param msg the message to sign
 * \param sig a pointer to a buffer of at least 128 bytes in which the
 * raw encoded signature will be stored
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 *
 */
extern int (*ast_sign_bin)(struct ast_key *key, const char *msg, int msglen, unsigned char *sig);

/*!
 * \brief Encrypt a message using a given private key
 * \param key a private key to use to encrypt
 * \param src the message to encrypt
 * \param srclen the length of the message to encrypt
 * \param dst a pointer to a buffer of at least srclen * 1.5 bytes in which the encrypted
 * answer will be stored
 *
 * \retval length of encrypted data on success.
 * \retval -1 on failure.
 *
 */
extern int (*ast_encrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key);

/*!
 * \brief Decrypt a message using a given private key
 * \param key a private key to use to decrypt
 * \param src the message to decrypt
 * \param srclen the length of the message to decrypt
 * \param dst a pointer to a buffer of at least srclen bytes in which the decrypted
 * answer will be stored
 *
 * \retval length of dencrypted data on success.
 * \retval -1 on failure.
 *
 */
extern int (*ast_decrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key);
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CRYPTO_H */
