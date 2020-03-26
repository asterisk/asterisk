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

struct ast_stir_shaken_payload;

struct ast_json;

/*!
 * \brief Retrieve the stir/shaken sorcery context
 *
 * \retval The stir/shaken sorcery context
 */
struct ast_sorcery *ast_stir_shaken_sorcery(void);

/*!
 * \brief Free a STIR/SHAKEN payload
 */
void ast_stir_shaken_payload_free(struct ast_stir_shaken_payload *payload);

/*!
 * \brief Sign a JSON STIR/SHAKEN payload
 *
 * \note This function will automatically add the "attest", "iat", and "origid" fields.
 */
struct ast_stir_shaken_payload *ast_stir_shaken_sign(struct ast_json *json);

#endif /* _RES_STIR_SHAKEN_H */
