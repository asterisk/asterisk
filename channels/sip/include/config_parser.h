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
 * \brief sip.conf parser header file
 */

#include "sip.h"

#ifndef _SIP_CONF_PARSE_H
#define _SIP_CONF_PARSE_H

/*!
 * \brief Parse register=> line in sip.conf
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int sip_parse_register_line(struct sip_registry *reg, int default_expiry, const char *value, int lineno);

/*!
 * \brief parses a config line for a host with a transport
 *
 * An example input would be: 
 *     <code>tls://www.google.com:8056</code>
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int sip_parse_host(char *line, int lineno, char **hostname, int *portnum, enum ast_transport *transport);

/*! \brief Parse the comma-separated nat= option values
 * \param value The comma-separated value
 * \param mask An array of ast_flags that will be set by this function
 *             and used as a mask for copying the flags later
 * \param flags An array of ast_flags that will be set by this function
 *
 * \note The nat-related values in both mask and flags are assumed to empty. This function
 * will treat the first "yes" or "no" value in a list of values as overiding all other values
 * and will stop parsing. Auto values will override their non-auto counterparts.
 */
void sip_parse_nat_option(const char *value, struct ast_flags *mask, struct ast_flags *flags);

/*!
 * \brief register config parsing tests
 */
void sip_config_parser_register_tests(void);

#endif
