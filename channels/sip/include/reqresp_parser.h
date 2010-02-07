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
 * \brief sip request response parser header file
 */

#ifndef _SIP_REQRESP_H
#define _SIP_REQRESP_H

/*!
 * \brief parses a URI in its components.
 *
 * \note
 * - Multiple scheme's can be specified ',' delimited. ex: "sip:,sips:"
 * - If a component is not requested, do not split around it. This means
 *   that if we don't have domain, we cannot split name:pass and domain:port.
 * - It is safe to call with ret_name, pass, domain, port pointing all to
 *   the same place.
 * - This function overwrites the the uri string.
 * 
 * \retval 0 on success
 * \retval -1 on error.
 *
 * \verbatim
 * general form we are expecting is sip:user:password;user-parameters@host:port;uri-parameters?headers
 * \endverbatim
 */
int parse_uri(char *uri, const char *scheme, char **ret_name, char **pass, char **domain, char **port, char **transport);

/*!
 * \brief  Get caller id name from SIP headers, copy into output buffer
 *
 * \retval input string pointer placed after display-name field if possible
 */
const char *get_calleridname(const char *input, char *output, size_t outputsize);

/*!
 * \brief register request parsing tests
 */
void sip_request_parser_register_tests(void);

/*!
 * \brief unregister request parsing tests
 */
void sip_request_parser_unregister_tests(void);

#endif
