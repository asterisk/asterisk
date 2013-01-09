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
int sip_parse_host(char *line, int lineno, char **hostname, int *portnum, enum sip_transport *transport);

/*!
 * \brief register config parsing tests
 */
void sip_config_parser_register_tests(void);

/*!
 * \brief unregister config parsing tests
 */
void sip_config_parser_unregister_tests(void);

#endif
