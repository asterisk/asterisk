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
#ifndef _STIR_SHAKEN_CERTIFICATE_H
#define _STIR_SHAKEN_CERTIFICATE_H

#include <openssl/evp.h>

struct ast_sorcery;

struct stir_shaken_certificate;

struct stir_shaken_certificate *stir_shaken_certificate_get_by_caller_id_number(const char *caller_id_number);

/*!
 * \brief Get the public key URL associated with a certificate
 *
 * \param cert The certificate to get the public key URL from
 *
 * \retval NULL on failure
 * \retval The public key URL on success
 */
const char *stir_shaken_certificate_get_public_key_url(struct stir_shaken_certificate *cert);

/*!
 * \brief Get the private key associated with a certificate
 *
 * \param cert The certificate to get the private key from
 *
 * \retval NULL on failure
 * \retval The private key on success
 */
EVP_PKEY *stir_shaken_certificate_get_private_key(struct stir_shaken_certificate *cert);

/*!
 * \brief Load time initialization for the stir/shaken 'certificate' configuration
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_certificate_load(void);

/*!
 * \brief Unload time cleanup for the stir/shaken 'certificate' configuration
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_certificate_unload(void);

#endif /* _STIR_SHAKEN_CERTIFICATE_H */

