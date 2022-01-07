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
#ifndef _STIR_SHAKEN_H
#define _STIR_SHAKEN_H

#include <openssl/evp.h>

/*!
 * \brief Output configuration settings to the Asterisk CLI
 *
 * \param obj A sorcery object containing configuration data
 * \param arg Asterisk CLI argument object
 * \param flags ao2 container flags
 *
 * \retval 0
 */
int stir_shaken_cli_show(void *obj, void *arg, int flags);

/*!
 * \brief Tab completion for name matching with STIR/SHAKEN CLI commands
 *
 * \param word The word to tab complete on
 * \param container The sorcery container to iterate through
 *
 * \retval The tab completion options
 */
char *stir_shaken_tab_complete_name(const char *word, struct ao2_container *container);

/*!
 * \brief Reads the public (or private) key from the specified path
 *
 * \param path The path to the file containing the private key
 * \param priv Specify 0 for public, 1 for private
 *
 * \retval NULL on failure
 * \retval The public/private key on success
 */
EVP_PKEY *stir_shaken_read_key(const char *path, int priv);

/*!
 * \brief Gets the serial number in hex form from the buffer (for X509)
 *
 * \note The returned string will need to be freed by the caller
 *
 * \param buf The BASE64 encoded buffer
 * \param buf_size The size of the data in buf
 *
 * \retval NULL on failure
 * \retval serial number on success
 */
char *stir_shaken_get_serial_number_x509(const char *buf, size_t buf_size);

#endif /* _STIR_SHAKEN_H */
