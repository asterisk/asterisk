/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Ashley Sanders <asanders@digium.com>
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
 *
 * \brief Definitions for the default DNS resolver for Asterisk.
 *
 * \author Ashley Sanders <asanders@digium.com>
 */

/*!
 * \brief Initializes the resolver.
 *
 * \retval  0 on success.
 * \retval -1 on failure.
 */
int ast_dns_system_resolver_init(void);
