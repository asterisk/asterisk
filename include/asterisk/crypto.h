/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Provide cryptographic signature routines
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_CRYPTO_H
#define _ASTERISK_CRYPTO_H

#include <asterisk/channel.h>
#include <asterisk/file.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_KEY_PUBLIC	(1 << 0)
#define AST_KEY_PRIVATE	(1 << 1)

struct ast_key;

//! Retrieve a key
/*! 
 * \param name of the key we are retrieving
 * \param int type of key (AST_KEY_PUBLIC or AST_KEY_PRIVATE)
 *
 * Returns the key on success or NULL on failure
 */
extern struct ast_key *ast_key_get(char *key, int type);

//! Initialize keys (that is, retrieve pass codes for all private keys)
/*!
 * \param fd a file descriptor for I/O for passwords
 *
 */
extern int ast_key_init(int fd);

//! Check the authenticity of a message signature using a given public key
/*!
 * \param key a public key to use to verify
 * \param msg the message that has been signed
 * \param sig the proposed valid signature in mime64-like encoding
 *
 * Returns 0 if the signature is valid, or -1 otherwise
 *
 */
extern int ast_check_signature(struct ast_key *key, char *msg, char *sig);

//! Check the authenticity of a message signature using a given public key
/*!
 * \param key a public key to use to verify
 * \param msg the message that has been signed
 * \param sig the proposed valid signature in raw binary representation
 *
 * Returns 0 if the signature is valid, or -1 otherwise
 *
 */
extern int ast_check_signature_bin(struct ast_key *key, char *msg, unsigned char *sig);

/*!
 * \param key a private key to use to create the signature
 * \param msg the message to sign
 * \param sig a pointer to a buffer of at least 256 bytes in which the
 * mime64-like encoded signature will be stored
 *
 * Returns 0 on success or -1 on failure.
 *
 */
extern int ast_sign(struct ast_key *key, char *msg, char *sig);
/*!
 * \param key a private key to use to create the signature
 * \param msg the message to sign
 * \param sig a pointer to a buffer of at least 128 bytes in which the
 * raw encoded signature will be stored
 *
 * Returns 0 on success or -1 on failure.
 *
 */
extern int ast_sign_bin(struct ast_key *key, char *msg, unsigned char *sig);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
