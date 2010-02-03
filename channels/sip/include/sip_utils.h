/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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
 * \brief sip utils header file
 */

#ifndef _SIP_UTILS_H
#define _SIP_UTILS_H


/*! \brief converts ascii port to int representation. If no
 *  pt buffer is provided or the pt has errors when being converted
 *  to an int value, the port provided as the standard is used.
 *
 *  \retval positive numeric port 
 */
unsigned int port_str2int(const char *pt, unsigned int standard);

#endif
