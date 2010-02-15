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

/*! 
 * \brief converts ascii port to int representation. 
 *
 * \arg pt[in] string that contains a port.
 * \arg standard[in] port to return in case the port string input is NULL
 *      or if there is a parsing error.
 *
 * \return An integer port representation.
 */
unsigned int port_str2int(const char *pt, unsigned int standard);

/*! \brief Locate closing quote in a string, skipping escaped quotes.
 * optionally with a limit on the search.
 * start must be past the first quote.
 */
const char *find_closing_quote(const char *start, const char *lim);


#endif
