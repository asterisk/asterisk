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
#ifndef _STIR_SHAKEN_STORE_H
#define _STIR_SHAKEN_STORE_H

struct ast_sorcery;

/*!
 * \brief Load time initialization for the stir/shaken 'store' configuration
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_store_load(void);

/*!
 * \brief Unload time cleanup for the stir/shaken 'store' configuration
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_store_unload(void);

#endif /* _STIR_SHAKEN_STORE_H */
