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
#ifndef _STIR_SHAKEN_GENERAL_H
#define _STIR_SHAKEN_GENERAL_H

struct ast_sorcery;

/*!
 * \brief General configuration for stir/shaken
 */
struct stir_shaken_general;

/*!
 * \brief Retrieve the stir/shaken 'general' configuration object
 *
 * A default configuration object is returned if no configuration was specified.
 * As well, NULL can be returned if there is no configuration, and a problem
 * occurred while loading the defaults.
 *
 * \note Object is returned with a reference that the caller is responsible
 *     for de-referencing.
 *
 * \retval A 'general' configuration object, or NULL
 */
struct stir_shaken_general *stir_shaken_general_get(void);

/*!
 * \brief Retrieve the 'ca_file' general configuration option value
 *
 * \note If a NULL configuration is given, then the default value is returned
 *
 * \param cfg A 'general' configuration object
 *
 * \retval The 'ca_file' value
 */
const char *ast_stir_shaken_ca_file(const struct stir_shaken_general *cfg);

/*!
 * \brief Retrieve the 'ca_path' general configuration option value
 *
 * \note If a NULL configuration is given, then the default value is returned
 *
 * \param cfg A 'general' configuration object
 *
 * \retval The 'ca_path' value
 */
const char *ast_stir_shaken_ca_path(const struct stir_shaken_general *cfg);

/*!
 * \brief Retrieve the 'cache_max_size' general configuration option value
 *
 * \note If a NULL configuration is given, then the default value is returned
 *
 * \param cfg A 'general' configuration object
 *
 * \retval The 'cache_max_size' value
 */
unsigned int ast_stir_shaken_cache_max_size(const struct stir_shaken_general *cfg);

/*!
 * \brief Retrieve the 'curl_timeout' general configuration option value
 *
 * \note If a NULL configuration is given, then the default value is returned
 *
 * \param cfg A 'general' configuration object
 *
 * \retval The 'curl_timeout' value
 */
unsigned int ast_stir_shaken_curl_timeout(const struct stir_shaken_general *cfg);

/*!
 * \brief Retrieve the 'signature_timeout' general configuration option value
 *
 * \note if a NULL configuration is given, then the default value is returned
 *
 * \param cfg A 'general' configuration object
 *
 * \retval The 'signature_timeout' value
 */
unsigned int ast_stir_shaken_signature_timeout(const struct stir_shaken_general *cfg);

/*!
 * \brief Load time initialization for the stir/shaken 'general' configuration
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_general_load(void);

/*!
 * \brief Unload time cleanup for the stir/shaken 'general' configuration
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_general_unload(void);

#endif /* _STIR_SHAKEN_GENERAL_H */
