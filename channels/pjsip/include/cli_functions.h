/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Fairview 5 Engineering, LLC.
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

/*!
 * \file
 *
 * \author \verbatim George Joseph <george.joseph@fairview5.com> \endverbatim
 *
 * \brief PJSIP CLI functions header file
 */

#ifndef _PJSIP_CLI_FUNCTIONS
#define _PJSIP_CLI_FUNCTIONS


/*!
 * \brief Registers the channel cli commands
 * \since 13.9.0
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_channel_cli_register(void);

/*!
 * \brief Unregisters the channel cli commands
 * \since 13.9.0
 */
void pjsip_channel_cli_unregister(void);


#endif /* _PJSIP_CLI_FUNCTIONS */
