/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 20075, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * Wrappers for AES encryption/decryption
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 *
 * These wrappers provided a generic interface to either the
 * AES methods provided by OpenSSL's crypto library, or the
 * AES implementation included with Asterisk.
 */

#ifndef _ASTERISK_AES_H
#define _ASTERISK_AES_H

#ifdef HAVE_CRYPTO

/* Use the OpenSSL crypto library */
#include "openssl/aes.h"

typedef AES_KEY ast_aes_encrypt_key;
typedef AES_KEY ast_aes_decrypt_key;

#define ast_aes_encrypt_key(key, context) AES_set_encrypt_key(key, 128, context)

#define ast_aes_decrypt_key(key, context) AES_set_decrypt_key(key, 128, context)

#define ast_aes_encrypt(in, out, context) AES_encrypt(in, out, context)

#define ast_aes_decrypt(in, out, context) AES_decrypt(in, out, context)

#else /* !HAVE_CRYPTO */

/* Use the included AES implementation */

#define AES_128
#include "aes_internal.h"

typedef aes_encrypt_ctx ast_aes_encrypt_key;
typedef aes_decrypt_ctx ast_aes_decrypt_key;

#define ast_aes_encrypt_key(key, context) aes_encrypt_key128(key, context)

#define ast_aes_decrypt_key(key, context) aes_decrypt_key128(key, context)

#define ast_aes_encrypt(in, out, context) aes_encrypt(in, out, context)

#define ast_aes_decrypt(in, out, context) aes_decrypt(in, out, context)

#endif /* !HAVE_CRYPTO */

#endif /* _ASTERISK_AES_H */
