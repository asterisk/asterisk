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
 *   that if we don't have domain, we cannot split name:pass.
 * - It is safe to call with ret_name, pass, hostport pointing all to
 *   the same place.
 * - If no secret parameter is provided, ret_name will return with both
 *   parts, user:secret.
 * - If the URI contains a port number, hostport will return with both
 *   parts, host:port.
 * - This function overwrites the the URI string.
 * 
 * \retval 0 on success
 * \retval -1 on error.
 *
 * \verbatim
 * general form we are expecting is sip:user:password;user-parameters@host:port;uri-parameters?headers
 * \endverbatim
 */
int parse_uri(char *uri, const char *scheme, char **ret_name, char **pass,
	      char **hostport, char **transport);

/*!
 * \brief parses a URI in to all of its components and any trailing residue
 *
 * \retval 0 on success
 * \retval -1 on error.
 *
 */
int parse_uri_full(char *uri, const char *scheme, char **user, char **pass,
		   char **hostport, struct uriparams *params, char **headers,
		   char **residue);

/*!
 * \brief  Get caller id name from SIP headers, copy into output buffer
 *
 * \retval input string pointer placed after display-name field if possible
 */
const char *get_calleridname(const char *input, char *output, size_t outputsize);

/*!
 * \brief  Get name and number from sip header
 *
 * \note name and number point to malloced memory on return and must be
 * freed. If name or number is not found, they will be returned as NULL.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int get_name_and_number(const char *hdr, char **name, char **number);

/*! \brief Pick out text in brackets from character string
 * \return pointer to terminated stripped string
 * \param tmp input string that will be modified
 *
 * Examples:
 * \verbatim
 * "foo" <bar>	valid input, returns bar
 *  foo returns the whole string
 * < "foo ... >	returns the string between brackets
 * < "foo...    bogus (missing closing bracket), returns the whole string
 * \endverbatim
 */
char *get_in_brackets(char *tmp);

/*! \brief Get text in brackets on a const without copy
 *
 * \param src String to search
 * \param[out] start Set to first character inside left bracket.
 * \param[out] length Set to lenght of string inside brackets
 * \retval 0 success
 * \retval -1 failure
 * \retval 1 no brackets so got all
 */
int get_in_brackets_const(const char *src,const char **start,int *length);

/*! \brief Get text in brackets and any trailing residue
 *
 * \retval 0 success
 * \retval -1 failure
 * \retval 1 no brackets so got all
 */
int get_in_brackets_full(char *tmp, char **out, char **residue);

/*! \brief Parse the ABNF structure
 * name-andor-addr = name-addr / addr-spec
 * into its components and return any trailing message-header parameters
 *
 * \retval 0 success
 * \retval -1 failure
 */
int parse_name_andor_addr(char *uri, const char *scheme, char **name,
			  char **user, char **pass, char **domain,
			  struct uriparams *params, char **headers,
			  char **remander);

/*! \brief Parse all contact header contacts
 * \retval 0 success
 * \retval -1 failure
 * \retval 1 all contacts (star)
 */

int get_comma(char *parse, char **out);

int parse_contact_header(char *contactheader, struct contactliststruct *contactlist);

/*!
 * \brief register request parsing tests
 */
void sip_request_parser_register_tests(void);

/*!
 * \brief unregister request parsing tests
 */
void sip_request_parser_unregister_tests(void);

/*!
 * \brief Parse supported header in incoming packet
 *
 * \details This function parses through the options parameters and
 * builds a bit field representing all the SIP options in that field. When an
 * item is found that is not supported, it is copied to the unsupported
 * out buffer.
 *
 * \param option list
 * \param unsupported out buffer (optional)
 * \param unsupported out buffer length (optional)
 *
 * \note Because this function can be called multiple times, it will append
 * whatever options are specified in \c options to \c unsupported. Callers
 * of this function should make sure the unsupported buffer is clear before
 * calling this function.
 */
unsigned int parse_sip_options(const char *options, char *unsupported, size_t unsupported_len);

/*!
 * \brief Compare two URIs as described in RFC 3261 Section 19.1.4
 *
 * \param input1 First URI
 * \param input2 Second URI
 * \retval 0 URIs match
 * \retval nonzero URIs do not match or one or both is malformed
 */
int sip_uri_cmp(const char *input1, const char *input2);

/*!
 * \brief initialize request and response parser data
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int sip_reqresp_parser_init(void);

/*!
 * \brief Free resources used by request and response parser
 */
void sip_reqresp_parser_exit(void);

/*!
 * \brief Parse a Via header
 *
 * This function parses the Via header and processes it according to section
 * 18.2 of RFC 3261 and RFC 3581. Since we don't have a transport layer, we
 * only care about the maddr and ttl parms.  The received and rport params are
 * not parsed.
 *
 * \note This function fails to parse some odd combinations of SWS in parameter
 * lists.
 *
 * \code
 * VIA syntax. RFC 3261 section 25.1
 * Via               =  ( "Via" / "v" ) HCOLON via-parm *(COMMA via-parm)
 * via-parm          =  sent-protocol LWS sent-by *( SEMI via-params )
 * via-params        =  via-ttl / via-maddr
 *                   / via-received / via-branch
 *                   / via-extension
 * via-ttl           =  "ttl" EQUAL ttl
 * via-maddr         =  "maddr" EQUAL host
 * via-received      =  "received" EQUAL (IPv4address / IPv6address)
 * via-branch        =  "branch" EQUAL token
 * via-extension     =  generic-param
 * sent-protocol     =  protocol-name SLASH protocol-version
 *                   SLASH transport
 * protocol-name     =  "SIP" / token
 * protocol-version  =  token
 * transport         =  "UDP" / "TCP" / "TLS" / "SCTP"
 *                   / other-transport
 * sent-by           =  host [ COLON port ]
 * ttl               =  1*3DIGIT ; 0 to 255
 * \endcode
 */
struct sip_via *parse_via(const char *header);

/*
 * \brief Free parsed Via data.
 */
void free_via(struct sip_via *v);

#endif
