/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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
#ifndef _RES_STIR_SHAKEN_H
#define _RES_STIR_SHAKEN_H

#include <openssl/evp.h>
#include <openssl/pem.h>

/*!
 * \brief Retrieve the stir/shaken sorcery context
 *
 * \retval The stir/shaken sorcery context
 */
struct ast_sorcery *ast_stir_shaken_sorcery(void);

/*!
 * \brief Get the private key associated with a caller id
 *
 * \param caller_id_number The caller id used to look up the private key
 *
 * \retval The private key
 */
EVP_PKEY *ast_stir_shaken_get_private_key(const char *caller_id_number);

#endif /* _RES_STIR_SHAKEN_H */
