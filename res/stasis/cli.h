/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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

#ifndef _ASTERISK_RES_STASIS_CLI_H
#define _ASTERISK_RES_STASIS_CLI_H

/*! \file
 *
 * \brief Internal API for Stasis application CLI commands
 *
 * \author Matt Jordan <mjordan@digium.com>
 * \since 13.13.0
 */

/*!
 * \brief Initialize the CLI commands
 *
 * \retval 0 on success
 * \retval non-zero on error
 */
int cli_init(void);

/*!
 * \brief Cleanup the CLI commands
 */
void cli_cleanup(void);

#endif /* _ASTERISK_RES_STASIS_CLI_H */
