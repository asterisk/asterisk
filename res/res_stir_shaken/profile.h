/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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
#ifndef _STIR_SHAKEN_PROFILE_H
#define _STIR_SHAKEN_PROFILE_H

#include "profile_private.h"

struct stir_shaken_profile *ast_stir_shaken_get_profile_by_name(const char *name);

/*!
 * \brief Load time initialization for the stir/shaken 'profile' object
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_profile_load(void);

/*!
 * \brief Unload time cleanup for the stir/shaken 'profile'
 *
 * \retval 0 on success, -1 on error
 */
int stir_shaken_profile_unload(void);

#endif /* _STIR_SHAKEN_PROFILE_H */
